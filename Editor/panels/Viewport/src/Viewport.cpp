#include "Luma/Editor/Panels/Viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Luma/Gizmo/TransformGizmo.h"
#include "Luma/Math/Math.h"
#include "Luma/Scene/Components.h"
#include "Luma/Slate/Icons.h"

namespace Luma::Editor::Panels {

using Slate::Align;
using Slate::Color;
using Slate::Icon;

namespace {

constexpr f32 kPillH = 24.0f;
constexpr f32 kInset = 8.0f;
constexpr f32 kIconBtnW = 26.0f;

// Press tracking for the toolbar's custom buttons (one pointer, one press).
u64 g_pressed = 0;
bool Clicked(Slate::Context& ui, u64 id, const Slate::Rect& r) {
    const bool hovered = r.Contains(ui.mouse());
    if (hovered && ui.mousePressed(0)) g_pressed = id;
    bool clicked = false;
    if (g_pressed == id && ui.mouseReleased(0)) {
        clicked = hovered;
        g_pressed = 0;
    }
    return clicked;
}

void DrawGlyph(Slate::Context& ui, const Slate::Rect& box, TextureHandle tex, Icon fallback,
               Color tint) {
    if (tex) {
        ui.Image(tex, box, tint);
    } else if (fallback != Icon::None) {
        Slate::DrawIcon(ui, box, fallback, tint);
    }
}

// Translucent toolbar chrome shared by every control (pill background).
void PillChrome(Slate::Context& ui, const Slate::Rect& r, bool hot, bool open, u64 animId) {
    Slate::Theme& t = ui.theme();
    const f32 hoverT = ui.Animate(animId ^ 0x7001BA2ull, hot || open, t.motion.hover);
    const Color fill = Slate::Mix(t.surface0.WithAlpha(205), t.buttonHover.WithAlpha(235), hoverT);
    ui.drawList().AddRectShadow(r, t.radius.sm, 0.35f, 4.0f);
    ui.drawList().AddRectFilledRounded(r, open ? t.focusRing : t.menu.outline.WithAlpha(200),
                                       t.radius.sm);
    ui.drawList().AddRectFilledRounded(r.Inset(1.0f, 1.0f), fill, t.radius.sm - 1.0f);
}

// Pill with an optional leading icon, a label and a drop-down chevron.
bool DropPill(Slate::Context& ui, u64 id, const Slate::Rect& r, TextureHandle tex, Icon icon,
              std::string_view label, bool open, f32 iconW = 14.0f) {
    Slate::Theme& t = ui.theme();
    const bool clicked = Clicked(ui, id, r);
    PillChrome(ui, r, r.Contains(ui.mouse()), open, id);
    f32 x = r.x + 7.0f;
    if (tex || icon != Icon::None) {
        DrawGlyph(ui, {x, r.y + (r.h - 14.0f) * 0.5f, iconW, 14.0f}, tex, icon, t.text);
        x += iconW + 6.0f;
    }
    const Slate::Font& f = ui.uiFont();
    ui.drawList().AddText(f, {x, r.y + (r.h - f.LineHeight()) * 0.5f}, label, t.text);
    Slate::DrawIcon(ui, {r.Right() - 17.0f, r.y + (r.h - 10.0f) * 0.5f, 10.0f, 10.0f},
                    Icon::ChevronDown, t.textDim);
    return clicked;
}

f32 DropPillWidth(Slate::Context& ui, std::string_view label, f32 iconW) {
    return 7.0f + (iconW > 0.0f ? iconW + 6.0f : 0.0f) + ui.uiFont().Measure(label).x + 8.0f +
           17.0f;
}

// Square icon button; `active` = selected tool / enabled toggle (accent fill).
bool IconToggle(Slate::Context& ui, u64 id, const Slate::Rect& r, TextureHandle tex,
                Icon fallback, bool active) {
    Slate::Theme& t = ui.theme();
    const bool clicked = Clicked(ui, id, r);
    const bool hot = r.Contains(ui.mouse());
    if (active) {
        ui.drawList().AddRectFilledRounded(r.Inset(2.0f, 2.0f), t.selectionBg, t.radius.sm);
    } else if (hot) {
        ui.drawList().AddRectFilledRounded(r.Inset(2.0f, 2.0f), t.buttonHover, t.radius.sm);
    }
    DrawGlyph(ui, {r.x + (r.w - 16.0f) * 0.5f, r.y + (r.h - 16.0f) * 0.5f, 16.0f, 16.0f}, tex,
              fallback, active ? t.selectionText : (hot ? t.text : t.textDim));
    return clicked;
}

std::string FormatStep(f32 v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    return buf;
}

Math::Vec3 OrbitOffset(f32 yaw, f32 pitch) {
    return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
}

}  // namespace

const char* ViewportPanel::ViewModeName(ViewMode mode) {
    switch (mode) {
        case ViewMode::Lit: return "Lit";
        case ViewMode::Unlit: return "Unlit";
        case ViewMode::LightingOnly: return "Lighting Only";
        case ViewMode::BaseColor: return "Base Color";
        case ViewMode::WorldNormal: return "World Normal";
        case ViewMode::Roughness: return "Roughness";
        case ViewMode::Metallic: return "Metallic";
    }
    return "Lit";
}

f32 ViewportPanel::CameraSpeedUnits(int setting) {
    static const f32 kSpeeds[8] = {0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};
    return kSpeeds[std::clamp(setting, 1, 8) - 1];
}

// --- Menus -----------------------------------------------------------------------

std::vector<Slate::MenuEntry> ViewportPanel::BuildCameraMenu(PanelContext& ctx) {
    Slate::MenuBuilder b;
    b.BeginSection("Field of View");
    struct Preset {
        const char* label;
        f32 degrees;
    };
    static const Preset kPresets[] = {
        {"Narrow (45)", 45.0f}, {"Default (52)", 51.57f}, {"Wide (75)", 75.0f},
        {"Ultra Wide (90)", 90.0f}};
    for (const Preset& p : kPresets) {
        const f32 rad = Math::Radians(p.degrees);
        Slate::MenuEntryOptions o;
        o.checked = std::fabs(m_settings.fovYRadians - rad) < 0.01f;
        b.AddEntry(p.label, Icon::None, [this, rad] { m_settings.fovYRadians = rad; }, o);
    }

    b.BeginSection("Camera");
    {
        Slate::MenuEntryOptions o;
        o.shortcut = "F";
        o.enabled = static_cast<bool>(ctx.onFocusEntity) && ctx.scene->IsValid(*ctx.selected);
        o.tooltip = "Frame the selected actor.";
        auto focus = ctx.onFocusEntity;
        const Entity sel = *ctx.selected;
        b.AddEntry("Frame Selected", Icon::Eye,
                   [focus, sel] {
                       if (focus) focus(sel);
                   },
                   o);
    }
    {
        Slate::MenuEntryOptions o;
        o.enabled = static_cast<bool>(ctx.onResetCamera);
        o.tooltip = "Return the camera to its default orbit around the origin.";
        auto reset = ctx.onResetCamera;
        b.AddEntry("Reset Camera", Icon::Refresh,
                   [reset] {
                       if (reset) reset();
                   },
                   o);
    }
    return b.Build();
}

std::vector<Slate::MenuEntry> ViewportPanel::BuildViewModeMenu() {
    Slate::MenuBuilder b;
    auto mode = [&](ViewMode m, Slate::MenuIcon icon, const char* tip) {
        Slate::MenuEntryOptions o;
        o.checked = (m_settings.viewMode == m);
        o.tooltip = tip;
        b.AddEntry(ViewModeName(m), icon, [this, m] { m_settings.viewMode = m; }, o);
    };
    b.BeginSection("View Mode");
    mode(ViewMode::Lit, Slate::MenuIcon::Texture(m_icons.lit, Icon::Light),
         "Final lit image with post processing.");
    mode(ViewMode::Unlit, Slate::MenuIcon::Texture(m_icons.unlit, Icon::Sphere),
         "Base color only, without lighting.");
    mode(ViewMode::LightingOnly, Icon::Light,
         "Lighting on neutral grey surfaces (materials ignored).");
    b.BeginSection("Buffer Visualization");
    mode(ViewMode::BaseColor, Icon::Image, "G-buffer base color.");
    mode(ViewMode::WorldNormal, Icon::Image, "World-space normals (XYZ as RGB).");
    mode(ViewMode::Roughness, Icon::Image, "G-buffer roughness (black = smooth).");
    mode(ViewMode::Metallic, Icon::Image, "G-buffer metallic (white = metal).");
    return b.Build();
}

std::vector<Slate::MenuEntry> ViewportPanel::BuildShowMenu() {
    Slate::MenuBuilder b;
    auto flag = [&](const char* label, bool* value, const char* tip) {
        Slate::MenuEntryOptions o;
        o.checked = *value;
        o.tooltip = tip;
        b.AddEntry(label, Icon::None, [value] { *value = !*value; }, o);
    };
    b.BeginSection("Show");
    flag("Grid", &m_settings.showGrid, "The ground reference grid.");
    flag("Stats", &m_settings.showStats, "Frame time and FPS in the viewport corner.");
    b.BeginSection("Post Processing");
    flag("Bloom", &m_settings.showBloom, "Scene bloom (from the active post-process volume).");
    flag("Eye Adaptation", &m_settings.showEyeAdaptation,
         "Automatic exposure (from the active post-process volume).");
    return b.Build();
}

// kind: 0 = location, 1 = rotation, 2 = scale (Unreal's default grid lists;
// location converted from Unreal centimeters to Luma meters).
std::vector<Slate::MenuEntry> ViewportPanel::BuildSnapMenu(PanelContext& ctx, int kind) {
    GizmoSnap& snap = ctx.gizmo->Snap();
    static const f32 kLocation[] = {0.01f, 0.05f, 0.1f, 0.5f, 1.0f, 5.0f, 10.0f, 50.0f};
    static const f32 kRotation[] = {5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 60.0f, 90.0f, 120.0f};
    static const f32 kScale[] = {1.0f, 0.5f, 0.25f, 0.125f, 0.1f, 0.0625f};
    const f32* values = kind == 0 ? kLocation : (kind == 1 ? kRotation : kScale);
    const int count = kind == 2 ? 6 : 8;
    bool* enabled = kind == 0 ? &snap.translate : (kind == 1 ? &snap.rotate : &snap.scale);
    f32* step = kind == 0 ? &snap.translateStep
                          : (kind == 1 ? &snap.rotateStepDeg : &snap.scaleStep);

    Slate::MenuBuilder b;
    b.BeginSection(kind == 0 ? "Location Grid (m)"
                             : (kind == 1 ? "Rotation Grid (degrees)" : "Scale Grid"));
    for (int i = 0; i < count; ++i) {
        const f32 v = values[i];
        Slate::MenuEntryOptions o;
        o.checked = std::fabs(*step - v) < 1e-5f;
        b.AddEntry(FormatStep(v), Icon::None,
                   [step, enabled, v] {
                       *step = v;
                       *enabled = true;  // picking a size turns snapping on
                   },
                   o);
    }
    b.BeginSection();
    {
        Slate::MenuEntryOptions o;
        o.checked = *enabled;
        b.AddEntry("Snapping Enabled", Icon::None, [enabled] { *enabled = !*enabled; }, o);
    }
    return b.Build();
}

std::vector<Slate::MenuEntry> ViewportPanel::BuildCameraSpeedMenu() {
    Slate::MenuBuilder b;
    b.BeginSection("Camera Speed");
    for (int s = 1; s <= 8; ++s) {
        char label[40];
        std::snprintf(label, sizeof(label), "%d   (%g m/s)", s,
                      static_cast<double>(CameraSpeedUnits(s)));
        Slate::MenuEntryOptions o;
        o.checked = m_settings.cameraSpeed == s;
        b.AddEntry(label, Icon::None, [this, s] { m_settings.cameraSpeed = s; }, o);
    }
    return b.Build();
}

// --- Toolbars ----------------------------------------------------------------------

Slate::Rect ViewportPanel::DrawToolbar(Slate::Context& ui, const Slate::Rect& body,
                                       PanelContext& ctx) {
    f32 x = body.x + kInset;
    const f32 y = body.y + kInset;

    const u64 camId = Slate::Context::ID("vp.camera");
    const char* camLabel = "Perspective";
    const Slate::Rect camR{x, y, DropPillWidth(ui, camLabel, 14.0f), kPillH};
    if (DropPill(ui, camId, camR, m_icons.perspective, Icon::Camera, camLabel,
                 ui.IsMenuOpen(camId))) {
        ui.OpenMenuBelow(camId, camR, BuildCameraMenu(ctx));
    }
    ui.Tooltip(camId ^ 0x71Full, camR, "Camera\nField of view, Frame Selected and Reset Camera.");
    x = camR.Right() + 4.0f;

    const u64 modeId = Slate::Context::ID("vp.viewmode");
    const char* modeLabel = ViewModeName(m_settings.viewMode);
    const TextureHandle modeTex =
        m_settings.viewMode == ViewMode::Lit
            ? m_icons.lit
            : (m_settings.viewMode == ViewMode::Unlit ? m_icons.unlit : 0);
    const Slate::Rect modeR{x, y, DropPillWidth(ui, modeLabel, 14.0f), kPillH};
    if (DropPill(ui, modeId, modeR, modeTex, Icon::Light, modeLabel, ui.IsMenuOpen(modeId))) {
        ui.OpenMenuBelow(modeId, modeR, BuildViewModeMenu());
    }
    ui.Tooltip(modeId ^ 0x71Full, modeR,
               "View Mode\nRender the viewport Lit, Unlit, Lighting Only, or as a "
               "G-buffer visualization (Base Color, World Normal, Roughness, Metallic).");
    x = modeR.Right() + 4.0f;

    const u64 showId = Slate::Context::ID("vp.show");
    const Slate::Rect showR{x, y, DropPillWidth(ui, "Show", 14.0f), kPillH};
    if (DropPill(ui, showId, showR, m_icons.show, Icon::Eye, "Show", ui.IsMenuOpen(showId))) {
        ui.OpenMenuBelow(showId, showR, BuildShowMenu());
    }
    ui.Tooltip(showId ^ 0x71Full, showR,
               "Show\nToggle the grid, stats and post-process effects in this viewport.");
    return {camR.x, camR.y, showR.Right() - camR.x, kPillH};
}

Slate::Rect ViewportPanel::DrawTransformToolbar(Slate::Context& ui, const Slate::Rect& body,
                                                const Slate::Rect& left, PanelContext& ctx) {
    TransformGizmo& gizmo = *ctx.gizmo;
    const GizmoSnap& snap = gizmo.Snap();
    const std::string snapLabels[3] = {FormatStep(snap.translateStep),
                                       FormatStep(snap.rotateStepDeg),
                                       FormatStep(snap.scaleStep)};
    const std::string speedLabel = std::to_string(m_settings.cameraSpeed);
    constexpr f32 kGroupGap = 6.0f;
    f32 snapW[3];
    f32 total = kIconBtnW * 3.0f + 4.0f + kGroupGap + kIconBtnW + 4.0f;  // tools + space
    for (int i = 0; i < 3; ++i) {
        snapW[i] = DropPillWidth(ui, snapLabels[i], 0.0f);
        total += kGroupGap + kIconBtnW + snapW[i];
    }
    const f32 speedW = DropPillWidth(ui, speedLabel, 21.0f);
    total += kGroupGap + speedW;

    f32 x = body.Right() - kInset - total;
    f32 y = body.y + kInset;
    if (x < left.Right() + 12.0f) {  // narrow viewport: second row
        x = std::max(body.x + kInset, body.Right() - kInset - total);
        y = left.Bottom() + 4.0f;
    }
    const f32 x0 = x;

    // [Translate][Rotate][Scale]
    const Slate::Rect tools{x, y, kIconBtnW * 3.0f + 4.0f, kPillH};
    PillChrome(ui, tools, false, false, Slate::Context::ID("vp.tools"));
    struct Tool {
        GizmoMode mode;
        TextureHandle tex;
        Icon fallback;
        const char* id;
    };
    const Tool kTools[3] = {{GizmoMode::Translate, m_icons.translate, Icon::Plus, "vp.t"},
                            {GizmoMode::Rotate, m_icons.rotate, Icon::Refresh, "vp.r"},
                            {GizmoMode::Scale, m_icons.scale, Icon::Cube, "vp.s"}};
    static const char* kToolTips[3] = {
        "Translate (W)\nMove the selected actor along an axis or plane.",
        "Rotate (E)\nRotate the selected actor around an axis.",
        "Scale (R)\nScale the selected actor along an axis, or uniformly from the center box."};
    for (int i = 0; i < 3; ++i) {
        const Slate::Rect r{x + 2.0f + kIconBtnW * static_cast<f32>(i), y, kIconBtnW, kPillH};
        if (IconToggle(ui, Slate::Context::ID(kTools[i].id), r, kTools[i].tex,
                       kTools[i].fallback, gizmo.Mode() == kTools[i].mode)) {
            gizmo.SetMode(kTools[i].mode);
        }
        ui.Tooltip(Slate::Context::ID(kTools[i].id) ^ 0x71Full, r, kToolTips[i]);
    }
    x = tools.Right() + kGroupGap;

    // World / Local
    {
        const bool local = gizmo.Space() == GizmoSpace::Local;
        const Slate::Rect r{x, y, kIconBtnW + 4.0f, kPillH};
        PillChrome(ui, r, false, false, Slate::Context::ID("vp.spacebg"));
        if (IconToggle(ui, Slate::Context::ID("vp.space"), r.Inset(2.0f, 0.0f),
                       local ? m_icons.localSpace : m_icons.worldSpace,
                       local ? Icon::Cube : Icon::Sphere, false)) {
            gizmo.SetSpace(local ? GizmoSpace::World : GizmoSpace::Local);
        }
        ui.Tooltip(Slate::Context::ID("tip.space"), r,
                   local ? "Coordinate System: Local\nHandles follow the actor's rotation. "
                           "Click for world space."
                         : "Coordinate System: World\nHandles align with the world axes. "
                           "Click for local space.");
        x = r.Right() + kGroupGap;
    }

    // Snapping: [toggle][value v] x3 (location / rotation / scale)
    const TextureHandle snapTex[3] = {m_icons.gridSnap, m_icons.rotate, m_icons.scale};
    const Icon snapFallback[3] = {Icon::Grip, Icon::Refresh, Icon::Cube};
    const char* snapIds[3] = {"vp.snap.t", "vp.snap.r", "vp.snap.s"};
    for (int i = 0; i < 3; ++i) {
        GizmoSnap& s = gizmo.Snap();
        bool* enabled = i == 0 ? &s.translate : (i == 1 ? &s.rotate : &s.scale);
        const Slate::Rect group{x, y, kIconBtnW + snapW[i], kPillH};
        const u64 valueId = Slate::Context::ID(snapIds[i]);
        PillChrome(ui, group, false, ui.IsMenuOpen(valueId), valueId ^ 0x55ull);
        if (IconToggle(ui, valueId ^ 0x1ull, {x, y, kIconBtnW, kPillH}, snapTex[i],
                       snapFallback[i], *enabled)) {
            *enabled = !*enabled;
        }
        static const char* kSnapTips[3] = {
            "Location Grid Snap\nSnap movement to the location grid. Click to toggle.",
            "Rotation Snap\nSnap rotation to fixed angle steps. Click to toggle.",
            "Scale Snap\nSnap scale to fixed increments. Click to toggle."};
        static const char* kSizeTips[3] = {
            "Location Grid Size\nGrid increment in meters.",
            "Rotation Snap Angle\nAngle increment in degrees.",
            "Scale Snap Increment\nScale increment per step."};
        ui.Tooltip(valueId ^ 0x71Aull, {x, y, kIconBtnW, kPillH}, kSnapTips[i]);
        const Slate::Rect valueR{x + kIconBtnW, y, snapW[i], kPillH};
        const bool hot = valueR.Contains(ui.mouse());
        if (Clicked(ui, valueId, valueR)) {
            ui.OpenMenuBelow(valueId, valueR, BuildSnapMenu(ctx, i));
        }
        ui.Tooltip(valueId ^ 0x71Bull, valueR, kSizeTips[i]);
        Slate::Theme& t = ui.theme();
        const Slate::Font& f = ui.uiFont();
        ui.drawList().AddText(f, {valueR.x + 6.0f, y + (kPillH - f.LineHeight()) * 0.5f},
                              snapLabels[i], *enabled ? t.text : t.textDim);
        Slate::DrawIcon(ui, {valueR.Right() - 17.0f, y + (kPillH - 10.0f) * 0.5f, 10.0f, 10.0f},
                        Icon::ChevronDown, hot ? t.text : t.textDim);
        x = group.Right() + kGroupGap;
    }

    // Camera speed
    const u64 speedId = Slate::Context::ID("vp.speed");
    const Slate::Rect speedR{x, y, speedW, kPillH};
    if (DropPill(ui, speedId, speedR, m_icons.cameraSpeed, Icon::Camera, speedLabel,
                 ui.IsMenuOpen(speedId), 21.0f)) {
        ui.OpenMenuBelow(speedId, speedR, BuildCameraSpeedMenu());
    }
    ui.Tooltip(speedId ^ 0x71Full, speedR,
               "Camera Speed\nFly speed while holding the right mouse button (W/A/S/D/Q/E). "
               "Scroll while flying to change it.");
    return {x0, y, speedR.Right() - x0, kPillH};
}

void ViewportPanel::DrawStats(Slate::Context& ui, const Slate::Rect& body, f32 top) {
    if (!m_settings.showStats || m_frameMs <= 0.0f) return;
    Slate::Theme& t = ui.theme();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.2f ms   %.0f FPS", m_frameMs, 1000.0f / m_frameMs);
    const Slate::Font& mono = ui.monoFont();
    const f32 w = mono.Measure(buf).x + 16.0f;
    const Slate::Rect r{body.Right() - w - kInset, top, w, kPillH};
    ui.drawList().AddRectFilledRounded(r, t.surface0.WithAlpha(200), t.radius.sm);
    const Color c = m_frameMs <= 17.5f ? t.success : (m_frameMs <= 34.0f ? t.warning : t.error);
    ui.drawList().AddText(mono, {r.x + 8.0f, r.y + (r.h - mono.LineHeight()) * 0.5f}, buf, c);
}

// --- Navigation (Unreal level-editor controls) ---------------------------------------

void ViewportPanel::Navigate(Slate::Context& ui, const Slate::Rect& body, PanelContext& ctx) {
    using Math::Vec3;
    f32& yaw = *ctx.camYaw;
    f32& pitch = *ctx.camPitch;
    f32& dist = *ctx.camDistance;
    Vec3& target = *ctx.camTarget;
    const Slate::Vec2 d = ui.mouseDelta();
    constexpr f32 kLook = 0.0045f;
    const f32 dt = std::clamp(m_dt, 0.0f, 0.1f);
    const bool inView = body.Contains(ui.mouse());

    // Right mouse: look around in place + WASD/QE fly (only when the press
    // started inside the view).
    if (ui.mousePressed(1)) m_flying = inView;
    if (!ui.isMouseDown(1)) m_flying = false;
    if (m_flying) {
        const Vec3 eye = target + OrbitOffset(yaw, pitch) * dist;
        yaw -= d.x * kLook;
        pitch = std::clamp(pitch + d.y * kLook, -1.5f, 1.5f);
        const Vec3 offset = OrbitOffset(yaw, pitch);
        const Vec3 forward = offset * -1.0f;
        const Vec3 right = Math::Normalize(Math::Cross(forward, Vec3(0, 1, 0)));
        Vec3 move{0, 0, 0};
        if (ui.keyHeld(87)) move = move + forward;            // W
        if (ui.keyHeld(83)) move = move - forward;            // S
        if (ui.keyHeld(68)) move = move + right;              // D
        if (ui.keyHeld(65)) move = move - right;              // A
        if (ui.keyHeld(69)) move = move + Vec3(0, 1, 0);      // E
        if (ui.keyHeld(81)) move = move - Vec3(0, 1, 0);      // Q
        Vec3 newEye = eye;
        if (Math::Dot(move, move) > 0.0f) {
            newEye = eye + Math::Normalize(move) * (CameraSpeedUnits(m_settings.cameraSpeed) * dt);
        }
        target = newEye - offset * dist;  // eye stays put while looking
        if (ui.scrollDelta() != 0.0f) {
            m_settings.cameraSpeed =
                std::clamp(m_settings.cameraSpeed + (ui.scrollDelta() > 0.0f ? 1 : -1), 1, 8);
        }
        ui.RequestCursor(CursorShape::Arrow);
        return;
    }

    // Alt + left drag: orbit the pivot.
    if (ui.altDown() && ui.mousePressed(0) && inView) m_altOrbit = true;
    if (!ui.isMouseDown(0)) m_altOrbit = false;
    if (m_altOrbit) {
        yaw -= d.x * 0.01f;
        pitch = std::clamp(pitch + d.y * 0.01f, -1.5f, 1.5f);
        return;
    }

    // Middle drag: pan in the view plane.
    if (ui.isMouseDown(2) && inView) {
        const Vec3 forward = OrbitOffset(yaw, pitch) * -1.0f;
        const Vec3 right = Math::Normalize(Math::Cross(forward, Vec3(0, 1, 0)));
        const Vec3 up = Math::Cross(right, forward);
        const f32 k = dist * 0.0016f;
        target = target - right * (d.x * k) + up * (d.y * k);
        return;
    }

    // Wheel: dolly toward / away from the pivot.
    if (inView && ui.scrollDelta() != 0.0f) {
        dist = std::clamp(dist * std::pow(0.88f, ui.scrollDelta()), 0.5f, 500.0f);
    }
}

void ViewportPanel::Draw(Slate::Context& ui, const Slate::Rect& body, PanelContext& ctx) {
    Slate::Theme& t = ui.theme();
    m_rect = body;

    // Frame timing (UI clock).
    const f32 now = ui.time();
    if (m_lastTime >= 0.0f) {
        m_dt = now - m_lastTime;
        const f32 ms = m_dt * 1000.0f;
        m_frameMs = m_frameMs <= 0.0f ? ms : m_frameMs + (ms - m_frameMs) * 0.08f;
    }
    m_lastTime = now;

    if (m_texture) {
        ui.Image(m_texture, body);
    } else {
        ui.Panel(body, t.surface0);
        ui.LabelIn(body, "3D Viewport", t.textDim, Align::Center);
    }

    const Slate::Rect leftBar = DrawToolbar(ui, body, ctx);
    const Slate::Rect rightBar = DrawTransformToolbar(ui, body, leftBar, ctx);
    DrawStats(ui, body, rightBar.Bottom() + 6.0f);
    const bool overToolbar = leftBar.Inset(-4.0f, -4.0f).Contains(ui.mouse()) ||
                             rightBar.Inset(-4.0f, -4.0f).Contains(ui.mouse());
    const bool inView = body.Contains(ui.mouse()) && !overToolbar;
    const bool navigating = ui.isMouseDown(1) || ui.altDown();

    const Math::Mat4 proj = Math::PerspectiveForViewport(
        m_settings.fovYRadians, body.w, body.h, ctx.nearZ, ctx.farZ);

    // Transform gizmo (left mouse) on the selected actor.
    TransformGizmo& gizmo = *ctx.gizmo;
    bool gizmoBusy = false;
    if (ctx.scene->IsValid(*ctx.selected) &&
        ctx.scene->Registry().all_of<TransformComponent>(*ctx.selected)) {
        auto& tf = ctx.scene->Registry().get<TransformComponent>(*ctx.selected);
        GizmoInput in;
        in.view = *ctx.view;
        in.proj = proj;
        in.viewportX = body.x;
        in.viewportY = body.y;
        in.viewportW = body.w;
        in.viewportH = body.h;
        in.mouseX = ui.mouse().x;
        in.mouseY = ui.mouse().y;
        in.scale = *ctx.gizmoScale;
        in.leftDown = (inView || gizmo.Dragging()) && ui.isMouseDown(0) && !m_altOrbit;
        in.leftPressed = inView && !navigating && ui.mousePressed(0);
        GizmoTransform cur{tf.position, tf.rotationEuler, tf.scale};
        GizmoTransform next;
        if (gizmo.Update(cur, in, next)) {
            tf.position = next.position;
            tf.rotationEuler = next.rotationEuler;
            tf.scale = next.scale;
        }
        gizmoBusy = gizmo.Dragging() || gizmo.Hovered() != GizmoHandle::None;
    }

    // Click-select: a left press that is released without dragging picks the
    // actor under the cursor (empty space clears the selection).
    if (ui.mousePressed(0) && inView && !navigating && !gizmoBusy) {
        m_pickArmed = true;
        m_pressPos = ui.mouse();
    }
    if (m_pickArmed && gizmo.Dragging()) m_pickArmed = false;
    if (m_pickArmed && ui.mouseReleased(0)) {
        m_pickArmed = false;
        const f32 dx = ui.mouse().x - m_pressPos.x, dy = ui.mouse().y - m_pressPos.y;
        if (dx * dx + dy * dy < 16.0f && ctx.onPickRay && body.w > 0.0f && body.h > 0.0f) {
            const Math::Mat4 inv = Math::Inverse(proj * *ctx.view);
            const f32 nx = (ui.mouse().x - body.x) / body.w * 2.0f - 1.0f;
            const f32 ny = (ui.mouse().y - body.y) / body.h * 2.0f - 1.0f;
            auto unproject = [&](f32 z) {
                const f32 x = inv.m[0] * nx + inv.m[4] * ny + inv.m[8] * z + inv.m[12];
                const f32 y = inv.m[1] * nx + inv.m[5] * ny + inv.m[9] * z + inv.m[13];
                const f32 zz = inv.m[2] * nx + inv.m[6] * ny + inv.m[10] * z + inv.m[14];
                const f32 w = inv.m[3] * nx + inv.m[7] * ny + inv.m[11] * z + inv.m[15];
                return Math::Vec3(x / w, y / w, zz / w);
            };
            const Math::Vec3 o = unproject(0.0f);
            ctx.onPickRay(o, Math::Normalize(unproject(1.0f) - o));
        }
    }
    if (!ui.isMouseDown(0) && !ui.mouseReleased(0)) m_pickArmed = false;

    if (gizmo.Dragging() || (overToolbar && !ui.isMouseDown(1))) return;
    Navigate(ui, body, ctx);
}

}  // namespace Luma::Editor::Panels
