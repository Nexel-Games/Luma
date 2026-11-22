#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Luma/Scene/Scene.h"

// Saves/loads a Scene to Luma's JSON scene format. The document is:
//
//   { "version": 3,
//     "componentOwners": { "RigidBody": "Luma.Physics.PhysX" },   // only if any
//     "entities": [ { "components": { "Name": {...}, "Transform": {...}, ... } } ] }
//
// Only entities carrying a NameComponent (the canonical scene objects) are
// written. Components are written and read through the runtime ComponentRegistry
// (Components()), so plugin components serialize exactly like built-ins.
//
// A component key with no registered type (its plugin is missing or disabled) is
// kept verbatim in UnresolvedComponents on the entity and written back on save,
// in its original position, so the file round-trips byte for byte and no data is
// lost. Registering the type later and calling ResolveUnresolvedComponents turns
// the payloads into live components.
//
// Versions: 1 stored a constant color per Mesh Renderer (albedo / metallic /
// roughness); loading one reports the inline values through `outLegacy` so the
// caller can migrate them (see MaterialMigration.h). 2 stores material slots.
// 3 adds "componentOwners". Files newer than kVersion are rejected.

namespace Luma {

// The inline material values of a version-1 Mesh Renderer.
struct LegacyMaterialValues {
    Math::Vec3 albedo{0.82f, 0.82f, 0.85f};  // linear base color
    f32 metallic = 0.0f;
    f32 roughness = 0.5f;
};
using LegacyMaterialList = std::vector<std::pair<Entity, LegacyMaterialValues>>;

class SceneSerializer {
public:
    static constexpr int kVersion = 3;

    static std::string SaveToString(const Scene& scene, bool pretty = true);
    static bool LoadFromString(Scene& scene, std::string_view json,
                               std::string* outError = nullptr,
                               LegacyMaterialList* outLegacy = nullptr);

    static bool SaveToFile(const Scene& scene, const std::filesystem::path& path,
                           std::string* outError = nullptr);
    static bool LoadFromFile(Scene& scene, const std::filesystem::path& path,
                             std::string* outError = nullptr,
                             LegacyMaterialList* outLegacy = nullptr);
};

}  // namespace Luma
