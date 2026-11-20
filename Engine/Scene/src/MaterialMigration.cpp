#include "Luma/Scene/MaterialMigration.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <set>

#include "Luma/Core/Log.h"

namespace Luma {
namespace {

using Material::MaterialInput;
using Material::MaterialInstance;
using Material::MaterialLibrary;
using Material::MaterialParameter;

constexpr f32 kEpsilon = 1e-4f;

bool Near(f32 a, f32 b) { return std::fabs(a - b) <= kEpsilon; }

bool IsDefault(const LegacyMaterialValues& v) {
    const LegacyMaterialValues d;
    return Near(v.albedo.x, d.albedo.x) && Near(v.albedo.y, d.albedo.y) &&
           Near(v.albedo.z, d.albedo.z) && Near(v.metallic, d.metallic) &&
           Near(v.roughness, d.roughness);
}

const MaterialInput& InputOf(const MaterialInstance& mi, MaterialParameter p) {
    return mi.inputs[static_cast<usize>(p)];
}

MaterialInstance MakeInstance(const std::string& name, const LegacyMaterialValues& v) {
    MaterialInstance mi;
    mi.name = name;
    mi.parent = std::string(Material::kDefaultMaterialPath);
    const auto defaults = Material::Material::DefaultInputs();
    auto override = [&](MaterialParameter p, Math::Vec4 constant) {
        MaterialInput input = defaults[static_cast<usize>(p)];
        input.constant = constant;
        mi.Override(p, input);
    };
    override(MaterialParameter::BaseColor, {v.albedo.x, v.albedo.y, v.albedo.z, 1.0f});
    override(MaterialParameter::Metallic, {v.metallic, v.metallic, v.metallic, v.metallic});
    override(MaterialParameter::Roughness, {v.roughness, v.roughness, v.roughness, v.roughness});
    return mi;
}

// True when `mi` is exactly what migrating `v` produces: a Default Material
// child overriding only the three migrated constants.
bool Matches(const MaterialInstance& mi, const LegacyMaterialValues& v) {
    if (mi.parent != Material::kDefaultMaterialPath) return false;
    if (mi.twoSided || mi.opacityMaskClip || mi.blendMode) return false;
    for (usize i = 0; i < Material::kMaterialParameterCount; ++i) {
        const auto p = static_cast<MaterialParameter>(i);
        const bool migrated = p == MaterialParameter::BaseColor ||
                              p == MaterialParameter::Metallic ||
                              p == MaterialParameter::Roughness;
        if (mi.IsOverridden(p) != migrated) return false;
        if (migrated && !InputOf(mi, p).texture.empty()) return false;
    }
    const auto& base = InputOf(mi, MaterialParameter::BaseColor).constant;
    return Near(base.x, v.albedo.x) && Near(base.y, v.albedo.y) && Near(base.z, v.albedo.z) &&
           Near(InputOf(mi, MaterialParameter::Metallic).constant.x, v.metallic) &&
           Near(InputOf(mi, MaterialParameter::Roughness).constant.x, v.roughness);
}

// "Red Box: #1" -> "Red_Box_1": letters, digits and single underscores.
std::string Sanitize(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(c);
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "Material" : out;
}

// Every instance in the migration folder: saved files plus unsaved ones.
std::vector<std::string> MigratedInstances(MaterialLibrary& library) {
    std::set<std::string> paths;
    const std::string prefix = std::string(kMigratedMaterialsFolder) + "/";
    if (auto probe = library.Paths().MakeFilePath(prefix + "_", "lmatinst")) {
        std::error_code ec;
        const std::filesystem::path folder = probe->parent_path();
        if (std::filesystem::is_directory(folder, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".lmatinst") continue;
                if (auto vp = library.Paths().ToVirtual(entry.path())) paths.insert(*vp);
            }
        }
    }
    for (const std::string& dirty : library.DirtyAssets()) {
        if (dirty.rfind(prefix, 0) == 0) paths.insert(dirty);
    }
    return {paths.begin(), paths.end()};
}

std::string EntityName(const Scene& scene, Entity e) {
    if (const auto* name = scene.Registry().try_get<NameComponent>(e)) return name->name;
    return {};
}

}  // namespace

MigrationResult MigrateLegacyMaterials(Scene& scene, const LegacyMaterialList& legacy,
                                       MaterialLibrary& library) {
    MigrationResult result;
    std::set<std::string> created;
    std::set<std::string> reused;
    std::vector<std::string> candidates = MigratedInstances(library);

    for (const auto& [entity, values] : legacy) {
        auto* mesh = scene.Registry().try_get<MeshRendererComponent>(entity);
        if (!mesh) continue;
        if (IsDefault(values)) {
            mesh->materials.clear();
            continue;
        }

        std::string path;
        for (const std::string& candidate : candidates) {
            const MaterialInstance* mi = library.GetInstance(candidate);
            if (mi && Matches(*mi, values)) {
                path = candidate;
                break;
            }
        }
        if (path.empty()) {
            const std::string desired = std::string(kMigratedMaterialsFolder) + "/MI_" +
                                        Sanitize(EntityName(scene, entity));
            MaterialInstance mi = MakeInstance({}, values);
            path = library.CreateInMemoryInstance(desired, mi);
            mi.name = path.substr(path.find_last_of('/') + 1);
            library.SetInstance(path, mi);
            result.created.emplace_back(path, mi);
            created.insert(path);
            candidates.push_back(path);
        } else if (!created.count(path) && reused.insert(path).second) {
            result.reused.push_back(path);
        }
        mesh->materials = {path};
    }

    if (!result.created.empty()) {
        LUMA_USER_INFO("Materials",
                       "Moved {} legacy color{} onto Material Instances in {} (save to keep)",
                       result.created.size(), result.created.size() == 1 ? "" : "s",
                       kMigratedMaterialsFolder);
    }
    return result;
}

}  // namespace Luma
