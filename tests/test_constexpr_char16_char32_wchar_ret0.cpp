// T30: Test constexpr default-construction of wide/Unicode char types via templates.
// Verifies the constexpr evaluator handles WChar/Char8/Char16/Char32 in T{} constructor.
//
// Uses template T{} path to reach evaluate_constructor_call with zero args,
// covering the switch cases that were missing before the fix.

template<typename T>
constexpr T zero_init() { return T{}; }

// Each instantiation exercises a separate switch case in ConstExprEvaluator_Core.cpp
constexpr char16_t v16  = zero_init<char16_t>();
constexpr char32_t v32  = zero_init<char32_t>();
constexpr wchar_t  vwc  = zero_init<wchar_t>();

// Mix with a struct containing these types
struct WideChars {
    char16_t a;
    char32_t b;
    wchar_t  c;
};

template<typename T>
struct Box {
    T value;
    constexpr T get() const { return value; }
};

int main() {
    if (v16 != static_cast<char16_t>(0)) return 1;
    if (v32 != static_cast<char32_t>(0)) return 2;
    if (vwc != static_cast<wchar_t>(0))  return 3;

    // Verify the template works at runtime too
    char16_t r16 = zero_init<char16_t>();
    char32_t r32 = zero_init<char32_t>();
    wchar_t  rwc = zero_init<wchar_t>();
    if (r16 != 0) return 4;
    if (r32 != 0) return 5;
    if (rwc != 0) return 6;

    return 0;
}
