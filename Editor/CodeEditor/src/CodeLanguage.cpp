#include "Luma/CodeEditor/CodeLanguage.h"

#include <algorithm>
#include <cctype>

namespace Luma::CodeEditor {
namespace {

constexpr std::string_view kCppKeywords[] = {
    "alignas", "alignof", "auto", "bool", "break", "case", "catch", "char", "class", "const",
    "consteval", "constexpr", "constinit", "continue", "co_await", "co_return", "co_yield",
    "decltype", "default", "delete", "do", "double", "else", "enum", "explicit", "export",
    "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable",
    "namespace", "new", "noexcept", "nullptr", "operator", "override", "private", "protected",
    "public", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast",
    "struct", "switch", "template", "this", "throw", "true", "try", "typedef", "typename", "union",
    "unsigned", "using", "virtual", "void", "volatile", "while", "final", "concept", "requires",
    "reinterpret_cast", "dynamic_cast", "const_cast", "#include", "#pragma", "#define", "#if",
    "#ifdef", "#ifndef", "#endif", "#else"};
constexpr std::string_view kLuaKeywords[] = {"and",   "break", "do",     "else", "elseif", "end",
                                             "false", "for",   "function", "goto", "if",   "in",
                                             "local", "nil",   "not",    "or",   "repeat", "return",
                                             "then",  "true",  "until",  "while"};
constexpr std::string_view kPythonKeywords[] = {
    "False", "None",   "True",  "and",    "as",   "assert", "async",    "await", "break",
    "class", "continue", "def", "del",    "elif", "else",   "except",   "finally", "for",
    "from",  "global", "if",    "import", "in",   "is",     "lambda",   "nonlocal", "not",
    "or",    "pass",   "raise", "return", "try",  "while",  "with",     "yield", "self"};

template <usize N>
bool Contains(const std::string_view (&words)[N], std::string_view word) {
    return std::find(std::begin(words), std::end(words), word) != std::end(words);
}

}  // namespace

std::string_view LanguageName(CodeLanguage language) {
    switch (language) {
        case CodeLanguage::Cpp: return "C++";
        case CodeLanguage::Lua: return "Lua";
        case CodeLanguage::Python: return "Python";
    }
    return "C++";
}

std::string_view LanguageExtension(CodeLanguage language) {
    switch (language) {
        case CodeLanguage::Cpp: return ".cpp";
        case CodeLanguage::Lua: return ".lua";
        case CodeLanguage::Python: return ".py";
    }
    return ".cpp";
}

bool LanguageFromExtension(std::string_view extension, CodeLanguage* out) {
    std::string ext(extension);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    CodeLanguage language;
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" || ext == ".hpp" || ext == ".inl") {
        language = CodeLanguage::Cpp;
    } else if (ext == ".lua") {
        language = CodeLanguage::Lua;
    } else if (ext == ".py") {
        language = CodeLanguage::Python;
    } else {
        return false;
    }
    if (out) *out = language;
    return true;
}

std::string_view LineComment(CodeLanguage language) {
    switch (language) {
        case CodeLanguage::Cpp: return "//";
        case CodeLanguage::Lua: return "--";
        case CodeLanguage::Python: return "#";
    }
    return "//";
}

bool IsKeyword(CodeLanguage language, std::string_view word) {
    switch (language) {
        case CodeLanguage::Cpp: return Contains(kCppKeywords, word);
        case CodeLanguage::Lua: return Contains(kLuaKeywords, word);
        case CodeLanguage::Python: return Contains(kPythonKeywords, word);
    }
    return false;
}

std::string StarterText(CodeLanguage language) {
    switch (language) {
        case CodeLanguage::Cpp:
            return "#include \"Luma/Core/Log.h\"\n"
                   "\n"
                   "namespace Game {\n"
                   "\n"
                   "// Called once when the object enters play.\n"
                   "void OnBeginPlay() {\n"
                   "    LUMA_USER_INFO(\"Game\", \"Hello from C++\");\n"
                   "}\n"
                   "\n"
                   "// Called every frame; dt is in seconds.\n"
                   "void OnTick(float dt) {\n"
                   "    (void)dt;\n"
                   "}\n"
                   "\n"
                   "}  // namespace Game\n";
        case CodeLanguage::Lua:
            return "-- Called once when the object enters play.\n"
                   "function OnBeginPlay()\n"
                   "    print(\"Hello from Lua\")\n"
                   "end\n"
                   "\n"
                   "-- Called every frame; dt is in seconds.\n"
                   "function OnTick(dt)\n"
                   "    local speed = 90\n"
                   "end\n";
        case CodeLanguage::Python:
            return "# Called once when the object enters play.\n"
                   "def on_begin_play(self):\n"
                   "    print(\"Hello from Python\")\n"
                   "\n"
                   "\n"
                   "# Called every frame; dt is in seconds.\n"
                   "def on_tick(self, dt):\n"
                   "    speed = 90\n";
    }
    return {};
}

}  // namespace Luma::CodeEditor
