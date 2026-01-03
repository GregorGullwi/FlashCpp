// Test pack expansion in variadic type traits like __is_constructible

template<bool V>
struct bool_constant {
	static constexpr bool value = V;
};

// Variadic type trait pattern with pack expansion - similar to __is_constructible_impl
template<typename T, typename... Args>
struct is_constructible_wrapper {
	// Use __is_constructible with pack expansion
	static constexpr bool value = __is_constructible(T, Args...);
};

// Also test without pack - just single type
template<typename T>
struct is_default_constructible_wrapper {
	static constexpr bool value = __is_constructible(T);
};

struct SimpleStruct {
	int x;
	SimpleStruct() : x(42) {}
	SimpleStruct(int v) : x(v) {}
	SimpleStruct(int a, int b) : x(a + b) {}
};

int main() {
	// Test 1: Default constructible (no args)
	static_assert(is_default_constructible_wrapper<int>::value, "int should be default constructible");
	static_assert(is_default_constructible_wrapper<SimpleStruct>::value, "SimpleStruct should be default constructible");
	
	// Test 2: Single arg constructible (pack with one element)
	static_assert(is_constructible_wrapper<int, int>::value, "int should be constructible from int");
	static_assert(is_constructible_wrapper<SimpleStruct, int>::value, "SimpleStruct should be constructible from int");
	
	// Test 3: Multiple args constructible (pack with multiple elements)
	static_assert(is_constructible_wrapper<SimpleStruct, int, int>::value, "SimpleStruct should be constructible from int, int");
	
	return 42;
}
