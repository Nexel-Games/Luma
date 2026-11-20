// TextEditState: the renderer-free text-editing core behind every Slate text
// field (selection, word motion, clipboard ops, undo, revert, UTF-8 carets).

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "Luma/Slate/TextEditState.h"

using Luma::usize;
using Luma::f32;
using Luma::Slate::CaretFromX;
using Luma::Slate::TextEditState;

TEST_CASE("Insert replaces the selection", "[slate][textedit]") {
    TextEditState s;
    s.Begin("hello world", false);
    s.SetCaret(0, false);
    s.SetCaret(5, true);
    s.Insert("bye");
    CHECK(s.Text() == "bye world");
    CHECK(s.Caret() == 3);
    CHECK_FALSE(s.HasSelection());
}

TEST_CASE("Backspace and Delete by word", "[slate][textedit]") {
    TextEditState s;
    s.Begin("foo bar_baz qux", false);
    s.End(false);
    s.Backspace(true);
    CHECK(s.Text() == "foo bar_baz ");
    s.Home(false);
    s.Delete(true);
    CHECK(s.Text() == " bar_baz ");
}

TEST_CASE("Ctrl-arrows jump words and Shift extends", "[slate][textedit]") {
    TextEditState s;
    s.Begin("one two three", false);
    s.Home(false);
    s.MoveRight(true, false);
    CHECK(s.Caret() == 3);
    s.MoveRight(true, true);
    CHECK(s.Selection() == std::pair<usize, usize>{3, 7});
    CHECK(s.SelectedText() == " two");
    s.MoveLeft(true, false);
    CHECK(s.Caret() == 4);
}

TEST_CASE("SelectAll, Cut, Undo, Redo", "[slate][textedit]") {
    TextEditState s;
    s.Begin("abc", false);
    s.SelectAll();
    CHECK(s.Cut() == "abc");
    CHECK(s.Text().empty());
    CHECK(s.Undo());
    CHECK(s.Text() == "abc");
    CHECK(s.Redo());
    CHECK(s.Text().empty());
    CHECK_FALSE(s.Redo());
}

TEST_CASE("Consecutive typing undoes as one step", "[slate][textedit]") {
    TextEditState s;
    s.Begin("", false);
    s.Insert("a");
    s.Insert("b");
    s.Insert("c");
    CHECK(s.Undo());
    CHECK(s.Text().empty());
}

TEST_CASE("Revert restores the focus-start text", "[slate][textedit]") {
    TextEditState s;
    s.Begin("10", true);
    CHECK(s.SelectedText() == "10");
    s.Insert("25");
    CHECK(s.Text() == "25");
    s.Revert();
    CHECK(s.Text() == "10");
    CHECK(s.Original() == "10");
}

TEST_CASE("UTF-8 caret steps by codepoint", "[slate][textedit]") {
    TextEditState s;
    s.Begin("a\xC3\xA9" "b", false);  // a, e-acute (2 bytes), b
    s.End(false);
    CHECK(s.Caret() == 4);
    s.MoveLeft(false, false);
    CHECK(s.Caret() == 3);
    s.MoveLeft(false, false);
    CHECK(s.Caret() == 1);
    s.SetCaret(2, false);  // inside the 2-byte sequence: snaps back
    CHECK(s.Caret() == 1);
    s.SetCaret(3, false);
    s.Backspace(false);
    CHECK(s.Text() == "ab");
}

TEST_CASE("maxLength truncates inserts", "[slate][textedit]") {
    TextEditState s;
    s.maxLength = 4;
    s.Begin("ab", false);
    s.End(false);
    s.Insert("cdef");
    CHECK(s.Text() == "abcd");
}

TEST_CASE("SelectWordAt picks the run", "[slate][textedit]") {
    TextEditState s;
    s.Begin("set value_2 now", false);
    s.SelectWordAt(6);
    CHECK(s.SelectedText() == "value_2");
}

TEST_CASE("CaretFromX picks the nearest boundary", "[slate][textedit]") {
    auto measure = [](std::string_view t) { return static_cast<f32>(t.size()) * 10.0f; };
    CHECK(CaretFromX("abcd", 14.0f, measure) == 1);
    CHECK(CaretFromX("abcd", 16.0f, measure) == 2);
    CHECK(CaretFromX("abcd", -5.0f, measure) == 0);
    CHECK(CaretFromX("abcd", 100.0f, measure) == 4);
}
