#pragma once

#include <string>
#include <vector>

#include "Luma/Material/Material.h"

// "Create Material from Textures": wires a texture set into a new Material by
// file-name tokens (split on '_', '-', '.', ' '; case-insensitive):
//   diff | albedo | basecolor | color -> Base Color
//   nor | normal | nrm               -> Normal ("dx" flips green, "gl" doesn't)
//   rough | roughness                -> Roughness (R)
//   metal | metallic | metalness     -> Metallic (R)
//   ao | occlusion                   -> Ambient Occlusion (R)
//   arm | orm                        -> AO = R, Roughness = G, Metallic = B
//   emiss | emissive                 -> Emissive Color
//   opacity | mask | alpha           -> Opacity Mask (R), blend mode Masked
// Textures matching none of these are left out and reported in `ignored`.
// Color space needs no setting: the importer marks data maps linear by the
// same names (IsTextureSRGB).

namespace Luma::Material {

struct MaterialFromTexturesResult {
    Material material;
    std::vector<std::string> ignored;  // virtual paths that matched no input
    std::string suggestedName;         // "M_<shared prefix>"
};

MaterialFromTexturesResult BuildMaterialFromTextures(
    const std::vector<std::string>& textureVirtualPaths);

}  // namespace Luma::Material
