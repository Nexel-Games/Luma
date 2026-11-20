#pragma once

#include <filesystem>

#include "Luma/Asset/LumaTextureFormat.h"

namespace Luma {

// Whether a texture's texels are sRGB-encoded color (true) or linear data
// such as normals, roughness or packed masks (false). The color space is a
// flag sampled through a matching image view; pixels are never converted.
//
// An explicit metadata flag wins. Without one, the file name decides: any
// token (split on '_', '-', '.', ' ') that names data — normal, nor, nrm,
// rough, metal, ao, occlusion, orm, arm, mask, opacity, height, disp — marks
// the texture linear, case-insensitively. Everything else is sRGB color.
bool IsTextureSRGB(const std::filesystem::path& file, const LumaTextureMetadata* meta);

}  // namespace Luma
