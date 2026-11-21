#include "Luma/Editor/Panels/Preferences.h"

#include <algorithm>
#include <cctype>

#include "Luma/Math/MathUtils.h"
#include "Luma/Slate/Icons.h"

namespace Luma::Editor::Panels {
namespace {
using Slate::Context;
using Slate::Rect;
constexpr std::array<const char*, 3> kCategories{"Viewports", "Navigation", "Snapping"};
struct Section { const char* name; int category; };
constexpr std::array<Section, static_cast<usize>(PreferenceSection::Count)> kSections{{
    {"Display", 0}, {"Post Processing", 0}, {"Camera", 1},
    {"Translation", 2}, {"Rotation", 2}, {"Scale", 2}
}};

bool Contains(std::string_view text, std::string_view query) {
    return std::ranges::search(text, query, [](unsigned char a, unsigned char b) {
        return std::tolower(a) == std::tolower(b);
    }).begin() != text.end();
}

bool KeyboardActivate(Context& ui, u64 id, const Rect& rect) {
    ui.RegisterFocusable(id, false);
    if (!ui.IsFieldFocused(id)) return false;
    ui.drawList().AddRectOutline(rect, ui.theme().focusRing, 1);
    return ui.enterPressed() || ui.keySpace();
}

void DrawControl(Context& ui, const PreferenceRow& row, Rect field) {
    const u64 id = Context::ID(row.label) ^ Context::ID("preferences.value");
    field.w = std::min(field.w, 164.0f);
    if (auto flag = std::get_if<bool*>(&row.value)) {
        ui.Checkbox(id, {field.x, field.y + 2, 18, 18}, "", **flag);
        return;
    }
    Slate::SpinBoxOptions options;
    options.min = row.angleInRadians ? 30.0 : static_cast<double>(row.minimum);
    options.max = row.angleInRadians ? 120.0 : static_cast<double>(row.maximum);
    options.suffix = row.suffix;
    if (auto number = std::get_if<i32*>(&row.value)) {
        options.speed = 0.1;
        ui.SpinBoxInt(id, field, **number, options);
    } else if (row.angleInRadians) {
        f32 degrees = Math::Degrees(static_cast<f32>(row.Read()));
        options.speed = 0.2;
        if (ui.SpinBox(id, field, degrees, options).changed) row.Set(Math::Radians(degrees));
    } else {
        ui.SpinBox(id, field, *std::get<f32*>(row.value), options);
    }
}
}  // namespace

bool PreferencesPanel::Draw(Context& ui, f32 width, f32 height) {
    const auto& t = ui.theme();
    const u64 searchId = Context::ID("preferences.search");
    bool escape = false;
    if (ui.keyEscape() && !ui.AnyMenuOpen()) {
        if (ui.IsFieldFocused(searchId) && !m_search.empty()) {
            m_search.clear();
            m_scroll = 0;
            ui.BlurField(searchId);
        } else {
            escape = !ui.textFieldFocused() || ui.IsFieldFocused(searchId);
        }
    }
    ui.Panel({0, 0, width, height}, t.surface0);
    ui.Panel({0, 0, width, 38}, t.tabWell);
    ui.LogoMark({20, 19}, 12);
    Rect tab{40, 4, 210, 34};
    ui.PanelRounded(tab, t.surface2, t.radius.sm);
    Slate::DrawIcon(ui, {52, 12, 18, 18}, Slate::Icon::Gear, t.textDim);
    ui.Heading({66, 4, 150, 34}, "Editor Preferences", t.text);
    Rect close{218, 7, 27, 28};
    const u64 closeId = Context::ID("preferences.close");
    const bool clickedClose = ui.MenuButton(closeId, close, "");
    Slate::DrawIcon(ui, close.Inset(7, 7), Slate::Icon::Close, t.textDim);
    ui.Tooltip(close, "Close Editor Preferences (Esc)");
    if (KeyboardActivate(ui, closeId, close) || clickedClose || escape) {
        ui.FocusWidget(0);
        return false;
    }

    const bool compact = width < 660;
    const f32 sidebar = compact ? 0.0f : std::clamp(width * 0.22f, 170.0f, 296.0f);
    const f32 margin = compact ? 10.0f : 22.0f;
    const f32 x = sidebar + margin;
    const f32 w = std::max(1.0f, width - x - margin);
    f32 y = 54;
    auto choose = [&](int category) {
        m_category = category;
        m_search.clear();
        m_scroll = 0;
    };
    if (compact) {
        static const std::vector<std::string> labels(kCategories.begin(), kCategories.end());
        const int next = ui.Combo(Context::ID("preferences.categories"), {x,y,w,26}, labels, m_category);
        if (next != m_category) choose(next);
        y += 34;
    } else {
        ui.Panel({0, 38, sidebar, height - 38}, t.surface1);
        ui.Panel({sidebar - 1, 38, 1, height - 38}, t.separator);
        ui.LabelIn({12, 54, sidebar - 20, 36}, "Level Editor", t.text, Slate::Align::Left, true);
        for (int i = 0; i < static_cast<int>(kCategories.size()); ++i) {
            Rect item{15, 98.0f + i * 32.0f, sidebar - 27, 30};
            const u64 id = Context::ID(kCategories[i]) ^ Context::ID("preferences.category");
            const bool selected = m_search.empty() && m_category == i;
            const bool picked = ui.Selectable(id, item, kCategories[i], selected);
            if (selected) ui.Panel({item.x, item.y + 5, 2, item.h - 10}, t.accent);
            if (KeyboardActivate(ui, id, item) || picked) choose(i);
        }
    }

    if (ui.ctrlDown() && ui.keyPressed(70)) ui.FocusField(searchId, m_search.size());
    if (ui.SearchBox(searchId, {x, y, w, 28}, m_search, "Search preferences")) m_scroll = 0;
    y += 40;
    const bool searching = !m_search.empty();
    ui.LabelIn({x, y, w, 40}, searching ? "Search Results" : std::string("Level Editor - ") + kCategories[m_category],
               t.text, Slate::Align::Left, true);
    y += 40;
    ui.LabelIn({x, y, w, 24}, "Personal settings for all Luma projects", t.textDim);
    y += 34;

    if (m_saveFailed) {
        ui.LabelIn({x, y, w - 76, 26}, "Could not save preferences.", t.error);
        if (ui.Button(Context::ID("preferences.retry"), {x + w - 72, y, 72, 26}, "Retry")) SaveChanges(true);
        y += 32;
    }
    const Rect list{x, y, w, std::max(0.0f, height - y - 12)};
    auto matches = [&](const PreferenceRow& row) {
        const auto& section = kSections[static_cast<usize>(row.section)];
        if (!searching) return section.category == m_category;
        return Contains(row.label, m_search) || Contains(section.name, m_search) ||
               Contains(kCategories[section.category], m_search);
    };
    std::array<int, kSections.size()> counts{};
    for (const auto& row : m_rows) {
        if (matches(row)) ++counts[static_cast<usize>(row.section)];
    }
    constexpr f32 headerH = 29, rowH = 34, gap = 9;
    f32 total = 0;
    for (usize s = 0; s < counts.size(); ++s) {
        if (counts[s]) total += headerH + gap + ((searching || m_expanded[s]) ? counts[s] * rowH : 0);
    }
    m_scroll = ui.VerticalScroll(Context::ID("preferences.scroll"), list, total, m_scroll);
    y -= m_scroll;
    const f32 rowW = w - (total > list.h ? 10.0f : 0.0f);
    ui.PushClip(list);
    if (total == 0) ui.LabelIn({x, y, w, 38}, "No matching preferences.", t.textDim);
    for (usize s = 0; s < counts.size(); ++s) {
        if (!counts[s]) continue;
        const auto& section = kSections[s];
        Rect header{x, y, rowW, headerH};
        ui.Panel(header, t.surface3);
        if (searching) {
            ui.Heading(header, std::string(kCategories[section.category]) + " / " + section.name, t.text);
        } else if (header.y >= list.y && header.Bottom() <= list.Bottom()) {
            u64 id = Context::ID(section.name) ^ Context::ID("preferences.section");
            ui.CollapsingHeader(id, header, section.name, m_expanded[s]);
            if (KeyboardActivate(ui, id, header)) m_expanded[s] = !m_expanded[s];
        } else {
            ui.Heading(header, section.name, t.text);
        }
        y += headerH;
        if (searching || m_expanded[s]) {
            for (const auto& row : m_rows) {
                if (static_cast<usize>(row.section) != s || !matches(row)) continue;
                Rect line{x, y, rowW, rowH};
                y += rowH;
                if (line.Bottom() <= list.y || line.y >= list.Bottom()) continue;
                ui.Panel(line, t.surface1);
                const f32 labelWidth = rowW * (compact ? 0.56f : 0.43f);
                Rect field = ui.PropertyRow(line, row.label, labelWidth);
                ui.Panel({field.x - 7, line.y, 1, line.h}, t.propertyDivider);
                if (field.y >= list.y && field.Bottom() <= list.Bottom()) DrawControl(ui, row, field);
            }
        }
        y += gap;
    }
    ui.PopClip();
    return true;
}
}  // namespace Luma::Editor::Panels
