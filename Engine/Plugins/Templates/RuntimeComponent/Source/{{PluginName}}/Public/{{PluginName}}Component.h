#pragma once

#include "Luma/Core/Types.h"
#include "Luma/Serialization/Reflection.h"

namespace Luma {

struct {{PluginName}}Component {
    i32 value = 0;
};

template <>
const TypeInfo<{{PluginName}}Component>& GetTypeInfo<{{PluginName}}Component>();

}  // namespace Luma
