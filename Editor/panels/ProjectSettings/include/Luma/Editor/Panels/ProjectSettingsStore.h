#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Editor/SettingsPageRegistry.h"
#include "Luma/Serialization/SerialValue.h"

// Persistence of the Project Settings pages. Every page owns one section (keyed
// by its id) of a JSON file under the project's Config/ folder:
//
//   Config/Game.json = { "fileVersion": 1, "Project.MapsAndModes": {...}, ... }
//
// Saving reads the file, replaces only the page's section and writes it back
// atomically, so other pages' sections and keys the editor doesn't know survive.
// A file that can't be parsed is reported and never overwritten (the user's data
// stays as it is until they fix or remove it).

namespace Luma::Editor::Panels {

class ProjectSettingsStore {
public:
    static constexpr int kFileVersion = 1;

    ProjectSettingsStore(const SettingsPageRegistry& pages, std::filesystem::path configDir);

    const std::filesystem::path& ConfigDir() const { return m_configDir; }

    // Loads every registered page from disk (pages without a saved section get
    // Null). Returns false when a file could not be read; `errors` lists why.
    bool LoadAll(std::vector<std::string>* errors = nullptr);
    // Loads pages registered since the last load (e.g. by a plugin).
    void LoadNewPages(std::vector<std::string>* errors = nullptr);

    void MarkDirty(std::string_view pageId);
    bool IsDirty(std::string_view pageId) const;
    bool AnyDirty() const { return !m_dirty.empty(); }

    // Writes one page's section. On failure the page stays dirty.
    bool Save(std::string_view pageId, std::string* error = nullptr);
    // Writes every dirty page; false (with `errors`) when any failed.
    bool SaveAll(std::vector<std::string>* errors = nullptr);

    std::filesystem::path FileFor(const SettingsPageDesc& page) const;

    // Plugin `Config/Default<Name>.json` documents, keyed by plugin id
    // (settings page `owner`). Load overlays the project Config/ section
    // on top. Save still writes only the project file.
    void SetPluginDefaultConfig(std::string pluginName, SerialValue document);
    void ClearPluginDefaultConfigs();

private:
    // Parses `file` (an empty document when it doesn't exist). False when it
    // exists but can't be read as a JSON object.
    bool ReadDocument(const std::filesystem::path& file, SerialValue& doc,
                      std::string* error) const;
    void LoadPage(const SettingsPageDesc& page, std::vector<std::string>* errors);

    const SettingsPageRegistry& m_pages;
    std::filesystem::path m_configDir;
    std::map<std::string, SerialValue> m_pluginDefaults;
    std::vector<std::string> m_loaded;      // page ids loaded from disk
    std::vector<std::string> m_dirty;       // page ids with unsaved edits
    std::vector<std::string> m_blockedFiles;  // unparsable files (never overwritten)
};

}  // namespace Luma::Editor::Panels
