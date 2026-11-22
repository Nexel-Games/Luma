#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Slate/Context.h"

// Asset editor windows (Unreal's asset-editor tabs): every asset editor (the
// Material Editor, the Material Instance Editor, ...) lives in a window that
// floats over the level editor with minimize / maximize / close buttons, can
// be dragged by its title bar, and docks into the window row above the
// toolbar by being dropped on it. The row starts with Home (the level
// editor); a docked or minimized window shows there as a tab, and a docked
// window fills the editor area while its tab is selected.

namespace Luma::Editor {

enum class AssetWindowState : u8 { Floating, Docked, Minimized };

struct AssetWindowDesc {
    std::string id;     // unique ("material:/Game/Materials/M_Wood")
    std::string title;  // initial title; `liveTitle` (if set) wins each frame
    TextureHandle icon = 0;
    std::function<void(Slate::Context&, const Slate::Rect&)> draw;
    // Popups the window owns (colour pickers, context palettes), drawn above every window.
    std::function<void(Slate::Context&)> drawFloating;
    std::function<std::string()> liveTitle;
    // Close request (title-bar / tab close): true closes now; false keeps the
    // window (e.g. a Save / Don't Save / Cancel prompt is up; the owner calls Close later).
    std::function<bool()> requestClose;
    // Called once the window is gone (owners free the editor here).
    std::function<void()> onClosed;
};

class AssetWindowManager {
public:
    static constexpr f32 kRowH = 28.0f;    // the window row
    static constexpr f32 kTitleH = 30.0f;  // floating window title bar

    // Opens a floating window centred in `workArea` (~80% of it). An id that
    // is already open is focused instead; returns false in that case.
    bool Open(AssetWindowDesc desc, const Slate::Rect& workArea);
    bool Has(const std::string& id) const;
    // Brings a window forward: raises a floating one, selects a docked tab,
    // restores a minimized one to how it was before.
    void Focus(const std::string& id);
    void Minimize(const std::string& id);
    void Dock(const std::string& id);   // into the row; selected
    void Float(const std::string& id);  // out of the row, at its last floating rect
    void ToggleMaximize(const std::string& id, const Slate::Rect& workArea);
    // Close request through the window's requestClose; closes when it agrees.
    void RequestClose(const std::string& id);
    void Close(const std::string& id);  // closes now (e.g. after a prompt resolved)

    // Home = the level editor. Selecting a docked tab leaves Home.
    void ShowHome() { m_active.clear(); }
    bool HomeActive() const { return m_active.empty(); }
    const std::string& ActiveTab() const { return m_active; }

    AssetWindowState State(const std::string& id) const;
    bool Maximized(const std::string& id) const;
    Slate::Rect FloatRect(const std::string& id) const;
    std::vector<std::string> Ids() const;      // bottom to top (z-order)
    std::vector<std::string> RowTabs() const;  // docked + minimized, in row order
    std::string Title(const std::string& id) const;

    // --- Drawing (host order: row, docked-or-level-editor, floating) -------
    void DrawRow(Slate::Context& ui, const Slate::Rect& row);
    // Draws the selected docked window into `area`; false = Home (the host
    // draws the level editor there).
    bool DrawDocked(Slate::Context& ui, const Slate::Rect& area);
    // True when the pointer is over a floating window (the host hides the
    // pointer from what is underneath while drawing it).
    bool CapturesPointer(Slate::Vec2 p) const;
    void DrawFloating(Slate::Context& ui, const Slate::Rect& workArea);

private:
    struct Window {
        AssetWindowDesc desc;
        AssetWindowState state = AssetWindowState::Floating;
        AssetWindowState beforeMinimize = AssetWindowState::Floating;
        Slate::Rect rect{};     // floating rect
        Slate::Rect restore{};  // rect before maximize
        bool maximized = false;
    };
    Window* Find(const std::string& id);
    const Window* Find(const std::string& id) const;
    void Raise(const std::string& id);
    void Remove(const std::string& id);
    void AddToRow(const std::string& id);

    std::vector<Window> m_windows;   // z-order: last is topmost
    std::vector<std::string> m_row;  // tab order of docked / minimized windows
    std::string m_active;            // selected docked tab ("" = Home)
    // Title-bar / tab drag.
    std::string m_dragging;
    bool m_dragFromTab = false;
    Slate::Vec2 m_dragOffset{0, 0};
    Slate::Vec2 m_pressAt{0, 0};
    Slate::Rect m_rowRect{};  // last row rect (drop target)
};

}  // namespace Luma::Editor
