// Simpler test: Template parameter used as base class - just instantiation
// This tests that the template can be instantiated without errors

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

// Type aliases
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Test: Simple template with parameter as base class
template<typename T>
struct wrapper : T {
};

// Instantiate to check compilation succeeds
wrapper<true_type> w1;
wrapper<false_type> w2;

// Template specialization with parameter as base
template<typename... Ts>
struct my_or;

// Specialization: no arguments = false
template<>
struct my_or<> : false_type {};

// Specialization: template parameter as base class
template<typename T>
struct my_or<T> : T {};

// Instantiate to check compilation succeeds  
my_or<true_type> m1;
my_or<false_type> m2;

int main() {
    // Just return 42 to indicate successful compilation
    return 42;
}
