// Test for dependent alias and defaulted template instantiation handling.
// Exercises three fixes:
// 1. Dependent value recovery in placeholder template args (Trait<T>::value)
// 2. Normalized caching for defaulted class-template instantiations
// 3. Alias raw and normalized instantiation names
//
// Before the fix, enable_if<true>::type could fail to resolve when accessed
// through an alias template that relied on default template arguments.

// --- enable_if with default second parameter ---
template<bool Cond, typename Type = void>
struct enable_if { };

template<typename Type>
struct enable_if<true, Type> {
	using type = Type;
};

// --- enable_if_t alias template: relies on enable_if<true> resolving
//     to enable_if<true, void> via the default argument ---
template<typename T>
using enable_if_t = typename enable_if<true>::type;

// --- is_same trait with dependent ::value ---
template<typename T, typename U>
struct is_same {
	static constexpr bool value = false;
};

template<typename T>
struct is_same<T, T> {
	static constexpr bool value = true;
};

// --- Function using enable_if with a dependent ::value default ---
// This exercises the dependent value recovery path:
// is_same<T, T>::value must be materialized as a concrete true value
// before enable_if can select its partial specialization.
template<typename T>
typename enable_if<is_same<T, T>::value, int>::type
identity(T x) {
	return x;
}

// --- Function using the enable_if_t alias (exercises cache normalization) ---
// enable_if_t<int> expands to enable_if<true>::type.
// The cache key for enable_if<true> (1 arg) must be normalized to
// enable_if<true, void> (2 args) so the partial specialization is found.
template<typename T>
enable_if_t<T> returns_void() { }

// --- Direct test: enable_if<true> and enable_if<true, void> are the same ---
// Both spellings must resolve to the same instantiation.
// This exercises the alias registration between raw and normalized names.
template<typename T>
typename enable_if<true>::type also_returns_void() { }

int main() {
	// Test 1: dependent ::value resolves correctly
	int result = identity(42);
	if (result != 42)
		return 1;

	// Test 2: enable_if_t alias works (cache normalization)
	returns_void<int>();

	// Test 3: enable_if<true>::type works without explicit second arg
	also_returns_void<int>();

	return 0;
}
