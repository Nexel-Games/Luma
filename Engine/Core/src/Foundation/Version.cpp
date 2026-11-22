#include "Luma/Core/Version.h"

#include <charconv>
#include <vector>

namespace Luma {

namespace {

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

bool IsIdentChar(char c) {
    return IsDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

bool AllDigits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!IsDigit(c)) return false;
    }
    return true;
}

// A numeric version part: digits only, no leading zero (except "0"), fits in u32.
std::optional<u32> ParseNumber(std::string_view s) {
    if (!AllDigits(s)) return std::nullopt;
    if (s.size() > 1 && s.front() == '0') return std::nullopt;
    u32 value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
    return value;
}

std::vector<std::string_view> Split(std::string_view s, char sep) {
    std::vector<std::string_view> parts;
    usize start = 0;
    while (true) {
        const usize pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            parts.push_back(s.substr(start));
            return parts;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
}

// Pre-release / build identifiers: non-empty, [0-9A-Za-z-]. Pre-release numeric
// identifiers may not have leading zeros.
bool ValidIdentifiers(std::string_view s, bool rejectLeadingZeros) {
    if (s.empty()) return false;
    for (std::string_view id : Split(s, '.')) {
        if (id.empty()) return false;
        for (char c : id) {
            if (!IsIdentChar(c)) return false;
        }
        if (rejectLeadingZeros && AllDigits(id) && id.size() > 1 && id.front() == '0') {
            return false;
        }
    }
    return true;
}

std::strong_ordering CompareIdentifier(std::string_view a, std::string_view b) {
    const bool an = AllDigits(a);
    const bool bn = AllDigits(b);
    if (an && bn) {
        // Same-length digit strings without leading zeros compare lexically;
        // a longer one is larger. (Avoids overflow for very long identifiers.)
        if (a.size() != b.size()) return a.size() <=> b.size();
        return a.compare(b) <=> 0;
    }
    if (an) return std::strong_ordering::less;  // numeric < alphanumeric
    if (bn) return std::strong_ordering::greater;
    return a.compare(b) <=> 0;
}

}  // namespace

std::optional<SemVer> SemVer::Parse(std::string_view text) {
    std::string_view core = text;
    std::string_view pre;
    std::string_view build;

    if (const usize plus = core.find('+'); plus != std::string_view::npos) {
        build = core.substr(plus + 1);
        core = core.substr(0, plus);
        if (!ValidIdentifiers(build, /*rejectLeadingZeros=*/false)) return std::nullopt;
    }
    if (const usize dash = core.find('-'); dash != std::string_view::npos) {
        pre = core.substr(dash + 1);
        core = core.substr(0, dash);
        if (!ValidIdentifiers(pre, /*rejectLeadingZeros=*/true)) return std::nullopt;
    }

    const std::vector<std::string_view> parts = Split(core, '.');
    if (parts.size() != 3) return std::nullopt;
    auto major = ParseNumber(parts[0]);
    auto minor = ParseNumber(parts[1]);
    auto patch = ParseNumber(parts[2]);
    if (!major || !minor || !patch) return std::nullopt;

    SemVer v;
    v.major = *major;
    v.minor = *minor;
    v.patch = *patch;
    v.pre = std::string(pre);
    return v;
}

std::string SemVer::ToString() const {
    std::string s = std::to_string(major) + "." + std::to_string(minor) + "." +
                    std::to_string(patch);
    if (!pre.empty()) s += "-" + pre;
    return s;
}

std::strong_ordering SemVer::operator<=>(const SemVer& other) const {
    if (auto c = major <=> other.major; c != 0) return c;
    if (auto c = minor <=> other.minor; c != 0) return c;
    if (auto c = patch <=> other.patch; c != 0) return c;
    if (pre.empty() || other.pre.empty()) {
        // A release ranks above any pre-release of the same version.
        if (pre.empty() && other.pre.empty()) return std::strong_ordering::equal;
        return pre.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
    }
    const std::vector<std::string_view> a = Split(pre, '.');
    const std::vector<std::string_view> b = Split(other.pre, '.');
    const usize n = a.size() < b.size() ? a.size() : b.size();
    for (usize i = 0; i < n; ++i) {
        if (auto c = CompareIdentifier(a[i], b[i]); c != 0) return c;
    }
    return a.size() <=> b.size();
}

bool SemVer::operator==(const SemVer& other) const {
    return (*this <=> other) == std::strong_ordering::equal;
}

SemVer EngineVersion() {
    return SemVer{LUMA_VERSION_MAJOR, LUMA_VERSION_MINOR, LUMA_VERSION_PATCH, {}};
}

const char* EngineVersionString() { return "Luma Engine " LUMA_VERSION_STRING; }

}  // namespace Luma
