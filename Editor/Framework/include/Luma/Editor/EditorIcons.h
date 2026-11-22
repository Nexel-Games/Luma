#pragma once

#include <string_view>

#include "Luma/Slate/IconKind.h"

// Registries describe icons by name (ComponentDesc::icon, menu entries, panels)
// so engine modules and plugins don't depend on the Slate icon enum. This maps a
// name to the Slate icon.

namespace Luma::Editor {

// The Slate icon called `name` ("Cube", "Light", ...); Icon::None when the name is
// empty or unknown.
Slate::Icon IconByName(std::string_view name);

}  // namespace Luma::Editor
