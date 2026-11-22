#include "Luma/Project/Project.h"

#include <fstream>
#include <iterator>
#include <system_error>

#include "Luma/Core/Config.h"
#include "Luma/Core/Log.h"
#include "Luma/Core/Version.h"
#include "Luma/Serialization/Json.h"

namespace Luma {
namespace fs = std::filesystem;

const char* ToString(GameTemplate tmpl) {
    switch (tmpl) {
        case GameTemplate::Empty:       return "Empty";
        case GameTemplate::FirstPerson: return "FirstPerson";
        case GameTemplate::ThirdPerson: return "ThirdPerson";
        case GameTemplate::TopDown:     return "TopDown";
    }
    return "Empty";
}

GameTemplate GameTemplateFromString(std::string_view text,
                                    GameTemplate fallback) {
    if (text == "Empty") return GameTemplate::Empty;
    if (text == "FirstPerson") return GameTemplate::FirstPerson;
    if (text == "ThirdPerson") return GameTemplate::ThirdPerson;
    if (text == "TopDown") return GameTemplate::TopDown;
    return fallback;
}

bool IsValidProjectName(std::string_view name) {
    if (name.empty() || name.size() > 128) return false;
    for (char c : name) {
        switch (c) {
            case '/':
            case '\\':
            case ':':
            case '*':
            case '?':
            case '"':
            case '<':
            case '>':
            case '|':
                return false;
            default:
                break;
        }
    }
    // No leading/trailing spaces or dots (Windows-hostile).
    if (name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return false;
    }
    return true;
}

namespace {

std::string CurrentEngineVersionString() {
    return EngineVersion().ToString();
}

void SetError(std::string* out, std::string message) {
    LUMA_LOG_ERROR("Project", "{}", message);
    if (out) *out = std::move(message);
}

// True when `text` looks like a JSON object (first non-whitespace char '{').
bool LooksLikeJson(std::string_view text) {
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        return c == '{';
    }
    return false;
}

constexpr std::string_view kReservedKeys[] = {"fileVersion", "name", "engineVersion",
                                              "template", "startupScene"};

// Reads an optional string field; false (with *error) when present but not a string.
bool ReadString(const SerialValue& doc, std::string_view key, std::string& out,
                const fs::path& file, std::string* error) {
    const SerialValue* v = doc.Find(key);
    if (!v) return true;
    if (!v->IsString()) {
        SetError(error, file.string() + ": field '" + std::string(key) + "' must be a string");
        return false;
    }
    out = v->AsString();
    return true;
}

// Moves a version-1 JSON key to its version-2 name (keeping its position).
void RenameKey(SerialValue& doc, std::string_view from, std::string_view to) {
    const SerialValue* old = doc.Find(from);
    if (!old) return;
    SerialValue rebuilt = SerialValue::MakeObject();
    for (const auto& [key, value] : doc.Members()) {
        if (key == from) {
            if (!doc.Contains(to)) rebuilt[to] = value;
        } else {
            rebuilt[key] = value;
        }
    }
    doc = std::move(rebuilt);
}

}  // namespace

std::optional<Project> Project::Create(const ProjectDesc& desc,
                                       std::string* outError) {
    if (!IsValidProjectName(desc.name)) {
        SetError(outError, "invalid project name: '" + desc.name + "'");
        return std::nullopt;
    }
    if (desc.parentDirectory.empty()) {
        SetError(outError, "no parent directory specified");
        return std::nullopt;
    }

    std::error_code ec;
    fs::path root = desc.parentDirectory / desc.name;
    fs::path lumaFile = root / (desc.name + kExtension);

    if (fs::exists(lumaFile)) {
        SetError(outError, "a project already exists at " + lumaFile.string());
        return std::nullopt;
    }

    for (const char* sub : {"", "Assets", "Assets/Scenes", "Config", "Source",
                           "Intermediate"}) {
        fs::create_directories(root / sub, ec);
        if (ec) {
            SetError(outError, "failed to create " + (root / sub).string() +
                                   ": " + ec.message());
            return std::nullopt;
        }
    }

    // Default startup scene path, stored project-relative with forward slashes.
    const std::string startupScene =
        std::string("Assets/Scenes/Main") + kSceneExtension;

    Project created;
    created.m_projectFile = fs::absolute(lumaFile, ec);
    created.m_name = desc.name;
    created.m_engineVersion = CurrentEngineVersionString();
    created.m_template = desc.gameTemplate;
    created.m_startupScene = startupScene;
    if (!created.Save(outError)) return std::nullopt;

    LUMA_LOG_INFO("Project", "created project '{}' ({}) at {}", desc.name,
                  ToString(desc.gameTemplate), root.string());
    return Load(lumaFile, outError);
}

std::optional<Project> Project::Load(const fs::path& lumaFile,
                                     std::string* outError) {
    std::error_code ec;
    if (!fs::exists(lumaFile, ec)) {
        SetError(outError, "project file not found: " + lumaFile.string());
        return std::nullopt;
    }

    Project project;
    project.m_projectFile = fs::absolute(lumaFile, ec);
    const std::string nameFallback = lumaFile.stem().string();

    std::ifstream in(lumaFile, std::ios::binary);
    if (!in) {
        SetError(outError, "failed to read project file: " + lumaFile.string());
        return std::nullopt;
    }
    std::string contents((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

    if (!LooksLikeJson(contents)) {
        // Legacy INI descriptor: converted to a version-2 document in memory.
        Config config;
        if (!config.LoadFromFile(lumaFile.string())) {
            SetError(outError, "failed to read project file: " + lumaFile.string());
            return std::nullopt;
        }
        project.m_name = config.GetString("Project.name", nameFallback);
        project.m_engineVersion = config.GetString("Project.engine_version", "0.0.0");
        project.m_template = GameTemplateFromString(config.GetString("Project.template", "Empty"));
        project.m_startupScene = config.GetString("Project.startup_scene", "");
        LUMA_LOG_INFO("Project", "loaded project '{}' (engine {}, template {}, INI descriptor)",
                      project.m_name, project.m_engineVersion, ToString(project.m_template));
        return project;
    }

    std::string parseError;
    auto doc = ParseJson(contents, &parseError);
    if (!doc || !doc->IsObject()) {
        SetError(outError, lumaFile.string() + ": invalid project file: " +
                               (doc ? std::string("not a JSON object") : parseError));
        return std::nullopt;
    }

    i64 version = 1;  // the first JSON form had no fileVersion
    if (const SerialValue* v = doc->Find("fileVersion")) {
        if (!v->IsInt() || v->AsInt() < 1) {
            SetError(outError, lumaFile.string() + ": field 'fileVersion' must be a positive "
                                                   "integer");
            return std::nullopt;
        }
        version = v->AsInt();
    }
    if (version > kFileVersion) {
        SetError(outError, lumaFile.string() + ": project file version " +
                               std::to_string(version) + " is newer than this engine supports (" +
                               std::to_string(kFileVersion) + "); update the engine to open it");
        return std::nullopt;
    }
    if (version == 1) {
        // 1 -> 2: projectName -> name, defaultScene -> startupScene.
        RenameKey(*doc, "projectName", "name");
        RenameKey(*doc, "defaultScene", "startupScene");
    }

    project.m_name = nameFallback;
    std::string templateName = ToString(GameTemplate::Empty);
    if (!ReadString(*doc, "name", project.m_name, lumaFile, outError) ||
        !ReadString(*doc, "engineVersion", project.m_engineVersion, lumaFile, outError) ||
        !ReadString(*doc, "template", templateName, lumaFile, outError) ||
        !ReadString(*doc, "startupScene", project.m_startupScene, lumaFile, outError)) {
        return std::nullopt;
    }
    if (project.m_name.empty()) project.m_name = nameFallback;
    project.m_template = GameTemplateFromString(templateName);
    project.m_document = std::move(*doc);
    LUMA_LOG_INFO("Project", "loaded project '{}' (engine {}, template {})", project.m_name,
                  project.m_engineVersion, ToString(project.m_template));
    return project;
}

bool Project::IsReservedKey(std::string_view key) {
    for (std::string_view k : kReservedKeys) {
        if (k == key) return true;
    }
    return false;
}

const SerialValue* Project::Find(std::string_view key) const { return m_document.Find(key); }

bool Project::Set(std::string_view key, SerialValue value, std::string* error) {
    if (key.empty()) {
        SetError(error, "project section rejected: the key is empty");
        return false;
    }
    if (IsReservedKey(key)) {
        SetError(error, "project section '" + std::string(key) +
                            "' is reserved; use the Project API to change it");
        return false;
    }
    m_document[key] = std::move(value);
    return true;
}

void Project::SetStartupScene(std::string path) { m_startupScene = std::move(path); }

SerialValue Project::Document() const {
    // Reserved keys first (in a fixed order), then every other key as loaded.
    SerialValue doc = SerialValue::MakeObject();
    doc["fileVersion"] = kFileVersion;
    doc["name"] = m_name;
    doc["engineVersion"] = m_engineVersion;
    doc["template"] = std::string(ToString(m_template));
    doc["startupScene"] = m_startupScene;
    // Keep the loaded order of the reserved keys when the file had them all.
    bool hasAll = true;
    for (std::string_view k : kReservedKeys) hasAll = hasAll && m_document.Contains(k);
    if (hasAll) {
        SerialValue ordered = SerialValue::MakeObject();
        for (const auto& [key, value] : m_document.Members()) {
            ordered[key] = IsReservedKey(key) ? *doc.Find(key) : value;
        }
        return ordered;
    }
    for (const auto& [key, value] : m_document.Members()) {
        if (!IsReservedKey(key)) doc[key] = value;
    }
    return doc;
}

bool Project::IsWritable() const {
    std::error_code ec;
    const fs::file_status st = fs::status(m_projectFile, ec);
    if (ec || !fs::exists(st)) return false;
    return (st.permissions() & fs::perms::owner_write) != fs::perms::none;
}

bool Project::Save(std::string* outError) const {
    std::error_code ec;
    if (fs::exists(m_projectFile, ec) && !IsWritable()) {
        SetError(outError, "project file is read-only: " + m_projectFile.string());
        return false;
    }
    const fs::path temp = m_projectFile.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out || !(out << WriteJson(Document(), true))) {
            SetError(outError, "failed to write " + temp.string());
            return false;
        }
    }
    fs::rename(temp, m_projectFile, ec);
    if (ec) {
        fs::remove(temp, ec);
        SetError(outError, "failed to replace " + m_projectFile.string());
        return false;
    }
    return true;
}

std::vector<fs::path> DiscoverProjects(const fs::path& searchRoot) {
    std::vector<fs::path> found;
    std::error_code ec;
    if (!fs::is_directory(searchRoot, ec)) return found;

    for (const auto& entry : fs::directory_iterator(searchRoot, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        for (const auto& child :
             fs::directory_iterator(entry.path(), ec)) {
            if (child.is_regular_file() &&
                child.path().extension() == Project::kExtension) {
                found.push_back(child.path());
                break;
            }
        }
    }
    return found;
}

}  // namespace Luma
