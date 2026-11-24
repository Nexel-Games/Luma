#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Luma/Plugins/PluginDescriptor.h"
#include "Luma/Plugins/PluginManifest.h"
#include "Luma/Serialization/Json.h"

using Luma::Plugins::DescriptorToValue;
using Luma::Plugins::ManifestError;
using Luma::Plugins::ModuleType;
using Luma::Plugins::ParseDescriptor;
using Luma::Plugins::PluginDescriptor;
using Luma::Plugins::ToManifest;
using Luma::Plugins::WriteDescriptor;
using Luma::SerialValue;

namespace {

void Put(SerialValue& obj, std::string_view omit, const char* key, SerialValue value) {
    if (omit != key) obj[key] = std::move(value);
}

SerialValue ValidV1(std::string_view omit = {}) {
    SerialValue obj = SerialValue::MakeObject();
    Put(obj, omit, "FileVersion", SerialValue{1});
    Put(obj, omit, "Version", SerialValue{3});
    Put(obj, omit, "VersionName", SerialValue{"1.2.0"});
    Put(obj, omit, "FriendlyName", SerialValue{"Sample Spinner"});
    Put(obj, omit, "Description", SerialValue{"Rotates actors."});
    Put(obj, omit, "Category", SerialValue{"Simulation"});
    Put(obj, omit, "CreatedBy", SerialValue{"Luma"});
    Put(obj, omit, "CreatedByURL", SerialValue{"https://example.com"});
    Put(obj, omit, "DocsURL", SerialValue{""});
    Put(obj, omit, "SupportURL", SerialValue{""});
    Put(obj, omit, "EngineVersion", SerialValue{">=0.1.0 <1.0.0"});
    Put(obj, omit, "EnabledByDefault", SerialValue{false});
    Put(obj, omit, "CanContainContent", SerialValue{true});
    Put(obj, omit, "IsBetaVersion", SerialValue{false});
    Put(obj, omit, "IsExperimentalVersion", SerialValue{false});
    Put(obj, omit, "Installed", SerialValue{false});
    if (omit != "SupportedTargetPlatforms") {
        SerialValue platforms = SerialValue::MakeArray();
        platforms.PushBack(SerialValue{"Windows"});
        platforms.PushBack(SerialValue{"Linux"});
        obj["SupportedTargetPlatforms"] = std::move(platforms);
    }
    if (omit != "Modules") {
        SerialValue runtime = SerialValue::MakeObject();
        runtime["Name"] = SerialValue{"SampleSpinner"};
        runtime["Type"] = SerialValue{"Runtime"};
        runtime["LoadingPhase"] = SerialValue{"Default"};
        SerialValue editor = SerialValue::MakeObject();
        editor["Name"] = SerialValue{"SampleSpinnerEditor"};
        editor["Type"] = SerialValue{"Editor"};
        editor["LoadingPhase"] = SerialValue{"PostEngineInit"};
        SerialValue allow = SerialValue::MakeArray();
        allow.PushBack(SerialValue{"Windows"});
        editor["PlatformAllowList"] = std::move(allow);
        SerialValue modules = SerialValue::MakeArray();
        modules.PushBack(std::move(runtime));
        modules.PushBack(std::move(editor));
        obj["Modules"] = std::move(modules);
    }
    if (omit != "Plugins") {
        SerialValue dep = SerialValue::MakeObject();
        dep["Name"] = SerialValue{"OtherPlugin"};
        dep["Enabled"] = SerialValue{true};
        dep["Optional"] = SerialValue{false};
        dep["Version"] = SerialValue{">=1.0.0"};
        SerialValue plugins = SerialValue::MakeArray();
        plugins.PushBack(std::move(dep));
        obj["Plugins"] = std::move(plugins);
    }
    return obj;
}

SerialValue ValidSchema1() {
    SerialValue obj = SerialValue::MakeObject();
    obj["schema"] = SerialValue{1};
    obj["id"] = SerialValue{"Luma.Sample"};
    obj["displayName"] = SerialValue{"Sample"};
    obj["description"] = SerialValue{"A sample plugin."};
    obj["category"] = SerialValue{"Tools"};
    obj["author"] = SerialValue{"Luma"};
    obj["url"] = SerialValue{"https://example.com/sample"};
    obj["version"] = SerialValue{"1.2.3"};
    obj["engineVersionRange"] = SerialValue{">=0.1.0 <1.0.0"};
    obj["kind"] = SerialValue{"RuntimeAndEditor"};
    SerialValue platforms = SerialValue::MakeArray();
    platforms.PushBack(SerialValue{"Windows"});
    obj["platforms"] = std::move(platforms);
    obj["enabledByDefault"] = SerialValue{true};
    SerialValue dep = SerialValue::MakeObject();
    dep["id"] = SerialValue{"Luma.Core"};
    dep["version"] = SerialValue{">=1.0.0 <2.0.0"};
    dep["optional"] = SerialValue{true};
    SerialValue deps = SerialValue::MakeArray();
    deps.PushBack(std::move(dep));
    obj["dependencies"] = std::move(deps);
    return obj;
}

std::optional<PluginDescriptor> Parse(const SerialValue& value, std::vector<ManifestError>& errors,
                                      const char* file = "SampleSpinner.lplugin") {
    return ParseDescriptor(Luma::WriteJson(value, false), file, errors);
}

bool HasField(const std::vector<ManifestError>& errors, std::string_view field) {
    for (const ManifestError& error : errors) {
        if (error.field == field) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("ParseDescriptor accepts a complete FileVersion 1 document", "[plugins][descriptor]") {
    std::vector<ManifestError> errors;
    auto desc = Parse(ValidV1(), errors);
    REQUIRE(desc.has_value());
    REQUIRE(errors.empty());
    REQUIRE(desc->fileVersion == 1);
    REQUIRE(desc->version == 3);
    REQUIRE(desc->versionName.ToString() == "1.2.0");
    REQUIRE(desc->name == "SampleSpinner");
    REQUIRE(desc->friendlyName == "Sample Spinner");
    REQUIRE(desc->description == "Rotates actors.");
    REQUIRE(desc->category == "Simulation");
    REQUIRE(desc->createdBy == "Luma");
    REQUIRE(desc->createdByUrl == "https://example.com");
    REQUIRE(desc->engineVersionRange == ">=0.1.0 <1.0.0");
    REQUIRE(desc->canContainContent);
    REQUIRE(desc->supportedTargetPlatforms.size() == 2);
    REQUIRE(desc->modules.size() == 2);
    REQUIRE(desc->modules[0].name == "SampleSpinner");
    REQUIRE(desc->modules[0].type == ModuleType::Runtime);
    REQUIRE(desc->modules[1].type == ModuleType::Editor);
    REQUIRE(desc->plugins.size() == 1);
    REQUIRE(desc->plugins[0].name == "OtherPlugin");
    REQUIRE_FALSE(desc->migratedFromSchema1);

    const Luma::Plugins::PluginManifest old = ToManifest(*desc);
    REQUIRE(old.id == "SampleSpinner");
    REQUIRE(old.kind == Luma::Plugins::PluginKind::RuntimeAndEditor);
}

TEST_CASE("ParseDescriptor reports missing and invalid FileVersion 1 fields",
          "[plugins][descriptor]") {
    const char* required[] = {"FileVersion", "Version", "VersionName", "FriendlyName",
                              "Category",    "CreatedBy", "EngineVersion"};
    for (const char* field : required) {
        INFO(field);
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(ValidV1(field), errors).has_value());
        REQUIRE(HasField(errors, field));
    }
    {
        SerialValue obj = ValidV1();
        obj["FileVersion"] = SerialValue{2};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "FileVersion"));
    }
    {
        SerialValue obj = ValidV1();
        obj["Version"] = SerialValue{0};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "Version"));
    }
    {
        SerialValue obj = ValidV1();
        obj["VersionName"] = SerialValue{"01.2.0"};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "VersionName"));
    }
    {
        SerialValue obj = ValidV1();
        obj["EngineVersion"] = SerialValue{">=1.0.0 || <2.0.0"};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "EngineVersion"));
    }
    {
        SerialValue obj = ValidV1();
        obj["EnabledByDefault"] = SerialValue{"yes"};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "EnabledByDefault"));
    }
    {
        SerialValue obj = ValidV1();
        SerialValue platforms = SerialValue::MakeArray();
        platforms.PushBack(SerialValue{"Solaris"});
        obj["SupportedTargetPlatforms"] = std::move(platforms);
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "SupportedTargetPlatforms[0]"));
    }
    {
        SerialValue obj = ValidV1();
        obj["Modules"].At(0)["Type"] = SerialValue{"Both"};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "Modules[0].Type"));
    }
    {
        SerialValue obj = ValidV1();
        obj["Modules"].At(1)["LoadingPhase"] = SerialValue{"Whenever"};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "Modules[1].LoadingPhase"));
    }
    {
        SerialValue obj = ValidV1();
        obj["Plugins"].At(0)["Name"] = SerialValue{".Leading"};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "Plugins[0].Name"));
    }
    {
        SerialValue obj = SerialValue::MakeObject();
        obj["schema"] = SerialValue{"nope"};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
    }
}

TEST_CASE("ParseDescriptor migrates schema 1 manifests", "[plugins][descriptor]") {
    std::vector<ManifestError> errors;
    auto desc = Parse(ValidSchema1(), errors, "Luma.Sample.lplugin");
    REQUIRE(desc.has_value());
    REQUIRE(errors.empty());
    REQUIRE(desc->migratedFromSchema1);
    REQUIRE(desc->name == "Luma.Sample");
    REQUIRE(desc->friendlyName == "Sample");
    REQUIRE(desc->versionName.ToString() == "1.2.3");
    REQUIRE(desc->enabledByDefault);
    REQUIRE(desc->modules.size() == 2);
    REQUIRE(desc->modules[0].type == ModuleType::Runtime);
    REQUIRE(desc->modules[1].name == "Luma.SampleEditor");
    REQUIRE(desc->modules[1].type == ModuleType::Editor);
    REQUIRE(desc->plugins.size() == 1);
    REQUIRE(desc->plugins[0].optional);
}

TEST_CASE("ParseDescriptor keeps unknown FileVersion 1 fields on rewrite",
          "[plugins][descriptor]") {
    SerialValue obj = ValidV1();
    obj["CustomVendorKey"] = SerialValue{"keep-me"};
    SerialValue extra = SerialValue::MakeObject();
    extra["inner"] = SerialValue{42};
    obj["VendorBlock"] = std::move(extra);

    std::vector<ManifestError> errors;
    auto desc = Parse(obj, errors);
    REQUIRE(desc.has_value());
    REQUIRE(errors.empty());
    REQUIRE(desc->extra.Find("CustomVendorKey"));
    REQUIRE(desc->extra.Find("VendorBlock"));

    const std::string written = WriteDescriptor(*desc, false);
    REQUIRE(written.find("CustomVendorKey") != std::string::npos);
    REQUIRE(written.find("keep-me") != std::string::npos);
    REQUIRE(written.find("VendorBlock") != std::string::npos);

    std::vector<ManifestError> again;
    auto round = ParseDescriptor(written, "SampleSpinner.lplugin", again);
    REQUIRE(round.has_value());
    REQUIRE(again.empty());
    REQUIRE(round->extra.Find("CustomVendorKey"));
    REQUIRE(round->extra.Find("CustomVendorKey")->AsString() == "keep-me");
}
