#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Core/Version.h"

// Plugin manifests (.lplugin JSON). Parsing validates the document and never
// loads or calls plugin code. Version constraints are npm-style ranges:
// exact, >=, >, <=, <, =, ==, ^, ~, combined with AND (whitespace). OR ("||")
// is rejected.

namespace Luma::Plugins {

enum class PluginKind : u8 { Runtime, Editor, RuntimeAndEditor };

const char* ToString(PluginKind kind);

// "Windows", "Linux", or "macOS" for the process that is running.
const char* HostPlatform();

// True for Windows, Linux, and macOS (the names manifests may list).
bool IsKnownPlatform(std::string_view name);

// [A-Za-z0-9.]+ with no leading, trailing, or doubled dot.
bool IsValidPluginId(std::string_view id);

struct PluginDependency {
    std::string id;
    std::string versionRange;  // e.g. ">=1.2.0 <2.0.0"
    bool optional = false;
};

struct PluginManifest {
    i32 schema = 1;
    std::string id;
    std::string displayName;
    std::string description;
    std::string category;
    std::string author;
    std::string url;
    SemVer version;
    std::string engineVersionRange;
    PluginKind kind = PluginKind::RuntimeAndEditor;
    std::vector<std::string> platforms;
    std::vector<PluginDependency> dependencies;
    std::vector<std::string> capabilities;
    bool enabledByDefault = false;
    std::filesystem::path manifestPath;
};

struct ManifestError {
    std::filesystem::path file;
    std::string field;
    std::string message;
};

// True when `v` satisfies `range`. On a malformed range, returns false and,
// when `error` is non-null, sets it. A well-formed range that `v` does not
// satisfy returns false and clears `error`.
bool SatisfiesRange(const SemVer& version, std::string_view range,
                    std::string* error = nullptr);

// Parses one manifest. Appends every problem it finds to `errors` (it does not
// clear the vector). Returns nullopt when the document is not a usable manifest.
std::optional<PluginManifest> ParseManifest(std::string_view json,
                                            const std::filesystem::path& file,
                                            std::vector<ManifestError>& errors);

}  // namespace Luma::Plugins
