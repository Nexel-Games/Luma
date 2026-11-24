#include "Luma/Plugins/PluginDescriptor.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <utility>

#include "Luma/Core/Log.h"
#include "Luma/Core/Version.h"
#include "Luma/Project/Project.h"
#include "Luma/Serialization/Json.h"

namespace Luma::Plugins {
namespace {

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && IsSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && IsSpace(s.back())) s.remove_suffix(1);
    return s;
}

std::string Trimmed(std::string_view s) { return std::string(Trim(s)); }

class Reader {
public:
    Reader(const std::filesystem::path& file, std::vector<ManifestError>& errors)
        : m_file(file), m_errors(errors) {}

    bool Failed() const { return m_failed; }
    void Error(std::string field, std::string message) {
        m_failed = true;
        m_errors.push_back(ManifestError{m_file, std::move(field), std::move(message)});
    }

private:
    const std::filesystem::path& m_file;
    std::vector<ManifestError>& m_errors;
    bool m_failed = false;
};

constexpr const char* kV1Fields[] = {
    "FileVersion",
    "Version",
    "VersionName",
    "FriendlyName",
    "Description",
    "Category",
    "CreatedBy",
    "CreatedByURL",
    "DocsURL",
    "SupportURL",
    "EngineVersion",
    "EnabledByDefault",
    "CanContainContent",
    "IsBetaVersion",
    "IsExperimentalVersion",
    "Installed",
    "SupportedTargetPlatforms",
    "Modules",
    "Plugins",
};

constexpr const char* kSchema1Fields[] = {
    "schema",       "id",           "displayName", "description",
    "category",     "author",       "url",         "version",
    "engineVersionRange", "kind",   "platforms",   "dependencies",
    "capabilities", "enabledByDefault",
};

constexpr const char* kModuleFields[] = {"Name", "Type", "LoadingPhase", "PlatformAllowList"};

template <usize N>
bool InList(const char* const (&list)[N], std::string_view key) {
    for (const char* field : list) {
        if (key == field) return true;
    }
    return false;
}

std::string RequireString(Reader& reader, const SerialValue& obj, const char* field, bool nonEmpty,
                          bool trim) {
    const SerialValue* value = obj.Find(field);
    if (!value) {
        reader.Error(field, std::string(field) + " is required");
        return {};
    }
    if (!value->IsString()) {
        reader.Error(field, std::string(field) + " must be a string");
        return {};
    }
    std::string text = trim ? Trimmed(value->AsString()) : value->AsString();
    if (nonEmpty && Trim(text).empty()) {
        reader.Error(field, std::string(field) + " must not be empty");
    }
    return text;
}

bool OptionalBool(Reader& reader, const SerialValue& obj, const char* field, bool fallback) {
    const SerialValue* value = obj.Find(field);
    if (!value) return fallback;
    if (!value->IsBool()) {
        reader.Error(field, std::string(field) + " must be a boolean");
        return fallback;
    }
    return value->AsBool();
}

template <usize N>
void CollectExtra(const SerialValue& obj, const char* const (&known)[N], SerialValue& extra) {
    extra = SerialValue::MakeObject();
    for (const auto& member : obj.Members()) {
        if (!InList(known, member.first)) extra[member.first] = member.second;
    }
}

std::vector<std::string> ReadPlatforms(Reader& reader, const SerialValue& obj, const char* field) {
    std::vector<std::string> out;
    const SerialValue* value = obj.Find(field);
    if (!value) {
        reader.Error(field, std::string(field) + " is required");
        return out;
    }
    if (!value->IsArray()) {
        reader.Error(field, std::string(field) + " must be an array of strings");
        return out;
    }
    if (value->Size() == 0) {
        reader.Error(field, std::string(field) + " must list at least one of Windows, Linux, macOS");
        return out;
    }
    for (usize i = 0; i < value->Size(); ++i) {
        const std::string path = std::string(field) + "[" + std::to_string(i) + "]";
        const SerialValue& entry = value->At(i);
        if (!entry.IsString()) {
            reader.Error(path, path + " must be a string");
            continue;
        }
        const std::string name = Trimmed(entry.AsString());
        if (!IsKnownPlatform(name)) {
            reader.Error(path, "unknown platform \"" + name + "\"; expected Windows, Linux, or macOS");
            continue;
        }
        out.push_back(name);
    }
    if (out.empty() && !reader.Failed()) {
        reader.Error(field, std::string(field) + " must list at least one of Windows, Linux, macOS");
    }
    return out;
}

void ParseModules(Reader& reader, const SerialValue& obj, PluginDescriptor& desc) {
    const SerialValue* modules = obj.Find("Modules");
    if (!modules) return;
    if (!modules->IsArray()) {
        reader.Error("Modules", "Modules must be an array");
        return;
    }
    for (usize i = 0; i < modules->Size(); ++i) {
        const std::string prefix = "Modules[" + std::to_string(i) + "]";
        const SerialValue& entry = modules->At(i);
        if (!entry.IsObject()) {
            reader.Error(prefix, prefix + " must be an object");
            continue;
        }
        for (const auto& member : entry.Members()) {
            if (!InList(kModuleFields, member.first)) {
                // Kept on the module object only when we rewrite the whole document
                // from extra; module extras are preserved via DescriptorToValue copy
                // of the original Modules array through extra if needed. Unknown
                // module keys are allowed and stored on extra as ModulesExtras.
            }
        }
        ModuleDescriptor mod;
        const SerialValue* name = entry.Find("Name");
        if (!name) reader.Error(prefix + ".Name", prefix + ".Name is required");
        else if (!name->IsString()) reader.Error(prefix + ".Name", prefix + ".Name must be a string");
        else {
            mod.name = name->AsString();
            if (!IsValidPluginId(mod.name)) {
                reader.Error(prefix + ".Name", prefix + ".Name \"" + mod.name +
                                                   "\" is not a valid module name");
            }
        }
        const SerialValue* type = entry.Find("Type");
        if (!type) reader.Error(prefix + ".Type", prefix + ".Type is required");
        else if (!type->IsString()) reader.Error(prefix + ".Type", prefix + ".Type must be a string");
        else if (!ParseModuleType(Trimmed(type->AsString()), mod.type)) {
            reader.Error(prefix + ".Type",
                         prefix + ".Type \"" + type->AsString() +
                             "\" must be Runtime, Developer, Editor, or UncookedOnly");
        }
        if (const SerialValue* phase = entry.Find("LoadingPhase")) {
            if (!phase->IsString()) {
                reader.Error(prefix + ".LoadingPhase", prefix + ".LoadingPhase must be a string");
            } else if (!ParseLoadingPhase(Trimmed(phase->AsString()), mod.loadingPhase)) {
                reader.Error(prefix + ".LoadingPhase",
                             prefix + ".LoadingPhase \"" + phase->AsString() + "\" is not a known phase");
            }
        }
        if (const SerialValue* allow = entry.Find("PlatformAllowList")) {
            if (!allow->IsArray()) {
                reader.Error(prefix + ".PlatformAllowList",
                             prefix + ".PlatformAllowList must be an array of strings");
            } else {
                for (usize p = 0; p < allow->Size(); ++p) {
                    const SerialValue& plat = allow->At(p);
                    if (!plat.IsString() || !IsKnownPlatform(Trimmed(plat.AsString()))) {
                        reader.Error(prefix + ".PlatformAllowList[" + std::to_string(p) + "]",
                                     "unknown platform on PlatformAllowList");
                    } else {
                        mod.platformAllowList.push_back(Trimmed(plat.AsString()));
                    }
                }
            }
        }
        desc.modules.push_back(std::move(mod));
    }
}

void ParsePluginRefs(Reader& reader, const SerialValue& obj, PluginDescriptor& desc) {
    const SerialValue* plugins = obj.Find("Plugins");
    if (!plugins) return;
    if (!plugins->IsArray()) {
        reader.Error("Plugins", "Plugins must be an array");
        return;
    }
    for (usize i = 0; i < plugins->Size(); ++i) {
        const std::string prefix = "Plugins[" + std::to_string(i) + "]";
        const SerialValue& entry = plugins->At(i);
        if (!entry.IsObject()) {
            reader.Error(prefix, prefix + " must be an object");
            continue;
        }
        PluginReference ref;
        const SerialValue* name = entry.Find("Name");
        if (!name) reader.Error(prefix + ".Name", prefix + ".Name is required");
        else if (!name->IsString()) reader.Error(prefix + ".Name", prefix + ".Name must be a string");
        else {
            ref.name = name->AsString();
            if (!IsValidPluginId(ref.name)) {
                reader.Error(prefix + ".Name", prefix + ".Name \"" + ref.name +
                                                   "\" is not a valid plugin name");
            }
        }
        const SerialValue* ver = entry.Find("Version");
        if (!ver) reader.Error(prefix + ".Version", prefix + ".Version is required");
        else if (!ver->IsString()) {
            reader.Error(prefix + ".Version", prefix + ".Version must be a version range string");
        } else {
            ref.versionRange = Trimmed(ver->AsString());
            std::string rangeError;
            if (!SatisfiesRange(SemVer{0, 0, 0}, ref.versionRange, &rangeError) &&
                !rangeError.empty()) {
                reader.Error(prefix + ".Version",
                             prefix + ".Version \"" + ref.versionRange + "\": " + rangeError);
            }
        }
        if (const SerialValue* enabled = entry.Find("Enabled")) {
            if (!enabled->IsBool()) reader.Error(prefix + ".Enabled", prefix + ".Enabled must be a boolean");
            else ref.enabled = enabled->AsBool();
        }
        if (const SerialValue* optional = entry.Find("Optional")) {
            if (!optional->IsBool()) {
                reader.Error(prefix + ".Optional", prefix + ".Optional must be a boolean");
            } else {
                ref.optional = optional->AsBool();
            }
        }
        desc.plugins.push_back(std::move(ref));
    }
}

std::optional<PluginDescriptor> ParseV1Object(const SerialValue& obj,
                                              const std::filesystem::path& file,
                                              std::vector<ManifestError>& errors) {
    const usize before = errors.size();
    Reader reader(file, errors);
    PluginDescriptor desc;
    desc.descriptorPath = file;
    desc.name = file.stem().string();
    CollectExtra(obj, kV1Fields, desc.extra);

    const SerialValue* fv = obj.Find("FileVersion");
    if (!fv) reader.Error("FileVersion", "FileVersion is required");
    else if (!fv->IsInt()) reader.Error("FileVersion", "FileVersion must be an integer");
    else if (fv->AsInt() != 1) {
        reader.Error("FileVersion", "unsupported FileVersion " + std::to_string(fv->AsInt()) +
                                        "; this engine reads FileVersion 1");
    } else {
        desc.fileVersion = 1;
    }

    const SerialValue* version = obj.Find("Version");
    if (!version) reader.Error("Version", "Version is required");
    else if (!version->IsInt()) reader.Error("Version", "Version must be an integer");
    else if (version->AsInt() < 1) reader.Error("Version", "Version must be >= 1");
    else desc.version = static_cast<i32>(version->AsInt());

    const std::string versionName = RequireString(reader, obj, "VersionName", true, true);
    if (!versionName.empty()) {
        auto parsed = SemVer::Parse(versionName);
        if (!parsed) {
            reader.Error("VersionName",
                         "VersionName \"" + versionName + "\" is not a valid semantic version");
        } else {
            desc.versionName = *parsed;
        }
    }

    desc.friendlyName = RequireString(reader, obj, "FriendlyName", true, true);
    if (const SerialValue* description = obj.Find("Description")) {
        if (!description->IsString()) reader.Error("Description", "Description must be a string");
        else desc.description = Trimmed(description->AsString());
    }
    desc.category = RequireString(reader, obj, "Category", true, true);
    desc.createdBy = RequireString(reader, obj, "CreatedBy", true, true);
    auto optUrl = [&](const char* field, std::string& out) {
        if (const SerialValue* v = obj.Find(field)) {
            if (!v->IsString()) reader.Error(field, std::string(field) + " must be a string");
            else out = Trimmed(v->AsString());
        }
    };
    optUrl("CreatedByURL", desc.createdByUrl);
    optUrl("DocsURL", desc.docsUrl);
    optUrl("SupportURL", desc.supportUrl);

    desc.engineVersionRange = RequireString(reader, obj, "EngineVersion", true, true);
    if (!desc.engineVersionRange.empty()) {
        std::string rangeError;
        SatisfiesRange(SemVer{0, 0, 0}, desc.engineVersionRange, &rangeError);
        if (!rangeError.empty()) {
            reader.Error("EngineVersion", "EngineVersion \"" + desc.engineVersionRange + "\": " +
                                              rangeError);
        }
    }

    desc.enabledByDefault = OptionalBool(reader, obj, "EnabledByDefault", false);
    desc.canContainContent = OptionalBool(reader, obj, "CanContainContent", false);
    desc.isBetaVersion = OptionalBool(reader, obj, "IsBetaVersion", false);
    desc.isExperimentalVersion = OptionalBool(reader, obj, "IsExperimentalVersion", false);
    desc.installed = OptionalBool(reader, obj, "Installed", false);
    if (obj.Find("SupportedTargetPlatforms")) {
        desc.supportedTargetPlatforms = ReadPlatforms(reader, obj, "SupportedTargetPlatforms");
    }
    ParseModules(reader, obj, desc);
    ParsePluginRefs(reader, obj, desc);

    if (errors.size() != before) return std::nullopt;
    return desc;
}

std::optional<PluginDescriptor> MigrateSchema1(const SerialValue& obj,
                                               const std::filesystem::path& file,
                                               std::vector<ManifestError>& errors) {
    auto old = ParseManifest(WriteJson(obj, false), file, errors);
    if (!old) return std::nullopt;
    PluginDescriptor desc;
    desc.fileVersion = 1;
    desc.version = 1;
    desc.versionName = old->version;
    desc.name = old->id;
    desc.friendlyName = old->displayName;
    desc.description = old->description;
    desc.category = old->category;
    desc.createdBy = old->author;
    desc.createdByUrl = old->url;
    desc.engineVersionRange = old->engineVersionRange;
    desc.enabledByDefault = old->enabledByDefault;
    desc.supportedTargetPlatforms = old->platforms;
    desc.descriptorPath = file;
    desc.migratedFromSchema1 = true;
    CollectExtra(obj, kSchema1Fields, desc.extra);

    ModuleDescriptor mod;
    mod.name = old->id;
    if (old->kind == PluginKind::Editor) mod.type = ModuleType::Editor;
    else mod.type = ModuleType::Runtime;
    desc.modules.push_back(std::move(mod));
    if (old->kind == PluginKind::RuntimeAndEditor) {
        ModuleDescriptor editor;
        editor.name = old->id + "Editor";
        editor.type = ModuleType::Editor;
        editor.loadingPhase = LoadingPhase::PostEngineInit;
        desc.modules.push_back(std::move(editor));
    }
    for (const PluginDependency& dep : old->dependencies) {
        PluginReference ref;
        ref.name = dep.id;
        ref.versionRange = dep.versionRange;
        ref.optional = dep.optional;
        ref.enabled = true;
        desc.plugins.push_back(std::move(ref));
    }
    return desc;
}

}  // namespace

const char* ToString(ModuleType type) {
    switch (type) {
        case ModuleType::Runtime: return "Runtime";
        case ModuleType::Developer: return "Developer";
        case ModuleType::Editor: return "Editor";
        case ModuleType::UncookedOnly: return "UncookedOnly";
    }
    return "Runtime";
}

const char* ToString(LoadingPhase phase) {
    switch (phase) {
        case LoadingPhase::EarliestPossible: return "EarliestPossible";
        case LoadingPhase::PostConfigInit: return "PostConfigInit";
        case LoadingPhase::PreDefault: return "PreDefault";
        case LoadingPhase::Default: return "Default";
        case LoadingPhase::PostDefault: return "PostDefault";
        case LoadingPhase::PostEngineInit: return "PostEngineInit";
    }
    return "Default";
}

const char* ToString(PluginTarget target) {
    switch (target) {
        case PluginTarget::Editor: return "Editor";
        case PluginTarget::Game: return "Game";
        case PluginTarget::Program: return "Program";
    }
    return "Editor";
}

const char* ToString(PluginResolveState state) {
    switch (state) {
        case PluginResolveState::Disabled: return "Disabled";
        case PluginResolveState::Invalid: return "Invalid";
        case PluginResolveState::Unavailable: return "Unavailable";
        case PluginResolveState::Active: return "Active";
    }
    return "Disabled";
}

bool ParseModuleType(std::string_view text, ModuleType& out) {
    if (text == "Runtime") {
        out = ModuleType::Runtime;
        return true;
    }
    if (text == "Developer") {
        out = ModuleType::Developer;
        return true;
    }
    if (text == "Editor") {
        out = ModuleType::Editor;
        return true;
    }
    if (text == "UncookedOnly") {
        out = ModuleType::UncookedOnly;
        return true;
    }
    return false;
}

bool ParseLoadingPhase(std::string_view text, LoadingPhase& out) {
    if (text == "EarliestPossible") {
        out = LoadingPhase::EarliestPossible;
        return true;
    }
    if (text == "PostConfigInit") {
        out = LoadingPhase::PostConfigInit;
        return true;
    }
    if (text == "PreDefault") {
        out = LoadingPhase::PreDefault;
        return true;
    }
    if (text == "Default") {
        out = LoadingPhase::Default;
        return true;
    }
    if (text == "PostDefault") {
        out = LoadingPhase::PostDefault;
        return true;
    }
    if (text == "PostEngineInit") {
        out = LoadingPhase::PostEngineInit;
        return true;
    }
    return false;
}

std::optional<PluginDescriptor> ParseDescriptor(std::string_view json,
                                                const std::filesystem::path& file,
                                                std::vector<ManifestError>& errors) {
    std::string jsonError;
    auto parsed = ParseJson(json, &jsonError);
    if (!parsed) {
        errors.push_back(ManifestError{file, "", jsonError.empty() ? "descriptor is not valid JSON"
                                                                   : jsonError});
        return std::nullopt;
    }
    if (!parsed->IsObject()) {
        errors.push_back(ManifestError{file, "", "descriptor must be a JSON object"});
        return std::nullopt;
    }
    if (parsed->Find("FileVersion")) return ParseV1Object(*parsed, file, errors);
    if (parsed->Find("schema")) return MigrateSchema1(*parsed, file, errors);
    errors.push_back(ManifestError{file, "FileVersion", "FileVersion is required"});
    return std::nullopt;
}

SerialValue DescriptorToValue(const PluginDescriptor& descriptor) {
    SerialValue obj = SerialValue::MakeObject();
    obj["FileVersion"] = descriptor.fileVersion;
    obj["Version"] = descriptor.version;
    obj["VersionName"] = descriptor.versionName.ToString();
    obj["FriendlyName"] = descriptor.friendlyName;
    if (!descriptor.description.empty()) obj["Description"] = descriptor.description;
    obj["Category"] = descriptor.category;
    obj["CreatedBy"] = descriptor.createdBy;
    if (!descriptor.createdByUrl.empty()) obj["CreatedByURL"] = descriptor.createdByUrl;
    if (!descriptor.docsUrl.empty()) obj["DocsURL"] = descriptor.docsUrl;
    if (!descriptor.supportUrl.empty()) obj["SupportURL"] = descriptor.supportUrl;
    obj["EngineVersion"] = descriptor.engineVersionRange;
    obj["EnabledByDefault"] = descriptor.enabledByDefault;
    obj["CanContainContent"] = descriptor.canContainContent;
    obj["IsBetaVersion"] = descriptor.isBetaVersion;
    obj["IsExperimentalVersion"] = descriptor.isExperimentalVersion;
    obj["Installed"] = descriptor.installed;
    SerialValue platforms = SerialValue::MakeArray();
    for (const std::string& p : descriptor.supportedTargetPlatforms) platforms.PushBack(p);
    obj["SupportedTargetPlatforms"] = std::move(platforms);
    if (!descriptor.modules.empty()) {
        SerialValue modules = SerialValue::MakeArray();
        for (const ModuleDescriptor& mod : descriptor.modules) {
            SerialValue m = SerialValue::MakeObject();
            m["Name"] = mod.name;
            m["Type"] = ToString(mod.type);
            m["LoadingPhase"] = ToString(mod.loadingPhase);
            if (!mod.platformAllowList.empty()) {
                SerialValue allow = SerialValue::MakeArray();
                for (const std::string& p : mod.platformAllowList) allow.PushBack(p);
                m["PlatformAllowList"] = std::move(allow);
            }
            modules.PushBack(std::move(m));
        }
        obj["Modules"] = std::move(modules);
    }
    if (!descriptor.plugins.empty()) {
        SerialValue plugins = SerialValue::MakeArray();
        for (const PluginReference& ref : descriptor.plugins) {
            SerialValue p = SerialValue::MakeObject();
            p["Name"] = ref.name;
            p["Version"] = ref.versionRange;
            p["Enabled"] = ref.enabled;
            p["Optional"] = ref.optional;
            plugins.PushBack(std::move(p));
        }
        obj["Plugins"] = std::move(plugins);
    }
    if (descriptor.extra.IsObject()) {
        for (const auto& member : descriptor.extra.Members()) {
            if (!obj.Find(member.first)) obj[member.first] = member.second;
        }
    }
    return obj;
}

std::string WriteDescriptor(const PluginDescriptor& descriptor, bool pretty) {
    return WriteJson(DescriptorToValue(descriptor), pretty);
}

bool WriteDescriptorFile(const PluginDescriptor& descriptor, const std::filesystem::path& path,
                         std::string* error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error) *error = "could not write " + path.string();
        return false;
    }
    out << WriteDescriptor(descriptor, true);
    if (!out) {
        if (error) *error = "failed while writing " + path.string();
        return false;
    }
    return true;
}

PluginManifest ToManifest(const PluginDescriptor& descriptor) {
    PluginManifest m;
    m.schema = 1;
    m.id = descriptor.name;
    m.displayName = descriptor.friendlyName;
    m.description = descriptor.description;
    m.category = descriptor.category;
    m.author = descriptor.createdBy;
    m.url = descriptor.createdByUrl;
    m.version = descriptor.versionName;
    m.engineVersionRange = descriptor.engineVersionRange;
    m.platforms = descriptor.supportedTargetPlatforms;
    m.enabledByDefault = descriptor.enabledByDefault;
    m.manifestPath = descriptor.descriptorPath;
    bool anyRuntime = false, anyEditor = false;
    for (const ModuleDescriptor& mod : descriptor.modules) {
        if (mod.type == ModuleType::Editor || mod.type == ModuleType::UncookedOnly) anyEditor = true;
        else anyRuntime = true;
    }
    if (anyEditor && anyRuntime) m.kind = PluginKind::RuntimeAndEditor;
    else if (anyEditor) m.kind = PluginKind::Editor;
    else m.kind = PluginKind::Runtime;
    for (const PluginReference& ref : descriptor.plugins) {
        PluginDependency dep;
        dep.id = ref.name;
        dep.versionRange = ref.versionRange;
        dep.optional = ref.optional || !ref.enabled;
        m.dependencies.push_back(std::move(dep));
    }
    return m;
}

std::vector<ProjectPluginEntry> ReadProjectPluginEntries(const Project* project) {
    std::vector<ProjectPluginEntry> out;
    if (!project) return out;
    if (const SerialValue* plugins = project->Find("Plugins"); plugins && plugins->IsArray()) {
        for (const SerialValue& v : plugins->Elements()) {
            if (!v.IsObject()) continue;
            const SerialValue* name = v.Find("Name");
            if (!name || !name->IsString()) continue;
            ProjectPluginEntry e;
            e.name = name->AsString();
            if (const SerialValue* enabled = v.Find("Enabled"); enabled && enabled->IsBool()) {
                e.enabled = enabled->AsBool();
            }
            out.push_back(std::move(e));
        }
        return out;
    }
    const SerialValue* old = project->Find("plugins");
    if (!old || !old->IsObject()) return out;
    std::set<std::string> disabled;
    if (const SerialValue* d = old->Find("disabled"); d && d->IsArray()) {
        for (const SerialValue& v : d->Elements()) {
            if (v.IsString()) disabled.insert(v.AsString());
        }
    }
    if (const SerialValue* e = old->Find("enabled"); e && e->IsArray()) {
        for (const SerialValue& v : e->Elements()) {
            if (!v.IsString()) continue;
            ProjectPluginEntry entry;
            entry.name = v.AsString();
            entry.enabled = disabled.count(entry.name) == 0;
            out.push_back(std::move(entry));
        }
    }
    for (const std::string& name : disabled) {
        bool listed = false;
        for (const auto& e : out) {
            if (e.name == name) listed = true;
        }
        if (!listed) out.push_back(ProjectPluginEntry{name, false});
    }
    return out;
}

bool ModuleLoadsOn(ModuleType type, PluginTarget target, bool shipping) {
    switch (type) {
        case ModuleType::Runtime: return true;
        case ModuleType::Developer: return !(target == PluginTarget::Game && shipping);
        case ModuleType::Editor: return target == PluginTarget::Editor;
        case ModuleType::UncookedOnly:
            return target == PluginTarget::Editor || target == PluginTarget::Program;
    }
    return false;
}

namespace {

bool Required(const PluginReference& ref) { return ref.enabled && !ref.optional; }

std::vector<std::string> SplitCycle(const std::vector<std::string>& stack,
                                    std::string_view backEdgeTo) {
    auto start = std::find(stack.begin(), stack.end(), backEdgeTo);
    std::vector<std::string> cycle;
    if (start == stack.end()) {
        cycle.push_back(std::string(backEdgeTo));
        return cycle;
    }
    cycle.assign(start, stack.end());
    cycle.emplace_back(backEdgeTo);
    return cycle;
}

std::string FormatCycle(const std::vector<std::string>& cycle) {
    std::ostringstream ss;
    ss << "plugin dependency cycle: ";
    for (usize i = 0; i < cycle.size(); ++i) {
        if (i) ss << " -> ";
        ss << cycle[i];
    }
    ss << ". Break the cycle in the descriptors.";
    return ss.str();
}

}  // namespace

std::vector<ResolvedPlugin> ResolvePlugins(std::span<const PluginDescriptor> valid,
                                           std::span<const PluginDescriptor> invalid,
                                           std::span<const ProjectPluginEntry> project,
                                           const ResolveConfig& config) {
    const std::string platform = config.platform.empty() ? std::string(HostPlatform()) : config.platform;
    std::map<std::string, bool> overrideEnabled;
    for (const ProjectPluginEntry& e : project) overrideEnabled[e.name] = e.enabled;

    std::map<std::string, ResolvedPlugin> byName;
    auto addInvalid = [&](PluginDescriptor d, std::string message) {
        ResolvedPlugin r;
        r.descriptor = std::move(d);
        r.state = PluginResolveState::Invalid;
        r.diagnostics.push_back(std::move(message));
        byName[r.descriptor.name.empty() ? r.descriptor.descriptorPath.string() : r.descriptor.name] =
            std::move(r);
    };
    for (const PluginDescriptor& d : invalid) {
        PluginDescriptor copy = d;
        if (copy.name.empty()) copy.name = copy.descriptorPath.stem().string();
        addInvalid(std::move(copy), copy.name + " descriptor failed validation.");
    }
    for (const PluginDescriptor& d : valid) {
        ResolvedPlugin r;
        r.descriptor = d;
        const auto over = overrideEnabled.find(d.name);
        r.enabledInProject = over != overrideEnabled.end() ? over->second : d.enabledByDefault;
        r.state = r.enabledInProject ? PluginResolveState::Active : PluginResolveState::Disabled;
        byName[d.name] = std::move(r);
    }
    for (const ProjectPluginEntry& e : project) {
        if (byName.count(e.name) == 0) {
            ResolvedPlugin r;
            r.descriptor.name = e.name;
            r.descriptor.friendlyName = e.name;
            r.enabledInProject = e.enabled;
            r.state = PluginResolveState::Unavailable;
            r.diagnostics.push_back(e.name +
                                    " is listed in the project but is not installed. Install it "
                                    "under Plugins/" +
                                    e.name + " or remove it from the project's Plugins list.");
            byName[e.name] = std::move(r);
        }
    }

    auto markUnavailable = [&](ResolvedPlugin& r, std::string message) {
        if (r.state == PluginResolveState::Invalid) return;
        r.state = PluginResolveState::Unavailable;
        r.diagnostics.push_back(std::move(message));
    };

    for (auto& [id, r] : byName) {
        if (r.state != PluginResolveState::Active) continue;
        if (!r.descriptor.engineVersionRange.empty()) {
            std::string rangeError;
            if (!SatisfiesRange(EngineVersion(), r.descriptor.engineVersionRange, &rangeError)) {
                markUnavailable(r, id + " requires engine " + r.descriptor.engineVersionRange +
                                       ", but this engine is " + EngineVersion().ToString() +
                                       ". Update the engine or change EngineVersion.");
                continue;
            }
        }
        bool platformOk = r.descriptor.supportedTargetPlatforms.empty();
        for (const std::string& p : r.descriptor.supportedTargetPlatforms) {
            if (p == platform) platformOk = true;
        }
        if (!platformOk) {
            markUnavailable(r, id + " does not support " + platform +
                                   ". Add this platform to SupportedTargetPlatforms.");
            continue;
        }
        for (const ModuleDescriptor& mod : r.descriptor.modules) {
            if (!ModuleLoadsOn(mod.type, config.target, config.shipping)) continue;
            if (!mod.platformAllowList.empty()) {
                bool ok = false;
                for (const std::string& p : mod.platformAllowList) {
                    if (p == platform) ok = true;
                }
                if (!ok) continue;
            }
            if (config.checkBuiltModules && config.builtModules.count(mod.name) == 0) {
                markUnavailable(r, id + " module \"" + mod.name +
                                       "\" is not built for this binary. Rebuild with "
                                       "LUMA_PLUGIN flags or disable the plugin.");
                break;
            }
        }
    }

    auto find = [&](std::string_view name) -> ResolvedPlugin* {
        auto it = byName.find(std::string(name));
        return it == byName.end() ? nullptr : &it->second;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& [id, r] : byName) {
            if (r.state != PluginResolveState::Active) continue;
            for (const PluginReference& ref : r.descriptor.plugins) {
                if (!Required(ref)) continue;
                ResolvedPlugin* other = find(ref.name);
                if (!other) {
                    markUnavailable(r, id + " requires " + ref.name + " " + ref.versionRange +
                                           ", which is not installed.");
                    changed = true;
                    break;
                }
                std::string rangeError;
                if (!SatisfiesRange(other->descriptor.versionName, ref.versionRange, &rangeError) &&
                    other->state != PluginResolveState::Unavailable &&
                    other->state != PluginResolveState::Invalid) {
                    if (!other->descriptor.versionName.ToString().empty() &&
                        other->descriptor.versionName != SemVer{}) {
                        markUnavailable(r, id + " requires " + ref.name + " " + ref.versionRange +
                                               ", but " + ref.name + " " +
                                               other->descriptor.versionName.ToString() +
                                               " is installed.");
                        changed = true;
                        break;
                    }
                }
                if (!other->enabledInProject || other->state == PluginResolveState::Disabled) {
                    markUnavailable(r, id + " requires " + ref.name +
                                           ", which is disabled. Enable it as well, or disable " +
                                           id + ".");
                    changed = true;
                    break;
                }
                if (other->state == PluginResolveState::Unavailable ||
                    other->state == PluginResolveState::Invalid) {
                    markUnavailable(r, id + " cannot start because " + ref.name + " is unavailable.");
                    changed = true;
                    break;
                }
            }
        }
    }

    std::set<std::string> candidates;
    std::map<std::string, std::vector<std::string>> depsOf;
    for (auto& [id, r] : byName) {
        if (r.state != PluginResolveState::Active) continue;
        candidates.insert(id);
        for (const PluginReference& ref : r.descriptor.plugins) {
            if (Required(ref) && candidates.count(ref.name)) depsOf[id].push_back(ref.name);
        }
    }
    // Rebuild deps against Active only after first pass
    depsOf.clear();
    candidates.clear();
    for (auto& [id, r] : byName) {
        if (r.state != PluginResolveState::Active) continue;
        candidates.insert(id);
    }
    for (auto& [id, r] : byName) {
        if (r.state != PluginResolveState::Active) continue;
        for (const PluginReference& ref : r.descriptor.plugins) {
            if (Required(ref) && candidates.count(ref.name)) depsOf[id].push_back(ref.name);
        }
    }

    std::vector<std::string> cycleMessage;
    std::map<std::string, u8> color;
    std::vector<std::string> stack;
    std::function<void(const std::string&)> dfs = [&](const std::string& id) {
        color[id] = 1;
        stack.push_back(id);
        for (const std::string& dep : depsOf[id]) {
            if (candidates.count(dep) == 0) continue;
            if (color[dep] == 1) {
                cycleMessage = SplitCycle(stack, dep);
                return;
            }
            if (color[dep] == 0) {
                dfs(dep);
                if (!cycleMessage.empty()) return;
            }
        }
        stack.pop_back();
        color[id] = 2;
    };
    for (const std::string& id : candidates) {
        if (color[id] == 0) dfs(id);
        if (!cycleMessage.empty()) break;
    }
    if (!cycleMessage.empty()) {
        const std::string message = FormatCycle(cycleMessage);
        std::set<std::string> inCycle(cycleMessage.begin(), cycleMessage.end());
        for (const std::string& id : inCycle) {
            if (ResolvedPlugin* r = find(id)) {
                if (r->state == PluginResolveState::Active) markUnavailable(*r, message);
            }
        }
    }

    std::vector<ResolvedPlugin> ordered;
    std::vector<ResolvedPlugin> rest;
    std::map<std::string, i32> indegree;
    std::map<std::string, std::vector<std::string>> adj;
    std::set<std::string> ready;
    for (auto& [id, r] : byName) {
        if (r.state == PluginResolveState::Active) indegree.emplace(id, 0);
    }
    for (auto& [id, r] : byName) {
        if (r.state != PluginResolveState::Active) continue;
        for (const PluginReference& ref : r.descriptor.plugins) {
            if (!Required(ref) || indegree.count(ref.name) == 0) continue;
            adj[ref.name].push_back(id);
            indegree[id]++;
        }
    }
    for (auto& [id, deg] : indegree) {
        if (deg == 0) ready.insert(id);
    }
    while (!ready.empty()) {
        const std::string id = *ready.begin();
        ready.erase(ready.begin());
        ordered.push_back(byName[id]);
        for (const std::string& to : adj[id]) {
            if (--indegree[to] == 0) ready.insert(to);
        }
    }
    for (auto& [id, r] : byName) {
        if (r.state == PluginResolveState::Active) {
            bool listed = false;
            for (const auto& o : ordered) {
                if (o.descriptor.name == id) listed = true;
            }
            if (!listed) rest.push_back(r);
        } else {
            rest.push_back(r);
        }
    }
    std::sort(rest.begin(), rest.end(), [](const ResolvedPlugin& a, const ResolvedPlugin& b) {
        return a.descriptor.name < b.descriptor.name;
    });
    ordered.insert(ordered.end(), rest.begin(), rest.end());
    return ordered;
}

}  // namespace Luma::Plugins
