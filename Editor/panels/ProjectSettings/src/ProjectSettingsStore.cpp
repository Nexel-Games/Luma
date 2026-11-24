#include "Luma/Editor/Panels/ProjectSettingsStore.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "Luma/Core/Log.h"
#include "Luma/Serialization/Json.h"

namespace Luma::Editor::Panels {

namespace fs = std::filesystem;

namespace {

bool Contains(const std::vector<std::string>& v, std::string_view s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

SerialValue OverlayObject(const SerialValue& base, const SerialValue& over) {
    if (over.IsNull()) return base;
    if (!over.IsObject() || !base.IsObject()) return over;
    SerialValue out = base;
    for (const auto& [key, value] : over.Members()) {
        if (const SerialValue* existing = out.Find(key);
            existing && existing->IsObject() && value.IsObject()) {
            out[key] = OverlayObject(*existing, value);
        } else {
            out[key] = value;
        }
    }
    return out;
}

bool Fail(std::string* error, std::string message) {
    LUMA_LOG_ERROR("ProjectSettings", "{}", message);
    if (error) *error = std::move(message);
    return false;
}

}  // namespace

ProjectSettingsStore::ProjectSettingsStore(const SettingsPageRegistry& pages, fs::path configDir)
    : m_pages(pages), m_configDir(std::move(configDir)) {}

fs::path ProjectSettingsStore::FileFor(const SettingsPageDesc& page) const {
    return m_configDir / page.configFile;
}

bool ProjectSettingsStore::ReadDocument(const fs::path& file, SerialValue& doc,
                                        std::string* error) const {
    doc = SerialValue::MakeObject();
    std::error_code ec;
    if (!fs::exists(file, ec)) return true;
    std::ifstream in(file, std::ios::binary);
    if (!in) return Fail(error, "could not read " + file.string());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string parseError;
    auto parsed = ParseJson(text, &parseError);
    if (!parsed || !parsed->IsObject()) {
        return Fail(error, file.string() + " is not a valid settings file (" +
                               (parsed ? std::string("not a JSON object") : parseError) + ")");
    }
    if (const SerialValue* v = parsed->Find("fileVersion"); v && v->AsInt() > kFileVersion) {
        return Fail(error, file.string() + " was written by a newer editor (fileVersion " +
                               std::to_string(v->AsInt()) + ")");
    }
    doc = std::move(*parsed);
    return true;
}

void ProjectSettingsStore::SetPluginDefaultConfig(std::string pluginName, SerialValue document) {
    m_pluginDefaults[std::move(pluginName)] = std::move(document);
}

void ProjectSettingsStore::ClearPluginDefaultConfigs() { m_pluginDefaults.clear(); }

void ProjectSettingsStore::LoadPage(const SettingsPageDesc& page,
                                    std::vector<std::string>* errors) {
    SerialValue layered;
    bool haveDefault = false;
    if (auto it = m_pluginDefaults.find(page.owner); it != m_pluginDefaults.end()) {
        if (const SerialValue* section = it->second.Find(page.id)) {
            layered = *section;
            haveDefault = true;
        }
    }
    const fs::path file = FileFor(page);
    SerialValue doc;
    std::string error;
    if (!ReadDocument(file, doc, &error)) {
        if (!Contains(m_blockedFiles, file.string())) {
            m_blockedFiles.push_back(file.string());
            if (errors) errors->push_back(error);
        }
        page.load(haveDefault ? layered : SerialValue{});
    } else {
        std::erase(m_blockedFiles, file.string());
        const SerialValue* section = doc.Find(page.id);
        if (section) {
            page.load(haveDefault ? OverlayObject(layered, *section) : *section);
        } else {
            page.load(haveDefault ? layered : SerialValue{});
        }
    }
    if (!Contains(m_loaded, page.id)) m_loaded.push_back(page.id);
}

bool ProjectSettingsStore::LoadAll(std::vector<std::string>* errors) {
    m_loaded.clear();
    m_dirty.clear();
    m_blockedFiles.clear();
    std::vector<std::string> local;
    for (const SettingsPageDesc* page : m_pages.All()) LoadPage(*page, &local);
    for (const std::string& e : local) {
        LUMA_USER_ERROR("ProjectSettings", "{}", e);
        if (errors) errors->push_back(e);
    }
    return local.empty();
}

void ProjectSettingsStore::LoadNewPages(std::vector<std::string>* errors) {
    std::vector<std::string> local;
    for (const SettingsPageDesc* page : m_pages.All()) {
        if (!Contains(m_loaded, page->id)) LoadPage(*page, &local);
    }
    for (const std::string& e : local) {
        LUMA_USER_ERROR("ProjectSettings", "{}", e);
        if (errors) errors->push_back(e);
    }
}

void ProjectSettingsStore::MarkDirty(std::string_view pageId) {
    if (!Contains(m_dirty, pageId)) m_dirty.emplace_back(pageId);
}

bool ProjectSettingsStore::IsDirty(std::string_view pageId) const {
    return Contains(m_dirty, pageId);
}

bool ProjectSettingsStore::Save(std::string_view pageId, std::string* error) {
    const SettingsPageDesc* page = m_pages.Find(pageId);
    if (!page) return Fail(error, "no settings page '" + std::string(pageId) + "'");
    const fs::path file = FileFor(*page);
    if (Contains(m_blockedFiles, file.string())) {
        return Fail(error, file.string() +
                               " could not be read, so it is not overwritten; fix or remove it, "
                               "then reopen the project");
    }
    SerialValue doc;
    if (!ReadDocument(file, doc, error)) {
        m_blockedFiles.push_back(file.string());
        return false;
    }
    std::error_code ec;
    if (fs::exists(file, ec) &&
        (fs::status(file, ec).permissions() & fs::perms::owner_write) == fs::perms::none) {
        return Fail(error, file.string() + " is read-only");
    }
    doc["fileVersion"] = kFileVersion;
    doc[page->id] = page->save();

    fs::create_directories(m_configDir, ec);
    if (ec) return Fail(error, "could not create " + m_configDir.string() + ": " + ec.message());
    const fs::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out || !(out << WriteJson(doc, true))) {
            return Fail(error, "could not write " + temp.string());
        }
    }
    fs::rename(temp, file, ec);
    if (ec) {
        fs::remove(temp, ec);
        return Fail(error, "could not replace " + file.string());
    }
    std::erase(m_dirty, std::string(pageId));
    return true;
}

bool ProjectSettingsStore::SaveAll(std::vector<std::string>* errors) {
    bool ok = true;
    const std::vector<std::string> dirty = m_dirty;
    for (const std::string& id : dirty) {
        std::string error;
        if (!Save(id, &error)) {
            ok = false;
            LUMA_USER_ERROR("ProjectSettings", "Could not save '{}': {}", id, error);
            if (errors) errors->push_back(error);
        }
    }
    return ok;
}

}  // namespace Luma::Editor::Panels
