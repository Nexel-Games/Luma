#include "Luma/Plugins/PluginWizard.h"

#include <cctype>
#include <fstream>
#include <sstream>

#include "Luma/Core/Log.h"
#include "Luma/Core/Version.h"

namespace Luma::Plugins {
namespace {

std::string DefaultEngineRange() {
    const SemVer v = EngineVersion();
    return ">=" + v.ToString() + " <" + std::to_string(v.major + 1) + ".0.0";
}

std::string ReplaceAll(std::string text, std::string_view from, std::string_view to) {
    if (from.empty()) return text;
    usize pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

struct Tokens {
    std::string pluginName;
    std::string friendlyName;
    std::string author;
    std::string description;
    std::string category;
    std::string engineVersion;
    std::string editorModule;
};

std::string ApplyTokens(std::string text, const Tokens& t) {
    text = ReplaceAll(std::move(text), "{{PluginName}}", t.pluginName);
    text = ReplaceAll(std::move(text), "{{FriendlyName}}", t.friendlyName);
    text = ReplaceAll(std::move(text), "{{Author}}", t.author);
    text = ReplaceAll(std::move(text), "{{Description}}", t.description);
    text = ReplaceAll(std::move(text), "{{Category}}", t.category);
    text = ReplaceAll(std::move(text), "{{EngineVersion}}", t.engineVersion);
    text = ReplaceAll(std::move(text), "{{EditorModuleName}}", t.editorModule);
    return text;
}

bool LooksText(const std::filesystem::path& rel) {
    const std::string ext = rel.extension().string();
    return ext != ".png" && ext != ".bin";
}

bool WriteBytes(const std::filesystem::path& path, const char* data, usize size, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error) *error = "could not write " + path.string();
        return false;
    }
    out.write(data, static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}

bool WriteDefaultIcon(const std::filesystem::path& path, std::string* error) {
    static const unsigned char kPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00,
        0x01, 0x00, 0x00, 0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49,
        0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    return WriteBytes(path, reinterpret_cast<const char*>(kPng), sizeof(kPng), error);
}

const char* TemplateFolder(PluginTemplate tmpl) {
    switch (tmpl) {
        case PluginTemplate::Blank: return "Blank";
        case PluginTemplate::ContentOnly: return "ContentOnly";
        case PluginTemplate::EditorToolbarButton: return "EditorToolbarButton";
        case PluginTemplate::EditorStandaloneWindow: return "EditorStandaloneWindow";
        case PluginTemplate::RuntimeComponent: return "RuntimeComponent";
    }
    return "Blank";
}

}  // namespace

const char* ToString(PluginTemplate tmpl) { return TemplateFolder(tmpl); }

const char* TemplateDisplayName(PluginTemplate tmpl) {
    switch (tmpl) {
        case PluginTemplate::Blank: return "Blank";
        case PluginTemplate::ContentOnly: return "Content Only";
        case PluginTemplate::EditorToolbarButton: return "Editor Toolbar Button";
        case PluginTemplate::EditorStandaloneWindow: return "Editor Standalone Window";
        case PluginTemplate::RuntimeComponent: return "Runtime Component";
    }
    return "Blank";
}

const char* TemplateDescription(PluginTemplate tmpl) {
    switch (tmpl) {
        case PluginTemplate::Blank:
            return "A Runtime module that starts and does nothing else.";
        case PluginTemplate::ContentOnly:
            return "Content and config only. No C++ modules.";
        case PluginTemplate::EditorToolbarButton:
            return "An Editor module that registers a Tools menu command.";
        case PluginTemplate::EditorStandaloneWindow:
            return "An Editor module that registers a dockable panel.";
        case PluginTemplate::RuntimeComponent:
            return "A Runtime module that registers a reflected component.";
    }
    return "";
}

bool ParsePluginTemplate(std::string_view text, PluginTemplate& out) {
    if (text == "Blank") {
        out = PluginTemplate::Blank;
        return true;
    }
    if (text == "ContentOnly") {
        out = PluginTemplate::ContentOnly;
        return true;
    }
    if (text == "EditorToolbarButton") {
        out = PluginTemplate::EditorToolbarButton;
        return true;
    }
    if (text == "EditorStandaloneWindow") {
        out = PluginTemplate::EditorStandaloneWindow;
        return true;
    }
    if (text == "RuntimeComponent") {
        out = PluginTemplate::RuntimeComponent;
        return true;
    }
    return false;
}

std::vector<PluginTemplate> AllPluginTemplates() {
    return {PluginTemplate::Blank, PluginTemplate::ContentOnly, PluginTemplate::EditorToolbarButton,
            PluginTemplate::EditorStandaloneWindow, PluginTemplate::RuntimeComponent};
}

bool IsValidPluginName(std::string_view name, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (name.empty()) return fail("Name is required.");
    if (name.front() == '.' || name.back() == '.') {
        return fail("Name cannot start or end with a dot. Use letters, digits, and dots in the middle.");
    }
    if (!std::isalpha(static_cast<unsigned char>(name.front()))) {
        return fail("Name must start with a letter.");
    }
    bool prevDot = false;
    for (char ch : name) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (ch == '.') {
            if (prevDot) return fail("Name cannot contain consecutive dots.");
            prevDot = true;
            continue;
        }
        prevDot = false;
        if (!std::isalnum(c)) {
            return fail("Name may only contain letters, digits, and dots in the middle.");
        }
    }
    return true;
}

bool GeneratePlugin(const PluginWizardRequest& request, PluginWizardResult* result,
                    std::string* error) {
    auto fail = [&](std::string message) {
        LUMA_USER_ERROR("Plugins", "{}", message);
        if (error) *error = std::move(message);
        return false;
    };

    std::string nameError;
    if (!IsValidPluginName(request.name, &nameError)) return fail(nameError);
    if (request.location.empty()) return fail("Location is required.");
    if (request.templateRoot.empty() ||
        !std::filesystem::is_directory(request.templateRoot / TemplateFolder(request.tmpl))) {
        return fail("Template folder is missing: " +
                    (request.templateRoot / TemplateFolder(request.tmpl)).string() +
                    ". Point the wizard at Engine/Plugins/Templates.");
    }

    Tokens tokens;
    tokens.pluginName = request.name;
    tokens.friendlyName = request.friendlyName.empty() ? request.name : request.friendlyName;
    tokens.author = request.author.empty() ? "Unknown" : request.author;
    tokens.description = request.description;
    tokens.category = request.category.empty() ? "Other" : request.category;
    tokens.engineVersion = DefaultEngineRange();
    tokens.editorModule = request.name + "Editor";

    const std::filesystem::path dest = request.location / request.name;
    std::error_code ec;
    if (std::filesystem::exists(dest, ec)) {
        return fail(dest.string() + " already exists. Choose a different name or location.");
    }

    const std::filesystem::path src = request.templateRoot / TemplateFolder(request.tmpl);
    PluginWizardResult local;
    local.pluginDir = dest;
    local.hasModules = request.tmpl != PluginTemplate::ContentOnly;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(src, ec)) {
        if (ec) return fail("could not read templates under " + src.string());
        if (!entry.is_regular_file(ec)) continue;
        std::filesystem::path rel = std::filesystem::relative(entry.path(), src, ec);
        if (ec) continue;
        const std::filesystem::path outPath = dest / ApplyTokens(rel.generic_string(), tokens);
        if (LooksText(rel)) {
            std::ifstream in(entry.path(), std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            std::string body = ApplyTokens(ss.str(), tokens);
            if (!WriteBytes(outPath, body.data(), body.size(), error)) return false;
        } else {
            std::filesystem::create_directories(outPath.parent_path(), ec);
            std::filesystem::copy_file(entry.path(), outPath, ec);
            if (ec) return fail("could not copy " + entry.path().string());
        }
        local.filesWritten.push_back(outPath);
    }

    const std::filesystem::path icon = dest / "Resources" / "Icon128.png";
    if (!std::filesystem::exists(icon, ec)) {
        if (!WriteDefaultIcon(icon, error)) return false;
        local.filesWritten.push_back(icon);
    }

    if (result) *result = std::move(local);
    LUMA_LOG_INFO("Plugins", "generated plugin {} from template {} at {}", request.name,
                  TemplateFolder(request.tmpl), dest.string());
    return true;
}

}  // namespace Luma::Plugins
