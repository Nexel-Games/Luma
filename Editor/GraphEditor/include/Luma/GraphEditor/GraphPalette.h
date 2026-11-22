#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Luma/GraphEditor/GraphModel.h"
#include "Luma/Slate/Context.h"

namespace Luma::GraphEditor {

struct PaletteGroup {
    std::string category;  // top-level category
    std::vector<GraphAction> actions;
};

// Actions matching every whitespace-separated term of `query`
// (case-insensitive, against label, category and keywords), grouped by
// top-level category, groups and entries sorted by name.
std::vector<PaletteGroup> FilterActions(const std::vector<GraphAction>& actions, std::string_view query);

// A searchable, collapsible list of actions (Unreal's SGraphPalette).
// Click an entry to call onPick; drag it onto a GraphEditorWidget to place it.
class GraphPalette {
public:
    explicit GraphPalette(std::string idPrefix = "palette") : m_id(std::move(idPrefix)) {}
    void Draw(Slate::Context& ui, const Slate::Rect& rect, const std::vector<GraphAction>& actions);
    std::function<void(const std::string& actionId)> onPick;
    std::string& Query() { return m_query; }

private:
    std::string m_id;
    std::string m_query;
    f32 m_scroll = 0.0f;
    std::map<std::string, bool> m_closed;
};

}  // namespace Luma::GraphEditor
