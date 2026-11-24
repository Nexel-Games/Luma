#include "Luma/Platform/Sound.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Luma {

void PlayAlertSound() {
#if defined(_WIN32)
    MessageBeep(MB_OK);  // the system's Default Beep (user-configurable)
#endif
}

}  // namespace Luma
