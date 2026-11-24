#include "Luma/Scene/Prefab.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Components.h"
#include "Luma/Scene/Hierarchy.h"
#include "Luma/Scene/SceneSerializer.h"
#include "Luma/Serialization/Json.h"

namespace Luma {
namespace {

constexpr int kPrefabVersion = 1;

bool Equal(const SerialValue* a, const SerialValue* b) {
    if (!a || !b) return a == b;
    return WriteJson(*a, false) == WriteJson(*b, false);
}

// `value` without the member `key` (SerialValue has no erase).
SerialValue Without(const SerialValue& value, std::string_view key) {
    SerialValue out = SerialValue::MakeObject();
    for (const auto& [k, v] : value.Members()) {
        if (k != key) out[k] = v;
    }
    return out;
}

Math::Vec3 ReadVec3(const SerialValue* v) {
    if (!v || !v->IsArray() || v->Size() < 3) return {0.0f, 0.0f, 0.0f};
    return {static_cast<f32>(v->At(0).AsFloat()), static_cast<f32>(v->At(1).AsFloat()),
            static_cast<f32>(v->At(2).AsFloat())};
}

SerialValue Vec3Value(const Math::Vec3& p) {
    SerialValue a = SerialValue::MakeArray();
    a.PushBack(static_cast<f64>(p.x));
    a.PushBack(static_cast<f64>(p.y));
    a.PushBack(static_cast<f64>(p.z));
    return a;
}

// Shifts the Transform position in a component set by `delta`.
void ShiftPosition(SerialValue& components, const Math::Vec3& delta) {
    if (!components.Contains("Transform")) return;
    SerialValue& t = components["Transform"];
    if (!t.IsObject()) return;
    const Math::Vec3 p = ReadVec3(t.Find("position"));
    t["position"] = Vec3Value({p.x + delta.x, p.y + delta.y, p.z + delta.z});
}

Math::Vec3 RootPosition(const Scene& scene, Entity root) {
    const auto* t = scene.Registry().try_get<TransformComponent>(root);
    return t ? t->position : Math::Vec3{0.0f, 0.0f, 0.0f};
}

// An entity's components as a prefab object stores them: no link component,
// Transform position relative to the instance root (the root's own = origin).
SerialValue ObjectPayload(const Scene& scene, Entity e, const Math::Vec3& rootPosition, SerialValue* owners) {
    SerialValue ownerMap = SerialValue::MakeObject();
    SerialValue comps = Without(SceneSerializer::WriteComponents(scene, e, ownerMap), kPrefabLinkComponentId);
    if (owners) *owners = Without(ownerMap, kPrefabLinkComponentId);
    ShiftPosition(comps, {-rootPosition.x, -rootPosition.y, -rootPosition.z});
    return comps;
}

// Writes `payload` (prefab-space) to `e`'s component `id`, adding it first if needed.
void WriteComponent(Scene& scene, Entity e, const std::string& id, SerialValue payload, const Math::Vec3& rootPosition) {
    const ComponentDesc* desc = Components().Find(id);
    if (!desc) return;  // a component of a plugin that isn't loaded: left as it is
    if (id == "Transform") {
        SerialValue wrapper = SerialValue::MakeObject();
        wrapper["Transform"] = std::move(payload);
        ShiftPosition(wrapper, rootPosition);
        payload = wrapper["Transform"];
    }
    if (!desc->has(scene, e)) desc->add(scene, e);
    desc->read(scene, e, payload);
}

std::vector<Entity> Subtree(const Scene& scene, Entity root) {
    std::vector<Entity> out{root};
    for (Entity d : DescendantsOf(scene, root)) out.push_back(d);
    return out;
}

// Linked entities of the instance rooted at `root`, by prefab object id.
std::map<u64, Entity> InstanceObjects(const Scene& scene, Entity root) {
    std::map<u64, Entity> out;
    const auto* rootLink = scene.Registry().try_get<PrefabLinkComponent>(root);
    if (!rootLink) return out;
    for (Entity e : Subtree(scene, root)) {
        const auto* link = scene.Registry().try_get<PrefabLinkComponent>(e);
        if (link && link->prefab == rootLink->prefab && (e == root || !link->root)) out[link->objectId] = e;
    }
    return out;
}

std::string NameOf(const SerialValue& components) {
    if (const SerialValue* n = components.Find("Name")) {
        if (const SerialValue* s = n->Find("name"); s && s->IsString()) return s->AsString();
    }
    return "Object";
}

void Link(Scene& scene, Entity e, const AssetId& id, const std::string& path, const PrefabObject& object, bool root) {
    PrefabLinkComponent link;
    link.prefab = id;
    link.prefabPath = path;
    link.objectId = object.id;
    link.root = root;
    link.baseline = object.components;
    scene.Registry().emplace_or_replace<PrefabLinkComponent>(e, std::move(link));
}

// Hands out object ids for one capture: each linked id at most once (a
// duplicated linked entity carries its source's id and becomes a new object),
// fresh ids from `next` on.
struct IdAllocator {
    u64 next = 1;
    std::set<u64> used;
    u64 Take(std::optional<u64> linked) {
        if (linked && *linked != 0 && used.insert(*linked).second) return *linked;
        while (used.count(next)) ++next;
        used.insert(next);
        return next++;
    }
};

// The object id `e` keeps when its instance (prefab `id`) is captured.
std::optional<u64> LinkedObjectId(const Scene& scene, Entity e, Entity root, const AssetId& id) {
    const auto* link = scene.Registry().try_get<PrefabLinkComponent>(e);
    if (!link || link->prefab != id || (e != root && link->root)) return std::nullopt;
    return link->objectId;
}

std::vector<u64> ObjectIds(const PrefabAsset& prefab) {
    std::vector<u64> ids;
    for (const PrefabObject& o : prefab.objects) ids.push_back(o.id);
    return ids;
}

// One instance against `prefab`. `revert` treats every value as not
// overridden and brings deleted objects back.
bool SyncInstance(Scene& scene, Entity root, const AssetId& id, const PrefabAsset& prefab, bool revert) {
    auto& reg = scene.Registry();
    const std::string path = reg.get<PrefabLinkComponent>(root).prefabPath;
    const std::vector<u64> known = reg.get<PrefabLinkComponent>(root).knownObjects;
    const Math::Vec3 rootPos = RootPosition(scene, root);
    std::map<u64, Entity> objects = InstanceObjects(scene, root);
    bool changed = false;

    for (const PrefabObject& object : prefab.objects) {
        auto found = objects.find(object.id);
        if (found == objects.end()) {
            const bool deletedByUser = std::find(known.begin(), known.end(), object.id) != known.end();
            if (deletedByUser && !revert) continue;
            // New in the prefab (or restored by Revert): create it under its parent.
            const Entity e = scene.CreateEntity(NameOf(object.components));
            SerialValue comps = object.components;
            ShiftPosition(comps, rootPos);
            SceneSerializer::ReadComponents(scene, e, comps, &object.owners);
            const auto parent = objects.find(object.parent);
            SetParent(scene, e, parent != objects.end() ? parent->second : root);
            Link(scene, e, id, path, object, false);
            objects[object.id] = e;
            changed = true;
            continue;
        }
        const Entity e = found->second;
        const bool isRoot = e == root;
        const SerialValue current = ObjectPayload(scene, e, rootPos, nullptr);
        const SerialValue baseline = reg.get<PrefabLinkComponent>(e).baseline;
        for (const auto& [component, incoming] : object.components.Members()) {
            if (isRoot && component == "Transform") continue;  // the instance's placement
            const SerialValue* cur = current.Find(component);
            const SerialValue* base = baseline.Find(component);
            if (!cur) {
                if (base && !revert) continue;  // removed by the user: stays removed
                WriteComponent(scene, e, component, incoming, rootPos);
                changed = true;
                continue;
            }
            SerialValue merged = *cur;
            bool componentChanged = false;
            for (const auto& [field, value] : incoming.Members()) {
                const SerialValue* curField = cur->Find(field);
                const SerialValue* baseField = base ? base->Find(field) : nullptr;
                const bool overridden = !revert && baseField && !Equal(curField, baseField);
                if (overridden || Equal(curField, &value)) continue;
                merged[field] = value;
                componentChanged = true;
            }
            if (componentChanged) {
                WriteComponent(scene, e, component, std::move(merged), rootPos);
                changed = true;
            }
        }
        // Components the prefab dropped (unmodified ones go), and with Revert
        // the ones the user added.
        for (const auto& [component, payload] : current.Members()) {
            if (object.components.Contains(component)) continue;
            if (isRoot && component == "Transform") continue;
            const SerialValue* base = baseline.Find(component);
            const bool fromPrefab = base != nullptr;
            const bool remove = revert ? true : (fromPrefab && Equal(&payload, base));
            if (!remove) continue;
            if (const ComponentDesc* desc = Components().Find(component); desc && desc->userAddable) {
                desc->remove(scene, e);
                changed = true;
            }
        }
        reg.get<PrefabLinkComponent>(e).baseline = object.components;
    }
    // Objects the prefab no longer has.
    for (const auto& [objectId, e] : objects) {
        if (e == root || prefab.Find(objectId) || !scene.IsValid(e)) continue;
        DestroySubtree(scene, e);
        changed = true;
    }
    reg.get<PrefabLinkComponent>(root).knownObjects = ObjectIds(prefab);
    return changed;
}

}  // namespace

const PrefabObject* PrefabAsset::Find(u64 id) const {
    for (const PrefabObject& o : objects) {
        if (o.id == id) return &o;
    }
    return nullptr;
}

// --- Files ---------------------------------------------------------------------------

std::string SavePrefabToString(const PrefabAsset& prefab, bool pretty) {
    SerialValue root = SerialValue::MakeObject();
    root["version"] = kPrefabVersion;
    root["type"] = std::string("Prefab");
    root["name"] = prefab.name;
    SerialValue objects = SerialValue::MakeArray();
    for (const PrefabObject& o : prefab.objects) {
        SerialValue obj = SerialValue::MakeObject();
        obj["id"] = static_cast<i64>(o.id);
        obj["parent"] = static_cast<i64>(o.parent);
        obj["components"] = o.components;
        if (o.owners.IsObject() && !o.owners.Members().empty()) obj["owners"] = o.owners;
        objects.PushBack(std::move(obj));
    }
    root["objects"] = std::move(objects);
    root["nextObject"] = static_cast<i64>(NextPrefabObjectId(prefab));
    return WriteJson(root, pretty);
}

std::optional<PrefabAsset> LoadPrefabFromString(std::string_view json, std::string* error) {
    const auto parsed = ParseJson(json, error);
    if (!parsed || !parsed->IsObject()) {
        if (error && error->empty()) *error = "not a JSON object";
        return std::nullopt;
    }
    if (const SerialValue* type = parsed->Find("type"); !type || type->AsString() != "Prefab") {
        if (error) *error = "not a prefab file";
        return std::nullopt;
    }
    if (const SerialValue* v = parsed->Find("version"); v && v->AsInt() > kPrefabVersion) {
        if (error) *error = "made by a newer version of Luma (prefab version " + std::to_string(v->AsInt()) + ")";
        return std::nullopt;
    }
    const SerialValue* objects = parsed->Find("objects");
    if (!objects || !objects->IsArray() || objects->Size() == 0) {
        if (error) *error = "the prefab has no objects";
        return std::nullopt;
    }
    PrefabAsset prefab;
    if (const SerialValue* n = parsed->Find("name")) prefab.name = n->AsString();
    if (const SerialValue* n = parsed->Find("nextObject"); n && n->AsInt() > 0) {
        prefab.nextObjectId = static_cast<u64>(n->AsInt());
    }
    for (const SerialValue& o : objects->Elements()) {
        PrefabObject object;
        object.id = static_cast<u64>(o.Find("id") ? o.Find("id")->AsInt() : 0);
        object.parent = static_cast<u64>(o.Find("parent") ? o.Find("parent")->AsInt() : 0);
        if (const SerialValue* c = o.Find("components"); c && c->IsObject()) object.components = *c;
        else object.components = SerialValue::MakeObject();
        if (const SerialValue* ow = o.Find("owners"); ow && ow->IsObject()) object.owners = *ow;
        else object.owners = SerialValue::MakeObject();
        if (object.id == 0) {
            if (error) *error = "an object has no id";
            return std::nullopt;
        }
        prefab.objects.push_back(std::move(object));
    }
    return prefab;
}

bool SavePrefab(const PrefabAsset& prefab, const std::filesystem::path& file, std::string* error) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out || !(out << SavePrefabToString(prefab))) {
        if (error) *error = "could not write " + file.generic_string();
        return false;
    }
    return true;
}

std::optional<PrefabAsset> LoadPrefab(const std::filesystem::path& file, std::string* error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (error) *error = "could not open " + file.generic_string();
        return std::nullopt;
    }
    std::stringstream text;
    text << in.rdbuf();
    return LoadPrefabFromString(text.str(), error);
}

u64 NextPrefabObjectId(const PrefabAsset& prefab) {
    u64 next = std::max<u64>(prefab.nextObjectId, 1);
    for (const PrefabObject& o : prefab.objects) next = std::max(next, o.id + 1);
    return next;
}

// --- Authoring ---------------------------------------------------------------------

PrefabAsset CapturePrefab(const Scene& scene, Entity root, std::string name, const PrefabAsset* previous) {
    PrefabAsset prefab;
    prefab.name = std::move(name);
    const Math::Vec3 rootPos = RootPosition(scene, root);
    const std::vector<Entity> subtree = Subtree(scene, root);
    // An instance root keeps its objects' ids; fresh ids start past every id
    // the prefab is known to have used.
    const auto* rootLink = scene.Registry().try_get<PrefabLinkComponent>(root);
    const bool relink = rootLink && rootLink->root;
    IdAllocator alloc;
    if (previous) alloc.next = NextPrefabObjectId(*previous);
    if (relink) {
        for (u64 known : rootLink->knownObjects) alloc.next = std::max(alloc.next, known + 1);
        for (Entity e : subtree) {
            if (const auto linked = LinkedObjectId(scene, e, root, rootLink->prefab)) {
                alloc.next = std::max(alloc.next, *linked + 1);
            }
        }
    }
    std::map<Entity, u64> ids;
    for (Entity e : subtree) {
        PrefabObject object;
        object.id = alloc.Take(relink ? LinkedObjectId(scene, e, root, rootLink->prefab) : std::nullopt);
        ids[e] = object.id;
        if (e != root) {
            const auto parent = ids.find(ParentOf(scene, e));
            object.parent = parent != ids.end() ? parent->second : ids[root];
        }
        object.components = ObjectPayload(scene, e, rootPos, &object.owners);
        prefab.objects.push_back(std::move(object));
    }
    prefab.nextObjectId = alloc.next;
    return prefab;
}

void LinkPrefabInstance(Scene& scene, Entity root, const PrefabAsset& prefab, const AssetId& id,
                        const std::string& path) {
    const std::vector<Entity> entities = Subtree(scene, root);
    for (usize i = 0; i < entities.size() && i < prefab.objects.size(); ++i) {
        Link(scene, entities[i], id, path, prefab.objects[i], i == 0);
    }
    scene.Registry().get<PrefabLinkComponent>(root).knownObjects = ObjectIds(prefab);
}

Entity InstantiatePrefab(Scene& scene, const PrefabAsset& prefab, const AssetId& id, const std::string& path,
                         const Math::Vec3& position) {
    if (prefab.objects.empty()) return kNullEntity;
    std::map<u64, Entity> created;
    Entity root = kNullEntity;
    for (const PrefabObject& object : prefab.objects) {
        const Entity e = scene.CreateEntity(NameOf(object.components));
        SerialValue comps = object.components;
        ShiftPosition(comps, position);
        SceneSerializer::ReadComponents(scene, e, comps, &object.owners);
        if (root == kNullEntity) {
            root = e;
        } else {
            const auto parent = created.find(object.parent);
            SetParent(scene, e, parent != created.end() ? parent->second : root);
        }
        created[object.id] = e;
        Link(scene, e, id, path, object, e == root);
    }
    scene.Registry().get<PrefabLinkComponent>(root).knownObjects = ObjectIds(prefab);
    return root;
}

// --- Instances -------------------------------------------------------------------

usize SyncPrefabInstances(Scene& scene, const AssetId& id, const PrefabAsset& prefab) {
    std::vector<Entity> roots;
    for (auto [e, link] : scene.Registry().view<PrefabLinkComponent>().each()) {
        if (link.root && link.prefab == id) roots.push_back(e);
    }
    usize changed = 0;
    for (Entity root : roots) {
        if (scene.IsValid(root) && SyncInstance(scene, root, id, prefab, false)) ++changed;
    }
    return changed;
}

Entity PrefabInstanceRoot(const Scene& scene, Entity entity) {
    for (Entity e = entity; e != kNullEntity && scene.IsValid(e); e = ParentOf(scene, e)) {
        const auto* link = scene.Registry().try_get<PrefabLinkComponent>(e);
        if (!link) return kNullEntity;
        if (link->root) return e;
    }
    return kNullEntity;
}

std::vector<AssetId> PrefabsInScene(const Scene& scene) {
    std::vector<AssetId> out;
    for (auto [e, link] : scene.Registry().view<const PrefabLinkComponent>().each()) {
        if (link.root && std::find(out.begin(), out.end(), link.prefab) == out.end()) out.push_back(link.prefab);
    }
    return out;
}

std::vector<PrefabOverride> PrefabOverrides(const Scene& scene, Entity entity) {
    std::vector<PrefabOverride> out;
    const auto* link = scene.Registry().try_get<PrefabLinkComponent>(entity);
    const Entity root = PrefabInstanceRoot(scene, entity);
    if (!link || root == kNullEntity) return out;
    const SerialValue current = ObjectPayload(scene, entity, RootPosition(scene, root), nullptr);
    for (const auto& [component, base] : link->baseline.Members()) {
        if (link->root && component == "Transform") continue;
        const SerialValue* cur = current.Find(component);
        if (!cur) {
            out.push_back({component, {}, PrefabOverride::Kind::RemovedComponent});
            continue;
        }
        for (const auto& [field, value] : base.Members()) {
            if (!Equal(cur->Find(field), &value)) out.push_back({component, field, PrefabOverride::Kind::Field});
        }
    }
    for (const auto& [component, payload] : current.Members()) {
        if (!link->baseline.Contains(component)) {
            out.push_back({component, {}, PrefabOverride::Kind::AddedComponent});
        }
    }
    return out;
}

bool PrefabInstanceModified(const Scene& scene, Entity root) {
    const auto* rootLink = scene.Registry().try_get<PrefabLinkComponent>(root);
    if (!rootLink) return false;
    const std::map<u64, Entity> objects = InstanceObjects(scene, root);
    for (const auto& [id, e] : objects) {
        if (!PrefabOverrides(scene, e).empty()) return true;
    }
    for (u64 id : rootLink->knownObjects) {
        if (!objects.count(id)) return true;  // an object the user deleted
    }
    for (Entity e : DescendantsOf(scene, root)) {
        if (!scene.Registry().all_of<PrefabLinkComponent>(e)) return true;  // an object the user added
    }
    return false;
}

void ApplyPrefabInstance(Scene& scene, Entity root, PrefabAsset& prefab) {
    const auto* rootLink = scene.Registry().try_get<PrefabLinkComponent>(root);
    if (!rootLink) return;
    const AssetId id = rootLink->prefab;
    const std::string path = rootLink->prefabPath;
    const Math::Vec3 rootPos = RootPosition(scene, root);
    const PrefabObject* oldRoot = prefab.Find(rootLink->objectId);
    const SerialValue rootTransform =
        oldRoot && oldRoot->components.Find("Transform") ? *oldRoot->components.Find("Transform") : SerialValue{};

    IdAllocator alloc;
    alloc.next = NextPrefabObjectId(prefab);
    for (u64 known : rootLink->knownObjects) alloc.next = std::max(alloc.next, known + 1);
    PrefabAsset updated;
    updated.name = prefab.name;
    std::map<Entity, u64> ids;
    for (Entity e : Subtree(scene, root)) {
        auto* link = scene.Registry().try_get<PrefabLinkComponent>(e);
        if (link && link->prefab != id && link->root && e != root) continue;  // a nested instance of another prefab
        PrefabObject object;
        const std::optional<u64> linkedId = LinkedObjectId(scene, e, root, id);
        object.id = alloc.Take(linkedId);
        // A new id (an added object, or a duplicate of a linked one) joins the prefab.
        const bool linked = linkedId && *linkedId == object.id;
        if (e != root) {
            const auto parent = ids.find(ParentOf(scene, e));
            object.parent = parent != ids.end() ? parent->second : ids[root];
        }
        object.components = ObjectPayload(scene, e, rootPos, &object.owners);
        if (e == root && rootTransform.IsObject()) object.components["Transform"] = rootTransform;
        ids[e] = object.id;
        if (!linked) Link(scene, e, id, path, object, false);  // the user's new object joins the prefab
        updated.objects.push_back(std::move(object));
    }
    updated.nextObjectId = alloc.next;
    prefab = std::move(updated);
    SyncInstance(scene, root, id, prefab, false);  // baselines = the new prefab
}

void RevertPrefabInstance(Scene& scene, Entity root, const PrefabAsset& prefab) {
    const auto* link = scene.Registry().try_get<PrefabLinkComponent>(root);
    if (!link || !link->root) return;
    SyncInstance(scene, root, link->prefab, prefab, true);
}

void UnpackPrefabInstance(Scene& scene, Entity root) {
    const auto* rootLink = scene.Registry().try_get<PrefabLinkComponent>(root);
    if (!rootLink) return;
    const AssetId id = rootLink->prefab;
    for (Entity e : Subtree(scene, root)) {
        const auto* link = scene.Registry().try_get<PrefabLinkComponent>(e);
        if (link && link->prefab == id) scene.Registry().remove<PrefabLinkComponent>(e);
    }
}

void DetachDuplicatedPrefabObjects(Scene& scene, Entity copy) {
    auto& reg = scene.Registry();
    const auto* link = reg.try_get<PrefabLinkComponent>(copy);
    if (!link || link->root) return;
    const AssetId id = link->prefab;
    for (Entity e : Subtree(scene, copy)) {
        const auto* l = reg.try_get<PrefabLinkComponent>(e);
        if (l && l->prefab == id && !l->root) reg.remove<PrefabLinkComponent>(e);
    }
}

usize RetargetPrefabLinks(Scene& scene, std::string_view oldPath, const AssetId& newId, const std::string& newPath) {
    usize changed = 0;
    for (auto [e, link] : scene.Registry().view<PrefabLinkComponent>().each()) {
        if (link.prefabPath != oldPath) continue;
        link.prefab = newId;
        link.prefabPath = newPath;
        ++changed;
    }
    return changed;
}

// --- Registration --------------------------------------------------------------------

RegistrationToken RegisterPrefabLinkComponent(ComponentRegistry& registry) {
    ComponentDesc d;
    d.id = kPrefabLinkComponentId;
    d.displayName = "Prefab Link";
    d.category = "Prefab";
    d.tooltip = "Links this object to the prefab it is an instance of.";
    d.owner = "Engine";
    d.userAddable = false;
    d.add = [](Scene& s, Entity e) { (void)s.Registry().get_or_emplace<PrefabLinkComponent>(e); };
    d.remove = [](Scene& s, Entity e) { s.Registry().remove<PrefabLinkComponent>(e); };
    d.has = [](const Scene& s, Entity e) { return s.Registry().all_of<PrefabLinkComponent>(e); };
    d.write = [](const Scene& s, Entity e, SerialValue& out) {
        const auto& link = s.Registry().get<PrefabLinkComponent>(e);
        out = SerialValue::MakeObject();
        out["prefab"] = ToString(link.prefab);
        out["path"] = link.prefabPath;
        out["object"] = static_cast<i64>(link.objectId);
        out["root"] = link.root;
        out["baseline"] = link.baseline;
        if (link.root) {
            SerialValue known = SerialValue::MakeArray();
            for (u64 id : link.knownObjects) known.PushBack(static_cast<i64>(id));
            out["known"] = std::move(known);
        }
    };
    d.read = [](Scene& s, Entity e, const SerialValue& in) {
        auto& link = s.Registry().get_or_emplace<PrefabLinkComponent>(e);
        if (const SerialValue* v = in.Find("prefab")) link.prefab = FromString(v->AsString());
        if (const SerialValue* v = in.Find("path")) link.prefabPath = v->AsString();
        if (const SerialValue* v = in.Find("object")) link.objectId = static_cast<u64>(v->AsInt());
        if (const SerialValue* v = in.Find("root")) link.root = v->AsBool();
        if (const SerialValue* v = in.Find("baseline"); v && v->IsObject()) link.baseline = *v;
        link.knownObjects.clear();
        if (const SerialValue* v = in.Find("known"); v && v->IsArray()) {
            for (const SerialValue& id : v->Elements()) link.knownObjects.push_back(static_cast<u64>(id.AsInt()));
        }
    };
    d.reset = [](Scene&, Entity) {};
    return registry.Register(std::move(d));
}

}  // namespace Luma
