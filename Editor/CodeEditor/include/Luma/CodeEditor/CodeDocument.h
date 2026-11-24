#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/CodeEditor/CodeLanguage.h"
#include "Luma/Core/Types.h"

// One open file in the Code Editor: its text as lines, language and view state.

namespace Luma::CodeEditor {

// A highlighted span of a line (byte range + kind).
enum class TokenKind : u8 { Plain, Keyword, String, Number, Comment, Preprocessor, Function, Type };
struct Token {
    usize begin = 0;
    usize end = 0;
    TokenKind kind = TokenKind::Plain;
};

// Splits one line into tokens for colouring. Line-based: block comments and
// multi-line strings are not tracked across lines yet.
std::vector<Token> TokenizeLine(CodeLanguage language, std::string_view line);

struct CodeDocument {
    std::string name;             // tab title ("Untitled1.lua")
    std::filesystem::path path;   // empty until saved
    CodeLanguage language = CodeLanguage::Cpp;
    std::vector<std::string> lines{std::string{}};
    bool dirty = false;
    // View state.
    usize caretLine = 0;
    usize caretColumn = 0;
    f32 scrollY = 0.0f;

    void SetText(std::string_view text);
    std::string Text() const;
};

}  // namespace Luma::CodeEditor
