#include "Luma/MaterialEditors/MaterialGraphEditor.h"

#include <algorithm>
#include <cstdio>

#include "EditorWidgets.h"
#include "Luma/Core/Log.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/MaterialCompiler/Translator.h"
#include "Luma/Slate/Icons.h"

namespace Luma::MaterialEditors {

using MaterialGraph::ExpressionRegistry;
using MaterialGraph::PropertyDesc;
using MaterialGraph::PropertyValue;
using Slate::Align;
using Slate::Context;
using Slate::Rect;
using namespace Detail;

namespace {

usize CountLines(const std::string& s) { return static_cast<usize>(std::count(s.begin(), s.end(), '\n')); }

template <typename E>
bool EnumCombo(Context& ui, u64 id, const Rect& r, const std::vector<std::string>& names, E& value) {
    const int cur = static_cast<int>(value);
    const int picked = GraphEditor::DarkCombo(ui, id, r, names, cur);
    if (picked == cur || picked < 0) return false;
    value = static_cast<E>(picked);
    return true;
}

}  // namespace

MaterialGraphEditor::MaterialGraphEditor(std::string materialPath, Material::MaterialLibrary& library,
                                         MaterialEditorServices services)
    : m_path(std::move(materialPath)), m_library(library), m_services(std::move(services)), m_palette("matpal") {
    const Material::GraphMaterial* m = m_library.GetGraphMaterial(m_path);
    if (!m) return;
    m_model = std::make_unique<MaterialGraphModel>(*m);
    m_controller = std::make_unique<GraphEditor::GraphController>(*m_model);
    m_controller->onChanged = [this] { OnGraphChanged(); };
    m_widget = std::make_unique<GraphEditor::GraphEditorWidget>(*m_controller, "matgraph." + m_path);
    m_palette.onPick = [this](const std::string& id) {
        // Click-to-add drops the node near the middle of the current view.
        const GraphEditor::ViewTransform& v = m_widget->View();
        m_controller->SpawnAction(id, {v.offset.x + 200.0f / v.Zoom(), v.offset.y + 150.0f / v.Zoom()});
    };
    m_preview.materialPath = m_path;
    m_palette.searchIcon = IconTexture(m_services, EditorIcon::Search);
    m_widget->PopupPalette().searchIcon = m_palette.searchIcon;
    m_widget->SetWatermark("LUMA MATERIAL", m_services.watermarkFont);
    Recompile();
}

std::string MaterialGraphEditor::Title() const {
    return AssetName(m_path) + (m_unapplied || IsDirty() ? "*" : "");
}

bool MaterialGraphEditor::IsDirty() const { return m_library.IsGraphAssetDirty(m_path); }

void MaterialGraphEditor::Apply() {
    if (!m_model) return;
    m_library.SetGraphMaterial(m_path, m_model->Material());
    m_unapplied = false;
}

bool MaterialGraphEditor::Save(std::string* error) {
    if (!m_model) {
        if (error) *error = "the material could not be loaded";
        return false;
    }
    Apply();
    return m_library.SaveGraphAsset(m_path, error);
}

void MaterialGraphEditor::OnGraphChanged() {
    m_unapplied = true;
    Recompile();
    if (liveUpdate) Apply();
}

void MaterialGraphEditor::Recompile() {
    m_stats = {};
    MaterialCompiler::Translator translator(m_model->Material(), &m_library, {});
    const MaterialCompiler::CompiledMaterial compiled = translator.Finish();
    std::map<MaterialGraph::NodeId, std::string> nodeErrors;
    auto describe = [&](const MaterialCompiler::CompileError& e) {
        std::string where = "Material";
        if (const MaterialGraph::Node* n = m_model->Material().graph.Find(e.node)) {
            const auto* d = ExpressionRegistry::Get().Find(n->type);
            where = d ? d->displayName : n->type;
        }
        return where + ": " + e.message;
    };
    for (const auto& e : compiled.errors) {
        m_stats.errors.push_back(describe(e));
        if (e.node != 0) {
            std::string& s = nodeErrors[e.node];
            s += (s.empty() ? "" : "\n") + e.message;
        }
    }
    for (const auto& w : compiled.warnings) m_stats.warnings.push_back(describe(w));
    m_stats.ok = compiled.Ok();
    m_stats.parameters = compiled.parameters.size();
    m_stats.textureSlots = compiled.layout.textures.size();
    m_stats.vertexLines = CountLines(compiled.vertexBody);
    m_stats.pixelLines = CountLines(compiled.pixelBody);
    m_model->SetErrors(std::move(nodeErrors));
}

void MaterialGraphEditor::BeginPropertyEdit() {
    if (!m_pendingEdit) m_pendingEdit = m_model->Snapshot();
}

void MaterialGraphEditor::Draw(Context& ui, const Rect& rect) {
    const Slate::Theme& t = ui.theme();
    ui.Panel(rect, t.panelBg);
    if (!m_model) {
        ui.LabelIn(rect, "'" + m_path + "' is not a graph material (v4).", t.textDim, Align::Center);
        return;
    }
    if (rect.Contains(ui.rawMouse()) && ui.ctrlDown() && ui.keyPressed(83) && !ui.textFieldFocused()) {
        std::string error;
        if (Save(&error)) LUMA_USER_INFO("Materials", "Saved '{}'", m_path);
        else LUMA_USER_ERROR("Materials", "Failed to save '{}': {}", m_path, error);
    }
    DrawToolbar(ui, {rect.x, rect.y, rect.w, kToolbarH});
    const f32 top = rect.y + kToolbarH;
    const f32 h = rect.h - kToolbarH;
    const f32 leftW = std::min(m_leftW, rect.w * 0.3f);
    // Palette drawer: a slim handle on the right edge (or the toolbar's
    // Palette toggle) slides the palette over the graph and hides it again.
    constexpr f32 kTabW = 14.0f;
    const Rect tab{rect.Right() - kTabW, top, kTabW, h};
    const Rect left{rect.x, top, leftW, h};
    const Rect centre{left.Right() + 2.0f, top, tab.x - left.Right() - 3.0f, h};
    // Stats collapses behind a slim horizontal edge handle (like the palette's).
    constexpr f32 kStatsHandleH = 12.0f;
    const f32 statsH = m_statsVisible ? std::min(m_statsH, centre.h * 0.4f) : 0.0f;
    const Rect graph{centre.x, centre.y, centre.w, centre.h - statsH - kStatsHandleH};
    const Rect statsHandle{centre.x, graph.Bottom(), centre.w, kStatsHandleH};
    const Rect stats{centre.x, statsHandle.Bottom(), centre.w, statsH};

    Rect preview, details;
    ui.SplitterH(Context::ID("mge.lsplit." + m_path), left, m_previewSplit, preview, details, 2.0f);
    DrawPreview(ui, preview);
    DrawDetails(ui, details);
    const f32 drawerW = std::min(m_rightW + 60.0f, graph.w * 0.6f);
    const Rect drawer{tab.x - drawerW, graph.y, drawerW, graph.h};
    if (m_paletteVisible) m_widget->SetInputExclusion(drawer);
    m_widget->Draw(ui, graph);
    if (m_statsVisible) DrawStats(ui, stats);
    {
        // Edge handle: a horizontal grip pill with chevrons pointing where Stats goes.
        const bool hover = statsHandle.Contains(ui.mouse()) && !ui.inputBlocked() && !ui.AnyMenuOpen() &&
                           !m_widget->PaletteOpen();
        ui.Panel(statsHandle, hover ? t.surface2 : t.panelBg);
        if (hover && ui.mousePressed(0)) m_statsVisible = !m_statsVisible;
        const f32 cx = statsHandle.x + statsHandle.w * 0.5f, cy = statsHandle.y + statsHandle.h * 0.5f;
        ui.PanelRounded({cx - 34.0f, cy - 3.0f, 68.0f, 6.0f}, hover ? t.accent : t.surface4, 3.0f);
        const f32 dir = m_statsVisible ? 1.0f : -1.0f;  // v hides, ^ shows
        const Slate::Color chev = hover ? t.text : t.textDim;
        for (f32 ox : {-48.0f, 48.0f}) {
            ui.drawList().AddLine({cx + ox - 4.0f, cy - 2.5f * dir}, {cx + ox, cy + 2.5f * dir}, chev, 1.5f);
            ui.drawList().AddLine({cx + ox, cy + 2.5f * dir}, {cx + ox + 4.0f, cy - 2.5f * dir}, chev, 1.5f);
        }
        ui.Tooltip(Context::ID("mge.stats.handle.tip"), statsHandle, m_statsVisible ? "Hide Stats" : "Show Stats");
    }
    if (m_paletteVisible) {
        ui.drawList().AddRectShadow(drawer, 2.0f, 0.7f);
        m_palette.Draw(ui, drawer, m_model->Actions(nullptr));
        VDivider(ui, drawer.x, drawer.y, drawer.h);
    }
    {
        // Edge handle: a grip pill with a chevron pointing where the drawer goes.
        const bool hover = tab.Contains(ui.mouse()) && !ui.inputBlocked() && !ui.AnyMenuOpen();
        ui.Panel(tab, hover ? t.surface2 : t.panelBg);
        if (hover && ui.mousePressed(0)) m_paletteVisible = !m_paletteVisible;
        const f32 cx = tab.x + tab.w * 0.5f, cy = graph.y + graph.h * 0.5f;
        const Rect pill{cx - 3.0f, cy - 34.0f, 6.0f, 68.0f};
        ui.PanelRounded(pill, hover ? t.accent : t.surface4, 3.0f);
        const f32 dir = m_paletteVisible ? 1.0f : -1.0f;  // > closes, < opens
        const Slate::Color chev = hover ? t.text : t.textDim;
        for (f32 oy : {-48.0f, 48.0f}) {
            ui.drawList().AddLine({cx - 2.5f * dir, cy + oy - 4.0f}, {cx + 2.5f * dir, cy + oy}, chev, 1.5f);
            ui.drawList().AddLine({cx + 2.5f * dir, cy + oy}, {cx - 2.5f * dir, cy + oy + 4.0f}, chev, 1.5f);
        }
        ui.Tooltip(Context::ID("mge.palette.tab.tip"), tab,
                   m_paletteVisible ? "Hide the Palette" : "Show the Palette");
    }
    // Black dividers between every panel.
    HDivider(ui, rect.x, top, rect.w);
    VDivider(ui, (left.Right() + centre.x) * 0.5f, top, h);
    VDivider(ui, (centre.Right() + tab.x) * 0.5f, top, h);
    HDivider(ui, left.x, (preview.Bottom() + details.y) * 0.5f, left.w);
    HDivider(ui, centre.x, statsHandle.y, centre.w);
    if (m_statsVisible) HDivider(ui, centre.x, statsHandle.Bottom(), centre.w);

    m_widget->DrawOverlays(ui);  // right-click palette above every panel

    // Close a property-edit gesture (one undo step per drag / edit).
    if (m_pendingEdit && !ui.isMouseDown(0) && !ui.textFieldFocused()) {
        m_controller->History().Push("Edit Property", std::move(*m_pendingEdit), m_model->Snapshot());
        m_pendingEdit.reset();
    }
}

void MaterialGraphEditor::DrawToolbar(Context& ui, const Rect& bar) {
    const Slate::Theme& t = ui.theme();
    ToolbarBackground(ui, bar);
    f32 x = bar.x + 6.0f;
    const f32 y = bar.y + 4.0f, h = bar.h - 8.0f;
    auto button = [&](const char* key, f32 w, EditorIcon png, Slate::Icon icon, const char* label, const char* tip,
                      bool active) {
        const Rect r{x, y, w, h};
        const TextureHandle tex = IconTexture(m_services, png);
        const Slate::Color tint = tex ? Slate::Color::RGB(255, 255, 255) : (active ? t.accent : t.text);
        const bool clicked = ToolButton(ui, Context::ID(std::string("mge.") + key + m_path), r, tex, icon, label, tint,
                                        active);
        ui.Tooltip(Context::ID(std::string("mge.tip.") + key), r, tip);
        x += w + 4.0f;
        return clicked;
    };
    if (button("apply", 82.0f, EditorIcon::Apply, Slate::Icon::Check, "Apply", "Apply\nCompile the graph and update the material.",
               m_unapplied))
        Apply();
    if (button("save", 76.0f, EditorIcon::Save, Slate::Icon::Save, "Save", "Save (Ctrl+S)\nApply, then write the asset.", false)) {
        std::string error;
        if (Save(&error)) LUMA_USER_INFO("Materials", "Saved '{}'", m_path);
        else LUMA_USER_ERROR("Materials", "Failed to save '{}': {}", m_path, error);
    }
    if (button("browse", 90.0f, EditorIcon::Browse, Slate::Icon::FolderOpen, "Browse", "Browse to Asset", false) &&
        m_services.browseToAsset)
        m_services.browseToAsset(m_path);
    if (button("home", 76.0f, EditorIcon::Home, Slate::Icon::Refresh, "Home", "Frame the whole graph (F frames the selection)", false))
        m_widget->RequestFrame();
    if (button("live", 112.0f, EditorIcon::LiveUpdate, Slate::Icon::Play, "Live Update", "Apply every change as you edit.", liveUpdate)) {
        liveUpdate = !liveUpdate;
        if (liveUpdate && m_unapplied) Apply();
    }
    x += 8.0f;
    bool isError = false;
    std::string status = CompileStatus(m_services, m_path, isError);
    if (status.empty() && !m_stats.ok) {
        status = std::to_string(m_stats.errors.size()) + " error(s) - see Stats";
        isError = true;
    }
    if (!status.empty()) ui.LabelIn({x, y, bar.Right() - x - 8.0f, h}, status, isError ? t.error : t.textDim);
}

void MaterialGraphEditor::DrawPreview(Context& ui, const Rect& area) {
    const Slate::Theme& t = ui.theme();
    ui.Panel(area, t.recessed);
    if (area.Contains(ui.mouse()) && ui.mousePressed(0)) m_orbiting = true;
    if (!ui.isMouseDown(0)) m_orbiting = false;
    if (m_orbiting) {
        m_preview.yaw += ui.mouseDelta().x * 0.01f;
        m_preview.pitch = std::clamp(m_preview.pitch + ui.mouseDelta().y * 0.01f, -1.4f, 1.4f);
    }
    m_preview.width = static_cast<u32>(std::max(area.w, 1.0f));
    m_preview.height = static_cast<u32>(std::max(area.h, 1.0f));
    const TextureHandle image = m_services.renderPreview ? m_services.renderPreview(m_preview) : 0;
    if (image) ui.Image(image, area);
    else ui.LabelIn(area, "Preview unavailable", t.textDisabled, Align::Center);
    // Preview shape buttons along the bottom (UE's viewport toolbar).
    const Slate::Icon icons[] = {Slate::Icon::Sphere, Slate::Icon::Cube, Slate::Icon::Plane, Slate::Icon::Cylinder};
    const EditorIcon pngs[] = {EditorIcon::Sphere, EditorIcon::Cube, EditorIcon::Plane, EditorIcon::Cylinder};
    const char* names[] = {"Sphere", "Cube", "Plane", "Cylinder"};
    f32 bx = area.x + 6.0f;
    for (int i = 0; i < 4; ++i) {
        const Rect b{bx, area.Bottom() - 32.0f, 28.0f, 26.0f};
        const bool active = static_cast<int>(m_preview.shape) == i;
        const TextureHandle png = IconTexture(m_services, pngs[i]);
        // Full-colour primitive icons draw untinted; the pressed state shows the selection.
        if (ToolButton(ui, Context::ID("mge.shape." + std::to_string(i) + m_path), b, png, icons[i], "",
                       png ? Slate::Color::RGB(255, 255, 255) : (active ? t.accent : t.text), active))
            m_preview.shape = static_cast<PreviewShape>(i);
        ui.Tooltip(Context::ID(std::string("mge.shape.tip.") + names[i]), b, std::string("Preview on a ") + names[i]);
        bx += 30.0f;
    }
}

void MaterialGraphEditor::DrawMaterialSettings(Context& ui, f32 x, f32& y, f32 w) {
    Material::MaterialSettings s = m_model->Material().settings;
    bool changed = false;
    const std::string k = "mge.set." + m_path;
    changed |= EnumCombo(ui, Context::ID(k + ".domain"), Row(ui, "Material Domain", x, y, w, false, 12.0f),
                         {"Surface", "Post Process"}, s.domain);
    changed |= EnumCombo(ui, Context::ID(k + ".blend"), Row(ui, "Blend Mode", x, y, w, false, 12.0f),
                         {"Opaque", "Masked", "Translucent", "Additive", "Modulate"}, s.blendMode);
    changed |= EnumCombo(ui, Context::ID(k + ".shading"), Row(ui, "Shading Model", x, y, w, false, 12.0f),
                         {"Default Lit", "Unlit", "Subsurface", "Clear Coat", "Two Sided Foliage"}, s.shadingModel);
    const Rect two = Row(ui, "Two Sided", x, y, w, false, 12.0f);
    changed |= ui.Checkbox(Context::ID(k + ".two"), {two.x, two.y + 3.0f, 16.0f, 16.0f}, {}, s.twoSided);
    Slate::SpinBoxOptions o;
    o.speed = 0.01f;
    o.min = 0.0f;
    o.max = 1.0f;
    o.sliderMin = 0.0f;
    o.sliderMax = 1.0f;
    changed |= ui.SpinBox(Context::ID(k + ".clip"), Row(ui, "Opacity Mask Clip", x, y, w, false, 12.0f),
                          s.opacityMaskClip, o)
                   .changed;
    if (s.blendMode == Material::BlendMode::Translucent) {
        changed |= EnumCombo(ui, Context::ID(k + ".tl"), Row(ui, "Lighting Mode", x, y, w, false, 12.0f),
                             {"Surface Per Pixel", "Unlit"}, s.translucencyLighting);
        changed |= EnumCombo(ui, Context::ID(k + ".refr"), Row(ui, "Refraction", x, y, w, false, 12.0f),
                             {"None", "Index Of Refraction", "Pixel Offset"}, s.refraction);
    }
    if (changed) {
        BeginPropertyEdit();
        m_model->MutableMaterial().settings = s;
        OnGraphChanged();
    }
}

void MaterialGraphEditor::DrawNodeProperties(Context& ui, MaterialGraph::NodeId id, f32 x, f32& y, f32 w) {
    const Slate::Theme& t = ui.theme();
    const MaterialGraph::Node* node = m_model->Material().graph.Find(id);
    const auto* d = node ? ExpressionRegistry::Get().Find(node->type) : nullptr;
    if (!d) return;
    if (!d->description.empty()) {
        ui.LabelIn({x + 12.0f, y, w - 12.0f, kRowH}, d->description, t.textDim);
        y += kRowH;
    }
    const std::string k = "mge.node." + m_path + "." + std::to_string(id) + ".";
    auto commit = [&](const std::string& key, PropertyValue v) {
        BeginPropertyEdit();
        m_model->SetProperty(id, key, std::move(v));
        OnGraphChanged();
    };
    for (const PropertyDesc& p : d->properties) {
        const PropertyValue v = MaterialGraph::PropertyOrDefault(*d, *node, p.key);
        const Rect r = Row(ui, p.label, x, y, w, false, 12.0f);
        if (!p.tooltip.empty()) ui.Tooltip(Context::ID(k + p.key + ".tip"), {x, r.y, kLabelW, r.h}, p.tooltip);
        const u64 wid = Context::ID(k + p.key);
        switch (p.kind) {
            case PropertyDesc::Kind::Bool: {
                bool b = v.AsBool();
                if (ui.Checkbox(wid, {r.x, r.y + 3.0f, 16.0f, 16.0f}, {}, b)) commit(p.key, PropertyValue(b));
                break;
            }
            case PropertyDesc::Kind::Int: {
                i32 i = static_cast<i32>(v.AsInt());
                if (ui.SpinBoxInt(wid, r, i).changed) commit(p.key, PropertyValue(static_cast<i64>(i)));
                break;
            }
            case PropertyDesc::Kind::Float: {
                f32 f = static_cast<f32>(v.AsFloat());
                Slate::SpinBoxOptions o;
                o.speed = 0.01f;
                if (p.max > p.min) {
                    o.min = static_cast<f32>(p.min);
                    o.max = static_cast<f32>(p.max);
                }
                if (ui.SpinBox(wid, r, f, o).changed) commit(p.key, PropertyValue(static_cast<f64>(f)));
                break;
            }
            case PropertyDesc::Kind::Vec4: {
                const Math::Vec4 c = v.AsVec4();
                f32 xyzw[4] = {c.x, c.y, c.z, c.w};
                Slate::SpinBoxOptions o;
                o.speed = 0.005f;
                if (ui.VectorField(wid, r, xyzw, 4, o).changed)
                    commit(p.key, PropertyValue(Math::Vec4{xyzw[0], xyzw[1], xyzw[2], xyzw[3]}));
                break;
            }
            case PropertyDesc::Kind::Enum: {
                const int cur = static_cast<int>(v.AsInt());
                const int picked = GraphEditor::DarkCombo(ui, wid, r, p.enumValues, cur);
                if (picked != cur && picked >= 0) commit(p.key, PropertyValue(static_cast<i64>(picked)));
                break;
            }
            case PropertyDesc::Kind::Asset:
                if (p.assetType == "Texture" && m_services.listTextures) {
                    const std::string picked = PickString(ui, wid, r, m_services.listTextures(), v.AsString());
                    if (!picked.empty() && picked != v.AsString()) commit(p.key, PropertyValue(picked));
                    break;
                }
                [[fallthrough]];
            case PropertyDesc::Kind::String:
            case PropertyDesc::Kind::Code: {
                std::string s = v.AsString();
                if (ui.TextField(wid, r, s)) commit(p.key, PropertyValue(s));
                break;
            }
        }
    }
    // Unconnected scalar inputs (UE's "Const A / Const B").
    for (u32 i = 0; i < d->inputs.size(); ++i) {
        const auto& pin = d->inputs[i];
        if (pin.type != MaterialGraph::ValueType::Float1 || pin.name.empty()) continue;
        if (m_model->Material().graph.Source({id, i})) continue;
        f32 f = static_cast<f32>(MaterialGraph::InputDefault(*d, *node, i).AsFloat());
        Slate::SpinBoxOptions o;
        o.speed = 0.01f;
        if (ui.SpinBox(Context::ID(k + "in." + pin.name), Row(ui, "Const " + pin.name, x, y, w, false, 12.0f), f, o)
                .changed)
            commit(MaterialGraph::InputPropertyKey(pin.name), PropertyValue(static_cast<f64>(f)));
    }
}

void MaterialGraphEditor::DrawDetails(Context& ui, const Rect& area) {
    const Slate::Theme& t = ui.theme();
    ui.Panel(area, t.panelBg);
    const Rect header = PanelTab(ui, "details.tab", area, "Details", IconTexture(m_services, EditorIcon::Details),
                                 IconAspect(m_services, EditorIcon::Details));
    const Rect view{area.x, area.y + header.h, area.w, area.h - header.h};
    ui.PushClip(view);
    const f32 x = view.x + 4.0f, w = view.w - 14.0f;
    f32 y = view.y + 4.0f - m_detailsScroll;
    const auto& sel = m_controller->Sel().Nodes();
    const bool oneNode = sel.size() == 1 && (*sel.begin() & kCommentBit) == 0 &&
                         m_model->Material().graph.Find(*sel.begin()) &&
                         *sel.begin() != m_model->Material().graph.OutputNode();
    if (oneNode) {
        const auto* n = m_model->Material().graph.Find(*sel.begin());
        const auto* d = ExpressionRegistry::Get().Find(n->type);
        Section(ui, "mge.nodesec." + m_path, d ? d->displayName : n->type, m_nodeOpen, x, y, w);
        if (m_nodeOpen) DrawNodeProperties(ui, *sel.begin(), x, y, w);
    } else if (sel.size() > 1) {
        ui.LabelIn({x + 8.0f, y, w, kRowH}, std::to_string(sel.size()) + " objects selected", t.textDim);
        y += kRowH;
    } else {
        Section(ui, "mge.settings." + m_path, "Material", m_settingsOpen, x, y, w);
        if (m_settingsOpen) DrawMaterialSettings(ui, x, y, w);
    }
    ui.PopClip();
    m_detailsScroll =
        ui.VerticalScroll(Context::ID("mge.dscroll." + m_path), view, y + m_detailsScroll - view.y + 8.0f, m_detailsScroll);
}

void MaterialGraphEditor::DrawStats(Context& ui, const Rect& area) {
    const Slate::Theme& t = ui.theme();
    ui.Panel(area, t.panelBg);
    const Rect header = PanelTab(ui, "stats.tab", area, "Stats", IconTexture(m_services, EditorIcon::Stats),
                                 IconAspect(m_services, EditorIcon::Stats));
    const Rect view{area.x, area.y + header.h, area.w, area.h - header.h};
    ui.PushClip(view);
    f32 y = view.y + 4.0f - m_statsScroll;
    auto line = [&](const std::string& s, Slate::Color c) {
        ui.LabelIn({view.x + 10.0f, y, view.w - 20.0f, 18.0f}, s, c);
        y += 18.0f;
    };
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Base pass shader: %zu pixel / %zu vertex statements    Parameters: %zu    Texture samplers: %zu",
                  m_stats.pixelLines, m_stats.vertexLines, m_stats.parameters, m_stats.textureSlots);
    line(buf, t.text);
    if (m_services.compileErrors) {
        const std::string gpu = m_services.compileErrors(m_path);
        if (!gpu.empty()) line("[GPU] " + gpu, t.error);
    }
    if (m_services.pendingCompiles && m_services.pendingCompiles() > 0) line("Compiling shaders...", t.textDim);
    for (const std::string& e : m_stats.errors) line("Error: " + e, t.error);
    for (const std::string& w : m_stats.warnings) line("Warning: " + w, t.warning);
    ui.PopClip();
    m_statsScroll =
        ui.VerticalScroll(Context::ID("mge.sscroll." + m_path), view, y + m_statsScroll - view.y + 8.0f, m_statsScroll);
}

}  // namespace Luma::MaterialEditors
