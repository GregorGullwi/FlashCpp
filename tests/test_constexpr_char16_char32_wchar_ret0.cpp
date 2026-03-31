// T30: Test constexpr default-construction of wide/Unicode char types via both
// direct keyword syntax and templates.
// Verifies the parser accepts direct wchar_t{} / char16_t{} / char32_t{} and the
// constexpr evaluator handles the resulting WChar/Char16/Char32 zero-init path.
//
// Uses both direct and template T{} paths to reach evaluate_constructor_call with
// zero args, covering the switch cases that were missing before the fix.

constexpr char16_t direct16 = char16_t{};
constexpr char32_t direct32 = char32_t{};
constexpr wchar_t directwc = wchar_t{};

template <typename T>
constexpr T zero_init() { return T{}; }

// Each instantiation exercises a separate switch case in ConstExprEvaluator_Core.cpp
constexpr char16_t v16 = zero_init<char16_t>();
constexpr char32_t v32 = zero_init<char32_t>();
constexpr wchar_t vwc = zero_init<wchar_t>();

// Mix with a struct containing these types
struct WideChars {
	char16_t a;
	char32_t b;
	wchar_t c;
};

template <typename T>
struct Box {
	T value;
	constexpr T get() const { return value; }
};

int main() {
	if (direct16 != static_cast<char16_t>(0))
		return 1;
	if (direct32 != static_cast<char32_t>(0))
		return 2;
	if (directwc != static_cast<wchar_t>(0))
		return 3;

	if (v16 != static_cast<char16_t>(0))
		return 4;
	if (v32 != static_cast<char32_t>(0))
		return 5;
	if (vwc != static_cast<wchar_t>(0))
		return 6;

	// Verify the template works at runtime too
	char16_t r16 = zero_init<char16_t>();
	char32_t r32 = zero_init<char32_t>();
	wchar_t rwc = zero_init<wchar_t>();
	if (r16 != 0)
		return 7;
	if (r32 != 0)
		return 8;
	if (rwc != 0)
		return 9;

	return 0;
}
