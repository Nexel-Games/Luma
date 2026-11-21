#pragma once

#include "Luma/Core/Types.h"

namespace Luma::Editor::Panels {

// Caps how much of a frame the Content Browser spends producing thumbnails
// (mesh/material previews render on the GPU and can take over a second each).
// The first thumbnail of a frame is always allowed so progress never stalls;
// more are allowed while the frame is still inside its budget. Tiles that
// were refused show their type icon and try again next frame.
class ThumbnailBudget {
public:
    explicit ThumbnailBudget(f64 budgetMs = 8.0) : m_budgetMs(budgetMs) {}

    void BeginFrame(f64 nowMs);
    // True if one more thumbnail may be produced now; false = defer it.
    bool TryStart(f64 nowMs);

    u32 StartedThisFrame() const { return m_started; }
    u32 DeferredThisFrame() const { return m_deferred; }

private:
    f64 m_budgetMs;
    f64 m_frameStartMs = 0.0;
    u32 m_started = 0;
    u32 m_deferred = 0;
};

}  // namespace Luma::Editor::Panels
