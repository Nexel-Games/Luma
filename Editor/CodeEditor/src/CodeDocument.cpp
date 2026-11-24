#include "Luma/CodeEditor/CodeDocument.h"

#include <cctype>

namespace Luma::CodeEditor {
namespace {

bool IsIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

}  // namespace

std::vector<Token> TokenizeLine(CodeLanguage language, std::string_view line) {
    std::vector<Token> out;
    const std::string_view comment = LineComment(language);
    usize i = 0;
    auto push = [&](usize b, usize e, TokenKind k) {
        if (e > b) out.push_back({b, e, k});
    };
    // A C++ preprocessor line is one token (after leading whitespace).
    if (language == CodeLanguage::Cpp) {
        usize s = 0;
        while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
        if (s < line.size() && line[s] == '#') {
            usize e = s + 1;
            while (e < line.size() && IsIdent(line[e])) ++e;
            push(0, s, TokenKind::Plain);
            push(s, e, TokenKind::Preprocessor);
            i = e;
        }
    }
    usize plainStart = i;
    auto flushPlain = [&](usize upTo) { push(plainStart, upTo, TokenKind::Plain); };
    while (i < line.size()) {
        const char c = line[i];
        if (line.substr(i, comment.size()) == comment) {
            flushPlain(i);
            push(i, line.size(), TokenKind::Comment);
            return out;
        }
        if (c == '"' || c == '\'') {
            flushPlain(i);
            usize e = i + 1;
            while (e < line.size() && line[e] != c) e += (line[e] == '\\' && e + 1 < line.size()) ? 2 : 1;
            e = std::min(e + 1, line.size());
            push(i, e, TokenKind::String);
            i = plainStart = e;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) && (i == 0 || !IsIdent(line[i - 1]))) {
            flushPlain(i);
            usize e = i + 1;
            while (e < line.size() && (IsIdent(line[e]) || line[e] == '.')) ++e;
            push(i, e, TokenKind::Number);
            i = plainStart = e;
            continue;
        }
        if (IsIdent(c) && (i == 0 || !IsIdent(line[i - 1]))) {
            usize e = i + 1;
            while (e < line.size() && IsIdent(line[e])) ++e;
            usize after = e;
            while (after < line.size() && line[after] == ' ') ++after;
            TokenKind kind = TokenKind::Plain;
            if (IsKeyword(language, line.substr(i, e - i))) {
                kind = TokenKind::Keyword;
            } else if (after < line.size() && line[after] == '(') {
                kind = TokenKind::Function;  // a call or a definition
            } else if (std::isupper(static_cast<unsigned char>(c)) && e - i > 1) {
                kind = TokenKind::Type;  // PascalCase names: types, namespaces
            }
            if (kind != TokenKind::Plain) {
                flushPlain(i);
                push(i, e, kind);
                plainStart = e;
            }
            i = e;
            continue;
        }
        ++i;
    }
    flushPlain(line.size());
    return out;
}

void CodeDocument::SetText(std::string_view text) {
    lines.clear();
    std::string current;
    for (char c : text) {
        if (c == '\r') continue;
        if (c == '\t') {  // the editor indents with 4 spaces
            current.append(4, ' ');
            continue;
        }
        if (c == '\n') {
            lines.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    lines.push_back(std::move(current));
    caretLine = caretColumn = 0;
    scrollY = 0.0f;
}

std::string CodeDocument::Text() const {
    std::string out;
    for (usize i = 0; i < lines.size(); ++i) {
        if (i) out.push_back('\n');
        out += lines[i];
    }
    return out;
}

}  // namespace Luma::CodeEditor
