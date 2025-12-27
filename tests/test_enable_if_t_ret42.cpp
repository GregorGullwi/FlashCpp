// Test type alias usage in template metaprogramming contexts
// Simulates how <type_traits> uses type aliases

template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
    using type = T;
};

// Type alias template
template<bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Function that uses enable_if_t in return type
template<typename T>
enable_if_t<true, int> test_function() {
    return 42;
}

int main() {
    return test_function<int>();
}
