#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Types.h"

// Generates a plugin folder from Engine/Plugins/Templates/. The editor does
// not compile C++; GeneratePlugin only writes files.

namespace Luma::Plugins {

enum class PluginTemplate : u8 {
    Blank,
    ContentOnly,
    EditorToolbarButton,
    EditorStandaloneWindow,
    RuntimeComponent,
};

const char* ToString(PluginTemplate tmpl);
const char* TemplateDisplayName(PluginTemplate tmpl);
const char* TemplateDescription(PluginTemplate tmpl);
bool ParsePluginTemplate(std::string_view text, PluginTemplate& out);
std::vector<PluginTemplate> AllPluginTemplates();

bool IsValidPluginName(std::string_view name, std::string* error = nullptr);

struct PluginWizardRequest {
    PluginTemplate tmpl = PluginTemplate::Blank;
    std::string name;
    std::string friendlyName;
    std::string author;
    std::string description;
    std::string category = "Other";
    std::filesystem::path location;      // parent Plugins/ directory
    std::filesystem::path templateRoot;  // Engine/Plugins/Templates
};

struct PluginWizardResult {
    std::filesystem::path pluginDir;
    std::vector<std::filesystem::path> filesWritten;
    bool hasModules = false;
};

bool GeneratePlugin(const PluginWizardRequest& request, PluginWizardResult* result,
                    std::string* error);

}  // namespace Luma::Plugins
