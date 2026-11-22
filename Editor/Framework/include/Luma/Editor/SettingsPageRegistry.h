#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Registration.h"
#include "Luma/Core/Types.h"
#include "Luma/Serialization/SerialValue.h"
#include "Luma/Slate/Context.h"

// Project Settings pages as a registry. Each page owns one section of a JSON
// config file under the project's Config/ folder (`configFile`, section key =
// page id): the Project Settings window loads every page from disk when it opens
// a project, shows the pages grouped by category, and writes the sections of
// changed pages back (see ProjectSettingsStore). Plugins add their pages here.
//
// Main-thread only.

namespace Luma::Editor {

struct SettingsPageDesc {
    std::string id;          // stable ("Project.General"); the section key in the file
    std::string category;    // left-column group ("Project", "Engine", "Plugins")
    std::string title;       // page title
    std::string tooltip;
    std::string owner;       // "Engine" or the plugin id
    std::string configFile;  // file name under <Project>/Config ("Game.json")

    // Applies the persisted section (Null when the page has never been saved).
    std::function<void(const SerialValue& section)> load;
    // The page's current values as its section.
    std::function<SerialValue()> save;
    // Draws the page body; returns true when the user changed a value.
    std::function<bool(Slate::Context&, const Slate::Rect&)> draw;
    f32 contentHeight = 0;  // scrollable page height in UI points
};

class SettingsPageRegistry {
public:
    SettingsPageRegistry();
    SettingsPageRegistry(const SettingsPageRegistry&) = delete;
    SettingsPageRegistry& operator=(const SettingsPageRegistry&) = delete;

    RegistrationToken Register(SettingsPageDesc page, std::string* error = nullptr);
    const SettingsPageDesc* Find(std::string_view id) const;
    std::vector<const SettingsPageDesc*> All() const;  // registration order
    // Categories in first-registration order, and the pages of one category.
    std::vector<std::string> Categories() const;
    std::vector<const SettingsPageDesc*> PagesIn(std::string_view category) const;
    u64 Revision() const { return m_revision; }

private:
    struct Entry {
        u64 handle = 0;
        std::unique_ptr<SettingsPageDesc> desc;
    };
    std::vector<Entry> m_entries;
    std::shared_ptr<void> m_alive;
    u64 m_nextHandle = 1;
    u64 m_revision = 0;
};

// The process-wide Project Settings page registry.
SettingsPageRegistry& SettingsPages();

// True for a plain file name usable under Config/ ("Game.json"): no directories,
// no "..", ends in ".json".
bool IsValidConfigFileName(std::string_view name);

}  // namespace Luma::Editor
