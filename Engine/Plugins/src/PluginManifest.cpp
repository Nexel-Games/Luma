#include "Luma/Plugins/PluginManifest.h"

#include <limits>
#include <utility>
#include <vector>

#include "Luma/Serialization/Json.h"

namespace Luma::Plugins {

namespace {

constexpr const char* kFields[] = {
    "schema",       "id",           "displayName", "description",
    "category",     "author",       "url",         "version",
    "engineVersionRange", "kind",   "platforms",   "dependencies",
    "capabilities", "enabledByDefault",
};

constexpr const char* kDependencyFields[] = {"id", "version", "optional"};

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && IsSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && IsSpace(s.back())) s.remove_suffix(1);
    return s;
}

std::string Trimmed(std::string_view s) { return std::string(Trim(s)); }

enum class CmpKind { Eq, Gt, Gte, Lt, Lte, Caret, Tilde };

struct Comparator {
    CmpKind kind = CmpKind::Eq;
    SemVer version;
    SemVer upper;
};

bool SameTuple(const SemVer& a, const SemVer& b) {
    return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
}

std::optional<SemVer> ExclusiveUpper(const SemVer& version, CmpKind kind,
                                     std::string* error) {
    constexpr u32 kMax = std::numeric_limits<u32>::max();
    SemVer upper;
    if (kind == CmpKind::Tilde) {
        if (version.minor == kMax) {
            if (error) *error = "tilde range overflows the minor version";
            return std::nullopt;
        }
        upper.major = version.major;
        upper.minor = version.minor + 1;
        return upper;
    }
    if (version.major > 0) {
        if (version.major == kMax) {
            if (error) *error = "caret range overflows the major version";
            return std::nullopt;
        }
        upper.major = version.major + 1;
        return upper;
    }
    if (version.minor > 0) {
        if (version.minor == kMax) {
            if (error) *error = "caret range overflows the minor version";
            return std::nullopt;
        }
        upper.minor = version.minor + 1;
        return upper;
    }
    if (version.patch == kMax) {
        if (error) *error = "caret range overflows the patch version";
        return std::nullopt;
    }
    upper.patch = version.patch + 1;
    return upper;
}

bool Matches(const SemVer& version, const Comparator& cmp) {
    switch (cmp.kind) {
        case CmpKind::Eq: return version == cmp.version;
        case CmpKind::Gt: return version > cmp.version;
        case CmpKind::Gte: return version >= cmp.version;
        case CmpKind::Lt: return version < cmp.version;
        case CmpKind::Lte: return version <= cmp.version;
        case CmpKind::Caret:
        case CmpKind::Tilde:
            return version >= cmp.version && version < cmp.upper;
    }
    return false;
}

struct OpSpec {
    std::string_view text;
    CmpKind kind;
};

constexpr OpSpec kOps[] = {
    {">=", CmpKind::Gte}, {"<=", CmpKind::Lte}, {"==", CmpKind::Eq},
    {">", CmpKind::Gt},   {"<", CmpKind::Lt},   {"=", CmpKind::Eq},
    {"^", CmpKind::Caret}, {"~", CmpKind::Tilde},
};

std::vector<std::string> Tokens(std::string_view text) {
    std::vector<std::string> tokens;
    std::string current;
    auto flush = [&] {
        if (!current.empty()) {
            tokens.push_back(std::move(current));
            current.clear();
        }
    };
    for (char c : text) {
        if (IsSpace(c)) flush();
        else current.push_back(c);
    }
    flush();
    return tokens;
}

bool ParseComparator(std::string_view token, const std::string* next,
                     bool* consumedNext, Comparator& out, std::string* error) {
    *consumedNext = false;
    std::string_view opText;
    CmpKind kind = CmpKind::Eq;
    std::string_view versionText = token;
    bool foundOp = false;
    for (const OpSpec& op : kOps) {
        if (token == op.text) {
            if (!next) {
                if (error) *error = "version range operator '" + std::string(op.text) +
                                    "' is missing a version";
                return false;
            }
            opText = op.text;
            kind = op.kind;
            versionText = *next;
            *consumedNext = true;
            foundOp = true;
            break;
        }
        if (token.size() > op.text.size() && token.substr(0, op.text.size()) == op.text) {
            opText = op.text;
            kind = op.kind;
            versionText = token.substr(op.text.size());
            foundOp = true;
            break;
        }
    }
    if (!foundOp) versionText = token;

    auto parsed = SemVer::Parse(Trim(versionText));
    if (!parsed) {
        if (error) {
            *error = "invalid version \"" + std::string(versionText) + "\" in range";
            if (!opText.empty()) *error += " after '" + std::string(opText) + "'";
        }
        return false;
    }
    out.kind = kind;
    out.version = *parsed;
    if (kind == CmpKind::Caret || kind == CmpKind::Tilde) {
        auto upper = ExclusiveUpper(*parsed, kind, error);
        if (!upper) return false;
        out.upper = *upper;
    }
    return true;
}

std::optional<std::vector<Comparator>> ParseRange(std::string_view range,
                                                  std::string* error) {
    const std::string_view trimmed = Trim(range);
    if (trimmed.empty()) {
        if (error) *error = "version range is empty";
        return std::nullopt;
    }
    if (trimmed.find("||") != std::string_view::npos) {
        if (error) *error = "OR ranges (||) are not supported; AND constraints with a space";
        return std::nullopt;
    }
    const std::vector<std::string> tokens = Tokens(trimmed);
    std::vector<Comparator> comparators;
    for (usize i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "||") {
            if (error) *error = "OR ranges (||) are not supported; AND constraints with a space";
            return std::nullopt;
        }
        const std::string* next = (i + 1 < tokens.size()) ? &tokens[i + 1] : nullptr;
        bool consumed = false;
        Comparator cmp;
        if (!ParseComparator(tokens[i], next, &consumed, cmp, error)) return std::nullopt;
        if (consumed) ++i;
        comparators.push_back(cmp);
    }
    if (comparators.empty()) {
        if (error) *error = "version range is empty";
        return std::nullopt;
    }
    return comparators;
}

class Reader {
public:
    Reader(const std::filesystem::path& file, std::vector<ManifestError>& errors)
        : m_file(file), m_errors(errors) {}

    bool Failed() const { return m_failed; }

    void Error(std::string field, std::string message) {
        m_failed = true;
        m_errors.push_back(ManifestError{m_file, std::move(field), std::move(message)});
    }

    const SerialValue* Field(const SerialValue& obj, const char* name) const {
        return obj.Find(name);
    }

private:
    const std::filesystem::path& m_file;
    std::vector<ManifestError>& m_errors;
    bool m_failed = false;
};

bool KnownField(std::string_view key) {
    for (const char* field : kFields) {
        if (key == field) return true;
    }
    return false;
}

bool KnownDependencyField(std::string_view key) {
    for (const char* field : kDependencyFields) {
        if (key == field) return true;
    }
    return false;
}

std::string RequireString(Reader& reader, const SerialValue& obj, const char* field,
                          bool nonEmpty, bool trim) {
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

}  // namespace

const char* ToString(PluginKind kind) {
    switch (kind) {
        case PluginKind::Runtime: return "Runtime";
        case PluginKind::Editor: return "Editor";
        case PluginKind::RuntimeAndEditor: return "RuntimeAndEditor";
    }
    return "RuntimeAndEditor";
}

const char* HostPlatform() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

bool IsKnownPlatform(std::string_view name) {
    return name == "Windows" || name == "Linux" || name == "macOS";
}

bool IsValidPluginId(std::string_view id) {
    if (id.empty() || id.front() == '.' || id.back() == '.') return false;
    for (usize i = 0; i < id.size(); ++i) {
        const char c = id[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '.';
        if (!ok) return false;
        if (i > 0 && c == '.' && id[i - 1] == '.') return false;
    }
    return true;
}

bool SatisfiesRange(const SemVer& version, std::string_view range, std::string* error) {
    auto comparators = ParseRange(range, error);
    if (!comparators) return false;
    if (error) error->clear();
    if (!version.pre.empty()) {
        bool allowed = false;
        for (const Comparator& cmp : *comparators) {
            if (!cmp.version.pre.empty() && SameTuple(cmp.version, version)) allowed = true;
        }
        if (!allowed) return false;
    }
    for (const Comparator& cmp : *comparators) {
        if (!Matches(version, cmp)) return false;
    }
    return true;
}

std::optional<PluginManifest> ParseManifest(std::string_view json,
                                            const std::filesystem::path& file,
                                            std::vector<ManifestError>& errors) {
    const usize errorsBefore = errors.size();
    std::string jsonError;
    auto parsed = ParseJson(json, &jsonError);
    if (!parsed) {
        errors.push_back(ManifestError{file, "", jsonError.empty() ? "manifest is not valid JSON"
                                                                   : jsonError});
        return std::nullopt;
    }
    if (!parsed->IsObject()) {
        errors.push_back(ManifestError{file, "", "manifest must be a JSON object"});
        return std::nullopt;
    }

    Reader reader(file, errors);
    for (const auto& member : parsed->Members()) {
        if (!KnownField(member.first)) {
            reader.Error(member.first, "unknown field \"" + member.first + "\"");
        }
    }

    PluginManifest manifest;
    manifest.manifestPath = file;

    const SerialValue* schema = parsed->Find("schema");
    if (!schema) {
        reader.Error("schema", "schema is required");
    } else if (!schema->IsInt()) {
        reader.Error("schema", "schema must be an integer");
    } else if (schema->AsInt() != 1) {
        reader.Error("schema", "unsupported manifest schema " + std::to_string(schema->AsInt()) +
                                   "; this engine reads schema 1");
    } else {
        manifest.schema = 1;
    }

    manifest.id = RequireString(reader, *parsed, "id", true, /*trim=*/false);
    if (!manifest.id.empty() && !IsValidPluginId(manifest.id)) {
        reader.Error("id",
                     "id \"" + manifest.id +
                         "\" must match [A-Za-z0-9.]+ with no leading, trailing, or doubled dot");
    }

    manifest.displayName = RequireString(reader, *parsed, "displayName", true, true);
    if (const SerialValue* description = parsed->Find("description")) {
        if (!description->IsString()) reader.Error("description", "description must be a string");
        else manifest.description = Trimmed(description->AsString());
    }
    manifest.category = RequireString(reader, *parsed, "category", true, true);
    manifest.author = RequireString(reader, *parsed, "author", true, true);
    if (const SerialValue* url = parsed->Find("url")) {
        if (!url->IsString()) reader.Error("url", "url must be a string");
        else manifest.url = Trimmed(url->AsString());
    }

    const std::string versionText = RequireString(reader, *parsed, "version", true, true);
    if (!versionText.empty()) {
        auto version = SemVer::Parse(versionText);
        if (!version) {
            reader.Error("version", "version \"" + versionText + "\" is not a valid semantic version");
        } else {
            manifest.version = *version;
        }
    }

    manifest.engineVersionRange =
        RequireString(reader, *parsed, "engineVersionRange", true, true);
    if (!manifest.engineVersionRange.empty()) {
        std::string rangeError;
        ParseRange(manifest.engineVersionRange, &rangeError);
        if (!rangeError.empty()) {
            reader.Error("engineVersionRange",
                         "engineVersionRange \"" + manifest.engineVersionRange + "\": " + rangeError);
        }
    }

    const std::string kindText = RequireString(reader, *parsed, "kind", true, true);
    if (!kindText.empty()) {
        if (kindText == "Runtime") manifest.kind = PluginKind::Runtime;
        else if (kindText == "Editor") manifest.kind = PluginKind::Editor;
        else if (kindText == "RuntimeAndEditor") manifest.kind = PluginKind::RuntimeAndEditor;
        else {
            reader.Error("kind",
                         "kind \"" + kindText +
                             "\" must be Runtime, Editor, or RuntimeAndEditor");
        }
    }

    const SerialValue* platforms = parsed->Find("platforms");
    if (!platforms) {
        reader.Error("platforms", "platforms is required");
    } else if (!platforms->IsArray()) {
        reader.Error("platforms", "platforms must be an array of strings");
    } else if (platforms->Size() == 0) {
        reader.Error("platforms", "platforms must list at least one of Windows, Linux, macOS");
    } else {
        for (usize i = 0; i < platforms->Size(); ++i) {
            const SerialValue& entry = platforms->At(i);
            const std::string field = "platforms[" + std::to_string(i) + "]";
            if (!entry.IsString()) {
                reader.Error(field, field + " must be a string");
                continue;
            }
            const std::string name = Trimmed(entry.AsString());
            if (!IsKnownPlatform(name)) {
                reader.Error(field, "unknown platform \"" + name +
                                       "\"; expected Windows, Linux, or macOS");
                continue;
            }
            manifest.platforms.push_back(name);
        }
        if (manifest.platforms.empty() && !reader.Failed()) {
            reader.Error("platforms", "platforms must list at least one of Windows, Linux, macOS");
        }
    }

    if (const SerialValue* deps = parsed->Find("dependencies")) {
        if (!deps->IsArray()) {
            reader.Error("dependencies", "dependencies must be an array");
        } else {
            for (usize i = 0; i < deps->Size(); ++i) {
                const std::string prefix = "dependencies[" + std::to_string(i) + "]";
                const SerialValue& entry = deps->At(i);
                if (!entry.IsObject()) {
                    reader.Error(prefix, prefix + " must be an object");
                    continue;
                }
                for (const auto& member : entry.Members()) {
                    if (!KnownDependencyField(member.first)) {
                        reader.Error(prefix + "." + member.first,
                                     "unknown field \"" + member.first + "\"");
                    }
                }
                PluginDependency dep;
                const SerialValue* id = entry.Find("id");
                if (!id) reader.Error(prefix + ".id", prefix + ".id is required");
                else if (!id->IsString()) reader.Error(prefix + ".id", prefix + ".id must be a string");
                else {
                    dep.id = id->AsString();
                    if (!IsValidPluginId(dep.id)) {
                        reader.Error(prefix + ".id",
                                     prefix + ".id \"" + dep.id + "\" is not a valid plugin id");
                    }
                }
                const SerialValue* ver = entry.Find("version");
                if (!ver) reader.Error(prefix + ".version", prefix + ".version is required");
                else if (!ver->IsString()) {
                    reader.Error(prefix + ".version", prefix + ".version must be a version range string");
                } else {
                    dep.versionRange = Trimmed(ver->AsString());
                    std::string rangeError;
                    ParseRange(dep.versionRange, &rangeError);
                    if (!rangeError.empty()) {
                        reader.Error(prefix + ".version",
                                     prefix + ".version \"" + dep.versionRange + "\": " + rangeError);
                    }
                }
                if (const SerialValue* optional = entry.Find("optional")) {
                    if (!optional->IsBool()) {
                        reader.Error(prefix + ".optional", prefix + ".optional must be a boolean");
                    } else {
                        dep.optional = optional->AsBool();
                    }
                }
                manifest.dependencies.push_back(std::move(dep));
            }
        }
    }

    if (const SerialValue* caps = parsed->Find("capabilities")) {
        if (!caps->IsArray()) {
            reader.Error("capabilities", "capabilities must be an array of strings");
        } else {
            for (usize i = 0; i < caps->Size(); ++i) {
                const std::string field = "capabilities[" + std::to_string(i) + "]";
                const SerialValue& entry = caps->At(i);
                if (!entry.IsString()) {
                    reader.Error(field, field + " must be a string");
                    continue;
                }
                const std::string cap = Trimmed(entry.AsString());
                if (cap.empty()) {
                    reader.Error(field, field + " must not be empty");
                    continue;
                }
                manifest.capabilities.push_back(cap);
            }
        }
    }

    if (const SerialValue* enabled = parsed->Find("enabledByDefault")) {
        if (!enabled->IsBool()) {
            reader.Error("enabledByDefault", "enabledByDefault must be a boolean");
        } else {
            manifest.enabledByDefault = enabled->AsBool();
        }
    }

    if (errors.size() != errorsBefore) return std::nullopt;
    return manifest;
}

}  // namespace Luma::Plugins
