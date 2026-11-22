#pragma once

#include <optional>
#include <set>
#include <string>

#include "Luma/GraphEditor/GraphController.h"
#include "Luma/GraphEditor/GraphPalette.h"
#include "Luma/Slate/Context.h"

namespace Luma::GraphEditor {

// Builds the searchable "add node" menu from model actions: categories
// ('|' nests) become submenus; picking one calls `onPick(actionId)`.
std::vector<Slate::MenuEntry> BuildActionMenu(const std::vector<GraphAction>& actions,
                                              const std::function<void(const std::string&)>& onPick);

// The Slate graph panel (SGraphPanel's role): draws the grid, comments,
// wires and nodes of a GraphController's model and turns input into
// controller calls.
//
//   LMB node            select (Ctrl toggle, Shift add) and drag to move
//   LMB pin             drag a link; drop on a pin to connect, on empty
//                       space for a filtered add-node menu (drag-off)
//   Alt+LMB pin         break its links
//   LMB empty           marquee (Shift add, Alt remove, Ctrl invert)
//   LMB comment corner  resize the comment
//   Double-click wire   insert a reroute node
//   RMB / MMB drag      pan;  wheel: zoom at the cursor
//   RMB click           empty canvas: the palette pops up at the cursor
//                       (category filter, search, tree; Enter adds the
//                       first match); node / pin / wire: context menu
//   Del, Ctrl+C/X/V/D/Z/Y (Ctrl+Shift+Z), C = comment, F = frame
// Palette drags of type kActionDragType drop as new nodes.
class GraphEditorWidget {
public:
    static constexpr const char* kActionDragType = "GraphAction";

    explicit GraphEditorWidget(GraphController& controller, std::string idPrefix = "graph");

    void Draw(Slate::Context& ui, const Slate::Rect& rect);
    // Popups above every panel (the right-click palette): call after the
    // host has drawn everything else.
    void DrawOverlays(Slate::Context& ui);
    // Large faint text in the canvas' bottom-right corner (Unreal's
    // "MATERIAL"); `font` null = the UI title font.
    void SetWatermark(std::string text, const Slate::Font* font = nullptr) {
        m_watermark = std::move(text);
        m_watermarkFont = font;
    }
    bool PaletteOpen() const { return m_paletteOpen; }
    GraphPalette& PopupPalette() { return m_popup; }
    void ClosePalette() { m_paletteOpen = false; }
    // A host panel drawn over the canvas (e.g. a palette drawer): the
    // canvas ignores the pointer there. Set every frame before Draw.
    void SetInputExclusion(const Slate::Rect& rect) { m_exclusion = rect; }

    ViewTransform& View() { return m_view; }
    // Frames everything (or the selection) on the next Draw.
    void RequestFrame(bool selectionOnly = false) {
        m_frameRequest = true;
        m_frameSelection = selectionOnly;
    }
    // Hovered this frame (for host shortcuts).
    bool Hovered() const { return m_hovered; }

private:
    enum class Mode : u8 { None, Pressing, Panning, Moving, Linking, Marquee, Resizing };

    struct Hit {
        std::optional<PinKey> pin;
        NodeId node = 0;
        bool commentCorner = false;
        std::optional<LinkView> wire;
    };
    Hit HitTest(Vec2 graphPoint, const std::vector<NodeView>& nodes) const;
    Vec2 PinAnchor(const PinKey& pin, const std::vector<NodeView>& nodes) const;

    void HandleInput(Slate::Context& ui, const Slate::Rect& rect, const std::vector<NodeView>& nodes);
    void HandleKeys(Slate::Context& ui, Vec2 mouseGraph);
    void OpenContextMenu(Slate::Context& ui, Vec2 screen, const Hit& hit);
    void OpenActionMenu(Slate::Context& ui, Vec2 screen, Vec2 graphPos, std::optional<PinKey> from);
    void Frame(const std::vector<NodeView>& nodes, Vec2 canvas);

    void DrawGrid(Slate::Context& ui, const Slate::Rect& rect);
    void DrawComment(Slate::Context& ui, const NodeView& n, const NodeGeometry& g);
    void DrawNode(Slate::Context& ui, const NodeView& n, const NodeGeometry& g, const std::set<NodeId>& connected);
    void DrawWire(Slate::Context& ui, Vec2 a, Vec2 b, Slate::Color color, f32 thickness);

    GraphController& m_controller;
    std::string m_id;
    ViewTransform m_view;
    Vec2 m_origin{0, 0};
    bool m_hovered = false;
    bool m_frameRequest = true;
    bool m_frameSelection = false;

    Mode m_mode = Mode::None;
    int m_pressButton = -1;
    Vec2 m_pressScreen{0, 0};
    Vec2 m_lastMouse{0, 0};
    Vec2 m_pressGraph{0, 0};
    Hit m_pressHit;
    PinKey m_linkFrom;
    std::set<NodeId> m_marqueeBefore;
    MarqueeOp m_marqueeOp = MarqueeOp::Replace;
    NodeId m_resizing = 0;
    Vec2 m_resizeStartSize{0, 0};
    std::optional<std::any> m_resizeBefore;
    std::optional<LinkView> m_hoverWire;
    std::string m_linkMessage;
    Slate::Rect m_exclusion{};
    std::string m_watermark;
    const Slate::Font* m_watermarkFont = nullptr;
    // Right-click / drag-off palette popup.
    GraphPalette m_popup;
    bool m_paletteOpen = false;
    int m_paletteAge = 0;  // frames since opening (ignore the opening click)
    Vec2 m_paletteScreen{0, 0}, m_paletteGraph{0, 0};
    std::optional<PinKey> m_paletteFrom;
    bool m_linkAllowed = false;
};

}  // namespace Luma::GraphEditor
