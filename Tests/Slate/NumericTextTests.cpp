// NumericText: the math-expression evaluator behind typed spin-box entry and
// the trimmed number formatting spin boxes display.

#include <catch2/catch_test_macros.hpp>

#include "Luma/Slate/NumericText.h"

using Luma::Slate::EvaluateNumericExpression;
using Luma::Slate::FormatNumber;

TEST_CASE("Numeric expressions evaluate", "[slate][numeric]") {
    CHECK(EvaluateNumericExpression("2*3.5") == 7.0);
    CHECK(EvaluateNumericExpression("=10/4") == 2.5);
    CHECK(EvaluateNumericExpression("-(1+2)*-2") == 6.0);
    CHECK(EvaluateNumericExpression(" 1 + 2 * 3 ") == 7.0);
    CHECK(EvaluateNumericExpression("1e3") == 1000.0);
    CHECK(EvaluateNumericExpression("8-2-1") == 5.0);
    CHECK(EvaluateNumericExpression(".5") == 0.5);
}

TEST_CASE("Invalid numeric expressions are rejected", "[slate][numeric]") {
    CHECK_FALSE(EvaluateNumericExpression("1/0").has_value());
    CHECK_FALSE(EvaluateNumericExpression("2+").has_value());
    CHECK_FALSE(EvaluateNumericExpression("abc").has_value());
    CHECK_FALSE(EvaluateNumericExpression("").has_value());
    CHECK_FALSE(EvaluateNumericExpression("(1+2").has_value());
    CHECK_FALSE(EvaluateNumericExpression("1 2").has_value());
}

TEST_CASE("Numbers format with trimmed fractional digits", "[slate][numeric]") {
    CHECK(FormatNumber(1.0, 1, 3) == "1.0");
    CHECK(FormatNumber(1.23456, 1, 3) == "1.235");
    CHECK(FormatNumber(2.5, 0, 3) == "2.5");
    CHECK(FormatNumber(3.0, 0, 0) == "3");
    CHECK(FormatNumber(3.0, 0, 3) == "3");
    CHECK(FormatNumber(-0.0001, 1, 3) == "0.0");
    CHECK(FormatNumber(-1.5, 1, 3) == "-1.5");
}
