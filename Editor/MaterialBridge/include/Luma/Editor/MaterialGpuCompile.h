#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Material/GraphAssets.h"
#include "Luma/MaterialCompiler/Preshader.h"
#include "Luma/RHI/MaterialGpuDesc.h"

namespace Luma::Material {
class MaterialLibrary;
}
namespace Luma::ShaderCompiler {
class Compiler;
}

// Graph material -> what a SceneRenderer draws: translate the graph
// (MaterialCompiler), compile every pass's GLSL to SPIR-V (ShaderCompiler,
// synchronously, through its disk cache) and evaluate the parameter block.

namespace Luma::Editor {

struct GpuMaterialBuild {
    bool ok = false;
    std::string errors;  // translator / shader errors, "; "-separated (empty when ok)
    CompiledMaterialGpu gpu;
    MaterialParameterBlock block;
    // Texture asset (virtual path, "" = none) for each gpu.textures entry; the
    // caller uploads them and fills GpuTextureBinding::texture.
    std::vector<std::string> texturePaths;
    // To re-evaluate the block when parameters or collections change.
    MaterialCompiler::PreshaderProgram preshader;
    MaterialCompiler::ParameterBlockLayout layout;
};

// `overrides` are an instance chain's parameter values (static switches pick
// the permutation); `clip` / `twoSided` its setting overrides. `library`
// resolves functions and collections (may be null).
GpuMaterialBuild BuildGpuMaterial(const Material::GraphMaterial& material, Material::MaterialLibrary* library,
                                  std::span<const Material::ParameterOverride> overrides,
                                  std::optional<f32> clip, std::optional<bool> twoSided,
                                  ShaderCompiler::Compiler& shaders, std::string_view label);

}  // namespace Luma::Editor
