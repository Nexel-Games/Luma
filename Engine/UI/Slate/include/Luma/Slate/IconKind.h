#pragma once

namespace Luma::Slate {

enum class Icon {
    None,
#define LUMA_ICON(role, source) role,
#include "Luma/Slate/IconSources.inc"
#undef LUMA_ICON
    Count
};

}  // namespace Luma::Slate
