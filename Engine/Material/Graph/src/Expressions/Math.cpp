// Math expressions.

#include "../ExpressionBuilder.h"

namespace Luma::MaterialGraph::Detail {

namespace {

void Binary(ExpressionRegistry& r, const char* type, const char* display, const char* desc,
            f64 a, f64 b, std::initializer_list<const char*> kw = {}) {
    r.Add(Expr(type, display, "Math", desc)
              .In("A", VT::Any, F(a))
              .In("B", VT::Any, F(b))
              .Out("", VT::Any)
              .Keywords(kw)
              .Build());
}

void Unary(ExpressionRegistry& r, const char* type, const char* display, const char* desc,
           std::initializer_list<const char*> kw = {}) {
    r.Add(Expr(type, display, "Math", desc).In("Input", VT::Any, F(0.0)).Out("", VT::Any).Keywords(kw).Build());
}

}  // namespace

void RegisterMath(ExpressionRegistry& r) {
    Binary(r, "Add", "Add", "A + B (a Float1 broadcasts to the other width).", 0, 1, {"+", "plus"});
    Binary(r, "Subtract", "Subtract", "A - B.", 0, 1, {"-", "minus"});
    Binary(r, "Multiply", "Multiply", "A * B, component-wise.", 0, 1, {"*", "times"});
    Binary(r, "Divide", "Divide", "A / B, component-wise.", 1, 1, {"/"});
    r.Add(Expr("Power", "Power", "Math", "Base raised to Exp.")
              .In("Base", VT::Any, F(0.0))
              .In("Exp", VT::Any, F(2.0))
              .Out("", VT::Any)
              .Keywords({"pow", "^"})
              .Build());
    Unary(r, "Abs", "Abs", "Absolute value.");
    Binary(r, "Min", "Min", "Component-wise minimum.", 0, 0);
    Binary(r, "Max", "Max", "Component-wise maximum.", 0, 0);
    r.Add(Expr("Clamp", "Clamp", "Math", "Clamps Input between Min and Max.")
              .In("Input", VT::Any, F(0.0))
              .In("Min", VT::Any, F(0.0))
              .In("Max", VT::Any, F(1.0))
              .Out("", VT::Any)
              .Build());
    Unary(r, "Saturate", "Saturate", "Clamps to [0, 1].", {"clamp01"});
    Unary(r, "OneMinus", "1-x", "1 - Input.", {"invert", "oneminus"});
    r.Add(Expr("Lerp", "Lerp", "Math", "Linear interpolation: A when Alpha = 0, B when Alpha = 1.")
              .In("A", VT::Any, F(0.0))
              .In("B", VT::Any, F(1.0))
              .In("Alpha", VT::Any, F(0.5))
              .Out("", VT::Any)
              .Keywords({"linear", "interpolate", "mix", "blend"})
              .Build());
    r.Add(Expr("Dot", "Dot", "Math", "Dot product of two vectors of the same width.")
              .In("A", VT::Any, F(0.0))
              .In("B", VT::Any, F(0.0))
              .Out("", VT::Float1)
              .Build());
    r.Add(Expr("Cross", "Cross", "Math", "Cross product of two Float3.")
              .In("A", VT::Float3, F(0.0))
              .In("B", VT::Float3, F(0.0))
              .Out("", VT::Float3)
              .Build());
    Unary(r, "Normalize", "Normalize", "Scales a vector to unit length.");
    r.Add(Expr("Length", "Length", "Math", "Length of a vector.")
              .In("Input", VT::Any, F(0.0))
              .Out("", VT::Float1)
              .Build());
    r.Add(Expr("Distance", "Distance", "Math", "Distance between A and B.")
              .In("A", VT::Any, F(0.0))
              .In("B", VT::Any, F(0.0))
              .Out("", VT::Float1)
              .Build());
    Unary(r, "SquareRoot", "Square Root", "sqrt(Input).", {"sqrt"});
    Unary(r, "Exp", "Exp", "e raised to Input.");
    Unary(r, "Log2", "Log2", "Base-2 logarithm.");
    for (const auto& [type, desc] : {std::pair{"Sine", "sin(Input * 2pi / Period); Period 0 = radians."},
                                     std::pair{"Cosine", "cos(Input * 2pi / Period); Period 0 = radians."}}) {
        r.Add(Expr(type, type, "Math", desc)
                  .In("Input", VT::Any, F(0.0))
                  .Out("", VT::Any)
                  .Prop("period", "Period", PK::Float, F(1.0),
                        "Input units per full cycle; 0 = Input is in radians.")
                  .Build());
    }
    Unary(r, "Tangent", "Tangent", "tan(Input) in radians.");
    Unary(r, "Arcsine", "Arcsine", "asin(Input) in radians.");
    Unary(r, "Arccosine", "Arccosine", "acos(Input) in radians.");
    r.Add(Expr("Arctangent2", "Arctangent2", "Math", "atan2(Y, X) in radians.")
              .In("Y", VT::Any, F(0.0))
              .In("X", VT::Any, F(1.0))
              .Out("", VT::Any)
              .Keywords({"atan2"})
              .Build());
    Unary(r, "Floor", "Floor", "Rounds down.");
    Unary(r, "Ceil", "Ceil", "Rounds up.");
    Unary(r, "Round", "Round", "Rounds to the nearest integer.");
    Unary(r, "Frac", "Frac", "Fractional part: x - floor(x).", {"fraction"});
    Binary(r, "Fmod", "Fmod", "Floating-point remainder of A / B (sign of A).", 0, 1, {"mod", "%"});
    Unary(r, "Sign", "Sign", "-1, 0 or 1.");
    r.Add(Expr("Step", "Step", "Math", "1 where X >= Y, else 0.")
              .In("Y", VT::Any, F(0.5))
              .In("X", VT::Any, F(0.0))
              .Out("", VT::Any)
              .Build());
    r.Add(Expr("SmoothStep", "SmoothStep", "Math", "Hermite interpolation between Min and Max.")
              .In("Min", VT::Any, F(0.0))
              .In("Max", VT::Any, F(1.0))
              .In("Value", VT::Any, F(0.5))
              .Out("", VT::Any)
              .Build());
    r.Add(Expr("If", "If", "Math",
               "Compares A and B and outputs one of three inputs (A > B, A == B within the "
               "threshold, A < B).")
              .In("A", VT::Float1, F(0.0))
              .In("B", VT::Float1, F(0.0))
              .In("AGreaterThanB", VT::Any, F(0.0))
              .In("AEqualsB", VT::Any, F(0.0))
              .In("ALessThanB", VT::Any, F(0.0))
              .Out("", VT::Any)
              .Prop("threshold", "Equals Threshold", PK::Float, F(0.00001))
              .Keywords({"compare", "branch", "condition"})
              .Build());
    r.Add(Expr("AppendVector", "Append", "Math", "Concatenates A and B into a wider vector.")
              .In("A", VT::Any, F(0.0))
              .In("B", VT::Any, F(0.0))
              .Out("", VT::Any)
              .Keywords({"append", "combine", "concat"})
              .Build());
    r.Add(Expr("ComponentMask", "Component Mask", "Math", "Keeps the selected channels in RGBA order.")
              .In("Input", VT::Any, F(0.0))
              .Out("", VT::Any)
              .Prop("r", "R", PK::Bool, B(true))
              .Prop("g", "G", PK::Bool, B(true))
              .Prop("b", "B", PK::Bool, B(false))
              .Prop("a", "A", PK::Bool, B(false))
              .Keywords({"mask", "swizzle", "channel"})
              .Build());
    r.Add(Expr("BreakOutFloat2Components", "Break Out Float2", "Math", "Splits a Float2.")
              .In("Float", VT::Float2, F(0.0))
              .Out("R", VT::Float1)
              .Out("G", VT::Float1)
              .Build());
    r.Add(Expr("BreakOutFloat3Components", "Break Out Float3", "Math", "Splits a Float3.")
              .In("Float", VT::Float3, F(0.0))
              .Out("R", VT::Float1)
              .Out("G", VT::Float1)
              .Out("B", VT::Float1)
              .Build());
    r.Add(Expr("BreakOutFloat4Components", "Break Out Float4", "Math", "Splits a Float4.")
              .In("Float", VT::Float4, F(0.0))
              .Out("R", VT::Float1)
              .Out("G", VT::Float1)
              .Out("B", VT::Float1)
              .Out("A", VT::Float1)
              .Build());
    r.Add(Expr("MakeFloat2", "Make Float2", "Math", "Builds a Float2.")
              .In("X", VT::Float1, F(0.0))
              .In("Y", VT::Float1, F(0.0))
              .Out("", VT::Float2)
              .Build());
    r.Add(Expr("MakeFloat3", "Make Float3", "Math", "Builds a Float3.")
              .In("X", VT::Float1, F(0.0))
              .In("Y", VT::Float1, F(0.0))
              .In("Z", VT::Float1, F(0.0))
              .Out("", VT::Float3)
              .Build());
    r.Add(Expr("MakeFloat4", "Make Float4", "Math", "Builds a Float4.")
              .In("X", VT::Float1, F(0.0))
              .In("Y", VT::Float1, F(0.0))
              .In("Z", VT::Float1, F(0.0))
              .In("W", VT::Float1, F(0.0))
              .Out("", VT::Float4)
              .Build());
    r.Add(Expr("Desaturation", "Desaturation", "Math",
               "Blends a colour toward its luminance by Fraction.")
              .In("Input", VT::Float3, F(0.0))
              .In("Fraction", VT::Float1, F(1.0))
              .Out("", VT::Float3)
              .Prop("luminanceFactors", "Luminance Factors", PK::Vec4, V(0.3f, 0.59f, 0.11f, 0))
              .Keywords({"grayscale", "greyscale", "saturation"})
              .Build());
    r.Add(Expr("RemapValueRange", "Remap Value Range", "Math",
               "Maps Input from [Input Low, Input High] to [Target Low, Target High].")
              .In("Input", VT::Any, F(0.5))
              .In("InputLow", VT::Any, F(0.0))
              .In("InputHigh", VT::Any, F(1.0))
              .In("TargetLow", VT::Any, F(0.0))
              .In("TargetHigh", VT::Any, F(1.0))
              .Out("", VT::Any)
              .Keywords({"remap", "range", "map"})
              .Build());
}

}  // namespace Luma::MaterialGraph::Detail
