// Test explicit(condition) with compile-time constant expressions
// This tests the constexpr evaluator integration for explicit conditions

template<typename T>
struct is_integral {
    static constexpr bool value = false;
};

template<>
struct is_integral<int> {
    static constexpr bool value = true;
};

template<>
struct is_integral<long> {
    static constexpr bool value = true;
};

// Test with constexpr constant
constexpr bool always_explicit = true;
constexpr bool never_explicit = false;

class ConstexprCondition {
public:
    explicit(always_explicit) ConstexprCondition(int x) : val(x) {}
    int val;
};

class ConstexprConditionFalse {
public:
    explicit(never_explicit) ConstexprConditionFalse(int x) : val(x) {}
    int val;
};

// Test with arithmetic expression
class ArithmeticCondition {
public:
    explicit(1 + 1 == 2) ArithmeticCondition(int x) : val(x) {}
    int val;
};

class ArithmeticConditionFalse {
public:
    explicit(1 + 1 == 3) ArithmeticConditionFalse(int x) : val(x) {}
    int val;
};

int main() {
    // ConstexprCondition is explicit (always_explicit = true)
    ConstexprCondition c1(10);  // OK: direct init
    // ConstexprCondition c2 = 10;  // ERROR: copy init with explicit
    
    // ConstexprConditionFalse is not explicit (never_explicit = false)
    ConstexprConditionFalse c3(20);  // OK: direct init
    ConstexprConditionFalse c4 = 30;  // OK: copy init allowed
    
    // ArithmeticCondition is explicit (1+1==2 is true)
    ArithmeticCondition a1(40);  // OK: direct init
    // ArithmeticCondition a2 = 40;  // ERROR: copy init with explicit
    
    // ArithmeticConditionFalse is not explicit (1+1==3 is false)
    ArithmeticConditionFalse a3(50);  // OK: direct init
    ArithmeticConditionFalse a4 = 60;  // OK: copy init allowed
    
    return c1.val + c3.val + c4.val + a1.val + a3.val + a4.val - 210;  // 10+20+30+40+50+60 = 210
}
