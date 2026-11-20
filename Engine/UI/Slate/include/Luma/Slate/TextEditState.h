#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Luma/Core/Types.h"

// The text-editing core behind every Slate text field: caret, selection,
// word motion, clipboard-style cut, per-field undo/redo and revert. Pure
// logic (no drawing, no input) so it is testable on its own; Context feeds it
// keys and mouse positions and draws the result.
//
// Positions are byte offsets into UTF-8 text, always on codepoint
// boundaries. A "word" is a run of letters, digits, '_' or non-ASCII
// characters; any other non-space characters form punctuation runs.

namespace Luma::Slate {

class TextEditState {
public:
    // Starts editing `text` (a field gaining focus). Records it as Original()
    // for Revert, clears the history, and places the caret at the end with
    // everything selected when `selectAll`.
    void Begin(std::string_view text, bool selectAll);

    const std::string& Text() const { return m_text; }
    const std::string& Original() const { return m_original; }
    usize Caret() const { return m_caret; }
    usize Anchor() const { return m_anchor; }
    bool HasSelection() const { return m_caret != m_anchor; }
    // Ordered [begin, end).
    std::pair<usize, usize> Selection() const;
    std::string SelectedText() const;

    // Inserts at the caret, replacing the selection; honours maxLength.
    void Insert(std::string_view s);
    // Deletes the selection, else one codepoint (or a word when `word`).
    void Backspace(bool word);
    void Delete(bool word);
    // Caret motion; `extend` grows the selection instead of collapsing it.
    void MoveLeft(bool word, bool extend);
    void MoveRight(bool word, bool extend);
    void Home(bool extend);
    void End(bool extend);
    // Places the caret (snapped back to a codepoint boundary).
    void SetCaret(usize pos, bool extend);
    void SelectAll();
    // Selects the word / punctuation / space run containing `pos`.
    void SelectWordAt(usize pos);
    // Removes and returns the selection ("" when nothing is selected).
    std::string Cut();
    bool Undo();
    bool Redo();
    // Back to the text editing started with.
    void Revert();

    usize maxLength = 0;  // bytes; 0 = unlimited
    f32 scroll = 0.0f;    // horizontal scroll of the field, px (owned by Context)

private:
    struct Snapshot {
        std::string text;
        usize caret = 0;
        usize anchor = 0;
    };
    enum class Op { None, Typing, Other };
    void Push(Op op);
    void EraseRange(usize begin, usize end);
    usize PrevBoundary(usize pos) const;
    usize NextBoundary(usize pos) const;
    usize PrevWord(usize pos) const;
    usize NextWord(usize pos) const;

    std::string m_text;
    std::string m_original;
    usize m_caret = 0;
    usize m_anchor = 0;
    std::vector<Snapshot> m_undo;
    std::vector<Snapshot> m_redo;
    Op m_lastOp = Op::None;
    static constexpr usize kHistory = 32;
};

// Caret index (a codepoint boundary of `text`) nearest to `x` px from the
// text start; `measure(prefix)` returns a prefix's width.
usize CaretFromX(std::string_view text, f32 x,
                 const std::function<f32(std::string_view)>& measure);

}  // namespace Luma::Slate
