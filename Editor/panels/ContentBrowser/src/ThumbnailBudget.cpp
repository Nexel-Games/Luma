#include "Luma/Editor/Panels/ThumbnailBudget.h"

namespace Luma::Editor::Panels {

void ThumbnailBudget::BeginFrame(f64 nowMs) {
    m_frameStartMs = nowMs;
    m_started = 0;
    m_deferred = 0;
}

bool ThumbnailBudget::TryStart(f64 nowMs) {
    if (m_started == 0 || nowMs - m_frameStartMs < m_budgetMs) {
        ++m_started;
        return true;
    }
    ++m_deferred;
    return false;
}

}  // namespace Luma::Editor::Panels
