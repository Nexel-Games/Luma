#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Luma/CodeEditor/CodeDocument.h"
#include "Luma/CodeEditor/CodeLanguage.h"
#include "Luma/Core/Registration.h"
#include "Luma/Editor/MenuRegistry.h"
#include "Luma/Slate/Context.h"

// Tools → Code Editor: the engine's code editor window (floats, docks into the
// window row like the asset editors). Edits C++, Lua and Python.
//
//   toolbar      Save / Build / Run / Debug / Stop / Format, file search, language
//   left         Project Explorer (the project's code files)
//   centre       file tabs + the editor surface
//   right        Code Inspector (file properties, symbols, documentation)
//   bottom       Output / Problems / Terminal / Log

namespace Luma::CodeEditor {

inline constexpr const char* kCodeEditorWindowId = "code-editor";

// A symbol found in a file (class / function / variable) for the inspector.
struct CodeSymbol {
    enum class Kind : u8 { Class, Function, Variable };
    Kind kind = Kind::Function;
    std::string name;
    usize line = 0;
    usize depth = 0;  // nesting (methods under their class)
};
std::vector<CodeSymbol> FindSymbols(const CodeDocument& doc);
// The comment block directly above `line` (comment markers stripped).
std::string CommentAbove(const CodeDocument& doc, usize line);

class CodeEditorWindow {
public:
    CodeEditorWindow();

    void Draw(Slate::Context& ui, const Slate::Rect& area);

    // The project whose Source / Scripts / Plugins the explorer shows.
    void SetProjectRoot(std::filesystem::path root);

    // A new untitled file in `language`, made the active tab.
    void NewDocument(CodeLanguage language);
    // Opens `file` (or focuses its tab); false when it can't be read.
    bool OpenFile(const std::filesystem::path& file);
    // Writes the active file. An untitled file is saved into the project
    // (Source/ for C++, Scripts/ for Lua and Python).
    bool SaveActive();
    void CloseDocument(usize index);
    const std::vector<CodeDocument>& Documents() const { return m_documents; }
    usize ActiveIndex() const { return m_active; }
    CodeLanguage CurrentLanguage() const { return m_language; }
    void SetLanguage(CodeLanguage language);

    // Artwork + branding (set by the host).
    TextureHandle logo = 0;
    const Slate::Font* brandFont = nullptr;  // "LUMA CODE" mark (Rubik Iso)
    // Window actions (the host's window manager).
    std::function<void()> onDock;
    std::function<void()> onFloat;
    std::function<void()> onMinimize;
    std::function<void()> onClose;

private:
    struct TreeNode {
        std::filesystem::path path;
        std::string name;
        bool folder = false;
        std::vector<TreeNode> children;
    };

    void DrawMenuBar(Slate::Context& ui, const Slate::Rect& bar);
    void DrawToolbar(Slate::Context& ui, const Slate::Rect& bar);
    void DrawExplorer(Slate::Context& ui, const Slate::Rect& side);
    void DrawTreeNode(Slate::Context& ui, const Slate::Rect& side, const TreeNode& node, int depth, f32& y);
    void DrawTabs(Slate::Context& ui, const Slate::Rect& strip);
    void DrawEditor(Slate::Context& ui, const Slate::Rect& body);
    void DrawInspector(Slate::Context& ui, const Slate::Rect& side);
    void DrawOutput(Slate::Context& ui, const Slate::Rect& panel);
    void Log(std::string line, bool error = false);
    void Rescan();
    CodeDocument* Active();

    std::vector<CodeDocument> m_documents;
    usize m_active = 0;
    CodeLanguage m_language = CodeLanguage::Cpp;
    int m_untitled[3] = {0, 0, 0};  // per-language "UntitledN" counters

    std::filesystem::path m_projectRoot;
    TreeNode m_tree;
    bool m_treeDirty = true;
    std::set<std::string> m_collapsed;  // folder paths the user closed
    std::string m_search;
    f32 m_treeScroll = 0.0f;

    bool m_showExplorer = true;
    bool m_showInspector = true;
    bool m_showOutput = true;
    int m_outputTab = 0;  // Output, Problems, Terminal, Log
    struct LogLine {
        std::string text;
        bool error = false;
    };
    std::vector<LogLine> m_output;
};

// Adds Tools → Code Editor, which calls `openWindow`.
void RegisterCodeEditorWindow(RegistrationSet& registrations, Editor::MenuRegistry& menus,
                              std::function<void()> openWindow);

}  // namespace Luma::CodeEditor
