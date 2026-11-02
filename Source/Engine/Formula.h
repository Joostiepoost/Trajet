// Formula.h — a small expression language for formula generators.
//
// Variables:  t   position in the item, 0 at its start, 1 at its end
//             s   seconds since the item start
//             b   beats since the item start (musical items; equals s in linear items)
//             pi, e
// Operators:  + - * / % ^ (power), unary -, comparisons < > <= >= == != (give 1 or 0),
//             parentheses
// Functions:  sin cos tan asin acos atan abs sqrt exp log log10 floor ceil round frac
//             sign min max pow clamp(x, lo, hi) mod(x, y) step(edge, x) mix(a, b, f)
//             tri(x) saw(x) sq(x) (periodic shapes, period 1, range 0..1)
//             noise(x) (smooth random, 0..1)  rand(x) (stepped random per integer, 0..1)
//
// The result is clamped to 0..1 and mapped onto the generator's min..max range.
// Parsing happens on the message thread; evaluation allocates nothing, so the
// audio thread can run it.

#pragma once

#include <JuceHeader.h>

namespace Formula
{
    struct Variables
    {
        double t = 0.0, s = 0.0, b = 0.0;
    };

    struct Node
    {
        enum class Kind { number, varT, varS, varB, unaryMinus, add, sub, mul, div, mod, pow,
                          lt, gt, le, ge, eq, ne, function };
        Kind kind = Kind::number;
        double number = 0.0;
        int function = 0;
        std::vector<std::unique_ptr<Node>> args;

        double eval (const Variables&) const noexcept;
    };

    using Expression = std::shared_ptr<const Node>;

    /** Parses text; on failure returns nullptr and puts a message in error. */
    Expression parse (const juce::String& text, juce::String& error);

    /** Parse with a small cache (message thread only). */
    Expression parseCached (const juce::String& text, juce::String& error);

    /** Smooth deterministic noise and stepped random, 0..1 (also used by random generators). */
    double hash01 (int seed, juce::int64 index) noexcept;
    double valueNoise (int seed, double x) noexcept;
}
