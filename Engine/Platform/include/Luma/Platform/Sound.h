#pragma once

// System sounds: the OS's own alert cues, so the editor sounds like the
// platform it runs on.

namespace Luma {

// The system "blocked" alert (Windows: Default Beep, as when clicking past a
// modal dialog). No-op where the platform has none.
void PlayAlertSound();

}  // namespace Luma
