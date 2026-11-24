#include "Luma/Sample/SampleSpinnerComponent.h"

#include "Luma/Scene/ComponentReflection.h"

namespace Luma {
template <>
const TypeInfo<SampleSpinnerComponent>& GetTypeInfo<SampleSpinnerComponent>() {
    static const TypeInfo<SampleSpinnerComponent> info = [] {
        TypeBuilder<SampleSpinnerComponent> b("SampleSpinner");
        b.Property("axis", &SampleSpinnerComponent::axis)
            .Category("Spinner")
            .Tooltip("Local Euler weights. (0,1,0) spins around yaw.");
        b.Property("degreesPerSecond", &SampleSpinnerComponent::degreesPerSecond)
            .Category("Spinner")
            .Range(-720.0, 720.0)
            .Tooltip("Rotation speed in degrees per second during Play.");
        return b.Build();
    }();
    return info;
}
}  // namespace Luma
