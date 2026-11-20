#include "Luma/Editor/Panels/Inspector.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include "Luma/Scene/Components.h"

namespace Luma::Editor::Panels {

using Slate::Align;
using Slate::Rect;

namespace {

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

void InspectorPanel::Draw(Slate::Context& ui, const Slate::Rect& body,
                          PanelContext& ctx) {
    Slate::Theme& t = ui.theme();

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

    // Header: entity name + "Add" component picker (Unreal Details panel).
    const u64 addId = Slate::Context::ID("insp.addcomp");
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
        ui.LabelIn({x, y, 82, 22}, label, t.textDim);
        ui.Vector3Field(id, {x + 82, y, w - 82, 22}, v);
        y += 28.0f;
    };
    auto floatRow = [&](const char* label, u64 id, f32& v) {
        ui.LabelIn({x, y, 118, 22}, label, t.textDim);
        ui.DragFloat(id, {x + 118, y, w - 118, 22}, v, 0.02f);
        y += 28.0f;
    };
    // Enum property: label + combo box (fits any column width, unlike a
    // segmented tab row). Returns the (possibly changed) index.
    auto comboRow = [&](const char* label, u64 id,
                        const std::vector<std::string>& items, int current) {
        ui.LabelIn({x, y, 82, 22}, label, t.textDim);
        int picked = ui.Combo(id, {x + 82, y, w - 82, 22}, items, current);
        y += 28.0f;
        return picked;
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
        ui.LabelIn({x, y, 82, 22}, label, t.textDim);
        // Wide swatch strip filling the rest of the row; clicking it opens
        // the floating color picker.
        Rect sw{x + 82, y + 2, w - 82, 18};
        // Edits write straight back into the component; the picker follows
        // the component through SetColor below (no-op while editing).
        popup.OnColorChanged = [&](const Slate::Color& c) {
            value = colorToVec3(c);
        };
        popup.SetColor(vec3ToColor(value));
        popup.DrawSwatch(ui, sw);
        y += 28.0f;
    };

    if (reg.all_of<TransformComponent>(*ctx.selected)) {
        sectionHeader("Transform", ComponentKind::Transform);
        auto& tf = reg.get<TransformComponent>(*ctx.selected);
        vec3Row("Position", Slate::Context::ID("insp.pos"), &tf.position.x);
        vec3Row("Rotation", Slate::Context::ID("insp.rot"),
                &tf.rotationEuler.x);
        vec3Row("Scale", Slate::Context::ID("insp.scl"), &tf.scale.x);
        y += 8.0f;
    }
    if (reg.all_of<MeshRendererComponent>(*ctx.selected)) {
        sectionHeader("Mesh Renderer", ComponentKind::MeshRenderer);
        auto& mr = reg.get<MeshRendererComponent>(*ctx.selected);
        mr.primitive = static_cast<MeshPrimitive>(
            comboRow("Shape", Slate::Context::ID("insp.shape"),
                     {"Cube", "Plane", "Sphere", "Cylinder"},
                     static_cast<int>(mr.primitive)));
        colorRow("Albedo", m_albedoPicker, mr.albedo);
        floatRow("Metallic", Slate::Context::ID("insp.met"), mr.metallic);
        floatRow("Roughness", Slate::Context::ID("insp.rgh"), mr.roughness);
        y += 8.0f;
    }
    if (reg.all_of<CameraComponent>(*ctx.selected)) {
        sectionHeader("Camera", ComponentKind::Camera);
        auto& cam = reg.get<CameraComponent>(*ctx.selected);
        cam.projection = static_cast<ProjectionType>(
            comboRow("Projection", Slate::Context::ID("insp.camproj"),
                     {"Perspective", "Orthographic"},
                     static_cast<int>(cam.projection)));
        if (cam.projection == ProjectionType::Perspective) {
            floatRow("Field of View", Slate::Context::ID("insp.camfov"),
                     cam.fovYDegrees);
            cam.fovYDegrees = std::clamp(cam.fovYDegrees, 5.0f, 170.0f);
        } else {
            floatRow("Ortho Height", Slate::Context::ID("insp.camortho"),
                     cam.orthoHeight);
            cam.orthoHeight = std::max(cam.orthoHeight, 0.01f);
        }
        floatRow("Near Clip", Slate::Context::ID("insp.camnear"), cam.nearZ);
        floatRow("Far Clip", Slate::Context::ID("insp.camfar"), cam.farZ);
        cam.nearZ = std::max(cam.nearZ, 0.001f);
        cam.farZ = std::max(cam.farZ, cam.nearZ + 0.01f);
        ui.Checkbox(Slate::Context::ID("insp.camprimary"), {x, y, 18, 18},
                    "Primary Camera", cam.primary);
        y += 26.0f;
        y += 8.0f;
    }
    if (reg.all_of<EnvironmentComponent>(*ctx.selected)) {
        sectionHeader("Environment", ComponentKind::Environment);
        auto& env = reg.get<EnvironmentComponent>(*ctx.selected);

        colorRow("Sky Tint", m_skyTintPicker, env.skyTint);

        // --- Atmosphere ---
        ui.LabelIn({x, y, w, 20}, "Atmosphere", t.accent);
        y += 24.0f;
        ui.Checkbox(Slate::Context::ID("insp.skyon"), {x, y, 18, 18},
                    "Sky Enabled", env.skyEnabled);
        y += 26.0f;
        vec3Row("Rayleigh Scatter", Slate::Context::ID("insp.rayscat"),
                &env.rayleighScattering.x);
        floatRow("Rayleigh Height (m)",
                 Slate::Context::ID("insp.rayh"), env.rayleighScaleHeight);
        floatRow("Mie Scatter", Slate::Context::ID("insp.miescat"),
                 env.mieScattering);
        floatRow("Mie Absorption", Slate::Context::ID("insp.mieabs"),
                 env.mieAbsorption);
        floatRow("Mie Height (m)", Slate::Context::ID("insp.mieh"),
                 env.mieScaleHeight);
        floatRow("Mie Anisotropy", Slate::Context::ID("insp.mieg"),
                 env.mieAnisotropy);
        floatRow("Ozone Scale", Slate::Context::ID("insp.ozone"),
                 env.ozoneScale);

        // --- Sky post-processing ---
        y += 4.0f;
        ui.LabelIn({x, y, w, 20}, "Sky Post", t.accent);
        y += 24.0f;
        floatRow("Sky Intensity", Slate::Context::ID("insp.skyi"),
                 env.skyIntensity);
        floatRow("Saturation", Slate::Context::ID("insp.sats"),
                 env.saturation);
        floatRow("Exposure", Slate::Context::ID("insp.exp"), env.exposure);

        // --- Ground & ambient ---
        y += 4.0f;
        ui.LabelIn({x, y, w, 20}, "Ground & Ambient", t.accent);
        y += 24.0f;
        colorRow("Ground Color", m_groundColorPicker, env.groundColor);
        floatRow("IBL Intensity", Slate::Context::ID("insp.ibl"),
                 env.iblIntensity);
        y += 8.0f;
    }
    if (reg.all_of<LightComponent>(*ctx.selected)) {
        sectionHeader("Light", ComponentKind::Light);
        auto& lc = reg.get<LightComponent>(*ctx.selected);
        lc.type = static_cast<LightType>(
            comboRow("Type", Slate::Context::ID("insp.ltype"),
                     {"Directional", "Point", "Spot", "Tube"},
                     static_cast<int>(lc.type)));
        colorRow("Color", m_lightColorPicker, lc.color);
        floatRow("Intensity", Slate::Context::ID("insp.lint"), lc.intensity);
        if (lc.type == LightType::Directional) {
            // --- Sun disk (directional) ---
            y += 4.0f;
            ui.LabelIn({x, y, w, 20}, "Sun Disk", t.accent);
            y += 24.0f;
            floatRow("Disk Size (deg)", Slate::Context::ID("insp.ldisks"),
                     lc.sunDiskSizeDeg);
            floatRow("Disk Intensity", Slate::Context::ID("insp.ldiski"),
                     lc.sunDiskIntensity);
        } else {
            // --- Attenuation (point/spot) ---
            y += 4.0f;
            ui.LabelIn({x, y, w, 20}, "Attenuation", t.accent);
            y += 24.0f;
            floatRow("Radius", Slate::Context::ID("insp.lrad"),
                     lc.attenuationRadius);
            floatRow("Falloff Power", Slate::Context::ID("insp.lpow"),
                     lc.attenuationPower);
            if (lc.type == LightType::Tube) {
                floatRow("Length", Slate::Context::ID("insp.llen"),
                         lc.length);
            }
            if (lc.type == LightType::Spot) {
                floatRow("Inner Angle", Slate::Context::ID("insp.lin"),
                         lc.innerAngleDeg);
                floatRow("Outer Angle", Slate::Context::ID("insp.lout"),
                         lc.outerAngleDeg);
            }
        }

        // --- Shadows ---
        y += 4.0f;
        ui.LabelIn({x, y, w, 20}, "Shadows", t.accent);
        y += 24.0f;
        ui.Checkbox(Slate::Context::ID("insp.lshadows"), {x, y, 18, 18},
                    "Cast Shadows", lc.castShadows);
        y += 26.0f;
        f32 mapSize = static_cast<f32>(lc.shadowMapSize);
        floatRow("Map Size", Slate::Context::ID("insp.lmapsize"), mapSize);
        lc.shadowMapSize = static_cast<i32>(mapSize);
        floatRow("Bias", Slate::Context::ID("insp.lbias"), lc.shadowBias);
        floatRow("Normal Bias", Slate::Context::ID("insp.lnbias"),
                 lc.normalBias);
        floatRow("Softness", Slate::Context::ID("insp.lsoft"),
                 lc.shadowSoftness);
        if (lc.type == LightType::Directional) {
            f32 cascades = static_cast<f32>(lc.cascadeCount);
            floatRow("Cascades", Slate::Context::ID("insp.lcascades"),
                     cascades);
            lc.cascadeCount = static_cast<i32>(cascades);
            floatRow("Shadow Distance", Slate::Context::ID("insp.ldist"),
                     lc.shadowDistance);
            floatRow("Split Lambda", Slate::Context::ID("insp.llambda"),            lc.cascadeSplitLambda);
        }
        y += 8.0f;
    }
    if (reg.all_of<PostProcessComponent>(*ctx.selected)) {
        sectionHeader("Post Process Volume", ComponentKind::PostProcess);
        auto& pp = reg.get<PostProcessComponent>(*ctx.selected);
        ui.Checkbox(Slate::Context::ID("insp.ppenabled"), {x, y, 18, 18},
                    "Enabled", pp.enabled);
        y += 26.0f;

        // --- Bloom ---
        ui.LabelIn({x, y, w, 20}, "Bloom", t.accent);
        y += 24.0f;
        ui.Checkbox(Slate::Context::ID("insp.pp.bloom"), {x + 16, y, 18, 18},
                    "Enabled", pp.bloomEnabled);
        y += 26.0f;
        floatRow("Intensity", Slate::Context::ID("insp.pp.bint"), pp.bloomIntensity);
        floatRow("Threshold", Slate::Context::ID("insp.pp.bthr"), pp.bloomThreshold);
        y += 4.0f;

        // --- Tone Mapping ---
        ui.LabelIn({x, y, w, 20}, "Tone Mapping", t.accent);
        y += 24.0f;
        pp.toneMapper = static_cast<PostProcessComponent::ToneMapper>(
            comboRow("Tone Mapper", Slate::Context::ID("insp.pp.tone"),
                     {"ACES", "Reinhard", "AgX", "Linear"},
                     static_cast<int>(pp.toneMapper)));
        floatRow("Exposure", Slate::Context::ID("insp.pp.exp"), pp.exposure);
        ui.Checkbox(Slate::Context::ID("insp.pp.autoexp"), {x + 16, y, 18, 18},
                    "Auto Exposure", pp.autoExposure);
        y += 26.0f;
        if (pp.autoExposure) {
            floatRow("Speed", Slate::Context::ID("insp.pp.aespd"), pp.autoExposureSpeed);
            floatRow("Min Brightness", Slate::Context::ID("insp.pp.aemin"), pp.autoExposureMin);
            floatRow("Max Brightness", Slate::Context::ID("insp.pp.aemax"), pp.autoExposureMax);
        }

        // --- Ambient Occlusion ---
        ui.LabelIn({x, y, w, 20}, "Ambient Occlusion", t.accent);
        y += 24.0f;
        ui.Checkbox(Slate::Context::ID("insp.pp.ssao"), {x + 16, y, 18, 18},
                    "SSAO", pp.ssaoEnabled);
        y += 26.0f;
        if (pp.ssaoEnabled) {
            floatRow("Radius", Slate::Context::ID("insp.pp.srad"), pp.ssaoRadius);
            floatRow("Intensity", Slate::Context::ID("insp.pp.sint"), pp.ssaoIntensity);
        }
        y += 4.0f;

        // --- Screen Space Reflections ---
        ui.LabelIn({x, y, w, 20}, "Reflections", t.accent);
        y += 24.0f;
        ui.Checkbox(Slate::Context::ID("insp.pp.ssr"), {x + 16, y, 18, 18},
                    "SSR", pp.ssrEnabled);
        y += 26.0f;
        if (pp.ssrEnabled) {
            floatRow("Intensity", Slate::Context::ID("insp.pp.sint2"), pp.ssrIntensity);
        }
        y += 4.0f;

        // --- Motion Blur ---
        ui.LabelIn({x, y, w, 20}, "Motion Blur", t.accent);
        y += 24.0f;
        ui.Checkbox(Slate::Context::ID("insp.pp.mb"), {x + 16, y, 18, 18},
                    "Enabled", pp.motionBlurEnabled);
        y += 26.0f;
        if (pp.motionBlurEnabled) {
            floatRow("Amount", Slate::Context::ID("insp.pp.mbamt"), pp.motionBlurAmount);
        }
        y += 4.0f;

        // --- Depth of Field ---
        ui.LabelIn({x, y, w, 20}, "Depth of Field", t.accent);
        y += 24.0f;
        pp.dofMethod = static_cast<PostProcessComponent::DOFMethod>(
            comboRow("Method", Slate::Context::ID("insp.pp.dof"),
                     {"Off", "Gaussian", "Circle Bokeh"},
                     static_cast<int>(pp.dofMethod)));
        if (pp.dofMethod != PostProcessComponent::DOFMethod::None) {
            floatRow("Focal Distance", Slate::Context::ID("insp.pp.doffoc"), pp.dofFocalDistance);
            floatRow("Scale", Slate::Context::ID("insp.pp.dofscale"), pp.dofScale);
        }
        y += 4.0f;

        // --- Color Grading ---
        ui.LabelIn({x, y, w, 20}, "Color Grading", t.accent);
        y += 24.0f;
        ui.Checkbox(Slate::Context::ID("insp.pp.cg"), {x + 16, y, 18, 18},
                    "Enabled", pp.colorGradingEnabled);
        y += 26.0f;
        if (pp.colorGradingEnabled) {
            floatRow("Saturation", Slate::Context::ID("insp.pp.cgsat"), pp.colorGradingSaturation);
            floatRow("Contrast", Slate::Context::ID("insp.pp.cgcon"), pp.colorGradingContrast);
            floatRow("Gamma", Slate::Context::ID("insp.pp.cggam"), pp.colorGradingGamma);
        }
        y += 4.0f;

        // --- Vignette ---
        ui.LabelIn({x, y, w, 20}, "Vignette", t.accent);
        y += 24.0f;
        ui.Checkbox(Slate::Context::ID("insp.pp.vig"), {x + 16, y, 18, 18},
                    "Enabled", pp.vignetteEnabled);
        y += 26.0f;
        if (pp.vignetteEnabled) {
            floatRow("Intensity", Slate::Context::ID("insp.pp.vigint"), pp.vignetteIntensity);
            floatRow("Radius", Slate::Context::ID("insp.pp.vigrad"), pp.vignetteRadius);
        }
        y += 4.0f;

        // --- Film Grain ---
        ui.LabelIn({x, y, w, 20}, "Film Grain", t.accent);
        y += 24.0f;
        ui.Checkbox(Slate::Context::ID("insp.pp.fg"), {x + 16, y, 18, 18},
                    "Enabled", pp.filmGrainEnabled);
        y += 26.0f;
        if (pp.filmGrainEnabled) {
            floatRow("Intensity", Slate::Context::ID("insp.pp.fgint"), pp.filmGrainIntensity);
        }
        y += 8.0f;
    }

    ui.PopClip();
    // Scroll region spans the content area below the pinned header; the bar
    // starts right under it and the range matches the visible region (so the
    // thumb reaches the end exactly when the last row is visible).
    f32 contentH = (y - contentTop) + m_scroll;
    m_scroll = ui.VerticalScroll(Slate::Context::ID("insp.scroll"),
                                 contentRegion, contentH, m_scroll);
}

void InspectorPanel::ClosePopups() {
    m_albedoPicker.Close();
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
    if (reg.all_of<MeshRendererComponent>(*ctx.selected)) {
        m_albedoPicker.DrawPanel(ui);
    }
    if (reg.all_of<EnvironmentComponent>(*ctx.selected)) {
        m_skyTintPicker.DrawPanel(ui);
        m_groundColorPicker.DrawPanel(ui);
    }
    if (reg.all_of<LightComponent>(*ctx.selected)) {
        m_lightColorPicker.DrawPanel(ui);
    }
}

}  // namespace Luma::Editor::Panels
