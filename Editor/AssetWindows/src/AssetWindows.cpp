#include "Luma/Editor/AssetWindows.h"

#include <algorithm>
#include <cmath>

#include "Luma/Slate/Icons.h"

namespace Luma::Editor {

using Slate::Color;
using Slate::Context;
using Slate::Rect;
using Slate::Vec2;

namespace {
constexpr f32 kDragThreshold = 4.0f;
constexpr f32 kButtonW = 40.0f;

// Hides the pointer from widgets drawn inside the scope (a window above
// covers them), then puts it back.
struct PointerHider {
    Context& ui;
    Vec2 saved;
    bool active;
    PointerHider(Context& c, bool hide) : ui(c), saved(c.mouse()), active(hide) {
        if (active) ui.WarpMouse(-100000.0f, -100000.0f);
    }
    ~PointerHider() {
        if (active) ui.WarpMouse(saved.x, saved.y);
    }
};
}  // namespace

AssetWindowManager::Window* AssetWindowManager::Find(const std::string& id) {
    for (auto& w : m_windows)
        if (w.desc.id == id) return &w;
    return nullptr;
}
const AssetWindowManager::Window* AssetWindowManager::Find(const std::string& id) const {
    for (const auto& w : m_windows)
        if (w.desc.id == id) return &w;
    return nullptr;
}

bool AssetWindowManager::Open(AssetWindowDesc desc, const Rect& work) {
    if (Find(desc.id)) {
        Focus(desc.id);
        return false;
    }
    Window w;
    w.rect = {work.x + work.w * 0.1f, work.y + work.h * 0.08f, work.w * 0.8f, work.h * 0.84f};
    w.restore = w.rect;
    w.desc = std::move(desc);
    m_windows.push_back(std::move(w));
    return true;
}

bool AssetWindowManager::Has(const std::string& id) const { return Find(id) != nullptr; }

void AssetWindowManager::Raise(const std::string& id) {
    auto it = std::find_if(m_windows.begin(), m_windows.end(), [&](const Window& w) { return w.desc.id == id; });
    if (it == m_windows.end() || std::next(it) == m_windows.end()) return;
    Window w = std::move(*it);
    m_windows.erase(it);
    m_windows.push_back(std::move(w));
}

void AssetWindowManager::AddToRow(const std::string& id) {
    if (std::find(m_row.begin(), m_row.end(), id) == m_row.end()) m_row.push_back(id);
}

void AssetWindowManager::Focus(const std::string& id) {
    Window* w = Find(id);
    if (!w) return;
    if (w->state == AssetWindowState::Minimized) {
        w->state = w->beforeMinimize;
        if (w->state == AssetWindowState::Floating) std::erase(m_row, id);
    }
    if (w->state == AssetWindowState::Docked) m_active = id;
    else Raise(id);
}

void AssetWindowManager::Minimize(const std::string& id) {
    Window* w = Find(id);
    if (!w || w->state == AssetWindowState::Minimized) return;
    w->beforeMinimize = w->state;
    w->state = AssetWindowState::Minimized;
    AddToRow(id);
    if (m_active == id) m_active.clear();
}

void AssetWindowManager::Dock(const std::string& id) {
    Window* w = Find(id);
    if (!w) return;
    w->state = AssetWindowState::Docked;
    AddToRow(id);
    m_active = id;
}

void AssetWindowManager::Float(const std::string& id) {
    Window* w = Find(id);
    if (!w) return;
    w->state = AssetWindowState::Floating;
    std::erase(m_row, id);
    if (m_active == id) m_active.clear();
    Raise(id);
}

void AssetWindowManager::ToggleMaximize(const std::string& id, const Rect& work) {
    Window* w = Find(id);
    if (!w) return;
    if (w->maximized) {
        w->rect = w->restore;
        w->maximized = false;
    } else {
        w->restore = w->rect;
        w->rect = work;
        w->maximized = true;
    }
}

void AssetWindowManager::Remove(const std::string& id) {
    std::erase(m_row, id);
    if (m_active == id) m_active.clear();
    if (m_dragging == id) m_dragging.clear();
    auto it = std::find_if(m_windows.begin(), m_windows.end(), [&](const Window& w) { return w.desc.id == id; });
    if (it == m_windows.end()) return;
    std::function<void()> closed = std::move(it->desc.onClosed);
    m_windows.erase(it);
    if (closed) closed();
}

void AssetWindowManager::RequestClose(const std::string& id) {
    Window* w = Find(id);
    if (!w) return;
    if (!w->desc.requestClose || w->desc.requestClose()) Remove(id);
}

void AssetWindowManager::Close(const std::string& id) { Remove(id); }

AssetWindowState AssetWindowManager::State(const std::string& id) const {
    const Window* w = Find(id);
    return w ? w->state : AssetWindowState::Floating;
}
bool AssetWindowManager::Maximized(const std::string& id) const {
    const Window* w = Find(id);
    return w && w->maximized;
}
Rect AssetWindowManager::FloatRect(const std::string& id) const {
    const Window* w = Find(id);
    return w ? w->rect : Rect{};
}
std::vector<std::string> AssetWindowManager::Ids() const {
    std::vector<std::string> ids;
    for (const auto& w : m_windows) ids.push_back(w.desc.id);
    return ids;
}
std::vector<std::string> AssetWindowManager::RowTabs() const { return m_row; }
std::string AssetWindowManager::Title(const std::string& id) const {
    const Window* w = Find(id);
    if (!w) return {};
    return w->desc.liveTitle ? w->desc.liveTitle() : w->desc.title;
}

// --- Drawing ----------------------------------------------------------------

void AssetWindowManager::DrawRow(Context& ui, const Rect& row) {
    const Slate::Theme& t = ui.theme();
    m_rowRect = row;
    ui.Panel(row, t.tabWell);
    ui.Panel({row.x, row.Bottom() - 1.0f, row.w, 1.0f}, Color::RGB(8, 8, 8));
    f32 x = row.x + 6.0f;
    const f32 y = row.y + 3.0f, h = row.h - 5.0f;

    // Home: the level editor.
    {
        const Rect r{x, y, 84.0f, h};
        const bool active = HomeActive();
        if (ui.IconTextButton(Context::ID("assetwin.home"), r, Slate::Icon::Scene, "Home", active ? t.accent : t.text,
                              active))
            ShowHome();
        ui.Tooltip(Context::ID("assetwin.home.tip"), r, "Home\nThe level editor (Viewport, Outliner, Inspector...).");
        x += r.w + 8.0f;
    }

    // Tabs of docked / minimized windows.
    std::string clicked, closing;
    for (const std::string& id : m_row) {
        const Window* w = Find(id);
        if (!w) continue;
        const std::string title = Title(id);
        const f32 tw = std::min(ui.uiFont().Measure(title).x + 58.0f, 280.0f);
        const Rect tab{x, y, tw, h};
        const bool active = m_active == id;
        const bool minimized = w->state == AssetWindowState::Minimized;
        const bool hover = tab.Contains(ui.mouse());
        ui.PanelRounded(tab, active ? t.surface3 : (hover ? t.surface2 : t.surface1), 4.0f);
        if (active) ui.drawList().AddRectOutlineRounded(tab, t.accent, 1.5f, 4.0f);
        f32 tx = tab.x + 8.0f;
        if (w->desc.icon) {
            ui.Image(w->desc.icon, {tx, tab.y + (tab.h - 18.0f) * 0.5f, 18.0f, 18.0f});
            tx += 23.0f;
        }
        ui.LabelIn({tx, tab.y, tab.Right() - tx - 22.0f, tab.h}, title, minimized ? t.textDim : t.text);
        const Rect close{tab.Right() - 20.0f, tab.y + (tab.h - 16.0f) * 0.5f, 16.0f, 16.0f};
        if (hover || active) {
            const bool overClose = close.Contains(ui.mouse());
            if (overClose) ui.PanelRounded(close, t.surface4, 3.0f);
            Slate::DrawIcon(ui, close.Inset(3.0f, 3.0f), Slate::Icon::Close, overClose ? t.text : t.textDim);
        }
        if (hover && ui.mousePressed(0) && !ui.inputBlocked()) {
            if (close.Contains(ui.mouse())) {
                closing = id;
            } else {
                clicked = id;
                // Dragging a tab down out of the row floats the window.
                m_dragging = id;
                m_dragFromTab = true;
                m_pressAt = ui.mouse();
                m_dragOffset = {w->rect.w * 0.5f, kTitleH * 0.5f};
            }
        }
        ui.Tooltip(Context::ID("assetwin.tab.tip." + id), tab,
                   minimized ? "Minimized - click to restore" : "Drag down to float this window");
        x += tw + 4.0f;
    }
    if (!clicked.empty()) Focus(clicked);
    if (!closing.empty()) RequestClose(closing);
}

bool AssetWindowManager::DrawDocked(Context& ui, const Rect& area) {
    if (m_active.empty()) return false;
    Window* w = Find(m_active);
    if (!w || w->state != AssetWindowState::Docked) {
        m_active.clear();
        return false;
    }
    if (w->desc.draw) w->desc.draw(ui, area);
    return true;
}

bool AssetWindowManager::CapturesPointer(Vec2 p) const {
    for (const auto& w : m_windows)
        if (w.state == AssetWindowState::Floating && w.rect.Contains(p)) return true;
    return false;
}

void AssetWindowManager::DrawFloating(Context& ui, const Rect& work) {
    const Slate::Theme& t = ui.theme();
    const Vec2 mouse = ui.mouse();

    // Title-bar / tab drag.
    if (!m_dragging.empty()) {
        Window* w = Find(m_dragging);
        if (!w || !ui.isMouseDown(0)) {
            if (w) {
                const bool overRow = m_rowRect.Contains(mouse);
                if (m_dragFromTab) {
                    // Released away from the row (dragged down): float it there.
                    if (!overRow && std::fabs(mouse.y - m_pressAt.y) > kDragThreshold * 4.0f) {
                        w->rect.x = mouse.x - m_dragOffset.x;
                        w->rect.y = mouse.y - m_dragOffset.y;
                        Float(m_dragging);
                    }
                } else if (overRow) {
                    Dock(m_dragging);
                }
            }
            m_dragging.clear();
            m_dragFromTab = false;
        } else if (!m_dragFromTab) {
            if (w->maximized) {  // dragging a maximized window restores it under the pointer
                w->maximized = false;
                const f32 frac = (mouse.x - w->rect.x) / std::max(w->rect.w, 1.0f);
                w->rect = w->restore;
                m_dragOffset.x = w->rect.w * frac;
            }
            w->rect.x = mouse.x - m_dragOffset.x;
            w->rect.y = std::max(mouse.y - m_dragOffset.y, 0.0f);
        }
        if (!m_dragging.empty() && !m_dragFromTab && m_rowRect.Contains(mouse)) {
            // Drop hint: the row lights up.
            ui.drawList().AddRectOutlineRounded(m_rowRect.Inset(1.0f, 1.0f), t.accent, 2.0f, 3.0f);
        }
    }

    // Snapshot ids: callbacks may open / close windows.
    std::vector<std::string> ids;
    for (const auto& w : m_windows)
        if (w.state == AssetWindowState::Floating) ids.push_back(w.desc.id);

    std::string raise, minimize, maximize, close;
    for (usize i = 0; i < ids.size(); ++i) {
        Window* w = Find(ids[i]);
        if (!w) continue;
        if (w->maximized) w->rect = work;
        const Rect r = w->rect;
        // A window above covers the pointer.
        bool covered = false;
        for (usize j = i + 1; j < ids.size(); ++j)
            if (const Window* above = Find(ids[j]); above && above->rect.Contains(mouse)) covered = true;
        PointerHider hide(ui, covered);
        const Vec2 m = ui.mouse();

        ui.drawList().AddRectShadow(r, 8.0f, 0.9f);
        ui.PanelRoundedBordered(r, t.panelBg, Color::RGB(70, 70, 74), 6.0f);
        const Rect title{r.x, r.y, r.w, kTitleH};
        ui.PanelRounded(title, t.surface2, 6.0f);
        ui.Panel({title.x, title.Bottom() - 6.0f, title.w, 6.0f}, t.surface2);  // square bottom corners
        ui.Panel({title.x, title.Bottom() - 1.0f, title.w, 1.0f}, Color::RGB(8, 8, 8));
        f32 tx = title.x + 10.0f;
        if (w->desc.icon) {
            ui.Image(w->desc.icon, {tx, title.y + (title.h - 16.0f) * 0.5f, 16.0f, 16.0f});
            tx += 22.0f;
        }
        ui.LabelIn({tx, title.y, title.w - (tx - title.x) - 3.0f * kButtonW, title.h}, Title(w->desc.id), t.text);

        // Minimize / maximize / close.
        const Rect bClose{title.Right() - kButtonW, title.y, kButtonW, title.h};
        const Rect bMax{bClose.x - kButtonW, title.y, kButtonW, title.h};
        const Rect bMin{bMax.x - kButtonW, title.y, kButtonW, title.h};
        auto button = [&](const Rect& b, Color hoverFill) {
            const bool over = b.Contains(m);
            if (over) ui.Panel(b.Inset(0.0f, 1.0f), hoverFill);
            return over;
        };
        const bool overMin = button(bMin, t.surface4), overMax = button(bMax, t.surface4),
                   overClose = button(bClose, Color::RGB(196, 43, 28));
        const Color glyph = t.text;
        const Vec2 cMin{bMin.x + bMin.w * 0.5f, bMin.y + bMin.h * 0.5f};
        ui.drawList().AddLine({cMin.x - 5.0f, cMin.y + 1.0f}, {cMin.x + 5.0f, cMin.y + 1.0f}, glyph, 1.2f);
        const Vec2 cMax{bMax.x + bMax.w * 0.5f, bMax.y + bMax.h * 0.5f};
        if (w->maximized) {  // restore glyph: two overlapping squares
            ui.drawList().AddRectOutline({cMax.x - 5.0f, cMax.y - 3.0f, 8.0f, 8.0f}, glyph, 1.0f);
            ui.drawList().AddLine({cMax.x - 3.0f, cMax.y - 5.0f}, {cMax.x + 5.0f, cMax.y - 5.0f}, glyph, 1.0f);
            ui.drawList().AddLine({cMax.x + 5.0f, cMax.y - 5.0f}, {cMax.x + 5.0f, cMax.y + 3.0f}, glyph, 1.0f);
        } else {
            ui.drawList().AddRectOutline({cMax.x - 5.0f, cMax.y - 5.0f, 10.0f, 10.0f}, glyph, 1.0f);
        }
        const Vec2 cClose{bClose.x + bClose.w * 0.5f, bClose.y + bClose.h * 0.5f};
        ui.drawList().AddLine({cClose.x - 5.0f, cClose.y - 5.0f}, {cClose.x + 5.0f, cClose.y + 5.0f}, glyph, 1.2f);
        ui.drawList().AddLine({cClose.x + 5.0f, cClose.y - 5.0f}, {cClose.x - 5.0f, cClose.y + 5.0f}, glyph, 1.2f);
        ui.Tooltip(Context::ID("assetwin.min." + w->desc.id), bMin, "Minimize to the window row");
        ui.Tooltip(Context::ID("assetwin.max." + w->desc.id), bMax, w->maximized ? "Restore" : "Maximize");
        ui.Tooltip(Context::ID("assetwin.close." + w->desc.id), bClose, "Close");

        if (r.Contains(m) && ui.mousePressed(0) && !ui.inputBlocked()) {
            raise = w->desc.id;
            if (overMin) minimize = w->desc.id;
            else if (overMax) maximize = w->desc.id;
            else if (overClose) close = w->desc.id;
            else if (title.Contains(m)) {
                if (ui.mouseDoubleClicked(0)) {
                    maximize = w->desc.id;
                } else {
                    m_dragging = w->desc.id;
                    m_dragFromTab = false;
                    m_dragOffset = {m.x - r.x, m.y - r.y};
                    m_pressAt = m;
                }
            }
        }

        // Content.
        const Rect content{r.x + 1.0f, title.Bottom(), r.w - 2.0f, r.h - kTitleH - 1.0f};
        ui.PushClip(content);
        if (w->desc.draw) w->desc.draw(ui, content);
        ui.PopClip();
    }
    if (!raise.empty()) Raise(raise);
    if (!minimize.empty()) Minimize(minimize);
    if (!maximize.empty()) ToggleMaximize(maximize, work);
    if (!close.empty()) RequestClose(close);

    // Window-owned popups above every window.
    for (const auto& w : m_windows)
        if (w.state != AssetWindowState::Minimized && w.desc.drawFloating &&
            (w.state == AssetWindowState::Floating || m_active == w.desc.id))
            w.desc.drawFloating(ui);
}

}  // namespace Luma::Editor
