// A partial-specialization argument written as a pointer-to-pointer callable
// keeps both pointer levels observable in its canonical pattern environment.
// Selection and the pattern's own schema follow that environment instead of the
// synthesized pattern spelling, and a different signature selects the primary.

template <class T>
struct Signal {
	static constexpr int value = 0;
	static constexpr int width = 0;
};

template <class T>
struct Signal<T (**)(T)> {
	static constexpr int value = 1;
	static constexpr int width = sizeof(T);
};

// Existing single-pointer and function-reference shapes stay distinct controls
// for the same callable signature.
template <class T>
struct Signal<T (*)(T)> {
	static constexpr int value = 2;
	static constexpr int width = sizeof(T) * 2;
};

template <class T>
struct Signal<T (&)(T)> {
	static constexpr int value = 3;
	static constexpr int width = sizeof(T) * 3;
};

// Exact match: the two-pointer callable binds T from both return and parameter.
static_assert(Signal<int (**)(int)>::value == 1);
static_assert(Signal<int (**)(int)>::width == static_cast<int>(sizeof(int)));
static_assert(Signal<long (**)(long)>::value == 1);
static_assert(Signal<long (**)(long)>::width == static_cast<int>(sizeof(long)));
// A signature mismatch cannot bind one T to both int and long.
static_assert(Signal<int (**)(long)>::value == 0);
static_assert(Signal<int (**)(long)>::width == 0);
// Controls: one pointer level and a function reference keep their own patterns.
static_assert(Signal<int (*)(int)>::value == 2);
static_assert(Signal<int (&)(int)>::value == 3);

int main() {
	const Signal<int (**)(int)> double_pointer;
	const Signal<int (**)(long)> mismatch;
	const Signal<int (*)(int)> single_pointer;
	const Signal<int (&)(int)> reference;
	if (double_pointer.value != 1 || double_pointer.width != static_cast<int>(sizeof(int))) {
		return 1;
	}
	if (mismatch.value != 0 || mismatch.width != 0) {
		return 2;
	}
	if (single_pointer.value != 2 || single_pointer.width != static_cast<int>(sizeof(int) * 2)) {
		return 3;
	}
	if (reference.value != 3 || reference.width != static_cast<int>(sizeof(int) * 3)) {
		return 4;
	}
	return 0;
}
