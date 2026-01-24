// Test: Pack expansion in functional cast expressions  
// This tests typename functional cast with pack expansion
// Pattern: typename T::type(value) in template context

template<typename T>
struct wrap {
    using type = T;
};

// Test 1: Typename functional cast in decltype (pattern from <type_traits>)
template<typename T1, typename T2>
using common_type = decltype(typename wrap<T1>::type(0) + typename wrap<T2>::type(0));

// Verify it compiles
using test1 = common_type<int, int>;

// Test 2: Multiple typename casts
template<typename T>
constexpr int get_size() {
    return sizeof(typename wrap<T>::type);
}

int main() {
    // Verify the pattern works
    constexpr int s1 = get_size<int>();     // 4
    constexpr int s2 = get_size<char>();    // 1
    constexpr int s3 = get_size<short>();   // 2
    
    // Return predictable value
    return 42;
}


