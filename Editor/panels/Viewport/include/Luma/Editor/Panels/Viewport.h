#pragma once

#include <vector>

#include "PanelContext.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Types.h"

// Viewport panel — hosts the 3D scene view (texture drawn from a SceneView
// the editor produces), Unreal-style navigation, the transform gizmo, and the
// viewport toolbar overlaid on the view's top edge:
//
//   [Perspective v][Lit v][Show v]    [T R S][World][# 0.1][@ 10][S 0.25][Cam 4]
//
// Navigation (Unreal level editor):
//   * Right mouse held: look around in place; W/A/S/D/Q/E fly; the wheel
//     changes camera speed.
//   * Alt + left drag: orbit the pivot.   * Middle drag: pan.
//   * Wheel: dolly.   * Left click: select the actor under the cursor.
// The toolbar edits ViewportSettings (read by the EditorScreen when building
// the SceneView) and the shared TransformGizmo (mode / space / snapping).

namespace Luma::Editor::Panels {

struct ViewportSettings {
    ViewMode viewMode = ViewMode::Lit;
    f32 fovYRadians = 0.9f;        // vertical field of view
    bool showGrid = true;
    bool showBloom = true;         // honours the scene's post-process bloom
    bool showEyeAdaptation = true; // honours the scene's auto exposure
    bool showStats = false;        // frame time / FPS readout
    int cameraSpeed = 4;           // 1..8 (Unreal camera speed setting)
};

// Toolbar textures from the editor icon pack (Content/Editor/Icons/ue).
// Zero handles fall back to pack glyphs.
struct ViewportIcons {
    TextureHandle translate = 0, rotate = 0, scale = 0;
    TextureHandle worldSpace = 0, localSpace = 0;
    TextureHandle gridSnap = 0;
    TextureHandle cameraSpeed = 0;
    TextureHandle perspective = 0, lit = 0, unlit = 0, show = 0;
};

class ViewportPanel {
public:
    void Draw(Slate::Context& ui, const Slate::Rect& body, PanelContext& ctx);

    // The rect the viewport most recently occupied — the EditorScreen reads
    // this to know where to render the 3D scene into.
    Slate::Rect Rect() const { return m_rect; }

    // The EditorScreen calls this each frame after rendering the 3D view so
    // the panel can display the texture.
    void SetTexture(Luma::TextureHandle tex) { m_texture = tex; }
    void SetIcons(const ViewportIcons& icons) { m_icons = icons; }

    const ViewportSettings& Settings() const { return m_settings; }
    ViewportSettings& Settings() { return m_settings; }

    // Display name of a view mode (toolbar label / menu entries).
    static const char* ViewModeName(ViewMode mode);
    // World units per second for a camera speed setting (1..8).
    static f32 CameraSpeedUnits(int setting);

private:
    // Left group (camera / view mode / show); returns its rect.
    Slate::Rect DrawToolbar(Slate::Context& ui, const Slate::Rect& body, PanelContext& ctx);
    // Right group (transform tools / space / snapping / camera speed).
    Slate::Rect DrawTransformToolbar(Slate::Context& ui, const Slate::Rect& body,
                                     const Slate::Rect& leftGroup, PanelContext& ctx);
    std::vector<Slate::MenuEntry> BuildCameraMenu(PanelContext& ctx);
    std::vector<Slate::MenuEntry> BuildViewModeMenu();
    std::vector<Slate::MenuEntry> BuildShowMenu();
    std::vector<Slate::MenuEntry> BuildSnapMenu(PanelContext& ctx, int kind);
    std::vector<Slate::MenuEntry> BuildCameraSpeedMenu();
    void DrawStats(Slate::Context& ui, const Slate::Rect& body, f32 top);
    void Navigate(Slate::Context& ui, const Slate::Rect& body, PanelContext& ctx);

    Slate::Rect m_rect{};
    Luma::TextureHandle m_texture = 0;
    ViewportSettings m_settings;
    ViewportIcons m_icons;
    // Frame timing (smoothed) for the stats readout + fly speed.
    f32 m_lastTime = -1.0f;
    f32 m_dt = 0.0f;
    f32 m_frameMs = 0.0f;
    // Click-select tracking: a left press that ends without dragging picks.
    bool m_pickArmed = false;
    bool m_meshPreviewing = false;  // a mesh drag is previewed in the viewport
    Slate::Vec2 m_pressPos{};
    bool m_altOrbit = false;  // Alt + left drag started an orbit
    bool m_flying = false;    // right button pressed inside the view
};

}  // namespace Luma::Editor::Panels
