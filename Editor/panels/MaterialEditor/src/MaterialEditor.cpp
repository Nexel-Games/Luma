#include "Luma/Editor/Panels/MaterialEditor.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Core/Log.h"
#include "Luma/Material/MaterialSerializer.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Slate/Icons.h"

namespace Luma::Editor::Panels {

using Slate::Color;
using Slate::Icon;
using Slate::Rect;
using Slate::Vec2;
using Luma::Material::MaterialSerializer;

// ---------------------------------------------------------------------------
// Open / Save
// ---------------------------------------------------------------------------

bool MaterialEditorPanel::Open(const std::filesystem::path& path) {
    std::string err;
    if (!MaterialSerializer::LoadFromFile(m_material, path, &err)) {
        LUMA_LOG_ERROR("MaterialEditor", "failed to load {}: {}", path.string(), err);
        return false;
    }
    m_path = path;
    m_open = true;
    m_dirty = false;
    m_previewDirty = true;
    return true;
}

bool MaterialEditorPanel::Save() {
    if (m_path.empty()) return false;
    std::string err;
    if (!MaterialSerializer::SaveToFile(m_material, m_path, &err)) {
        LUMA_LOG_ERROR("MaterialEditor", "failed to save {}: {}", m_path.string(), err);
        return false;
    }
    m_dirty = false;
    return true;
}

Renderer2::DeferredSceneView MaterialEditorPanel::BuildDeferredSceneView(
    u32 width, u32 height) {
    using namespace Math;

    const f32 yaw = Radians(m_previewYaw);
    const f32 pitch = Radians(std::clamp(m_previewPitch, -82.0f, 82.0f));
    const f32 distance = std::clamp(m_previewDistance, 1.2f, 12.0f);
    const f32 cosPitch = std::cos(pitch);
    Vec3 eye{
        m_previewTarget.x + distance * cosPitch * std::sin(yaw),
        m_previewTarget.y + distance * std::sin(pitch),
        m_previewTarget.z + distance * cosPitch * std::cos(yaw)};

    SceneInstance& sphere = m_materialInstances[0];
    sphere = {};
    sphere.model = Translate({0.0f, 0.5f, 0.0f}) *
                                   Scale({1.0f, 1.0f, 1.0f});
    sphere.primitive = MeshPrimitive::Sphere;
    sphere.albedo =
        m_material.VectorValue(Luma::Material::MaterialProperty::BaseColor);
    sphere.metallic =
        m_material.ScalarValue(Luma::Material::MaterialProperty::Metallic);
    sphere.roughness =
        m_material.ScalarValue(Luma::Material::MaterialProperty::Roughness);

    SceneInstance& ground = m_materialInstances[1];
    ground = {};
    ground.model = Translate({0.0f, -0.04f, 0.0f}) *
                   Scale({8.0f, 0.08f, 8.0f});
    ground.primitive = MeshPrimitive::Cube;
    ground.albedo = {0.32f, 0.34f, 0.36f};
    ground.metallic = 0.0f;
    ground.roughness = 0.82f;

    m_materialLights[0] = {};
    m_materialLights[0].type = 1;
    m_materialLights[0].position = {2.5f, 3.5f, 3.0f};
    m_materialLights[0].range = 10.0f;
    m_materialLights[0].color = {1.0f, 0.88f, 0.72f};
    m_materialLights[0].intensity = 7.0f;
    m_materialLights[0].castShadows = false;

    m_materialSceneSource = {};
    m_materialSceneSource.sky.enabled = true;
    m_materialSceneSource.sky.sunDirection = Normalize(Vec3{0.35f, 0.65f, 0.55f});
    m_materialSceneSource.sky.sunIntensity = 0.45f;
    m_materialSceneSource.sky.exposure = 1.4f;
    m_materialSceneSource.sky.saturation = 0.85f;
    m_materialSceneSource.grid.enabled = false;
    m_materialSceneSource.lighting.sunDirection =
        m_materialSceneSource.sky.sunDirection;
    m_materialSceneSource.lighting.sunColor = {1.0f, 0.92f, 0.78f};
    m_materialSceneSource.lighting.sunIntensity = 4.0f;
    m_materialSceneSource.lighting.skyZenith = {0.10f, 0.14f, 0.22f};
    m_materialSceneSource.lighting.skyHorizon = {0.22f, 0.25f, 0.30f};
    m_materialSceneSource.lighting.groundColor = {0.07f, 0.08f, 0.10f};
    m_materialSceneSource.lighting.iblIntensity = 0.45f;
    m_materialSceneSource.view = LookAt(eye, m_previewTarget, Vec3{0.0f, 1.0f, 0.0f});
    m_materialSceneSource.fovYRadians = 0.8f;
    m_materialSceneSource.nearZ = 0.05f;
    m_materialSceneSource.farZ = 100.0f;
    m_materialSceneSource.instances = m_materialInstances.data();
    m_materialSceneSource.instanceCount =
        static_cast<u32>(m_materialInstances.size());
    m_materialSceneSource.lights = m_materialLights.data();
    m_materialSceneSource.lightCount = static_cast<u32>(m_materialLights.size());

    Renderer2::DeferredSceneView view;
    view.cameraPosition = eye;
    view.cameraDirection = Normalize(m_previewTarget - eye);
    view.viewMatrix = m_materialSceneSource.view;
    view.fov = m_materialSceneSource.fovYRadians;
    view.nearPlane = m_materialSceneSource.nearZ;
    view.farPlane = m_materialSceneSource.farZ;
    view.width = width;
    view.height = height;
    view.projectionMatrix = Perspective(view.fov,
                                        static_cast<f32>(width) /
                                            static_cast<f32>(height),
                                        view.nearPlane, view.farPlane);
    view.viewProjectionMatrix = view.projectionMatrix * view.viewMatrix;
    view.sceneData = &m_materialSceneSource;
    view.lightingParams = &m_materialSceneSource.lighting;
    return view;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void MaterialEditorPanel::Draw(Slate::Context& ui, const Slate::Rect& body,
                               PanelContext& ctx) {
    if (!m_open) {
        ui.LabelIn(body, "No material open.", ui.theme().textDim);
        return;
    }
    const f32 toolbarH = 34.0f;
    DrawToolbar(ui, {body.x, body.y, body.w, toolbarH});
    const f32 sidebarW = std::min(460.0f, std::max(380.0f, body.w * 0.28f));
    Rect sidebar{body.x, body.y + toolbarH, sidebarW,
                 body.h - toolbarH};
    Rect graph{sidebar.Right(), sidebar.y, body.Right() - sidebar.Right(),
               sidebar.h};
    DrawSidebar(ui, sidebar, ctx);
    DrawGraphCanvas(ui, graph);
}

void MaterialEditorPanel::DrawToolbar(Slate::Context& ui, const Rect& toolbar) {
    Slate::Theme& t = ui.theme();
    ui.GradientRect(toolbar, t.surface2, t.surface1);
    ui.Panel({toolbar.x, toolbar.Bottom() - 1.0f, toolbar.w, 1.0f},
             t.separator);
    f32 x = toolbar.x + 8.0f;
    auto command = [&](const char* label, u64 id, f32 width) {
        Rect r{x, toolbar.y + 5.0f, width, 24.0f};
        if (ui.Button(id, r, label)) {
            if (std::string_view(label) == "Save") Save();
            if (std::string_view(label) == "Clean Graph") m_graphPan = {0, 0};
        }
        x += width + 6.0f;
    };
    command("Apply", Slate::Context::ID("material.apply"), 58.0f);
    command("Save", Slate::Context::ID("material.save"), 58.0f);
    command("Search", Slate::Context::ID("material.search"), 68.0f);
    command("Clean Graph", Slate::Context::ID("material.clean"), 96.0f);
    command("Preview State", Slate::Context::ID("material.preview"), 98.0f);
    const std::string title = m_material.name.empty() ? "Untitled" : m_material.name;
    ui.Heading({toolbar.Right() - 260.0f, toolbar.y, 250.0f, toolbar.h},
               m_dirty ? title + " *" : title, t.text, Slate::Align::Right);
}

void MaterialEditorPanel::DrawSidebar(Slate::Context& ui, const Rect& rect,
                                      PanelContext& ctx) {
    Slate::Theme& t = ui.theme();
    ui.Panel(rect, t.surface1);
    const f32 previewH = std::min(310.0f, rect.h * 0.46f);
    DrawMaterialViewport(ui, {rect.x + 6.0f, rect.y + 6.0f, rect.w - 12.0f,
                              previewH}, ctx);
    const f32 tabsY = rect.y + previewH + 14.0f;
    ui.Panel({rect.x, tabsY, rect.w, 32.0f}, t.surface2);
    ui.Tab(Slate::Context::ID("material.details"), {rect.x + 4.0f, tabsY + 2.0f,
            78.0f, 28.0f}, "Details", true);
    ui.Tab(Slate::Context::ID("material.parameters"), {rect.x + 82.0f,
            tabsY + 2.0f, 92.0f, 28.0f}, "Parameters", false);
    ui.Tab(Slate::Context::ID("material.nodes"), {rect.x + 174.0f,
            tabsY + 2.0f, 70.0f, 28.0f}, "Nodes", false);
    Rect details{rect.x + 6.0f, tabsY + 38.0f, rect.w - 12.0f,
                 rect.Bottom() - tabsY - 44.0f};
    DrawDetailsPanel(ui, details, ctx);
    DrawPropertyRows(ui, details, ctx);
}

void MaterialEditorPanel::DrawGraphCanvas(Slate::Context& ui, const Rect& rect) {
    ui.Panel(rect, Color::RGB(18, 20, 23));
    ui.PushClip(rect);
    const f32 grid = 32.0f * m_graphZoom;
    const f32 ox = rect.x + std::fmod(m_graphPan.x, grid);
    const f32 oy = rect.y + std::fmod(m_graphPan.y, grid);
    for (f32 x = ox; x < rect.Right(); x += grid)
        ui.drawList().AddLine({x, rect.y}, {x, rect.Bottom()},
                              Color::RGBA(39, 43, 48, 150), 1.0f);
    for (f32 y = oy; y < rect.Bottom(); y += grid)
        ui.drawList().AddLine({rect.x, y}, {rect.Right(), y},
                              Color::RGBA(39, 43, 48, 150), 1.0f);

    if (rect.Contains(ui.mouse()) && ui.isMouseDown(2)) {
        m_graphPan.x += ui.mouseDelta().x;
        m_graphPan.y += ui.mouseDelta().y;
        ui.RequestCursor(Luma::CursorShape::ResizeEW);
    }

    GraphNodeState nodes[] = {
        {"base-color", "Base Color", m_baseColorPosition, {210, 150},
         Color::RGBA(165, 120, 35, 255), false, m_selectedNode == 0},
        {"metallic", "Metallic", m_metallicPosition, {190, 106},
         Color::RGBA(92, 145, 55, 255), false, m_selectedNode == 1},
        {"roughness", "Roughness", m_roughnessPosition, {190, 106},
         Color::RGBA(92, 145, 55, 255), false, m_selectedNode == 2},
        {"output", "Material Output", m_outputPosition, {255, 390},
         Color::RGBA(100, 75, 45, 255), true, m_selectedNode == 3},
    };
    auto screenRect = [&](const GraphNodeState& n) {
        return Rect{rect.x + m_graphPan.x + n.position.x * m_graphZoom,
                    rect.y + m_graphPan.y + n.position.y * m_graphZoom,
                    n.size.x * m_graphZoom, n.size.y * m_graphZoom};
    };
    // Wires are drawn before node bodies, matching Unreal's graph layering.
    const Rect base = screenRect(nodes[0]);
    const Rect metal = screenRect(nodes[1]);
    const Rect rough = screenRect(nodes[2]);
    const Rect output = screenRect(nodes[3]);
    auto wire = [&](Vec2 a, Vec2 b, Color c) {
        Vec2 p1{a.x + 55.0f, a.y};
        Vec2 p2{b.x - 55.0f, b.y};
        for (int i = 0; i < 16; ++i) {
            f32 t0 = static_cast<f32>(i) / 16.0f;
            f32 t1 = static_cast<f32>(i + 1) / 16.0f;
            auto bezier = [&](f32 t) {
                f32 u = 1.0f - t;
                return Vec2{u*u*u*a.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*b.x,
                            u*u*u*a.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*b.y};
            };
            ui.drawList().AddLine(bezier(t0), bezier(t1), c, 2.0f);
        }
    };
    wire({base.Right(), base.y + 40.0f},
         {output.x, output.y + 42.0f}, Color::RGB(220, 180, 55));
    wire({metal.Right(), metal.y + 42.0f},
         {output.x, output.y + 80.0f}, Color::RGB(110, 190, 85));
    wire({rough.Right(), rough.y + 42.0f},
         {output.x, output.y + 118.0f}, Color::RGB(110, 190, 85));
    for (int i = 0; i < 4; ++i) DrawGraphNode(ui, nodes[i], rect);
    m_baseColorPosition = nodes[0].position;
    m_metallicPosition = nodes[1].position;
    m_roughnessPosition = nodes[2].position;
    m_outputPosition = nodes[3].position;
    ui.PopClip();
}

void MaterialEditorPanel::DrawGraphNode(Slate::Context& ui, GraphNodeState& node,
                                        const Rect& canvas) {
    Rect r{canvas.x + m_graphPan.x + node.position.x * m_graphZoom,
           canvas.y + m_graphPan.y + node.position.y * m_graphZoom,
           node.size.x * m_graphZoom, node.size.y * m_graphZoom};
    if (r.Contains(ui.mouse()) && ui.mousePressed(0)) {
        m_selectedNode = node.id == std::string_view("base-color") ? 0 :
                         node.id == std::string_view("metallic") ? 1 :
                         node.id == std::string_view("roughness") ? 2 : 3;
        m_draggedNode = m_selectedNode;
        m_dragOffset = {ui.mouse().x - r.x, ui.mouse().y - r.y};
    }
    if (m_draggedNode >= 0 && ui.isMouseDown(0) && m_selectedNode ==
        (node.id == std::string_view("base-color") ? 0 : node.id == std::string_view("metallic") ? 1 : node.id == std::string_view("roughness") ? 2 : 3)) {
        node.position.x = (ui.mouse().x - canvas.x - m_graphPan.x - m_dragOffset.x) /
                          m_graphZoom;
        node.position.y = (ui.mouse().y - canvas.y - m_graphPan.y - m_dragOffset.y) /
                          m_graphZoom;
    }
    if (!ui.isMouseDown(0)) m_draggedNode = -1;
    Slate::GraphNodeWidget::Spec spec;
    spec.title = node.title;
    spec.headerTint = node.tint;
    spec.selected = node.selected;
    spec.centerTitle = node.output;
    spec.largeLabels = node.output;
    spec.inputLabelsInside = node.output;
    if (node.output) {
        spec.inputs = {{"Base Color", Color::RGB(220, 180, 55)},
                       {"Metallic", Color::RGB(110, 190, 85)},
                       {"Roughness", Color::RGB(110, 190, 85)},
                       {"Specular", Color::RGB(110, 190, 85)},
                       {"Normal", Color::RGB(90, 150, 220)},
                       {"Emissive", Color::RGB(220, 90, 170)}};
        spec.inputRowMap = {0, 1, 2, 3, 5, 7};
    } else {
        spec.outputs = {{"", node.tint}};
    }
    Slate::GraphNodeWidget::Draw(ui, r, spec, Slate::GraphNodeWidget::kTitleH,
                                 Slate::GraphNodeWidget::kPinRowH);
    if (!node.output) {
        const f32 y = Slate::GraphNodeWidget::BodyTop(
            r, Slate::GraphNodeWidget::kTitleH, Slate::GraphNodeWidget::kPinRowH, 1);
        std::string value;
        if (node.id == std::string_view("base-color")) {
            const Math::Vec3 color = m_material.VectorValue(
                Luma::Material::MaterialProperty::BaseColor);
            value = "RGB " + std::to_string(color.x) + ", " +
                    std::to_string(color.y) + ", " + std::to_string(color.z);
        } else if (node.id == std::string_view("metallic")) {
            value = "Value " + std::to_string(
                m_material.ScalarValue(Luma::Material::MaterialProperty::Metallic));
        } else {
            value = "Value " + std::to_string(
                m_material.ScalarValue(Luma::Material::MaterialProperty::Roughness));
        }
        ui.LabelIn({r.x + 12.0f, y + 10.0f, r.w - 24.0f, 24.0f},
                   value, ui.theme().textDim);
    }
}

// ---------------------------------------------------------------------------
// Material scene viewport
// ---------------------------------------------------------------------------

void MaterialEditorPanel::DrawMaterialViewport(Slate::Context& ui,
                                               const Slate::Rect& rect,
                                               PanelContext& ctx) {
    (void)ctx;
    Slate::Theme& t = ui.theme();
    ui.PanelRoundedBordered(rect, t.panelBg, t.outline, t.radius.sm, t.border.hairline);
    ui.Label({rect.x + 8.0f, rect.y + 4.0f}, "Perspective", t.textDim);
    ui.LabelIn({rect.Right() - 118.0f, rect.y + 4.0f, 110.0f, 18.0f},
               "Material Scene", t.textDim, Slate::Align::Right);

    Rect vp{rect.x + 4.0f, rect.y + 22.0f, rect.w - 8.0f, rect.h - 26.0f};
    m_previewRect = vp;

    const u32 previewW = static_cast<u32>(std::max(1.0f, vp.w));
    const u32 previewH = static_cast<u32>(std::max(1.0f, vp.h));

    if (m_previewTexture) {
        ui.Image(m_previewTexture, vp);
    } else {
        ui.Panel(vp, Slate::Color::RGB(13, 13, 13));
    }
    m_previewW = previewW;
    m_previewH = previewH;
    m_previewDirty = false;

    // Match the editor viewport interaction model: right-drag orbits around
    // the scene target, middle-drag trucks the target, wheel dollies.
    if (vp.Contains(ui.mouse()) && ui.isMouseDown(1)) {
        Slate::Vec2 delta = ui.mouseDelta();
        m_previewYaw -= delta.x * 0.35f;
        m_previewPitch += delta.y * 0.35f;
        m_previewPitch = std::clamp(m_previewPitch, -82.0f, 82.0f);
        m_previewDirty = true;
        ui.RequestCursor(Luma::CursorShape::ResizeEW);
    }
    if (vp.Contains(ui.mouse()) && ui.isMouseDown(2)) {
        Slate::Vec2 delta = ui.mouseDelta();
        const f32 panScale = m_previewDistance * 0.0025f;
        const f32 yaw = Math::Radians(m_previewYaw);
        Math::Vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
        Math::Vec3 up{0.0f, 1.0f, 0.0f};
        m_previewTarget = m_previewTarget - right * (delta.x * panScale) +
                          up * (delta.y * panScale);
        m_previewDirty = true;
        ui.RequestCursor(Luma::CursorShape::ResizeEW);
    }
    if (vp.Contains(ui.mouse())) {
        const f32 scroll = ui.scrollDelta();
        if (scroll != 0.0f) {
            m_previewDistance = std::clamp(
                m_previewDistance * std::pow(0.9f, scroll), 1.2f, 12.0f);
            m_previewDirty = true;
        }
    }
}

// ---------------------------------------------------------------------------
// Details panel (name, blend mode, texture slots)
// ---------------------------------------------------------------------------

void MaterialEditorPanel::DrawDetailsPanel(Slate::Context& ui,
                                           const Slate::Rect& rect,
                                           PanelContext& /*ctx*/) {
    Slate::Theme& t = ui.theme();
    const f32 kRowH = 22.0f;
    f32 y = rect.y + 4.0f;

    // Name.
    ui.Label({rect.x + 6.0f, y}, "Name", t.textDim);
    y += 18.0f;
    Rect nameR{rect.x + 6.0f, y, rect.w - 12.0f, kRowH};
    if (ui.TextField(Slate::Context::ID("mat.name"), nameR, m_material.name)) {
        MarkDirty();
    }
    y += kRowH + 8.0f;

    // Blend mode.
    ui.Label({rect.x + 6.0f, y}, "Blend Mode", t.textDim);
    y += 18.0f;
    static const char* kBlendNames[] = {"Opaque", "Masked", "Translucent"};
    for (int i = 0; i < 3; ++i) {
        Rect btn{rect.x + 6.0f + i * 68.0f, y, 64.0f, kRowH};
        bool active = (static_cast<int>(m_material.blendMode) == i);
        bool hovered = btn.Contains(ui.mouse());
        if (active) {
            ui.drawList().AddRectFilledRounded(btn, t.accent, t.radius.sm);
        } else if (hovered) {
            ui.drawList().AddRectFilledRounded(btn, t.surface2, t.radius.sm);
        }
        ui.Label({btn.x + 4.0f, btn.y + (btn.h - ui.font().LineHeight()) * 0.5f},
                 kBlendNames[i], active ? Color{1,1,1,1} : t.text);
        if (hovered && ui.mouseReleased(0)) {
            m_material.blendMode = static_cast<Luma::Material::BlendMode>(i);
            MarkDirty();
        }
    }
    y += kRowH + 8.0f;

    // Texture slots.
    auto drawTexSlot = [&](const char* label, AssetId& slot) {
        ui.Label({rect.x + 6.0f, y}, label, t.textDim);
        y += 18.0f;
        Rect slotR{rect.x + 6.0f, y, rect.w - 12.0f, kRowH};
        std::string texName = slot.IsValid() ? ToString(slot) : "(none)";
        ui.Label({slotR.x + 4.0f, slotR.y + (slotR.h - ui.font().LineHeight()) * 0.5f},
                 texName, slot.IsValid() ? t.text : t.textDim);
        if (slotR.Contains(ui.mouse()) && ui.mouseReleased(0)) {
            // Toggle off if assigned.
            if (slot.IsValid()) {
                slot = AssetId{};
                MarkDirty();
            }
        }
        y += kRowH + 4.0f;
    };
    drawTexSlot("Base Color Map", m_material.baseColorMap);
    drawTexSlot("Normal Map", m_material.normalMap);
    drawTexSlot("Roughness Map", m_material.roughnessMap);
    drawTexSlot("Metallic Map", m_material.metallicMap);
}

// ---------------------------------------------------------------------------
// Property rows (scalar/vector values for each MaterialProperty)
// ---------------------------------------------------------------------------

void MaterialEditorPanel::DrawPropertyRows(Slate::Context& ui,
                                           const Slate::Rect& rect,
                                           PanelContext& /*ctx*/) {
    Slate::Theme& t = ui.theme();
    const f32 kRowH = 22.0f;

    // The properties section starts below the details panel.  We draw it
    // at the bottom of rightCol (details panel takes the top portion).
    // Simple: just draw after a fixed offset.
    f32 startY = rect.y + 320.0f;  // below details
    if (startY >= rect.Bottom()) return;

    ui.Label({rect.x + 6.0f, startY}, "Material Properties", t.textDim);
    startY += 20.0f;

    struct PropRow {
        Luma::Material::MaterialProperty prop;
        const char* label;
        bool isVector;  // true = RGB color, false = scalar
    };
    static const PropRow rows[] = {
        {Luma::Material::MaterialProperty::BaseColor, "Base Color", true},
        {Luma::Material::MaterialProperty::Metallic, "Metallic", false},
        {Luma::Material::MaterialProperty::Roughness, "Roughness", false},
        {Luma::Material::MaterialProperty::Specular, "Specular", false},
        {Luma::Material::MaterialProperty::EmissiveColor, "Emissive Color", true},
        {Luma::Material::MaterialProperty::Opacity, "Opacity", false},
        {Luma::Material::MaterialProperty::AmbientOcclusion, "AO", false},
    };

    f32 y = startY;
    for (const auto& row : rows) {
        if (y + kRowH > rect.Bottom()) break;
        ui.Label({rect.x + 6.0f, y + 2.0f}, row.label, t.textDim);

        if (row.isVector) {
            Math::Vec3 v = m_material.VectorValue(row.prop);
            // Color swatch using ColorPickerPopup.
            Rect swatch{rect.Right() - 40.0f, y, 34.0f, kRowH};
            auto& picker = m_colorPickers[row.prop];
            picker.SetColor(Color::RGB(
                static_cast<u8>(v.x * 255.0f),
                static_cast<u8>(v.y * 255.0f),
                static_cast<u8>(v.z * 255.0f)));
            picker.DrawSwatch(ui, swatch);
            picker.OnColorChanged = [this, prop = row.prop](const Color& c) {
                m_material.SetVectorValue(prop, Math::Vec3{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f});
                MarkDirty();
            };
        } else {
            // Scalar field.
            f32 val = m_material.ScalarValue(row.prop);
            Rect field{rect.x + 80.0f, y, rect.w - 90.0f, kRowH};
            std::string str = std::to_string(val);
            if (ui.TextField(Slate::Context::ID("mat.scalar"), field, str)) {
                try { m_material.SetScalarValue(row.prop, std::stof(str)); MarkDirty(); }
                catch (...) {}
            }
        }
        y += kRowH + 4.0f;
    }
}

// ---------------------------------------------------------------------------
// Floating color pickers
// ---------------------------------------------------------------------------

void MaterialEditorPanel::DrawFloatingPickers(Slate::Context& ui) {
    for (auto& [prop, picker] : m_colorPickers) {
        if (!picker.IsOpen()) continue;
        picker.DrawPanel(ui);
    }
}

}  // namespace Luma::Editor::Panels
