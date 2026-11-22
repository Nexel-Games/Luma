#include "Luma/GraphEditor/GraphPalette.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "Luma/GraphEditor/GraphEditorWidget.h"

namespace Luma::GraphEditor {

namespace {
std::string Lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
}  // namespace

std::vector<PaletteGroup> FilterActions(const std::vector<GraphAction>& actions, std::string_view query) {
    std::vector<std::string> terms;
    std::istringstream in{Lower(query)};
    for (std::string t; in >> t;) terms.push_back(t);
    std::map<std::string, std::vector<GraphAction>> groups;
    for (const GraphAction& a : actions) {
        const std::string hay = Lower(a.label + " " + a.category + " " + a.keywords);
        const bool match = std::all_of(terms.begin(), terms.end(),
                                       [&](const std::string& t) { return hay.find(t) != std::string::npos; });
        if (!match) continue;
        const std::string top = a.category.substr(0, a.category.find('|'));
        groups[top.empty() ? std::string("Misc") : top].push_back(a);
    }
    std::vector<PaletteGroup> out;
    for (auto& [cat, list] : groups) {
        std::sort(list.begin(), list.end(), [](const GraphAction& x, const GraphAction& y) { return x.label < y.label; });
        out.push_back({cat, std::move(list)});
    }
    return out;
}

void GraphPalette::Draw(Slate::Context& ui, const Slate::Rect& rect, const std::vector<GraphAction>& actions) {
    using Slate::Context;
    using Slate::Rect;
    const Slate::Theme& t = ui.theme();
    ui.Panel(rect, t.panelBg);
    const Rect header{rect.x, rect.y, rect.w, 24.0f};
    ui.Panel(header, t.surface1);
    ui.LabelIn({header.x + 8.0f, header.y, header.w - 16.0f, header.h}, "Palette", t.text);
    ui.SearchBox(Context::ID(m_id + ".search"), {rect.x + 4.0f, rect.y + 28.0f, rect.w - 8.0f, 24.0f}, m_query,
                 "Search expressions");
    const Rect view{rect.x, rect.y + 56.0f, rect.w, rect.h - 56.0f};
    ui.PushClip(view);
    const bool searching = !m_query.empty();
    f32 y = view.y - m_scroll;
    constexpr f32 rowH = 22.0f;
    for (const PaletteGroup& g : FilterActions(actions, m_query)) {
        bool open = searching || !m_closed[g.category];
        if (ui.CollapsingHeader(Context::ID(m_id + ".cat." + g.category), {view.x + 2.0f, y, view.w - 12.0f, rowH},
                                g.category, open) &&
            !searching) {
            m_closed[g.category] = !open;
        }
        y += rowH + 2.0f;
        if (!open) continue;
        for (const GraphAction& a : g.actions) {
            const Rect row{view.x + 14.0f, y, view.w - 26.0f, rowH};
            const u64 id = Context::ID(m_id + ".a." + a.id);
            if (row.Bottom() > view.y && row.y < view.Bottom()) {
                Slate::DragPayload payload;
                payload.type = GraphEditorWidget::kActionDragType;
                payload.data = a.id;
                payload.label = a.label;
                ui.BeginDrag(id, row, payload);
                if (ui.Selectable(id, row, a.label, false) && onPick) onPick(a.id);
                if (!a.tooltip.empty()) ui.Tooltip(Context::ID(m_id + ".tip." + a.id), row, a.tooltip);
            }
            y += rowH;
        }
    }
    ui.PopClip();
    m_scroll = ui.VerticalScroll(Context::ID(m_id + ".scroll"), view, y + m_scroll - view.y + 8.0f, m_scroll);
}

}  // namespace Luma::GraphEditor
