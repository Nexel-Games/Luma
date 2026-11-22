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
#include "Luma/Scene/Hierarchy.h"
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
        struct RowItem { Entity entity; int depth; };
        std::vector<RowItem> visible;
        auto matches = [&](Entity entity) {
            if (needle.empty()) return true;
            std::string ln = view.get<const NameComponent>(entity).name;
            for (char& c : ln) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return ln.find(needle) != std::string::npos;
        };
        if (!needle.empty()) {
            for (Entity entity : view) {
                if (matches(entity)) visible.push_back({entity, 0});
            }
        } else {
            std::function<void(Entity, int)> walk = [&](Entity entity, int depth) {
                visible.push_back({entity, depth});
                if (m_collapsed.count(entity) != 0) return;
                for (Entity child : ChildrenOf(*ctx.scene, entity)) walk(child, depth + 1);
            };
            for (Entity entity : view) {
                if (ParentOf(*ctx.scene, entity) == kNullEntity) walk(entity, 0);
            }
        }

        if (visible.empty()) {
            ui.LabelIn({body.x, body.y + kHeaderH + 8.0f, body.w, 22},
                       "  No matching entities", t.textDim);
            m_listScroll = 0.0f;
        } else {
            const f32 kIconS = 16.0f;
            const f32 kRowStep = 19.0f;  // row pitch; stripes are one row tall
            // The list lives below the header; it is clipped to its region so
            // scrolled rows can't overlap the header, and shifted up by the
            // scroll offset. The scrollbar is added after the loop once the
            // content height is known.
            const f32 listTop = body.y + kHeaderH + 2.0f;
            Rect listRegion{body.x, listTop, body.w, body.Bottom() - listTop};
            ui.PushClip(listRegion);
            f32 y = listTop - m_listScroll;
            Entity rowMenuEntity = kNullEntity;
            usize rowIndex = 0;
            for (const RowItem& item : visible) {
                const Entity e = item.entity;
                const f32 indent = static_cast<f32>(item.depth) * 14.0f;
                const std::string& name =
                    view.get<const NameComponent>(e).name;
                // Unreal-style striping: every other row on a darker band,
                // full width and touching, so the rows read as a table.
                if (rowIndex++ % 2 == 0) {
                    ui.drawList().AddRectFilled({body.x, y, body.w, kRowStep}, t.listRowAlt);
                }
                Rect row{body.x + 4, y, body.w - 8, kRowStep};
                bool selected = e == *ctx.selected;
                if (ctx.selection != nullptr) {
                    selected = std::find(ctx.selection->begin(), ctx.selection->end(), e) != ctx.selection->end();
                }
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
                const bool hasChildren = !ChildrenOf(*ctx.scene, e).empty();
                Rect chevron{row.x + indent, row.y, 14.0f, row.h};
                if (hasChildren && clicked && chevron.Contains(ui.mouse())) {
                    if (m_collapsed.count(e) != 0) m_collapsed.erase(e);
                    else m_collapsed.insert(e);
                    clicked = false;
                }
                if (selected) {
                    ui.drawList().AddRectFilledRounded(row, t.selectionBg,
                                                       t.radius.sm);
                } else if (hovered) {
                    ui.drawList().AddRectFilledRounded(row, t.surface2,
                                                       t.radius.sm);
                }
                const std::string iconName = iconFor(e);
                const bool hidden = ctx.isEntityHidden && ctx.isEntityHidden(e);
                const Color iconColor = hidden ? t.textDisabled : (selected ? t.selectionText : t.text);
                const Rect iconR{row.x + indent + 14.0f, row.y + (row.h - kIconS) * 0.5f, kIconS, kIconS};
                if (hasChildren) {
                    Slate::DrawIcon(ui, {chevron.x + 1.0f, chevron.y + 2.0f, 12.0f, 12.0f},
                                    m_collapsed.count(e) ? Icon::ChevronRight : Icon::ChevronDown, t.textDim);
                }
                // The registered image, else its glyph fallback (e.g. a static
                // mesh actor without its icon texture still gets a glyph).
                const bool hasIcon =
                    !iconName.empty() && DrawNamedIcon(ui, iconR, iconName, iconColor);
                const f32 textX = row.x + indent + 14.0f +
                    (hasIcon ? t.space.xs + kIconS + t.space.sm : t.space.sm);
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
                             name, hidden ? t.textDisabled : (selected ? t.selectionText : t.text));
                }
                if (clicked) {
                    *ctx.selected = e;
                    if (ctx.selection != nullptr) {
                        ctx.selection->clear();
                        ctx.selection->push_back(e);
                    }
                }
                y += kRowStep;
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
                const bool keep = ctx.selection != nullptr && ctx.selection->size() > 1 &&
                    std::find(ctx.selection->begin(), ctx.selection->end(), rowMenuEntity) != ctx.selection->end();
                if (!keep) {
                    *ctx.selected = rowMenuEntity;
                    if (ctx.selection != nullptr) {
                        ctx.selection->clear();
                        ctx.selection->push_back(rowMenuEntity);
                    }
                }
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
                    BuildEmptyMenu(ctx), mo);
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

std::vector<Slate::MenuEntry> WorldOutlinerPanel::BuildEmptyMenu(PanelContext& ctx) {
    Slate::MenuBuilder b;
    b.BeginSection("Actor");
    {
        Slate::MenuEntryOptions o;
        o.enabled = static_cast<bool>(ctx.canPasteEntities) && ctx.canPasteEntities();
        o.shortcut = "Ctrl+V";
        o.tooltip = o.enabled ? "Paste copied actors at the scene root." : "The clipboard is empty.";
        auto paste = ctx.onPasteEntities;
        b.AddEntry("Paste", Icon::None, [paste] { if (paste) paste(kNullEntity, false); }, o);
    }
    b.BeginSection("View");
    {
        auto show = ctx.onShowAllActors;
        b.AddEntry("Show All Actors", Icon::Eye, [show] { if (show) show(); });
    }
    b.BeginSection("Create");
    auto create = ctx.onCreateEntity;
    b.AddSubMenu("Place Actor", Icon::Plus,
                 [this, create](Slate::MenuBuilder& m) { FillPlaceActors(m, create); });
    return b.Build();
}

std::vector<Slate::MenuEntry> WorldOutlinerPanel::BuildEntityMenu(Entity e,
                                                                  PanelContext& ctx) {
    Scene* scene = ctx.scene;
    // An entity holding a one-per-scene component (e.g. the Environment)
    // stays unique: it can't be duplicated, cut or copied.
    const ComponentDesc* single = OnePerSceneComponentOf(*scene, e);
    const bool isSingle = single != nullptr;
    const std::string singleName = single ? single->displayName : std::string{};
    const bool canPaste = static_cast<bool>(ctx.canPasteEntities) && ctx.canPasteEntities();
    const bool hasChildren = static_cast<bool>(ctx.entityHasChildren) && ctx.entityHasChildren(e);
    const bool isRoot = !ctx.entityIsRoot || ctx.entityIsRoot(e);
    const bool hasTransform = scene->Registry().all_of<TransformComponent>(e);
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
    auto command = [&](const char* label, const char* shortcut, Icon icon, bool enabled, const std::string& tip,
                       const std::function<void()>& run) {
        Slate::MenuEntryOptions o;
        o.enabled = enabled;
        o.shortcut = shortcut;
        o.tooltip = tip;
        b.AddEntry(label, icon, run, o);
    };
    command("Duplicate", "Ctrl+D", Icon::Duplicate, static_cast<bool>(ctx.onDuplicateEntity) && !isSingle,
            isSingle ? "A scene has a single " + singleName + "." : "Copy this actor and its children.",
            [dup = ctx.onDuplicateEntity, e] { if (dup) dup(e); });
    command("Cut", "Ctrl+X", Icon::None, static_cast<bool>(ctx.onCutEntities) && !isSingle,
            isSingle ? "The " + singleName + " stays in the scene." : "Cut this actor. It moves when you paste.",
            [cut = ctx.onCutEntities, e] { if (cut) cut(e); });
    command("Copy", "Ctrl+C", Icon::None, static_cast<bool>(ctx.onCopyEntities) && !isSingle,
            isSingle ? "The " + singleName + " stays in the scene." : "Copy this actor and its children.",
            [copy = ctx.onCopyEntities, e] { if (copy) copy(e); });
    command("Paste", "Ctrl+V", Icon::None, canPaste && static_cast<bool>(ctx.onPasteEntities),
            canPaste ? "Paste as a sibling of this actor." : "The clipboard is empty.",
            [paste = ctx.onPasteEntities, e] { if (paste) paste(e, false); });
    command("Paste as Child", "", Icon::None, canPaste && static_cast<bool>(ctx.onPasteEntities),
            canPaste ? "Paste under this actor." : "The clipboard is empty.",
            [paste = ctx.onPasteEntities, e] { if (paste) paste(e, true); });
    command("Delete", "Delete", Icon::Trash, static_cast<bool>(ctx.onDeleteEntity),
            "Delete this actor and its children. Assets on disk are kept.",
            [del = ctx.onDeleteEntity, e] { if (del) del(e); });

    b.BeginSection("Hierarchy");
    command("Create Empty Child", "", Icon::Plus, static_cast<bool>(ctx.onCreateChild),
            "Create an empty actor under this one.",
            [create = ctx.onCreateChild, e] { if (create) create(e); });
    command("Select Children", "", Icon::None, hasChildren && static_cast<bool>(ctx.onSelectChildren),
            hasChildren ? "Select every descendant." : "This actor has no children.",
            [select = ctx.onSelectChildren, e] { if (select) select(e); });
    command("Move to Root", "", Icon::None, !isRoot && static_cast<bool>(ctx.onMoveToRoot),
            isRoot ? "This actor is already at the root." : "Unparent this actor. Its world position stays.",
            [move = ctx.onMoveToRoot, e] { if (move) move(e); });

    b.BeginSection("View");
    command("Focus in Viewport", "F", Icon::Focus,
            hasTransform && static_cast<bool>(ctx.onFocusEntity),
            "Move the viewport camera to frame this actor.",
            [focus = ctx.onFocusEntity, e] { if (focus) focus(e); });
    command("Hide Actor", "", Icon::EyeOff, static_cast<bool>(ctx.onHideEntities),
            "Hide this actor in the editor viewport. Saving the scene does not keep this.",
            [hide = ctx.onHideEntities, e] { if (hide) hide(e); });
    command("Show All Actors", "", Icon::Eye, static_cast<bool>(ctx.onShowAllActors),
            "Show every actor hidden in this session.",
            [show = ctx.onShowAllActors] { if (show) show(); });

    b.BeginSection("Create");
    auto create = ctx.onCreateEntity;
    b.AddSubMenu("Place Actor", Icon::Plus,
                 [this, create](Slate::MenuBuilder& m) { FillPlaceActors(m, create); });
    return b.Build();
}


}  // namespace Luma::Editor::Panels
