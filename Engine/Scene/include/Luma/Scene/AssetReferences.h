#pragma once

#include <string_view>

#include "Luma/Core/Types.h"

namespace Luma {

class Scene;

// Rewrites every asset path the scene's components hold (static meshes and
// material slots) that equals `from`, or lies under it when `from` is a
// folder ("/Game/Props" -> "/Game/Rocks" also maps "/Game/Props/SM_A"), to
// the same path under `to`. Whole path segments only. Returns the number of
// references changed. (Unreal fixes references up through redirectors.)
u32 RetargetAssetReferences(Scene& scene, std::string_view from, std::string_view to);

}  // namespace Luma
