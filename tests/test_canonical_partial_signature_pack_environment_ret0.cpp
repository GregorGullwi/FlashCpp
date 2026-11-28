// A callable partial whose parameter list is a template parameter pack must
// publish a canonical pattern environment and bind the pack from the concrete
// signature. Bare function and function-pointer pack patterns stay distinct.

template <class T>
struct PackSignal {
	static constexpr int value = 0;
	static constexpr int width = 0;
};

template <class R, class... Args>
struct PackSignal<R(Args...)> {
	static constexpr int value = 1;
	static constexpr int width = sizeof(R);
};

template <class R, class... Args>
struct PackSignal<R (*)(Args...)> {
	static constexpr int value = 2;
	static constexpr int width = sizeof(R) + static_cast<int>(sizeof...(Args));
};

// A fixed-signature partial remains a control for the same callable shapes.
template <class T>
struct PackSignal<T (*)(T, T)> {
	static constexpr int value = 3;
	static constexpr int width = sizeof(T) * 2;
};

static_assert(PackSignal<int(char, long)>::value == 1);
static_assert(PackSignal<int(char, long)>::width == static_cast<int>(sizeof(int)));
static_assert(PackSignal<int (*)(char, long)>::value == 2);
static_assert(PackSignal<int (*)(char, long)>::width == static_cast<int>(sizeof(int) + 2));
// An empty parameter list still matches the pack pattern.
static_assert(PackSignal<int (*)()>::value == 2);
static_assert(PackSignal<int (*)()>::width == static_cast<int>(sizeof(int)));
// A matching fixed signature keeps its own more specialized pattern.
static_assert(PackSignal<short (*)(short, short)>::value == 3);
static_assert(PackSignal<short (*)(short, short)>::width == static_cast<int>(sizeof(short) * 2));
// A non-callable argument selects the primary.
static_assert(PackSignal<int>::value == 0);

int main() {
	const PackSignal<int(char, long)> function_pattern;
	const PackSignal<int (*)(char, long)> pointer_pattern;
	const PackSignal<int (*)()> empty_pattern;
	const PackSignal<short (*)(short, short)> fixed_pattern;
	if (function_pattern.value != 1 || function_pattern.width != static_cast<int>(sizeof(int))) {
		return 1;
	}
	if (pointer_pattern.value != 2 || pointer_pattern.width != static_cast<int>(sizeof(int) + 2)) {
		return 2;
	}
	if (empty_pattern.value != 2 || empty_pattern.width != static_cast<int>(sizeof(int))) {
		return 3;
	}
	if (fixed_pattern.value != 3 || fixed_pattern.width != static_cast<int>(sizeof(short) * 2)) {
		return 4;
	}
	return 0;
}
