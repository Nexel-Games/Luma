#include "Luma/Slate/Menu.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "Luma/Slate/Context.h"
#include "Luma/Slate/Icons.h"
#include "Luma/Slate/Theme.h"

namespace Luma::Slate {
namespace {

std::string Lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::vector<std::string> Terms(std::string_view query) {
    std::vector<std::string> terms;
    std::string cur;
    for (char c : query) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) terms.push_back(Lower(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) terms.push_back(Lower(cur));
    return terms;
}

void CollectHits(const std::vector<MenuEntry>& entries, const std::string& path,
                 const std::vector<std::string>& terms,
                 std::vector<MenuSearchHit>& out) {
    std::string heading;  // nearest section heading at this level
    for (const MenuEntry& e : entries) {
        switch (e.kind) {
            case MenuEntry::Kind::Heading:
                heading = e.label;
                break;
            case MenuEntry::Kind::Separator:
                break;
            case MenuEntry::Kind::SubMenu: {
                std::string sub = path.empty() ? e.label : path + " > " + e.label;
                CollectHits(e.children, sub, terms, out);
                break;
            }
            case MenuEntry::Kind::Action: {
                // Every term must occur in the label or the group path, and
                // at least one in the label itself (so a group name alone
                // doesn't pull in its whole contents).
                std::string group = path.empty() ? heading : path;
                const std::string label = Lower(e.label);
                const std::string where = Lower(group);
                bool all = true;
                bool inLabel = false;
                for (const std::string& t : terms) {
                    const bool l = label.find(t) != std::string::npos;
                    inLabel = inLabel || l;
                    if (!l && where.find(t) == std::string::npos) {
                        all = false;
                        break;
                    }
                }
                if (all && inLabel) out.push_back({&e, group});
                break;
            }
        }
    }
}

// Section headings: small, uppercase, letter-spaced (Starship "Menu.Heading").
f32 TrackedWidth(const Font& f, std::string_view text, f32 tracking) {
    f32 w = 0.0f;
    char buf[1];
    for (char c : text) {
        buf[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        w += f.Measure(std::string_view(buf, 1)).x + tracking;
    }
    return w > 0.0f ? w - tracking : 0.0f;
}

void DrawTracked(DrawList& dl, const Font& f, Vec2 pos, std::string_view text,
                 Color color, f32 tracking) {
    char buf[1];
    f32 x = pos.x;
    for (char c : text) {
        buf[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::string_view g(buf, 1);
        dl.AddText(f, {x, pos.y}, g, color);
        x += f.Measure(g).x + tracking;
    }
}

}  // namespace

// --- MenuBuilder -------------------------------------------------------------

MenuBuilder& MenuBuilder::BeginSection(std::string_view heading) {
    if (m_hasSection && !m_entries.empty() &&
        m_entries.back().kind != MenuEntry::Kind::Separator) {
        MenuEntry sep;
        sep.kind = MenuEntry::Kind::Separator;
        m_entries.push_back(std::move(sep));
    }
    m_hasSection = true;
    if (!heading.empty()) {
        MenuEntry h;
        h.kind = MenuEntry::Kind::Heading;
        h.label = std::string(heading);
        m_entries.push_back(std::move(h));
    }
    return *this;
}

MenuBuilder& MenuBuilder::EndSection() { return *this; }

MenuBuilder& MenuBuilder::AddEntry(std::string_view label, MenuIcon icon,
                                   std::function<void()> action,
                                   MenuEntryOptions options) {
    MenuEntry e;
    e.kind = MenuEntry::Kind::Action;
    e.label = std::string(label);
    e.icon = icon;
    e.action = std::move(action);
    e.tooltip = std::move(options.tooltip);
    e.shortcut = std::move(options.shortcut);
    e.enabled = options.enabled;
    e.checked = options.checked;
    m_entries.push_back(std::move(e));
    m_hasSection = true;
    return *this;
}

MenuBuilder& MenuBuilder::AddSubMenu(std::string_view label, MenuIcon icon,
                                     const std::function<void(MenuBuilder&)>& fill,
                                     std::string_view tooltip) {
    MenuBuilder sub;
    if (fill) fill(sub);
    MenuEntry e;
    e.kind = MenuEntry::Kind::SubMenu;
    e.label = std::string(label);
    e.icon = icon;
    e.tooltip = std::string(tooltip);
    e.children = sub.Build();
    // A submenu with nothing (or only headings) to pick is shown disabled.
    e.enabled = std::any_of(e.children.begin(), e.children.end(),
                            [](const MenuEntry& c) {
                                return c.kind == MenuEntry::Kind::Action ||
                                       c.kind == MenuEntry::Kind::SubMenu;
                            });
    m_entries.push_back(std::move(e));
    m_hasSection = true;
    return *this;
}

MenuBuilder& MenuBuilder::AddSeparator() {
    if (!m_entries.empty() &&
        m_entries.back().kind != MenuEntry::Kind::Separator) {
        MenuEntry sep;
        sep.kind = MenuEntry::Kind::Separator;
        m_entries.push_back(std::move(sep));
    }
    return *this;
}

std::vector<MenuEntry> MenuBuilder::Build() {
    // Drop dangling separators at either end.
    while (!m_entries.empty() &&
           m_entries.back().kind == MenuEntry::Kind::Separator) {
        m_entries.pop_back();
    }
    while (!m_entries.empty() &&
           m_entries.front().kind == MenuEntry::Kind::Separator) {
        m_entries.erase(m_entries.begin());
    }
    m_hasSection = false;
    return std::move(m_entries);
}

std::vector<MenuSearchHit> SearchMenu(const std::vector<MenuEntry>& entries,
                                      std::string_view query) {
    std::vector<MenuSearchHit> hits;
    std::vector<std::string> terms = Terms(query);
    if (terms.empty()) return hits;
    CollectHits(entries, {}, terms, hits);
    // Group by location (stable: preserves menu order inside a group), and
    // float groups holding a label-prefix match to the top.
    const std::string& first = terms.front();
    auto groupScore = [&](const std::string& group) {
        for (const MenuSearchHit& h : hits) {
            if (h.group == group && Lower(h.entry->label).rfind(first, 0) == 0) {
                return 0;
            }
        }
        return 1;
    };
    std::vector<std::string> order;
    for (const MenuSearchHit& h : hits) {
        if (std::find(order.begin(), order.end(), h.group) == order.end()) {
            order.push_back(h.group);
        }
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](const std::string& a, const std::string& b) {
                         return groupScore(a) < groupScore(b);
                     });
    std::vector<MenuSearchHit> sorted;
    sorted.reserve(hits.size());
    for (const std::string& g : order) {
        for (const MenuSearchHit& h : hits) {
            if (h.group == g) sorted.push_back(h);
        }
    }
    return sorted;
}

// --- MenuHost ----------------------------------------------------------------

void MenuHost::Open(u64 owner, const Rect& anchor, bool below,
                    std::vector<MenuEntry> entries, const MenuOptions& options,
                    bool rightButtonHeld) {
    Close();
    m_open = true;
    m_owner = owner;
    m_root = std::move(entries);
    m_options = options;
    m_anchor = anchor;
    m_below = below;
    m_levels.emplace_back();
    m_rootPlaced = false;
    m_justOpened = true;
    m_rightHeld = rightButtonHeld;
}

void MenuHost::Close() {
    m_open = false;
    m_owner = 0;
    m_root.clear();
    m_levels.clear();
    m_hits.clear();
    m_search.clear();
    m_lastSearch.clear();
    m_activeLevel = 0;
    m_justOpened = false;
    m_pressInside = false;
    m_rightHeld = false;
    m_intentLevel = m_intentRow = -1;
    m_tipEntry = nullptr;
}

Rect MenuHost::LevelRect(int level) const {
    if (level < 0 || level >= LevelCount()) return {};
    return m_levels[static_cast<usize>(level)].rect;
}

Rect MenuHost::EntryRect(int level, std::string_view label) const {
    if (level < 0 || level >= LevelCount()) return {};
    const Level& L = m_levels[static_cast<usize>(level)];
    for (usize i = 0; i < L.rows.size(); ++i) {
        const Row& r = L.rows[i];
        if (r.type == Row::Type::Entry && r.entry && r.entry->label == label) {
            return RowRect(L, static_cast<int>(i));
        }
    }
    return {};
}

void MenuHost::BuildRows(Context& ctx, Level& level) const {
    const Theme& t = ctx.theme();
    level.rows.clear();
    f32 y = t.menu.padY;
    auto push = [&](Row::Type type, const MenuEntry* e, std::string text, f32 h) {
        Row r;
        r.type = type;
        r.entry = e;
        r.text = std::move(text);
        r.y = y;
        r.h = h;
        level.rows.push_back(std::move(r));
        y += h;
    };

    const bool root = (level.parent == nullptr);
    if (root && m_options.searchable) {
        push(Row::Type::Search, nullptr, {}, t.menu.searchH + 12.0f);
    }
    if (root && !m_search.empty()) {
        if (m_hits.empty()) {
            push(Row::Type::Empty, nullptr, "No results for \"" + m_search + "\"",
                 t.menu.rowH);
        }
        const std::string* group = nullptr;
        for (const MenuSearchHit& h : m_hits) {
            if (!group || *group != h.group) {
                if (group) push(Row::Type::Separator, nullptr, {}, t.menu.separatorH);
                if (!h.group.empty()) {
                    push(Row::Type::Heading, nullptr, h.group, t.menu.headingH);
                }
                group = &h.group;
            }
            push(Row::Type::Entry, h.entry, {}, t.menu.rowH);
        }
    } else {
        const std::vector<MenuEntry>& list = root ? m_root : level.parent->children;
        for (const MenuEntry& e : list) {
            switch (e.kind) {
                case MenuEntry::Kind::Heading:
                    push(Row::Type::Heading, &e, e.label, t.menu.headingH);
                    break;
                case MenuEntry::Kind::Separator:
                    push(Row::Type::Separator, &e, {}, t.menu.separatorH);
                    break;
                default:
                    push(Row::Type::Entry, &e, {}, t.menu.rowH);
                    break;
            }
        }
        if (list.empty()) {
            push(Row::Type::Empty, nullptr, "No actions available", t.menu.rowH);
        }
    }
    level.contentH = y + t.menu.padY;
}

f32 MenuHost::MeasureWidth(Context& ctx, const Level& level) const {
    const Theme& t = ctx.theme();
    const bool root = (level.parent == nullptr);
    f32 w = root ? (m_options.minWidth > 0.0f ? m_options.minWidth : t.menu.minWidth)
                 : t.menu.subMinWidth;
    const f32 labelX = t.menu.padX + t.menu.iconSize + 8.0f;
    for (const Row& r : level.rows) {
        f32 rw = 0.0f;
        switch (r.type) {
            case Row::Type::Entry: {
                rw = labelX + ctx.uiFont().Measure(r.entry->label).x;
                if (!r.entry->shortcut.empty()) {
                    rw += 28.0f + ctx.smallFont().Measure(r.entry->shortcut).x;
                }
                rw += (r.entry->kind == MenuEntry::Kind::SubMenu) ? 34.0f
                                                                 : t.menu.padX + 8.0f;
                break;
            }
            case Row::Type::Heading:
                rw = t.menu.padX * 2.0f +
                     TrackedWidth(ctx.smallFont(), r.text, t.menu.headingTracking);
                break;
            case Row::Type::Empty:
                rw = t.menu.padX * 2.0f + ctx.uiFont().Measure(r.text).x;
                break;
            default:
                break;
        }
        w = std::max(w, rw);
    }
    return std::min(w, std::max(t.menu.minWidth, ctx.displayWidth() * 0.5f));
}

void MenuHost::PlaceRoot(Context& ctx, Level& level) {
    const f32 dw = ctx.displayWidth();
    const f32 dh = ctx.displayHeight();
    const f32 margin = 4.0f;
    const f32 w = MeasureWidth(ctx, level);
    const f32 h = std::min(level.contentH, std::max(40.0f, dh - margin * 2.0f));
    f32 x = m_anchor.x;
    f32 y = m_rootY;
    if (!m_rootPlaced) {
        // First placement decides the side; afterwards the top edge stays
        // put while the content (search results) grows or shrinks.
        y = m_below ? m_anchor.Bottom() + 2.0f : m_anchor.y + 1.0f;
        if (y + h > dh - margin) {
            // Drop-downs flip above their anchor; cursor menus slide up.
            y = m_below ? m_anchor.y - h - 2.0f : dh - margin - h;
        }
        if (m_below && y < margin) y = std::max(margin, dh - margin - h);
        y = std::max(margin, y);
        m_rootY = y;
        m_rootPlaced = true;
    }
    if (y + h > dh - margin) y = std::max(margin, dh - margin - h);
    if (x + w > dw - margin) x = dw - margin - w;
    x = std::max(margin, x);
    level.rect = {x, y, w, h};
    level.scroll = std::clamp(level.scroll, 0.0f, std::max(0.0f, level.contentH - h));
}

void MenuHost::PlaceChild(Context& ctx, const Level& parent, Level& child) const {
    const Theme& t = ctx.theme();
    const f32 dw = ctx.displayWidth();
    const f32 dh = ctx.displayHeight();
    const f32 margin = 4.0f;
    const f32 w = MeasureWidth(ctx, child);
    const f32 h = std::min(child.contentH, std::max(40.0f, dh - margin * 2.0f));
    Rect row = RowRect(parent, parent.openRow);
    // Open to the right, overlapping the parent's border; flip left when
    // the screen edge is in the way.
    f32 x = parent.rect.Right() - 2.0f;
    if (x + w > dw - margin) x = std::max(margin, parent.rect.x - w + 2.0f);
    f32 y = row.y - t.menu.padY;
    if (y + h > dh - margin) y = dh - margin - h;
    y = std::max(margin, y);
    child.rect = {x, y, w, h};
    child.scroll = std::clamp(child.scroll, 0.0f, std::max(0.0f, child.contentH - h));
}

Rect MenuHost::RowRect(const Level& level, int row) const {
    if (row < 0 || row >= static_cast<int>(level.rows.size())) return {};
    const Row& r = level.rows[static_cast<usize>(row)];
    return {level.rect.x + 1.0f, level.rect.y + r.y - level.scroll,
            level.rect.w - 2.0f, r.h};
}

int MenuHost::RowAt(const Level& level, Vec2 p) const {
    if (!level.rect.Contains(p)) return -1;
    const f32 py = p.y - level.rect.y + level.scroll;
    for (usize i = 0; i < level.rows.size(); ++i) {
        const Row& r = level.rows[i];
        if (py >= r.y && py < r.y + r.h) return static_cast<int>(i);
    }
    return -1;
}

bool MenuHost::Selectable(const Row& row) const {
    return row.type == Row::Type::Entry && row.entry && row.entry->enabled;
}

int MenuHost::StepFocus(const Level& level, int from, int dir) const {
    const int n = static_cast<int>(level.rows.size());
    if (n == 0) return -1;
    int i = from;
    for (int step = 0; step < n; ++step) {
        i += dir;
        if (i < 0) i = n - 1;
        if (i >= n) i = 0;
        if (Selectable(level.rows[static_cast<usize>(i)])) return i;
    }
    return -1;
}

void MenuHost::CloseFrom(int level) {
    if (level < 0 || level >= LevelCount()) return;
    m_levels.resize(static_cast<usize>(level) + 1);
    m_levels[static_cast<usize>(level)].openRow = -1;
    if (m_activeLevel > level) m_activeLevel = level;
}

void MenuHost::OpenChild(Context& ctx, int level, int row, bool focusFirst) {
    CloseFrom(level);
    Level& parent = m_levels[static_cast<usize>(level)];
    const Row& r = parent.rows[static_cast<usize>(row)];
    if (!r.entry || r.entry->kind != MenuEntry::Kind::SubMenu || !r.entry->enabled) {
        return;
    }
    parent.openRow = row;
    Level child;
    child.parent = r.entry;
    BuildRows(ctx, child);
    PlaceChild(ctx, parent, child);
    if (focusFirst) {
        child.focus = StepFocus(child, -1, +1);
        m_activeLevel = level + 1;
    }
    m_levels.push_back(std::move(child));
}

void MenuHost::Draw(Context& ctx) {
    if (!m_open) return;
    const Theme& t = ctx.theme();
    const Vec2 m = ctx.mouse();
    const f32 now = ctx.time();

    // --- Search state ----------------------------------------------------
    if (m_options.searchable && !ctx.IsFieldFocused(kSearchFieldId)) {
        ctx.FocusField(kSearchFieldId, m_search.size());
    }
    const bool searchChanged = (m_search != m_lastSearch);
    if (searchChanged) {
        m_lastSearch = m_search;
        m_hits = SearchMenu(m_root, m_search);
        CloseFrom(0);
        m_levels[0].scroll = 0.0f;
        m_levels[0].focus = -1;
        m_activeLevel = 0;
        m_tipEntry = nullptr;  // results moved under the pointer
    }

    // --- Layout (rows are rebuilt each frame; focus/scroll persist) ------
    for (usize i = 0; i < m_levels.size(); ++i) {
        Level& L = m_levels[i];
        BuildRows(ctx, L);
        if (i == 0) {
            PlaceRoot(ctx, L);
        } else {
            PlaceChild(ctx, m_levels[i - 1], L);
        }
        if (L.focus >= static_cast<int>(L.rows.size())) L.focus = -1;
    }
    if (searchChanged && !m_search.empty()) {
        m_levels[0].focus = StepFocus(m_levels[0], -1, +1);
    }

    // --- Pointer -----------------------------------------------------------
    int hitLevel = -1;
    for (int i = LevelCount() - 1; i >= 0; --i) {
        if (m_levels[static_cast<usize>(i)].rect.Contains(m)) {
            hitLevel = i;
            break;
        }
    }
    const bool anyPress = ctx.mousePressed(0) || ctx.mousePressed(1) ||
                          ctx.mousePressed(2);
    if (!m_justOpened && anyPress && hitLevel < 0) {
        Close();  // outside click dismisses (and is consumed)
        return;
    }
    // The press that opened the menu does not count: a long list can be
    // placed over its anchor, under the cursor, and the release of that same
    // click must not pick the row it lands on.
    if (ctx.mousePressed(0) && hitLevel >= 0 && !m_justOpened) m_pressInside = true;

    const bool moved = ctx.mouseDelta().x != 0.0f || ctx.mouseDelta().y != 0.0f;
    const MenuEntry* hoveredEntry = nullptr;
    int hoveredRow = -1;
    if (hitLevel >= 0) {
        Level& L = m_levels[static_cast<usize>(hitLevel)];
        if (ctx.scrollDelta() != 0.0f && L.contentH > L.rect.h) {
            L.scroll = std::clamp(L.scroll - ctx.scrollDelta() * t.menu.rowH * 2.0f,
                                  0.0f, L.contentH - L.rect.h);
        }
        hoveredRow = RowAt(L, m);
        const Row* row = hoveredRow >= 0 ? &L.rows[static_cast<usize>(hoveredRow)]
                                         : nullptr;
        if (row && row->type == Row::Type::Entry) {
            hoveredEntry = row->entry;
            if (moved || m_justOpened) {
                L.focus = row->entry->enabled ? hoveredRow : -1;
                m_activeLevel = hitLevel;
                if (hitLevel + 1 < LevelCount() && L.openRow != hoveredRow) {
                    // keyboard focus returns to this level
                    m_levels[static_cast<usize>(hitLevel) + 1].focus = -1;
                }
            }
            // Hover intent: switch/close submenus after a short rest.
            if (L.openRow != hoveredRow) {
                if (m_intentLevel != hitLevel || m_intentRow != hoveredRow) {
                    m_intentLevel = hitLevel;
                    m_intentRow = hoveredRow;
                    m_intentStart = now;
                }
                const bool immediate = (L.openRow < 0);
                if (immediate || now - m_intentStart >= t.menu.submenuDelay) {
                    if (row->entry->kind == MenuEntry::Kind::SubMenu &&
                        row->entry->enabled) {
                        OpenChild(ctx, hitLevel, hoveredRow, false);
                    } else if (L.openRow >= 0) {
                        CloseFrom(hitLevel);
                    }
                }
            } else {
                m_intentLevel = m_intentRow = -1;
            }
        }
    }

    // Tooltip tracking (keyboard activity hides it until the pointer rests).
    if (ctx.keyDown() || ctx.keyUp() || searchChanged) {
        m_tipEntry = hoveredEntry;
        m_tipStart = now + 10.0f;
    } else if (hoveredEntry != m_tipEntry || moved) {
        m_tipEntry = hoveredEntry;
        m_tipStart = now;
        m_tipPos = m;
    }

    auto activate = [&](int level, int rowIndex) {
        const Row& row = m_levels[static_cast<usize>(level)].rows[static_cast<usize>(rowIndex)];
        if (!row.entry || !row.entry->enabled) return;
        if (row.entry->kind == MenuEntry::Kind::SubMenu) {
            OpenChild(ctx, level, rowIndex, true);
            return;
        }
        std::function<void()> fn = row.entry->action;  // copy: Close() frees it
        Close();
        if (fn) fn();
    };

    if (hitLevel >= 0 && hoveredRow >= 0) {
        const Row& row = m_levels[static_cast<usize>(hitLevel)].rows[static_cast<usize>(hoveredRow)];
        if (row.type == Row::Type::Entry) {
            if (ctx.mouseReleased(0) && m_pressInside) {
                activate(hitLevel, hoveredRow);
                if (!m_open) return;
            } else if (ctx.mouseReleased(1) && m_rightHeld && !m_justOpened &&
                       row.entry->kind == MenuEntry::Kind::Action) {
                // Press-drag-release on a right-click context menu.
                activate(hitLevel, hoveredRow);
                if (!m_open) return;
            }
        }
    }
    if (ctx.mouseReleased(0)) m_pressInside = false;
    if (ctx.mouseReleased(1)) m_rightHeld = false;

    // --- Keyboard ----------------------------------------------------------
    m_activeLevel = std::clamp(m_activeLevel, 0, LevelCount() - 1);
    {
        const bool fieldOwnsCaret = !m_search.empty();
        const int a = m_activeLevel;
        auto& A = m_levels[static_cast<usize>(a)];
        if (ctx.keyDown()) A.focus = StepFocus(A, A.focus, +1);
        if (ctx.keyUp()) A.focus = StepFocus(A, A.focus < 0 ? 0 : A.focus, -1);
        if ((ctx.keyDown() || ctx.keyUp()) && A.focus >= 0) {
            // Keep the focused row scrolled into view.
            const Row& r = A.rows[static_cast<usize>(A.focus)];
            if (r.y - A.scroll < 0.0f) A.scroll = r.y;
            if (r.y + r.h - A.scroll > A.rect.h) A.scroll = r.y + r.h - A.rect.h;
        }
        if (ctx.keyRight() && !fieldOwnsCaret && A.focus >= 0) {
            const Row& r = A.rows[static_cast<usize>(A.focus)];
            if (r.entry && r.entry->kind == MenuEntry::Kind::SubMenu) {
                OpenChild(ctx, a, A.focus, true);
            }
        } else if (ctx.keyLeft() && !fieldOwnsCaret && a > 0) {
            CloseFrom(a - 1);
            m_activeLevel = a - 1;
        }
        if (ctx.enterPressed()) {
            const int f = m_levels[static_cast<usize>(m_activeLevel)].focus;
            if (f >= 0) {
                activate(m_activeLevel, f);
                if (!m_open) return;
            }
        }
        if (ctx.keyEscape()) {
            if (!m_search.empty()) {
                m_search.clear();
            } else if (m_activeLevel > 0) {
                CloseFrom(m_activeLevel - 1);
                m_activeLevel -= 1;
            } else {
                Close();
                return;
            }
        }
    }

    // --- Draw ----------------------------------------------------------------
    for (int i = 0; i < LevelCount(); ++i) DrawLevel(ctx, i);
    DrawTooltip(ctx);
    m_justOpened = false;
}

void MenuHost::DrawLevel(Context& ctx, int index) {
    const Theme& t = ctx.theme();
    DrawList& dl = ctx.drawList();
    Level& L = m_levels[static_cast<usize>(index)];
    const Rect r = L.rect;

    dl.AddRectFilledRounded(r, t.menu.outline, t.menu.radius);
    const Rect inner = r.Inset(1.0f, 1.0f);
    dl.AddRectFilledRounded(inner, t.menu.background,
                            std::max(0.0f, t.menu.radius - 1.0f));
    dl.PushClip(inner);

    const Font& ui = ctx.uiFont();
    const Font& small = ctx.smallFont();
    for (usize k = 0; k < L.rows.size(); ++k) {
        const Row& row = L.rows[k];
        const Rect rr = RowRect(L, static_cast<int>(k));
        if (rr.Bottom() < inner.y || rr.y > inner.Bottom()) continue;
        switch (row.type) {
            case Row::Type::Search: {
                Rect field{rr.x + 7.0f, rr.y + 6.0f, rr.w - 14.0f, t.menu.searchH};
                ctx.SearchBox(kSearchFieldId, field, m_search, m_options.searchHint);
                break;
            }
            case Row::Type::Heading: {
                Vec2 p{rr.x + t.menu.padX - 1.0f,
                       rr.y + (rr.h - small.LineHeight()) * 0.5f + 2.0f};
                DrawTracked(dl, small, p, row.text, t.menu.headingText,
                            t.menu.headingTracking);
                break;
            }
            case Row::Type::Separator:
                dl.AddRectFilled({rr.x + 8.0f, std::floor(rr.y + rr.h * 0.5f),
                                  rr.w - 16.0f, 1.0f},
                                 t.menu.separator);
                break;
            case Row::Type::Empty:
                dl.AddText(ui, {rr.x + t.menu.padX, rr.y + (rr.h - ui.LineHeight()) * 0.5f},
                           row.text, t.textDim);
                break;
            case Row::Type::Entry: {
                const MenuEntry& e = *row.entry;
                const bool focused = (L.focus == static_cast<int>(k)) && e.enabled;
                const bool openParent = (L.openRow == static_cast<int>(k));
                const bool highlight = focused && !(openParent && m_activeLevel > index);
                if (highlight) {
                    dl.AddRectFilled(rr, t.menu.highlightFill);
                } else if (openParent) {
                    dl.AddRectFilled(rr, t.menu.hoverFill);
                }
                const Color text = !e.enabled ? t.textDisabled
                                   : highlight ? t.menu.highlightText
                                               : t.text;
                const Color icon = !e.enabled ? t.textDisabled
                                   : highlight ? t.menu.highlightText
                                               : t.menu.iconTint;
                const f32 is = t.menu.iconSize;
                Rect ib{rr.x + t.menu.padX - 2.0f, rr.y + (rr.h - is) * 0.5f, is, is};
                if (e.checked) {
                    DrawIcon(ctx, ib, Icon::Check, highlight ? t.menu.highlightText : t.accent);
                } else if (e.icon.texture) {
                    dl.AddImage(e.icon.texture, ib, {0.0f, 0.0f, 1.0f, 1.0f}, icon);
                } else if (e.icon.glyph != Icon::None) {
                    DrawIcon(ctx, ib, e.icon.glyph, icon);
                }
                const f32 labelX = rr.x + t.menu.padX + is + 6.0f;
                dl.AddText(ui, {labelX, rr.y + (rr.h - ui.LineHeight()) * 0.5f},
                           e.label, text);
                f32 right = rr.Right() - t.menu.padX;
                if (e.kind == MenuEntry::Kind::SubMenu) {
                    DrawIcon(ctx, {rr.Right() - 22.0f, rr.y + (rr.h - 12.0f) * 0.5f, 12.0f, 12.0f},
                             Icon::ChevronRight, icon);
                    right -= 16.0f;
                }
                if (!e.shortcut.empty()) {
                    Vec2 s = small.Measure(e.shortcut);
                    Color sc = highlight ? t.menu.highlightText.WithAlpha(210)
                                         : t.menu.shortcutText;
                    dl.AddText(small, {right - s.x, rr.y + (rr.h - small.LineHeight()) * 0.5f},
                               e.shortcut, sc);
                }
                break;
            }
        }
    }
    dl.PopClip();

    // Overflow scrollbar (thin, Starship-style).
    if (L.contentH > r.h + 0.5f) {
        const f32 trackH = r.h - 8.0f;
        const f32 thumbH = std::max(18.0f, trackH * (r.h / L.contentH));
        const f32 maxScroll = L.contentH - r.h;
        const f32 ty = r.y + 4.0f + (trackH - thumbH) * (L.scroll / maxScroll);
        dl.AddRectFilledRounded({r.Right() - 6.0f, ty, 3.0f, thumbH},
                                t.textDim.WithAlpha(150), 1.5f);
    }
}

void MenuHost::DrawTooltip(Context& ctx) {
    if (!m_tipEntry || m_tipEntry->tooltip.empty()) return;
    const Theme& t = ctx.theme();
    if (ctx.time() - m_tipStart < t.menu.tooltipDelay) return;

    // Greedy word wrap to the tooltip width.
    const Font& f = ctx.uiFont();
    std::vector<std::string> lines;
    std::string line;
    std::string word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && f.Measure(candidate).x > t.menu.tooltipMaxW) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char c : m_tipEntry->tooltip) {
        if (c == ' ' || c == '\n') {
            flushWord();
            if (c == '\n') {
                lines.push_back(line);
                line.clear();
            }
        } else {
            word.push_back(c);
        }
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);

    f32 w = 0.0f;
    for (const std::string& l : lines) w = std::max(w, f.Measure(l).x);
    const f32 pad = 8.0f;
    const f32 lh = f.LineHeight();
    Rect box{m_tipPos.x + 14.0f, m_tipPos.y + 20.0f, w + pad * 2.0f,
             lh * static_cast<f32>(lines.size()) + pad * 1.5f};
    if (box.Right() > ctx.displayWidth() - 4.0f) box.x = ctx.displayWidth() - 4.0f - box.w;
    if (box.Bottom() > ctx.displayHeight() - 4.0f) box.y = m_tipPos.y - box.h - 8.0f;
    box.x = std::max(4.0f, box.x);
    box.y = std::max(4.0f, box.y);

    DrawList& dl = ctx.drawList();
    dl.AddRectFilledRounded(box, t.menu.outline, t.radius.sm);
    dl.AddRectFilledRounded(box.Inset(1.0f, 1.0f), t.surface0, t.radius.sm);
    f32 y = box.y + pad * 0.75f;
    for (const std::string& l : lines) {
        dl.AddText(f, {box.x + pad, y}, l, t.text);
        y += lh;
    }
}

}  // namespace Luma::Slate
