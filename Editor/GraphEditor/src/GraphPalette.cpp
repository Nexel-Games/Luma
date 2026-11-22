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

void GraphPalette::FocusSearch(Slate::Context& ui) { ui.FocusField(Slate::Context::ID(m_id + ".search")); }

void GraphPalette::Reset() {
    m_query.clear();
    m_category.clear();
    m_scroll = 0.0f;
    m_open.clear();
}

void GraphPalette::Draw(Slate::Context& ui, const Slate::Rect& rect, const std::vector<GraphAction>& actions) {
    using Slate::Context;
    using Slate::Rect;
    const Slate::Theme& t = ui.theme();
    ui.Panel(rect, t.panelBg);
    // Category filter (UE: "Category: All").
    std::vector<PaletteGroup> groups = FilterActions(actions, m_query);
    {
        std::vector<std::string> cats{"All"};
        for (const PaletteGroup& g : FilterActions(actions, "")) cats.push_back(g.category);
        int current = 0;
        for (usize i = 1; i < cats.size(); ++i)
            if (cats[i] == m_category) current = static_cast<int>(i);
        ui.LabelIn({rect.x + 6.0f, rect.y + 4.0f, 64.0f, 22.0f}, "Category:", t.textDim);
        const int picked = ui.Combo(Context::ID(m_id + ".category"), {rect.x + 72.0f, rect.y + 4.0f, rect.w - 78.0f, 22.0f},
                                    cats, current);
        if (picked != current && picked >= 0) m_category = picked == 0 ? std::string() : cats[static_cast<usize>(picked)];
        if (!m_category.empty())
            std::erase_if(groups, [&](const PaletteGroup& g) { return g.category != m_category; });
    }
    ui.SearchBox(Context::ID(m_id + ".search"), {rect.x + 4.0f, rect.y + 30.0f, rect.w - 8.0f, 24.0f}, m_query,
                 searchIcon, "Search");
    const Rect view{rect.x, rect.y + 58.0f, rect.w, rect.h - 58.0f};
    const Slate::Color divider = Slate::Color::RGB(8, 8, 8);
    ui.Panel({rect.x, view.y - 2.0f, rect.w, 2.0f}, divider);  // under filter + search
    if (!m_query.empty() && ui.enterPressed() && !groups.empty() && onPick) {
        onPick(groups.front().actions.front().id);
        return;
    }
    ui.PushClip(view);
    const bool searching = !m_query.empty() || !m_category.empty();
    f32 y = view.y - m_scroll;
    constexpr f32 rowH = 22.0f;
    for (const PaletteGroup& g : groups) {
        auto it = m_open.try_emplace(g.category, m_categoriesOpen).first;
        bool open = searching || it->second;
        if (ui.CollapsingHeader(Context::ID(m_id + ".cat." + g.category), {view.x + 2.0f, y, view.w - 12.0f, rowH},
                                g.category, open) &&
            !searching) {
            it->second = open;
        }
        ui.Panel({view.x, y + rowH + 1.0f, view.w, 1.0f}, divider);  // under each category
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
