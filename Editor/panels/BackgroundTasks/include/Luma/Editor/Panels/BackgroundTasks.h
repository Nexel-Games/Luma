#pragma once

#include <string>
#include <unordered_map>

#include "Luma/Slate/Context.h"
#include "Luma/Slate/Types.h"
#include "Luma/Tasks/TaskManager.h"

// Background Tasks UI (editor status bar).
//
// DrawStatusIndicator draws, at the right end of the status bar, a spinner
// with "N Running" (or "N Failed") and an aggregate progress bar; it is
// hidden when nothing is running or failed. Clicking it toggles the task
// panel, which DrawPanel renders as an overlay above the status bar (call it
// after the dock so it sits on top): one row per top-level task with title,
// percentage, progress bar, a detail line ("144 / 200 shaders", status or the
// error), and a cancel (running, cancellable) or dismiss (finished) button.

namespace Luma::Editor::Panels {

class BackgroundTasksPanel {
public:
    void DrawStatusIndicator(Slate::Context& ui, const Slate::Rect& statusBar,
                             const Tasks::TaskManager& tasks);
    void DrawPanel(Slate::Context& ui, Tasks::TaskManager& tasks);

    void Open() { m_open = true; }
    void Close() { m_open = false; }
    bool IsOpen() const { return m_open; }

    // Layout of the last frame (input routing + tests).
    const Slate::Rect& IndicatorRect() const { return m_indicator; }
    const std::string& IndicatorText() const { return m_indicatorText; }
    usize RowCount() const { return m_rowCount; }
    // The cancel / dismiss button of a task row (zero rect when none).
    Slate::Rect ActionRect(Tasks::TaskId id) const;

private:
    bool m_open = false;
    Slate::Rect m_indicator{};
    Slate::Rect m_statusBar{};
    Slate::Rect m_panel{};
    std::string m_indicatorText;
    usize m_rowCount = 0;
    f32 m_scroll = 0.0f;
    std::unordered_map<Tasks::TaskId, Slate::Rect> m_actions;
};

}  // namespace Luma::Editor::Panels
