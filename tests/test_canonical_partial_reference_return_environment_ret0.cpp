// A callable partial whose return type is a reference must publish a canonical
// pattern environment that keeps the reference wrapper, so a value-returning
// callable selects a different partial or the primary.

template <class T>
struct RefSignal {
	static constexpr int value = 0;
	static constexpr int width = 0;
};

template <class T>
struct RefSignal<T& (T)> {
	static constexpr int value = 1;
	static constexpr int width = sizeof(T);
};

template <class T>
struct RefSignal<T& (*)(T)> {
	static constexpr int value = 2;
	static constexpr int width = sizeof(T) * 2;
};

// A value-returning callable is the control for the reference wrapper.
template <class T>
struct RefSignal<T (T)> {
	static constexpr int value = 3;
	static constexpr int width = sizeof(T) * 3;
};

static_assert(RefSignal<int& (int)>::value == 1);
static_assert(RefSignal<int& (int)>::width == static_cast<int>(sizeof(int)));
static_assert(RefSignal<int& (*)(int)>::value == 2);
static_assert(RefSignal<int& (*)(int)>::width == static_cast<int>(sizeof(int) * 2));
static_assert(RefSignal<int (int)>::value == 3);
static_assert(RefSignal<int (int)>::width == static_cast<int>(sizeof(int) * 3));
// A mismatched parameter still selects the primary.
static_assert(RefSignal<int& (long)>::value == 0);

int main() {
	const RefSignal<int& (int)> reference_return;
	const RefSignal<int& (*)(int)> pointer_reference_return;
	const RefSignal<int (int)> value_return;
	const RefSignal<int& (long)> mismatch;
	if (reference_return.value != 1 || reference_return.width != static_cast<int>(sizeof(int))) {
		return 1;
	}
	if (pointer_reference_return.value != 2 || pointer_reference_return.width != static_cast<int>(sizeof(int) * 2)) {
		return 2;
	}
	if (value_return.value != 3 || value_return.width != static_cast<int>(sizeof(int) * 3)) {
		return 3;
	}
	if (mismatch.value != 0 || mismatch.width != 0) {
		return 4;
	}
	return 0;
}
