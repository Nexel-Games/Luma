#pragma once

#include <string>
#include <vector>

#include "Luma/Material/GraphAssets.h"
#include "Luma/Material/Material.h"
#include "Luma/Material/MaterialInstance.h"

// Converts Phase-1 fixed-recipe assets to graph assets.
//
// Every Phase-1 input becomes an equivalent node chain driving the Material
// Output, with parameters named after the input ("Base Color", "Base Color
// Texture", "Base Color Tint", "Base Color Tiling", ...):
//
//   Tiling/Offset params -> UV -> Texture Sample Parameter (channel pin)
//     -> One Minus (invert) -> * Tint/Scale param ----+
//                                                     Static Switch "Use <Input> Texture"
//   Constant param ---------------------------------- +        -> output pin
//
// Every input carries both branches (the static switch strips the unused one
// at compile time), so any Phase-1 instance override — including adding a
// texture the parent didn't have — stays expressible as a parameter override.
// Height drives one shared Parallax Occlusion Mapping node whose UVs feed
// every sample; Bump adds Normal From Heightmap blended onto the normal;
// Cavity multiplies Ambient Occlusion; Emissive gets an intensity parameter.

namespace Luma::Material {

// `notes` (optional) receives one human-readable line per converted input and
// per approximation.
GraphMaterial MigrateV3(const Material& old, std::vector<std::string>* notes);

// Phase-1 instance -> named parameter overrides. Each overridden input writes
// all of its expressible fields (Phase-1 overrides replaced the whole input);
// graph-structure fields (channel, invert, flip green) that differ from the
// resolved parent can't be parameters and are reported in `notes`.
GraphMaterialInstance MigrateInstanceV1(const MaterialInstance& instance,
                                        const Material& resolvedParent,
                                        std::vector<std::string>* notes);

}  // namespace Luma::Material
