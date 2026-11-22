#include "Luma/MaterialEditors/MaterialInstanceEditor.h"

#include <algorithm>
#include <cmath>

#include "EditorWidgets.h"
#include "Luma/Core/Log.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Slate/Icons.h"
#include "Luma/Slate/Menu.h"

namespace Luma::MaterialEditors {

using Material::ParameterOverride;
using Slate::Align;
using Slate::Context;
using Slate::Rect;
using namespace Detail;

std::string AssetName(std::string_view path) {
    const usize slash = path.find_last_of('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

MaterialInstanceEditor::MaterialInstanceEditor(std::string instancePath, Material::MaterialLibrary& library,
                                               MaterialEditorServices services)
    : m_model(std::move(instancePath), library), m_services(std::move(services)) {
    m_preview.materialPath = m_model.Path();
}

std::string MaterialInstanceEditor::Title() const {
    return AssetName(m_model.Path()) + (m_model.IsDirty() ? "*" : "");
}

void MaterialInstanceEditor::SaveWithMessage() {
    std::string error;
    if (m_model.Save(&error)) {
        LUMA_USER_INFO("Materials", "Saved '{}'", m_model.Path());
    } else {
        LUMA_USER_ERROR("Materials", "Failed to save '{}': {}", m_model.Path(), error);
    }
}

void MaterialInstanceEditor::Draw(Context& ui, const Rect& rect) {
    const Slate::Theme& t = ui.theme();
    ui.Panel(rect, t.panelBg);
    if (!m_model.Valid()) {
        ui.LabelIn(rect, "'" + m_model.Path() + "' could not be loaded.", t.textDim, Align::Center);
        return;
    }
    if (rect.Contains(ui.rawMouse()) && ui.ctrlDown() && ui.keyPressed(83) && !ui.textFieldFocused()) {
        SaveWithMessage();
    }
    DrawToolbar(ui, {rect.x, rect.y, rect.w, kToolbarH});
    const Rect body{rect.x, rect.y + kToolbarH, rect.w, rect.h - kToolbarH};
    if (!m_dockBuilt) {
        m_dockBuilt = true;
        const std::string vp = m_model.Path() + "#Viewport", dt = m_model.Path() + "#Details";
        m_dock.AddPanel(vp, "Viewport", [this](Context& c, const Rect& r) { DrawPreview(c, r); },
                        IconTexture(m_services, EditorIcon::Sphere));
        m_dock.AddPanel(dt, "Details", [this](Context& c, const Rect& r) { DrawDetails(c, r); },
                        IconTexture(m_services, EditorIcon::Details));
        m_dock.DockRoot(vp);
        m_dock.DockWith(dt, vp, Slate::DockDir::Right, 1.0f - m_split);  // UE: 70% viewport
    }
    m_dock.Draw(ui, body);
    m_colorPopup.Draw(ui);
    HDivider(ui, rect.x, body.y, rect.w);
}

void MaterialInstanceEditor::DrawToolbar(Context& ui, const Rect& bar) {
    const Slate::Theme& t = ui.theme();
    ToolbarBackground(ui, bar);
    f32 x = bar.x + 6.0f;
    const f32 y = bar.y + 4.0f, h = bar.h - 8.0f;
    const Rect save{x, y, 76.0f, h};
    if (ToolButton(ui, Context::ID("mie.save." + m_model.Path()), save, IconTexture(m_services, EditorIcon::Save),
                   Slate::Icon::Save, "Save", t.text)) {
        SaveWithMessage();
    }
    ui.Tooltip(Context::ID("mie.save.tip"), save, "Save (Ctrl+S)\nWrite this instance to disk.");
    x += save.w + 4.0f;
    const Rect browse{x, y, 92.0f, h};
    if (ToolButton(ui, Context::ID("mie.browse." + m_model.Path()), browse, IconTexture(m_services, EditorIcon::Browse),
                   Slate::Icon::FolderOpen, "Browse", t.text) &&
        m_services.browseToAsset) {
        m_services.browseToAsset(m_model.Path());
    }
    ui.Tooltip(Context::ID("mie.browse.tip"), browse, "Browse to Asset\nShow this asset in the Content Browser.");
    x += browse.w + 12.0f;
    bool isError = false;
    const std::string status = CompileStatus(m_services, m_model.Path(), isError);
    if (!status.empty()) {
        ui.LabelIn({x, y, bar.Right() - x - 8.0f, h}, status, isError ? t.error : t.textDim);
    }
}

void MaterialInstanceEditor::DrawPreview(Context& ui, const Rect& panel) {
    const Slate::Theme& t = ui.theme();
    // A left margin eases the image off the panel edge (no frame).
    ui.Panel(panel, t.panelBg);
    const Rect area{panel.x + 3.0f, panel.y, panel.w - 3.0f, panel.h};
    ui.Panel(area, t.recessed);
    // Orbit: left-drag inside the viewport.
    if (area.Contains(ui.mouse()) && ui.mousePressed(0)) m_orbiting = true;
    if (!ui.isMouseDown(0)) m_orbiting = false;
    if (m_orbiting) {
        const Slate::Vec2 d = ui.mouseDelta();
        m_preview.yaw += d.x * 0.01f;
        m_preview.pitch = std::clamp(m_preview.pitch + d.y * 0.01f, -1.4f, 1.4f);
    }
    m_preview.width = static_cast<u32>(std::max(area.w, 1.0f));
    m_preview.height = static_cast<u32>(std::max(area.h, 1.0f));
    const TextureHandle image = m_services.renderPreview ? m_services.renderPreview(m_preview) : 0;
    if (image != 0) {
        ui.Image(image, area);
    } else {
        ui.LabelIn(area, "Preview unavailable", t.textDisabled, Align::Center);
    }
    ui.LabelIn({area.x + 8.0f, area.y + 6.0f, area.w - 16.0f, 18.0f}, "Perspective  |  Lit", t.textDim);
}

void MaterialInstanceEditor::ApplyParent(const std::string& parent) {
    if (parent.empty() || parent == m_model.Parent()) return;
    m_parentError.clear();
    if (!m_model.SetParent(parent, &m_parentError)) {
        LUMA_USER_ERROR("Materials", "Can't set parent of '{}': {}", m_model.Path(), m_parentError);
    }
}

void MaterialInstanceEditor::DrawParentSlot(Context& ui, f32 x, f32& y, f32 w) {
    const Slate::Theme& t = ui.theme();
    constexpr f32 kSlotH = 50.0f;
    // Row chrome like the other properties (label column, black lines).
    ui.LabelIn({x + 12.0f, y, kLabelW - 18.0f, kSlotH}, "Parent", t.text);
    ui.Panel({x, y + kSlotH - 1.0f, w, 1.0f}, kDividerColor);
    ui.Panel({x + kLabelW - 4.0f, y, 1.0f, kSlotH - 1.0f}, kDividerColor);
    const Rect slot{x + kLabelW, y + 4.0f, w - kLabelW - 4.0f, kSlotH - 8.0f};
    y += kSlotH;

    const std::string current = m_model.Parent();
    const Rect arrow{slot.Right() - 28.0f, slot.y + (slot.h - 24.0f) * 0.5f, 24.0f, 24.0f};
    const Rect browse{arrow.x - 28.0f, arrow.y, 24.0f, 24.0f};
    const bool hover = slot.Contains(ui.mouse()) && !ui.inputBlocked();
    ui.PanelRoundedBordered(slot, t.fieldBg, hover ? t.accent : Slate::Color::RGB(70, 70, 72), 5.0f);
    // Thumbnail tile with the material icon.
    const Rect thumb{slot.x + 5.0f, slot.y + 4.0f, slot.h - 8.0f, slot.h - 8.0f};
    ui.PanelRounded(thumb, t.surface3, 4.0f);
    if (const TextureHandle icon = IconTexture(m_services, EditorIcon::Sphere)) {
        ui.Image(icon, thumb.Inset(4.0f, 4.0f));
    }
    const f32 tx = thumb.Right() + 8.0f, tw = std::max(browse.x - tx - 6.0f, 20.0f);
    ui.LabelIn({tx, slot.y + 3.0f, tw, 20.0f}, current.empty() ? std::string("None") : AssetName(current), t.text);
    ui.LabelIn({tx, slot.y + 21.0f, tw, 16.0f}, current, t.textDim);

    // Browse to the parent in the Content Browser.
    if (ui.Button(Context::ID("mie.parent.browse." + m_model.Path()), browse, {}) && m_services.browseToAsset &&
        !current.empty()) {
        m_services.browseToAsset(current);
    }
    Slate::DrawIcon(ui, browse.Inset(4.0f, 4.0f), Slate::Icon::Search, t.textDim);
    ui.Tooltip(Context::ID("mie.parent.browse.tip"), browse, "Browse to the parent in the Content Browser");

    // Arrow: pick a parent from a searchable list.
    if (ui.Button(Context::ID("mie.parent.pick." + m_model.Path()), arrow, {})) {
        std::vector<std::string> items;
        if (m_services.listMaterials) items = m_services.listMaterials();
        Slate::MenuBuilder mb;
        mb.BeginSection("Parent Material");
        for (const std::string& item : items) {
            if (item == m_model.Path()) continue;
            Slate::MenuEntryOptions o;
            o.tooltip = item;
            o.checked = item == current;
            mb.AddEntry(AssetName(item), Slate::Icon::None, [this, item] { ApplyParent(item); }, o);
        }
        mb.EndSection();
        Slate::MenuOptions mo;
        mo.searchable = true;
        mo.searchHint = "Search materials";
        ui.OpenMenuBelow(Context::ID("mie.parent.menu." + m_model.Path()), slot, mb.Build(), mo);
    }
    Slate::DrawIcon(ui, arrow.Inset(5.0f, 5.0f), Slate::Icon::ChevronDown, t.text);
    ui.Tooltip(Context::ID("mie.parent.pick.tip"), arrow, "Choose the parent material");

    // Double-click the slot: open the parent in its editor.
    if (hover && !arrow.Contains(ui.mouse()) && !browse.Contains(ui.mouse()) && ui.mouseDoubleClicked(0) &&
        m_services.openAsset && !current.empty()) {
        m_services.openAsset(current);
    }
    ui.Tooltip(Context::ID("mie.parent.slot.tip"), {slot.x, slot.y, browse.x - slot.x, slot.h},
               "Double-click to open the parent material");
}

f32 MaterialInstanceEditor::DrawParameter(Context& ui, const InstanceParameter& p, f32 x, f32 y, f32 w) {
    const Slate::Theme& t = ui.theme();
    const std::string key = m_model.Path() + ".p." + p.info.name;
    // Override checkbox, then the label (dimmed while inherited).
    bool overridden = p.overridden;
    const Rect check{x + 14.0f, y + 5.0f, 16.0f, 16.0f};
    if (ui.Checkbox(Context::ID(key + ".ovr"), check, {}, overridden)) {
        m_model.SetOverride(p.info.name, overridden);
    }
    f32 rowY = y;
    Rect field = Row(ui, p.info.name, x, rowY, w - 24.0f, !p.overridden, 36.0f);
    ui.Panel({x + w - 24.0f, y + kRowH - 1.0f, 24.0f, 1.0f}, kDividerColor);  // row line under the reset column
    if (!p.info.description.empty()) {
        ui.Tooltip(Context::ID(key + ".tip"), {x + 36.0f, y, kLabelW - 36.0f, kRowH}, p.info.description);
    }
    ParameterOverride v = p.value;
    bool changed = false;
    switch (p.info.kind) {
        case MaterialCompiler::ParameterInfo::Kind::Scalar: {
            Slate::SpinBoxOptions o;
            o.speed = 0.01f;
            if (p.info.sliderMax > p.info.sliderMin) {
                o.sliderMin = p.info.sliderMin;
                o.sliderMax = p.info.sliderMax;
            }
            changed = ui.SpinBox(Context::ID(key + ".v"), field, v.scalar, o).changed;
            break;
        }
        case MaterialCompiler::ParameterInfo::Kind::Vector: {
            const Rect swatch{field.x, field.y, 24.0f, field.h};
            const std::string name = p.info.name;
            // Rounded swatch: click for the colour picker (overrides the parameter).
            m_colorPopup.Swatch(ui, Context::ID(key + ".swatch"), swatch, v.vector, [this, name](const Math::Vec4& nv) {
                ParameterOverride o;
                o.name = name;
                o.kind = ParameterOverride::Kind::Vector;
                o.vector = nv;
                m_model.SetValue(o);
            });
            f32 xyzw[4] = {v.vector.x, v.vector.y, v.vector.z, v.vector.w};
            Slate::SpinBoxOptions o;
            o.speed = 0.005f;
            changed = ui.VectorField(Context::ID(key + ".v"), {field.x + 26.0f, field.y, field.w - 26.0f, field.h},
                                     xyzw, 4, o)
                          .changed;
            v.vector = {xyzw[0], xyzw[1], xyzw[2], xyzw[3]};
            break;
        }
        case MaterialCompiler::ParameterInfo::Kind::Texture: {
            std::vector<std::string> items{""};
            if (m_services.listTextures) {
                for (std::string& s : m_services.listTextures()) items.push_back(std::move(s));
            }
            std::vector<std::string> labels;
            int current = 0;
            for (usize i = 0; i < items.size(); ++i) {
                labels.push_back(items[i].empty() ? std::string("None") : AssetName(items[i]));
                if (items[i] == v.texture) current = static_cast<int>(i);
            }
            if (current == 0 && !v.texture.empty()) {
                items.push_back(v.texture);
                labels.push_back(AssetName(v.texture));
                current = static_cast<int>(items.size() - 1);
            }
            const int picked = GraphEditor::DarkCombo(ui, Context::ID(key + ".v"), field, labels, current);
            if (picked != current && picked >= 0) {
                v.texture = items[static_cast<usize>(picked)];
                changed = true;
            }
            break;
        }
        case MaterialCompiler::ParameterInfo::Kind::StaticSwitch: {
            changed = ui.Checkbox(Context::ID(key + ".v"), {field.x, field.y + 3.0f, 16.0f, 16.0f}, {},
                                  v.staticSwitch);
            break;
        }
    }
    if (changed) {
        v.name = p.info.name;
        v.kind = static_cast<ParameterOverride::Kind>(p.info.kind);
        m_model.SetValue(v);
    }
    // Reset to parent (UE's yellow arrow), only while overridden.
    if (p.overridden) {
        const Rect reset{x + w - 22.0f, y + 3.0f, 20.0f, 20.0f};
        if (ui.Button(Context::ID(key + ".reset"), reset, {})) m_model.Reset(p.info.name);
        if (const TextureHandle arrow = IconTexture(m_services, EditorIcon::Reset)) {
            ui.Image(arrow, reset.Inset(4.0f, 4.0f));  // the "differs from parent" arrow (themed)
        } else {
            Slate::DrawIcon(ui, reset.Inset(2.0f, 2.0f), Slate::Icon::Reset, t.warning);
        }
        ui.Tooltip(Context::ID(key + ".reset.tip"), reset, "Reset to the parent's value");
    }
    return kRowH;
}

void MaterialInstanceEditor::DrawDetails(Context& ui, const Rect& area) {
    const Slate::Theme& t = ui.theme();
    ui.Panel(area, t.panelBg);
    const Rect header{area.x, area.y, area.w, 0.0f};  // title: the dock tab
    const Rect view{area.x, area.y + header.h, area.w, area.h - header.h};
    ui.PushClip(view);
    const f32 x = view.x + 12.0f, w = view.w - 24.0f;  // left margin so rows never hug the edge
    f32 y = view.y + 4.0f - m_scroll;

    Section(ui, "mie.general." + m_model.Path(), "General", m_generalOpen, x, y, w);
    if (m_generalOpen) {
        DrawParentSlot(ui, x, y, w);
        if (!m_parentError.empty()) {
            ui.LabelIn({x + kLabelW, y, w - kLabelW, 18.0f}, m_parentError, t.error);
            y += 20.0f;
        }
    }

    Section(ui, "mie.previewing." + m_model.Path(), "Previewing", m_previewingOpen, x, y, w);
    if (m_previewingOpen) {
        const Rect mesh = Row(ui, "Preview Mesh", x, y, w, false, 12.0f);
        std::vector<std::string> items{"Sphere", "Cube", "Plane", "Cylinder"};
        std::vector<std::string> meshes;
        if (m_services.listMeshes) meshes = m_services.listMeshes();
        items.insert(items.end(), meshes.begin(), meshes.end());
        const std::string picked = PickString(ui, Context::ID("mie.mesh." + m_model.Path()), mesh, items,
                                              m_previewMeshLabel);
        if (!picked.empty()) {
            m_previewMeshLabel = picked;
            const auto it = std::find(items.begin(), items.begin() + 4, picked);
            if (it != items.begin() + 4) {
                m_preview.shape = static_cast<PreviewShape>(it - items.begin());
                m_preview.meshPath.clear();
            } else {
                m_preview.shape = PreviewShape::Mesh;
                m_preview.meshPath = picked;
            }
        }
        const Rect floor = Row(ui, "Show Floor", x, y, w, false, 12.0f);
        ui.Checkbox(Context::ID("mie.floor." + m_model.Path()), {floor.x, floor.y + 3.0f, 16.0f, 16.0f}, {},
                    m_preview.floor);
        const Rect sky = Row(ui, "Show Sky", x, y, w, false, 12.0f);
        ui.Checkbox(Context::ID("mie.sky." + m_model.Path()), {sky.x, sky.y + 3.0f, 16.0f, 16.0f}, {},
                    m_preview.sky);
    }

    Section(ui, "mie.groups." + m_model.Path(), "Parameter Groups", m_groupsOpen, x, y, w);
    if (m_groupsOpen) {
        const std::vector<ParameterGroup> groups = m_model.Groups();
        if (groups.empty()) {
            ui.LabelIn({x + 12.0f, y, w, kRowH}, "The parent material has no parameters.", t.textDisabled);
            y += kRowH;
        }
        for (const ParameterGroup& g : groups) {
            auto [it, _] = m_groupOpen.try_emplace(g.name, true);
            Section(ui, "mie.group." + m_model.Path() + "." + g.name, g.name, it->second, x + 8.0f, y, w - 8.0f,
                    /*nested=*/true);
            if (!it->second) continue;
            for (const InstanceParameter& p : g.parameters) y += DrawParameter(ui, p, x + 8.0f, y, w - 8.0f);
        }
    }
    ui.PopClip();
    const f32 content = y + m_scroll - view.y + 8.0f;
    m_scroll = ui.VerticalScroll(Context::ID("mie.scroll." + m_model.Path()), view, content, m_scroll);
}

}  // namespace Luma::MaterialEditors
