#pragma once

#include <string>
#include <vector>

#include "Luma/Editor/InspectorDrawers.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Components.h"
#include "PanelContext.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Menu.h"
#include "Luma/Slate/Types.h"

// Inspector panel (Unreal's Details panel) — shows the selected entity's
// components with editable fields.
//
// Everything comes from the runtime registries: the sections are the entity's
// registered components (Components(), registration order), each drawn by its
// InspectorDrawers() entry or, without one, by the default reflection drawer.
// The built-in components register their custom drawers through the same
// registry (see RegisterBuiltinInspectorDrawers). The Add picker lists every
// user-addable registered component by category, honouring one-per-entity and
// one-per-scene rules. Components kept for a missing plugin
// (UnresolvedComponents) show as read-only "missing plugin — data kept" sections
// that can only be removed after a confirmation.
//
// Color rows open floating ColorPicker panels drawn by DrawFloatingPickers, which
// the editor shell calls AFTER the dock so they aren't clipped to this column (it
// also draws the remove-confirmation dialog).

namespace Luma::Editor::Panels {

class InspectorPanel {
public:
    InspectorPanel();

    void Draw(Slate::Context& ui, const Slate::Rect& body, PanelContext& ctx);

    // Widget id of a property row for `entity`: the row key mixed with the
    // entity, so focus / typed text never carries over to another actor.
    static u64 RowId(std::string_view key, Entity entity) {
        return InspectorRows::RowId(key, entity);
    }

    // Draws open floating color pickers and the remove-confirmation dialog.
    // Call after the dock's clip stack is unwound.
    void DrawFloatingPickers(Slate::Context& ui, PanelContext& ctx);

    // Closes any open popup (e.g. after the selection changes).
    void ClosePopups();

    // Material slot rows laid out by the last Draw (drop targets), one per
    // mesh section.
    const std::vector<Slate::Rect>& MaterialSlotRects() const { return m_slotRects; }
    // The Static Mesh row laid out by the last Draw (drop target); empty when
    // the selection has no Mesh Renderer.
    const Slate::Rect& StaticMeshRowRect() const { return m_meshRowRect; }
    // Component ids given a section by the last Draw, in draw order.
    const std::vector<std::string>& LastDrawnSections() const { return m_drawnSections; }

    // "Add" picker (searchable, categorized; unavailable components are listed
    // disabled with the reason as tooltip).
    std::vector<Slate::MenuEntry> BuildAddComponentMenu(Scene* scene, Entity e);
    // Right-click menu on a component header: reset / remove.
    std::vector<Slate::MenuEntry> BuildComponentMenu(Scene* scene, Entity e,
                                                     const ComponentDesc& desc);

    // Removing a component kept for a missing plugin needs a confirmation.
    void RequestRemoveUnresolved(Entity e, std::string componentId);
    void ConfirmRemoveUnresolved(Scene& scene);
    void CancelRemoveUnresolved();
    const std::string& PendingRemoveUnresolved() const { return m_pendingRemoveId; }

private:
    void DrawSectionHeader(Slate::Context& ui, PanelContext& ctx, const Slate::Rect& body,
                           f32& y, f32 x, f32 w, std::string_view title, u64 menuKey,
                           std::vector<Slate::MenuEntry> menu, Slate::Color tick);

    InspectorColorPickers m_pickers;

    // Last entity the inspector drew, used to close popups when the selection
    // changes (the pickers target the selected entity's components).
    Entity m_lastSelected = kNullEntity;

    // Vertical scroll offset of the component content (px). Reset to the top
    // when the selection changes so a new entity starts scrolled at zero.
    f32 m_scroll = 0.0f;

    std::vector<Slate::Rect> m_slotRects;
    Slate::Rect m_meshRowRect{};
    std::vector<std::string> m_drawnSections;

    Entity m_pendingRemoveEntity = kNullEntity;
    std::string m_pendingRemoveId;
    bool m_removeDialogOpen = false;  // the confirmation modal is open in the Context
};

// Registers the Inspector drawers of the built-in components into `registry`
// (Name is hidden: the header shows it). InspectorPanel registers them into the
// process-wide registry on first construction.
RegistrationSet RegisterBuiltinInspectorDrawers(InspectorDrawerRegistry& registry);

}  // namespace Luma::Editor::Panels
