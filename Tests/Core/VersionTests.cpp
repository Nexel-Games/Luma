#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Luma/Core/Version.h"

using Luma::SemVer;

TEST_CASE("SemVer parses major.minor.patch", "[core][version]") {
    auto v = SemVer::Parse("1.22.333");
    REQUIRE(v.has_value());
    REQUIRE(v->major == 1);
    REQUIRE(v->minor == 22);
    REQUIRE(v->patch == 333);
    REQUIRE(v->pre.empty());
    REQUIRE(v->ToString() == "1.22.333");
}

TEST_CASE("SemVer parses pre-release and ignores build metadata", "[core][version]") {
    auto v = SemVer::Parse("2.0.0-rc.1+build.7");
    REQUIRE(v.has_value());
    REQUIRE(v->major == 2);
    REQUIRE(v->pre == "rc.1");
    REQUIRE(v->ToString() == "2.0.0-rc.1");

    auto b = SemVer::Parse("1.0.0+20261121");
    REQUIRE(b.has_value());
    REQUIRE(b->pre.empty());
    REQUIRE(*b == SemVer{1, 0, 0, {}});
}

TEST_CASE("SemVer rejects invalid strings", "[core][version]") {
    const char* bad[] = {"",        "1",         "1.2",      "1.2.3.4", "a.b.c",   "01.2.3",
                         "1.02.3",  "1.2.03",    "1.2.3-",   "1.2.3+",  "1.2.3-a..b",
                         "1.2.3-01", "-1.2.3",   " 1.2.3",   "1.2.3 ",  "1.2.3-a$b",
                         "99999999999.0.0",      "1..3",     "1.2.3-rc+"};
    for (const char* s : bad) {
        INFO(s);
        REQUIRE_FALSE(SemVer::Parse(s).has_value());
    }
}

TEST_CASE("SemVer orders numerically and by pre-release precedence", "[core][version]") {
    auto P = [](const char* s) { return *SemVer::Parse(s); };
    REQUIRE(P("1.0.0") < P("2.0.0"));
    REQUIRE(P("2.0.0") < P("2.1.0"));
    REQUIRE(P("2.1.0") < P("2.1.1"));
    REQUIRE(P("1.9.0") < P("1.10.0"));
    // A pre-release sorts before its release.
    REQUIRE(P("1.0.0-alpha") < P("1.0.0"));
    // The SemVer 2.0 precedence example chain.
    const char* chain[] = {"1.0.0-alpha",      "1.0.0-alpha.1", "1.0.0-alpha.beta",
                           "1.0.0-beta",       "1.0.0-beta.2",  "1.0.0-beta.11",
                           "1.0.0-rc.1",       "1.0.0"};
    for (std::size_t i = 0; i + 1 < std::size(chain); ++i) {
        INFO(chain[i] << " < " << chain[i + 1]);
        REQUIRE(P(chain[i]) < P(chain[i + 1]));
        REQUIRE(P(chain[i + 1]) > P(chain[i]));
    }
    REQUIRE(P("1.2.3") == P("1.2.3+meta"));
    REQUIRE(P("1.2.3-x") != P("1.2.3"));
}

TEST_CASE("EngineVersion comes from the CMake project version", "[core][version]") {
    const SemVer v = Luma::EngineVersion();
    REQUIRE(v.major == LUMA_VERSION_MAJOR);
    REQUIRE(v.minor == LUMA_VERSION_MINOR);
    REQUIRE(v.patch == LUMA_VERSION_PATCH);
    REQUIRE(Luma::EngineVersionString() == "Luma Engine " + v.ToString());
    auto reparsed = SemVer::Parse(v.ToString());
    REQUIRE(reparsed.has_value());
    REQUIRE(*reparsed == v);
}
