#include "Luma/Slate/NumericText.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace Luma::Slate {
namespace {

// Recursive-descent parser:
//   expr   := term (('+' | '-') term)*
//   term   := unary (('*' | '/') unary)*
//   unary  := ('-' | '+') unary | primary
//   primary:= number | '(' expr ')'
class Parser {
public:
    explicit Parser(std::string_view s) : m_s(s) {}

    std::optional<f64> Parse() {
        auto v = Expr();
        Skip();
        if (!v || m_pos != m_s.size()) return std::nullopt;
        return v;
    }

private:
    void Skip() {
        while (m_pos < m_s.size() && (m_s[m_pos] == ' ' || m_s[m_pos] == '\t')) ++m_pos;
    }
    bool Eat(char c) {
        Skip();
        if (m_pos < m_s.size() && m_s[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    std::optional<f64> Expr() {
        auto lhs = Term();
        while (lhs) {
            if (Eat('+')) {
                auto rhs = Term();
                if (!rhs) return std::nullopt;
                *lhs += *rhs;
            } else if (Eat('-')) {
                auto rhs = Term();
                if (!rhs) return std::nullopt;
                *lhs -= *rhs;
            } else {
                break;
            }
        }
        return lhs;
    }

    std::optional<f64> Term() {
        auto lhs = Unary();
        while (lhs) {
            if (Eat('*')) {
                auto rhs = Unary();
                if (!rhs) return std::nullopt;
                *lhs *= *rhs;
            } else if (Eat('/')) {
                auto rhs = Unary();
                if (!rhs || *rhs == 0.0) return std::nullopt;
                *lhs /= *rhs;
            } else {
                break;
            }
        }
        return lhs;
    }

    std::optional<f64> Unary() {
        if (Eat('-')) {
            auto v = Unary();
            if (!v) return std::nullopt;
            return -*v;
        }
        if (Eat('+')) return Unary();
        return Primary();
    }

    std::optional<f64> Primary() {
        if (Eat('(')) {
            auto v = Expr();
            if (!v || !Eat(')')) return std::nullopt;
            return v;
        }
        Skip();
        const char* begin = m_s.data() + m_pos;
        const char* end = m_s.data() + m_s.size();
        // from_chars rejects a leading '.', so accept ".5" by parsing "0.5".
        f64 value = 0.0;
        if (begin < end && *begin == '.') {
            std::string withZero = "0" + std::string(begin, end);
            auto [p, ec] = std::from_chars(withZero.data(), withZero.data() + withZero.size(),
                                           value, std::chars_format::general);
            if (ec != std::errc{}) return std::nullopt;
            m_pos += static_cast<usize>(p - withZero.data()) - 1;
            return value;
        }
        auto [p, ec] = std::from_chars(begin, end, value, std::chars_format::general);
        if (ec != std::errc{}) return std::nullopt;
        m_pos += static_cast<usize>(p - begin);
        return value;
    }

    std::string_view m_s;
    usize m_pos = 0;
};

}  // namespace

std::optional<f64> EvaluateNumericExpression(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    if (!text.empty() && text.front() == '=') text.remove_prefix(1);
    if (text.empty()) return std::nullopt;
    auto v = Parser(text).Parse();
    if (!v || !std::isfinite(*v)) return std::nullopt;
    return v;
}

std::string FormatNumber(f64 value, int minFractionalDigits, int maxFractionalDigits) {
    maxFractionalDigits = std::clamp(maxFractionalDigits, 0, 12);
    minFractionalDigits = std::clamp(minFractionalDigits, 0, maxFractionalDigits);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", maxFractionalDigits, value);
    std::string s = buf;
    if (const usize dot = s.find('.'); dot != std::string::npos) {
        const usize keep = dot + 1 + static_cast<usize>(minFractionalDigits);
        while (s.size() > keep && s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    // "-0", "-0.0": the value rounded to zero.
    if (s.front() == '-' && s.find_first_not_of("-0.") == std::string::npos) s.erase(0, 1);
    return s;
}

}  // namespace Luma::Slate
