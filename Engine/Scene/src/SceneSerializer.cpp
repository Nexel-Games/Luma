#include "Luma/Scene/SceneSerializer.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <span>

#include "Luma/Scene/ComponentReflection.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Hierarchy.h"
#include "Luma/Serialization/Json.h"
#include "Luma/Serialization/Reflection.h"

namespace Luma {

namespace {

// Version-1 Mesh Renderers carried inline material values; report them so the
// caller can migrate them onto material assets.
void ReadLegacyMaterial(entt::entity e, const SerialValue& comps, LegacyMaterialList* out) {
    if (!out) return;
    const SerialValue* mesh = comps.Find("MeshRenderer");
    if (!mesh) return;
    const SerialValue* albedo = mesh->Find("albedo");
    const SerialValue* metallic = mesh->Find("metallic");
    const SerialValue* roughness = mesh->Find("roughness");
    if (!albedo && !metallic && !roughness) return;
    LegacyMaterialValues values;
    if (albedo) SerialTraits<Math::Vec3>::Read(*albedo, values.albedo);
    if (metallic) values.metallic = static_cast<f32>(metallic->AsFloat(values.metallic));
    if (roughness) values.roughness = static_cast<f32>(roughness->AsFloat(values.roughness));
    out->emplace_back(e, values);
}

// Upgrades a parsed document from `fromVersion` to SceneSerializer::kVersion in
// place, one step at a time.
//   1 -> 2: Mesh Renderer inline material values became material slots. The
//           values are reported to the caller (ReadLegacyMaterial), which migrates
//           them onto material assets; the document itself is unchanged.
//   2 -> 3: added "componentOwners" (the plugin that provides each non-engine
//           component). A version-2 file has no plugin components, so no data
//           changes.
void MigrateDocument(SerialValue& root, i64 fromVersion) {
    for (i64 v = fromVersion; v < SceneSerializer::kVersion; ++v) {
        switch (v) {
            case 1:  // 1 -> 2: reported while reading entities (legacy materials)
            case 2:  // 2 -> 3: no data change
            default:
                break;
        }
    }
    root["version"] = static_cast<i64>(SceneSerializer::kVersion);
}

// Writes one entity's components: in the order of the file it was loaded from
// (when it carries preserved payloads), then any remaining registered components
// in registration order, then any remaining preserved payloads.
SerialValue WriteComponents(const Scene& scene, Entity e, const ComponentRegistry& registry,
                            SerialValue& owners) {
    SerialValue comps = SerialValue::MakeObject();
    const auto* unresolved = scene.Registry().try_get<UnresolvedComponents>(e);

    auto writeRegistered = [&](const ComponentDesc& desc) {
        SerialValue payload;
        desc.write(scene, e, payload);
        comps[desc.id] = std::move(payload);
        if (desc.owner != "Engine") owners[desc.id] = desc.owner;
    };
    auto writePreserved = [&](const UnresolvedComponent& p) {
        comps[p.id] = p.payload;
        if (!p.owner.empty()) owners[p.id] = p.owner;
    };

    if (unresolved) {
        for (const std::string& key : unresolved->keyOrder) {
            if (comps.Contains(key)) continue;
            if (const ComponentDesc* desc = registry.Find(key); desc && desc->has(scene, e)) {
                writeRegistered(*desc);
            } else if (const UnresolvedComponent* p = unresolved->Find(key)) {
                writePreserved(*p);
            }
        }
    }
    for (const ComponentDesc* desc : registry.All()) {
        if (!comps.Contains(desc->id) && desc->has(scene, e)) writeRegistered(*desc);
    }
    if (unresolved) {
        for (const UnresolvedComponent& p : unresolved->payloads) {
            if (!comps.Contains(p.id)) writePreserved(p);
        }
    }
    return comps;
}

}  // namespace

std::string SceneSerializer::SaveToString(const Scene& scene, bool pretty) {
    const ComponentRegistry& registry = Components();
    SerialValue root = SerialValue::MakeObject();
    root["version"] = kVersion;

    SerialValue owners = SerialValue::MakeObject();
    SerialValue entities = SerialValue::MakeArray();
    // NameComponent marks the canonical scene objects. Walk its storage in packed
    // order: loading emplaces Names in file order, so a load -> save round trip
    // keeps the entity order (EnTT views iterate the packed array backwards).
    std::vector<Entity> order;
    if (const auto* names = scene.Registry().storage<NameComponent>()) {
        const Entity* packed = names->data();
        order.assign(packed, packed + names->size());
    }
    for (usize index = 0; index < order.size(); ++index) {
        const Entity e = order[index];
        SerialValue entity = SerialValue::MakeObject();
        if (Entity parent = ParentOf(scene, e); parent != kNullEntity) {
            const auto it = std::find(order.begin(), order.end(), parent);
            if (it != order.end()) entity["parent"] = static_cast<i64>(it - order.begin());
        }
        entity["components"] = WriteComponents(scene, e, registry, owners);
        entities.PushBack(std::move(entity));
    }
    if (owners.Size() > 0) {
        // Sorted by component id so the output does not depend on entity order.
        std::map<std::string, SerialValue> sorted;
        for (const auto& member : owners.Members()) sorted.emplace(member.first, member.second);
        SerialValue ordered = SerialValue::MakeObject();
        for (auto& [id, owner] : sorted) ordered[id] = std::move(owner);
        root["componentOwners"] = std::move(ordered);
    }
    root["entities"] = std::move(entities);
    return WriteJson(root, pretty);
}

static bool ReadScene(Scene& scene, std::string_view json, std::string* outError,
                      LegacyMaterialList* outLegacy, std::span<const Entity> originalIds) {
    auto parsed = ParseJson(json, outError);
    if (!parsed) return false;

    const SerialValue* versionValue = parsed->Find("version");
    if (!versionValue || !versionValue->IsInt() || versionValue->AsInt() < 1) {
        if (outError) *outError = "scene document has no valid 'version'";
        return false;
    }
    const i64 version = versionValue->AsInt();
    if (version > SceneSerializer::kVersion) {
        if (outError) {
            *outError = "scene file version " + std::to_string(version) +
                        " is newer than this engine supports (" + std::to_string(SceneSerializer::kVersion) +
                        "); update the engine to open it";
        }
        return false;
    }
    {
        const SerialValue* entities = parsed->Find("entities");
        if (!entities || !entities->IsArray()) {
            if (outError) *outError = "scene document has no 'entities' array";
            return false;
        }
    }
    MigrateDocument(*parsed, version);
    const SerialValue& entities = *parsed->Find("entities");

    const SerialValue* owners = parsed->Find("componentOwners");
    auto ownerOf = [owners](const std::string& id) -> std::string {
        if (!owners) return {};
        const SerialValue* o = owners->Find(id);
        return o ? o->AsString() : std::string{};
    };

    const ComponentRegistry& registry = Components();
    entt::registry& reg = scene.Registry();
    reg.clear();  // replace any existing contents
    if (outLegacy) outLegacy->clear();

    std::vector<Entity> created;
    std::vector<i64> parents;
    for (const SerialValue& entity : entities.Elements()) {
        const SerialValue* comps = entity.Find("components");
        if (!comps || !comps->IsObject()) continue;
        entt::entity e = originalIds.empty() ? reg.create() : reg.create(originalIds[created.size()]);
        created.push_back(e);
        parents.push_back(entity.Find("parent") ? entity.Find("parent")->AsInt(-1) : -1);
        UnresolvedComponents unresolved;
        for (const auto& member : comps->Members()) {
            const std::string& key = member.first;
            const SerialValue& payload = member.second;
            unresolved.keyOrder.push_back(key);
            if (const ComponentDesc* desc = registry.Find(key)) {
                desc->read(scene, e, payload);
            } else {
                unresolved.payloads.push_back({key, ownerOf(key), payload});
            }
        }
        ReadLegacyMaterial(e, *comps, outLegacy);
        if (!unresolved.payloads.empty()) {
            reg.emplace<UnresolvedComponents>(e, std::move(unresolved));
        }
    }
    for (usize index = 0; index < created.size(); ++index) {
        const i64 parentIndex = parents[index];
        if (parentIndex < 0 || static_cast<usize>(parentIndex) >= created.size()) continue;
        SetParent(scene, created[index], created[static_cast<usize>(parentIndex)]);
    }
    return true;
}

bool SceneSerializer::LoadFromString(Scene& scene, std::string_view json, std::string* error,
                                     LegacyMaterialList* legacy) {
    return ReadScene(scene, json, error, legacy, {});
}

bool SceneSerializer::Clone(const Scene& source, Scene& destination, std::string* error) {
    Scene copy;
    const auto* names = source.Registry().storage<NameComponent>();
    const auto ids = names ? std::span<const Entity>(names->data(), names->size()) : std::span<const Entity>{};
    if (!ReadScene(copy, SaveToString(source, false), error, nullptr, ids)) return false;
    destination = std::move(copy);
    return true;
}

bool SceneSerializer::SaveToFile(const Scene& scene,
                                 const std::filesystem::path& path,
                                 std::string* outError) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (outError) *outError = "could not open file for writing";
        return false;
    }
    out << SaveToString(scene);
    return true;
}

bool SceneSerializer::LoadFromFile(Scene& scene,
                                   const std::filesystem::path& path,
                                   std::string* outError,
                                   LegacyMaterialList* outLegacy) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (outError) *outError = "could not open file for reading";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return LoadFromString(scene, ss.str(), outError, outLegacy);
}

}  // namespace Luma
