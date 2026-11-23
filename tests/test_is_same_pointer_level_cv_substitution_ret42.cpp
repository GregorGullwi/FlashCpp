// Template substitution must keep every pointer level's cv. When `T* const*` is
// substituted with int, the result is `int* const*`, not `int**`, so the
// instantiated `__is_same` answers stay distinct from the plain pointer type.
struct S {
	int member;
};

template <class T>
struct Same {
	static constexpr bool against_plain = __is_same(T* const*, int**);
	static constexpr bool against_const_pointer = __is_same(T* const*, int* const*);
	static_assert(!against_plain, "T* const* is not int**");
	static_assert(against_const_pointer, "T* const* is int* const* when T is int");
};

template <class T>
constexpr bool variable_against_plain = __is_same(T* const*, int**);

template <class T>
constexpr bool function_against_plain() {
	return __is_same(T* const*, int**);
}

template <class T>
struct DeferredAssert {
	static_assert(!__is_same(T* const*, int**), "T* const* is not int**");
};

template struct DeferredAssert<int>;
template struct DeferredAssert<double>;

static_assert(!variable_against_plain<int>, "variable template keeps per-level cv");
static_assert(!variable_against_plain<double>, "variable template keeps outer pointer cv");
static_assert(!function_against_plain<int>(), "function template keeps per-level cv");

int main() {
	if (Same<int>::against_plain) {
		return 1;
	}
	if (!Same<int>::against_const_pointer) {
		return 2;
	}
	if (variable_against_plain<int>) {
		return 3;
	}
	if (function_against_plain<int>()) {
		return 4;
	}
	if (__is_same(int* const*, int**)) {
		return 5;
	}
	if (__is_same(S* const*, S**)) {
		return 6;
	}
	return 42;
}
