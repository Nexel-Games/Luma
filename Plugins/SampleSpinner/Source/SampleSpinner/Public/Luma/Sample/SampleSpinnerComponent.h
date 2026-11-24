#pragma once

#include "Luma/Math/Math.h"
#include "Luma/Serialization/Reflection.h"

namespace Luma {

// Rotates the entity during Play. Axis is local Euler weights (0,1,0 = yaw).
struct SampleSpinnerComponent {
    Math::Vec3 axis{0.0f, 1.0f, 0.0f};
    f32 degreesPerSecond = 90.0f;
};

template <>
const TypeInfo<SampleSpinnerComponent>& GetTypeInfo<SampleSpinnerComponent>();

}  // namespace Luma
