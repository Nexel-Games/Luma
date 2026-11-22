#pragma once

#include <string>
#include <vector>

#include "Luma/MaterialEditors/EditorServices.h"
#include "Luma/Slate/Context.h"

// Small Details / toolbar helpers shared by the two material editors.

namespace Luma::MaterialEditors::Detail {

constexpr f32 kToolbarH = 34.0f;
constexpr f32 kRowH = 26.0f;
constexpr f32 kHeaderH = 24.0f;
constexpr f32 kLabelW = 132.0f;

// Unreal-style black panel dividers.
inline const Slate::Color kDividerColor = Slate::Color::RGB(8, 8, 8);
constexpr f32 kDividerW = 2.0f;
inline void VDivider(Slate::Context& ui, f32 x, f32 y, f32 h) {
    ui.Panel({x - kDividerW * 0.5f, y, kDividerW, h}, kDividerColor);
}
inline void HDivider(Slate::Context& ui, f32 x, f32 y, f32 w) {
    ui.Panel({x, y - kDividerW * 0.5f, w, kDividerW}, kDividerColor);
}

// A Details section header (full width), advancing y.
inline bool Section(Slate::Context& ui, std::string_view id, std::string_view label, bool& open, f32 x,
                    f32& y, f32 w) {
    const bool clicked = ui.CollapsingHeader(Slate::Context::ID(id), {x, y, w, kHeaderH - 2.0f}, label, open);
    ui.Panel({x, y + kHeaderH - 1.0f, w, 1.0f}, kDividerColor);  // line under the header
    y += kHeaderH;
    return clicked;
}

// Label column + returns the value rect; advances y.
inline Slate::Rect Row(Slate::Context& ui, std::string_view label, f32 x, f32& y, f32 w, bool dim = false,
                       f32 indent = 0.0f) {
    const Slate::Theme& t = ui.theme();
    const Slate::Rect row{x, y + 2.0f, w, kRowH - 4.0f};
    // Black row line + the name/value column divider (Unreal's Details grid).
    ui.Panel({x, y + kRowH - 1.0f, w, 1.0f}, kDividerColor);
    ui.Panel({x + kLabelW - 4.0f, y, 1.0f, kRowH - 1.0f}, kDividerColor);
    ui.LabelIn({row.x + indent, row.y, kLabelW - indent - 6.0f, row.h}, label, dim ? t.textDisabled : t.textDim);
    y += kRowH;
    return {row.x + kLabelW, row.y, std::max(row.w - kLabelW - 4.0f, 40.0f), row.h};
}

// Combo over `items` showing `current`; returns the picked item or "".
inline std::string PickString(Slate::Context& ui, u64 id, const Slate::Rect& rect,
                              const std::vector<std::string>& items, const std::string& current) {
    int index = -1;
    for (usize i = 0; i < items.size(); ++i)
        if (items[i] == current) index = static_cast<int>(i);
    std::vector<std::string> shown = items;
    if (index < 0) {
        shown.insert(shown.begin(), current.empty() ? std::string("None") : current);
        index = 0;
    }
    const int picked = ui.Combo(id, rect, shown, index);
    if (picked != index && picked >= 0 && picked < static_cast<int>(shown.size())) return shown[picked];
    return {};
}

inline TextureHandle IconTexture(const MaterialEditorServices& s, EditorIcon icon) {
    return s.icons ? s.icons->Get(icon) : TextureHandle{0};
}

// A toolbar button with an icon-pack PNG in Slate's icon slot (falls back
// to the procedural `fallback` glyph when the PNG is missing).
inline bool ToolButton(Slate::Context& ui, u64 id, const Slate::Rect& r, TextureHandle texture, Slate::Icon fallback,
                       std::string_view label, Slate::Color tint, bool active = false) {
    if (texture == 0) return ui.IconTextButton(id, r, fallback, label, tint, active);
    const bool clicked = ui.IconTextButton(id, r, Slate::Icon::None, label, tint.WithAlpha(0), active);
    constexpr f32 kSlot = 14.0f;  // IconTextButton's icon slot
    constexpr f32 kSize = 20.0f;
    const f32 total = kSlot + (label.empty() ? 0.0f : 6.0f + ui.uiFont().Measure(label).x);
    const f32 cx = r.x + (r.w - total) * 0.5f + kSlot * 0.5f;
    ui.Image(texture, {cx - kSize * 0.5f, r.y + (r.h - kSize) * 0.5f, kSize, kSize});  // colour is baked in
    return clicked;
}

// Toolbar strip background.
inline void ToolbarBackground(Slate::Context& ui, const Slate::Rect& bar) {
    const Slate::Theme& t = ui.theme();
    ui.Panel(bar, t.surface1);
    ui.Panel({bar.x, bar.Bottom() - 1.0f, bar.w, 1.0f}, t.separator);
}

// Compile status text for the toolbar ("" when idle and clean).
inline std::string CompileStatus(const MaterialEditorServices& s, std::string_view path, bool& isError) {
    isError = false;
    if (s.compileErrors) {
        std::string e = s.compileErrors(path);
        if (!e.empty()) {
            isError = true;
            return e;
        }
    }
    if (s.pendingCompiles && s.pendingCompiles() > 0) return "Compiling shaders...";
    return {};
}

}  // namespace Luma::MaterialEditors::Detail
