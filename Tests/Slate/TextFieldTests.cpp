// EditableText / TextField / Tab focus: full text-field behaviour driven
// through Context with synthetic input (events are fed between frames, as the
// app's event pump does).

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Luma/Core/Types.h"
#include "Luma/Slate/Context.h"
#include "Luma/Slate/Menu.h"
#include "SlateTestHarness.h"

using Luma::Slate::Context;
using Luma::Slate::Rect;
using Luma::Slate::TextCommit;
using Luma::Slate::TextResult;
using Luma::Slate::Test::Harness;

namespace {
constexpr int kEnter = 257, kEscape = 256, kTab = 258, kCtrl = 341, kShift = 340;
constexpr int kA = 65, kC = 67, kV = 86;

void Type(Context& ctx, std::string_view s) {
    for (char c : s) ctx.OnText(static_cast<Luma::u32>(c));
}
void Key(Context& ctx, int key) {
    ctx.OnKey(key, true);
    ctx.OnKey(key, false);
}
}  // namespace

TEST_CASE("Typing inserts at caret and Enter commits", "[slate][textfield]") {
    Harness h;
    std::string text;
    const Rect r{10, 10, 200, 24};
    TextResult res;
    h.Click(20, 20, [&] { res = h.ctx.EditableText(1, r, text); });
    CHECK(h.ctx.IsFieldFocused(1));
    Type(h.ctx, "hi");
    h.Frame([&] { res = h.ctx.EditableText(1, r, text); });
    CHECK(text == "hi");
    CHECK(res.changed);
    Key(h.ctx, kEnter);
    h.Frame([&] { res = h.ctx.EditableText(1, r, text); });
    CHECK(res.commit == TextCommit::OnEnter);
    CHECK_FALSE(h.ctx.IsFieldFocused(1));
}

TEST_CASE("Escape reverts and reports OnCleared", "[slate][textfield]") {
    Harness h;
    std::string text = "10";
    const Rect r{10, 10, 200, 24};
    TextResult res;
    h.Click(200, 20, [&] { h.ctx.EditableText(1, r, text); });
    Type(h.ctx, "5");
    h.Frame([&] { h.ctx.EditableText(1, r, text); });
    CHECK(text == "105");
    Key(h.ctx, kEscape);
    h.Frame([&] { res = h.ctx.EditableText(1, r, text); });
    CHECK(text == "10");
    CHECK(res.commit == TextCommit::OnCleared);
    CHECK_FALSE(h.ctx.IsFieldFocused(1));
}

TEST_CASE("Clicking another field commits the first and leaves the second intact",
          "[slate][textfield]") {
    Harness h;
    std::string a = "aaa", b = "bbb";
    const Rect ra{10, 10, 200, 24}, rb{10, 50, 200, 24};
    TextResult resA, resB;
    // B is drawn before A: the hardest order for the handoff.
    auto draw = [&] {
        resB = h.ctx.EditableText(2, rb, b);
        resA = h.ctx.EditableText(1, ra, a);
    };
    h.Click(200, 20, draw);
    Type(h.ctx, "x");
    h.Frame(draw);
    CHECK(a == "aaax");
    h.Click(200, 60, [&] {
        draw();
        if (resA.commit == TextCommit::OnFocusLost) resA.changed = true;  // keep the signal
    });
    CHECK(h.ctx.IsFieldFocused(2));
    CHECK(b == "bbb");
    CHECK(a == "aaax");
    Type(h.ctx, "y");
    h.Frame(draw);
    CHECK(b == "bbby");
    CHECK(a == "aaax");
}

TEST_CASE("Focus loss is reported once to the field that lost it", "[slate][textfield]") {
    Harness h;
    std::string a = "a", b = "b";
    const Rect ra{10, 10, 200, 24}, rb{10, 50, 200, 24};
    int lost = 0;
    auto draw = [&] {
        if (h.ctx.EditableText(1, ra, a).commit == TextCommit::OnFocusLost) ++lost;
        h.ctx.EditableText(2, rb, b);
    };
    h.Click(200, 20, draw);
    h.Click(200, 60, draw);
    h.Frame(draw);
    CHECK(lost == 1);
}

TEST_CASE("Ctrl+A then Ctrl+C copies all text", "[slate][textfield]") {
    Harness h;
    std::string text = "hello";
    const Rect r{10, 10, 200, 24};
    h.Click(20, 20, [&] { h.ctx.EditableText(1, r, text); });
    h.ctx.OnKey(kCtrl, true);
    Key(h.ctx, kA);
    h.Frame([&] { h.ctx.EditableText(1, r, text); });
    Key(h.ctx, kC);
    h.Frame([&] { h.ctx.EditableText(1, r, text); });
    h.ctx.OnKey(kCtrl, false);
    CHECK(h.ctx.ClipboardText() == "hello");
}

TEST_CASE("Ctrl+V pastes over the selection", "[slate][textfield]") {
    Harness h;
    std::string text = "abc";
    const Rect r{10, 10, 200, 24};
    h.ctx.SetClipboardText("XY");
    h.Click(20, 20, [&] { h.ctx.EditableText(1, r, text); });
    h.ctx.OnKey(kCtrl, true);
    Key(h.ctx, kA);
    h.Frame([&] { h.ctx.EditableText(1, r, text); });
    Key(h.ctx, kV);
    h.Frame([&] { h.ctx.EditableText(1, r, text); });
    h.ctx.OnKey(kCtrl, false);
    CHECK(text == "XY");
}

TEST_CASE("Double-click selects a word", "[slate][textfield]") {
    Harness h;
    std::string text = "one two";
    const Rect r{10, 10, 300, 24};
    // Double-click past the end of the text: the caret lands on the last
    // word, which gets selected; typing replaces it.
    h.Click(290, 20, [&] { h.ctx.EditableText(1, r, text); });
    h.Click(290, 20, [&] { h.ctx.EditableText(1, r, text); });
    Type(h.ctx, "X");
    h.Frame([&] { h.ctx.EditableText(1, r, text); });
    CHECK(text == "one X");
}

TEST_CASE("Tab moves focus through registered fields and wraps", "[slate][textfield]") {
    Harness h;
    std::string a, b, c;
    auto draw = [&] {
        h.ctx.EditableText(1, {10, 10, 100, 24}, a);
        h.ctx.EditableText(2, {10, 40, 100, 24}, b);
        h.ctx.EditableText(3, {10, 70, 100, 24}, c);
    };
    h.Click(20, 20, draw);
    REQUIRE(h.ctx.IsFieldFocused(1));
    Key(h.ctx, kTab);
    h.Frame(draw);
    h.Frame(draw);
    CHECK(h.ctx.IsFieldFocused(2));
    h.ctx.OnKey(kShift, true);
    Key(h.ctx, kTab);
    h.Frame(draw);
    h.Frame(draw);
    CHECK(h.ctx.IsFieldFocused(1));
    Key(h.ctx, kTab);
    h.Frame(draw);
    h.Frame(draw);
    h.ctx.OnKey(kShift, false);
    CHECK(h.ctx.IsFieldFocused(3));
}

TEST_CASE("Typed text is ignored while a menu is open", "[slate][textfield]") {
    Harness h;
    std::string text = "keep";
    const Rect r{10, 10, 200, 24};
    h.Click(20, 20, [&] { h.ctx.EditableText(1, r, text); });
    Luma::Slate::MenuBuilder b;
    b.AddEntry("Item", {}, [] {});
    h.Frame([&] {
        h.ctx.EditableText(1, r, text);
        h.ctx.OpenMenu(99, {400, 400}, b.Build());
    });
    Type(h.ctx, "zz");
    h.Frame([&] { h.ctx.EditableText(1, r, text); });
    CHECK(text == "keep");
}

TEST_CASE("TextField keeps its bool API", "[slate][textfield]") {
    Harness h;
    std::string text;
    const Rect r{10, 10, 200, 24};
    h.Click(20, 20, [&] { h.ctx.TextField(5, r, text); });
    Type(h.ctx, "a");
    bool changed = false;
    h.Frame([&] { changed = h.ctx.TextField(5, r, text); });
    CHECK(changed);
    CHECK(text == "a");
    CHECK(h.ctx.textFieldFocused());
}

TEST_CASE("Clicking places the caret under the pointer", "[slate][textfield]") {
    Harness h;
    std::string text = "abc";
    const Rect r{10, 10, 200, 24};
    h.Click(12, 20, [&] { h.ctx.EditableText(1, r, text); });  // left of the text
    Type(h.ctx, "Z");
    h.Frame([&] { h.ctx.EditableText(1, r, text); });
    CHECK(text == "Zabc");
    h.Click(205, 20, [&] { h.ctx.EditableText(1, r, text); });  // past the end
    Type(h.ctx, "!");
    h.Frame([&] { h.ctx.EditableText(1, r, text); });
    CHECK(text == "Zabc!");
}
