#include <catch2/catch_test_macros.hpp>

#include "Luma/CodeEditor/CodeDocument.h"
#include "Luma/CodeEditor/CodeEditorWindow.h"
#include "Luma/CodeEditor/CodeLanguage.h"

using namespace Luma;
using namespace Luma::CodeEditor;

TEST_CASE("Code languages map from file extensions", "[codeeditor]") {
    CodeLanguage l{};
    REQUIRE(LanguageFromExtension(".HPP", &l));
    REQUIRE(l == CodeLanguage::Cpp);
    REQUIRE(LanguageFromExtension(".lua", &l));
    REQUIRE(l == CodeLanguage::Lua);
    REQUIRE(LanguageFromExtension(".py", &l));
    REQUIRE(l == CodeLanguage::Python);
    REQUIRE_FALSE(LanguageFromExtension(".txt", &l));
}

TEST_CASE("Tokenizer colours keywords, strings, calls and comments", "[codeeditor]") {
    const std::string line = "local x = Foo(\"hi\") -- note";
    const auto toks = TokenizeLine(CodeLanguage::Lua, line);
    auto kindOf = [&](std::string_view word) {
        for (const Token& t : toks) {
            if (line.substr(t.begin, t.end - t.begin) == word) return t.kind;
        }
        return TokenKind::Plain;
    };
    REQUIRE(kindOf("local") == TokenKind::Keyword);
    REQUIRE(kindOf("Foo") == TokenKind::Function);
    REQUIRE(kindOf("\"hi\"") == TokenKind::String);
    REQUIRE(kindOf("-- note") == TokenKind::Comment);
}

TEST_CASE("Symbols nest methods under their class, with the comment above", "[codeeditor]") {
    CodeDocument doc;
    doc.language = CodeLanguage::Cpp;
    doc.SetText("// Moves the player.\nclass Player {\npublic:\n    // Per frame.\n    void Tick(float dt);\n"
                "    float speed = 1.0f;\n};\n\nvoid Player::Tick(float dt) {\n    if (dt > 0) {}\n}\n");
    const auto symbols = FindSymbols(doc);
    REQUIRE(symbols.size() == 3);
    REQUIRE(symbols[0].kind == CodeSymbol::Kind::Class);
    REQUIRE(symbols[0].name == "Player");
    REQUIRE(CommentAbove(doc, symbols[0].line) == "Moves the player.");
    REQUIRE(symbols[1].kind == CodeSymbol::Kind::Variable);
    REQUIRE(symbols[1].name == "speed");
    REQUIRE(symbols[1].depth == 1);
    REQUIRE(symbols[2].name == "Player::Tick");
    REQUIRE(symbols[2].depth == 0);

    CodeDocument py;
    py.language = CodeLanguage::Python;
    py.SetText("class A:\n    def run(self):\n        pass\n");
    const auto ps = FindSymbols(py);
    REQUIRE(ps.size() == 2);
    REQUIRE(ps[1].name == "run");
    REQUIRE(ps[1].depth == 1);
}
