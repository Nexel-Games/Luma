#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "Luma/Core/Types.h"

// Text <-> number helpers for numeric entry widgets (SpinBox, VectorField).

namespace Luma::Slate {

// Evaluates a typed numeric expression: decimal / exponent literals, + - * /,
// parentheses, unary minus/plus, whitespace, and an optional leading '='
// (spreadsheet habit). Returns nullopt for malformed input or a division by
// zero.
std::optional<f64> EvaluateNumericExpression(std::string_view text);

// Fixed-point text with at most `maxFractionalDigits` decimals, trailing
// zeros trimmed down to `minFractionalDigits` (the '.' goes when that is 0).
// Never produces a negative zero ("-0.0").
std::string FormatNumber(f64 value, int minFractionalDigits, int maxFractionalDigits);

}  // namespace Luma::Slate
