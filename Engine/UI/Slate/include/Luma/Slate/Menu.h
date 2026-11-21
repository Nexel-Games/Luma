#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/RHI/Renderer.h"
#include "Luma/Slate/IconKind.h"
#include "Luma/Slate/Types.h"

// Luma Slate popup menus — the one menu system every editor surface uses
// (context menus, "+ Add" pickers, toolbar drop-downs).
//
// Modeled on Unreal's menu architecture:
//   * MenuBuilder   ~ FMenuBuilder: callers describe a menu declaratively as
//                     sections (headings), entries (icon, label, shortcut
//                     hint, tooltip, check state, enabled state, action) and
//                     nested submenus.
//   * MenuHost      ~ FSlateApplication's menu stack: owns the one open
//                     menu tree, draws it in an overlay pass after every
//                     panel (so a menu is never clipped by, or drawn under,
//                     the panel that opened it), and captures input while
//                     open so clicks never leak into the panels underneath.
//
// A menu is a snapshot: the entries are built when it opens and their
// actions run after the menu closes. Actions must therefore capture what
// they need by value (or point at long-lived objects), never at stack
// locals of the frame that opened the menu.

namespace Luma::Slate {

class Context;
struct Theme;

// Leading glyph of a menu row: a procedural Icon, or a texture (drawn
// tinted like the label) with an optional procedural fallback.
struct MenuIcon {
    Icon glyph = Icon::None;
    TextureHandle texture = 0;
    // Asked every frame the entry is drawn (asset thumbnails that may still
    // be generating); 0 shows `glyph`. Drawn untinted (full colour).
    std::function<TextureHandle()> thumbnail;

    MenuIcon() = default;
    MenuIcon(Icon g) : glyph(g) {}  // NOLINT(google-explicit-constructor)
    static MenuIcon Texture(TextureHandle tex, Icon fallback = Icon::None) {
        MenuIcon m;
        m.glyph = fallback;
        m.texture = tex;
        return m;
    }
    static MenuIcon Thumbnail(std::function<TextureHandle()> provider, Icon fallback = Icon::None) {
        MenuIcon m;
        m.glyph = fallback;
        m.thumbnail = std::move(provider);
        return m;
    }
    bool Empty() const { return glyph == Icon::None && texture == 0 && !thumbnail; }
};

// Optional per-entry presentation/state.
struct MenuEntryOptions {
    std::string tooltip;   // shown after a short hover delay
    std::string shortcut;  // right-aligned key hint (only for wired keys)
    bool enabled = true;
    bool checked = false;  // draws a check mark in the icon column
};

struct MenuEntry {
    enum class Kind : u8 { Action, SubMenu, Heading, Separator };

    Kind kind = Kind::Action;
    std::string label;
    std::string tooltip;
    std::string shortcut;
    MenuIcon icon;
    bool enabled = true;
    bool checked = false;
    std::function<void()> action;    // Action
    std::vector<MenuEntry> children; // SubMenu
};

// Declarative menu description (FMenuBuilder-style). Sections after the
// first are separated by a hairline; a section may have no heading.
class MenuBuilder {
public:
    MenuBuilder& BeginSection(std::string_view heading = {});
    MenuBuilder& EndSection();
    MenuBuilder& AddEntry(std::string_view label, MenuIcon icon,
                          std::function<void()> action,
                          MenuEntryOptions options = {});
    MenuBuilder& AddSubMenu(std::string_view label, MenuIcon icon,
                            const std::function<void(MenuBuilder&)>& fill,
                            std::string_view tooltip = {});
    MenuBuilder& AddSeparator();

    bool Empty() const { return m_entries.empty(); }
    // Returns the built entries and resets the builder.
    std::vector<MenuEntry> Build();

private:
    std::vector<MenuEntry> m_entries;
    bool m_hasSection = false;
};

struct MenuOptions {
    // Adds a search box on top. Typing flattens the whole tree (submenus
    // included) into a filtered result list grouped by where each entry
    // lives — Unreal's searchable "Add" / "Place Actors" pickers.
    bool searchable = false;
    std::string searchHint = "Search";
    f32 minWidth = 0.0f;  // 0 = Theme::menu.minWidth
    // Icon box size; 0 = Theme::menu.iconSize. Asset pickers use ~32 for
    // thumbnails (entry rows grow to fit).
    f32 iconSize = 0.0f;
};

// One search result: the matching action and the path of the submenus /
// section it lives under (e.g. "Lights"), used as its group heading.
struct MenuSearchHit {
    const MenuEntry* entry = nullptr;
    std::string group;
};

// Case-insensitive search over every Action in `entries` (recursing into
// submenus). An entry matches when every whitespace-separated term of
// `query` occurs in its label or its group path. Disabled entries are
// kept (shown disabled) so the user learns why they can't pick them.
std::vector<MenuSearchHit> SearchMenu(const std::vector<MenuEntry>& entries,
                                      std::string_view query);

// The open menu stack. One per Context; at most one menu tree is open at a
// time (opening another closes the first), like Unreal's menu stack.
class MenuHost {
public:
    // Widget id of the menu's search field (keyboard focus while open).
    static constexpr u64 kSearchFieldId = 0x4D454E55534541ull;

    // Opens `entries` at `anchor`. `below` = drop-down below the anchor
    // (flipping above when it doesn't fit); otherwise it opens at the
    // anchor's top-left (a cursor-positioned context menu).
    void Open(u64 owner, const Rect& anchor, bool below,
              std::vector<MenuEntry> entries, const MenuOptions& options,
              bool rightButtonHeld);
    void Close();

    bool IsOpen() const { return m_open; }
    bool IsOpen(u64 owner) const { return m_open && m_owner == owner; }

    // Lays out, handles input for, and draws the open menu. Called by
    // Context::EndFrame with the real (unblocked) input, after every
    // panel has drawn.
    void Draw(Context& ctx);

    // --- Introspection (tests / tooling) --------------------------------
    int LevelCount() const { return static_cast<int>(m_levels.size()); }
    Rect LevelRect(int level) const;
    // Screen rect of the first row in `level` whose label is `label`
    // (Action / SubMenu rows only). Zero rect when absent.
    Rect EntryRect(int level, std::string_view label) const;
    const std::string& SearchText() const { return m_search; }

private:
    struct Row {
        enum class Type : u8 { Search, Heading, Separator, Entry, Empty };
        Type type = Type::Entry;
        const MenuEntry* entry = nullptr;
        std::string text;  // Heading / Empty text
        f32 y = 0.0f;      // relative to the level's content top
        f32 h = 0.0f;
    };
    struct Level {
        const MenuEntry* parent = nullptr;  // null for the root level
        std::vector<Row> rows;
        Rect rect{};
        f32 contentH = 0.0f;
        f32 scroll = 0.0f;
        int focus = -1;     // focused/hovered row index (Entry rows only)
        int openRow = -1;   // row whose submenu is open (-1 = none)
    };

    void BuildRows(Context& ctx, Level& level) const;
    f32 MeasureWidth(Context& ctx, const Level& level) const;
    f32 IconSize(const Theme& t) const;
    f32 EntryRowHeight(const Theme& t) const;
    void PlaceRoot(Context& ctx, Level& level);
    void PlaceChild(Context& ctx, const Level& parent, Level& child) const;
    int RowAt(const Level& level, Vec2 p) const;
    Rect RowRect(const Level& level, int row) const;
    bool Selectable(const Row& row) const;
    int StepFocus(const Level& level, int from, int dir) const;
    void OpenChild(Context& ctx, int level, int row, bool focusFirst);
    void CloseFrom(int level);  // closes levels > level
    void DrawLevel(Context& ctx, int index);
    void DrawTooltip(Context& ctx);

    bool m_open = false;
    u64 m_owner = 0;
    std::vector<MenuEntry> m_root;
    MenuOptions m_options;
    Rect m_anchor{};
    bool m_below = false;
    bool m_rootPlaced = false;  // root position fixed after first layout
    f32 m_rootY = 0.0f;
    std::string m_search;
    std::string m_lastSearch;
    std::vector<MenuSearchHit> m_hits;
    std::vector<Level> m_levels;
    int m_activeLevel = 0;      // keyboard focus level
    bool m_justOpened = false;  // ignore the opening frame's clicks
    bool m_pressInside = false; // left press began inside the menu
    bool m_rightHeld = false;   // opened by a still-held right press
    // Hover intent: a submenu switches only after the pointer rests on a
    // new row briefly, so diagonal moves toward an open submenu don't
    // collapse it.
    int m_intentLevel = -1;
    int m_intentRow = -1;
    f32 m_intentStart = 0.0f;
    // Tooltip hover tracking.
    const MenuEntry* m_tipEntry = nullptr;
    f32 m_tipStart = 0.0f;
    Vec2 m_tipPos{};
};

}  // namespace Luma::Slate
