#pragma once

#include <algorithm>
#include <functional>

#include "Luma/Math/Math.h"
#include "Luma/Slate/ColorPicker.h"
#include "Luma/Slate/Context.h"

// A floating colour picker for the material editors: Slate's ColorPicker
// (SV square, hue strip, RGB/HSV/hex fields) inside a rounded, shadowed
// card with a title and an old/new comparison swatch. One per editor; any
// rounded swatch opens it for one value. Closes on Escape or a click outside.

namespace Luma::MaterialEditors::Detail {

inline Slate::Color ToSlateColor(const Math::Vec4& v) {
    auto c8 = [](f32 c) { return static_cast<u8>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return Slate::Color::RGBA(c8(v.x), c8(v.y), c8(v.z), c8(v.w));
}

class ColorPopup {
public:
    using Apply = std::function<void(const Math::Vec4&)>;

    // A rounded colour swatch; clicking it opens the popup for `value`.
    // Returns true when the swatch was clicked.
    bool Swatch(Slate::Context& ui, u64 id, const Slate::Rect& rect, const Math::Vec4& value, Apply apply) {
        const Slate::Theme& t = ui.theme();
        const bool hover = rect.Contains(ui.mouse()) && !ui.inputBlocked();
        ui.PanelRoundedBordered(rect, ToSlateColor({value.x, value.y, value.z, 1.0f}),
                                hover ? t.accent : Slate::Color::RGB(80, 80, 84), 4.0f);
        if (hover && ui.mousePressed(0)) {
            Open(id, {rect.x, rect.Bottom() + 6.0f}, value, std::move(apply));
            return true;
        }
        return false;
    }

    void Open(u64 owner, Slate::Vec2 anchor, const Math::Vec4& value, Apply apply) {
        m_owner = owner;
        m_anchor = anchor;
        m_original = value;
        m_apply = std::move(apply);
        m_picker.SetColor(ToSlateColor(value));
        m_open = true;
        m_age = 0;
    }
    bool IsOpen() const { return m_open; }
    bool IsOpenFor(u64 owner) const { return m_open && m_owner == owner; }
    void Close() { m_open = false; }

    // Draw after every panel (floats above them).
    void Draw(Slate::Context& ui) {
        if (!m_open) return;
        const Slate::Theme& t = ui.theme();
        constexpr f32 kPad = 14.0f, kTitleH = 26.0f, kPickerW = 300.0f, kSwatchH = 22.0f;
        const f32 w = kPickerW + kPad * 2.0f;
        const f32 h = kTitleH + Slate::ColorPicker::kHeight + kSwatchH + kPad * 2.0f + 10.0f;
        Slate::Rect card{m_anchor.x, m_anchor.y, w, h};
        card.x = std::clamp(card.x, 8.0f, std::max(ui.displayWidth() - w - 8.0f, 8.0f));
        if (card.Bottom() > ui.displayHeight() - 8.0f) card.y = std::max(8.0f, ui.displayHeight() - h - 8.0f);

        if (m_age > 0 && (ui.keyEscape() ||
                          (!card.Contains(ui.mouse()) && (ui.mousePressed(0) || ui.mousePressed(1))))) {
            m_open = false;
            return;
        }
        // Rounded card with a soft shadow and a hairline edge.
        ui.drawList().AddRectShadow(card, 12.0f, 0.9f);
        ui.PanelRoundedBordered(card, t.surface2, Slate::Color::RGB(78, 78, 82), 12.0f);
        ui.LabelIn({card.x + kPad, card.y + 6.0f, w - 2.0f * kPad, kTitleH - 6.0f}, "Color Picker", t.text);
        ui.Panel({card.x + 10.0f, card.y + kTitleH + 2.0f, w - 20.0f, 1.0f}, Slate::Color::RGB(8, 8, 8));
        const Slate::Rect pick{card.x + kPad, card.y + kTitleH + 10.0f, kPickerW, Slate::ColorPicker::kHeight};
        if (m_picker.Draw(ui, pick) && m_apply) {
            const Slate::Color c = m_picker.color();
            m_apply({c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, m_original.w});
        }
        // Old | New comparison (rounded).
        const f32 sy = pick.Bottom() + 8.0f, half = kPickerW * 0.5f;
        ui.LabelIn({pick.x, sy, 40.0f, kSwatchH}, "Old", t.textDim);
        ui.PanelRounded({pick.x + 36.0f, sy, half - 44.0f, kSwatchH},
                        ToSlateColor({m_original.x, m_original.y, m_original.z, 1.0f}), 5.0f);
        ui.LabelIn({pick.x + half, sy, 40.0f, kSwatchH}, "New", t.textDim);
        ui.PanelRounded({pick.x + half + 36.0f, sy, half - 36.0f, kSwatchH}, m_picker.color().WithAlpha(255), 5.0f);
        ++m_age;
    }

private:
    Slate::ColorPicker m_picker;
    bool m_open = false;
    int m_age = 0;
    u64 m_owner = 0;
    Slate::Vec2 m_anchor{0, 0};
    Math::Vec4 m_original{1, 1, 1, 1};
    Apply m_apply;
};

}  // namespace Luma::MaterialEditors::Detail
