// Test: Type alias used as base class
// This pattern is used extensively in <type_traits> and <ratio>

namespace ns {
    struct Base {
        static constexpr int value = 42;
    };
    
    using AliasBase = Base;
}

struct Derived : ns::AliasBase {};

// Also test template type alias as base class
template<typename T, T v>
struct integral_constant {
    static constexpr T val = v;
};

using false_type = integral_constant<bool, false>;
using true_type = integral_constant<bool, true>;

struct MyFalseType : false_type {};
struct MyTrueType : true_type {};

int main() {
    return Derived::value - 42 + (MyFalseType::val ? 1 : 0) + (MyTrueType::val ? 0 : 1);
}
