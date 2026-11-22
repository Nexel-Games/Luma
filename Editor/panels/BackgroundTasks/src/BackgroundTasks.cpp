#include "Luma/Editor/Panels/BackgroundTasks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Luma/Slate/Icons.h"

namespace Luma::Editor::Panels {

using Slate::Color;
using Slate::Rect;
using Slate::Vec2;
using Tasks::TaskSnapshot;
using Tasks::TaskState;

namespace {

constexpr f32 kPanelWidth = 380.0f;
constexpr f32 kHeaderH = 34.0f;
constexpr f32 kRowH = 58.0f;
constexpr f32 kFooterH = 26.0f;
constexpr usize kMaxVisibleRows = 6;
// Progress track; indeterminate tasks get a sliding segment.
void DrawBar(Slate::Context& ui, const Rect& r, const TaskSnapshot& s, Color fill) {
    const Slate::Theme& t = ui.theme();
    ui.PanelRounded(r, t.recessed, r.h * 0.5f);
    if (s.Indeterminate() && !s.Finished()) {
        const f32 seg = r.w * 0.3f;
        const f32 phase = std::fmod(ui.time() * 0.8f, 1.0f);
        const f32 x = r.x - seg + (r.w + seg) * phase;
        ui.PushClip(r);
        ui.PanelRounded({x, r.y, seg, r.h}, fill, r.h * 0.5f);
        ui.PopClip();
        return;
    }
    const f32 f = s.state == TaskState::Completed ? 1.0f : s.Fraction();
    if (f > 0.0f) ui.PanelRounded({r.x, r.y, std::max(r.h, r.w * f), r.h}, fill, r.h * 0.5f);
}

std::string Percent(const TaskSnapshot& s) {
    if (s.Indeterminate() && !s.Finished()) return {};
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d%%",
                  static_cast<int>(std::lround((s.state == TaskState::Completed ? 1.0f
                                                                               : s.Fraction()) *
                                              100.0f)));
    return buf;
}

std::string Detail(const TaskSnapshot& s) {
    switch (s.state) {
        case TaskState::Failed: return s.error.empty() ? "Failed" : "Failed: " + s.error;
        case TaskState::Cancelled: return "Cancelled";
        case TaskState::Completed: return "Done";
        case TaskState::Running: break;
    }
    if (s.cancelRequested) return "Cancelling...";
    std::string text = s.CountText();
    if (!s.status.empty()) text = text.empty() ? s.status : text + "  -  " + s.status;
    return text.empty() ? "Processing..." : text;
}

}  // namespace

Rect BackgroundTasksPanel::ActionRect(Tasks::TaskId id) const {
    auto it = m_actions.find(id);
    return it == m_actions.end() ? Rect{} : it->second;
}

void BackgroundTasksPanel::DrawStatusIndicator(Slate::Context& ui, const Rect& statusBar,
                                               const Tasks::TaskManager& tasks) {
    const Slate::Theme& t = ui.theme();
    m_statusBar = statusBar;
    const auto snap = tasks.Snapshot();
    u32 running = 0, failed = 0;
    for (const TaskSnapshot& s : snap) {
        if (s.parent != 0) continue;
        running += s.state == TaskState::Running;
        failed += s.state == TaskState::Failed;
    }
    if (running == 0 && failed == 0) {
        m_indicator = {};
        m_indicatorText.clear();
        m_open = false;
        return;
    }
    m_indicatorText = running > 0 ? std::to_string(running) + " Running"
                                   : std::to_string(failed) + " Failed";
    const f32 textW = ui.font().Measure(m_indicatorText).x;
    const f32 pad = t.space.lg;  // LabelIn's built-in left inset
    const f32 barW = running > 0 ? 72.0f : 0.0f;
    const f32 w = 10.0f + 14.0f + 8.0f + textW + (barW > 0 ? 10.0f + barW : 0.0f) + 10.0f;
    m_indicator = {statusBar.Right() - w - 6.0f, statusBar.y + 2.0f, w, statusBar.h - 4.0f};

    const bool hovered = m_indicator.Contains(ui.mouse());
    if (hovered && ui.mousePressed(0)) m_open = !m_open;
    if (hovered || m_open) ui.PanelRounded(m_indicator, t.surface3, t.radius.sm);
    ui.Tooltip(Slate::Context::ID("bgtasks.indicator"), m_indicator,
               "Background Tasks\nClick to show the running operations.");

    f32 x = m_indicator.x + 10.0f;
    const f32 cy = m_indicator.y + m_indicator.h * 0.5f;
    if (running > 0) {
        Slate::DrawIcon(ui, {x, cy - 7, 14, 14}, Slate::Icon::Busy, t.accent, ui.time() * 6.0f);
    } else {
        Slate::DrawIcon(ui, {x, cy - 7.0f, 14.0f, 14.0f}, Slate::Icon::Close, t.error);
    }
    x += 14.0f + 8.0f;
    ui.LabelIn({x - pad, m_indicator.y, textW + pad + 4.0f, m_indicator.h}, m_indicatorText,
               running > 0 ? t.text : t.error);
    x += textW + 10.0f;
    if (barW > 0.0f) {
        TaskSnapshot aggregate;
        aggregate.total = 1000;
        aggregate.done = static_cast<u64>(tasks.AggregateFraction() * 1000.0f);
        bool anyDeterminate = false;
        for (const TaskSnapshot& s : snap) {
            anyDeterminate |= s.parent == 0 && !s.Finished() && !s.Indeterminate();
        }
        if (!anyDeterminate) aggregate.total = 0;  // only indeterminate work: slide
        DrawBar(ui, {x, cy - 2.5f, barW, 5.0f}, aggregate, t.accent);
    }
}

void BackgroundTasksPanel::DrawPanel(Slate::Context& ui, Tasks::TaskManager& tasks) {
    m_actions.clear();
    m_rowCount = 0;
    if (!m_open || m_indicator.w <= 0.0f) {
        m_open = false;
        return;
    }
    const Slate::Theme& t = ui.theme();
    const f32 pad = t.space.lg;  // LabelIn / Heading left inset
    std::vector<TaskSnapshot> rows;
    u32 running = 0;
    for (TaskSnapshot& s : tasks.Snapshot()) {
        if (s.parent != 0) continue;
        running += s.state == TaskState::Running;
        rows.push_back(std::move(s));
    }
    m_rowCount = rows.size();

    const usize visible = std::min(rows.size(), kMaxVisibleRows);
    const f32 h = kHeaderH + kRowH * static_cast<f32>(std::max<usize>(visible, 1)) +
                  (running > 0 ? kFooterH : 6.0f);
    const f32 w = std::min(kPanelWidth, m_statusBar.w - 12.0f);
    m_panel = {m_statusBar.Right() - w - 6.0f, m_statusBar.y - h - 6.0f, w, h};

    // Close on Escape or a press outside the panel and the indicator.
    if (ui.keyEscape() ||
        (ui.mousePressed(0) && !m_panel.Contains(ui.mouse()) &&
         !m_indicator.Contains(ui.mouse()))) {
        m_open = false;
        return;
    }

    ui.drawList().AddRectShadow(m_panel, t.radius.md, 0.6f);
    ui.PanelRoundedBordered(m_panel, t.surface4, t.outline, t.radius.md, t.border.hairline);
    const Rect header{m_panel.x + 12.0f - pad, m_panel.y, m_panel.w - 24.0f + 2.0f * pad, kHeaderH};
    ui.Heading(header, "Background Tasks", t.text);
    ui.LabelIn(header, running > 0 ? std::to_string(running) + " Running" : "Idle", t.textDim,
               Slate::Align::Right);
    ui.Panel({m_panel.x, m_panel.y + kHeaderH - 1.0f, m_panel.w, 1.0f}, t.separator);

    const Rect list{m_panel.x, m_panel.y + kHeaderH,
                    m_panel.w, kRowH * static_cast<f32>(std::max<usize>(visible, 1))};
    const f32 contentH = kRowH * static_cast<f32>(rows.size());
    m_scroll = ui.VerticalScroll(Slate::Context::ID("bgtasks.scroll"), list, contentH, m_scroll);
    ui.PushClip(list);
    f32 y = list.y - m_scroll;
    for (const TaskSnapshot& s : rows) {
        const Rect row{list.x + 12.0f, y, list.w - 24.0f, kRowH};
        y += kRowH;
        if (row.Bottom() < list.y || row.y > list.Bottom()) continue;
        const bool failed = s.state == TaskState::Failed;
        const Color accent = failed ? t.error
                             : s.state == TaskState::Completed ? t.success
                                                               : t.accent;
        // Action button (cancel while running, dismiss when finished).
        const bool canCancel = s.state == TaskState::Running && s.cancellable && !s.cancelRequested;
        const bool canDismiss = s.state != TaskState::Running;
        const Rect action{row.Right() - 20.0f, row.y + 8.0f, 20.0f, 20.0f};
        const f32 titleRight = (canCancel || canDismiss) ? action.x - 8.0f : row.Right();
        ui.LabelIn({row.x - pad, row.y + 6.0f, titleRight - row.x - 44.0f + pad, 22.0f}, s.title, t.text);
        ui.LabelIn({titleRight - 44.0f, row.y + 6.0f, 44.0f + pad, 22.0f}, Percent(s), t.textDim,
                   Slate::Align::Right);
        if (canCancel || canDismiss) {
            m_actions[s.id] = action;
            const u64 id = Slate::Context::ID("bgtasks.action") ^ s.id;
            if (ui.IconTextButton(id, action, Slate::Icon::Close, {}, t.textDim)) {
                if (canCancel) {
                    tasks.RequestCancel(s.id);
                } else {
                    tasks.Dismiss(s.id);
                }
            }
            ui.Tooltip(id ^ 0x71, action,
                       canCancel ? "Cancel\nStop this operation." : "Dismiss\nRemove from the list.");
        }
        DrawBar(ui, {row.x, row.y + 31.0f, row.w, 5.0f}, s, accent);
        ui.LabelIn({row.x - pad, row.y + 38.0f, row.w + pad, 18.0f}, Detail(s),
                   failed ? t.error : t.textDim);
        if (failed) ui.Tooltip(Slate::Context::ID("bgtasks.err") ^ s.id, row, s.title + "\n" + s.error);
    }
    ui.PopClip();
    if (rows.empty()) {
        ui.LabelIn({list.x, list.y, list.w, kRowH}, "No background operations.", t.textDim,
                   Slate::Align::Center);
    }
    if (running > 0) {
        ui.LabelIn({m_panel.x + 12.0f - pad, m_panel.Bottom() - kFooterH, m_panel.w - 24.0f + pad, kFooterH},
                   "Processing background operations...", t.textDim);
    }
}

}  // namespace Luma::Editor::Panels
