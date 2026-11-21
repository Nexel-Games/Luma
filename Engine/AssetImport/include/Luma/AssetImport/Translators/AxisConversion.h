#pragma once

#include "Luma/Math/Math.h"

struct aiScene;

namespace Luma::Import {

// FBX axis indices: 0 = X, 1 = Y, 2 = Z. unitScaleCm is FBX UnitScaleFactor
// (centimetres per file unit). glTF and OBJ are Y-up metres, which is the default.
struct SourceAxes {
    int up = 1;
    int upSign = 1;
    int front = 2;
    int frontSign = 1;
    int coord = 0;
    int coordSign = 1;
    f64 unitScaleCm = 100.0;
};

// Source space → Luma (Y-up, -Z front, metres). Scale is unitScaleCm / 100.
Math::Mat4 SourceToLuma(const SourceAxes& axes);

// Missing keys keep the SourceAxes defaults (Y-up, metres).
SourceAxes AxesFromAssimpMetadata(const aiScene& scene);

}  // namespace Luma::Import
