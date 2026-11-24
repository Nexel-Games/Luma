#include "Luma/CodeEditor/CodeEditorWindow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "Luma/Platform/Process.h"
#include "Luma/Slate/Icons.h"
#include "Luma/Slate/Menu.h"

namespace Luma::CodeEditor {
namespace {

using Slate::Color;
using Slate::Rect;

const Color kWhite = Color::RGBA(255, 255, 255, 255);
const Color kSoft = Color::RGBA(205, 210, 220, 255);

// Syntax colours (Luma: warm keywords on a cool, deep background).
Color TokenColor(TokenKind kind) {
    switch (kind) {
        case TokenKind::Keyword: return Color::RGBA(255, 150, 60, 255);
        case TokenKind::String: return Color::RGBA(152, 214, 122, 255);
        case TokenKind::Number: return Color::RGBA(186, 160, 255, 255);
        case TokenKind::Comment: return Color::RGBA(112, 168, 112, 255);
        case TokenKind::Preprocessor: return Color::RGBA(236, 112, 196, 255);
        case TokenKind::Function: return Color::RGBA(250, 214, 110, 255);
        case TokenKind::Type: return Color::RGBA(96, 212, 220, 255);
        case TokenKind::Plain: break;
    }
    return Color::RGBA(232, 236, 242, 255);
}

// Text in `font`, vertically centred in `r` (clipped to it).
void Text(Slate::Context& ui, const Slate::Font& font, const Rect& r, std::string_view text, Color color,
          Slate::Align align = Slate::Align::Left) {
    const f32 w = font.Measure(text).x;
    f32 x = r.x;
    if (align == Slate::Align::Center) x = r.x + (r.w - w) * 0.5f;
    else if (align == Slate::Align::Right) x = r.Right() - w;
    auto& draw = ui.drawList();
    draw.PushClip(r);
    draw.AddText(font, {x, r.y + (r.h - font.LineHeight()) * 0.5f}, text, color);
    draw.PopClip();
}

// The language badge: a small coloured tile with the language's short name.
void LanguageBadge(Slate::Context& ui, const Rect& r, CodeLanguage language) {
    auto& d = ui.drawList();
    std::string_view label = "C++";
    switch (language) {
        case CodeLanguage::Cpp:
            d.AddRectFilledRounded(r, Color::RGBA(122, 80, 226, 255), 3.0f);
            break;
        case CodeLanguage::Lua:
            d.AddRectFilledRounded(r, Color::RGBA(44, 92, 214, 255), 3.0f);
            label = "Lua";
            break;
        case CodeLanguage::Python:
            d.AddRectFilledRounded(r, Color::RGBA(53, 114, 165, 255), 3.0f);
            d.AddRectFilled({r.x + 2.0f, r.Bottom() - 3.0f, r.w - 4.0f, 2.0f}, Color::RGBA(247, 201, 72, 255));
            label = "Py";
            break;
    }
    Slate::Font& f = ui.smallFont();
    Text(ui, f, r, label, kWhite, Slate::Align::Center);
}

std::vector<std::string> LanguageNames() {
    std::vector<std::string> names;
    for (CodeLanguage l : kAllLanguages) names.emplace_back(LanguageName(l));
    return names;
}

std::string Trim(std::string_view s) {
    usize b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

bool IsIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':'; }

bool IsCodeFile(const std::filesystem::path& p) { return LanguageFromExtension(p.extension().string(), nullptr); }

bool SkippedFolder(const std::string& name) {
    static const char* kSkip[] = {"build", "Build", "Binaries", "Intermediate", "Saved", "DerivedDataCache",
                                  ".git", ".vs", ".vscode", ".idea", "node_modules", "__pycache__", "out"};
    for (const char* s : kSkip) {
        if (name == s) return true;
    }
    return !name.empty() && name[0] == '.';
}

constexpr f32 kMenuBarH = 28.0f;
constexpr f32 kToolbarH = 48.0f;
constexpr f32 kExplorerW = 260.0f;
constexpr f32 kInspectorW = 320.0f;
constexpr f32 kOutputH = 170.0f;
constexpr f32 kTabsH = 34.0f;
constexpr f32 kPaneHeaderH = 32.0f;
constexpr f32 kRowH = 24.0f;

// A pane's title strip ("Project Explorer", "Code Inspector").
void PaneHeader(Slate::Context& ui, const Rect& r, Slate::Icon icon, std::string_view title) {
    Slate::Theme& t = ui.theme();
    ui.Panel(r, t.surface1);
    ui.Panel({r.x, r.Bottom() - 1.0f, r.w, 1.0f}, t.separator);
    Slate::DrawIcon(ui, {r.x + 10.0f, r.y + (r.h - 16.0f) * 0.5f, 16.0f, 16.0f}, icon, t.accent);
    Text(ui, ui.subtitleFont(), {r.x + 34.0f, r.y, r.w - 44.0f, r.h}, title, kWhite);
}

// A collapsible-looking section label inside the inspector.
void SectionHeader(Slate::Context& ui, const Rect& r, std::string_view title) {
    Slate::Theme& t = ui.theme();
    ui.Panel(r, Slate::Mix(t.surface1, t.surface2, 0.5f));
    Slate::DrawIcon(ui, {r.x + 8.0f, r.y + (r.h - 12.0f) * 0.5f, 12.0f, 12.0f}, Slate::Icon::ChevronDown, kSoft);
    Text(ui, ui.font(), {r.x + 26.0f, r.y, r.w - 30.0f, r.h}, title, kWhite);
}

}  // namespace

// ---- Symbols -------------------------------------------------------------------

std::vector<CodeSymbol> FindSymbols(const CodeDocument& doc) {
    std::vector<CodeSymbol> out;
    // C++: brace scopes, so methods nest under their class.
    std::vector<bool> scopeIsClass;
    bool pendingClass = false;
    for (usize i = 0; i < doc.lines.size(); ++i) {
        const std::string line = Trim(doc.lines[i]);
        const std::string_view comment = LineComment(doc.language);
        const std::string code = line.rfind(comment, 0) == 0 ? std::string{} : line;
        const usize indent = doc.lines[i].find_first_not_of(' ');
        auto word = [](const std::string& s, usize from) {
            usize e = from;
            while (e < s.size() && IsIdentChar(s[e])) ++e;
            return s.substr(from, e - from);
        };
        switch (doc.language) {
            case CodeLanguage::Python: {
                const usize depth = indent == std::string::npos ? 0 : indent / 4;
                if (code.rfind("class ", 0) == 0) {
                    out.push_back({CodeSymbol::Kind::Class, word(code, 6), i, depth});
                } else if (code.rfind("def ", 0) == 0) {
                    out.push_back({CodeSymbol::Kind::Function, word(code, 4), i, depth});
                }
                break;
            }
            case CodeLanguage::Lua: {
                usize at = std::string::npos;
                if (code.rfind("function ", 0) == 0) at = 9;
                else if (code.rfind("local function ", 0) == 0) at = 15;
                if (at != std::string::npos) {
                    out.push_back({CodeSymbol::Kind::Function, word(code, at), i, 0});
                } else if (code.rfind("local ", 0) == 0 && indent == 0) {
                    out.push_back({CodeSymbol::Kind::Variable, word(code, 6), i, 0});
                }
                break;
            }
            case CodeLanguage::Cpp: {
                const usize depth = static_cast<usize>(std::count(scopeIsClass.begin(), scopeIsClass.end(), true));
                const bool isClass = (code.rfind("class ", 0) == 0 || code.rfind("struct ", 0) == 0) &&
                                     code.back() != ';';
                if (isClass) {
                    const usize at = code[0] == 'c' ? 6 : 7;
                    out.push_back({CodeSymbol::Kind::Class, word(code, at), i, depth});
                    pendingClass = true;
                } else if (!code.empty() && code[0] != '#') {
                    const usize paren = code.find('(');
                    static const char* kNotFunctions[] = {"if", "for", "while", "switch", "return", "catch",
                                                          "sizeof", "else", "do", "case"};
                    if (paren != std::string::npos && paren > 0 && code.back() != ';' && code.back() != ',') {
                        usize e = paren;
                        while (e > 0 && code[e - 1] == ' ') --e;
                        usize b = e;
                        while (b > 0 && IsIdentChar(code[b - 1])) --b;
                        const std::string name = code.substr(b, e - b);
                        const bool keyword = std::any_of(std::begin(kNotFunctions), std::end(kNotFunctions),
                                                         [&](const char* k) { return name == k; });
                        // A definition starts the line with its return type: something before the name.
                        if (!name.empty() && !keyword && b > 0 && code.find('=') > paren) {
                            out.push_back({CodeSymbol::Kind::Function, name, i, depth});
                        }
                    } else if (depth > 0 && code.back() == ';' && paren == std::string::npos &&
                               code.find(' ') != std::string::npos && code.rfind("using", 0) != 0 &&
                               code.rfind("return", 0) != 0 && code.find("public") == std::string::npos) {
                        // A member variable: the last identifier before ';' / '='.
                        std::string decl = code.substr(0, std::min(code.find('='), code.size() - 1));
                        decl = Trim(decl);
                        usize b = decl.size();
                        while (b > 0 && IsIdentChar(decl[b - 1])) --b;
                        if (b < decl.size()) out.push_back({CodeSymbol::Kind::Variable, decl.substr(b), i, depth});
                    }
                }
                for (char c : code) {
                    if (c == '{') {
                        scopeIsClass.push_back(pendingClass);
                        pendingClass = false;
                    } else if (c == '}' && !scopeIsClass.empty()) {
                        scopeIsClass.pop_back();
                    }
                }
                break;
            }
        }
    }
    return out;
}

std::string CommentAbove(const CodeDocument& doc, usize line) {
    const std::string_view comment = LineComment(doc.language);
    std::vector<std::string> parts;
    for (usize i = line; i-- > 0;) {
        const std::string l = Trim(doc.lines[i]);
        if (l.rfind(comment, 0) != 0) break;
        parts.push_back(Trim(std::string_view(l).substr(comment.size())));
    }
    std::string out;
    for (usize i = parts.size(); i-- > 0;) {
        if (!out.empty()) out += ' ';
        out += parts[i];
    }
    return out;
}

// ---- Documents -----------------------------------------------------------------

CodeEditorWindow::CodeEditorWindow() {
    NewDocument(CodeLanguage::Cpp);
    m_output.clear();
}

void CodeEditorWindow::SetProjectRoot(std::filesystem::path root) {
    if (root == m_projectRoot) return;
    m_projectRoot = std::move(root);
    m_treeDirty = true;
}

void CodeEditorWindow::Log(std::string line, bool error) {
    m_output.push_back({std::move(line), error});
    if (m_output.size() > 500) m_output.erase(m_output.begin());
}

void CodeEditorWindow::NewDocument(CodeLanguage language) {
    CodeDocument doc;
    doc.language = language;
    const int n = ++m_untitled[static_cast<int>(language)];
    doc.name = "Untitled" + std::to_string(n) + std::string(LanguageExtension(language));
    doc.SetText(StarterText(language));
    doc.dirty = true;  // never saved
    m_documents.push_back(std::move(doc));
    m_active = m_documents.size() - 1;
    m_language = language;
}

bool CodeEditorWindow::OpenFile(const std::filesystem::path& file) {
    for (usize i = 0; i < m_documents.size(); ++i) {
        if (m_documents[i].path == file) {
            m_active = i;
            m_language = m_documents[i].language;
            return true;
        }
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        Log("Could not open " + file.string(), true);
        return false;
    }
    std::stringstream text;
    text << in.rdbuf();
    CodeDocument doc;
    doc.path = file;
    doc.name = file.filename().string();
    if (!LanguageFromExtension(file.extension().string(), &doc.language)) doc.language = m_language;
    doc.SetText(text.str());
    m_documents.push_back(std::move(doc));
    m_active = m_documents.size() - 1;
    m_language = m_documents.back().language;
    Log("Opened " + file.string());
    return true;
}

bool CodeEditorWindow::SaveActive() {
    CodeDocument* doc = Active();
    if (!doc) return false;
    std::error_code ec;
    if (doc->path.empty()) {
        if (m_projectRoot.empty()) {
            Log("Open a project to save " + doc->name + ".", true);
            return false;
        }
        const std::filesystem::path folder =
            m_projectRoot / (doc->language == CodeLanguage::Cpp ? "Source" : "Scripts");
        std::filesystem::create_directories(folder, ec);
        std::filesystem::path target = folder / doc->name;
        for (int n = 2; std::filesystem::exists(target, ec); ++n) {
            const std::filesystem::path stem = std::filesystem::path(doc->name).stem();
            target = folder / (stem.string() + "_" + std::to_string(n) + std::string(LanguageExtension(doc->language)));
        }
        doc->path = target;
        doc->name = target.filename().string();
        m_treeDirty = true;
    }
    std::ofstream out(doc->path, std::ios::binary | std::ios::trunc);
    const std::string text = doc->Text() + "\n";
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        Log("Could not save " + doc->path.string(), true);
        return false;
    }
    doc->dirty = false;
    Log("Saved " + doc->path.string());
    return true;
}

void CodeEditorWindow::CloseDocument(usize index) {
    if (index >= m_documents.size()) return;
    m_documents.erase(m_documents.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_documents.empty()) {
        m_active = 0;
    } else if (m_active >= m_documents.size() || m_active > index) {
        m_active = m_active > 0 ? m_active - 1 : 0;
    }
    if (CodeDocument* doc = Active()) m_language = doc->language;
}

void CodeEditorWindow::SetLanguage(CodeLanguage language) {
    m_language = language;
    CodeDocument* doc = Active();
    if (!doc || doc->language == language) return;
    // The active file switches language mode; an untitled file's name follows.
    doc->language = language;
    if (doc->path.empty()) {
        const usize dot = doc->name.rfind('.');
        doc->name = doc->name.substr(0, dot) + std::string(LanguageExtension(language));
    }
}

CodeDocument* CodeEditorWindow::Active() {
    return m_active < m_documents.size() ? &m_documents[m_active] : nullptr;
}

void CodeEditorWindow::Rescan() {
    m_treeDirty = false;
    m_tree = {};
    if (m_projectRoot.empty()) return;
    m_tree.path = m_projectRoot;
    m_tree.name = m_projectRoot.filename().string();
    m_tree.folder = true;
    int budget = 4000;
    std::function<void(TreeNode&, int)> scan = [&](TreeNode& node, int depth) {
        std::error_code ec;
        std::vector<std::filesystem::directory_entry> entries;
        for (std::filesystem::directory_iterator it(node.path, ec), end; !ec && it != end; it.increment(ec)) {
            entries.push_back(*it);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            if (a.is_directory() != b.is_directory()) return a.is_directory();
            return a.path().filename().string() < b.path().filename().string();
        });
        for (const auto& e : entries) {
            if (--budget < 0) return;
            const std::string name = e.path().filename().string();
            if (e.is_directory()) {
                if (depth >= 8 || SkippedFolder(name)) continue;
                TreeNode child{e.path(), name, true, {}};
                scan(child, depth + 1);
                if (!child.children.empty()) node.children.push_back(std::move(child));  // code folders only
            } else if (IsCodeFile(e.path())) {
                node.children.push_back({e.path(), name, false, {}});
            }
        }
    };
    scan(m_tree, 0);
}

// ---- Layout --------------------------------------------------------------------

void CodeEditorWindow::Draw(Slate::Context& ui, const Rect& area) {
    Slate::Theme& t = ui.theme();
    if (m_treeDirty) Rescan();
    ui.Panel(area, Slate::Darken(t.windowBg, 0.15f));
    const Rect menuBar{area.x, area.y, area.w, kMenuBarH};
    const Rect toolbar{area.x, menuBar.Bottom(), area.w, kToolbarH};
    const f32 top = toolbar.Bottom() + 4.0f;
    const f32 gap = 4.0f;
    const f32 outputH = m_showOutput ? std::min(kOutputH, (area.Bottom() - top) * 0.4f) : 0.0f;
    const f32 bottom = area.Bottom() - (m_showOutput ? outputH + gap : 0.0f);
    const f32 explorerW = m_showExplorer ? std::min(kExplorerW, area.w * 0.25f) : 0.0f;
    const f32 inspectorW = m_showInspector ? std::min(kInspectorW, area.w * 0.28f) : 0.0f;
    const Rect explorer{area.x + gap, top, explorerW, bottom - top};
    const Rect inspector{area.Right() - gap - inspectorW, top, inspectorW, bottom - top};
    const f32 mainX = m_showExplorer ? explorer.Right() + gap : area.x + gap;
    const f32 mainR = m_showInspector ? inspector.x - gap : area.Right() - gap;
    const Rect main{mainX, top, mainR - mainX, bottom - top};
    const Rect tabs{main.x, main.y, main.w, kTabsH};
    const Rect editor{main.x, tabs.Bottom(), main.w, main.Bottom() - tabs.Bottom()};
    const Rect output{area.x + gap, area.Bottom() - outputH, area.w - gap * 2.0f, outputH - gap};

    if (m_showExplorer) DrawExplorer(ui, explorer);
    DrawTabs(ui, tabs);
    DrawEditor(ui, editor);
    if (m_showInspector) DrawInspector(ui, inspector);
    if (m_showOutput) DrawOutput(ui, output);
    DrawToolbar(ui, toolbar);
    DrawMenuBar(ui, menuBar);  // last: its menus open over the rest
}

void CodeEditorWindow::DrawMenuBar(Slate::Context& ui, const Rect& bar) {
    Slate::Theme& t = ui.theme();
    ui.Panel(bar, t.surface1);
    ui.Panel({bar.x, bar.Bottom() - 1.0f, bar.w, 1.0f}, t.separator);
    using Slate::MenuBuilder;
    auto opt = [](std::string tip, bool enabled = true, bool checked = false) {
        Slate::MenuEntryOptions o;
        o.tooltip = std::move(tip);
        o.enabled = enabled;
        o.checked = checked;
        return o;
    };
    const bool hasDoc = Active() != nullptr;
    struct Item {
        const char* label;
        std::function<std::vector<Slate::MenuEntry>()> build;
    };
    const Item items[] = {
        {"File",
         [&] {
             MenuBuilder b;
             b.BeginSection("New");
             b.AddEntry("New C++ File", Slate::Icon::Script, [this] { NewDocument(CodeLanguage::Cpp); },
                        opt("An untitled C++ file."));
             b.AddEntry("New Lua Script", Slate::Icon::Script, [this] { NewDocument(CodeLanguage::Lua); },
                        opt("An untitled Lua script."));
             b.AddEntry("New Python Script", Slate::Icon::Script, [this] { NewDocument(CodeLanguage::Python); },
                        opt("An untitled Python script."));
             b.EndSection();
             b.BeginSection("File");
             b.AddEntry("Save", Slate::Icon::Save, [this] { SaveActive(); },
                        opt("Save the active file (untitled files go to Source/ or Scripts/).", hasDoc));
             b.AddEntry("Show in Explorer", Slate::Icon::Browse,
                        [this] {
                            if (const CodeDocument* d = Active(); d && !d->path.empty()) {
                                OpenExternal(d->path.parent_path().string());
                            }
                        },
                        opt("Open the active file's folder.", hasDoc && !Active()->path.empty()));
             b.AddEntry("Refresh Project Files", Slate::Icon::Refresh, [this] { m_treeDirty = true; },
                        opt("Rescan the project's code files."));
             b.EndSection();
             b.BeginSection("Close");
             b.AddEntry("Close Tab", Slate::Icon::Close, [this] { CloseDocument(m_active); },
                        opt("Close the active file.", hasDoc));
             b.AddEntry("Close Window", Slate::Icon::Close, [this] { if (onClose) onClose(); }, opt(""));
             b.EndSection();
             return b.Build();
         }},
        {"View",
         [&] {
             MenuBuilder b;
             b.BeginSection("Panes");
             b.AddEntry("Project Explorer", Slate::Icon::Folder, [this] { m_showExplorer = !m_showExplorer; },
                        opt("", true, m_showExplorer));
             b.AddEntry("Code Inspector", Slate::Icon::Eye, [this] { m_showInspector = !m_showInspector; },
                        opt("", true, m_showInspector));
             b.AddEntry("Output", Slate::Icon::File, [this] { m_showOutput = !m_showOutput; },
                        opt("", true, m_showOutput));
             b.EndSection();
             return b.Build();
         }},
        {"Language",
         [&] {
             MenuBuilder b;
             b.BeginSection("Active File");
             for (CodeLanguage l : kAllLanguages) {
                 b.AddEntry(LanguageName(l), Slate::Icon::Script, [this, l] { SetLanguage(l); },
                            opt("Edit the active file as " + std::string(LanguageName(l)) + ".", true,
                                m_language == l));
             }
             b.EndSection();
             return b.Build();
         }},
        {"Window",
         [&] {
             MenuBuilder b;
             b.BeginSection("Window");
             b.AddEntry("Dock in Window Row", Slate::Icon::Dot, [this] { if (onDock) onDock(); }, opt(""));
             b.AddEntry("Float", Slate::Icon::Dot, [this] { if (onFloat) onFloat(); }, opt(""));
             b.AddEntry("Minimize", Slate::Icon::Minus, [this] { if (onMinimize) onMinimize(); }, opt(""));
             b.EndSection();
             return b.Build();
         }},
    };
    f32 x = bar.x + 6.0f;
    for (const Item& item : items) {
        const f32 w = ui.font().Measure(item.label).x + 18.0f;
        const Rect r{x, bar.y + 2.0f, w, bar.h - 4.0f};
        const u64 id = Slate::Context::ID(std::string("code.menu.") + item.label);
        bool open = ui.MenuButton(id, r, item.label);
        bool anyOpen = false;
        for (const Item& other : items) {
            anyOpen = anyOpen || ui.IsMenuOpen(Slate::Context::ID(std::string("code.menu.") + other.label));
        }
        if (anyOpen && !ui.IsMenuOpen(id) && r.Contains(ui.mouse())) open = true;
        if (open && !ui.IsMenuOpen(id)) {
            Slate::MenuOptions o;
            o.minWidth = 230.0f;
            ui.OpenMenuBelow(id, r, item.build(), o);
        }
        x += w + 2.0f;
    }
}

void CodeEditorWindow::DrawToolbar(Slate::Context& ui, const Rect& bar) {
    Slate::Theme& t = ui.theme();
    ui.GradientRect(bar, t.surface2, t.surface1);
    ui.Panel({bar.x, bar.Bottom() - 1.0f, bar.w, 1.0f}, t.separator);
    const f32 h = 34.0f;
    const f32 cy = bar.y + (bar.h - h) * 0.5f;
    // A toolbar button: icon + label; disabled ones are drawn dim and ignore input.
    auto button = [&](const char* key, f32 x, Slate::Icon icon, std::string_view label, Color iconColor,
                      bool enabled, std::string_view tip) {
        const f32 w = ui.font().Measure(label).x + 52.0f;
        const Rect r{x, cy, w, h};
        const bool hover = enabled && r.Contains(ui.mouse()) && !ui.inputBlocked();
        ui.drawList().AddRectFilledRounded(r, hover ? t.buttonHover : t.button, t.radius.md);
        const u8 a = enabled ? 255 : 90;
        Slate::DrawIcon(ui, {r.x + 12.0f, r.y + (h - 20.0f) * 0.5f, 20.0f, 20.0f}, icon, iconColor.WithAlpha(a));
        Text(ui, ui.font(), {r.x + 40.0f, r.y, w - 44.0f, h}, label, kWhite.WithAlpha(a));
        ui.Tooltip(Slate::Context::ID(std::string("code.tool.tip.") + key), r, tip);
        bool clicked = false;
        if (hover) {
            ui.RequestCursor(CursorShape::Hand);
            clicked = ui.mousePressed(0);
        }
        return std::pair{clicked, w};
    };
    f32 x = bar.x + 12.0f;
    // Brand mark first.
    const f32 logoS = 30.0f;
    if (logo) ui.Image(logo, {x, bar.y + (bar.h - logoS) * 0.5f, logoS, logoS});
    else ui.LogoMark({x + logoS * 0.5f, bar.y + bar.h * 0.5f}, logoS * 0.5f);
    x += logoS + 14.0f;
    const bool hasDoc = Active() != nullptr;
    auto [save, saveW] = button("save", x, Slate::Icon::Save, "Save", Color::RGBA(92, 160, 255, 255), hasDoc,
                                "Save the active file.");
    if (save) SaveActive();
    x += saveW + 8.0f;
    const char* later = "Coming with the code build pipeline.";
    x += button("build", x, Slate::Icon::Cube, "Build", Color::RGBA(255, 170, 60, 255), false, later).second + 8.0f;
    x += button("run", x, Slate::Icon::Play, "Run", t.success, false, later).second + 8.0f;
    x += button("debug", x, Slate::Icon::Focus, "Debug", Color::RGBA(110, 190, 255, 255), false, later).second + 8.0f;
    x += button("stop", x, Slate::Icon::Stop, "Stop", t.textDim, false, later).second + 8.0f;
    x += button("format", x, Slate::Icon::Grip, "Format", Color::RGBA(120, 170, 255, 255), false, later).second + 14.0f;

    // Right side: language, pane toggles.
    f32 rx = bar.Right() - 12.0f;
    auto toggle = [&](const char* key, Slate::Icon icon, bool& value, std::string_view tip) {
        rx -= h;
        const Rect r{rx, cy, h, h};
        const bool hover = r.Contains(ui.mouse()) && !ui.inputBlocked();
        ui.drawList().AddRectFilledRounded(r, value ? t.accent.WithAlpha(50) : (hover ? t.buttonHover : t.button),
                                           t.radius.md);
        Slate::DrawIcon(ui, r.Inset(8.0f, 8.0f), icon, value ? t.accent : kSoft);
        ui.Tooltip(Slate::Context::ID(std::string("code.toggle.tip.") + key), r, tip);
        if (hover && ui.mousePressed(0)) value = !value;
        rx -= 6.0f;
    };
    toggle("output", Slate::Icon::File, m_showOutput, "Show / hide the Output panel.");
    toggle("inspector", Slate::Icon::Eye, m_showInspector, "Show / hide the Code Inspector.");
    toggle("explorer", Slate::Icon::Folder, m_showExplorer, "Show / hide the Project Explorer.");
    rx -= 10.0f;
    const f32 comboW = 150.0f;
    const Rect combo{rx - comboW, cy, comboW, h};
    const Rect badge{combo.x - 36.0f, cy + (h - 20.0f) * 0.5f, 30.0f, 20.0f};
    LanguageBadge(ui, badge, m_language);
    const std::vector<std::string> names = LanguageNames();
    const int current = static_cast<int>(m_language);
    const int picked = ui.Combo(Slate::Context::ID("code.language"), combo, names, current);
    ui.Tooltip(Slate::Context::ID("code.language.tip"), combo,
               "The active file's language; new files are created in it.");
    if (picked != current && picked >= 0 && picked < static_cast<int>(kAllLanguages.size())) {
        SetLanguage(kAllLanguages[static_cast<usize>(picked)]);
    }
    // File search between the buttons and the language.
    const f32 searchR = badge.x - 16.0f;
    if (searchR - x > 140.0f) {
        ui.SearchBox(Slate::Context::ID("code.search"), {x, cy, std::min(320.0f, searchR - x), h}, m_search,
                     "Search in files...");
    }
}

void CodeEditorWindow::DrawExplorer(Slate::Context& ui, const Rect& side) {
    Slate::Theme& t = ui.theme();
    const Rect head{side.x, side.y, side.w, kPaneHeaderH};
    const Rect body{side.x, head.Bottom(), side.w, side.h - kPaneHeaderH};
    ui.Panel(body, Slate::Darken(t.surface1, 0.18f));
    PaneHeader(ui, head, Slate::Icon::Folder, "Project Explorer");
    const Rect refresh{head.Right() - 30.0f, head.y + 5.0f, 22.0f, 22.0f};
    if (refresh.Contains(ui.mouse()) && !ui.inputBlocked()) {
        ui.drawList().AddRectFilledRounded(refresh, t.buttonHover, 4.0f);
        if (ui.mousePressed(0)) m_treeDirty = true;
    }
    Slate::DrawIcon(ui, refresh.Inset(3.0f, 3.0f), Slate::Icon::Refresh, kSoft);
    ui.Tooltip(Slate::Context::ID("code.explorer.refresh"), refresh, "Rescan the project's code files.");

    if (m_projectRoot.empty()) {
        Text(ui, ui.font(), {body.x + 14.0f, body.y + 10.0f, body.w - 28.0f, 22.0f}, "No project open", t.textDim);
        return;
    }
    const bool hover = body.Contains(ui.mouse()) && !ui.inputBlocked();
    if (hover && ui.scrollDelta() != 0.0f) m_treeScroll -= ui.scrollDelta() * kRowH * 3.0f;
    m_treeScroll = std::max(0.0f, m_treeScroll);
    ui.drawList().PushClip(body);
    f32 y = body.y + 6.0f - m_treeScroll;
    if (!m_search.empty()) {
        // Search: matching files, flat.
        std::string query = m_search;
        std::transform(query.begin(), query.end(), query.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::function<void(const TreeNode&)> walk = [&](const TreeNode& n) {
            for (const TreeNode& c : n.children) {
                if (c.folder) {
                    walk(c);
                    continue;
                }
                std::string name = c.name;
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (name.find(query) != std::string::npos) DrawTreeNode(ui, body, c, 0, y);
            }
        };
        walk(m_tree);
    } else {
        DrawTreeNode(ui, body, m_tree, 0, y);
    }
    const f32 contentH = y + m_treeScroll - body.y;
    m_treeScroll = std::min(m_treeScroll, std::max(0.0f, contentH - body.h + 12.0f));
    if (m_tree.children.empty()) {
        Text(ui, ui.font(), {body.x + 14.0f, body.y + 34.0f, body.w - 28.0f, 22.0f},
             "No code files yet", t.textDim);
    }
    ui.drawList().PopClip();
}

void CodeEditorWindow::DrawTreeNode(Slate::Context& ui, const Rect& side, const TreeNode& node, int depth, f32& y) {
    Slate::Theme& t = ui.theme();
    const f32 indent = 16.0f * static_cast<f32>(depth);
    const Rect row{side.x + 4.0f, y, side.w - 8.0f, kRowH};
    y += kRowH;
    const bool visible = row.Bottom() > side.y && row.y < side.Bottom();
    const std::string key = node.path.generic_string();
    const bool open = !m_collapsed.count(key);
    const CodeDocument* active = m_active < m_documents.size() ? &m_documents[m_active] : nullptr;
    const bool selected = !node.folder && active && active->path == node.path;
    if (visible) {
        const bool hover = row.Contains(ui.mouse()) && side.Contains(ui.mouse()) && !ui.inputBlocked();
        if (selected) ui.drawList().AddRectFilledRounded(row, t.accent.WithAlpha(200), t.radius.md);
        else if (hover) ui.drawList().AddRectFilledRounded(row, Color::RGBA(255, 255, 255, 16), t.radius.md);
        f32 x = row.x + 6.0f + indent;
        if (node.folder) {
            Slate::DrawIcon(ui, {x, row.y + 6.0f, 12.0f, 12.0f},
                            open ? Slate::Icon::ChevronDown : Slate::Icon::ChevronRight, kSoft);
            Slate::DrawIcon(ui, {x + 16.0f, row.y + 3.0f, 18.0f, 18.0f},
                            open ? Slate::Icon::FolderOpen : Slate::Icon::Folder, Color::RGBA(255, 190, 70, 255));
            Text(ui, ui.font(), {x + 40.0f, row.y, row.Right() - x - 44.0f, row.h}, node.name, kWhite);
        } else {
            CodeLanguage lang = CodeLanguage::Cpp;
            LanguageFromExtension(node.path.extension().string(), &lang);
            LanguageBadge(ui, {x + 16.0f, row.y + 5.0f, 26.0f, 14.0f}, lang);
            Text(ui, ui.font(), {x + 48.0f, row.y, row.Right() - x - 52.0f, row.h}, node.name, kWhite);
        }
        if (hover && ui.mousePressed(0)) {
            if (node.folder) {
                if (open) m_collapsed.insert(key);
                else m_collapsed.erase(key);
            } else {
                OpenFile(node.path);
            }
        }
    }
    if (node.folder && open) {
        for (const TreeNode& c : node.children) DrawTreeNode(ui, side, c, depth + 1, y);
    }
}

void CodeEditorWindow::DrawTabs(Slate::Context& ui, const Rect& strip) {
    Slate::Theme& t = ui.theme();
    ui.Panel(strip, t.surface1);
    ui.Panel({strip.x, strip.Bottom() - 1.0f, strip.w, 1.0f}, t.separator);
    f32 x = strip.x;
    std::optional<usize> close;
    for (usize i = 0; i < m_documents.size(); ++i) {
        const CodeDocument& doc = m_documents[i];
        const f32 w = std::min(260.0f, ui.font().Measure(doc.name).x + 84.0f);
        if (x + w > strip.Right() - 40.0f) break;
        const Rect tab{x, strip.y, w, strip.h - 1.0f};
        const bool active = i == m_active;
        const bool hover = tab.Contains(ui.mouse()) && !ui.inputBlocked();
        ui.Panel(tab, active ? Slate::Darken(t.surface1, 0.35f) : (hover ? t.surface2 : t.surface1));
        if (active) ui.Panel({tab.x, tab.y, tab.w, 2.0f}, t.accent);
        ui.Panel({tab.Right() - 1.0f, tab.y + 6.0f, 1.0f, tab.h - 12.0f}, t.separator);
        LanguageBadge(ui, {tab.x + 10.0f, tab.y + (tab.h - 16.0f) * 0.5f, 30.0f, 16.0f}, doc.language);
        Text(ui, ui.font(), {tab.x + 48.0f, tab.y, tab.w - 76.0f, tab.h}, doc.name, active ? kWhite : kSoft);
        const Rect xr{tab.Right() - 26.0f, tab.y + (tab.h - 18.0f) * 0.5f, 18.0f, 18.0f};
        const bool xHover = xr.Contains(ui.mouse()) && hover;
        if (doc.dirty && !xHover) {
            ui.drawList().AddCircleFilled({xr.x + 9.0f, xr.y + 9.0f}, 4.0f, kSoft);
        } else {
            if (xHover) ui.drawList().AddRectFilledRounded(xr, Color::RGBA(255, 255, 255, 30), 4.0f);
            Slate::DrawIcon(ui, xr.Inset(3.0f, 3.0f), Slate::Icon::Close, xHover ? kWhite : kSoft);
        }
        if (hover && ui.mousePressed(0)) {
            if (xHover) close = i;
            else m_active = i, m_language = doc.language;
        }
        x += w;
    }
    // "+": a new file in the current language.
    const Rect plus{x + 6.0f, strip.y + (strip.h - 26.0f) * 0.5f, 26.0f, 26.0f};
    const bool pHover = plus.Contains(ui.mouse()) && !ui.inputBlocked();
    if (pHover) ui.drawList().AddRectFilledRounded(plus, t.buttonHover, 4.0f);
    Slate::DrawAddGlyph(ui, plus.Inset(6.0f, 6.0f), kSoft);
    ui.Tooltip(Slate::Context::ID("code.tabs.new"), plus,
               "New untitled " + std::string(LanguageName(m_language)) + " file.");
    if (pHover && ui.mousePressed(0)) NewDocument(m_language);
    if (close) CloseDocument(*close);
}

void CodeEditorWindow::DrawEditor(Slate::Context& ui, const Rect& body) {
    Slate::Theme& t = ui.theme();
    const Color bg = Slate::Darken(t.surface1, 0.42f);
    ui.Panel(body, bg);
    // Branding mark, faint, bottom-right (under the text).
    if (brandFont) {
        const std::string_view mark = "LUMA CODE";
        const Slate::Vec2 sz = brandFont->Measure(mark);
        ui.drawList().PushClip(body);
        ui.drawList().AddText(*brandFont, {body.Right() - sz.x - 28.0f, body.Bottom() - sz.y - 18.0f}, mark,
                              Color::RGBA(255, 255, 255, 20));
        ui.drawList().PopClip();
    }
    CodeDocument* doc = Active();
    if (!doc) {
        // Empty state: start a file in any language.
        const f32 cx = body.x + body.w * 0.5f;
        f32 y = body.y + body.h * 0.5f - 70.0f;
        Text(ui, ui.subtitleFont(), {body.x, y, body.w, 28.0f}, "No file open", kWhite, Slate::Align::Center);
        y += 40.0f;
        const f32 bw = 170.0f, gap = 10.0f;
        f32 bx = cx - (bw * 3.0f + gap * 2.0f) * 0.5f;
        for (CodeLanguage l : kAllLanguages) {
            const std::string label = "New " + std::string(LanguageName(l));
            if (ui.IconTextButton(Slate::Context::ID("code.empty." + label), {bx, y, bw, 32.0f}, Slate::Icon::Plus,
                                  label, t.success, false, 18.0f)) {
                NewDocument(l);
            }
            bx += bw + gap;
        }
        return;
    }

    const Slate::Font& mono = ui.monoFont();
    const f32 lineH = std::floor(mono.LineHeight() + 7.0f);
    const usize count = doc->lines.size();
    const std::string digits = std::to_string(count);
    const f32 gutterW = mono.Measure(std::string(std::max<usize>(digits.size(), 3), '9')).x + 34.0f;
    const Rect gutter{body.x, body.y, gutterW, body.h};
    const Rect text{gutter.Right(), body.y, body.w - gutterW, body.h};
    const f32 charW = std::max(1.0f, mono.Measure("M").x);

    // Scroll (wheel) and caret placement (click).
    const f32 contentH = lineH * static_cast<f32>(count) + body.h * 0.5f;
    const bool hover = body.Contains(ui.mouse()) && !ui.inputBlocked();
    if (hover && ui.scrollDelta() != 0.0f) doc->scrollY -= ui.scrollDelta() * lineH * 3.0f;
    doc->scrollY = std::clamp(doc->scrollY, 0.0f, std::max(0.0f, contentH - body.h));
    const f32 top = body.y + 8.0f - doc->scrollY;
    if (hover && text.Contains(ui.mouse())) ui.RequestCursor(CursorShape::IBeam);
    if (hover && ui.mousePressed(0)) {
        const f32 rel = (ui.mouse().y - top) / lineH;
        const usize line = static_cast<usize>(std::clamp(rel, 0.0f, static_cast<f32>(count - 1)));
        const f32 col = std::round((ui.mouse().x - (text.x + 12.0f)) / charW);
        doc->caretLine = line;
        doc->caretColumn = std::min(static_cast<usize>(std::max(0.0f, col)), doc->lines[line].size());
    }

    ui.Panel(gutter, bg);
    auto& draw = ui.drawList();
    draw.PushClip(body);
    const usize first = static_cast<usize>(std::max(0.0f, std::floor((body.y - top) / lineH)));
    const usize last = std::min(count, first + static_cast<usize>(body.h / lineH) + 2);
    for (usize i = first; i < last; ++i) {
        const f32 y = top + lineH * static_cast<f32>(i);
        const bool caretRow = i == doc->caretLine;
        if (caretRow) {
            draw.AddRectFilled({text.x, y, text.w, lineH}, Color::RGBA(255, 255, 255, 9));
            draw.AddRectFilled({gutter.Right() - 4.0f, y + 2.0f, 2.0f, lineH - 4.0f}, t.accent);
        }
        // Indent guides (every 4 columns of leading space).
        const std::string& line = doc->lines[i];
        const usize lead = std::min(line.find_first_not_of(' '), line.size());
        for (usize g = 4; g <= lead; g += 4) {
            draw.AddRectFilled({text.x + 12.0f + charW * static_cast<f32>(g - 4), y, 1.0f, lineH},
                               Color::RGBA(255, 255, 255, 18));
        }
        // Line number.
        const std::string num = std::to_string(i + 1);
        const f32 nw = mono.Measure(num).x;
        draw.AddText(mono, {gutter.Right() - 16.0f - nw, y + (lineH - mono.LineHeight()) * 0.5f}, num,
                     caretRow ? kWhite : Color::RGBA(118, 126, 140, 255));
        // Code, token by token.
        const f32 ty = y + (lineH - mono.LineHeight()) * 0.5f;
        for (const Token& tok : TokenizeLine(doc->language, line)) {
            const std::string_view s = std::string_view(line).substr(tok.begin, tok.end - tok.begin);
            draw.AddText(mono, {text.x + 12.0f + charW * static_cast<f32>(tok.begin), ty}, s, TokenColor(tok.kind));
        }
    }
    // Caret (blinks).
    const f32 cy = top + lineH * static_cast<f32>(doc->caretLine);
    if (std::fmod(ui.time(), 1.0f) < 0.6f) {
        draw.AddRectFilled({text.x + 12.0f + charW * static_cast<f32>(doc->caretColumn), cy + 3.0f, 2.0f, lineH - 6.0f},
                           t.accent);
    }
    draw.PopClip();
    ui.Panel({gutter.Right(), body.y, 1.0f, body.h}, Color::RGBA(255, 255, 255, 14));
}

void CodeEditorWindow::DrawInspector(Slate::Context& ui, const Rect& side) {
    Slate::Theme& t = ui.theme();
    const Rect head{side.x, side.y, side.w, kPaneHeaderH};
    const Rect body{side.x, head.Bottom(), side.w, side.h - kPaneHeaderH};
    ui.Panel(body, Slate::Darken(t.surface1, 0.18f));
    PaneHeader(ui, head, Slate::Icon::Eye, "Code Inspector");
    CodeDocument* doc = Active();
    if (!doc) {
        Text(ui, ui.font(), {body.x + 14.0f, body.y + 10.0f, body.w - 28.0f, 22.0f}, "No file open", t.textDim);
        return;
    }
    ui.drawList().PushClip(body);
    f32 y = body.y + 10.0f;
    // File card.
    LanguageBadge(ui, {body.x + 12.0f, y + 4.0f, 40.0f, 32.0f}, doc->language);
    Text(ui, ui.subtitleFont(), {body.x + 62.0f, y, body.w - 70.0f, 22.0f}, doc->name, kWhite);
    std::string where = "Not saved yet";
    if (!doc->path.empty()) {
        std::error_code ec;
        const std::filesystem::path rel =
            m_projectRoot.empty() ? doc->path : std::filesystem::relative(doc->path, m_projectRoot, ec);
        where = (ec || rel.empty() ? doc->path : rel).generic_string();
    }
    Text(ui, ui.font(), {body.x + 62.0f, y + 22.0f, body.w - 70.0f, 20.0f}, where, kSoft);
    y += 52.0f;

    // File properties.
    SectionHeader(ui, {body.x, y, body.w, 26.0f}, "File Properties");
    y += 30.0f;
    usize bytes = 0;
    for (const std::string& l : doc->lines) bytes += l.size() + 1;
    const std::pair<std::string, std::string> props[] = {
        {"Language", std::string(LanguageName(doc->language))},
        {"Lines", std::to_string(doc->lines.size())},
        {"Size", std::to_string(bytes) + " bytes"},
        {"Saved", doc->dirty ? "No" : "Yes"},
    };
    for (const auto& [k, v] : props) {
        Text(ui, ui.font(), {body.x + 14.0f, y, 110.0f, 24.0f}, k, kSoft);
        const Rect field{body.x + 124.0f, y + 1.0f, body.w - 136.0f, 22.0f};
        ui.drawList().AddRectFilledRounded(field, Color::RGBA(0, 0, 0, 60), 3.0f);
        Text(ui, ui.font(), field.Inset(8.0f, 0.0f), v, kWhite);
        y += 26.0f;
    }
    y += 6.0f;

    // Symbols.
    const std::vector<CodeSymbol> symbols = FindSymbols(*doc);
    SectionHeader(ui, {body.x, y, body.w, 26.0f}, "Symbols");
    y += 30.0f;
    const CodeSymbol* current = nullptr;
    for (const CodeSymbol& s : symbols) {
        if (s.line <= doc->caretLine && s.kind != CodeSymbol::Kind::Variable) current = &s;
    }
    const f32 symbolsEnd = body.Bottom() - 150.0f;
    for (const CodeSymbol& s : symbols) {
        if (y > symbolsEnd) break;
        const f32 x = body.x + 14.0f + 16.0f * static_cast<f32>(s.depth);
        const Rect row{body.x + 4.0f, y, body.w - 8.0f, 22.0f};
        const bool hover = row.Contains(ui.mouse()) && !ui.inputBlocked();
        if (&s == current || hover) {
            ui.drawList().AddRectFilledRounded(row, &s == current ? t.accent.WithAlpha(40) : Color::RGBA(255, 255, 255, 14),
                                               3.0f);
        }
        Color dot = Color::RGBA(250, 214, 110, 255);
        const char* glyph = "f";
        if (s.kind == CodeSymbol::Kind::Class) dot = Color::RGBA(255, 150, 60, 255), glyph = "C";
        if (s.kind == CodeSymbol::Kind::Variable) dot = Color::RGBA(96, 212, 220, 255), glyph = "v";
        ui.drawList().AddCircleFilled({x + 7.0f, y + 11.0f}, 7.0f, dot.WithAlpha(60));
        Text(ui, ui.smallFont(), {x, y, 14.0f, 22.0f}, glyph, dot, Slate::Align::Center);
        Text(ui, ui.font(), {x + 20.0f, y, row.Right() - x - 24.0f, 22.0f},
             s.name + (s.kind == CodeSymbol::Kind::Function ? "()" : ""), kWhite);
        if (hover && ui.mousePressed(0)) {
            doc->caretLine = s.line;
            doc->caretColumn = 0;
            doc->scrollY = std::max(0.0f, (static_cast<f32>(s.line) - 4.0f) * (ui.monoFont().LineHeight() + 7.0f));
        }
        y += 22.0f;
    }
    if (symbols.empty()) {
        Text(ui, ui.font(), {body.x + 14.0f, y, body.w - 28.0f, 22.0f}, "No symbols found", t.textDim);
        y += 22.0f;
    }
    y = std::max(y + 8.0f, symbolsEnd + 4.0f);

    // Documentation: the comment above the symbol at the caret.
    SectionHeader(ui, {body.x, y, body.w, 26.0f}, "Documentation");
    y += 32.0f;
    if (current) {
        Text(ui, ui.subtitleFont(), {body.x + 14.0f, y, body.w - 28.0f, 24.0f}, current->name, t.accent);
        y += 26.0f;
        const std::string doc_ = CommentAbove(*doc, current->line);
        ui.LabelIn({body.x + 2.0f, y, body.w - 16.0f, body.Bottom() - y - 6.0f},
                   doc_.empty() ? "No comment above this symbol." : doc_, doc_.empty() ? t.textDim : kWhite);
    } else {
        Text(ui, ui.font(), {body.x + 14.0f, y, body.w - 28.0f, 22.0f}, "Place the caret in a symbol.", t.textDim);
    }
    ui.drawList().PopClip();
}

void CodeEditorWindow::DrawOutput(Slate::Context& ui, const Rect& panel) {
    Slate::Theme& t = ui.theme();
    const Rect tabs{panel.x, panel.y, panel.w, kPaneHeaderH};
    const Rect body{panel.x, tabs.Bottom(), panel.w, panel.h - kPaneHeaderH};
    ui.Panel(tabs, t.surface1);
    ui.Panel({tabs.x, tabs.Bottom() - 1.0f, tabs.w, 1.0f}, t.separator);
    ui.Panel(body, Slate::Darken(t.surface1, 0.4f));
    const std::string names[] = {"Output", "Problems (0)", "Terminal", "Log"};
    const Slate::Icon icons[] = {Slate::Icon::File, Slate::Icon::Dot, Slate::Icon::Script, Slate::Icon::File};
    f32 x = tabs.x;
    for (int i = 0; i < 4; ++i) {
        const f32 w = ui.font().Measure(names[i]).x + 48.0f;
        const Rect r{x, tabs.y, w, tabs.h - 1.0f};
        const bool active = m_outputTab == i;
        const bool hover = r.Contains(ui.mouse()) && !ui.inputBlocked();
        if (active || hover) ui.Panel(r, active ? Slate::Darken(t.surface1, 0.4f) : t.surface2);
        if (active) ui.Panel({r.x, r.y, r.w, 2.0f}, t.accent);
        Slate::DrawIcon(ui, {r.x + 12.0f, r.y + (r.h - 16.0f) * 0.5f, 16.0f, 16.0f}, icons[i],
                        active ? t.accent : kSoft);
        Text(ui, ui.font(), {r.x + 34.0f, r.y, w - 36.0f, r.h}, names[i], active ? kWhite : kSoft);
        if (hover && ui.mousePressed(0)) m_outputTab = i;
        x += w;
    }
    // Clear.
    const Rect clear{tabs.Right() - 86.0f, tabs.y + 4.0f, 78.0f, tabs.h - 8.0f};
    const bool cHover = clear.Contains(ui.mouse()) && !ui.inputBlocked();
    ui.drawList().AddRectFilledRounded(clear, cHover ? t.buttonHover : t.button, t.radius.md);
    Slate::DrawIcon(ui, {clear.x + 10.0f, clear.y + (clear.h - 14.0f) * 0.5f, 14.0f, 14.0f}, Slate::Icon::Trash, kSoft);
    Text(ui, ui.font(), {clear.x + 30.0f, clear.y, clear.w - 32.0f, clear.h}, "Clear", kWhite);
    if (cHover && ui.mousePressed(0)) m_output.clear();

    const Slate::Font& mono = ui.monoFont();
    const f32 lineH = mono.LineHeight() + 4.0f;
    ui.drawList().PushClip(body);
    f32 y = body.y + 8.0f;
    auto line = [&](std::string_view s, Color c) {
        ui.drawList().AddText(mono, {body.x + 14.0f, y}, s, c);
        y += lineH;
    };
    switch (m_outputTab) {
        case 1:
            line("No problems reported.", t.textDim);
            break;
        case 2:
            line("The terminal is not connected yet.", t.textDim);
            break;
        case 3:
            line("The engine log is in the Console panel.", t.textDim);
            break;
        default: {
            if (m_output.empty()) line("Build output and editor messages appear here.", t.textDim);
            const usize fit = static_cast<usize>(std::max(1.0f, (body.h - 12.0f) / lineH));
            const usize start = m_output.size() > fit ? m_output.size() - fit : 0;
            for (usize i = start; i < m_output.size(); ++i) {
                line("[Code] " + m_output[i].text, m_output[i].error ? t.error : kSoft);
            }
            break;
        }
    }
    ui.drawList().PopClip();
}

void RegisterCodeEditorWindow(RegistrationSet& registrations, Editor::MenuRegistry& menus,
                              std::function<void()> openWindow) {
    Editor::MenuEntryDesc entry;
    entry.id = "Tools.CodeEditor";
    entry.menu = "Tools";
    entry.section = "Programming";
    entry.label = "Code Editor";
    entry.icon = "CodeEditor";  // registered by the editor (Luma_Icons/CodeEditor)
    entry.tooltip = "Edit C++, Lua and Python code inside the engine.";
    entry.owner = "Engine";
    entry.order = 100;
    entry.execute = [openWindow] {
        if (openWindow) openWindow();
    };
    registrations.Add(menus.RegisterEntry(std::move(entry)));
}

}  // namespace Luma::CodeEditor
