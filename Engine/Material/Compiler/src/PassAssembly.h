#pragma once

#include <vector>

#include "Luma/Material/Material.h"
#include "Luma/MaterialCompiler/CompiledMaterial.h"

namespace Luma::MaterialCompiler::Detail {

// The passes a material draws in: Opaque/Masked surfaces -> G-buffer, depth
// (shadows), selection; translucent surfaces -> forward, selection;
// post-process -> its fullscreen pass.
std::vector<Pass> PassesFor(const Material::MaterialSettings& settings);

// Fills `c.passes` from the compiled bodies, outputs and interpolators.
void AssemblePasses(CompiledMaterial& c, const Material::MaterialSettings& settings);

}  // namespace Luma::MaterialCompiler::Detail
