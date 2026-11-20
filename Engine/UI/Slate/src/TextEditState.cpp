#include "Luma/Slate/TextEditState.h"

#include <algorithm>
#include <cmath>

namespace Luma::Slate {
namespace {

bool IsContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u; }

enum class CharClass { Space, Word, Punct };

CharClass ClassOf(char c) {
    const auto u = static_cast<unsigned char>(c);
    if (u == ' ' || u == '\t' || u == '\n' || u == '\r') return CharClass::Space;
    if (u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') ||
        (u >= 'A' && u <= 'Z') || u == '_') {
        return CharClass::Word;
    }
    return CharClass::Punct;
}

// Largest codepoint boundary <= pos.
usize SnapBack(std::string_view text, usize pos) {
    pos = std::min(pos, text.size());
    while (pos > 0 && pos < text.size() && IsContinuation(text[pos])) --pos;
    return pos;
}

}  // namespace

void TextEditState::Begin(std::string_view text, bool selectAll) {
    m_text.assign(text);
    if (maxLength > 0 && m_text.size() > maxLength) m_text.resize(SnapBack(m_text, maxLength));
    m_original = m_text;
    m_caret = m_text.size();
    m_anchor = selectAll ? 0 : m_caret;
    m_undo.clear();
    m_redo.clear();
    m_lastOp = Op::None;
    scroll = 0.0f;
}

std::pair<usize, usize> TextEditState::Selection() const {
    return {std::min(m_caret, m_anchor), std::max(m_caret, m_anchor)};
}

std::string TextEditState::SelectedText() const {
    const auto [b, e] = Selection();
    return m_text.substr(b, e - b);
}

void TextEditState::Push(Op op) {
    // Consecutive typing coalesces into a single undo step.
    if (op == Op::Typing && m_lastOp == Op::Typing) return;
    m_undo.push_back({m_text, m_caret, m_anchor});
    if (m_undo.size() > kHistory) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_lastOp = op;
}

void TextEditState::EraseRange(usize begin, usize end) {
    m_text.erase(begin, end - begin);
    m_caret = m_anchor = begin;
}

usize TextEditState::PrevBoundary(usize pos) const {
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && IsContinuation(m_text[pos])) --pos;
    return pos;
}

usize TextEditState::NextBoundary(usize pos) const {
    if (pos >= m_text.size()) return m_text.size();
    ++pos;
    while (pos < m_text.size() && IsContinuation(m_text[pos])) ++pos;
    return pos;
}

usize TextEditState::PrevWord(usize pos) const {
    while (pos > 0 && ClassOf(m_text[PrevBoundary(pos)]) == CharClass::Space) {
        pos = PrevBoundary(pos);
    }
    if (pos == 0) return 0;
    const CharClass run = ClassOf(m_text[PrevBoundary(pos)]);
    while (pos > 0 && ClassOf(m_text[PrevBoundary(pos)]) == run) pos = PrevBoundary(pos);
    return pos;
}

usize TextEditState::NextWord(usize pos) const {
    while (pos < m_text.size() && ClassOf(m_text[pos]) == CharClass::Space) {
        pos = NextBoundary(pos);
    }
    if (pos >= m_text.size()) return m_text.size();
    const CharClass run = ClassOf(m_text[pos]);
    while (pos < m_text.size() && ClassOf(m_text[pos]) == run) pos = NextBoundary(pos);
    return pos;
}

void TextEditState::Insert(std::string_view s) {
    if (s.empty() && !HasSelection()) return;
    Push(s.size() == 1 && !HasSelection() ? Op::Typing : Op::Other);
    if (HasSelection()) {
        const auto [b, e] = Selection();
        EraseRange(b, e);
    }
    std::string_view insert = s;
    if (maxLength > 0) {
        const usize room = m_text.size() >= maxLength ? 0 : maxLength - m_text.size();
        if (insert.size() > room) insert = insert.substr(0, SnapBack(insert, room));
    }
    m_text.insert(m_caret, insert);
    m_caret += insert.size();
    m_anchor = m_caret;
}

void TextEditState::Backspace(bool word) {
    if (HasSelection()) {
        Push(Op::Other);
        const auto [b, e] = Selection();
        EraseRange(b, e);
        return;
    }
    if (m_caret == 0) return;
    Push(Op::Other);
    const usize from = word ? PrevWord(m_caret) : PrevBoundary(m_caret);
    EraseRange(from, m_caret);
}

void TextEditState::Delete(bool word) {
    if (HasSelection()) {
        Push(Op::Other);
        const auto [b, e] = Selection();
        EraseRange(b, e);
        return;
    }
    if (m_caret >= m_text.size()) return;
    Push(Op::Other);
    const usize to = word ? NextWord(m_caret) : NextBoundary(m_caret);
    EraseRange(m_caret, to);
}

void TextEditState::MoveLeft(bool word, bool extend) {
    m_lastOp = Op::None;
    if (!extend && !word && HasSelection()) {
        m_caret = m_anchor = Selection().first;
        return;
    }
    m_caret = word ? PrevWord(m_caret) : PrevBoundary(m_caret);
    if (!extend) m_anchor = m_caret;
}

void TextEditState::MoveRight(bool word, bool extend) {
    m_lastOp = Op::None;
    if (!extend && !word && HasSelection()) {
        m_caret = m_anchor = Selection().second;
        return;
    }
    m_caret = word ? NextWord(m_caret) : NextBoundary(m_caret);
    if (!extend) m_anchor = m_caret;
}

void TextEditState::Home(bool extend) { SetCaret(0, extend); }
void TextEditState::End(bool extend) { SetCaret(m_text.size(), extend); }

void TextEditState::SetCaret(usize pos, bool extend) {
    m_lastOp = Op::None;
    m_caret = SnapBack(m_text, pos);
    if (!extend) m_anchor = m_caret;
}

void TextEditState::SelectAll() {
    m_lastOp = Op::None;
    m_anchor = 0;
    m_caret = m_text.size();
}

void TextEditState::SelectWordAt(usize pos) {
    m_lastOp = Op::None;
    if (m_text.empty()) {
        m_caret = m_anchor = 0;
        return;
    }
    pos = SnapBack(m_text, std::min(pos, m_text.size() - 1));
    const CharClass run = ClassOf(m_text[pos]);
    usize b = pos;
    while (b > 0 && ClassOf(m_text[PrevBoundary(b)]) == run) b = PrevBoundary(b);
    usize e = pos;
    while (e < m_text.size() && ClassOf(m_text[e]) == run) e = NextBoundary(e);
    m_anchor = b;
    m_caret = e;
}

std::string TextEditState::Cut() {
    if (!HasSelection()) return {};
    Push(Op::Other);
    std::string out = SelectedText();
    const auto [b, e] = Selection();
    EraseRange(b, e);
    return out;
}

bool TextEditState::Undo() {
    if (m_undo.empty()) return false;
    m_redo.push_back({m_text, m_caret, m_anchor});
    const Snapshot s = m_undo.back();
    m_undo.pop_back();
    m_text = s.text;
    m_caret = s.caret;
    m_anchor = s.anchor;
    m_lastOp = Op::None;
    return true;
}

bool TextEditState::Redo() {
    if (m_redo.empty()) return false;
    m_undo.push_back({m_text, m_caret, m_anchor});
    const Snapshot s = m_redo.back();
    m_redo.pop_back();
    m_text = s.text;
    m_caret = s.caret;
    m_anchor = s.anchor;
    m_lastOp = Op::None;
    return true;
}

void TextEditState::Revert() {
    if (m_text == m_original) return;
    Push(Op::Other);
    m_text = m_original;
    m_caret = m_anchor = m_text.size();
}

usize CaretFromX(std::string_view text, f32 x,
                 const std::function<f32(std::string_view)>& measure) {
    usize best = 0;
    f32 bestDist = std::abs(x);
    for (usize pos = 1; pos <= text.size(); ++pos) {
        if (pos < text.size() && IsContinuation(text[pos])) continue;
        const f32 d = std::abs(measure(text.substr(0, pos)) - x);
        if (d < bestDist) {
            bestDist = d;
            best = pos;
        }
    }
    return best;
}

}  // namespace Luma::Slate
