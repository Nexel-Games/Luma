#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "Luma/Plugins/PluginDescriptor.h"

// What a packaged game *would* get. There is no cook/package pipeline yet, so
// every plan is blocked. Build-level separation (no Editor module in LumaEngine)
// is still required now.

namespace Luma::Plugins {

enum class PackagingRole : u8 {
    RuntimeModule,
    DeveloperModule,
    EditorModule,
    UncookedOnlyModule,
    Content,
    Config,
    Source,
    Icon,
    Other,
};

const char* ToString(PackagingRole role);

struct PackagingItem {
    std::string plugin;
    std::filesystem::path relative;
    PackagingRole role = PackagingRole::Other;
    bool inPackagedGame = false;
};

struct GamePackagePlan {
    bool blocked = true;
    std::string blockedReason =
        "Luma has no packaging pipeline. Plugin Content/ and Config/ cannot be cooked or copied into a shipped game yet.";
    std::vector<PackagingItem> items;
};

// Walks each enabled plugin folder (descriptorPath's parent). Runtime modules,
// Content/, and Config/ are marked inPackagedGame; Editor/Developer/UncookedOnly
// modules, Source/, and editor DLLs are not. The plan itself stays blocked.
GamePackagePlan PlanGamePackage(std::span<const PluginDescriptor> enabled);

}  // namespace Luma::Plugins
