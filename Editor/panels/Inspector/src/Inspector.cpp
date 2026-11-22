#include "Luma/Editor/Panels/Inspector.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "Luma/Core/Log.h"
#include "Luma/Editor/EditorIcons.h"
#include "Luma/Editor/Panels/MaterialSlots.h"
#include "Luma/Editor/Panels/StaticMeshSlot.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Serialization/Json.h"
#include "Luma/Slate/Icons.h"
#include "Luma/Scene/Components.h"
#include "Luma/Slate/Menu.h"

namespace Luma::Editor::Panels {

using Slate::Align;
using Slate::Rect;

namespace {

// A color-row setter that looks the component up again when the floating picker
// reports a change (later in the frame), so it never holds a dangling reference.
template <typename C>
std::function<void(const Math::Vec3&)> SetVec3(Scene& scene, Entity e, Math::Vec3 C::*field) {
    Scene* s = &scene;
    return [s, e, field](const Math::Vec3& v) {
        if (!s->IsValid(e)) return;
        if (C* c = s->Registry().try_get<C>(e)) c->*field = v;
    };
}

// Spin box option presets: limits, drag speed, unit suffix.
Slate::SpinBoxOptions Opts(f64 speed, std::optional<f64> lo, std::optional<f64> hi,
                           std::string_view suffix = {}, int maxDigits = 3) {
    Slate::SpinBoxOptions o;
    o.speed = speed;
    o.min = lo;
    o.max = hi;
    o.suffix = suffix;
    o.maxFractionalDigits = maxDigits;
    return o;
}

// 0..1 values get a slider fill bar.
Slate::SpinBoxOptions Unit01(f64 speed = 0.005) {
    Slate::SpinBoxOptions o;
    o.speed = speed;
    o.min = 0.0;
    o.max = 1.0;
    o.sliderMin = 0.0;
    o.sliderMax = 1.0;
    return o;
}

// Combo over an enum field.
template <typename E>
void EnumCombo(InspectorRows& rows, std::string_view label, std::string_view key,
               const std::vector<std::string>& items, E& value) {
    int current = static_cast<int>(value);
    if (rows.Combo(label, key, items, current)) value = static_cast<E>(current);
}

// --- Built-in component drawers -------------------------------------------------

void DrawTransform(InspectorRows& rows) {
    auto& tf = rows.scene.Registry().get<TransformComponent>(rows.entity);
    rows.Vec3("Position", "insp.pos", tf.position);
    rows.Vec3("Rotation", "insp.rot", tf.rotationEuler);
    rows.Vec3("Scale", "insp.scl", tf.scale);
}

// Mesh Renderer "Static Mesh" row (Unreal Details): thumbnail, searchable mesh
// picker, Use Selected / Browse / Reset; accepts dropped meshes.
void DrawStaticMeshRow(InspectorRows& rows, MeshRendererComponent& mr) {
    Slate::Context& ui = rows.ui;
    PanelContext& ctx = rows.panel;
    const f32 x = rows.x;
    const f32 w = rows.w;
    f32& y = rows.y;
    Slate::Theme& t = ui.theme();
    const Entity e = rows.entity;
    const std::string key = "insp.mesh." + std::to_string(entt::to_integral(e));
    // Same row as a material slot:
    //   Static Mesh [thumb]  [ SM_Rock                     v ]
    //                        (<-) (browse) (reset)   Static Mesh
    const f32 kLabelW = rows.NameColumn();
    constexpr f32 kThumb = 48.0f;
    constexpr f32 kButton = 22.0f;
    constexpr f32 kGap = 2.0f;
    const Rect row{x, y, w, kThumb + 6.0f};
    if (rows.staticMeshRowRect) *rows.staticMeshRowRect = row;
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
    if (ui.IconTextButton(Slate::Context::ID(key + ".use"), btn, Slate::Icon::UseSelected, {}, Slate::Color::RGB(255, 255, 255)) &&
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
    if (ui.IconTextButton(Slate::Context::ID(key + ".browse"), btn, Slate::Icon::Browse, {},
                          hasMesh ? Slate::Color::RGB(255, 255, 255) : t.textDisabled) &&
        hasMesh && ctx.onBrowseToAsset) {
        ctx.onBrowseToAsset(mr.mesh);
    }
    ui.Tooltip(Slate::Context::ID(key + ".browse.tip"), btn,
               "Browse to Asset\nShow the static mesh in the Content Browser.");
    btn.x += kButton + kGap;
    if (ui.IconTextButton(Slate::Context::ID(key + ".reset"), btn, Slate::Icon::Reset, {},
                          hasMesh ? Slate::Color::RGB(255, 255, 255) : t.textDisabled) &&
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
    rows.Grid(row.y - 3.0f, row.h + 6.0f, x + kLabelW - 6.0f);
    y += row.h + 6.0f;
}

// Mesh Renderer "Materials" list: one row per section (thumbnail, picker,
// browse, clear, create instance; drop target), extra slots as Unused.
void DrawMaterialSlots(InspectorRows& rows, MeshRendererComponent& mr) {
    Slate::Context& ui = rows.ui;
    PanelContext& ctx = rows.panel;
    const f32 x = rows.x;
    const f32 w = rows.w;
    f32& y = rows.y;
    Slate::Theme& t = ui.theme();
    const Entity e = rows.entity;
    // A static mesh names its slots and supplies each slot's default material.
    const std::vector<MeshSlotInfo> meshSlots =
        mr.UsesMeshAsset() && ctx.meshSections ? ctx.meshSections(e) : std::vector<MeshSlotInfo>{};
    const u32 sections = meshSlots.empty()
                             ? std::max(ctx.sectionCount ? ctx.sectionCount(e) : 1u, 1u)
                             : static_cast<u32>(meshSlots.size());
    const u32 slotCount = std::max<u32>(sections, static_cast<u32>(mr.materials.size()));
    ui.LabelIn({x, y, w, 20}, "Materials", t.text);
    rows.Grid(y - 2.0f, 26.0f, 0.0f);
    ui.Tooltip(rows.Id("insp.mat.tip"), {x, y, w, 20},
               mr.UsesMeshAsset()
                   ? "Materials\nOne material per mesh section. Empty slots use the mesh's own "
                     "material. Drag a material from the Content Browser onto a slot."
                   : "Materials\nOne material per mesh section. Empty slots use the Default "
                     "Material. Drag a material from the Content Browser onto a slot.");
    y += 28.0f;
    // Unreal-style slot: label column | thumbnail | asset combo + actions.
    //   Element 0   [thumb]  [ M_Bricks                  v ]
    //                        (<-) (search) (reset)   Material Instance
    const f32 kLabelW = rows.NameColumn();
    constexpr f32 kThumb = 48.0f;
    constexpr f32 kButton = 22.0f;
    constexpr f32 kGap = 2.0f;
    for (u32 slot = 0; slot < slotCount; ++slot) {
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
        if (!unused && rows.materialSlotRects) rows.materialSlotRects->push_back(row);
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
        if (ui.IconTextButton(Slate::Context::ID(key + ".use"), btn, Slate::Icon::UseSelected, {},
                              Slate::Color::RGB(255, 255, 255)) &&
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
        if (ui.IconTextButton(Slate::Context::ID(key + ".browse"), btn, Slate::Icon::Browse, {},
                              Slate::Color::RGB(255, 255, 255)) &&
            ctx.onBrowseToAsset) {
            ctx.onBrowseToAsset(shown);
        }
        ui.Tooltip(Slate::Context::ID(key + ".browse.tip"), btn,
                   "Browse to Asset\nShow the material in the Content Browser.");
        btn.x += kButton + kGap;
        if (ui.IconTextButton(Slate::Context::ID(key + ".reset"), btn, Slate::Icon::Reset, {},
                              isDefault ? t.textDisabled : Slate::Color::RGB(255, 255, 255)) &&
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
        rows.Grid(row.y - 3.0f, row.h + 6.0f, x + kLabelW - 6.0f);
        y += row.h + 6.0f;
    }
}

void DrawMeshRenderer(InspectorRows& rows) {
    auto& mr = rows.scene.Registry().get<MeshRendererComponent>(rows.entity);
    DrawStaticMeshRow(rows, mr);
    // The primitive is only drawn while no static mesh is assigned.
    if (!mr.UsesMeshAsset()) {
        EnumCombo(rows, "Shape", "insp.shape", {"Cube", "Plane", "Sphere", "Cylinder"},
                  mr.primitive);
    }
    DrawMaterialSlots(rows, mr);
}

void DrawCamera(InspectorRows& rows) {
    auto& cam = rows.scene.Registry().get<CameraComponent>(rows.entity);
    EnumCombo(rows, "Projection", "insp.camproj", {"Perspective", "Orthographic"},
              cam.projection);
    if (cam.projection == ProjectionType::Perspective) {
        rows.Float("Field of View", "insp.camfov", cam.fovYDegrees,
                   Opts(0.2, 5.0, 170.0, "°", 1));
    } else {
        rows.Float("Ortho Height", "insp.camortho", cam.orthoHeight,
                   Opts(0.05, 0.01, std::nullopt, " m"));
    }
    rows.Float("Near Clip", "insp.camnear", cam.nearZ, Opts(0.005, 0.001, std::nullopt, " m"));
    rows.Float("Far Clip", "insp.camfar", cam.farZ, Opts(1.0, 0.01, std::nullopt, " m", 1));
    // Sanitise values that may come from a hand-edited scene file (the
    // spin-box limits only apply to edits made here).
    cam.fovYDegrees = std::clamp(cam.fovYDegrees, 5.0f, 170.0f);
    cam.orthoHeight = std::max(cam.orthoHeight, 0.01f);
    cam.nearZ = std::max(cam.nearZ, 0.001f);
    cam.farZ = std::max(cam.farZ, cam.nearZ + 0.01f);
    rows.Check("Primary Camera", "insp.camprimary", cam.primary);
}

void DrawEnvironment(InspectorRows& rows) {
    auto& env = rows.scene.Registry().get<EnvironmentComponent>(rows.entity);
    rows.Color("Sky Tint", "insp.skytint", env.skyTint,
               SetVec3(rows.scene, rows.entity, &EnvironmentComponent::skyTint));

    rows.SubHeading("Atmosphere");
    rows.Check("Sky Enabled", "insp.skyon", env.skyEnabled);
    rows.Vec3("Rayleigh Scatter", "insp.rayscat", env.rayleighScattering);
    rows.Float("Rayleigh Height", "insp.rayh", env.rayleighScaleHeight,
               Opts(10.0, 0.0, std::nullopt, " m", 0));
    rows.Float("Mie Scatter", "insp.miescat", env.mieScattering,
               Opts(0.0001, 0.0, std::nullopt, {}, 6));
    rows.Float("Mie Absorption", "insp.mieabs", env.mieAbsorption,
               Opts(0.0001, 0.0, std::nullopt, {}, 6));
    rows.Float("Mie Height", "insp.mieh", env.mieScaleHeight,
               Opts(10.0, 0.0, std::nullopt, " m", 0));
    {
        Slate::SpinBoxOptions g = Unit01();
        g.min = g.sliderMin = -0.99;
        g.max = g.sliderMax = 0.99;
        rows.Float("Mie Anisotropy", "insp.mieg", env.mieAnisotropy, g);
    }
    rows.Float("Ozone Scale", "insp.ozone", env.ozoneScale, Opts(0.01, 0.0, std::nullopt));

    rows.SubHeading("Sky Post");
    rows.Float("Sky Intensity", "insp.skyi", env.skyIntensity, Opts(0.01, 0.0, std::nullopt));
    rows.Float("Saturation", "insp.sats", env.saturation, Opts(0.01, 0.0, 2.0));
    rows.Float("Exposure", "insp.exp", env.exposure,
               Opts(0.01, std::nullopt, std::nullopt, " EV"));

    rows.SubHeading("Ground & Ambient");
    rows.Color("Ground Color", "insp.groundcolor", env.groundColor,
               SetVec3(rows.scene, rows.entity, &EnvironmentComponent::groundColor));
    rows.Float("IBL Intensity", "insp.ibl", env.iblIntensity, Opts(0.01, 0.0, std::nullopt));
}

void DrawLight(InspectorRows& rows) {
    auto& lc = rows.scene.Registry().get<LightComponent>(rows.entity);
    EnumCombo(rows, "Type", "insp.ltype", {"Directional", "Point", "Spot", "Tube"}, lc.type);
    rows.Color("Color", "insp.lcolor", lc.color,
               SetVec3(rows.scene, rows.entity, &LightComponent::color));
    rows.Float("Intensity", "insp.lint", lc.intensity, Opts(0.05, 0.0, std::nullopt));
    if (lc.type == LightType::Directional) {
        rows.SubHeading("Sun Disk");
        rows.Float("Disk Size", "insp.ldisks", lc.sunDiskSizeDeg,
                   Opts(0.01, 0.0, 30.0, "°", 2));
        rows.Float("Disk Intensity", "insp.ldiski", lc.sunDiskIntensity,
                   Opts(0.01, 0.0, std::nullopt));
    } else {
        rows.SubHeading("Attenuation");
        rows.Float("Radius", "insp.lrad", lc.attenuationRadius,
                   Opts(0.05, 0.0, std::nullopt, " m"));
        rows.Float("Falloff Power", "insp.lpow", lc.attenuationPower, Opts(0.01, 0.0, 16.0));
        if (lc.type == LightType::Tube) {
            rows.Float("Length", "insp.llen", lc.length, Opts(0.02, 0.0, std::nullopt, " m"));
        }
        if (lc.type == LightType::Spot) {
            rows.Float("Inner Angle", "insp.lin", lc.innerAngleDeg,
                       Opts(0.2, 0.0, 89.0, "°", 1));
            rows.Float("Outer Angle", "insp.lout", lc.outerAngleDeg,
                       Opts(0.2, 0.0, 89.0, "°", 1));
        }
    }

    rows.SubHeading("Shadows");
    rows.Check("Cast Shadows", "insp.lshadows", lc.castShadows);
    rows.Int("Map Size", "insp.lmapsize", lc.shadowMapSize, Opts(4.0, 256.0, 8192.0, " px"));
    rows.Float("Bias", "insp.lbias", lc.shadowBias, Opts(0.0001, 0.0, 0.1, {}, 4));
    rows.Float("Normal Bias", "insp.lnbias", lc.normalBias, Opts(0.001, 0.0, 1.0, {}, 4));
    rows.Float("Softness", "insp.lsoft", lc.shadowSoftness, Opts(0.01, 0.0, std::nullopt));
    if (lc.type == LightType::Directional) {
        rows.Int("Cascades", "insp.lcascades", lc.cascadeCount, Opts(0.05, 1.0, 4.0));
        rows.Float("Shadow Distance", "insp.ldist", lc.shadowDistance,
                   Opts(0.5, 1.0, std::nullopt, " m", 1));
        rows.Float("Split Lambda", "insp.llambda", lc.cascadeSplitLambda, Unit01());
    }
}

void DrawPostProcess(InspectorRows& rows) {
    auto& pp = rows.scene.Registry().get<PostProcessComponent>(rows.entity);
    rows.Check("Enabled", "insp.ppenabled", pp.enabled);

    rows.SubHeading("Bloom");
    rows.Check("Enabled", "insp.pp.bloom", pp.bloomEnabled);
    rows.Float("Intensity", "insp.pp.bint", pp.bloomIntensity);
    rows.Float("Threshold", "insp.pp.bthr", pp.bloomThreshold);

    rows.SubHeading("Tone Mapping");
    EnumCombo(rows, "Tone Mapper", "insp.pp.tone", {"ACES", "Reinhard", "AgX", "Linear"},
              pp.toneMapper);
    rows.Float("Exposure", "insp.pp.exp", pp.exposure);
    rows.Check("Auto Exposure", "insp.pp.autoexp", pp.autoExposure);
    if (pp.autoExposure) {
        rows.Float("Speed", "insp.pp.aespd", pp.autoExposureSpeed);
        rows.Float("Min Brightness", "insp.pp.aemin", pp.autoExposureMin);
        rows.Float("Max Brightness", "insp.pp.aemax", pp.autoExposureMax);
    }

    rows.SubHeading("Ambient Occlusion");
    rows.Check("SSAO", "insp.pp.ssao", pp.ssaoEnabled);
    if (pp.ssaoEnabled) {
        rows.Float("Radius", "insp.pp.srad", pp.ssaoRadius);
        rows.Float("Intensity", "insp.pp.sint", pp.ssaoIntensity);
    }

    rows.SubHeading("Reflections");
    rows.Check("SSR", "insp.pp.ssr", pp.ssrEnabled);
    if (pp.ssrEnabled) rows.Float("Intensity", "insp.pp.sint2", pp.ssrIntensity);

    rows.SubHeading("Motion Blur");
    rows.Check("Enabled", "insp.pp.mb", pp.motionBlurEnabled);
    if (pp.motionBlurEnabled) rows.Float("Amount", "insp.pp.mbamt", pp.motionBlurAmount);

    rows.SubHeading("Depth of Field");
    EnumCombo(rows, "Method", "insp.pp.dof", {"Off", "Gaussian", "Circle Bokeh"}, pp.dofMethod);
    if (pp.dofMethod != PostProcessComponent::DOFMethod::None) {
        rows.Float("Focal Distance", "insp.pp.doffoc", pp.dofFocalDistance);
        rows.Float("Scale", "insp.pp.dofscale", pp.dofScale);
    }

    rows.SubHeading("Color Grading");
    rows.Check("Enabled", "insp.pp.cg", pp.colorGradingEnabled);
    if (pp.colorGradingEnabled) {
        rows.Float("Saturation", "insp.pp.cgsat", pp.colorGradingSaturation);
        rows.Float("Contrast", "insp.pp.cgcon", pp.colorGradingContrast);
        rows.Float("Gamma", "insp.pp.cggam", pp.colorGradingGamma);
    }

    rows.SubHeading("Vignette");
    rows.Check("Enabled", "insp.pp.vig", pp.vignetteEnabled);
    if (pp.vignetteEnabled) {
        rows.Float("Intensity", "insp.pp.vigint", pp.vignetteIntensity);
        rows.Float("Radius", "insp.pp.vigrad", pp.vignetteRadius);
    }

    rows.SubHeading("Film Grain");
    rows.Check("Enabled", "insp.pp.fg", pp.filmGrainEnabled);
    if (pp.filmGrainEnabled) rows.Float("Intensity", "insp.pp.fgint", pp.filmGrainIntensity);
}

InspectorDrawer EngineDrawer(std::string id, std::function<void(InspectorRows&)> draw) {
    InspectorDrawer d;
    d.componentId = std::move(id);
    d.owner = "Engine";
    d.draw = std::move(draw);
    return d;
}

void EnsureBuiltinInspectorDrawers() {
    static RegistrationSet builtins = RegisterBuiltinInspectorDrawers(InspectorDrawers());
}

}  // namespace

RegistrationSet RegisterBuiltinInspectorDrawers(InspectorDrawerRegistry& registry) {
    RegistrationSet set;
    InspectorDrawer name;
    name.componentId = "Name";
    name.owner = "Engine";
    name.hidden = true;  // the Inspector header shows (and the Outliner edits) the name
    set.Add(registry.Register(std::move(name)));
    set.Add(registry.Register(EngineDrawer("Transform", DrawTransform)));
    set.Add(registry.Register(EngineDrawer("MeshRenderer", DrawMeshRenderer)));
    set.Add(registry.Register(EngineDrawer("Camera", DrawCamera)));
    set.Add(registry.Register(EngineDrawer("Light", DrawLight)));
    set.Add(registry.Register(EngineDrawer("Environment", DrawEnvironment)));
    set.Add(registry.Register(EngineDrawer("PostProcess", DrawPostProcess)));
    return set;
}

InspectorPanel::InspectorPanel() { EnsureBuiltinInspectorDrawers(); }

std::vector<Slate::MenuEntry> InspectorPanel::BuildAddComponentMenu(Scene* scene, Entity e) {
    const UnresolvedComponents* kept =
        scene->IsValid(e) ? scene->Registry().try_get<UnresolvedComponents>(e) : nullptr;

    // Group by category, categories in first-registration order.
    std::vector<std::string> categories;
    std::map<std::string, std::vector<const ComponentDesc*>> byCategory;
    for (const ComponentDesc* d : Components().All()) {
        if (!d->userAddable) continue;
        const std::string cat = d->category.empty() ? std::string("Other") : d->category;
        if (!byCategory.contains(cat)) categories.push_back(cat);
        byCategory[cat].push_back(d);
    }

    Slate::MenuBuilder b;
    for (const std::string& cat : categories) {
        b.BeginSection(cat);
        for (const ComponentDesc* d : byCategory[cat]) {
            // Why the component can't be added (empty = it can).
            std::string blocked;
            if (d->has(*scene, e)) {
                blocked = "This entity already has a " + d->displayName + " (one per entity).";
            } else if (kept && kept->Find(d->id)) {
                blocked = "This entity keeps data for " + d->displayName +
                          " from a missing plugin; remove that first.";
            } else if (d->onePerScene) {
                for (Entity other : scene->Registry().view<entt::entity>()) {
                    if (other != e && d->has(*scene, other)) {
                        blocked = "The scene already has a " + d->displayName +
                                  " (one per scene).";
                        break;
                    }
                }
            }
            const std::string id = d->id;
            auto addEntry = [&](const std::string& label, const std::string& icon,
                                const std::string& tooltip, std::size_t variant) {
                Slate::MenuEntryOptions o;
                o.enabled = blocked.empty();
                o.tooltip = blocked.empty() ? tooltip : blocked;
                b.AddEntry(label, IconByName(icon),
                           [scene, e, id, variant] {
                               const ComponentDesc* desc = Components().Find(id);
                               if (!desc || !scene->IsValid(e) || desc->has(*scene, e)) return;
                               if (variant == SIZE_MAX) {
                                   desc->add(*scene, e);
                               } else if (variant < desc->variants.size()) {
                                   desc->variants[variant].apply(*scene, e);
                               }
                           },
                           o);
            };
            if (d->variants.empty()) {
                addEntry(d->displayName, d->icon, d->tooltip, SIZE_MAX);
            } else {
                for (std::size_t i = 0; i < d->variants.size(); ++i) {
                    const ComponentVariant& v = d->variants[i];
                    addEntry(v.displayName, v.icon.empty() ? d->icon : v.icon,
                             v.tooltip.empty() ? d->tooltip : v.tooltip, i);
                }
            }
        }
        b.EndSection();
    }
    return b.Build();
}

std::vector<Slate::MenuEntry> InspectorPanel::BuildComponentMenu(Scene* scene, Entity e,
                                                                 const ComponentDesc& desc) {
    using Slate::Icon;
    const std::string id = desc.id;
    auto apply = [scene, e, id](bool remove) {
        const ComponentDesc* d = Components().Find(id);
        if (!d || !scene->IsValid(e) || !d->has(*scene, e)) return;
        if (remove) {
            d->remove(*scene, e);
        } else {
            d->reset(*scene, e);
        }
    };

    Slate::MenuBuilder b;
    b.BeginSection("Component");
    Slate::MenuEntryOptions reset;
    reset.tooltip = "Restore every property of this component to its default.";
    b.AddEntry("Reset to Defaults", Icon::Reset,
               [this, apply] {
                   ClosePopups();
                   apply(false);
               },
               reset);
    Slate::MenuEntryOptions removeOpts;
    removeOpts.enabled = desc.userAddable;
    removeOpts.tooltip = removeOpts.enabled
                             ? "Remove this component from the entity."
                             : "Every entity needs a " + desc.displayName + ".";
    b.AddEntry("Remove Component", Icon::Trash,
               [this, apply] {
                   ClosePopups();
                   apply(true);
               },
               removeOpts);
    return b.Build();
}

void InspectorPanel::RequestRemoveUnresolved(Entity e, std::string componentId) {
    m_pendingRemoveEntity = e;
    m_pendingRemoveId = std::move(componentId);
}

void InspectorPanel::ConfirmRemoveUnresolved(Scene& scene) {
    const Entity e = m_pendingRemoveEntity;
    const std::string id = m_pendingRemoveId;
    CancelRemoveUnresolved();
    if (!scene.IsValid(e)) return;
    auto* kept = scene.Registry().try_get<UnresolvedComponents>(e);
    if (!kept || !kept->Remove(id)) return;
    if (kept->payloads.empty()) scene.Registry().remove<UnresolvedComponents>(e);
    LUMA_USER_INFO("Inspector", "Removed the kept data of '{}'", id);
}

void InspectorPanel::CancelRemoveUnresolved() {
    m_pendingRemoveEntity = kNullEntity;
    m_pendingRemoveId.clear();
}

void InspectorPanel::DrawSectionHeader(Slate::Context& ui, PanelContext& /*ctx*/,
                                       const Rect& body, f32& y, f32 x, f32 w,
                                       std::string_view title, u64 menuKey,
                                       std::vector<Slate::MenuEntry> menu, Slate::Color tick) {
    Slate::Theme& t = ui.theme();
    const Rect band{body.x, y, body.w, 26};
    ui.Panel(band, t.surface2);
    ui.Panel({band.x, band.Bottom() - 1.0f, band.w, 1.0f}, t.separator);
    ui.Panel({body.x, y, 2, 26}, tick);
    ui.Heading({x, y, w, 26}, title, t.text);
    if (ui.mousePressed(1) && band.Contains(ui.mouse())) {
        ui.OpenMenu(menuKey, ui.mouse(), std::move(menu));
    }
    y += 32.0f;
}

void InspectorPanel::Draw(Slate::Context& ui, const Slate::Rect& body, PanelContext& ctx) {
    Slate::Theme& t = ui.theme();
    m_slotRects.clear();
    m_meshRowRect = {};
    m_drawnSections.clear();
    m_pickers.BeginFrame();

    if (!ctx.scene->IsValid(*ctx.selected)) {
        ui.LabelIn({body.x, body.y + 8, body.w, 22}, "  No selection", t.textDim);
        return;
    }
    const Entity selected = *ctx.selected;
    // Selection changed — close popups so they don't target the previous
    // entity's components, and jump the content scroll back to the top.
    if (selected != m_lastSelected) {
        ClosePopups();
        m_lastSelected = selected;
        m_scroll = 0.0f;
    }
    Scene& scene = *ctx.scene;
    auto& reg = scene.Registry();
    const f32 x = body.x + 12.0f;
    f32 y = body.y + 12.0f;
    const f32 w = body.w - 24.0f;

    // Header: entity name + "Add" component picker (Unreal Details panel).
    const u64 addId = RowId("insp.addcomp", selected);
    const Rect addBtn{body.Right() - 84.0f, y, 72.0f, 24.0f};
    const NameComponent* name = reg.try_get<NameComponent>(selected);
    ui.Heading({x, y, addBtn.x - x - 8.0f, 24}, name ? name->name : std::string("(unnamed)"),
               t.text);
    if (ui.IconTextButton(addId, addBtn, Slate::Icon::Plus, "Add", t.success,
                          ui.IsMenuOpen(addId))) {
        Slate::MenuOptions opts;
        opts.searchable = true;
        opts.searchHint = "Search components";
        opts.minWidth = 240.0f;
        ui.OpenMenuBelow(addId, addBtn, BuildAddComponentMenu(ctx.scene, selected), opts);
    }
    ui.Tooltip(addId ^ 0x71Full, addBtn,
               "Add Component\nAttach a new component to the selected actor. "
               "Right-click a component header to reset or remove it.");
    // The header row stays pinned; the component content below it scrolls.
    // The content region is clipped so scrolled rows can't overlap the pinned
    // header, and the scrollbar is added once the content height is known.
    const f32 contentTop = body.y + 46.0f;
    Rect contentRegion{body.x, contentTop, body.w, body.h - 46.0f};
    ui.PushClip(contentRegion);
    y = contentTop - m_scroll;

    for (const ComponentDesc* desc : Components().All()) {
        if (!desc->has(scene, selected)) continue;
        const InspectorDrawer* drawer = InspectorDrawers().Find(desc->id);
        if (drawer && drawer->hidden) continue;
        m_drawnSections.push_back(desc->id);
        DrawSectionHeader(ui, ctx, body, y, x, w, desc->displayName,
                          Slate::Context::ID("insp.section." + desc->id) ^ 0x5EC7ull,
                          BuildComponentMenu(ctx.scene, selected, *desc), t.accent);
        InspectorRows rows(ui, ctx, scene, selected, x, y, w, m_pickers);
        rows.panelLeft = body.x;
        rows.panelRight = body.Right();
        rows.materialSlotRects = &m_slotRects;
        rows.staticMeshRowRect = &m_meshRowRect;
        if (drawer) {
            drawer->draw(rows);
        } else {
            DrawReflectedComponent(rows, *desc);
        }
        y = rows.y + 8.0f;
        // A drawer may have removed the component or the entity; stop drawing
        // this entity if it is gone.
        if (!scene.IsValid(selected)) break;
    }

    // Components kept for plugins that are not loaded: read-only, removable
    // only after a confirmation.
    if (scene.IsValid(selected)) {
        if (const auto* kept = reg.try_get<UnresolvedComponents>(selected)) {
            for (const UnresolvedComponent& c : kept->payloads) {
                m_drawnSections.push_back(c.id);
                const std::string id = c.id;
                Slate::MenuBuilder b;
                b.BeginSection("Missing Plugin");
                Slate::MenuEntryOptions o;
                o.tooltip = "Delete the kept data of this component (asks for confirmation).";
                b.AddEntry("Remove Component...", Slate::Icon::Trash,
                           [this, selected, id] { RequestRemoveUnresolved(selected, id); }, o);
                DrawSectionHeader(ui, ctx, body, y, x, w, c.id + " (missing plugin)",
                                  Slate::Context::ID("insp.missing." + c.id) ^ 0x5EC7ull,
                                  b.Build(), t.warning);
                InspectorRows rows(ui, ctx, scene, selected, x, y, w, m_pickers);
                rows.Note(MissingComponentLabel(c), t.warning);
                if (c.payload.IsObject()) {
                    for (const auto& [field, value] : c.payload.Members()) {
                        rows.ReadOnly(PrettyPropertyName(field),
                                      value.IsString() ? value.AsString() : WriteJson(value, false));
                    }
                } else {
                    rows.ReadOnly("Value", WriteJson(c.payload, false));
                }
                y = rows.y + 8.0f;
            }
        }
    }

    ui.PopClip();
    // Scroll region spans the content area below the pinned header; the bar
    // starts right under it and the range matches the visible region (so the
    // thumb reaches the end exactly when the last row is visible).
    const f32 contentH = (y - contentTop) + m_scroll;
    m_scroll = ui.VerticalScroll(RowId("insp.scroll", selected), contentRegion, contentH, m_scroll);
}

void InspectorPanel::ClosePopups() { m_pickers.CloseAll(); }

void InspectorPanel::DrawFloatingPickers(Slate::Context& ui, PanelContext& ctx) {
    // Called after the dock (clip stack unwound) so the floating picker panels
    // can overflow this column. Only pickers whose rows were drawn by the last
    // Draw are shown.
    m_pickers.DrawPanels(ui);

    const u64 id = Slate::Context::ID("insp.removemissing");
    const UnresolvedComponents* kept =
        (!m_pendingRemoveId.empty() && ctx.scene && ctx.scene->IsValid(m_pendingRemoveEntity))
            ? ctx.scene->Registry().try_get<UnresolvedComponents>(m_pendingRemoveEntity)
            : nullptr;
    const UnresolvedComponent* c = kept ? kept->Find(m_pendingRemoveId) : nullptr;
    if (!c) {
        // Nothing (left) to confirm: drop the request and a dialog still open
        // from an earlier frame (the entity or its data went away).
        if (!m_pendingRemoveId.empty()) CancelRemoveUnresolved();
        if (m_removeDialogOpen) {
            ui.EndModal();
            m_removeDialogOpen = false;
        }
        return;
    }
    Slate::Theme& t = ui.theme();
    const Rect content = ui.BeginModal(id, "Remove Kept Component Data?", {520.0f, 170.0f});
    m_removeDialogOpen = true;
    const std::string owner = c->owner.empty() ? std::string("an unknown plugin") : c->owner;
    ui.LabelIn({content.x, content.y, content.w, 22.0f},
               "'" + c->id + "' comes from " + owner + ", which is not loaded.", t.text);
    ui.LabelIn({content.x, content.y + 24.0f, content.w, 22.0f},
               "Its data is kept in the scene. Removing it deletes that data", t.textDim);
    ui.LabelIn({content.x, content.y + 46.0f, content.w, 22.0f},
               "the next time the scene is saved.", t.textDim);
    // The button row closes the modal itself when either button is pressed.
    const Slate::Context::ModalResult r = ui.ModalButtonRow(id, content, "Remove", "Cancel");
    if (r.ok) {
        m_removeDialogOpen = false;
        ConfirmRemoveUnresolved(*ctx.scene);
    } else if (r.cancel || !r.open) {
        m_removeDialogOpen = false;
        CancelRemoveUnresolved();
    }
}

}  // namespace Luma::Editor::Panels
