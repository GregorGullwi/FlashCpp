// A partial-specialization argument written as a function pointer publishes a
// canonical pattern environment composed from the canonical return type and the
// ordered canonical parameter types. Selection and the pattern's own schema
// follow that environment instead of the synthesized pattern spelling.

template <class T>
struct Probe {
	static constexpr int value = 0;
	static constexpr int width = 0;
};

template <class T>
struct Probe<T (*)(T)> {
	static constexpr int value = 1;
	static constexpr int width = sizeof(T);
};

template <class T, class U>
struct Probe<T (*)(U, T)> {
	static constexpr int value = 2;
	static constexpr int width = sizeof(T) + sizeof(U);
};

template <class T>
struct Probe<T(T)> {
	static constexpr int value = 3;
	static constexpr int width = sizeof(T) * 2;
};

static_assert(Probe<int (*)(int)>::value == 1);
static_assert(Probe<int (*)(int)>::width == static_cast<int>(sizeof(int)));
static_assert(Probe<long (*)(char, long)>::value == 2);
static_assert(Probe<long (*)(char, long)>::width == static_cast<int>(sizeof(long) + sizeof(char)));
// A bare function type stays distinct from a function pointer with the same
// return and parameter types.
static_assert(Probe<int(int)>::value == 3);
static_assert(Probe<int(int)>::width == static_cast<int>(sizeof(int) * 2));
// A different callable signature selects the primary specialization.
static_assert(Probe<int (*)(char)>::value == 0);
static_assert(Probe<int>::value == 0);

int main() {
	const Probe<int (*)(int)> matched;
	const Probe<long (*)(char, long)> pair_matched;
	const Probe<int(int)> function_type;
	const Probe<int (*)(char)> primary;
	if (matched.value != 1 || matched.width != static_cast<int>(sizeof(int))) {
		return 1;
	}
	if (pair_matched.value != 2 || pair_matched.width != static_cast<int>(sizeof(long) + sizeof(char))) {
		return 2;
	}
	if (function_type.value != 3 || function_type.width != static_cast<int>(sizeof(int) * 2)) {
		return 3;
	}
	if (primary.value != 0 || primary.width != 0) {
		return 4;
	}
	return 0;
}
