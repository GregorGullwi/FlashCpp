// A callable partial whose exception specification is a dependent noexcept
// expression must publish a canonical pattern environment keyed by the
// expression's ExprId. Plain non-noexcept callables keep matching the
// non-deduced exception specification; a plain noexcept or a different
// parameter list selects the primary.
// Regression for the name-keyed fallback: the record must be found through the
// published environment, never through the synthesized $pattern spelling.

template <class T>
struct Signal {
	static constexpr int value = 0;
	static constexpr int width = 0;
};

template <class R, class T>
struct Signal<R (*)(T) noexcept(noexcept(T()))> {
	static constexpr int value = 1;
	static constexpr int width = sizeof(T);
};

// A bare function keeps its own shape with the same dependent specification.
template <class R, class T>
struct Signal<R (T) noexcept(noexcept(T()))> {
	static constexpr int value = 2;
	static constexpr int width = sizeof(T) * 2;
};

// A different dependent noexcept expression on a different parameter shape
// publishes its own environment and stays distinct from the single-parameter
// pattern above.
template <class R, class T>
struct Signal<R (*)(T, T) noexcept(sizeof(T) > 0)> {
	static constexpr int value = 3;
	static constexpr int width = sizeof(T) * 3;
};

// Plain non-noexcept controls: the dependent specification is a non-deduced
// context, so these select the dependent-noexcept partials.
static_assert(Signal<int (*)(int)>::value == 1);
static_assert(Signal<int (*)(int)>::width == static_cast<int>(sizeof(int)));
static_assert(Signal<short (*)(short)>::width == static_cast<int>(sizeof(short)));
static_assert(Signal<int (int)>::value == 2);
static_assert(Signal<int (int)>::width == static_cast<int>(sizeof(int) * 2));
static_assert(Signal<int (*)(int, int)>::value == 3);
static_assert(Signal<int (*)(int, int)>::width == static_cast<int>(sizeof(int) * 3));

// Another parameter shape keeps its own dependent exception specification.
static_assert(Signal<int (*)(int, long)>::value == 0);

// A plain noexcept specification is a different expression and selects the
// primary, as does an explicit noexcept on an otherwise matching signature.
static_assert(Signal<int (*)(int) noexcept>::value == 0);
static_assert(Signal<int (int) noexcept>::value == 0);

// A non-callable control.
static_assert(Signal<int>::value == 0);

int main() {
	const Signal<int (*)(int)> pointer_pattern;
	const Signal<short (*)(short)> narrow_pointer_pattern;
	const Signal<int (int)> function_pattern;
	const Signal<int (*)(int, int)> pair_pattern;
	const Signal<int (*)(int, long)> signature_mismatch;
	const Signal<int (*)(int) noexcept> noexcept_control;
	if (pointer_pattern.value != 1 || pointer_pattern.width != static_cast<int>(sizeof(int))) {
		return 1;
	}
	if (narrow_pointer_pattern.value != 1 || narrow_pointer_pattern.width != static_cast<int>(sizeof(short))) {
		return 2;
	}
	if (function_pattern.value != 2 || function_pattern.width != static_cast<int>(sizeof(int) * 2)) {
		return 3;
	}
	if (pair_pattern.value != 3 || pair_pattern.width != static_cast<int>(sizeof(int) * 3)) {
		return 4;
	}
	if (signature_mismatch.value != 0 || signature_mismatch.width != 0) {
		return 5;
	}
	if (noexcept_control.value != 0 || noexcept_control.width != 0) {
		return 6;
	}
	return 0;
}
