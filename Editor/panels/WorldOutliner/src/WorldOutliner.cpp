#include "Luma/Editor/Panels/WorldOutliner.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <vector>

#include <map>

#include "Luma/Editor/EditorIcons.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Components.h"
#include "Luma/Scene/EntityTemplates.h"
#include "Luma/Slate/Icons.h"

namespace Luma::Editor::Panels {

using Slate::Align;
using Slate::Color;
using Slate::Icon;
using Slate::Rect;

void WorldOutlinerPanel::Draw(Slate::Context& ui, const Slate::Rect& body,
                              PanelContext& ctx) {
    Slate::Theme& t = ui.theme();

    // --- In-panel header: [+ add] [search........] ------------------------
    // The Place Actors trigger lives on the LEFT of a header strip inside
    // the panel body (next to the entity search), matching the toolbar style
    // of the Content Browser. Right-click on empty list space opens the same
    // menu at the cursor; right-click on a row opens the entity menu.
    const f32 kHeaderH = 30.0f;
    Rect header{body.x, body.y, body.w, kHeaderH};
    ui.GradientRect(header, t.surface2, t.surface1);
    ui.Panel({header.x, header.Bottom() - 1.0f, header.w, 1.0f}, t.separator);

    // "+" add-gameobject button, icon only, left side of the header.
    const f32 kBtn = 20.0f;
    const u64 btnId = Slate::Context::ID("outliner.add");
    Rect btn{header.x + 6.0f, header.y + (kHeaderH - kBtn) * 0.5f, kBtn,
             kBtn};
    {
        bool hovered = btn.Contains(ui.mouse());
        if (hovered) {
            ui.RequestCursor(Luma::CursorShape::Hand);
        }
        if (hovered && ui.mousePressed(0)) m_pressedRow = btnId;
        bool clicked = false;
        if (m_pressedRow == btnId && ui.mouseReleased(0)) {
            if (hovered) clicked = true;
            m_pressedRow = 0;
        }
        f32 hoverT = ui.Animate(btnId ^ 0xA0D0ull, hovered, t.motion.hover);
        if (hoverT > 0.01f) {
            Color fill = t.menu.hoverFill.WithAlpha(
                static_cast<u8>(255.0f * hoverT));
            ui.drawList().AddRectFilledRounded(btn, fill, t.radius.sm);
        }
        const bool menuOpen = ui.IsMenuOpen(btnId);
        if (menuOpen) {
            ui.drawList().AddRectOutline(btn, t.accent, t.border.hairline);
        }
        if (m_createButtonIcon) {
            ui.Image(m_createButtonIcon, {btn.x + 1.0f, btn.y + 1.0f, 18.0f,
                                          18.0f});
        } else {
            Slate::DrawIcon(ui, btn, Icon::Plus, menuOpen ? t.accent : t.textDim);
        }
        if (clicked) {
            Slate::MenuOptions mo;
            mo.searchable = true;
            mo.searchHint = "Search actors";
            ui.OpenMenuBelow(btnId, btn, BuildPlaceActorsMenu(ctx), mo);
        }
        ui.Tooltip(btnId ^ 0x71Full, btn,
                   "Place Actors\nAdd an actor to the scene. Right-click the list for the "
                   "same menu, or a row for actor actions.");
    }

    // Entity search box filling the rest of the header (filters the list).
    Rect searchR{btn.Right() + 6.0f, header.y + 3.0f,
                 header.Right() - btn.Right() - 12.0f, kHeaderH - 6.0f};
    ui.SearchBox(Slate::Context::ID("outliner.entitysearch"), searchR,
                 m_entityFilter, m_texSearchGlass, "Search...");

    // Actor icon shown beside each entity name: the outliner icon of its
    // highest-priority registered component (ComponentDesc::outlinerIcon),
    // tinted like the row's label. Empty when no component provides one.
    auto iconFor = [&](Entity e) { return OutlinerIconFor(*ctx.scene, e); };

    auto view = ctx.scene->Registry().view<const NameComponent>();
    if (view.begin() == view.end()) {
        ui.LabelIn({body.x, body.y + kHeaderH + 8.0f, body.w, 22},
                   "  (no entities)", t.textDim);
        m_listScroll = 0.0f;
    } else {
        // Filter the list by the header search text (case-insensitive
        // substring on the entity name).
        std::string needle;
        for (char c : m_entityFilter) {
            needle += static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
        }
        std::vector<Entity> visible;
        for (Entity e : view) {
            const std::string& name = view.get<const NameComponent>(e).name;
            if (needle.empty()) {
                visible.push_back(e);
                continue;
            }
            std::string ln = name;
            for (char& c : ln) {
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }
            if (ln.find(needle) != std::string::npos) visible.push_back(e);
        }

        if (visible.empty()) {
            ui.LabelIn({body.x, body.y + kHeaderH + 8.0f, body.w, 22},
                       "  No matching entities", t.textDim);
            m_listScroll = 0.0f;
        } else {
            const f32 kIconS = 16.0f;
            // The list lives below the header; it is clipped to its region so
            // scrolled rows can't overlap the header, and shifted up by the
            // scroll offset. The scrollbar is added after the loop once the
            // content height is known.
            const f32 listTop = body.y + kHeaderH + 2.0f;
            Rect listRegion{body.x, listTop, body.w, body.Bottom() - listTop};
            ui.PushClip(listRegion);
            f32 y = listTop - m_listScroll;
            Entity rowMenuEntity = kNullEntity;
            for (Entity e : visible) {
                const std::string& name =
                    view.get<const NameComponent>(e).name;
                Rect row{body.x + 4, y, body.w - 8, 24};
                bool selected = (e == *ctx.selected);
                const u64 rowId =
                    Slate::Context::ID(name.c_str()) ^ 0x0F0F0F0Full;
                bool hovered = row.Contains(ui.mouse()) &&
                               listRegion.Contains(ui.mouse());
                if (hovered) {
                    ui.RequestCursor(Luma::CursorShape::Hand);
                }
                if (hovered && ui.mousePressed(0)) {
                    m_pressedRow = rowId;
                }
                if (hovered && ui.mousePressed(1)) {
                    rowMenuEntity = e;
                }
                bool clicked = false;
                if (m_pressedRow == rowId && ui.mouseReleased(0)) {
                    if (hovered) clicked = true;
                    m_pressedRow = 0;
                }
                if (selected) {
                    ui.drawList().AddRectFilledRounded(row, t.selectionBg,
                                                       t.radius.sm);
                } else if (hovered) {
                    ui.drawList().AddRectFilledRounded(row, t.surface2,
                                                       t.radius.sm);
                }
                const std::string iconName = iconFor(e);
                const bool hasIcon =
                    !iconName.empty() &&
                    DrawNamedIcon(ui,
                                  {row.x + t.space.xs, row.y + (row.h - kIconS) * 0.5f,
                                   kIconS, kIconS},
                                  iconName, selected ? t.selectionText : t.text);
                const f32 textX = hasIcon ? row.x + t.space.xs + kIconS + t.space.sm
                                          : row.x + t.space.md + t.space.xs;
                if (m_renaming == e) {
                    // Inline rename: Enter / focus loss commits, Escape cancels.
                    const u64 fieldId = Slate::Context::ID("outliner.rename");
                    if (m_renameFocusPending) {
                        ui.FocusField(fieldId, m_renameBuf.size());
                        m_renameFocusPending = false;
                    }
                    const bool cancel = ui.keyEscape();
                    Rect field{textX - 4.0f, row.y + 1.0f, row.Right() - textX, row.h - 2.0f};
                    ui.TextField(fieldId, field, m_renameBuf);
                    if (cancel) {
                        ui.BlurField(fieldId);
                        m_renaming = kNullEntity;
                    } else if (!ui.IsFieldFocused(fieldId)) {
                        if (!m_renameBuf.empty() && ctx.scene->IsValid(e)) {
                            ctx.scene->Registry().get<NameComponent>(e).name = m_renameBuf;
                        }
                        m_renaming = kNullEntity;
                    }
                } else {
                    ui.Label({textX,
                              row.y + (row.h - ui.font().LineHeight()) * 0.5f},
                             name, selected ? t.selectionText : t.text);
                }
                if (clicked) *ctx.selected = e;
                y += 26.0f;
            }
            ui.PopClip();
            // Scroll region spans the list area below the header. Content
            // height is recovered from where the row loop ended.
            f32 contentH = (y - listTop) + m_listScroll;
            m_listScroll = ui.VerticalScroll(
                Slate::Context::ID("outliner.list"), listRegion, contentH,
                m_listScroll);

            // Right-click a row: select it and open its entity menu.
            if (rowMenuEntity != kNullEntity) {
                *ctx.selected = rowMenuEntity;
                ui.OpenMenu(Slate::Context::ID("outliner.entitymenu"), ui.mouse(),
                            BuildEntityMenu(rowMenuEntity, ctx));
            }
        }
    }

    // Right-click on empty list space: Place Actors at the cursor.
    const Rect listArea{body.x, body.y + kHeaderH, body.w, body.h - kHeaderH};
    if (ui.mousePressed(1) && listArea.Contains(ui.mouse()) && !ui.AnyMenuOpen()) {
        Slate::MenuOptions mo;
        mo.searchable = true;
        mo.searchHint = "Search actors";
        ui.OpenMenu(Slate::Context::ID("outliner.placemenu"), ui.mouse(),
                    BuildPlaceActorsMenu(ctx), mo);
    }

    // Keyboard shortcuts while the pointer is over the outliner.
    if (body.Contains(ui.mouse()) && !ui.textFieldFocused() &&
        ctx.scene->IsValid(*ctx.selected)) {
        if (ui.keyPressed(291)) {  // F2 (Delete / Ctrl+D / F are editor-wide)
            m_renaming = *ctx.selected;
            m_renameBuf = ctx.scene->Registry().get<NameComponent>(m_renaming).name;
            m_renameFocusPending = true;
        }
    }

}

void WorldOutlinerPanel::FillPlaceActors(
    Slate::MenuBuilder& b, const std::function<void(const std::string&)>& create) {
    // One submenu per template category, in first-registration order.
    std::vector<std::string> categories;
    std::map<std::string, std::vector<const EntityTemplate*>> byCategory;
    for (const EntityTemplate* t : EntityTemplates().All()) {
        const std::string cat = t->category.empty() ? std::string("Other") : t->category;
        if (!byCategory.contains(cat)) categories.push_back(cat);
        byCategory[cat].push_back(t);
    }
    for (const std::string& cat : categories) {
        const std::vector<const EntityTemplate*>& entries = byCategory[cat];
        std::string catIcon;
        for (const EntityTemplate* t : entries) {
            if (!t->categoryIcon.empty()) {
                catIcon = t->categoryIcon;
                break;
            }
        }
        b.AddSubMenu(cat, MenuIconByName(catIcon), [&](Slate::MenuBuilder& m) {
            for (const EntityTemplate* t : entries) {
                Slate::MenuEntryOptions o;
                o.enabled = static_cast<bool>(create);
                o.tooltip = t->tooltip;
                const std::string id = t->id;
                m.AddEntry(t->displayName, MenuIconByName(t->icon),
                           [create, id] {
                               if (create) create(id);
                           },
                           o);
            }
        });
    }
}

std::vector<Slate::MenuEntry> WorldOutlinerPanel::BuildPlaceActorsMenu(PanelContext& ctx) {
    Slate::MenuBuilder b;
    b.BeginSection("Place Actors");
    FillPlaceActors(b, ctx.onCreateEntity);
    return b.Build();
}

std::vector<Slate::MenuEntry> WorldOutlinerPanel::BuildEntityMenu(Entity e,
                                                                  PanelContext& ctx) {
    Scene* scene = ctx.scene;
    const ComponentDesc* single = OnePerSceneComponentOf(*scene, e);
    Slate::MenuBuilder b;

    b.BeginSection("Actor");
    {
        Slate::MenuEntryOptions o;
        o.shortcut = "F2";
        b.AddEntry("Rename", Icon::None,
                   [this, scene, e] {
                       if (!scene->IsValid(e)) return;
                       m_renaming = e;
                       m_renameBuf = scene->Registry().get<NameComponent>(e).name;
                       m_renameFocusPending = true;
                   },
                   o);
    }
    {
        Slate::MenuEntryOptions o;
        o.enabled = static_cast<bool>(ctx.onDuplicateEntity) && !single;
        o.tooltip = single ? "A scene has a single " + single->displayName + "."
                           : "Create a copy of this actor and all its components.";
        o.shortcut = "Ctrl+D";
        auto dup = ctx.onDuplicateEntity;
        b.AddEntry("Duplicate", Icon::Plus,
                   [dup, e] {
                       if (dup) dup(e);
                   },
                   o);
    }
    {
        Slate::MenuEntryOptions o;
        o.shortcut = "Delete";
        o.enabled = static_cast<bool>(ctx.onDeleteEntity);
        auto del = ctx.onDeleteEntity;
        b.AddEntry("Delete", Icon::Trash,
                   [del, e] {
                       if (del) del(e);
                   },
                   o);
    }

    b.BeginSection("View");
    {
        Slate::MenuEntryOptions o;
        o.enabled = static_cast<bool>(ctx.onFocusEntity);
        o.tooltip = "Move the viewport camera to frame this actor.";
        o.shortcut = "F";
        auto focus = ctx.onFocusEntity;
        b.AddEntry("Focus in Viewport", Icon::Eye,
                   [focus, e] {
                       if (focus) focus(e);
                   },
                   o);
    }

    b.BeginSection("Create");
    auto create = ctx.onCreateEntity;
    b.AddSubMenu("Place Actor", Icon::Plus,
                 [this, create](Slate::MenuBuilder& m) { FillPlaceActors(m, create); });
    return b.Build();
}


}  // namespace Luma::Editor::Panels
