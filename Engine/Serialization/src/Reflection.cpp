#include "Luma/Serialization/Reflection.h"

namespace Luma {

std::string PrettyPropertyName(std::string_view name) {
    std::string out;
    out.reserve(name.size() + 8);
    bool startWord = true;
    char prev = 0;
    for (char c : name) {
        if (c == '_' || c == ' ') {
            startWord = true;
            prev = c;
            continue;
        }
        const bool upper = c >= 'A' && c <= 'Z';
        const bool digit = c >= '0' && c <= '9';
        const bool prevLower = prev >= 'a' && prev <= 'z';
        const bool prevDigit = prev >= '0' && prev <= '9';
        // A new word starts at a lower->Upper change and at a letter<->digit change.
        if ((upper && prevLower) || (digit && !prevDigit && prev != 0) ||
            (!digit && prevDigit)) {
            startWord = true;
        }
        if (startWord && !out.empty()) out.push_back(' ');
        if (startWord && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        out.push_back(c);
        startWord = false;
        prev = c;
    }
    return out;
}

}  // namespace Luma
