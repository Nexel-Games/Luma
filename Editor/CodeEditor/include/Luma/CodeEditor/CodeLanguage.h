#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Types.h"

// The languages the Code Editor edits. Each carries what the editor needs to
// know about it: display name, file extension, comment syntax, keywords and a
// starter template for a new file.

namespace Luma::CodeEditor {

enum class CodeLanguage : u8 { Cpp, Lua, Python };

inline constexpr std::array<CodeLanguage, 3> kAllLanguages = {CodeLanguage::Cpp, CodeLanguage::Lua,
                                                              CodeLanguage::Python};

std::string_view LanguageName(CodeLanguage language);       // "C++", "Lua", "Python"
std::string_view LanguageExtension(CodeLanguage language);  // ".cpp", ".lua", ".py"
// Language for a file extension (".h", ".hpp", ".cpp", ".lua", ".py"); false when unknown.
bool LanguageFromExtension(std::string_view extension, CodeLanguage* out);
// Line-comment prefix ("//", "--", "#").
std::string_view LineComment(CodeLanguage language);
bool IsKeyword(CodeLanguage language, std::string_view word);
// The text of a new, untitled file in `language`.
std::string StarterText(CodeLanguage language);

}  // namespace Luma::CodeEditor
