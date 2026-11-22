#pragma once

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Luma/GraphEditor/GraphEditorWidget.h"
#include "Luma/GraphEditor/GraphPalette.h"
#include "Luma/MaterialEditors/EditorServices.h"
#include "Luma/MaterialEditors/MaterialGraphModel.h"

namespace Luma::Material {
class MaterialLibrary;
}

namespace Luma::MaterialEditors {

// What the editor's own (main-thread) translation reports — the Stats /
// Errors panel. GPU compile status comes from the host (services).
struct MaterialStats {
    bool ok = true;
    std::vector<std::string> errors, warnings;  // "<node title>: message"
    usize parameters = 0;
    usize textureSlots = 0;
    usize vertexLines = 0, pixelLines = 0;      // generated statements (instruction estimate)
};

// The node-graph Material Editor in Unreal's layout:
//   toolbar      Apply, Save, Browse, Home (frame graph), Live Update, status
//   left         preview viewport over Details (material settings, or the
//                selected node's properties)
//   centre       the GraphEditorWidget over a MaterialGraphModel
//   right        searchable expression palette (click or drag to add)
//   bottom       Stats / Errors
// Edits stay local until Apply (or every change with Live Update), which
// hands the graph to the MaterialLibrary; the bridge compiles in the
// background.
class MaterialGraphEditor {
public:
    MaterialGraphEditor(std::string materialPath, Material::MaterialLibrary& library,
                        MaterialEditorServices services);

    void Draw(Slate::Context& ui, const Slate::Rect& rect);

    const std::string& AssetPath() const { return m_path; }
    bool Valid() const { return m_model != nullptr; }
    std::string Title() const;
    bool HasUnappliedChanges() const { return m_unapplied; }
    bool IsDirty() const;
    // Pushes the graph to the library (the bridge recompiles).
    void Apply();
    bool Save(std::string* error);  // Apply + write the file

    MaterialGraphModel& Model() { return *m_model; }
    GraphEditor::GraphController& Controller() { return *m_controller; }
    const MaterialStats& Stats() const { return m_stats; }
    bool liveUpdate = false;
    bool PaletteVisible() const { return m_paletteVisible; }
    void SetPaletteVisible(bool visible) { m_paletteVisible = visible; }
    bool StatsVisible() const { return m_statsVisible; }
    void SetStatsVisible(bool visible) { m_statsVisible = visible; }
    GraphEditor::GraphEditorWidget& Widget() { return *m_widget; }

private:
    void OnGraphChanged();
    void Recompile();
    void BeginPropertyEdit();
    void DrawToolbar(Slate::Context& ui, const Slate::Rect& bar);
    void DrawPreview(Slate::Context& ui, const Slate::Rect& area);
    void DrawDetails(Slate::Context& ui, const Slate::Rect& area);
    void DrawMaterialSettings(Slate::Context& ui, f32 x, f32& y, f32 w);
    void DrawNodeProperties(Slate::Context& ui, MaterialGraph::NodeId node, f32 x, f32& y, f32 w);
    void DrawStats(Slate::Context& ui, const Slate::Rect& area);

    std::string m_path;
    Material::MaterialLibrary& m_library;
    MaterialEditorServices m_services;
    std::unique_ptr<MaterialGraphModel> m_model;
    std::unique_ptr<GraphEditor::GraphController> m_controller;
    std::unique_ptr<GraphEditor::GraphEditorWidget> m_widget;
    GraphEditor::GraphPalette m_palette;
    MaterialStats m_stats;
    bool m_unapplied = false;
    std::optional<std::any> m_pendingEdit;  // snapshot before a property edit gesture

    PreviewRequest m_preview;
    bool m_orbiting = false;
    f32 m_leftW = 300.0f, m_rightW = 240.0f, m_statsH = 120.0f;
    f32 m_previewSplit = 0.45f;
    f32 m_detailsScroll = 0.0f, m_statsScroll = 0.0f;
    bool m_settingsOpen = true, m_nodeOpen = true;
    bool m_paletteVisible = false;  // drawer over the graph, toggled by the side tab
    bool m_statsVisible = true;     // bottom Stats panel, toggled by its edge handle
};

}  // namespace Luma::MaterialEditors
