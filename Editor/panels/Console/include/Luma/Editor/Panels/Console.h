#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Luma/Core/LogBuffer.h"
#include "PanelContext.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Types.h"

// Console panel — the editor's Output Log (Unreal-style). By default it shows
// only user-facing messages (LUMA_USER_*: project, scene and asset activity
// and failures); engine diagnostics appear when "Show Engine Logs" is on.
// Reads log records from a Luma::LogBuffer sink (pulled incrementally by sequence number) and
// shows them as severity-colored rows with timestamp + category, with:
//   * a text search over category + message,
//   * a Filters menu (severity toggles + per-category visibility),
//   * error / warning count badges,
//   * Clear, and follow-tail scrolling (sticks to the newest line while the
//     view is scrolled to the bottom).
// Right-clicking a row offers "show only / hide" for its category.

namespace Luma::Editor::Panels {

class ConsolePanel {
public:
    // The log history to display (shared with the logger). Null = the panel
    // shows a hint instead of entries.
    void SetSource(std::shared_ptr<LogBuffer> source) { m_source = std::move(source); }
    // Icon-pack textures for the error / warning count badges (0 = glyphs).
    void SetBadgeIcons(TextureHandle error, TextureHandle warning) {
        m_iconError = error;
        m_iconWarning = warning;
    }

    void Draw(Slate::Context& ui, const Slate::Rect& body, PanelContext& ctx);

    // Newest visible error takes priority; otherwise return the newest visible log.
    std::optional<LogEntry> LatestStatusEntry();

    // Severity buckets the Filters menu toggles (Unreal: Errors / Warnings /
    // Messages / Verbose).
    struct Filter {
        bool errors = true;     // Error + Fatal
        bool warnings = true;   // Warning
        bool messages = true;   // Info
        bool verbose = false;   // Debug + Trace (off by default, like UE)
        // Engine diagnostics (renderer init, asset loading, ...). Off by
        // default: the Output Log is about the user's project.
        bool engineLogs = false;
        std::set<std::string> hiddenCategories;
        std::string text;       // case-insensitive, matches category or message
    };
    // Pure filter predicate (exposed for tests).
    static bool Passes(const LogEntry& e, const Filter& f);

private:
    void Pull();
    std::vector<Slate::MenuEntry> BuildFilterMenu();
    std::vector<Slate::MenuEntry> BuildRowMenu(const std::string& category);

    std::shared_ptr<LogBuffer> m_source;
    std::deque<LogEntry> m_entries;   // local copy (capped at source capacity)
    u64 m_lastSequence = 0;
    std::set<std::string> m_categories;      // every category seen (sorted)
    std::set<std::string> m_userCategories;  // categories of user-facing entries
    Filter m_filter;
    f32 m_scroll = 0.0f;
    bool m_followTail = true;
    TextureHandle m_iconError = 0;
    TextureHandle m_iconWarning = 0;
};

}  // namespace Luma::Editor::Panels
