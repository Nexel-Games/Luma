#include "Luma/Editor/Panels/Console.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

#include "Luma/Slate/Icons.h"

namespace Luma::Editor::Panels {

using Slate::Align;
using Slate::Color;
using Slate::Icon;
using Slate::Rect;

namespace {

std::string Lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool ContainsCI(std::string_view hay, const std::string& needleLower) {
    if (needleLower.empty()) return true;
    if (hay.size() < needleLower.size()) return false;
    auto it = std::search(hay.begin(), hay.end(), needleLower.begin(), needleLower.end(),
                          [](char a, char b) {
                              return std::tolower(static_cast<unsigned char>(a)) == b;
                          });
    return it != hay.end();
}

// "HH:MM:SS.mmm" in local time.
std::string FormatTime(std::chrono::system_clock::time_point tp) {
    using namespace std::chrono;
    const std::time_t secs = system_clock::to_time_t(tp);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &secs);
#else
    localtime_r(&secs, &local);
#endif
    const auto ms = duration_cast<milliseconds>(tp.time_since_epoch()).count() % 1000;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min,
                  local.tm_sec, static_cast<int>(ms));
    return buf;
}

bool IsError(LogLevel l) { return l == LogLevel::Error || l == LogLevel::Fatal; }

}  // namespace

bool ConsolePanel::Passes(const LogEntry& e, const Filter& f) {
    if (!e.userFacing && !f.engineLogs) return false;
    switch (e.level) {
        case LogLevel::Fatal:
        case LogLevel::Error:
            if (!f.errors) return false;
            break;
        case LogLevel::Warning:
            if (!f.warnings) return false;
            break;
        case LogLevel::Info:
            if (!f.messages) return false;
            break;
        default:  // Debug / Trace
            if (!f.verbose) return false;
            break;
    }
    if (f.hiddenCategories.count(e.category)) return false;
    if (!f.text.empty()) {
        const std::string needle = Lower(f.text);
        if (!ContainsCI(e.category, needle) && !ContainsCI(e.message, needle)) return false;
    }
    return true;
}

void ConsolePanel::Pull() {
    if (!m_source) return;
    std::vector<LogEntry> fresh;
    m_lastSequence = m_source->Fetch(m_lastSequence, fresh);
    for (LogEntry& e : fresh) {
        m_categories.insert(e.category);
        if (e.userFacing) m_userCategories.insert(e.category);
        m_entries.push_back(std::move(e));
    }
    while (m_entries.size() > m_source->Capacity()) m_entries.pop_front();
}

std::optional<LogEntry> ConsolePanel::LatestStatusEntry() {
    Pull();
    const LogEntry* latest = nullptr;
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        if (!Passes(*it, m_filter)) continue;
        if (IsError(it->level)) return *it;
        if (!latest) latest = &*it;
    }
    if (latest) return *latest;
    return std::nullopt;
}

std::vector<Slate::MenuEntry> ConsolePanel::BuildFilterMenu() {
    Slate::MenuBuilder b;
    auto toggle = [&](const char* label, bool* flag, Color dot, const char* tip) {
        (void)dot;
        Slate::MenuEntryOptions o;
        o.checked = *flag;
        o.tooltip = tip;
        b.AddEntry(label, Icon::None, [flag] { *flag = !*flag; }, o);
    };
    b.BeginSection("Verbosity");
    toggle("Errors", &m_filter.errors, {}, "Error and fatal messages.");
    toggle("Warnings", &m_filter.warnings, {}, "Warning messages.");
    toggle("Messages", &m_filter.messages, {}, "Informational messages.");
    toggle("Verbose", &m_filter.verbose, {}, "Debug and trace messages.");

    b.BeginSection("Source");
    {
        Slate::MenuEntryOptions o;
        o.checked = m_filter.engineLogs;
        o.tooltip = "Also show engine diagnostics (renderer, assets, platform). "
                    "Off: only messages about your project.";
        bool* flag = &m_filter.engineLogs;
        b.AddEntry("Show Engine Logs", Icon::None, [flag] { *flag = !*flag; }, o);
    }

    b.BeginSection("Categories");
    {
        Slate::MenuEntryOptions o;
        o.enabled = !m_filter.hiddenCategories.empty();
        b.AddEntry("Show All Categories", Icon::Eye,
                   [this] { m_filter.hiddenCategories.clear(); }, o);
    }
    for (const std::string& cat :
         m_filter.engineLogs ? m_categories : m_userCategories) {
        Slate::MenuEntryOptions o;
        o.checked = m_filter.hiddenCategories.count(cat) == 0;
        b.AddEntry(cat, Icon::None,
                   [this, cat] {
                       if (!m_filter.hiddenCategories.erase(cat)) {
                           m_filter.hiddenCategories.insert(cat);
                       }
                   },
                   o);
    }
    return b.Build();
}

std::vector<Slate::MenuEntry> ConsolePanel::BuildRowMenu(const std::string& category) {
    Slate::MenuBuilder b;
    b.BeginSection("Category");
    b.AddEntry("Show Only \"" + category + "\"", Icon::Eye, [this, category] {
        m_filter.hiddenCategories.clear();
        for (const std::string& c : m_categories) {
            if (c != category) m_filter.hiddenCategories.insert(c);
        }
    });
    b.AddEntry("Hide \"" + category + "\"", Icon::EyeOff,
               [this, category] { m_filter.hiddenCategories.insert(category); });
    {
        Slate::MenuEntryOptions o;
        o.enabled = !m_filter.hiddenCategories.empty();
        b.AddEntry("Show All Categories", Icon::None,
                   [this] { m_filter.hiddenCategories.clear(); }, o);
    }
    b.BeginSection("Log");
    b.AddEntry("Clear Log", Icon::Trash, [this] {
        m_entries.clear();
        m_scroll = 0.0f;
        m_followTail = true;
    });
    return b.Build();
}

void ConsolePanel::Draw(Slate::Context& ui, const Slate::Rect& body,
                        PanelContext& /*ctx*/) {
    Slate::Theme& t = ui.theme();
    Pull();

    // --- Toolbar: [search][Filters]            [errors][warnings][Clear] ---
    constexpr f32 kBarH = 32.0f;
    const Rect bar{body.x, body.y, body.w, kBarH};
    ui.Panel(bar, t.surface2);
    ui.Panel({bar.x, bar.Bottom() - 1.0f, bar.w, 1.0f}, t.separator);
    const f32 cy = bar.y + 4.0f;
    const f32 searchW = std::clamp(body.w * 0.35f, 140.0f, 320.0f);
    ui.SearchBox(Slate::Context::ID("console.search"), {bar.x + 6.0f, cy, searchW, 24.0f},
                 m_filter.text, "Search log");
    const u64 filterId = Slate::Context::ID("console.filters");
    const Rect filterR{bar.x + 12.0f + searchW, cy, 86.0f, 24.0f};
    if (ui.IconTextButton(filterId, filterR, Icon::ChevronDown, "Filters", t.textDim,
                          ui.IsMenuOpen(filterId))) {
        Slate::MenuOptions mo;
        mo.minWidth = 220.0f;
        ui.OpenMenuBelow(filterId, filterR, BuildFilterMenu(), mo);
    }
    ui.Tooltip(filterId ^ 0x71Full, filterR,
               "Filters\nChoose which severities and categories are shown, or include "
               "engine logs.");

    const Rect clearR{bar.Right() - 80.0f, cy, 74.0f, 24.0f};
    ui.Tooltip(Slate::Context::ID("tip.console.clear"), clearR,
               "Clear\nRemove all messages from the log.");
    if (ui.IconTextButton(Slate::Context::ID("console.clear"), clearR, Icon::Trash, "Clear",
                          t.textDim)) {
        m_entries.clear();
        m_scroll = 0.0f;
        m_followTail = true;
    }
    // Error / warning totals for what the current source scope shows (user
    // messages, plus engine logs when enabled), regardless of the severity
    // toggles, so a hidden error is still announced.
    usize errorCount = 0;
    usize warningCount = 0;
    for (const LogEntry& e : m_entries) {
        if (!e.userFacing && !m_filter.engineLogs) continue;
        if (IsError(e.level)) ++errorCount;
        else if (e.level == LogLevel::Warning) ++warningCount;
    }
    // Count badges (dimmed at zero), right-aligned before Clear.
    const Slate::Font& small = ui.mediumFont();
    auto badge = [&](f32 right, usize count, Color color, Icon icon, TextureHandle tex) {
        const std::string text = std::to_string(count);
        const f32 tw = small.Measure(text).x;
        const f32 w = 16.0f + 6.0f + tw + 12.0f;
        const Rect r{right - w, cy, w, 24.0f};
        const bool any = count > 0;
        ui.drawList().AddRectFilledRounded(r, any ? color.WithAlpha(38) : t.surface1,
                                           t.radius.pill);
        if (tex) {
            ui.Image(tex, {r.x + 6.0f, r.y + 4.0f, 16.0f, 16.0f},
                     any ? Color::RGB(255, 255, 255) : t.textDisabled);
        } else {
            Slate::DrawIcon(ui, {r.x + 6.0f, r.y + 4.0f, 16.0f, 16.0f}, icon,
                            any ? color : t.textDisabled);
        }
        ui.drawList().AddText(small, {r.x + 26.0f, r.y + (r.h - small.LineHeight()) * 0.5f},
                              text, any ? color : t.textDisabled);
        ui.Tooltip(Slate::Context::ID(text) ^ static_cast<u64>(color.r) ^ 0x71Cull, r,
                   color.r == t.error.r && color.g == t.error.g
                       ? "Errors\nNumber of errors in the log."
                       : "Warnings\nNumber of warnings in the log.");
        return r.x - 6.0f;
    };
    f32 right = clearR.x - 8.0f;
    right = badge(right, warningCount, t.warning, Icon::Dot, m_iconWarning);
    badge(right, errorCount, t.error, Icon::Close, m_iconError);

    // --- Visible rows ------------------------------------------------------
    const Rect list{body.x, bar.Bottom(), body.w, body.Bottom() - bar.Bottom()};
    ui.Panel(list, t.recessed);
    std::vector<const LogEntry*> rows;
    rows.reserve(m_entries.size());
    for (const LogEntry& e : m_entries) {
        if (Passes(e, m_filter)) rows.push_back(&e);
    }

    const Slate::Font& mono = ui.monoFont();
    const f32 rowH = std::floor(mono.LineHeight() + 5.0f);
    constexpr f32 kPadY = 4.0f;
    const f32 contentH = static_cast<f32>(rows.size()) * rowH + kPadY * 2.0f;
    const f32 maxScroll = std::max(0.0f, contentH - list.h);
    if (m_followTail) m_scroll = maxScroll;
    m_scroll = std::clamp(m_scroll, 0.0f, maxScroll);

    if (rows.empty()) {
        const char* hint = !m_source            ? "Log output is not connected."
                           : m_entries.empty()  ? "No messages."
                                                : "No messages match the current filters.";
        ui.LabelIn({list.x, list.y + 10.0f, list.w, 22.0f}, hint, t.textDim, Align::Center);
    } else {
        ui.PushClip(list);
        const usize first = static_cast<usize>(std::max(0.0f, (m_scroll - kPadY) / rowH));
        const usize last = std::min(rows.size(),
                                    first + static_cast<usize>(list.h / rowH) + 2);
        const f32 timeW = mono.Measure("00:00:00.000").x;
        for (usize i = first; i < last; ++i) {
            const LogEntry& e = *rows[i];
            const Rect r{list.x, list.y + kPadY + static_cast<f32>(i) * rowH - m_scroll,
                         list.w, rowH};
            const bool hovered = r.Contains(ui.mouse()) && list.Contains(ui.mouse());
            Color text = t.text;
            if (IsError(e.level)) {
                ui.Panel(r, t.error.WithAlpha(26));
                text = t.error;
            } else if (e.level == LogLevel::Warning) {
                ui.Panel(r, t.warning.WithAlpha(18));
                text = t.warning;
            } else if (e.level == LogLevel::Debug || e.level == LogLevel::Trace) {
                text = t.textDim;
            }
            if (hovered) ui.Panel(r, t.menu.hoverFill.WithAlpha(70));
            const f32 ty = r.y + (r.h - mono.LineHeight()) * 0.5f;
            f32 x = r.x + 10.0f;
            ui.drawList().AddText(mono, {x, ty}, FormatTime(e.time), t.textDisabled);
            x += timeW + 12.0f;
            const std::string cat = e.category + ":";
            ui.drawList().AddText(mono, {x, ty}, cat, t.textDim);
            x += mono.Measure(cat).x + 8.0f;
            ui.drawList().AddText(mono, {x, ty}, e.message, text);
            if (hovered && ui.mousePressed(1)) {
                ui.OpenMenu(Slate::Context::ID("console.rowmenu"), ui.mouse(),
                            BuildRowMenu(e.category));
            }
        }
        ui.PopClip();
    }

    const f32 before = m_scroll;
    m_scroll = ui.VerticalScroll(Slate::Context::ID("console.scroll"), list, contentH,
                                 m_scroll);
    // Follow the tail while the view sits at the bottom; scrolling up pins it.
    // Only user scrolling (wheel / drag) changes the mode — layout passes
    // where the list is collapsed or still resizing must not drop it.
    if (list.h > rowH * 2.0f && std::fabs(m_scroll - before) > 0.5f) {
        m_followTail = m_scroll >= maxScroll - 1.0f;
    }
}

}  // namespace Luma::Editor::Panels
