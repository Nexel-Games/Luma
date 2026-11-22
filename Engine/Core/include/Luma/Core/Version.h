#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

#include "Luma/Core/Types.h"
#include "Luma/Core/VersionConfig.h"  // generated: LUMA_VERSION_MAJOR/MINOR/PATCH

namespace Luma {

// A Semantic Versioning 2.0.0 version (https://semver.org): MAJOR.MINOR.PATCH with
// an optional pre-release ("-rc.1"). Build metadata ("+build.7") is accepted by
// Parse but not stored, because it never affects precedence.
struct SemVer {
    u32 major = 0;
    u32 minor = 0;
    u32 patch = 0;
    std::string pre;  // dot-separated pre-release identifiers, empty for a release

    // Strict parse; returns nullopt for anything that is not a valid SemVer
    // (missing parts, leading zeros, empty identifiers, whitespace, overflow, ...).
    static std::optional<SemVer> Parse(std::string_view text);

    // "1.2.3" or "1.2.3-rc.1".
    std::string ToString() const;

    // SemVer precedence: numeric major/minor/patch, then a pre-release sorts
    // before the release, and pre-release identifiers compare numerically when
    // both are numeric, numeric before alphanumeric, otherwise ASCII order, and a
    // shorter identifier list sorts first when all shared identifiers are equal.
    std::strong_ordering operator<=>(const SemVer& other) const;
    bool operator==(const SemVer& other) const;
};

// The running engine's version (from the CMake project version).
SemVer EngineVersion();

// Human-readable version string, e.g. "Luma Engine 0.1.0".
const char* EngineVersionString();

}  // namespace Luma
