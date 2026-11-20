#pragma once

#include <filesystem>
#include <array>
#include <string>
#include <unordered_map>

#include "Luma/Asset/MaterialThumbnailRenderer.h"
#include "Luma/Material/Material.h"
#include "Luma/Renderer/DeferredShadingRenderer.h"
#include "Luma/Slate/ColorPickerPopup.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/GraphNodeWidget.h"
#include "PanelContext.h"

// Material Editor panel. The graph surface is intentionally UI-first: it
// provides the Unreal-style workspace and interaction model while the shader
// graph/runtime compiler is developed independently.

namespace Luma::Editor::Panels {

class MaterialEditorPanel {
public:
    MaterialEditorPanel() = default;

    bool Open(const std::filesystem::path& path);
    bool Save();
    bool IsOpen() const { return m_open; }
    const std::filesystem::path& Path() const { return m_path; }
    void MarkDirty() { m_dirty = true; m_previewDirty = true; }
    Slate::Rect MaterialViewportRect() const { return m_previewRect; }
    void SetMaterialViewportTexture(Luma::TextureHandle texture) {
        m_previewTexture = texture;
    }
    Renderer2::DeferredSceneView BuildDeferredSceneView(u32 width, u32 height);

    void Draw(Slate::Context& ui, const Slate::Rect& body, PanelContext& ctx);
    void DrawFloatingPickers(Slate::Context& ui);

private:
    struct GraphNodeState {
        const char* id = "";
        const char* title = "";
        Slate::Vec2 position{};
        Slate::Vec2 size{};
        Slate::Color tint{};
        bool output = false;
        bool selected = false;
    };

    void DrawToolbar(Slate::Context& ui, const Slate::Rect& body);
    void DrawGraphCanvas(Slate::Context& ui, const Slate::Rect& rect);
    void DrawGraphNode(Slate::Context& ui, GraphNodeState& node,
                       const Slate::Rect& canvas);
    void DrawSidebar(Slate::Context& ui, const Slate::Rect& rect,
                     PanelContext& ctx);
    void DrawMaterialViewport(Slate::Context& ui, const Slate::Rect& rect,
                              PanelContext& ctx);
    void DrawDetailsPanel(Slate::Context& ui, const Slate::Rect& rect,
                          PanelContext& ctx);
    void DrawPropertyRows(Slate::Context& ui, const Slate::Rect& rect,
                          PanelContext& ctx);

    bool m_open = false;
    bool m_dirty = false;
    Luma::Material::Material m_material;
    std::filesystem::path m_path;

    // Material scene viewport state.
    bool m_previewDirty = true;
    Luma::TextureHandle m_previewTexture = 0;
    u32 m_previewW = 0, m_previewH = 0;
    f32 m_previewYaw = 30.0f, m_previewPitch = 15.0f;
    f32 m_previewDistance = 2.0f;
    Math::Vec3 m_previewTarget{0.0f, 0.5f, 0.0f};
    Slate::Rect m_previewRect{};
    bool m_environmentLoaded = false;
    Luma::MaterialThumbnailData m_previewEnvironment;
    SceneView m_materialSceneSource;
    std::array<SceneInstance, 2> m_materialInstances{};
    std::array<SceneLight, 1> m_materialLights{};

    // Color picker popups (keyed by MaterialProperty).
    std::unordered_map<Luma::Material::MaterialProperty,
                       Slate::ColorPickerPopup>
        m_colorPickers;

    bool m_detailsMaterialOpen = true;

    Slate::Vec2 m_graphPan{0.0f, 0.0f};
    f32 m_graphZoom = 1.0f;
    int m_draggedNode = -1;
    Slate::Vec2 m_dragOffset{};
    int m_selectedNode = 3;
    Slate::Vec2 m_baseColorPosition{70.0f, 100.0f};
    Slate::Vec2 m_metallicPosition{70.0f, 300.0f};
    Slate::Vec2 m_roughnessPosition{70.0f, 450.0f};
    Slate::Vec2 m_outputPosition{430.0f, 190.0f};
};

}  // namespace Luma::Editor::Panels
