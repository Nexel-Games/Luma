#pragma once

#include <array>
#include <optional>
#include <string>

#include "Luma/Editor/Panels/MaterialEditModel.h"
#include "Luma/Slate/ColorPickerPopup.h"
#include "Luma/Slate/Context.h"
#include "PanelContext.h"

namespace Luma::Editor::Panels {

// The Material Editor: one dockable tab per open Material / Material
// Instance. Toolbar (Save, Browse to Asset, preview shape, sky backdrop), a
// live orbitable preview, and Details: Material Settings plus one section per
// input (constant, texture slot, channel, tint, tiling/offset, normal
// strength/flip, emissive intensity). Instances add a Parent picker and an
// override checkbox per row; inherited rows are shown dimmed. Every edit
// applies live (preview and level viewport) and marks the asset dirty.
class MaterialEditorPanel {
public:
    explicit MaterialEditorPanel(std::string assetPath);

    // Binds the library before the first Draw (so IsDirty works for a tab
    // that has not been drawn yet).
    void Bind(Material::MaterialLibrary& library);

    void Draw(Slate::Context& ui, const Slate::Rect& rect, PanelContext& ctx);
    // Color pickers float above the dock (call after the dock, unclipped).
    void DrawFloatingPickers(Slate::Context& ui);

    bool IsDirty() const;
    // Call before the dock draws: tabs not drawn this frame are not hovered.
    void BeginFrame() { m_drawnRect = {}; }
    // The pointer is over this tab's content (drawn this frame).
    bool Hovered(const Slate::Context& ui) const {
        return m_drawnRect.w > 0.0f && m_drawnRect.Contains(ui.rawMouse());
    }
    const std::string& AssetPath() const { return m_path; }
    // Tab title: the asset name, "*" appended while dirty.
    std::string Title() const;
    bool Save(std::string* error);
    void ClosePopups();

private:
    struct Layout;  // per-frame cursor (defined in the .cpp)

    void DrawToolbar(Slate::Context& ui, const Slate::Rect& bar, PanelContext& ctx);
    void DrawPreview(Slate::Context& ui, const Slate::Rect& area, PanelContext& ctx);
    void DrawDetails(Slate::Context& ui, const Slate::Rect& area, PanelContext& ctx);
    void DrawParentRow(Slate::Context& ui, Layout& l, PanelContext& ctx);
    void DrawSettings(Slate::Context& ui, Layout& l);
    void DrawInput(Slate::Context& ui, Layout& l, PanelContext& ctx,
                   Material::MaterialParameter p);

    std::string m_path;
    Material::MaterialLibrary* m_library = nullptr;
    std::optional<MaterialEditModel> m_model;

    // Preview.
    int m_shape = 0;  // 0 sphere, 1 cube, 2 plane, 3 cylinder
    bool m_sky = true;
    f32 m_yaw = 0.6f;
    f32 m_pitch = 0.3f;
    bool m_orbiting = false;

    // Details.
    f32 m_scroll = 0.0f;
    f32 m_split = 0.42f;  // preview share of the panel width
    std::array<bool, Material::kMaterialParameterCount> m_open{};
    bool m_settingsOpen = true;
    std::array<Slate::ColorPickerPopup, Material::kMaterialParameterCount> m_constantPickers;
    std::array<Slate::ColorPickerPopup, Material::kMaterialParameterCount> m_tintPickers;
    std::string m_parentError;
    Slate::Rect m_drawnRect{};
};

}  // namespace Luma::Editor::Panels
