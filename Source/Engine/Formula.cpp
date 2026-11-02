#include "Formula.h"

namespace Formula
{
    //==============================================================================
    double hash01 (int seed, juce::int64 index) noexcept
    {
        // SplitMix64: fast, well-mixed, the same everywhere.
        auto z = (juce::uint64) index * 0x9E3779B97F4A7C15ull + (juce::uint64) (juce::uint32) seed * 0xD1B54A32D192ED03ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return (double) (z >> 11) * (1.0 / 9007199254740992.0);
    }

    double valueNoise (int seed, double x) noexcept
    {
        const double fl = std::floor (x);
        const auto i = (juce::int64) fl;
        const double f = x - fl;
        const double u = f * f * (3.0 - 2.0 * f);   // smoothstep
        return hash01 (seed, i) * (1.0 - u) + hash01 (seed, i + 1) * u;
    }

    //==============================================================================
    namespace
    {
        struct FunctionInfo { const char* name; int args; };
        const FunctionInfo functions[] = {
            { "sin", 1 }, { "cos", 1 }, { "tan", 1 }, { "asin", 1 }, { "acos", 1 }, { "atan", 1 },
            { "abs", 1 }, { "sqrt", 1 }, { "exp", 1 }, { "log", 1 }, { "log10", 1 }, { "floor", 1 },
            { "ceil", 1 }, { "round", 1 }, { "frac", 1 }, { "sign", 1 }, { "min", 2 }, { "max", 2 },
            { "pow", 2 }, { "clamp", 3 }, { "mod", 2 }, { "step", 2 }, { "mix", 3 },
            { "tri", 1 }, { "saw", 1 }, { "sq", 1 }, { "noise", 1 }, { "rand", 1 } };
        constexpr int numFunctions = (int) (sizeof (functions) / sizeof (functions[0]));

        double frac (double x) noexcept { return x - std::floor (x); }

        double callFunction (int f, const double* a) noexcept
        {
            switch (f)
            {
                case 0:  return std::sin (a[0]);
                case 1:  return std::cos (a[0]);
                case 2:  return std::tan (a[0]);
                case 3:  return std::asin (juce::jlimit (-1.0, 1.0, a[0]));
                case 4:  return std::acos (juce::jlimit (-1.0, 1.0, a[0]));
                case 5:  return std::atan (a[0]);
                case 6:  return std::abs (a[0]);
                case 7:  return std::sqrt (juce::jmax (0.0, a[0]));
                case 8:  return std::exp (juce::jmin (700.0, a[0]));
                case 9:  return a[0] > 0.0 ? std::log (a[0]) : 0.0;
                case 10: return a[0] > 0.0 ? std::log10 (a[0]) : 0.0;
                case 11: return std::floor (a[0]);
                case 12: return std::ceil (a[0]);
                case 13: return std::round (a[0]);
                case 14: return frac (a[0]);
                case 15: return a[0] > 0.0 ? 1.0 : (a[0] < 0.0 ? -1.0 : 0.0);
                case 16: return juce::jmin (a[0], a[1]);
                case 17: return juce::jmax (a[0], a[1]);
                case 18: return std::pow (a[0], a[1]);
                case 19: return juce::jlimit (juce::jmin (a[1], a[2]), juce::jmax (a[1], a[2]), a[0]);
                case 20: return std::abs (a[1]) > 1.0e-300 ? a[0] - a[1] * std::floor (a[0] / a[1]) : 0.0;
                case 21: return a[1] >= a[0] ? 1.0 : 0.0;
                case 22: return a[0] + (a[1] - a[0]) * a[2];
                case 23: { const double p = frac (a[0]); return p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p; }
                case 24: return frac (a[0]);
                case 25: return frac (a[0]) < 0.5 ? 1.0 : 0.0;
                case 26: return valueNoise (7919, a[0]);
                case 27: return hash01 (104729, (juce::int64) std::floor (a[0]));
                default: return 0.0;
            }
        }

        //==============================================================================
        class Parser
        {
        public:
            explicit Parser (const juce::String& t) : text (t.toStdString()) {}

            std::unique_ptr<Node> parseAll (juce::String& error)
            {
                auto node = comparison();
                skipSpace();
                if (node != nullptr && pos < text.size())
                    fail ("unexpected '" + juce::String::charToString ((juce::juce_wchar) text[pos]) + "'");
                if (! problem.isEmpty()) { error = problem; return nullptr; }
                return node;
            }

        private:
            std::string text;
            size_t pos = 0;
            juce::String problem;

            void fail (const juce::String& message)
            {
                if (problem.isEmpty())
                    problem = message + " (at position " + juce::String ((int) pos + 1) + ")";
            }

            void skipSpace()    { while (pos < text.size() && std::isspace ((unsigned char) text[pos])) ++pos; }
            bool accept (char c) { skipSpace(); if (pos < text.size() && text[pos] == c) { ++pos; return true; } return false; }
            bool accept (const char* s)
            {
                skipSpace();
                const size_t n = std::strlen (s);
                if (text.compare (pos, n, s) == 0) { pos += n; return true; }
                return false;
            }

            static std::unique_ptr<Node> make (Node::Kind k, std::unique_ptr<Node> a, std::unique_ptr<Node> b = nullptr)
            {
                auto n = std::make_unique<Node>();
                n->kind = k;
                if (a) n->args.push_back (std::move (a));
                if (b) n->args.push_back (std::move (b));
                return n;
            }

            std::unique_ptr<Node> comparison()
            {
                auto left = additive();
                while (left != nullptr)
                {
                    Node::Kind k;
                    if      (accept ("<=")) k = Node::Kind::le;
                    else if (accept (">=")) k = Node::Kind::ge;
                    else if (accept ("==")) k = Node::Kind::eq;
                    else if (accept ("!=")) k = Node::Kind::ne;
                    else if (accept ('<'))  k = Node::Kind::lt;
                    else if (accept ('>'))  k = Node::Kind::gt;
                    else break;
                    auto right = additive();
                    if (right == nullptr) return nullptr;
                    left = make (k, std::move (left), std::move (right));
                }
                return left;
            }

            std::unique_ptr<Node> additive()
            {
                auto left = multiplicative();
                while (left != nullptr)
                {
                    Node::Kind k;
                    if      (accept ('+')) k = Node::Kind::add;
                    else if (accept ('-')) k = Node::Kind::sub;
                    else break;
                    auto right = multiplicative();
                    if (right == nullptr) return nullptr;
                    left = make (k, std::move (left), std::move (right));
                }
                return left;
            }

            std::unique_ptr<Node> multiplicative()
            {
                auto left = unary();
                while (left != nullptr)
                {
                    Node::Kind k;
                    if      (accept ('*')) k = Node::Kind::mul;
                    else if (accept ('/')) k = Node::Kind::div;
                    else if (accept ('%')) k = Node::Kind::mod;
                    else break;
                    auto right = unary();
                    if (right == nullptr) return nullptr;
                    left = make (k, std::move (left), std::move (right));
                }
                return left;
            }

            std::unique_ptr<Node> unary()
            {
                if (accept ('-')) { auto a = unary(); return a ? make (Node::Kind::unaryMinus, std::move (a)) : nullptr; }
                if (accept ('+')) return unary();
                return power();
            }

            std::unique_ptr<Node> power()
            {
                auto base = primary();
                if (base != nullptr && accept ('^'))
                {
                    auto exponent = unary();   // right-associative: 2^3^2 = 2^(3^2)
                    if (exponent == nullptr) return nullptr;
                    return make (Node::Kind::pow, std::move (base), std::move (exponent));
                }
                return base;
            }

            std::unique_ptr<Node> primary()
            {
                skipSpace();
                if (pos >= text.size()) { fail ("the formula ends too early"); return nullptr; }

                const char c = text[pos];

                if (std::isdigit ((unsigned char) c) || c == '.')
                {
                    const size_t start = pos;
                    while (pos < text.size() && (std::isdigit ((unsigned char) text[pos]) || text[pos] == '.')) ++pos;
                    if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')
                        && pos + 1 < text.size() && (std::isdigit ((unsigned char) text[pos + 1]) || text[pos + 1] == '-' || text[pos + 1] == '+'))
                    {
                        pos += 2;
                        while (pos < text.size() && std::isdigit ((unsigned char) text[pos])) ++pos;
                    }
                    auto n = std::make_unique<Node>();
                    n->number = std::atof (text.substr (start, pos - start).c_str());
                    return n;
                }

                if (c == '(')
                {
                    ++pos;
                    auto inner = comparison();
                    if (inner == nullptr) return nullptr;
                    if (! accept (')')) { fail ("missing ')'"); return nullptr; }
                    return inner;
                }

                if (std::isalpha ((unsigned char) c) || c == '_')
                {
                    const size_t start = pos;
                    while (pos < text.size() && (std::isalnum ((unsigned char) text[pos]) || text[pos] == '_')) ++pos;
                    const auto name = text.substr (start, pos - start);

                    if (name == "t")  { auto n = std::make_unique<Node>(); n->kind = Node::Kind::varT; return n; }
                    if (name == "s")  { auto n = std::make_unique<Node>(); n->kind = Node::Kind::varS; return n; }
                    if (name == "b")  { auto n = std::make_unique<Node>(); n->kind = Node::Kind::varB; return n; }
                    if (name == "pi") { auto n = std::make_unique<Node>(); n->number = juce::MathConstants<double>::pi; return n; }
                    if (name == "e")  { auto n = std::make_unique<Node>(); n->number = juce::MathConstants<double>::euler; return n; }

                    for (int f = 0; f < numFunctions; ++f)
                        if (name == functions[f].name)
                        {
                            if (! accept ('(')) { fail ("'" + juce::String (name) + "' needs ( ) after it"); return nullptr; }
                            auto n = std::make_unique<Node>();
                            n->kind = Node::Kind::function;
                            n->function = f;
                            for (int a = 0; a < functions[f].args; ++a)
                            {
                                if (a > 0 && ! accept (',')) { fail ("'" + juce::String (name) + "' needs " + juce::String (functions[f].args) + " values"); return nullptr; }
                                auto arg = comparison();
                                if (arg == nullptr) return nullptr;
                                n->args.push_back (std::move (arg));
                            }
                            if (! accept (')')) { fail ("missing ')' after " + juce::String (name)); return nullptr; }
                            return n;
                        }

                    fail ("unknown name '" + juce::String (name) + "'");
                    return nullptr;
                }

                fail ("unexpected '" + juce::String::charToString ((juce::juce_wchar) c) + "'");
                return nullptr;
            }
        };
    }

    //==============================================================================
    double Node::eval (const Variables& v) const noexcept
    {
        auto a = [&] (size_t i) { return args[i]->eval (v); };

        switch (kind)
        {
            case Kind::number:     return number;
            case Kind::varT:       return v.t;
            case Kind::varS:       return v.s;
            case Kind::varB:       return v.b;
            case Kind::unaryMinus: return -a (0);
            case Kind::add:        return a (0) + a (1);
            case Kind::sub:        return a (0) - a (1);
            case Kind::mul:        return a (0) * a (1);
            case Kind::div:        { const double d = a (1); return std::abs (d) > 1.0e-300 ? a (0) / d : 0.0; }
            case Kind::mod:        { const double d = a (1); return std::abs (d) > 1.0e-300 ? std::fmod (a (0), d) : 0.0; }
            case Kind::pow:        return std::pow (a (0), a (1));
            case Kind::lt:         return a (0) <  a (1) ? 1.0 : 0.0;
            case Kind::gt:         return a (0) >  a (1) ? 1.0 : 0.0;
            case Kind::le:         return a (0) <= a (1) ? 1.0 : 0.0;
            case Kind::ge:         return a (0) >= a (1) ? 1.0 : 0.0;
            case Kind::eq:         return std::abs (a (0) - a (1)) < 1.0e-9 ? 1.0 : 0.0;
            case Kind::ne:         return std::abs (a (0) - a (1)) >= 1.0e-9 ? 1.0 : 0.0;
            case Kind::function:
            {
                double values[3] = { 0.0, 0.0, 0.0 };
                for (size_t i = 0; i < args.size() && i < 3; ++i)
                    values[i] = a (i);
                return callFunction (function, values);
            }
            default: return 0.0;
        }
    }

    Expression parse (const juce::String& text, juce::String& error)
    {
        error = {};
        if (text.trim().isEmpty()) { error = "empty formula"; return nullptr; }
        Parser parser (text);
        auto node = parser.parseAll (error);
        return node != nullptr ? Expression (std::move (node)) : nullptr;
    }

    Expression parseCached (const juce::String& text, juce::String& error)
    {
        static std::map<juce::String, std::pair<Expression, juce::String>> cache;
        auto it = cache.find (text);
        if (it != cache.end()) { error = it->second.second; return it->second.first; }

        if (cache.size() > 200) cache.clear();
        auto expr = parse (text, error);
        cache[text] = { expr, error };
        return expr;
    }
}
