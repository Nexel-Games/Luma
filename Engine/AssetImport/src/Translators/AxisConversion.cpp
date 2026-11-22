#include "Luma/AssetImport/Translators/AxisConversion.h"

#include <assimp/scene.h>

namespace Luma::Import {
namespace {

Math::Vec3 Axis(int index, int sign) {
    const f32 s = sign < 0 ? -1.0f : 1.0f;
    if (index == 0) {
        return {s, 0.0f, 0.0f};
    }
    if (index == 1) {
        return {0.0f, s, 0.0f};
    }
    if (index == 2) {
        return {0.0f, 0.0f, s};
    }
    return {0.0f, 0.0f, 0.0f};
}

bool ReadInt(const aiMetadata& meta, const char* key, int& out) {
    int32_t value = 0;
    if (!meta.Get(key, value)) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

bool ReadScale(const aiMetadata& meta, f64& out) {
    double asDouble = 0.0;
    if (meta.Get("UnitScaleFactor", asDouble)) {
        out = asDouble;
        return true;
    }
    float asFloat = 0.0f;
    if (meta.Get("UnitScaleFactor", asFloat)) {
        out = static_cast<f64>(asFloat);
        return true;
    }
    return false;
}

}  // namespace

Math::Mat4 SourceToLuma(const SourceAxes& axes) {
    // Columns of this basis are Luma's axes expressed in source space.
    // +X = coord, +Y = up, +Z = front. source→Luma is the transpose, then
    // scaled by unitScaleCm/100 (FBX stores centimetres per file unit).
    //
    // The task text also multiplies front by -1, because "FBX front points
    // into the screen". robot.fbx already stores FrontAxisSign = -1 for that
    // into-screen direction. Applying the minus again mirrors Z relative to
    // the glTF. Omitting it makes the default Y-up/metres axes an identity
    // and puts the centimetre Z-up fixture on the glTF positions.
    const Math::Vec3 x = Axis(axes.coord, axes.coordSign);
    const Math::Vec3 y = Axis(axes.up, axes.upSign);
    const Math::Vec3 z = Axis(axes.front, axes.frontSign);

    Math::Mat4 rotation = Math::Mat4::Identity();
    rotation.m[0] = x.x;
    rotation.m[1] = y.x;
    rotation.m[2] = z.x;
    rotation.m[4] = x.y;
    rotation.m[5] = y.y;
    rotation.m[6] = z.y;
    rotation.m[8] = x.z;
    rotation.m[9] = y.z;
    rotation.m[10] = z.z;

    const f32 scale = static_cast<f32>(axes.unitScaleCm / 100.0);
    return Math::Scale({scale, scale, scale}) * rotation;
}

SourceAxes AxesFromAssimpMetadata(const aiScene& scene) {
    SourceAxes axes;
    const aiMetadata* meta = scene.mMetaData;
    if (meta == nullptr) {
        return axes;
    }
    ReadInt(*meta, "UpAxis", axes.up);
    ReadInt(*meta, "UpAxisSign", axes.upSign);
    ReadInt(*meta, "FrontAxis", axes.front);
    ReadInt(*meta, "FrontAxisSign", axes.frontSign);
    ReadInt(*meta, "CoordAxis", axes.coord);
    ReadInt(*meta, "CoordAxisSign", axes.coordSign);
    ReadScale(*meta, axes.unitScaleCm);
    return axes;
}

}  // namespace Luma::Import
