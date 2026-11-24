#include "Luma/Plugins/PluginPackaging.h"

#include <algorithm>

namespace Luma::Plugins {
namespace {

PackagingRole RoleForModule(ModuleType type) {
    switch (type) {
        case ModuleType::Runtime: return PackagingRole::RuntimeModule;
        case ModuleType::Developer: return PackagingRole::DeveloperModule;
        case ModuleType::Editor: return PackagingRole::EditorModule;
        case ModuleType::UncookedOnly: return PackagingRole::UncookedOnlyModule;
    }
    return PackagingRole::Other;
}

bool ModuleGoesInGame(ModuleType type) {
    return type == ModuleType::Runtime;
}

void AddItem(GamePackagePlan& plan, const std::string& plugin, std::filesystem::path relative,
             PackagingRole role, bool inGame) {
    PackagingItem item;
    item.plugin = plugin;
    item.relative = std::move(relative);
    item.role = role;
    item.inPackagedGame = inGame;
    plan.items.push_back(std::move(item));
}

}  // namespace

const char* ToString(PackagingRole role) {
    switch (role) {
        case PackagingRole::RuntimeModule: return "RuntimeModule";
        case PackagingRole::DeveloperModule: return "DeveloperModule";
        case PackagingRole::EditorModule: return "EditorModule";
        case PackagingRole::UncookedOnlyModule: return "UncookedOnlyModule";
        case PackagingRole::Content: return "Content";
        case PackagingRole::Config: return "Config";
        case PackagingRole::Source: return "Source";
        case PackagingRole::Icon: return "Icon";
        case PackagingRole::Other: return "Other";
    }
    return "Other";
}

GamePackagePlan PlanGamePackage(std::span<const PluginDescriptor> enabled) {
    GamePackagePlan plan;
    for (const PluginDescriptor& plugin : enabled) {
        const std::filesystem::path root = plugin.descriptorPath.empty()
                                               ? std::filesystem::path{}
                                               : plugin.descriptorPath.parent_path();
        for (const ModuleDescriptor& module : plugin.modules) {
            const PackagingRole role = RoleForModule(module.type);
            AddItem(plan, plugin.name, std::filesystem::path("Source") / module.name, role,
                    ModuleGoesInGame(module.type));
        }
        if (plugin.canContainContent) {
            AddItem(plan, plugin.name, "Content", PackagingRole::Content, true);
            AddItem(plan, plugin.name, "Config", PackagingRole::Config, true);
        }
        AddItem(plan, plugin.name, "Source", PackagingRole::Source, false);
        AddItem(plan, plugin.name, std::filesystem::path("Resources") / "Icon128.png",
                PackagingRole::Icon, false);
        AddItem(plan, plugin.name, "Binaries", PackagingRole::Other, false);
        (void)root;
    }
    return plan;
}

}  // namespace Luma::Plugins
