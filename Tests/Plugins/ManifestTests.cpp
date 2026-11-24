#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Luma/Plugins/PluginManifest.h"
#include "Luma/Serialization/Json.h"

using Luma::Plugins::ManifestError;
using Luma::Plugins::ParseManifest;
using Luma::Plugins::PluginKind;
using Luma::Plugins::PluginManifest;
using Luma::Plugins::SatisfiesRange;
using Luma::SemVer;
using Luma::SerialValue;

namespace {

SemVer V(const char* text) { return *SemVer::Parse(text); }

void Put(SerialValue& obj, std::string_view omit, const char* key, SerialValue value) {
    if (omit != key) obj[key] = std::move(value);
}

SerialValue Valid(std::string_view omit = {}) {
    SerialValue obj = SerialValue::MakeObject();
    Put(obj, omit, "schema", SerialValue{1});
    Put(obj, omit, "id", SerialValue{"Luma.Sample"});
    Put(obj, omit, "displayName", SerialValue{"Sample"});
    Put(obj, omit, "description", SerialValue{"A sample plugin."});
    Put(obj, omit, "category", SerialValue{"Tools"});
    Put(obj, omit, "author", SerialValue{"Luma"});
    Put(obj, omit, "url", SerialValue{"https://example.com/sample"});
    Put(obj, omit, "version", SerialValue{"1.2.3"});
    Put(obj, omit, "engineVersionRange", SerialValue{">=0.1.0 <1.0.0"});
    Put(obj, omit, "kind", SerialValue{"Runtime"});
    if (omit != "platforms") {
        SerialValue platforms = SerialValue::MakeArray();
        platforms.PushBack(SerialValue{"Windows"});
        platforms.PushBack(SerialValue{"Linux"});
        platforms.PushBack(SerialValue{"macOS"});
        obj["platforms"] = std::move(platforms);
    }
    if (omit != "dependencies") {
        SerialValue dep = SerialValue::MakeObject();
        dep["id"] = SerialValue{"Luma.Core"};
        dep["version"] = SerialValue{">=1.0.0 <2.0.0"};
        dep["optional"] = SerialValue{true};
        SerialValue deps = SerialValue::MakeArray();
        deps.PushBack(std::move(dep));
        obj["dependencies"] = std::move(deps);
    }
    if (omit != "capabilities") {
        SerialValue caps = SerialValue::MakeArray();
        caps.PushBack(SerialValue{"components"});
        obj["capabilities"] = std::move(caps);
    }
    if (omit != "enabledByDefault") obj["enabledByDefault"] = SerialValue{false};
    return obj;
}

std::optional<PluginManifest> Parse(const SerialValue& value, std::vector<ManifestError>& errors) {
    return ParseManifest(Luma::WriteJson(value, false), "Luma.Sample.lplugin", errors);
}

bool HasField(const std::vector<ManifestError>& errors, std::string_view field) {
    for (const ManifestError& error : errors) {
        if (error.field == field) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("ParseManifest accepts a complete manifest", "[plugins][manifest]") {
    std::vector<ManifestError> errors;
    auto manifest = Parse(Valid(), errors);
    REQUIRE(manifest.has_value());
    REQUIRE(errors.empty());
    REQUIRE(manifest->schema == 1);
    REQUIRE(manifest->id == "Luma.Sample");
    REQUIRE(manifest->displayName == "Sample");
    REQUIRE(manifest->description == "A sample plugin.");
    REQUIRE(manifest->category == "Tools");
    REQUIRE(manifest->author == "Luma");
    REQUIRE(manifest->url == "https://example.com/sample");
    REQUIRE(manifest->version == V("1.2.3"));
    REQUIRE(manifest->engineVersionRange == ">=0.1.0 <1.0.0");
    REQUIRE(manifest->kind == PluginKind::Runtime);
    REQUIRE(manifest->platforms.size() == 3);
    REQUIRE(manifest->dependencies.size() == 1);
    REQUIRE(manifest->dependencies[0].id == "Luma.Core");
    REQUIRE(manifest->dependencies[0].versionRange == ">=1.0.0 <2.0.0");
    REQUIRE(manifest->dependencies[0].optional);
    REQUIRE(manifest->capabilities.size() == 1);
    REQUIRE(manifest->capabilities[0] == "components");
    REQUIRE_FALSE(manifest->enabledByDefault);
    REQUIRE(manifest->manifestPath == "Luma.Sample.lplugin");
}

TEST_CASE("ParseManifest reports every missing required field", "[plugins][manifest]") {
    const char* required[] = {"schema", "id",           "displayName", "category", "author",
                              "version", "engineVersionRange", "kind",  "platforms"};
    for (const char* field : required) {
        INFO(field);
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(Valid(field), errors).has_value());
        REQUIRE(HasField(errors, field));
    }
}

TEST_CASE("ParseManifest reports several missing fields at once", "[plugins][manifest]") {
    std::vector<ManifestError> errors;
    const char* json =
        R"({"schema":1,"displayName":"Sample","category":"Tools","author":"Luma","engineVersionRange":">=0.1.0 <1.0.0","kind":"Runtime","platforms":["Windows"]})";
    REQUIRE_FALSE(ParseManifest(json, "Luma.Sample.lplugin", errors).has_value());
    REQUIRE(HasField(errors, "id"));
    REQUIRE(HasField(errors, "version"));
}

TEST_CASE("ParseManifest rejects wrong field types", "[plugins][manifest]") {
    struct Bad {
        const char* field;
        SerialValue value;
    };
    const Bad cases[] = {
        {"schema", SerialValue{"1"}},
        {"id", SerialValue{1}},
        {"version", SerialValue{true}},
        {"kind", SerialValue{1}},
        {"engineVersionRange", SerialValue{1}},
        {"platforms", SerialValue{"Windows"}},
        {"dependencies", SerialValue{true}},
        {"enabledByDefault", SerialValue{"yes"}},
        {"description", SerialValue{1}},
    };
    for (const Bad& bad : cases) {
        INFO(bad.field);
        SerialValue obj = Valid();
        obj[bad.field] = bad.value;
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, bad.field));
    }
}

TEST_CASE("ParseManifest rejects illegal plugin ids", "[plugins][manifest]") {
    const char* ids[] = {"", "Luma Sample", "Luma/Sample", ".Luma", "Luma.", "Luma..Sample",
                         "Luma-Sample", "Luma.Sample "};
    for (const char* id : ids) {
        INFO(id);
        SerialValue obj = Valid();
        obj["id"] = SerialValue{id};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "id"));
    }
}

TEST_CASE("ParseManifest rejects invalid versions and ranges", "[plugins][manifest]") {
    {
        SerialValue obj = Valid();
        obj["version"] = SerialValue{"01.2.3"};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "version"));
    }
    const char* ranges[] = {"", "nope", ">=1.2", ">=1.0.0 || <2.0.0", "^", "1.2.3 - 2.0.0"};
    for (const char* range : ranges) {
        INFO(range);
        SerialValue obj = Valid();
        obj["engineVersionRange"] = SerialValue{range};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "engineVersionRange"));
    }
}

TEST_CASE("ParseManifest rejects unknown fields, bad kind, and unknown platforms",
          "[plugins][manifest]") {
    {
        SerialValue obj = Valid();
        obj["verson"] = SerialValue{"1.0.0"};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "verson"));
    }
    {
        SerialValue obj = Valid();
        obj["kind"] = SerialValue{"Both"};
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "kind"));
    }
    {
        SerialValue obj = Valid();
        SerialValue platforms = SerialValue::MakeArray();
        platforms.PushBack(SerialValue{"Solaris"});
        obj["platforms"] = std::move(platforms);
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "platforms[0]"));
    }
    {
        SerialValue obj = Valid();
        SerialValue platforms = SerialValue::MakeArray();
        obj["platforms"] = std::move(platforms);
        std::vector<ManifestError> errors;
        REQUIRE_FALSE(Parse(obj, errors).has_value());
        REQUIRE(HasField(errors, "platforms"));
    }
}

TEST_CASE("SatisfiesRange implements exact, comparison, caret, tilde, and AND",
          "[plugins][manifest]") {
    REQUIRE(SatisfiesRange(V("1.2.3"), "1.2.3"));
    REQUIRE_FALSE(SatisfiesRange(V("1.2.4"), "1.2.3"));
    REQUIRE(SatisfiesRange(V("1.2.3"), "=1.2.3"));
    REQUIRE(SatisfiesRange(V("1.2.3"), "==1.2.3"));
    REQUIRE(SatisfiesRange(V("1.2.4"), ">1.2.3"));
    REQUIRE(SatisfiesRange(V("1.2.3"), ">= 1.2.3"));
    REQUIRE(SatisfiesRange(V("1.2.3"), "<=1.2.3"));
    REQUIRE_FALSE(SatisfiesRange(V("1.2.4"), "<1.2.4"));
    REQUIRE(SatisfiesRange(V("1.2.3"), "<1.2.4"));
    REQUIRE(SatisfiesRange(V("1.2.3"), ">=1.2.0 <2.0.0"));
    REQUIRE_FALSE(SatisfiesRange(V("2.0.0"), ">=1.2.0 <2.0.0"));
    REQUIRE_FALSE(SatisfiesRange(V("1.1.9"), ">=1.2.0 <2.0.0"));

    REQUIRE(SatisfiesRange(V("1.5.0"), "^1.2.3"));
    REQUIRE(SatisfiesRange(V("1.2.3"), "^1.2.3"));
    REQUIRE_FALSE(SatisfiesRange(V("2.0.0"), "^1.2.3"));
    REQUIRE(SatisfiesRange(V("0.2.9"), "^0.2.3"));
    REQUIRE_FALSE(SatisfiesRange(V("0.3.0"), "^0.2.3"));
    REQUIRE(SatisfiesRange(V("0.0.3"), "^0.0.3"));
    REQUIRE_FALSE(SatisfiesRange(V("0.0.4"), "^0.0.3"));

    REQUIRE(SatisfiesRange(V("1.2.9"), "~1.2.3"));
    REQUIRE(SatisfiesRange(V("1.2.3"), "~1.2.3"));
    REQUIRE_FALSE(SatisfiesRange(V("1.3.0"), "~1.2.3"));

    // A pre-release matches only when a comparator names that major.minor.patch
    // with a pre-release tag.
    REQUIRE_FALSE(SatisfiesRange(V("1.2.4-alpha"), ">=1.2.3 <2.0.0"));
    REQUIRE(SatisfiesRange(V("1.2.3-beta"), ">=1.2.3-alpha <1.3.0"));
    REQUIRE_FALSE(SatisfiesRange(V("1.2.4-alpha"), ">=1.2.3-alpha <1.3.0"));
    REQUIRE(SatisfiesRange(V("1.2.3-alpha"), "1.2.3-alpha"));

    std::string error = "stale";
    REQUIRE_FALSE(SatisfiesRange(V("1.0.0"), ">=2.0.0", &error));
    REQUIRE(error.empty());
    REQUIRE_FALSE(SatisfiesRange(V("1.0.0"), "not-a-range", &error));
    REQUIRE_FALSE(error.empty());
}
