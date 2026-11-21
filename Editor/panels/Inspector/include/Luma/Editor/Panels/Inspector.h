#pragma once

#include "Luma/Scene/Components.h"
#include "PanelContext.h"
#include "Luma/Slate/ColorPickerPopup.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Types.h"

// Inspector panel — shows the selected entity's components with editable
// fields. Reflection-driven: every reflected component (Name, Transform,
// MeshRenderer, Camera, Light, Environment) auto-builds its rows from its
// TypeInfo. The + Component popup lets the user add missing components.
//
// Color properties (light color, sky tint, ground color) are compact
// swatch strips: clicking one opens a floating ColorPicker panel (see
// ColorPickerPopup). The panels are drawn by DrawFloatingPickers, which the
// editor shell calls AFTER the dock so they aren't clipped to this column.

namespace Luma::Editor::Panels {

class InspectorPanel {
public:
    void Draw(Slate::Context& ui, const Slate::Rect& body, PanelContext& ctx);

    // Widget id of a property row for `entity`: the row key mixed with the
    // entity, so focus / typed text never carries over to another actor.
    static u64 RowId(std::string_view key, Entity entity);

    // Draws any open floating color picker panels. Call after the dock's clip
    // stack is unwound so the panels can overflow this column.
    void DrawFloatingPickers(Slate::Context& ui, PanelContext& ctx);

    // Closes any open popup (e.g. after the selection changes).
    void ClosePopups();

    // Material slot rows laid out by the last Draw (drop targets), one per
    // mesh section.
    const std::vector<Slate::Rect>& MaterialSlotRects() const { return m_slotRects; }

    // Components the Details panel knows how to add/remove/reset.
    enum class ComponentKind { Transform, MeshRenderer, Camera, Light,
                               Environment, PostProcess };

private:
    // "Add" picker (searchable, categorized; already-present components
    // are listed disabled with the reason as tooltip).
    std::vector<Slate::MenuEntry> BuildAddComponentMenu(Scene* scene, Entity e);
    // Right-click menu on a component header: reset / remove.
    std::vector<Slate::MenuEntry> BuildComponentMenu(Scene* scene, Entity e,
                                                     ComponentKind kind);

    // Mesh Renderer "Materials" list: one row per section (thumbnail, picker,
    // browse, clear, create instance; drop target), extra slots as Unused.
    void DrawMaterialSlots(Slate::Context& ui, PanelContext& ctx, MeshRendererComponent& mr,
                           f32 x, f32& y, f32 w);

    // Floating color pickers for the color property rows. State lives here so
    // each picker's edit buffers + open state persist across frames.
    Slate::ColorPickerPopup m_lightColorPicker;
    Slate::ColorPickerPopup m_skyTintPicker;
    Slate::ColorPickerPopup m_groundColorPicker;

    // Last entity the inspector drew, used to close popups when the selection
    // changes (the pickers target the selected entity's components).
    Entity m_lastSelected = kNullEntity;

    // Vertical scroll offset of the component content (px). Reset to the top
    // when the selection changes so a new entity starts scrolled at zero.
    f32 m_scroll = 0.0f;

    std::vector<Slate::Rect> m_slotRects;
};

}  // namespace Luma::Editor::Panels
