#include "Luma/Editor/Panels/MaterialEditor.h"

#include <algorithm>
#include <cmath>

#include "Luma/Core/Log.h"
#include "Luma/Slate/Menu.h"

namespace Luma::Editor::Panels {

using Material::MaterialInput;
using Material::MaterialParameter;
using Slate::Align;
using Slate::Color;
using Slate::Context;
using Slate::Rect;

namespace {

constexpr f32 kToolbarH = 34.0f;
constexpr f32 kRowH = 26.0f;
constexpr f32 kLabelW = 128.0f;
constexpr f32 kOverrideW = 22.0f;

std::string AssetName(const std::string& path) {
    const usize slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

Color ToColor(const Math::Vec4& v) {
    auto c8 = [](f32 x) { return static_cast<u8>(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return Color::RGB(c8(v.x), c8(v.y), c8(v.z));
}

Math::Vec4 ToVec4(const Color& c, f32 w) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, w};
}

const char* InputTooltip(MaterialParameter p) {
    switch (p) {
        case MaterialParameter::BaseColor: return "Base Color\nThe surface's albedo (linear color).";
        case MaterialParameter::Metallic: return "Metallic\n0 = dielectric, 1 = metal.";
        case MaterialParameter::Specular:
            return "Specular\nDielectric reflectance: 0.5 = 4 % (most materials).";
        case MaterialParameter::Roughness: return "Roughness\n0 = mirror, 1 = fully diffuse.";
        case MaterialParameter::Normal:
            return "Normal\nTangent-space normal map (OpenGL convention, +Y up).";
        case MaterialParameter::EmissiveColor:
            return "Emissive Color\nLight emitted by the surface (HDR, drives bloom).";
        case MaterialParameter::AmbientOcclusion:
            return "Ambient Occlusion\nDarkens indirect (sky) light in crevices.";
        case MaterialParameter::OpacityMask:
            return "Opacity Mask\nMasked blend mode: pixels below the clip value are cut out.";
        case MaterialParameter::Cavity:
            return "Cavity\nFine occlusion in small cracks; multiplies Ambient Occlusion.";
        case MaterialParameter::Height:
            return "Height\nDisplacement map (white = high). Drives parallax occlusion mapping.";
        case MaterialParameter::Bump:
            return "Bump\nFine height detail that perturbs the surface normal.";
        case MaterialParameter::Count: break;
    }
    return "";
}

// DragFloat with a [min, max] range.
// A clamped spin box; ranges that fit a slider (min..max both finite and
// small) show a fill bar. Returns true when the value changed.
bool DragClamped(Context& ui, u64 id, const Rect& rect, f32& value, f32 speed, f32 min,
                 f32 max, bool slider = false, int maxDigits = 3) {
    Slate::SpinBoxOptions o;
    o.speed = speed;
    o.min = min;
    o.max = max;
    if (slider) {
        o.sliderMin = min;
        o.sliderMax = max;
    }
    o.maxFractionalDigits = maxDigits;
    return ui.SpinBox(id, rect, value, o).changed;
}

}  // namespace

// Per-frame details cursor.
struct MaterialEditorPanel::Layout {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 w = 0.0f;
    // Field rect of a property row (label column on the left), advancing y.
    Rect Row(Context& ui, std::string_view label, std::string_view tooltip, bool dim) {
        const Rect row{x, y, w, kRowH - 4.0f};
        const Slate::Theme& t = ui.theme();
        ui.LabelIn({row.x + kOverrideW, row.y, kLabelW - kOverrideW, row.h}, label,
                   dim ? t.textDisabled : t.textDim);
        if (!tooltip.empty()) {
            ui.Tooltip(Context::ID(std::string(label) + std::string(tooltip)),
                       {row.x, row.y, kLabelW, row.h}, tooltip);
        }
        y += kRowH;
        return {row.x + kLabelW, row.y, std::max(row.w - kLabelW, 40.0f), row.h};
    }
};

MaterialEditorPanel::MaterialEditorPanel(std::string assetPath) : m_path(std::move(assetPath)) {
    m_open.fill(true);
}

void MaterialEditorPanel::Bind(Material::MaterialLibrary& library) {
    if (m_library == &library && m_model) return;
    m_library = &library;
    m_model.emplace(m_path, library);
}

bool MaterialEditorPanel::IsDirty() const { return m_model && m_model->IsDirty(); }

std::string MaterialEditorPanel::Title() const {
    return AssetName(m_path) + (IsDirty() ? "*" : "");
}

bool MaterialEditorPanel::Save(std::string* error) {
    if (!m_model) {
        if (error) *error = "no material library";
        return false;
    }
    return m_model->Save(error);
}

void MaterialEditorPanel::ClosePopups() {
    for (auto& p : m_constantPickers) p.Close();
    for (auto& p : m_tintPickers) p.Close();
}

void MaterialEditorPanel::DrawFloatingPickers(Context& ui) {
    for (auto& p : m_constantPickers) p.DrawPanel(ui);
    for (auto& p : m_tintPickers) p.DrawPanel(ui);
}

void MaterialEditorPanel::Draw(Context& ui, const Rect& rect, PanelContext& ctx) {
    const Slate::Theme& t = ui.theme();
    m_drawnRect = rect;
    ui.Panel(rect, t.panelBg);
    if (ctx.materials) Bind(*ctx.materials);
    if (!m_model || !m_model->Valid()) {
        ui.LabelIn(rect, "'" + m_path + "' could not be loaded.", t.textDim, Align::Center);
        return;
    }
    // Ctrl+S saves the asset under the pointer (the tab being worked in).
    if (ctx.canSave && rect.Contains(ui.rawMouse()) && ui.ctrlDown() && ui.keyPressed(83) &&
        !ui.textFieldFocused()) {
        std::string error;
        if (Save(&error)) {
            LUMA_USER_INFO("Materials", "Saved '{}'", m_path);
        } else {
            LUMA_USER_ERROR("Materials", "Failed to save '{}': {}", m_path, error);
        }
    }
    DrawToolbar(ui, {rect.x, rect.y, rect.w, kToolbarH}, ctx);
    const Rect body{rect.x, rect.y + kToolbarH, rect.w, rect.h - kToolbarH};
    Rect preview, details;
    ui.SplitterV(Context::ID("mated.split." + m_path), body, m_split, preview, details, 2.0f);
    DrawPreview(ui, preview, ctx);
    DrawDetails(ui, details, ctx);
}

void MaterialEditorPanel::DrawToolbar(Context& ui, const Rect& bar, PanelContext& ctx) {
    const Slate::Theme& t = ui.theme();
    ui.Panel(bar, t.surface1);
    ui.Panel({bar.x, bar.Bottom() - 1.0f, bar.w, 1.0f}, t.separator);
    f32 x = bar.x + 6.0f;
    const f32 y = bar.y + 4.0f, h = bar.h - 8.0f;

    const Rect save{x, y, 74.0f, h};
    if (ui.IconTextButton(Context::ID("mated.save." + m_path), save, Slate::Icon::Save, "Save",
                          ctx.canSave ? t.text : t.textDisabled) && ctx.canSave) {
        std::string error;
        if (Save(&error)) {
            LUMA_USER_INFO("Materials", "Saved '{}'", m_path);
        } else {
            LUMA_USER_ERROR("Materials", "Failed to save '{}': {}", m_path, error);
        }
    }
    ui.Tooltip(Context::ID("mated.save.tip"), save, ctx.canSave ? "Save (Ctrl+S)\nWrite this asset to disk." : "Stop play before saving");
    x += save.w + 4.0f;

    const Rect browse{x, y, 92.0f, h};
    if (ui.IconTextButton(Context::ID("mated.browse." + m_path), browse,
                          Slate::Icon::Browse, "Browse", Color::RGB(255, 255, 255)) &&
        ctx.onBrowseToAsset) {
        ctx.onBrowseToAsset(m_path);
    }
    ui.Tooltip(Context::ID("mated.browse.tip"), browse,
               "Browse to Asset\nShow this asset in the Content Browser.");
    x += browse.w + 12.0f;

    const Rect shape{x, y, 110.0f, h};
    m_shape = ui.Combo(Context::ID("mated.shape." + m_path), shape,
                       {"Sphere", "Cube", "Plane", "Cylinder"}, m_shape);
    ui.Tooltip(Context::ID("mated.shape.tip"), shape, "Preview Mesh\nShape the material is shown on.");
    x += shape.w + 12.0f;

    const Rect skyToggle{x, y + (h - 18.0f) * 0.5f, 34.0f, 18.0f};
    ui.Toggle(Context::ID("mated.sky." + m_path), skyToggle, m_sky);
    ui.LabelIn({skyToggle.Right() + 6.0f, y, 80.0f, h}, "Sky", t.textDim);
    ui.Tooltip(Context::ID("mated.sky.tip"), {skyToggle.x, y, 80.0f, h},
               "Preview Background\nOn: sky backdrop and sky light. Off: dark studio.");
}

void MaterialEditorPanel::DrawPreview(Context& ui, const Rect& area, PanelContext& ctx) {
    const Slate::Theme& t = ui.theme();
    ui.Panel(area, t.windowBg);
    if (area.w < 8.0f || area.h < 8.0f) return;
    // Orbit: drag inside the preview.
    if (ui.mousePressed(0) && area.Contains(ui.mouse())) m_orbiting = true;
    if (!ui.isMouseDown(0)) m_orbiting = false;
    if (m_orbiting) {
        m_yaw -= ui.mouseDelta().x * 0.01f;
        m_pitch = std::clamp(m_pitch + ui.mouseDelta().y * 0.01f, -1.45f, 1.45f);
        ui.RequestCursor(CursorShape::Hand);
    }
    TextureHandle image = 0;
    if (ctx.renderMaterialPreview) {
        image = ctx.renderMaterialPreview(m_path, m_shape, m_yaw, m_pitch,
                                          static_cast<u32>(area.w), static_cast<u32>(area.h),
                                          m_sky);
    }
    if (image) {
        ui.Image(image, area);
    } else {
        ui.LabelIn(area, "Preview unavailable", t.textDim, Align::Center);
    }
    ui.Tooltip(Context::ID("mated.preview." + m_path), area, "Preview\nDrag to orbit.");
}

void MaterialEditorPanel::DrawDetails(Context& ui, const Rect& area, PanelContext& ctx) {
    const Slate::Theme& t = ui.theme();
    ui.Panel(area, t.panelBg);
    const Rect content{area.x + 8.0f, area.y + 6.0f, area.w - 22.0f, area.h - 12.0f};
    ui.PushClip(area);
    Layout l;
    l.x = content.x;
    l.y = content.y - m_scroll;
    l.w = content.w;

    if (m_model->IsInstance()) DrawParentRow(ui, l, ctx);
    DrawSettings(ui, l);
    for (usize i = 0; i < Material::kMaterialParameterCount; ++i) {
        DrawInput(ui, l, ctx, static_cast<MaterialParameter>(i));
    }
    ui.PopClip();
    const f32 contentH = (l.y + m_scroll) - content.y + 12.0f;
    m_scroll = ui.VerticalScroll(Context::ID("mated.scroll." + m_path), area, contentH, m_scroll);
}

void MaterialEditorPanel::DrawParentRow(Context& ui, Layout& l, PanelContext& ctx) {
    const Slate::Theme& t = ui.theme();
    const Rect field = l.Row(ui, "Parent",
                             "Parent\nThe Material or Instance this instance inherits from.",
                             false);
    const std::string parent = m_model->Parent();
    const u64 id = Context::ID("mated.parent." + m_path);
    if (ui.MenuButton(id, field, parent.empty() ? "None" : AssetName(parent)) &&
        ctx.listMaterials) {
        Slate::MenuBuilder b;
        b.BeginSection("Materials");
        for (const std::string& candidate : ctx.listMaterials()) {
            if (candidate == m_path) continue;
            b.AddEntry(AssetName(candidate), {}, [this, candidate] {
                std::string error;
                if (m_model->SetParent(candidate, &error)) {
                    m_parentError.clear();
                    LUMA_USER_INFO("Materials", "'{}' now inherits from '{}'", m_path, candidate);
                } else {
                    m_parentError = error;
                    LUMA_USER_ERROR("Materials", "Cannot set parent of '{}': {}", m_path, error);
                }
            }, {.tooltip = candidate});
        }
        Slate::MenuOptions options;
        options.searchable = true;
        options.searchHint = "Search materials";
        ui.OpenMenuBelow(id, field, b.Build(), options);
    }
    if (!m_parentError.empty()) {
        ui.LabelIn({l.x + kLabelW, l.y - 4.0f, l.w - kLabelW, 18.0f}, m_parentError, t.error);
        l.y += 18.0f;
    }
    l.y += 4.0f;
}

void MaterialEditorPanel::DrawSettings(Context& ui, Layout& l) {
    const Slate::Theme& t = ui.theme();
    ui.CollapsingHeader(Context::ID("mated.settings." + m_path), {l.x, l.y, l.w, 24.0f},
                        "Material Settings", m_settingsOpen);
    l.y += 28.0f;
    if (!m_settingsOpen) return;
    const bool instance = m_model->IsInstance();
    const Material::MaterialSettings s = m_model->EffectiveSettings();
    using Setting = MaterialEditModel::Setting;

    // Instance rows start with an override checkbox; inherited rows are dimmed
    // and edits to them are not applied.
    auto overrideBox = [&](Setting setting, const char* key) {
        if (!instance) return true;
        bool on = m_model->IsSettingOverridden(setting);
        const Rect box{l.x, l.y + 3.0f, 16.0f, 16.0f};
        if (ui.Checkbox(Context::ID(std::string("mated.ovs.") + key + m_path), box, {}, on)) {
            m_model->SetSettingOverride(setting, on);
        }
        ui.Tooltip(Context::ID(std::string("mated.ovs.tip.") + key), box,
                   "Override\nUnchecked: inherited from the parent.");
        return on;
    };

    {
        const bool on = overrideBox(Setting::BlendMode, "blend");
        const Rect f = l.Row(ui, "Blend Mode",
                             "Blend Mode\nOpaque, or Masked (cut out by the Opacity Mask).", !on);
        const int mode = ui.Combo(Context::ID("mated.blend." + m_path), f, {"Opaque", "Masked"},
                                  static_cast<int>(s.blendMode));
        if (on && mode != static_cast<int>(s.blendMode)) {
            m_model->SetBlendMode(static_cast<Material::BlendMode>(mode));
        }
    }
    if (!instance) {
        const Rect f = l.Row(ui, "Shading Model",
                             "Shading Model\nDefault Lit, or Unlit (base color + emissive only).",
                             false);
        const int model = ui.Combo(Context::ID("mated.shading." + m_path), f,
                                   {"Default Lit", "Unlit"}, static_cast<int>(s.shadingModel));
        if (model != static_cast<int>(s.shadingModel)) {
            m_model->SetShadingModel(static_cast<Material::ShadingModel>(model));
        }
    }
    {
        const bool on = overrideBox(Setting::TwoSided, "twosided");
        const Rect f = l.Row(ui, "Two Sided",
                             "Two Sided\nRender and cast shadows from both faces.", !on);
        bool twoSided = s.twoSided;
        if (ui.Checkbox(Context::ID("mated.twosided." + m_path), {f.x, f.y + 3.0f, 16.0f, 16.0f},
                        {}, twoSided) && on) {
            m_model->SetTwoSided(twoSided);
        }
    }
    {
        const bool on = overrideBox(Setting::OpacityMaskClip, "clip");
        const Rect f = l.Row(ui, "Mask Clip",
                             "Opacity Mask Clip Value\nMasked pixels below this value are cut out.",
                             !on);
        f32 clip = s.opacityMaskClip;
        if (DragClamped(ui, Context::ID("mated.clip." + m_path), f, clip, 0.005f, 0.0f, 1.0f, true) && on) {
            m_model->SetOpacityMaskClip(clip);
        }
    }
    (void)t;
    l.y += 4.0f;
}

void MaterialEditorPanel::DrawInput(Context& ui, Layout& l, PanelContext& ctx,
                                    MaterialParameter p) {
    const Slate::Theme& t = ui.theme();
    const usize i = static_cast<usize>(p);
    const std::string key = std::string(Material::ParameterKey(p)) + "." + m_path;
    const bool instance = m_model->IsInstance();
    const bool overridden = m_model->IsOverridden(p);

    // Header: [override] Name.
    const Rect header{l.x, l.y, l.w, 24.0f};
    if (instance) {
        bool on = overridden;
        const Rect box{header.x + 4.0f, header.y + 4.0f, 16.0f, 16.0f};
        ui.Panel(header, t.surface2);
        if (ui.Checkbox(Context::ID("mated.ov." + key), box, {}, on)) {
            m_model->SetOverride(p, on);
        }
        ui.Tooltip(Context::ID("mated.ov.tip." + key), box,
                   "Override\nUnchecked: the value is inherited from the parent.");
        bool open = m_open[i];
        ui.CollapsingHeader(Context::ID("mated.hdr." + key),
                            {header.x + kOverrideW + 4.0f, header.y, header.w - kOverrideW - 4.0f,
                             header.h},
                            Material::ParameterDisplayName(p), open);
        m_open[i] = open;
    } else {
        bool open = m_open[i];
        ui.CollapsingHeader(Context::ID("mated.hdr." + key), header,
                            Material::ParameterDisplayName(p), open);
        m_open[i] = open;
    }
    ui.Tooltip(Context::ID("mated.hdr.tip." + key), header, InputTooltip(p));
    l.y += 28.0f;
    if (!m_open[i]) return;

    // Inherited rows show the parent's values dimmed; edits are not applied.
    const bool dim = instance && !overridden;
    MaterialInput input = m_model->EffectiveInput(p);
    bool changed = false;
    const bool isColor = Material::IsColorParameter(p);
    const f32 rowsTop = l.y;

    // Constant.
    {
        const Rect f = l.Row(ui, "Value",
                             isColor ? "Value\nThe color used when no texture is set."
                                     : "Value\nThe value used when no texture is set.",
                             dim);
        if (isColor) {
            Slate::ColorPickerPopup& picker = m_constantPickers[i];
            picker.OnColorChanged = [this, p, dim](const Color& c) {
                if (dim) return;
                MaterialInput edited = m_model->EffectiveInput(p);
                edited.constant = ToVec4(c, edited.constant.w);
                m_model->SetInput(p, edited);
            };
            picker.SetColor(ToColor(input.constant));
            picker.DrawSwatch(ui, {f.x, f.y + 2.0f, f.w, f.h - 4.0f});
        } else if (p == MaterialParameter::Normal) {
            f32 xyz[3] = {input.constant.x, input.constant.y, input.constant.z};
            if (ui.Vector3Field(Context::ID("mated.const." + key), f, xyz)) {
                input.constant = {xyz[0], xyz[1], xyz[2], 0.0f};
                changed = true;
            }
        } else {
            f32 v = input.constant.x;
            if (DragClamped(ui, Context::ID("mated.const." + key), f, v, 0.005f, 0.0f, 1.0f, true)) {
                input.constant = {v, v, v, v};
                changed = true;
            }
        }
    }

    // Texture slot: [thumbnail][asset picker][clear][browse].
    {
        const Rect f = l.Row(ui, "Texture",
                             isColor ? "Texture\nsRGB color map (sampled as sRGB when the file is color)."
                                     : "Texture\nData map, sampled linearly.",
                             dim);
        const f32 thumb = f.h;
        if (!input.texture.empty() && ctx.assetThumbnail) {
            if (TextureHandle tex = ctx.assetThumbnail(input.texture)) {
                ui.Image(tex, {f.x, f.y, thumb, thumb});
            }
        }
        ui.PanelRoundedBordered({f.x, f.y, thumb, thumb}, Color::RGBA(0, 0, 0, 0), t.outline,
                                t.radius.sm);
        const f32 buttons = 2.0f * (f.h + 2.0f);
        const Rect pick{f.x + thumb + 4.0f, f.y, f.w - thumb - 4.0f - buttons, f.h};
        const u64 pickId = Context::ID("mated.tex." + key);
        const bool missing = m_model->IsTextureMissing(p);
        const std::string pickLabel = input.texture.empty() ? std::string("None")
                                      : missing ? "Missing: " + AssetName(input.texture)
                                                : AssetName(input.texture);
        if (ui.MenuButton(pickId, pick, pickLabel) &&
            ctx.listTextures && !dim) {
            Slate::MenuBuilder b;
            b.BeginSection("Textures");
            for (const std::string& tex : ctx.listTextures()) {
                b.AddEntry(AssetName(tex), {}, [this, p, tex] {
                    MaterialInput edited = m_model->EffectiveInput(p);
                    edited.texture = tex;
                    m_model->SetInput(p, edited);
                }, {.tooltip = tex});
            }
            Slate::MenuOptions options;
            options.searchable = true;
            options.searchHint = "Search textures";
            ui.OpenMenuBelow(pickId, pick, b.Build(), options);
        }
        if (!input.texture.empty()) {
            ui.Tooltip(pickId, pick,
                       missing ? input.texture + "\nThe file is missing: the value is used until "
                                                 "it is restored."
                               : input.texture);
        }
        const Rect clear{pick.Right() + 2.0f, f.y, f.h, f.h};
        if (ui.IconTextButton(Context::ID("mated.clear." + key), clear, Slate::Icon::Reset, {},
                              input.texture.empty() ? t.textDisabled : Color::RGB(255, 255, 255)) &&
            !input.texture.empty()) {
            input.texture.clear();
            changed = true;
        }
        ui.Tooltip(Context::ID("mated.clear.tip." + key), clear,
                   "Reset to Value\nRemove the texture and use the input value.");
        const Rect browse{clear.Right() + 2.0f, f.y, f.h, f.h};
        if (ui.IconTextButton(Context::ID("mated.browsetex." + key), browse,
                              Slate::Icon::Browse, {}, input.texture.empty() ? t.textDisabled : Color::RGB(255, 255, 255)) &&
            !input.texture.empty() && ctx.onBrowseToAsset) {
            ctx.onBrowseToAsset(input.texture);
        }
        ui.Tooltip(Context::ID("mated.browsetex.tip." + key), browse,
                   "Browse to Texture\nShow the texture in the Content Browser.");
    }

    if (!input.texture.empty()) {
        const Rect fc = l.Row(ui, "Channel",
                              "Channel\nWhich texture channel(s) feed this input (packed maps).",
                              dim);
        const int channel = ui.Combo(Context::ID("mated.ch." + key), fc,
                                     {"RGB", "R", "G", "B", "A"}, static_cast<int>(input.channel));
        if (channel != static_cast<int>(input.channel)) {
            input.channel = static_cast<Material::TextureChannel>(channel);
            changed = true;
        }
        if (isColor) {
            const Rect ft = l.Row(ui, "Tint", "Tint\nMultiplies the texture color.", dim);
            Slate::ColorPickerPopup& picker = m_tintPickers[i];
            picker.OnColorChanged = [this, p, dim](const Color& c) {
                if (dim) return;
                MaterialInput edited = m_model->EffectiveInput(p);
                edited.tint = ToVec4(c, edited.tint.w);
                m_model->SetInput(p, edited);
            };
            picker.SetColor(ToColor(input.tint));
            picker.DrawSwatch(ui, {ft.x, ft.y + 2.0f, ft.w, ft.h - 4.0f});
        } else if (p != MaterialParameter::Normal && p != MaterialParameter::Height &&
                   p != MaterialParameter::Bump) {
            const Rect fs = l.Row(ui, "Scale", "Scale\nMultiplies the texture value.", dim);
            f32 scale = input.tint.x;
            if (DragClamped(ui, Context::ID("mated.scale." + key), fs, scale, 0.005f, 0.0f, 4.0f)) {
                input.tint = {scale, scale, scale, scale};
                changed = true;
            }
        }
        if (!isColor && p != MaterialParameter::Normal) {
            const Rect finv = l.Row(ui, "Invert",
                                    "Invert\nUses 1 - value (e.g. a Gloss map feeding Roughness).", dim);
            if (ui.Checkbox(Context::ID("mated.inv." + key), {finv.x, finv.y + 3.0f, 16.0f, 16.0f}, {},
                            input.invert)) {
                changed = true;
            }
        }
        const Rect ftile = l.Row(ui, "Tiling", "Tiling\nUV repeat count (U, V).", dim);
        const Rect half{ftile.x, ftile.y, ftile.w * 0.5f - 2.0f, ftile.h};
        if (DragClamped(ui, Context::ID("mated.tu." + key), half, input.uvTiling.x, 0.01f, 0.01f, 256.0f) ||
            DragClamped(ui, Context::ID("mated.tv." + key), {half.Right() + 4.0f, half.y, half.w, half.h},
                         input.uvTiling.y, 0.01f, 0.01f, 256.0f)) {
            changed = true;
        }
        const Rect foff = l.Row(ui, "Offset", "Offset\nUV shift (U, V).", dim);
        const Rect halfO{foff.x, foff.y, foff.w * 0.5f - 2.0f, foff.h};
        if (DragClamped(ui, Context::ID("mated.ou." + key), halfO, input.uvOffset.x, 0.005f, -16.0f, 16.0f) ||
            DragClamped(ui, Context::ID("mated.ov2." + key), {halfO.Right() + 4.0f, halfO.y, halfO.w, halfO.h},
                         input.uvOffset.y, 0.005f, -16.0f, 16.0f)) {
            changed = true;
        }
    }
    if (p == MaterialParameter::Normal) {
        const Rect fstr = l.Row(ui, "Strength", "Normal Strength\nScales the normal map's bumps.", dim);
        if (DragClamped(ui, Context::ID("mated.nstr." + key), fstr, input.normalStrength, 0.01f, 0.0f, 8.0f)) {
            changed = true;
        }
        const Rect fflip = l.Row(ui, "Flip Green",
                                 "Flip Green\nFor DirectX-style (-Y) normal maps.", dim);
        if (ui.Checkbox(Context::ID("mated.flip." + key), {fflip.x, fflip.y + 3.0f, 16.0f, 16.0f}, {},
                        input.flipGreen)) {
            changed = true;
        }
    }
    if (p == MaterialParameter::Height) {
        const Rect fh = l.Row(ui, "Scale",
                              "Height Scale\nParallax depth in UV units (0 disables parallax).", dim);
        if (DragClamped(ui, Context::ID("mated.hscale." + key), fh, input.normalStrength, 0.001f, 0.0f, 0.3f, true, 4)) {
            changed = true;
        }
    }
    if (p == MaterialParameter::Bump) {
        const Rect fb = l.Row(ui, "Strength", "Bump Strength\nHeight change per texel (like a normal-from-height filter).", dim);
        if (DragClamped(ui, Context::ID("mated.bstr." + key), fb, input.normalStrength, 0.01f, 0.0f, 16.0f)) {
            changed = true;
        }
    }
    if (p == MaterialParameter::EmissiveColor) {
        const Rect fi = l.Row(ui, "Intensity", "Emissive Intensity\nHDR multiplier (above 1 blooms).", dim);
        if (DragClamped(ui, Context::ID("mated.eint." + key), fi, input.emissiveIntensity, 0.05f, 0.0f, 1000.0f)) {
            changed = true;
        }
    }

    if (dim) {
        // Inherited: dim the rows (their edits are discarded).
        ui.Panel({l.x, rowsTop, l.w, l.y - rowsTop}, Color::RGBA(0, 0, 0, 70));
    } else if (changed) {
        m_model->SetInput(p, input);
    }
    l.y += 6.0f;
}

}  // namespace Luma::Editor::Panels
