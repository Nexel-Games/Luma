#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Luma/Scene/Scene.h"

// Saves/loads a Scene to Luma's JSON scene format. The document is:
//
//   { "version": 2,
//     "entities": [ { "parent": <index>, "components": { "Name": {...}, ... } } ] }
//
// "parent" is optional. It is the index of the parent in this entities array.
// Transforms stay in world space across that link.
//
// Only entities carrying a NameComponent (the canonical scene objects) are
// written. Loading clears the target scene first, then reconstructs entities and
// their components; unknown component keys are ignored so files stay forward- and
// backward-compatible as components evolve.

//
// Version 1 stored a constant color per Mesh Renderer (albedo / metallic /
// roughness). Version 2 stores material slots instead; loading a version-1
// Mesh Renderer reports its inline values through `outLegacy` so the caller
// can migrate them (see MaterialMigration.h).

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
    static constexpr int kVersion = 2;

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
