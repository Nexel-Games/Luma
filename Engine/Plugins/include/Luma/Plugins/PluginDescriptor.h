#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Core/Version.h"
#include "Luma/Plugins/PluginManifest.h"
#include "Luma/Serialization/SerialValue.h"

// Approved PLUGIN_ARCHITECTURE.md descriptor (FileVersion 1). Discovery reads
// this JSON and never runs plugin code. Unknown keys are kept on rewrite.

namespace Luma {
class Project;
}

namespace Luma::Plugins {

enum class ModuleType : u8 { Runtime, Developer, Editor, UncookedOnly };
enum class LoadingPhase : u8 {
    EarliestPossible,
    PostConfigInit,
    PreDefault,
    Default,
    PostDefault,
    PostEngineInit,
};
enum class PluginTarget : u8 { Editor, Game, Program };
enum class PluginResolveState : u8 { Disabled, Invalid, Unavailable, Active };

const char* ToString(ModuleType type);
const char* ToString(LoadingPhase phase);
const char* ToString(PluginTarget target);
const char* ToString(PluginResolveState state);

bool ParseModuleType(std::string_view text, ModuleType& out);
bool ParseLoadingPhase(std::string_view text, LoadingPhase& out);

struct ModuleDescriptor {
    std::string name;
    ModuleType type = ModuleType::Runtime;
    LoadingPhase loadingPhase = LoadingPhase::Default;
    std::vector<std::string> platformAllowList;  // empty = all
};

struct PluginReference {
    std::string name;
    std::string versionRange;
    bool enabled = true;
    bool optional = false;
};

struct PluginDescriptor {
    i32 fileVersion = 1;
    i32 version = 1;          // integer that increases every release
    SemVer versionName;
    std::string name;         // folder / file / identity
    std::string friendlyName;
    std::string description;
    std::string category;
    std::string createdBy;
    std::string createdByUrl;
    std::string docsUrl;
    std::string supportUrl;
    std::string engineVersionRange;
    bool enabledByDefault = false;
    bool canContainContent = false;
    bool isBetaVersion = false;
    bool isExperimentalVersion = false;
    bool installed = false;
    std::vector<std::string> supportedTargetPlatforms;
    std::vector<ModuleDescriptor> modules;
    std::vector<PluginReference> plugins;
    std::filesystem::path descriptorPath;
    // Top-level keys this engine does not define, in document order.
    SerialValue extra = SerialValue::MakeObject();
    bool migratedFromSchema1 = false;
};

std::optional<PluginDescriptor> ParseDescriptor(std::string_view json,
                                                const std::filesystem::path& file,
                                                std::vector<ManifestError>& errors);

// FileVersion 1 JSON, including extra keys. Does not drop unknown fields.
SerialValue DescriptorToValue(const PluginDescriptor& descriptor);
std::string WriteDescriptor(const PluginDescriptor& descriptor, bool pretty = true);
bool WriteDescriptorFile(const PluginDescriptor& descriptor, const std::filesystem::path& path,
                         std::string* error = nullptr);

PluginManifest ToManifest(const PluginDescriptor& descriptor);

struct ProjectPluginEntry {
    std::string name;
    bool enabled = true;
};

// Architecture: `.luma` `"Plugins": [{ "Name", "Enabled" }]`. Also reads the
// older `"plugins": { "schema": 1, "enabled", "disabled" }` object.
std::vector<ProjectPluginEntry> ReadProjectPluginEntries(const Project* project);

struct ResolveConfig {
    PluginTarget target = PluginTarget::Editor;
    bool shipping = false;
    std::string platform;  // empty = HostPlatform()
    // When true, a module that would load here but is not in the set makes the
    // plugin Unavailable ("not built for this binary").
    bool checkBuiltModules = false;
    std::set<std::string> builtModules;
};

struct ResolvedPlugin {
    PluginDescriptor descriptor;
    PluginResolveState state = PluginResolveState::Disabled;
    std::vector<std::string> diagnostics;
    bool enabledInProject = false;
};

// `invalid` are discovered folders whose descriptor failed validation (name from
// the folder when the document had none). Every input plugin appears in the
// output. Active plugins are listed in deterministic dependency order.
std::vector<ResolvedPlugin> ResolvePlugins(std::span<const PluginDescriptor> valid,
                                           std::span<const PluginDescriptor> invalid,
                                           std::span<const ProjectPluginEntry> project,
                                           const ResolveConfig& config = {});

bool ModuleLoadsOn(ModuleType type, PluginTarget target, bool shipping);

}  // namespace Luma::Plugins
