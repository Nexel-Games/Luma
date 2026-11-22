#include "Luma/Scene/SceneSerializer.h"

#include <fstream>
#include <sstream>

#include "Luma/Scene/ComponentReflection.h"
#include "Luma/Scene/Hierarchy.h"
#include "Luma/Serialization/Json.h"
#include "Luma/Serialization/Reflection.h"

namespace Luma {

namespace {

// Writes component `C` of entity `e` into `comps[key]` if the entity has one.
template <class C>
void WriteComponent(const entt::registry& reg, entt::entity e, SerialValue& comps,
                    const char* key) {
    if (const C* c = reg.try_get<C>(e)) {
        comps[key] = SerializeObject(*c);
    }
}

// Reads component `C` from `comps[key]` onto entity `e` if the member is present.
template <class C>
void ReadComponent(entt::registry& reg, entt::entity e, const SerialValue& comps,
                   const char* key) {
    if (const SerialValue* field = comps.Find(key)) {
        C comp{};
        DeserializeObject(*field, comp);
        reg.emplace_or_replace<C>(e, std::move(comp));
    }
}

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

}  // namespace

std::string SceneSerializer::SaveToString(const Scene& scene, bool pretty) {
    SerialValue root = SerialValue::MakeObject();
    root["version"] = kVersion;

    SerialValue entities = SerialValue::MakeArray();
    const entt::registry& reg = scene.Registry();
    // NameComponent marks the canonical scene objects; iterate those.
    auto view = reg.view<const NameComponent>();
    std::vector<Entity> order;
    for (auto e : view) order.push_back(e);
    for (usize index = 0; index < order.size(); ++index) {
        Entity e = order[index];
        SerialValue entity = SerialValue::MakeObject();
        if (Entity parent = ParentOf(scene, e); parent != kNullEntity) {
            for (usize parentIndex = 0; parentIndex < order.size(); ++parentIndex) {
                if (order[parentIndex] == parent) {
                    entity["parent"] = static_cast<i64>(parentIndex);
                    break;
                }
            }
        }
        SerialValue comps = SerialValue::MakeObject();
        WriteComponent<NameComponent>(reg, e, comps, "Name");
        WriteComponent<TransformComponent>(reg, e, comps, "Transform");
        WriteComponent<MeshRendererComponent>(reg, e, comps, "MeshRenderer");
        WriteComponent<CameraComponent>(reg, e, comps, "Camera");
        WriteComponent<LightComponent>(reg, e, comps, "Light");
        WriteComponent<EnvironmentComponent>(reg, e, comps, "Environment");
        WriteComponent<PostProcessComponent>(reg, e, comps, "PostProcess");
        entity["components"] = std::move(comps);
        entities.PushBack(std::move(entity));
    }
    root["entities"] = std::move(entities);
    return WriteJson(root, pretty);
}

bool SceneSerializer::LoadFromString(Scene& scene, std::string_view json,
                                     std::string* outError,
                                     LegacyMaterialList* outLegacy) {
    auto parsed = ParseJson(json, outError);
    if (!parsed) return false;

    const SerialValue* entities = parsed->Find("entities");
    if (!entities || !entities->IsArray()) {
        if (outError) *outError = "scene document has no 'entities' array";
        return false;
    }

    entt::registry& reg = scene.Registry();
    reg.clear();  // replace any existing contents
    if (outLegacy) outLegacy->clear();

    std::vector<Entity> created;
    std::vector<i64> parents;
    for (const SerialValue& entity : entities->Elements()) {
        const SerialValue* comps = entity.Find("components");
        if (!comps) continue;
        entt::entity e = reg.create();
        created.push_back(e);
        parents.push_back(entity.Find("parent") ? entity.Find("parent")->AsInt(-1) : -1);
        ReadComponent<NameComponent>(reg, e, *comps, "Name");
        ReadComponent<TransformComponent>(reg, e, *comps, "Transform");
        ReadComponent<MeshRendererComponent>(reg, e, *comps, "MeshRenderer");
        ReadLegacyMaterial(e, *comps, outLegacy);
        ReadComponent<CameraComponent>(reg, e, *comps, "Camera");
        ReadComponent<LightComponent>(reg, e, *comps, "Light");
        ReadComponent<EnvironmentComponent>(reg, e, *comps, "Environment");
        ReadComponent<PostProcessComponent>(reg, e, *comps, "PostProcess");
    }
    for (usize index = 0; index < created.size(); ++index) {
        const i64 parentIndex = parents[index];
        if (parentIndex < 0 || static_cast<usize>(parentIndex) >= created.size()) continue;
        SetParent(scene, created[index], created[static_cast<usize>(parentIndex)]);
    }
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
