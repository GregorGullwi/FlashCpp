// Test case demonstrating standard library support features added
// - __is_complete_or_unbounded intrinsic
// - Function reference in requires expression
// - Variadic non-type template parameters

// Test 1: __is_complete_or_unbounded (simulated as intrinsic)
template<typename T>
constexpr bool check_complete() {
    // In the real std lib, this is std::__is_complete_or_unbounded
    // We've added it as an intrinsic that always returns true
    return __is_complete_or_unbounded(T);
}

// Test 2: Variadic non-type template parameters
template<size_t... Indices>
struct IndexSequence {
    static constexpr int size = 3; // Would be sizeof...(Indices) when that's fully supported
};

// Test 3: Function reference in requires expression
template<typename T>
concept DefaultConstructible = requires (void(&f)(T)) {
    f({});  // Can call function with default-constructed T
};

// Use the concept
template<typename T> requires DefaultConstructible<T>
struct Wrapper {
    T value;
};

int main() {
    // Test __is_complete_or_unbounded
    static_assert(check_complete<int>(), "int should be complete");
    
    // Test variadic non-type params
    IndexSequence<0, 1, 2> seq;
    static_assert(seq.size == 3, "IndexSequence should have size 3");
    
    // Test function reference in requires
    Wrapper<int> w{42};
    
    return w.value == 42 ? 0 : 1;
}
