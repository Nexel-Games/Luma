#include "Luma/Editor/Panels/Inspector.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include "Luma/Core/Log.h"
#include "Luma/Editor/Panels/MaterialSlots.h"
#include "Luma/Editor/Panels/StaticMeshSlot.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Slate/Icons.h"
#include "Luma/Scene/Components.h"
#include "Luma/Slate/Menu.h"

namespace Luma::Editor::Panels {

using Slate::Align;
using Slate::Rect;

namespace {

// Unreal Details layout: one name column for every property row, so the
// vertical divider lines up down the whole panel.
f32 NameColumnWidth(f32 w) { return std::clamp(w * 0.36f, 96.0f, 180.0f); }

// Unreal-style property grid: a near-black line under the row across the
// panel and one between the name and value columns. `left`/`right` span
// the panel; `divX` is the column divider.
void DrawRowGrid(Slate::Context& ui, f32 left, f32 right, f32 top, f32 h, f32 divX) {
    const Slate::Color line = ui.theme().propertyDivider;
    ui.Panel({left, top + h - 1.0f, right - left, 1.0f}, line);
    if (divX > left) ui.Panel({divX, top, 1.0f, h - 1.0f}, line);
}

template <typename C>
bool Has(const Scene* scene, Entity e) {
    return scene->IsValid(e) && scene->Registry().all_of<C>(e);
}

template <typename C>
void AddIfMissing(Scene* scene, Entity e) {
    if (scene->IsValid(e) && !scene->Registry().all_of<C>(e)) {
        scene->Registry().emplace<C>(e);
    }
}

// Resets (replaces with a default-constructed value) or removes component C.
template <typename C>
void ResetOrRemove(entt::registry& reg, Entity e, bool remove) {
    if (!reg.all_of<C>(e)) return;
    if (remove) {
        reg.remove<C>(e);
    } else {
        reg.replace<C>(e, C{});
    }
}

}  // namespace

std::vector<Slate::MenuEntry> InspectorPanel::BuildAddComponentMenu(Scene* scene,
                                                                    Entity e) {
    using Slate::Icon;
    const bool hasMesh = Has<MeshRendererComponent>(scene, e);
    const bool hasCam = Has<CameraComponent>(scene, e);
    const bool hasLight = Has<LightComponent>(scene, e);
    const bool hasEnv = Has<EnvironmentComponent>(scene, e);
    const bool hasPP = Has<PostProcessComponent>(scene, e);
    bool envElsewhere = false;
    for (Entity other : scene->Registry().view<EnvironmentComponent>()) {
        if (other != e) envElsewhere = true;
    }
    auto opts = [](bool blocked, std::string tip, std::string reason) {
        Slate::MenuEntryOptions o;
        o.enabled = !blocked;
        o.tooltip = blocked ? std::move(reason) : std::move(tip);
        return o;
    };
    const std::string kOnePer = "Already on this entity (one per entity).";

    Slate::MenuBuilder b;
    b.BeginSection("Rendering");
    b.AddEntry("Mesh Renderer", Icon::Cube,
               [scene, e] { AddIfMissing<MeshRendererComponent>(scene, e); },
               opts(hasMesh, "Draws a primitive or mesh asset with a PBR material.",
                    kOnePer));
    b.AddEntry("Camera", Icon::Camera,
               [scene, e] { AddIfMissing<CameraComponent>(scene, e); },
               opts(hasCam,
                    "A view into the scene. The primary camera drives the game view.",
                    kOnePer));

    b.BeginSection("Lighting");
    struct LightDef {
        const char* name;
        LightType type;
        const char* tip;
    };
    static const LightDef kLights[] = {
        {"Directional Light", LightType::Directional,
         "Infinitely distant sun light; drives the sky and casts cascaded shadows."},
        {"Point Light", LightType::Point,
         "Emits in all directions with physical inverse-square falloff."},
        {"Spot Light", LightType::Spot, "A cone of light with inner and outer angles."},
        {"Tube Light", LightType::Tube, "A linear emitter of a given length."},
    };
    for (const LightDef& l : kLights) {
        const LightType type = l.type;
        b.AddEntry(l.name, Icon::Light,
                   [scene, e, type] {
                       if (!scene->IsValid(e) ||
                           scene->Registry().all_of<LightComponent>(e)) {
                           return;
                       }
                       scene->Registry().emplace<LightComponent>(e).type = type;
                   },
                   opts(hasLight, l.tip,
                        "This entity already has a Light (one per entity)."));
    }

    b.BeginSection("Environment");
    b.AddEntry("Sky & Environment", Icon::Sphere,
               [scene, e] { AddIfMissing<EnvironmentComponent>(scene, e); },
               opts(hasEnv || envElsewhere,
                    "Physically based sky atmosphere and ambient (sky) lighting.",
                    hasEnv ? kOnePer
                           : std::string("The scene already has an Environment "
                                         "(one per scene).")));

    b.BeginSection("Volumes");
    b.AddEntry("Post Process Volume", Icon::Eye,
               [scene, e] { AddIfMissing<PostProcessComponent>(scene, e); },
               opts(hasPP, "Bloom, exposure, tone mapping and other camera effects.",
                    kOnePer));
    return b.Build();
}

std::vector<Slate::MenuEntry> InspectorPanel::BuildComponentMenu(Scene* scene,
                                                                 Entity e,
                                                                 ComponentKind kind) {
    using Slate::Icon;
    auto apply = [scene, e, kind](bool remove) {
        if (!scene->IsValid(e)) return;
        auto& reg = scene->Registry();
        switch (kind) {
            case ComponentKind::Transform:
                ResetOrRemove<TransformComponent>(reg, e, remove);
                break;
            case ComponentKind::MeshRenderer:
                ResetOrRemove<MeshRendererComponent>(reg, e, remove);
                break;
            case ComponentKind::Camera:
                ResetOrRemove<CameraComponent>(reg, e, remove);
                break;
            case ComponentKind::Light:
                // Reset keeps the light's type (the type is its identity).
                if (!remove && reg.all_of<LightComponent>(e)) {
                    LightComponent fresh;
                    fresh.type = reg.get<LightComponent>(e).type;
                    reg.replace<LightComponent>(e, fresh);
                } else {
                    ResetOrRemove<LightComponent>(reg, e, remove);
                }
                break;
            case ComponentKind::Environment:
                ResetOrRemove<EnvironmentComponent>(reg, e, remove);
                break;
            case ComponentKind::PostProcess:
                ResetOrRemove<PostProcessComponent>(reg, e, remove);
                break;
        }
    };

    Slate::MenuBuilder b;
    b.BeginSection("Component");
    Slate::MenuEntryOptions reset;
    reset.tooltip = "Restore every property of this component to its default.";
    b.AddEntry("Reset to Defaults", Icon::Refresh,
               [this, apply] {
                   ClosePopups();
                   apply(false);
               },
               reset);
    Slate::MenuEntryOptions removeOpts;
    removeOpts.enabled = (kind != ComponentKind::Transform);
    removeOpts.tooltip = removeOpts.enabled ? "Remove this component from the entity."
                                            : "Every entity needs a Transform.";
    b.AddEntry("Remove Component", Icon::Trash,
               [this, apply] {
                   ClosePopups();
                   apply(true);
               },
               removeOpts);
    return b.Build();
}

u64 InspectorPanel::RowId(std::string_view key, Entity entity) {
    const u64 e = static_cast<u64>(entt::to_integral(entity)) + 1;
    return Slate::Context::ID(key) ^ (e * 0x9E3779B97F4A7C15ull);
}

void InspectorPanel::Draw(Slate::Context& ui, const Slate::Rect& body,
                          PanelContext& ctx) {
    Slate::Theme& t = ui.theme();
    m_slotRects.clear();
    m_meshRowRect = {};

    if (!ctx.scene->IsValid(*ctx.selected)) {
        ui.LabelIn({body.x, body.y + 8, body.w, 22}, "  No selection",
                   t.textDim);
        return;
    }
    // Selection changed — close popups so they don't target the previous
    // entity's components, and jump the content scroll back to the top.
    if (*ctx.selected != m_lastSelected) {
        ClosePopups();
        m_lastSelected = *ctx.selected;
        m_scroll = 0.0f;
    }
    auto& reg = ctx.scene->Registry();
    f32 x = body.x + 12.0f;
    f32 y = body.y + 12.0f;
    f32 w = body.w - 24.0f;
    const f32 nameCol = NameColumnWidth(w);
    // Property rows are 22 px controls in 28 px bands (3 px above, 3 below).
    auto rowGrid = [&] { DrawRowGrid(ui, body.x, body.Right(), y - 3.0f, 28.0f, x + nameCol - 6.0f); };

    // Header: entity name + "Add" component picker (Unreal Details panel).
    const u64 addId = RowId("insp.addcomp", *ctx.selected);
    const Rect addBtn{body.Right() - 84.0f, y, 72.0f, 24.0f};
    ui.Heading({x, y, addBtn.x - x - 8.0f, 24},
               reg.get<NameComponent>(*ctx.selected).name, t.text);
    if (ui.IconTextButton(addId, addBtn, Slate::Icon::Plus, "Add", t.success,
                          ui.IsMenuOpen(addId))) {
        Slate::MenuOptions opts;
        opts.searchable = true;
        opts.searchHint = "Search components";
        opts.minWidth = 240.0f;
        ui.OpenMenuBelow(addId, addBtn,
                         BuildAddComponentMenu(ctx.scene, *ctx.selected), opts);
    }
    ui.Tooltip(addId ^ 0x71Full, addBtn,
               "Add Component\nAttach a new component to the selected actor. "
               "Right-click a component header to reset or remove it.");
    // The header row (name + + Component) stays pinned; the component
    // content below it scrolls. The content region is clipped so scrolled
    // rows can't overlap the pinned header, and the scrollbar is added at
    // the end once the content height is known.
    const f32 contentTop = body.y + 46.0f;
    Rect contentRegion{body.x, contentTop, body.w, body.h - 46.0f};
    ui.PushClip(contentRegion);
    y = contentTop - m_scroll;

    // Component category header (Starship Details style): flat header
    // band, accent tick, and a right-click menu for component actions.
    auto sectionHeader = [&](const char* label, ComponentKind kind) {
        const Rect band{body.x, y, body.w, 26};
        ui.Panel(band, t.surface2);
        ui.Panel({band.x, band.Bottom() - 1.0f, band.w, 1.0f}, t.separator);
        ui.Panel({body.x, y, 2, 26}, t.accent);
        ui.Heading({x, y, w, 26}, label, t.text);
        const u64 menuId = Slate::Context::ID(label) ^ 0x5EC7ull;
        if (ui.mousePressed(1) && band.Contains(ui.mouse())) {
            ui.OpenMenu(menuId, ui.mouse(),
                        BuildComponentMenu(ctx.scene, *ctx.selected, kind));
        }
        y += 32.0f;
    };
    auto vec3Row = [&](const char* label, u64 id, f32* v) {
        ui.LabelIn({x, y, nameCol - 8.0f, 22}, label, t.textDim);
        ui.Vector3Field(id, {x + nameCol, y, w - nameCol, 22}, v);
        rowGrid();
        y += 28.0f;
    };
    // Numeric property: label + spin box (drag to scrub, click to type).
    auto floatRow = [&](const char* label, u64 id, f32& v,
                        const Slate::SpinBoxOptions& o = {}) {
        ui.LabelIn({x, y, nameCol - 8.0f, 22}, label, t.textDim);
        ui.SpinBox(id, {x + nameCol, y, w - nameCol, 22}, v, o);
        rowGrid();
        y += 28.0f;
    };
    auto intRow = [&](const char* label, u64 id, i32& v, const Slate::SpinBoxOptions& o) {
        ui.LabelIn({x, y, nameCol - 8.0f, 22}, label, t.textDim);
        ui.SpinBoxInt(id, {x + nameCol, y, w - nameCol, 22}, v, o);
        rowGrid();
        y += 28.0f;
    };
    // Option presets: limits, drag speed, unit suffix.
    auto opts = [](f64 speed, std::optional<f64> lo, std::optional<f64> hi,
                   std::string_view suffix = {}, int maxDigits = 3) {
        Slate::SpinBoxOptions o;
        o.speed = speed;
        o.min = lo;
        o.max = hi;
        o.suffix = suffix;
        o.maxFractionalDigits = maxDigits;
        return o;
    };
    // 0..1 values get a slider fill bar.
    auto unit01 = [](f64 speed = 0.005) {
        Slate::SpinBoxOptions o;
        o.speed = speed;
        o.min = 0.0;
        o.max = 1.0;
        o.sliderMin = 0.0;
        o.sliderMax = 1.0;
        return o;
    };
    // Enum property: label + combo box (fits any column width, unlike a
    // segmented tab row). Returns the (possibly changed) index.
    auto comboRow = [&](const char* label, u64 id,
                        const std::vector<std::string>& items, int current) {
        ui.LabelIn({x, y, nameCol - 8.0f, 22}, label, t.textDim);
        int picked = ui.Combo(id, {x + nameCol, y, w - nameCol, 22}, items, current);
        rowGrid();
        y += 28.0f;
        return picked;
    };

    // Bool property: name in the name column, checkbox in the value column.
    auto checkRow = [&](const char* label, u64 id, bool& value) {
        ui.LabelIn({x, y, nameCol - 8.0f, 22}, label, t.textDim);
        ui.Checkbox(id, {x + nameCol, y + 2.0f, 18.0f, 18.0f}, "", value);
        rowGrid();
        y += 28.0f;
    };
    // Sub-category inside a component (Unreal: a nested group label).
    auto subHeading = [&](const char* label) {
        ui.LabelIn({x, y, w, 20}, label, t.accent);
        DrawRowGrid(ui, body.x, body.Right(), y - 2.0f, 26.0f, 0.0f);
        y += 28.0f;
    };

    // --- Color property rows -------------------------------------------------
    // Component colors are Math::Vec3 (0..1 floats); the picker works in
    // 8-bit Slate::Color. A color row is a compact swatch strip — clicking it
    // opens a floating ColorPicker panel (drawn by DrawFloatingPickers after
    // the dock) instead of a bulky inline picker.
    auto vec3ToColor = [](const Math::Vec3& v) {
        return Slate::Color::RGB(
            static_cast<u8>(std::clamp(v.x, 0.0f, 1.0f) * 255.0f + 0.5f),
            static_cast<u8>(std::clamp(v.y, 0.0f, 1.0f) * 255.0f + 0.5f),
            static_cast<u8>(std::clamp(v.z, 0.0f, 1.0f) * 255.0f + 0.5f));
    };
    auto colorToVec3 = [](const Slate::Color& c) {
        return Math::Vec3(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
    };
    auto colorRow = [&](const char* label, Slate::ColorPickerPopup& popup,
                        Math::Vec3& value) {
        ui.LabelIn({x, y, nameCol - 8.0f, 22}, label, t.textDim);
        // Wide swatch strip filling the rest of the row; clicking it opens
        // the floating color picker.
        Rect sw{x + nameCol, y + 2, w - nameCol, 18};
        // Edits write straight back into the component; the picker follows
        // the component through SetColor below (no-op while editing).
        popup.OnColorChanged = [&](const Slate::Color& c) {
            value = colorToVec3(c);
        };
        popup.SetColor(vec3ToColor(value));
        popup.DrawSwatch(ui, sw);
        rowGrid();
        y += 28.0f;
    };

    if (reg.all_of<TransformComponent>(*ctx.selected)) {
        sectionHeader("Transform", ComponentKind::Transform);
        auto& tf = reg.get<TransformComponent>(*ctx.selected);
        vec3Row("Position", RowId("insp.pos", *ctx.selected), &tf.position.x);
        vec3Row("Rotation", RowId("insp.rot", *ctx.selected),
                &tf.rotationEuler.x);
        vec3Row("Scale", RowId("insp.scl", *ctx.selected), &tf.scale.x);
        y += 8.0f;
    }
    if (reg.all_of<MeshRendererComponent>(*ctx.selected)) {
        sectionHeader("Mesh Renderer", ComponentKind::MeshRenderer);
        auto& mr = reg.get<MeshRendererComponent>(*ctx.selected);
        DrawStaticMeshRow(ui, ctx, mr, x, y, w);
        // The primitive is only drawn while no static mesh is assigned.
        if (!mr.UsesMeshAsset()) {
            mr.primitive = static_cast<MeshPrimitive>(
                comboRow("Shape", RowId("insp.shape", *ctx.selected),
                         {"Cube", "Plane", "Sphere", "Cylinder"},
                         static_cast<int>(mr.primitive)));
        }
        DrawMaterialSlots(ui, ctx, mr, x, y, w);
        y += 8.0f;
    }
    if (reg.all_of<CameraComponent>(*ctx.selected)) {
        sectionHeader("Camera", ComponentKind::Camera);
        auto& cam = reg.get<CameraComponent>(*ctx.selected);
        cam.projection = static_cast<ProjectionType>(
            comboRow("Projection", RowId("insp.camproj", *ctx.selected),
                     {"Perspective", "Orthographic"},
                     static_cast<int>(cam.projection)));
        if (cam.projection == ProjectionType::Perspective) {
            floatRow("Field of View", RowId("insp.camfov", *ctx.selected),
                     cam.fovYDegrees, opts(0.2, 5.0, 170.0, "\u00B0", 1));
        } else {
            floatRow("Ortho Height", RowId("insp.camortho", *ctx.selected),
                     cam.orthoHeight, opts(0.05, 0.01, std::nullopt, " m"));
        }
        floatRow("Near Clip", RowId("insp.camnear", *ctx.selected), cam.nearZ,
                 opts(0.005, 0.001, std::nullopt, " m"));
        floatRow("Far Clip", RowId("insp.camfar", *ctx.selected), cam.farZ,
                 opts(1.0, 0.01, std::nullopt, " m", 1));
        // Sanitise values that may come from a hand-edited scene file (the
        // spin-box limits only apply to edits made here).
        cam.fovYDegrees = std::clamp(cam.fovYDegrees, 5.0f, 170.0f);
        cam.orthoHeight = std::max(cam.orthoHeight, 0.01f);
        cam.nearZ = std::max(cam.nearZ, 0.001f);
        cam.farZ = std::max(cam.farZ, cam.nearZ + 0.01f);
        checkRow("Primary Camera", RowId("insp.camprimary", *ctx.selected), cam.primary);
        y += 8.0f;
    }
    if (reg.all_of<EnvironmentComponent>(*ctx.selected)) {
        sectionHeader("Environment", ComponentKind::Environment);
        auto& env = reg.get<EnvironmentComponent>(*ctx.selected);

        colorRow("Sky Tint", m_skyTintPicker, env.skyTint);

        // --- Atmosphere ---
        subHeading("Atmosphere");
        checkRow("Sky Enabled", RowId("insp.skyon", *ctx.selected), env.skyEnabled);
        vec3Row("Rayleigh Scatter", RowId("insp.rayscat", *ctx.selected),
                &env.rayleighScattering.x);
        floatRow("Rayleigh Height",
                 RowId("insp.rayh", *ctx.selected), env.rayleighScaleHeight,
                 opts(10.0, 0.0, std::nullopt, " m", 0));
        floatRow("Mie Scatter", RowId("insp.miescat", *ctx.selected),
                 env.mieScattering, opts(0.0001, 0.0, std::nullopt, {}, 6));
        floatRow("Mie Absorption", RowId("insp.mieabs", *ctx.selected),
                 env.mieAbsorption, opts(0.0001, 0.0, std::nullopt, {}, 6));
        floatRow("Mie Height", RowId("insp.mieh", *ctx.selected),
                 env.mieScaleHeight, opts(10.0, 0.0, std::nullopt, " m", 0));
        {
            Slate::SpinBoxOptions g = unit01();
            g.min = g.sliderMin = -0.99;
            g.max = g.sliderMax = 0.99;
            floatRow("Mie Anisotropy", RowId("insp.mieg", *ctx.selected), env.mieAnisotropy, g);
        }
        floatRow("Ozone Scale", RowId("insp.ozone", *ctx.selected),
                 env.ozoneScale, opts(0.01, 0.0, std::nullopt));

        // --- Sky post-processing ---
        y += 4.0f;
        subHeading("Sky Post");
        floatRow("Sky Intensity", RowId("insp.skyi", *ctx.selected),
                 env.skyIntensity, opts(0.01, 0.0, std::nullopt));
        floatRow("Saturation", RowId("insp.sats", *ctx.selected),
                 env.saturation, opts(0.01, 0.0, 2.0));
        floatRow("Exposure", RowId("insp.exp", *ctx.selected), env.exposure,
                 opts(0.01, std::nullopt, std::nullopt, " EV"));

        // --- Ground & ambient ---
        y += 4.0f;
        subHeading("Ground & Ambient");
        colorRow("Ground Color", m_groundColorPicker, env.groundColor);
        floatRow("IBL Intensity", RowId("insp.ibl", *ctx.selected),
                 env.iblIntensity, opts(0.01, 0.0, std::nullopt));
        y += 8.0f;
    }
    if (reg.all_of<LightComponent>(*ctx.selected)) {
        sectionHeader("Light", ComponentKind::Light);
        auto& lc = reg.get<LightComponent>(*ctx.selected);
        lc.type = static_cast<LightType>(
            comboRow("Type", RowId("insp.ltype", *ctx.selected),
                     {"Directional", "Point", "Spot", "Tube"},
                     static_cast<int>(lc.type)));
        colorRow("Color", m_lightColorPicker, lc.color);
        floatRow("Intensity", RowId("insp.lint", *ctx.selected), lc.intensity,
                 opts(0.05, 0.0, std::nullopt));
        if (lc.type == LightType::Directional) {
            // --- Sun disk (directional) ---
            y += 4.0f;
            subHeading("Sun Disk");
            floatRow("Disk Size", RowId("insp.ldisks", *ctx.selected),
                     lc.sunDiskSizeDeg, opts(0.01, 0.0, 30.0, "\u00B0", 2));
            floatRow("Disk Intensity", RowId("insp.ldiski", *ctx.selected),
                     lc.sunDiskIntensity, opts(0.01, 0.0, std::nullopt));
        } else {
            // --- Attenuation (point/spot) ---
            y += 4.0f;
            subHeading("Attenuation");
            floatRow("Radius", RowId("insp.lrad", *ctx.selected),
                     lc.attenuationRadius, opts(0.05, 0.0, std::nullopt, " m"));
            floatRow("Falloff Power", RowId("insp.lpow", *ctx.selected),
                     lc.attenuationPower, opts(0.01, 0.0, 16.0));
            if (lc.type == LightType::Tube) {
                floatRow("Length", RowId("insp.llen", *ctx.selected),
                         lc.length, opts(0.02, 0.0, std::nullopt, " m"));
            }
            if (lc.type == LightType::Spot) {
                floatRow("Inner Angle", RowId("insp.lin", *ctx.selected),
                         lc.innerAngleDeg, opts(0.2, 0.0, 89.0, "\u00B0", 1));
                floatRow("Outer Angle", RowId("insp.lout", *ctx.selected),
                         lc.outerAngleDeg, opts(0.2, 0.0, 89.0, "\u00B0", 1));
            }
        }

        // --- Shadows ---
        y += 4.0f;
        subHeading("Shadows");
        checkRow("Cast Shadows", RowId("insp.lshadows", *ctx.selected), lc.castShadows);
        intRow("Map Size", RowId("insp.lmapsize", *ctx.selected), lc.shadowMapSize,
               opts(4.0, 256.0, 8192.0, " px"));
        floatRow("Bias", RowId("insp.lbias", *ctx.selected), lc.shadowBias,
                 opts(0.0001, 0.0, 0.1, {}, 4));
        floatRow("Normal Bias", RowId("insp.lnbias", *ctx.selected),
                 lc.normalBias, opts(0.001, 0.0, 1.0, {}, 4));
        floatRow("Softness", RowId("insp.lsoft", *ctx.selected),
                 lc.shadowSoftness, opts(0.01, 0.0, std::nullopt));
        if (lc.type == LightType::Directional) {
            intRow("Cascades", RowId("insp.lcascades", *ctx.selected), lc.cascadeCount,
                   opts(0.05, 1.0, 4.0));
            floatRow("Shadow Distance", RowId("insp.ldist", *ctx.selected),
                     lc.shadowDistance, opts(0.5, 1.0, std::nullopt, " m", 1));
            floatRow("Split Lambda", RowId("insp.llambda", *ctx.selected),
                     lc.cascadeSplitLambda, unit01());
        }
        y += 8.0f;
    }
    if (reg.all_of<PostProcessComponent>(*ctx.selected)) {
        sectionHeader("Post Process Volume", ComponentKind::PostProcess);
        auto& pp = reg.get<PostProcessComponent>(*ctx.selected);
        checkRow("Enabled", RowId("insp.ppenabled", *ctx.selected), pp.enabled);

        // --- Bloom ---
        subHeading("Bloom");
        checkRow("Enabled", RowId("insp.pp.bloom", *ctx.selected), pp.bloomEnabled);
        floatRow("Intensity", RowId("insp.pp.bint", *ctx.selected), pp.bloomIntensity);
        floatRow("Threshold", RowId("insp.pp.bthr", *ctx.selected), pp.bloomThreshold);
        y += 4.0f;

        // --- Tone Mapping ---
        subHeading("Tone Mapping");
        pp.toneMapper = static_cast<PostProcessComponent::ToneMapper>(
            comboRow("Tone Mapper", RowId("insp.pp.tone", *ctx.selected),
                     {"ACES", "Reinhard", "AgX", "Linear"},
                     static_cast<int>(pp.toneMapper)));
        floatRow("Exposure", RowId("insp.pp.exp", *ctx.selected), pp.exposure);
        checkRow("Auto Exposure", RowId("insp.pp.autoexp", *ctx.selected), pp.autoExposure);
        if (pp.autoExposure) {
            floatRow("Speed", RowId("insp.pp.aespd", *ctx.selected), pp.autoExposureSpeed);
            floatRow("Min Brightness", RowId("insp.pp.aemin", *ctx.selected), pp.autoExposureMin);
            floatRow("Max Brightness", RowId("insp.pp.aemax", *ctx.selected), pp.autoExposureMax);
        }

        // --- Ambient Occlusion ---
        subHeading("Ambient Occlusion");
        checkRow("SSAO", RowId("insp.pp.ssao", *ctx.selected), pp.ssaoEnabled);
        if (pp.ssaoEnabled) {
            floatRow("Radius", RowId("insp.pp.srad", *ctx.selected), pp.ssaoRadius);
            floatRow("Intensity", RowId("insp.pp.sint", *ctx.selected), pp.ssaoIntensity);
        }
        y += 4.0f;

        // --- Screen Space Reflections ---
        subHeading("Reflections");
        checkRow("SSR", RowId("insp.pp.ssr", *ctx.selected), pp.ssrEnabled);
        if (pp.ssrEnabled) {
            floatRow("Intensity", RowId("insp.pp.sint2", *ctx.selected), pp.ssrIntensity);
        }
        y += 4.0f;

        // --- Motion Blur ---
        subHeading("Motion Blur");
        checkRow("Enabled", RowId("insp.pp.mb", *ctx.selected), pp.motionBlurEnabled);
        if (pp.motionBlurEnabled) {
            floatRow("Amount", RowId("insp.pp.mbamt", *ctx.selected), pp.motionBlurAmount);
        }
        y += 4.0f;

        // --- Depth of Field ---
        subHeading("Depth of Field");
        pp.dofMethod = static_cast<PostProcessComponent::DOFMethod>(
            comboRow("Method", RowId("insp.pp.dof", *ctx.selected),
                     {"Off", "Gaussian", "Circle Bokeh"},
                     static_cast<int>(pp.dofMethod)));
        if (pp.dofMethod != PostProcessComponent::DOFMethod::None) {
            floatRow("Focal Distance", RowId("insp.pp.doffoc", *ctx.selected), pp.dofFocalDistance);
            floatRow("Scale", RowId("insp.pp.dofscale", *ctx.selected), pp.dofScale);
        }
        y += 4.0f;

        // --- Color Grading ---
        subHeading("Color Grading");
        checkRow("Enabled", RowId("insp.pp.cg", *ctx.selected), pp.colorGradingEnabled);
        if (pp.colorGradingEnabled) {
            floatRow("Saturation", RowId("insp.pp.cgsat", *ctx.selected), pp.colorGradingSaturation);
            floatRow("Contrast", RowId("insp.pp.cgcon", *ctx.selected), pp.colorGradingContrast);
            floatRow("Gamma", RowId("insp.pp.cggam", *ctx.selected), pp.colorGradingGamma);
        }
        y += 4.0f;

        // --- Vignette ---
        subHeading("Vignette");
        checkRow("Enabled", RowId("insp.pp.vig", *ctx.selected), pp.vignetteEnabled);
        if (pp.vignetteEnabled) {
            floatRow("Intensity", RowId("insp.pp.vigint", *ctx.selected), pp.vignetteIntensity);
            floatRow("Radius", RowId("insp.pp.vigrad", *ctx.selected), pp.vignetteRadius);
        }
        y += 4.0f;

        // --- Film Grain ---
        subHeading("Film Grain");
        checkRow("Enabled", RowId("insp.pp.fg", *ctx.selected), pp.filmGrainEnabled);
        if (pp.filmGrainEnabled) {
            floatRow("Intensity", RowId("insp.pp.fgint", *ctx.selected), pp.filmGrainIntensity);
        }
        y += 8.0f;
    }

    ui.PopClip();
    // Scroll region spans the content area below the pinned header; the bar
    // starts right under it and the range matches the visible region (so the
    // thumb reaches the end exactly when the last row is visible).
    f32 contentH = (y - contentTop) + m_scroll;
    m_scroll = ui.VerticalScroll(RowId("insp.scroll", *ctx.selected),
                                 contentRegion, contentH, m_scroll);
}

void InspectorPanel::DrawStaticMeshRow(Slate::Context& ui, PanelContext& ctx,
                                       MeshRendererComponent& mr, f32 x, f32& y, f32 w) {
    Slate::Theme& t = ui.theme();
    const Entity e = *ctx.selected;
    const std::string key = "insp.mesh." + std::to_string(entt::to_integral(e));
    // Same row as a material slot:
    //   Static Mesh [thumb]  [ SM_Rock                     v ]
    //                        (<-) (browse) (reset)   Static Mesh
    const f32 kLabelW = NameColumnWidth(w);
    constexpr f32 kThumb = 48.0f;
    constexpr f32 kButton = 22.0f;
    constexpr f32 kGap = 2.0f;
    const Rect row{x, y, w, kThumb + 6.0f};
    m_meshRowRect = row;
    const bool hasMesh = mr.UsesMeshAsset();
    const std::string name = hasMesh ? AssetNameOf(mr.mesh) : std::string("None");

    ui.LabelIn({x, row.y + (kThumb - 18.0f) * 0.5f + 3.0f, kLabelW - 4.0f, 18.0f}, "Static Mesh",
               t.textDim);

    const Rect thumb{x + kLabelW, row.y + 3.0f, kThumb, kThumb};
    const bool thumbHover = thumb.Contains(ui.mouse());
    ui.PanelRoundedBordered(thumb, t.surface2, thumbHover ? t.focusRing : t.outline, t.radius.sm,
                            thumbHover ? t.border.thick : t.border.hairline);
    if (hasMesh && ctx.assetThumbnail) {
        if (TextureHandle tex = ctx.assetThumbnail(mr.mesh)) ui.Image(tex, thumb.Inset(2.0f, 2.0f));
    } else if (!hasMesh) {
        Slate::DrawIcon(ui, thumb.Inset(12.0f, 12.0f), Slate::Icon::StaticMesh, t.textDisabled);
    }
    if (thumbHover && hasMesh && ui.mouseDoubleClicked(0) && ctx.onBrowseToAsset) {
        ctx.onBrowseToAsset(mr.mesh);
    }
    ui.Tooltip(Slate::Context::ID(key + ".thumb"), thumb,
               hasMesh ? mr.mesh : std::string("No static mesh: the Shape primitive is drawn."));

    // Picker: searchable list of every static mesh asset, plus None.
    const f32 cx = thumb.Right() + 6.0f;
    const Rect pick{cx, row.y + 3.0f, std::max(row.Right() - cx - 2.0f, 60.0f), kButton};
    const u64 pickId = Slate::Context::ID(key + ".pick");
    const bool pickHover = pick.Contains(ui.mouse());
    const bool pickOpen = ui.IsMenuOpen(pickId);
    ui.PanelRoundedBordered(pick, pickHover || pickOpen ? t.surface4 : t.surface3,
                            pickOpen ? t.focusRing : t.outline, t.radius.sm, t.border.hairline);
    ui.LabelIn({pick.x + 8.0f, pick.y, pick.w - 28.0f, pick.h}, name, hasMesh ? t.text : t.textDim);
    Slate::DrawIcon(ui, {pick.Right() - 20.0f, pick.y + 4.0f, 14.0f, 14.0f}, Slate::Icon::ChevronDown,
                    t.textDim);
    // Menu actions run later: capture the scene + entity, never the component.
    Scene* scene = ctx.scene;
    auto assign = [scene, e](const std::string& path) {
        if (!scene || !scene->IsValid(e)) return;
        if (auto* m = scene->Registry().try_get<MeshRendererComponent>(e)) AssignStaticMesh(*m, path);
    };
    if (pickHover && ui.mousePressed(0) && !pickOpen) {
        Slate::MenuBuilder b;
        b.BeginSection("Static Meshes");
        b.AddEntry("None", Slate::Icon::Close, [assign] { assign({}); },
                   {.tooltip = "Draw the Shape primitive instead."});
        if (ctx.listMeshes) {
            std::vector<std::string> meshes = ctx.listMeshes();
            std::sort(meshes.begin(), meshes.end(), [](const std::string& a, const std::string& b2) {
                const std::string na = AssetNameOf(a), nb = AssetNameOf(b2);
                return std::lexicographical_compare(
                    na.begin(), na.end(), nb.begin(), nb.end(),
                    [](char l, char r) { return std::tolower(static_cast<unsigned char>(l)) <
                                                std::tolower(static_cast<unsigned char>(r)); });
            });
            auto thumbnail = ctx.assetThumbnail;
            for (const std::string& candidate : meshes) {
                b.AddEntry(AssetNameOf(candidate),
                           Slate::MenuIcon::Thumbnail(
                               [thumbnail, candidate] { return thumbnail ? thumbnail(candidate) : TextureHandle{0}; },
                               Slate::Icon::StaticMesh),
                           [assign, candidate] { assign(candidate); }, {.tooltip = candidate});
            }
        }
        b.EndSection();
        Slate::MenuOptions options;
        options.searchable = true;
        options.searchHint = "Search static meshes";
        options.iconSize = 32.0f;
        options.maxHeight = ui.displayHeight() * 0.45f;
        ui.OpenMenuBelow(pickId, pick, b.Build(), options);
    }
    ui.Tooltip(pickId ^ 0x7177ull, pick, hasMesh ? mr.mesh + "\nStatic Mesh" : "None");

    // Use Selected | Browse to Asset | Reset to Default.
    Rect btn{cx, pick.Bottom() + 4.0f, kButton, kButton};
    const f32 kindX = cx + 3.0f * (kButton + kGap) + 6.0f;
    if (ui.font().Measure("Static Mesh").x <= pick.Right() - kindX) {
        ui.LabelIn({kindX, btn.y, pick.Right() - kindX, kButton}, hasMesh ? "Static Mesh" : "Primitive",
                   t.textDisabled);
    }
    if (ui.IconTextButton(Slate::Context::ID(key + ".use"), btn, Slate::Icon::ArrowLeft, {}, t.textDim) &&
        ctx.selectedAsset) {
        const std::string selected = ctx.selectedAsset();
        auto isMesh = [&ctx](const std::string& path) {
            if (!ctx.listMeshes) return false;
            const std::vector<std::string> meshes = ctx.listMeshes();
            return std::find(meshes.begin(), meshes.end(), path) != meshes.end();
        };
        if (UseSelectedAssetForMesh(mr, selected, isMesh)) {
            LUMA_USER_INFO("Meshes", "Assigned '{}'", selected);
        } else {
            LUMA_USER_WARN("Meshes", "Select a Static Mesh in the Content Browser first");
        }
    }
    ui.Tooltip(Slate::Context::ID(key + ".use.tip"), btn,
               "Use Selected Asset\nAssign the static mesh selected in the Content Browser.");
    btn.x += kButton + kGap;
    if (ui.IconTextButton(Slate::Context::ID(key + ".browse"), btn, Slate::Icon::FolderOpen, {},
                          hasMesh ? t.textDim : t.textDisabled) &&
        hasMesh && ctx.onBrowseToAsset) {
        ctx.onBrowseToAsset(mr.mesh);
    }
    ui.Tooltip(Slate::Context::ID(key + ".browse.tip"), btn,
               "Browse to Asset\nShow the static mesh in the Content Browser.");
    btn.x += kButton + kGap;
    if (ui.IconTextButton(Slate::Context::ID(key + ".reset"), btn, Slate::Icon::Reset, {},
                          hasMesh ? t.textDim : t.textDisabled) &&
        hasMesh) {
        AssignStaticMesh(mr, {});
    }
    ui.Tooltip(Slate::Context::ID(key + ".reset.tip"), btn,
               "Reset to Default\nRemove the static mesh and draw the Shape primitive.");

    // Drop target: a static mesh dragged from the Content Browser.
    if (const Slate::DragPayload* drop = ui.AcceptDrop(row, "asset/mesh")) {
        AssignStaticMesh(mr, drop->data);
        LUMA_USER_INFO("Meshes", "Assigned '{}'", drop->data);
    }
    DrawRowGrid(ui, x - 12.0f, x + w + 12.0f, row.y - 3.0f, row.h + 6.0f, x + kLabelW - 6.0f);
    y += row.h + 6.0f;
}

void InspectorPanel::DrawMaterialSlots(Slate::Context& ui, PanelContext& ctx,
                                       MeshRendererComponent& mr, f32 x, f32& y, f32 w) {
    Slate::Theme& t = ui.theme();
    const Entity e = *ctx.selected;
    // A static mesh names its slots and supplies each slot's default material.
    const std::vector<MeshSlotInfo> meshSlots =
        mr.UsesMeshAsset() && ctx.meshSections ? ctx.meshSections(e) : std::vector<MeshSlotInfo>{};
    const u32 sections = meshSlots.empty()
                             ? std::max(ctx.sectionCount ? ctx.sectionCount(e) : 1u, 1u)
                             : static_cast<u32>(meshSlots.size());
    const u32 rows = std::max<u32>(sections, static_cast<u32>(mr.materials.size()));
    ui.LabelIn({x, y, w, 20}, "Materials", t.text);
    DrawRowGrid(ui, x - 12.0f, x + w + 12.0f, y - 2.0f, 26.0f, 0.0f);
    ui.Tooltip(RowId("insp.mat.tip", *ctx.selected), {x, y, w, 20},
               mr.UsesMeshAsset()
                   ? "Materials\nOne material per mesh section. Empty slots use the mesh's own "
                     "material. Drag a material from the Content Browser onto a slot."
                   : "Materials\nOne material per mesh section. Empty slots use the Default "
                     "Material. Drag a material from the Content Browser onto a slot.");
    y += 28.0f;
    // Unreal-style slot: label column | thumbnail | asset combo + actions.
    //   Element 0   [thumb]  [ M_Bricks                  v ]
    //                        (<-) (search) (reset)   Material Instance
    const f32 kLabelW = NameColumnWidth(w);
    constexpr f32 kThumb = 48.0f;
    constexpr f32 kButton = 22.0f;
    constexpr f32 kGap = 2.0f;
    for (u32 slot = 0; slot < rows; ++slot) {
        const bool unused = slot >= sections;
        const std::string path = slot < mr.materials.size() ? mr.materials[slot] : std::string{};
        const bool isDefault = path.empty();  // not overridden
        const std::string meshDefault =
            slot < meshSlots.size() ? meshSlots[slot].defaultMaterial : std::string{};
        const bool fromMesh = isDefault && !meshDefault.empty();
        const std::string shown = !isDefault  ? path
                                  : fromMesh ? meshDefault
                                             : std::string(Material::kDefaultMaterialPath);
        const usize slash = shown.find_last_of('/');
        const std::string name = shown.substr(slash + 1);
        const Rect row{x, y, w, kThumb + 6.0f};
        if (!unused) m_slotRects.push_back(row);
        const std::string key = "insp.mat." + std::to_string(entt::to_integral(e)) + "." +
                                std::to_string(slot);

        // Label column, centred on the thumbnail.
        const Rect label{x, row.y + (kThumb - 18.0f) * 0.5f + 3.0f, kLabelW - 4.0f, 18.0f};
        const std::string slotLabel =
            unused                     ? "Unused " + std::to_string(slot)
            : slot < meshSlots.size() ? meshSlots[slot].name
                                      : "Element " + std::to_string(slot);
        ui.LabelIn(label, slotLabel, unused ? t.textDisabled : t.textDim);
        if (!unused && slot < meshSlots.size()) {
            ui.Tooltip(Slate::Context::ID(key + ".label"), label,
                       meshSlots[slot].name + "\nElement " + std::to_string(slot));
        }
        if (unused) {
            ui.Tooltip(Slate::Context::ID(key + ".unused"), label,
                       "Unused slot\nThe mesh has fewer sections; the slot is kept for meshes "
                       "with more.");
        }

        // Thumbnail: hover border; double-click opens the Material Editor.
        const Rect thumb{x + kLabelW, row.y + 3.0f, kThumb, kThumb};
        const bool thumbHover = thumb.Contains(ui.mouse());
        ui.PanelRoundedBordered(thumb, t.surface2, thumbHover ? t.focusRing : t.outline,
                                t.radius.sm, thumbHover ? t.border.thick : t.border.hairline);
        if (ctx.assetThumbnail) {
            if (TextureHandle tex = ctx.assetThumbnail(shown)) {
                ui.Image(tex, thumb.Inset(2.0f, 2.0f),
                         isDefault ? Slate::Color::RGBA(255, 255, 255, 150)
                                   : Slate::Color::RGB(255, 255, 255));
            }
        }
        if (thumbHover && ui.mouseDoubleClicked(0) && ctx.onOpenAsset) ctx.onOpenAsset(shown);
        ui.Tooltip(Slate::Context::ID(key + ".thumb"), thumb,
                   name + "\nDouble-click to open it in the Material Editor.");

        // Asset combo: name + chevron; opens a searchable material list.
        const f32 cx = thumb.Right() + 6.0f;
        const Rect pick{cx, row.y + 3.0f, std::max(row.Right() - cx - 2.0f, 60.0f), kButton};
        const u64 pickId = Slate::Context::ID(key + ".pick");
        const bool pickHover = pick.Contains(ui.mouse());
        const bool pickOpen = ui.IsMenuOpen(pickId);
        ui.PanelRoundedBordered(pick, pickHover || pickOpen ? t.surface4 : t.surface3,
                                pickOpen ? t.focusRing : t.outline, t.radius.sm,
                                t.border.hairline);
        ui.LabelIn({pick.x + 8.0f, pick.y, pick.w - 28.0f, pick.h},
                   fromMesh ? name : isDefault ? std::string("DefaultMaterial") : name,
                   isDefault ? t.textDim : t.text);
        Slate::DrawIcon(ui, {pick.Right() - 20.0f, pick.y + 4.0f, 14.0f, 14.0f},
                        Slate::Icon::ChevronDown, t.textDim);
        if (pickHover && ui.mousePressed(0) && !pickOpen) {
            Slate::MenuBuilder b;
            b.BeginSection("Actions");
            b.AddEntry("Create Material Instance", Slate::Icon::Plus,
                       [&mr, slot, lib = ctx.materials] {
                           if (!lib) return;
                           std::string error;
                           if (CreateInstanceForSlot(mr, slot, *lib, &error).empty()) {
                               LUMA_USER_ERROR("Materials",
                                               "Could not create a Material Instance: {}", error);
                           }
                       },
                       {.tooltip = "Create a Material Instance of this material next to it "
                                   "and assign it here."});
            b.EndSection();
            b.BeginSection("Materials");
            b.AddEntry(fromMesh || !meshDefault.empty() ? "Mesh Material" : "DefaultMaterial", {},
                       [&mr, slot] { AssignMaterialSlot(mr, slot, {}); },
                       {.tooltip = meshDefault.empty() ? std::string(Material::kDefaultMaterialPath)
                                                       : "The mesh's own material: " + meshDefault});
            if (ctx.listMaterials) {
                auto thumbnail = ctx.assetThumbnail;
                for (const std::string& candidate : ctx.listMaterials()) {
                    if (candidate == Material::kDefaultMaterialPath) continue;
                    const usize s2 = candidate.find_last_of('/');
                    b.AddEntry(candidate.substr(s2 + 1),
                               Slate::MenuIcon::Thumbnail(
                                   [thumbnail, candidate] {
                                       return thumbnail ? thumbnail(candidate) : TextureHandle{0};
                                   },
                                   Slate::Icon::Sphere),
                               [&mr, slot, candidate] { AssignMaterialSlot(mr, slot, candidate); },
                               {.tooltip = candidate});
                }
            }
            b.EndSection();
            Slate::MenuOptions options;
            options.searchable = true;
            options.searchHint = "Search materials";
            options.iconSize = 32.0f;
            options.maxHeight = ui.displayHeight() * 0.45f;
            ui.OpenMenuBelow(pickId, pick, b.Build(), options);
        }
        ui.Tooltip(pickId ^ 0x7177ull, pick,
                   shown + "\n" + (isDefault ? "Default Material" : ctx.materials &&
                                        ctx.materials->IsInstance(shown) ? "Material Instance"
                                                                         : "Material"));

        // Second line: Use Selected | Browse to Asset | Reset to Default, then
        // what kind of asset the slot holds.
        Rect btn{cx, pick.Bottom() + 4.0f, kButton, kButton};
        const bool instance = !isDefault && ctx.materials && ctx.materials->IsInstance(shown);
        const char* kind = fromMesh    ? "Mesh Material"
                           : isDefault ? "Default Material"
                           : instance  ? "Material Instance"
                                       : "Material";
        const f32 kindX = cx + 3.0f * (kButton + kGap) + 6.0f;
        if (ui.font().Measure(kind).x <= pick.Right() - kindX) {  // only when it fits
            ui.LabelIn({kindX, btn.y, pick.Right() - kindX, kButton}, kind, t.textDisabled);
        }
        if (ui.IconTextButton(Slate::Context::ID(key + ".use"), btn, Slate::Icon::ArrowLeft, {},
                              t.textDim) &&
            ctx.selectedAsset && ctx.materials) {
            const std::string selected = ctx.selectedAsset();
            if (UseSelectedAssetForSlot(mr, slot, selected, *ctx.materials)) {
                LUMA_USER_INFO("Materials", "Assigned '{}' to element {}", selected, slot);
            } else {
                LUMA_USER_WARN("Materials",
                               "Select a Material or Material Instance in the Content Browser "
                               "first");
            }
        }
        ui.Tooltip(Slate::Context::ID(key + ".use.tip"), btn,
                   "Use Selected Asset\nAssign the material selected in the Content Browser.");
        btn.x += kButton + kGap;
        if (ui.IconTextButton(Slate::Context::ID(key + ".browse"), btn, Slate::Icon::FolderOpen, {},
                              t.textDim) &&
            ctx.onBrowseToAsset) {
            ctx.onBrowseToAsset(shown);
        }
        ui.Tooltip(Slate::Context::ID(key + ".browse.tip"), btn,
                   "Browse to Asset\nShow the material in the Content Browser.");
        btn.x += kButton + kGap;
        if (ui.IconTextButton(Slate::Context::ID(key + ".reset"), btn, Slate::Icon::Reset, {},
                              isDefault ? t.textDisabled : t.textDim) &&
            !isDefault) {
            AssignMaterialSlot(mr, slot, {});
        }
        ui.Tooltip(Slate::Context::ID(key + ".reset.tip"), btn,
                   meshDefault.empty() ? "Reset to Default\nUse the Default Material for this slot."
                                       : "Reset to Default\nUse the mesh's own material for this slot.");

        // Drop target: a material dragged from the Content Browser.
        if (!unused) {
            if (const Slate::DragPayload* drop = ui.AcceptDrop(row, "asset/material")) {
                AssignMaterialSlot(mr, slot, drop->data);
                LUMA_USER_INFO("Materials", "Assigned '{}' to element {}", drop->data, slot);
            }
        }
        DrawRowGrid(ui, x - 12.0f, x + w + 12.0f, row.y - 3.0f, row.h + 6.0f, x + kLabelW - 6.0f);
        y += row.h + 6.0f;
    }
}

void InspectorPanel::ClosePopups() {
    m_lightColorPicker.Close();
    m_skyTintPicker.Close();
    m_groundColorPicker.Close();
}

void InspectorPanel::DrawFloatingPickers(Slate::Context& ui,
                                         PanelContext& ctx) {
    // Called after the dock (clip stack unwound) so the floating picker
    // panels can overflow this column. Only draw panels whose component is
    // present on the current selection; OnColorChanged (set per-row in Draw)
    // writes the edits straight into the component.
    if (!ctx.scene->IsValid(*ctx.selected)) return;
    auto& reg = ctx.scene->Registry();
    if (reg.all_of<EnvironmentComponent>(*ctx.selected)) {
        m_skyTintPicker.DrawPanel(ui);
        m_groundColorPicker.DrawPanel(ui);
    }
    if (reg.all_of<LightComponent>(*ctx.selected)) {
        m_lightColorPicker.DrawPanel(ui);
    }
}

}  // namespace Luma::Editor::Panels
