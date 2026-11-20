#pragma once

#include <string>
#include <utility>
#include <vector>

#include "Luma/Material/MaterialInstance.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Scene/SceneSerializer.h"

// Moves version-1 inline Mesh Renderer colors onto material assets.
//
// Values equal to the Default Material (within 1e-4) become an empty slot.
// Every other set of values becomes a Material Instance of the Default
// Material overriding Base Color, Metallic and Roughness, under
// /Game/Materials/Migrated/MI_<first entity name>. Instances are shared: by
// value within the scene, and with instances already in that folder (saved
// files or unsaved ones from an earlier migration this session).
//
// New instances live only in the library (dirty) until it saves them, so
// opening an old project never writes files on its own.

namespace Luma {

inline constexpr std::string_view kMigratedMaterialsFolder = "/Game/Materials/Migrated";

struct MigrationResult {
    // Virtual path + asset of every instance created by this migration.
    std::vector<std::pair<std::string, Material::MaterialInstance>> created;
    // Existing instances the scene now references.
    std::vector<std::string> reused;
};

MigrationResult MigrateLegacyMaterials(Scene& scene, const LegacyMaterialList& legacy,
                                       Material::MaterialLibrary& library);

}  // namespace Luma
