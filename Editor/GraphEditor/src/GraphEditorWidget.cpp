#include "Luma/GraphEditor/GraphEditorWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "Luma/Slate/Menu.h"

namespace Luma::GraphEditor {

using Slate::Align;
using Slate::Color;
using Slate::Context;

namespace {

constexpr int kKeyC = 67, kKeyD = 68, kKeyF = 70, kKeyV = 86, kKeyX = 88, kKeyY = 89, kKeyZ = 90;
constexpr f32 kDragThreshold = 4.0f;
constexpr f32 kCornerSize = 14.0f;

f32 Dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Inserts `entry` under the '|'-separated category path.
void InsertAction(std::vector<Slate::MenuEntry>& level, std::string_view category, Slate::MenuEntry entry) {
    if (category.empty()) {
        level.push_back(std::move(entry));
        return;
    }
    const usize bar = category.find('|');
    const std::string head(category.substr(0, bar));
    const std::string_view rest = bar == std::string_view::npos ? std::string_view{} : category.substr(bar + 1);
    auto it = std::find_if(level.begin(), level.end(), [&](const Slate::MenuEntry& e) {
        return e.kind == Slate::MenuEntry::Kind::SubMenu && e.label == head;
    });
    if (it == level.end()) {
        Slate::MenuEntry sub;
        sub.kind = Slate::MenuEntry::Kind::SubMenu;
        sub.label = head;
        level.push_back(std::move(sub));
        it = level.end() - 1;
    }
    InsertAction(it->children, rest, std::move(entry));
}

}  // namespace

std::vector<Slate::MenuEntry> BuildActionMenu(const std::vector<GraphAction>& actions,
                                              const std::function<void(const std::string&)>& onPick) {
    std::vector<Slate::MenuEntry> root;
    for (const GraphAction& a : actions) {
        Slate::MenuEntry e;
        e.kind = Slate::MenuEntry::Kind::Action;
        e.label = a.label;
        e.tooltip = a.keywords.empty() ? a.tooltip : a.tooltip + "\n(" + a.keywords + ")";
        const std::string id = a.id;
        auto pick = onPick;
        e.action = [pick, id] { pick(id); };
        InsertAction(root, a.category, std::move(e));
    }
    // Categories first, alphabetically, like UE's palette.
    std::function<void(std::vector<Slate::MenuEntry>&)> sort = [&](std::vector<Slate::MenuEntry>& v) {
        std::stable_sort(v.begin(), v.end(), [](const Slate::MenuEntry& a, const Slate::MenuEntry& b) {
            const bool sa = a.kind == Slate::MenuEntry::Kind::SubMenu, sb = b.kind == Slate::MenuEntry::Kind::SubMenu;
            if (sa != sb) return sa;
            return a.label < b.label;
        });
        for (auto& e : v) sort(e.children);
    };
    sort(root);
    return root;
}

GraphEditorWidget::GraphEditorWidget(GraphController& controller, std::string idPrefix)
    : m_controller(controller), m_id(std::move(idPrefix)) {}

Vec2 GraphEditorWidget::PinAnchor(const PinKey& pin, const std::vector<NodeView>& nodes) const {
    for (const NodeView& n : nodes) {
        if (n.id != pin.node) continue;
        const NodeGeometry g = LayoutNode(n, m_controller.Metrics());
        const auto& anchors = pin.dir == PinDir::Input ? g.inputs : g.outputs;
        if (pin.index < anchors.size()) return anchors[pin.index];
    }
    return {0, 0};
}

GraphEditorWidget::Hit GraphEditorWidget::HitTest(Vec2 p, const std::vector<NodeView>& nodes) const {
    Hit hit;
    const NodeMetrics& m = m_controller.Metrics();
    // Topmost first: nodes are drawn after comments, later ones on top.
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        if (it->kind == NodeKind::Comment) continue;
        const NodeGeometry g = LayoutNode(*it, m);
        if (auto pin = HitPin(*it, g, p, m)) {
            hit.pin = pin;
            hit.node = it->id;
            return hit;
        }
        if (HitNode(*it, g, p, m)) {
            hit.node = it->id;
            return hit;
        }
    }
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        if (it->kind != NodeKind::Comment) continue;
        const NodeGeometry g = LayoutNode(*it, m);
        const Rect corner{g.body.Right() - kCornerSize, g.body.Bottom() - kCornerSize, kCornerSize, kCornerSize};
        if (corner.Contains(p)) {
            hit.node = it->id;
            hit.commentCorner = true;
            return hit;
        }
        if (HitNode(*it, g, p, m)) {
            hit.node = it->id;
            return hit;
        }
    }
    const f32 tolerance = 6.0f / m_view.Zoom();
    for (const LinkView& l : m_controller.Model().Links()) {
        const Wire w = MakeWire(PinAnchor(l.from, nodes), PinAnchor(l.to, nodes));
        if (DistanceToWire(w, p) <= tolerance) {
            hit.wire = l;
            break;
        }
    }
    return hit;
}

void GraphEditorWidget::Frame(const std::vector<NodeView>& nodes, Vec2 canvas) {
    bool any = false;
    f32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (const NodeView& n : nodes) {
        if (m_frameSelection && !m_controller.Sel().Has(n.id)) continue;
        const Rect b = LayoutNode(n, m_controller.Metrics()).body;
        if (!any) {
            x0 = b.x, y0 = b.y, x1 = b.Right(), y1 = b.Bottom();
            any = true;
        } else {
            x0 = std::min(x0, b.x), y0 = std::min(y0, b.y);
            x1 = std::max(x1, b.Right()), y1 = std::max(y1, b.Bottom());
        }
    }
    if (any) m_view.Frame({x0, y0, x1 - x0, y1 - y0}, canvas);
}

void GraphEditorWidget::Draw(Context& ui, const Rect& rect) {
    m_origin = {rect.x, rect.y};
    std::vector<NodeView> nodes = m_controller.Model().Nodes();
    // Comments draw (and hit) beneath nodes.
    std::stable_partition(nodes.begin(), nodes.end(), [](const NodeView& n) { return n.kind == NodeKind::Comment; });
    if (m_frameRequest && rect.w > 1.0f) {
        Frame(nodes, {rect.w, rect.h});
        m_frameRequest = false;
    }

    ui.PushClip(rect);
    DrawGrid(ui, rect);
    HandleInput(ui, rect, nodes);
    nodes = m_controller.Model().Nodes();
    std::stable_partition(nodes.begin(), nodes.end(), [](const NodeView& n) { return n.kind == NodeKind::Comment; });

    const NodeMetrics& m = m_controller.Metrics();
    for (const NodeView& n : nodes)
        if (n.kind == NodeKind::Comment) DrawComment(ui, n, LayoutNode(n, m));

    // Wires.
    const std::vector<LinkView> links = m_controller.Model().Links();
    std::set<std::pair<NodeId, u64>> connected;  // (node, dir<<32|index)
    const f32 thick = std::clamp(2.0f * m_view.Zoom(), 1.0f, 3.0f);
    for (const LinkView& l : links) {
        connected.insert({l.from.node, (1ull << 32) | l.from.index});
        connected.insert({l.to.node, l.to.index});
        const bool hover = m_hoverWire && m_hoverWire->from == l.from && m_hoverWire->to == l.to;
        const bool sel = m_controller.Sel().Has(l.from.node) || m_controller.Sel().Has(l.to.node);
        DrawWire(ui, m_view.ToScreen(PinAnchor(l.from, nodes), m_origin), m_view.ToScreen(PinAnchor(l.to, nodes), m_origin),
                 hover || sel ? Slate::Lighten(l.color, 0.35f) : l.color, hover ? thick + 1.5f : thick);
    }

    for (const NodeView& n : nodes) {
        if (n.kind == NodeKind::Comment) continue;
        std::set<NodeId> pins;  // encoded: input i -> i, output i -> 1000 + i
        for (u32 i = 0; i < n.inputs.size(); ++i)
            if (connected.count({n.id, i})) pins.insert(i);
        for (u32 i = 0; i < n.outputs.size(); ++i)
            if (connected.count({n.id, (1ull << 32) | i})) pins.insert(1000 + i);
        DrawNode(ui, n, LayoutNode(n, m), pins);
    }

    // Link being dragged.
    if (m_mode == Mode::Linking) {
        const Vec2 a = m_view.ToScreen(PinAnchor(m_linkFrom, nodes), m_origin);
        const Vec2 mouse = ui.mouse();
        const Color c = m_linkMessage.empty() ? ui.theme().text : (m_linkAllowed ? ui.theme().success : ui.theme().error);
        if (m_linkFrom.dir == PinDir::Output) DrawWire(ui, a, mouse, c, thick + 0.5f);
        else DrawWire(ui, mouse, a, c, thick + 0.5f);
        if (!m_linkMessage.empty()) {
            const Rect tip{mouse.x + 14.0f, mouse.y + 10.0f, 8.0f + ui.font().Measure(m_linkMessage).x, 20.0f};
            ui.PanelRounded(tip, ui.theme().surface3, 3.0f);
            ui.LabelIn(tip.Inset(4.0f, 0.0f), m_linkMessage, c);
        }
    }
    // Marquee.
    if (m_mode == Mode::Marquee) {
        const Rect r = RectFromCorners(m_pressScreen, ui.mouse());
        ui.Panel(r, ui.theme().accent.WithAlpha(40));
        ui.drawList().AddRectOutline(r, ui.theme().accent, 1.0f);
    }
    // Zoom readout (UE shows "Zoom -2" etc. in the corner).
    char zoom[32];
    std::snprintf(zoom, sizeof(zoom), "Zoom %.0f%%", m_view.Zoom() * 100.0f);
    ui.LabelIn({rect.Right() - 110.0f, rect.y + 6.0f, 100.0f, 18.0f}, zoom, ui.theme().textDim, Align::Right);
    ui.PopClip();
}

void GraphEditorWidget::DrawGrid(Context& ui, const Rect& rect) {
    ui.Panel(rect, Color::RGB(38, 38, 38));
    const f32 z = m_view.Zoom();
    auto lines = [&](f32 spacing, Color c) {
        const f32 step = spacing * z;
        if (step < 6.0f) return;
        const Vec2 g0 = m_view.ToGraph({rect.x, rect.y}, m_origin);
        f32 gx = std::floor(g0.x / spacing) * spacing;
        for (f32 sx = m_view.ToScreen({gx, 0}, m_origin).x; sx < rect.Right(); sx += step)
            ui.Panel({sx, rect.y, 1.0f, rect.h}, c);
        f32 gy = std::floor(g0.y / spacing) * spacing;
        for (f32 sy = m_view.ToScreen({0, gy}, m_origin).y; sy < rect.Bottom(); sy += step)
            ui.Panel({rect.x, sy, rect.w, 1.0f}, c);
    };
    lines(16.0f, Color::RGB(46, 46, 46));
    lines(128.0f, Color::RGB(26, 26, 26));
}

void GraphEditorWidget::DrawWire(Context& ui, Vec2 a, Vec2 b, Color color, f32 thickness) {
    const Wire w = MakeWire(a, b);
    const int segments = std::clamp(static_cast<int>(Dist(a, b) / 12.0f), 8, 48);
    Vec2 prev = w.At(0.0f);
    for (int i = 1; i <= segments; ++i) {
        const Vec2 p = w.At(static_cast<f32>(i) / static_cast<f32>(segments));
        ui.drawList().AddLine(prev, p, color, thickness);
        prev = p;
    }
}

void GraphEditorWidget::DrawComment(Context& ui, const NodeView& n, const NodeGeometry& g) {
    const Slate::Theme& t = ui.theme();
    const Rect r = m_view.RectToScreen(g.body, m_origin);
    const Color fill = n.headerColor.a == 255 ? n.headerColor.WithAlpha(70) : n.headerColor;
    ui.PanelRounded(r, fill, 4.0f);
    const Rect title{r.x, r.y, r.w, m_controller.Metrics().headerH * m_view.Zoom()};
    ui.PanelRounded(title, fill.WithAlpha(static_cast<u8>(std::min(255, fill.a * 2 + 40))), 4.0f);
    if (m_view.Zoom() >= 0.3f) ui.LabelIn(title.Inset(8.0f, 0.0f), n.title, t.text);
    if (m_controller.Sel().Has(n.id)) ui.drawList().AddRectOutlineRounded(r, t.accent, 2.0f, 4.0f);
    // Resize grip.
    const f32 c = kCornerSize * m_view.Zoom();
    ui.Triangle({r.Right() - c, r.Bottom()}, {r.Right(), r.Bottom() - c}, {r.Right(), r.Bottom()}, t.textDisabled);
}

void GraphEditorWidget::DrawNode(Context& ui, const NodeView& n, const NodeGeometry& g, const std::set<NodeId>& pins) {
    const Slate::Theme& t = ui.theme();
    const NodeMetrics& m = m_controller.Metrics();
    const f32 z = m_view.Zoom();
    const Rect r = m_view.RectToScreen(g.body, m_origin);
    const bool selected = m_controller.Sel().Has(n.id);
    const bool text = z >= 0.5f;        // titles
    const bool pinText = z >= 0.85f;    // pin labels (fonts do not scale with zoom)
    const f32 pinR = std::max(4.5f * z, 2.0f);

    auto drawPin = [&](Vec2 anchor, const PinView& pin, bool linked) {
        const Vec2 s = m_view.ToScreen(anchor, m_origin);
        ui.drawList().AddCircleFilled(s, pinR, pin.color);
        if (!linked) ui.drawList().AddCircleFilled(s, pinR * 0.55f, Color::RGB(24, 24, 24));
    };

    if (n.kind == NodeKind::Reroute) {
        const Vec2 c{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
        const Color col = n.outputs.empty() ? t.text : n.outputs[0].color;
        ui.drawList().AddCircleFilled(c, r.w * 0.45f, selected ? t.accent : Color::RGB(20, 20, 20));
        ui.drawList().AddCircleFilled(c, r.w * 0.32f, col);
        return;
    }

    ui.drawList().AddRectShadow(r, 4.0f, 0.5f);
    ui.PanelRounded(r, Color::RGBA(22, 22, 22, 235), 4.0f);
    const Rect header{r.x, r.y, r.w, m.headerH * z};
    ui.drawList().AddRectFilledGradientH(header, n.headerColor, Slate::Darken(n.headerColor, 0.6f));
    if (text) {
        ui.LabelIn({header.x + 8.0f, header.y, header.w - 16.0f, header.h}, n.title, t.text);
        if (!n.subtitle.empty()) ui.LabelIn({header.x + 8.0f, header.y, header.w - 16.0f, header.h}, n.subtitle, t.textDim, Align::Right);
    }
    for (u32 i = 0; i < n.inputs.size(); ++i) {
        drawPin(g.inputs[i], n.inputs[i], pins.count(i) != 0);
        if (pinText && !n.inputs[i].name.empty()) {
            const Vec2 s = m_view.ToScreen(g.inputs[i], m_origin);
            ui.LabelIn({s.x + 10.0f, s.y - 9.0f, r.w * 0.5f, 18.0f}, n.inputs[i].name, t.textDim);
        }
    }
    for (u32 i = 0; i < n.outputs.size(); ++i) {
        drawPin(g.outputs[i], n.outputs[i], pins.count(1000 + i) != 0);
        if (pinText && !n.outputs[i].name.empty()) {
            const Vec2 s = m_view.ToScreen(g.outputs[i], m_origin);
            ui.LabelIn({s.x - 10.0f - r.w * 0.5f, s.y - 9.0f, r.w * 0.5f, 18.0f}, n.outputs[i].name, t.textDim,
                       Align::Right);
        }
    }
    if (!n.error.empty()) {
        ui.drawList().AddRectOutlineRounded(r, t.error, 2.0f, 4.0f);
        if (text) {
            const Rect badge{r.x, r.Bottom() + 2.0f, std::max(r.w, 60.0f), 18.0f};
            ui.PanelRounded(badge, t.error, 3.0f);
            ui.LabelIn(badge.Inset(4.0f, 0.0f), "ERROR", Color::RGB(255, 255, 255));
            ui.Tooltip(Context::ID(m_id + ".err." + std::to_string(n.id)), badge, n.error);
        }
    } else if (selected) {
        ui.drawList().AddRectOutlineRounded(r, t.accent, 2.0f, 4.0f);
    }
}

void GraphEditorWidget::OpenActionMenu(Context& ui, Vec2 screen, Vec2 graphPos, std::optional<PinKey> from) {
    GraphController* controller = &m_controller;
    const PinKey* fromPtr = from ? &*from : nullptr;
    auto entries = BuildActionMenu(m_controller.Model().Actions(fromPtr), [controller, graphPos, from](const std::string& id) {
        controller->SpawnAction(id, graphPos, from);
    });
    Slate::MenuOptions o;
    o.searchable = true;
    o.searchHint = "Search nodes";
    o.maxHeight = 420.0f;
    ui.OpenMenu(Context::ID(m_id + ".actions"), screen, std::move(entries), o);
}

void GraphEditorWidget::OpenContextMenu(Context& ui, Vec2 screen, const Hit& hit) {
    GraphController* c = &m_controller;
    Slate::MenuBuilder mb;
    if (hit.pin) {
        const PinKey pin = *hit.pin;
        mb.BeginSection("Pin");
        mb.AddEntry("Break Link(s)", Slate::Icon::Close, [c, pin] { c->BreakPin(pin); });
        mb.EndSection();
    } else if (hit.node != 0) {
        if (!m_controller.Sel().Has(hit.node)) m_controller.SelectOnly(hit.node);
        const Vec2 at = m_view.ToGraph(screen, m_origin);
        mb.BeginSection("Node");
        mb.AddEntry("Delete", Slate::Icon::Trash, [c] { c->DeleteSelection(); }, {.shortcut = "Del"});
        mb.AddEntry("Duplicate", Slate::Icon::Plus, [c] { c->Duplicate(); }, {.shortcut = "Ctrl+D"});
        mb.AddEntry("Create Comment", Slate::Icon::None, [c, at] { c->AddComment(at); }, {.shortcut = "C"});
        mb.EndSection();
    } else if (hit.wire) {
        const LinkView link = *hit.wire;
        const Vec2 at = m_view.ToGraph(screen, m_origin);
        mb.BeginSection("Wire");
        mb.AddEntry("Add Reroute Node", Slate::Icon::Dot, [c, link, at] { c->InsertReroute(link, at); });
        mb.AddEntry("Break Link", Slate::Icon::Close, [c, link] { c->BreakPin(link.to); });
        mb.EndSection();
    } else {
        OpenActionMenu(ui, screen, m_view.ToGraph(screen, m_origin), std::nullopt);
        return;
    }
    ui.OpenMenu(Context::ID(m_id + ".ctx"), screen, mb.Build());
}

void GraphEditorWidget::HandleKeys(Context& ui, Vec2 mouseGraph) {
    if (ui.textFieldFocused()) return;
    if (ui.keyDelete()) m_controller.DeleteSelection();
    if (ui.ctrlDown()) {
        if (ui.keyPressed(kKeyC)) ui.SetClipboardText(m_controller.CopySelection());
        if (ui.keyPressed(kKeyX)) {
            std::string clip;
            m_controller.CutSelection(clip);
            ui.SetClipboardText(clip);
        }
        if (ui.keyPressed(kKeyV)) m_controller.Paste(ui.ClipboardText(), mouseGraph);
        if (ui.keyPressed(kKeyD)) m_controller.Duplicate();
        if (ui.keyPressed(kKeyZ)) ui.shiftDown() ? m_controller.Redo() : m_controller.Undo();
        if (ui.keyPressed(kKeyY)) m_controller.Redo();
    } else {
        if (ui.keyPressed(kKeyC)) m_controller.AddComment(mouseGraph);
        if (ui.keyPressed(kKeyF)) RequestFrame(!m_controller.Sel().Empty());
    }
}

void GraphEditorWidget::HandleInput(Context& ui, const Rect& rect, const std::vector<NodeView>& nodes) {
    const Vec2 mouse = ui.mouse();
    const Vec2 mg = m_view.ToGraph(mouse, m_origin);
    m_hovered = rect.Contains(mouse) && !ui.inputBlocked();
    m_hoverWire.reset();

    // Palette drops.
    if (const Slate::DragPayload* drop = ui.AcceptDrop(rect, kActionDragType)) {
        m_controller.SpawnAction(drop->data, mg);
    }

    if (m_hovered && m_mode == Mode::None) {
        if (ui.scrollDelta() != 0.0f) m_view.ZoomAt(ui.scrollDelta() > 0.0f ? 1 : -1, mouse, m_origin);
        HandleKeys(ui, mg);
        const Hit hover = HitTest(mg, nodes);
        if (!hover.node && !hover.pin) m_hoverWire = hover.wire;
        if (hover.pin) {
            for (const NodeView& n : nodes) {
                if (n.id != hover.pin->node) continue;
                const auto& pins = hover.pin->dir == PinDir::Input ? n.inputs : n.outputs;
                if (hover.pin->index < pins.size() && !pins[hover.pin->index].tooltip.empty()) {
                    const Vec2 s = m_view.ToScreen(PinAnchor(*hover.pin, nodes), m_origin);
                    ui.Tooltip(Context::ID(m_id + ".pin." + std::to_string(n.id) + "." + std::to_string(hover.pin->index) +
                                           (hover.pin->dir == PinDir::Input ? "i" : "o")),
                               {s.x - 8.0f, s.y - 8.0f, 16.0f, 16.0f}, pins[hover.pin->index].tooltip);
                }
            }
        }
        for (int button : {0, 1, 2}) {
            if (!ui.mousePressed(button)) continue;
            m_pressButton = button;
            m_pressScreen = mouse;
            m_pressGraph = mg;
            m_pressHit = HitTest(mg, nodes);
            m_lastMouse = mouse;
            m_mode = Mode::Pressing;
            if (button == 0) {
                if (m_pressHit.wire && !m_pressHit.node && ui.mouseDoubleClicked(0)) {
                    m_controller.InsertReroute(*m_pressHit.wire, mg);
                    m_mode = Mode::None;
                } else if (m_pressHit.pin) {
                    if (ui.altDown()) {
                        m_controller.BreakPin(*m_pressHit.pin);
                        m_mode = Mode::None;
                    } else {
                        m_mode = Mode::Linking;
                        m_linkFrom = *m_pressHit.pin;
                    }
                } else if (m_pressHit.commentCorner) {
                    m_mode = Mode::Resizing;
                    m_resizing = m_pressHit.node;
                    m_resizeBefore = m_controller.Model().Snapshot();
                    for (const NodeView& n : nodes)
                        if (n.id == m_resizing) m_resizeStartSize = n.size;
                } else if (m_pressHit.node) {
                    const NodeId id = m_pressHit.node;
                    if (ui.ctrlDown()) {
                        m_controller.Sel().Toggle(id);
                    } else if (ui.shiftDown()) {
                        m_controller.Sel().Add(id);
                    } else if (!m_controller.Sel().Has(id)) {
                        m_controller.SelectOnly(id);
                    }
                    if (m_controller.onSelectionChanged) m_controller.onSelectionChanged();
                    if (m_controller.Sel().Has(id)) {
                        m_controller.BeginMove();
                        m_mode = Mode::Moving;
                    }
                } else {
                    m_mode = Mode::Marquee;
                    m_marqueeOp = MarqueeOpFromModifiers(ui.ctrlDown(), ui.shiftDown(), ui.altDown());
                    m_marqueeBefore = m_controller.Sel().Nodes();
                }
            }
            break;
        }
    }

    if (m_mode == Mode::None) return;
    const bool down = m_pressButton >= 0 && ui.isMouseDown(m_pressButton);
    const Vec2 delta{mouse.x - m_lastMouse.x, mouse.y - m_lastMouse.y};
    m_lastMouse = mouse;

    switch (m_mode) {
        case Mode::Pressing:
            if (down && Dist(mouse, m_pressScreen) > kDragThreshold && m_pressButton != 0) m_mode = Mode::Panning;
            if (!down) {
                if (m_pressButton == 1) OpenContextMenu(ui, mouse, m_pressHit);
                m_mode = Mode::None;
            }
            break;
        case Mode::Panning:
            m_view.Pan(delta);
            if (!down) m_mode = Mode::None;
            break;
        case Mode::Moving:
            if (delta.x != 0.0f || delta.y != 0.0f)
                m_controller.DragSelection({delta.x / m_view.Zoom(), delta.y / m_view.Zoom()});
            if (!down) {
                m_controller.EndMove();
                m_mode = Mode::None;
            }
            break;
        case Mode::Resizing: {
            const Vec2 size{std::max(m_resizeStartSize.x + (mg.x - m_pressGraph.x), 80.0f),
                            std::max(m_resizeStartSize.y + (mg.y - m_pressGraph.y), 60.0f)};
            m_controller.Model().SetSize(m_resizing, size);
            if (!down) {
                m_controller.History().Push("Resize Comment", std::move(*m_resizeBefore), m_controller.Model().Snapshot());
                m_resizeBefore.reset();
                if (m_controller.onChanged) m_controller.onChanged();
                m_mode = Mode::None;
            }
            break;
        }
        case Mode::Linking: {
            const Hit over = HitTest(mg, nodes);
            m_linkMessage.clear();
            m_linkAllowed = false;
            if (over.pin && !(*over.pin == m_linkFrom)) {
                const ConnectCheck check = m_controller.CheckLink(m_linkFrom, *over.pin);
                m_linkAllowed = check.Allowed();
                m_linkMessage = check.message.empty() ? (m_linkAllowed ? "Connect" : "Not allowed") : check.message;
            }
            if (!down) {
                m_mode = Mode::None;
                if (over.pin) {
                    m_controller.TryLink(m_linkFrom, *over.pin);
                } else if (Dist(mouse, m_pressScreen) > kDragThreshold) {
                    OpenActionMenu(ui, mouse, mg, m_linkFrom);  // drag-off
                }
                m_linkMessage.clear();
            }
            break;
        }
        case Mode::Marquee: {
            const Rect gr = RectFromCorners(m_pressGraph, mg);
            std::vector<NodeId> hits;
            for (const NodeView& n : nodes) {
                const Rect b = LayoutNode(n, m_controller.Metrics()).body;
                if (n.kind == NodeKind::Comment ? ContainsRect(gr, b) : Overlaps(gr, b)) hits.push_back(n.id);
            }
            if (Dist(mouse, m_pressScreen) > kDragThreshold || !down) {
                m_controller.Sel().ApplyMarquee(hits, m_marqueeOp, m_marqueeBefore);
                if (m_controller.onSelectionChanged) m_controller.onSelectionChanged();
            }
            if (!down) m_mode = Mode::None;
            break;
        }
        case Mode::None: break;
    }
}

}  // namespace Luma::GraphEditor
