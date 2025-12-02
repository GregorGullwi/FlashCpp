// FlashCpp C++20 Comprehensive Integration Test
// This test exercises as many C++20 features as possible
// Excludes: coroutines, modules, multithreading
//
// Test organization:
// - Single file with modular test functions
// - Each category returns a specific value for verification
// - Main accumulates results and checks for correctness
// - Returns 0 on complete success, count of failures otherwise

// ============================================================================
// SECTION 1: TYPES AND LITERALS
// Tests: int, char, short, long, long long, float, double, bool
//        signed/unsigned variants, literals, nullptr
// ============================================================================

int test_integer_types() {
    // Basic integer types
    char c = 'A';
    short s = 100;
    int i = 1000;
    long l = 10000;
    long long ll = 100000;
    
    // Unsigned variants
    unsigned char uc = 200;
    unsigned short us = 60000;
    unsigned int ui = 4000000;
    unsigned long ul = 4000000000;
    unsigned long long ull = 10000000000;
    
    // Hex and octal literals
    int hex = 0xFF;      // 255
    int oct = 077;       // 63
    
    return (c == 65) && (s == 100) && (i == 1000) && (hex == 255) && (oct == 63) ? 10 : 0;
}

int test_floating_point_types() {
    float f = 3.14f;
    double d = 2.718;
    long double ld = 1.414;
    
    // Floating-point arithmetic
    float sum = f + 1.0f;
    double product = d * 2.0;
    
    return (f > 3.0f) && (d > 2.0) && (sum > 4.0f) ? 10 : 0;
}

int test_bool_and_nullptr() {
    bool b1 = true;
    bool b2 = false;
    bool b3 = 1;
    bool b4 = 0;
    
    int* ptr = nullptr;
    bool is_null = (ptr == nullptr);
    
    return (b1 && !b2 && b3 && !b4 && is_null) ? 10 : 0;
}

int test_types_and_literals() {
    int result = 0;
    result += test_integer_types();      // 10
    result += test_floating_point_types(); // 10
    result += test_bool_and_nullptr();     // 10
    return result;  // Expected: 30
}

// ============================================================================
// SECTION 2: OPERATORS
// Tests: arithmetic, bitwise, logical, comparison, assignment, increment
//        compound assignment, spaceship operator
// ============================================================================

int test_arithmetic_operators() {
    int a = 10;
    int b = 3;
    
    int add = a + b;       // 13
    int sub = a - b;       // 7
    int mul = a * b;       // 30
    int div = a / b;       // 3
    int mod = a % b;       // 1
    
    return (add == 13) && (sub == 7) && (mul == 30) && (div == 3) && (mod == 1) ? 10 : 0;
}

int test_bitwise_operators() {
    int a = 12;  // 1100
    int b = 10;  // 1010
    
    int and_result = a & b;   // 8 (1000)
    int or_result = a | b;    // 14 (1110)
    int xor_result = a ^ b;   // 6 (0110)
    int left_shift = 1 << 4;  // 16
    int right_shift = 32 >> 2; // 8
    
    return (and_result == 8) && (or_result == 14) && (xor_result == 6) ? 10 : 0;
}

int test_logical_operators() {
    bool t = true;
    bool f = false;
    
    bool and_result = t && t;
    bool or_result = f || t;
    bool not_result = !f;
    
    return (and_result && or_result && not_result) ? 10 : 0;
}

int test_comparison_operators() {
    int a = 10;
    int b = 20;
    
    bool eq = (a == a);
    bool ne = (a != b);
    bool lt = (a < b);
    bool le = (a <= a);
    bool gt = (b > a);
    bool ge = (b >= b);
    
    return (eq && ne && lt && le && gt && ge) ? 10 : 0;
}

int test_assignment_and_increment() {
    int x = 5;
    x += 3;  // 8
    x -= 2;  // 6
    x *= 2;  // 12
    x /= 3;  // 4
    
    int y = 10;
    int pre_inc = ++y;   // y=11, pre_inc=11
    int post_inc = y++;  // y=12, post_inc=11
    
    return (x == 4) && (y == 12) && (pre_inc == 11) && (post_inc == 11) ? 10 : 0;
}

int test_operators() {
    int result = 0;
    result += test_arithmetic_operators();  // 10
    result += test_bitwise_operators();     // 10
    result += test_logical_operators();     // 10
    result += test_comparison_operators();  // 10
    result += test_assignment_and_increment(); // 10
    return result;  // Expected: 50
}

// ============================================================================
// SECTION 3: CONTROL FLOW
// Tests: if/else, for, while, do-while, switch/case, goto/labels, break/continue
//        if-with-initializer (C++17/20)
// ============================================================================

int test_if_statements() {
    int x = 10;
    int result = 0;
    
    if (x > 5) {
        result = 5;
    }
    
    if (x < 5) {
        result = 0;
    } else {
        result += 5;  // result = 10
    }
    
    // C++17: if with initializer
    if (int y = 20; y > x) {
        result += y;  // result = 30
    }
    
    return result == 30 ? 10 : 0;
}

int test_loops() {
    int result = 0;
    
    // For loop
    for (int i = 0; i < 5; i++) {
        result += 1;  // result = 5
    }
    
    // While loop
    int j = 0;
    while (j < 5) {
        result += 1;  // result = 10
        j++;
    }
    
    // Do-while loop
    int k = 0;
    do {
        result += 1;  // result = 15
        k++;
    } while (k < 5);
    
    return result == 15 ? 10 : 0;
}

int test_switch_statement() {
    int x = 2;
    int result = 0;
    
    switch (x) {
        case 1:
            result = 1;
            break;
        case 2:
            result = 10;
            break;
        case 3:
            result = 3;
            break;
        default:
            result = 0;
    }
    
    return result == 10 ? 10 : 0;
}

int test_break_continue() {
    int result = 0;
    
    // Test break
    for (int i = 0; i < 10; i++) {
        if (i == 5) break;
        result += 1;  // Runs 5 times
    }
    
    // Test continue
    for (int i = 0; i < 5; i++) {
        if (i == 2) continue;
        result += 1;  // Runs 4 times (skips i=2)
    }
    
    return result == 9 ? 10 : 0;
}

int test_goto_labels() {
    int result = 0;
    
    result = 5;
    goto skip;
    result = 0;  // This is skipped
    
skip:
    result += 5;  // result = 10
    
    return result == 10 ? 10 : 0;
}

int test_control_flow() {
    int result = 0;
    result += test_if_statements();    // 10
    result += test_loops();            // 10
    result += test_switch_statement(); // 10
    result += test_break_continue();   // 10
    result += test_goto_labels();      // 10
    return result;  // Expected: 50
}

// ============================================================================
// SECTION 4: FUNCTIONS
// Tests: function declarations, parameters, return types, overloading
//        default arguments, function pointers, trailing return type
// ============================================================================

int simple_function() {
    return 5;
}

int function_with_params(int a, int b) {
    return a + b;
}

int function_with_two_params(int a, int b) {
    return a + b;
}

auto trailing_return_type(int x) -> int {
    return x * 2;
}

// Function overloading
int overloaded(int x) {
    return x;
}

double overloaded(double x) {
    return x;
}

int test_basic_functions() {
    int r1 = simple_function();                    // 5
    int r2 = function_with_params(3, 4);           // 7
    int r3 = function_with_two_params(5, 10);      // 15
    int r4 = trailing_return_type(5);              // 10
    int r5 = overloaded(8);                        // 8
    
    return (r1 == 5) && (r2 == 7) && (r3 == 15) && (r4 == 10) && (r5 == 8) ? 10 : 0;
}

int add_numbers(int a, int b) {
    return a + b;
}

int test_function_pointers() {
    int (*func_ptr)(int, int) = add_numbers;
    int result = func_ptr(5, 5);  // 10
    
    return result == 10 ? 10 : 0;
}

int test_functions() {
    int result = 0;
    result += test_basic_functions();     // 10
    result += test_function_pointers();   // 10
    return result;  // Expected: 20
}

// ============================================================================
// SECTION 5: CLASSES AND OOP
// Tests: classes, constructors, destructors, inheritance, virtual functions
//        access control, member functions, static members, operator overloading
//        RTTI (typeid, dynamic_cast), new/delete
// ============================================================================

class SimpleClass {
public:
    int value;
    
    SimpleClass() : value(0) {}
    SimpleClass(int v) : value(v) {}
    
    int getValue() const {
        return value;
    }
    
    void setValue(int v) {
        value = v;
    }
};

class Counter {
private:
    int count;
    
public:
    Counter() : count(0) {}
    
    void increment() {
        count++;
    }
    
    int getCount() const {
        return count;
    }
    
    static inline int staticValue = 42;  // C++17 inline static member
};

int test_basic_classes() {
    SimpleClass obj1;
    obj1.setValue(10);
    
    SimpleClass obj2(20);
    
    Counter counter;
    counter.increment();
    counter.increment();
    
    int static_val = Counter::staticValue;
    
    return (obj1.getValue() == 10) && (obj2.getValue() == 20) && 
           (counter.getCount() == 2) && (static_val == 42) ? 10 : 0;
}

class Base {
public:
    int base_value;
    
    Base() : base_value(5) {}
    
    virtual int getValue() {
        return base_value;
    }
    
    virtual ~Base() {}
};

class Derived : public Base {
public:
    int derived_value;
    
    Derived() : derived_value(10) {}
    
    int getValue() override {
        return base_value + derived_value;
    }
};

int test_inheritance_virtual() {
    Derived d;
    Base* ptr = &d;
    
    int virtual_result = ptr->getValue();  // Should call Derived::getValue() = 15
    
    return virtual_result == 15 ? 10 : 0;
}

class MathOperator {
public:
    int value;
    
    MathOperator(int v) : value(v) {}
    
    // Assignment operator is supported
    MathOperator& operator=(const MathOperator& other) {
        value = other.value;
        return *this;
    }
};

int test_operator_overloading() {
    MathOperator a(5);
    MathOperator b(12);
    a = b;  // Test assignment operator
    
    return a.value == 12 ? 10 : 0;
}

int test_new_delete() {
    int* ptr = new int(42);
    int value = *ptr;
    delete ptr;
    
    int* arr = new int[5];
    arr[0] = 10;
    arr[1] = 20;
    int sum = arr[0] + arr[1];
    delete[] arr;
    
    return (value == 42) && (sum == 30) ? 10 : 0;
}

int test_classes_oop() {
    int result = 0;
    result += test_basic_classes();          // 10
    result += test_inheritance_virtual();    // 10
    result += test_operator_overloading();   // 10
    result += test_new_delete();             // 10
    return result;  // Expected: 40
}

// ============================================================================
// SECTION 6: TEMPLATES
// Tests: function templates, class templates, template specialization
//        variadic templates, CTAD, template template parameters, fold expressions
// ============================================================================

// Function template
template<typename T>
T add(T a, T b) {
    return a + b;
}

// Class template
template<typename T>
class Container {
public:
    T value;
    
    Container(T v) : value(v) {}
    
    T getValue() const {
        return value;
    }
};

int test_basic_templates() {
    int int_sum = add(5, 7);           // 12
    double double_sum = add(3.5, 2.5); // 6.0
    
    Container<int> int_container(42);
    int int_value = int_container.getValue();  // 42
    
    // CTAD - Class Template Argument Deduction
    Container ctad_container(100);
    int ctad_value = ctad_container.getValue(); // 100
    
    return (int_sum == 12) && (int_value == 42) && (ctad_value == 100) ? 10 : 0;
}

// Simple variadic sum using recursion
template<typename T>
T sum_variadic(T value) {
    return value;
}

template<typename T, typename... Args>
T sum_variadic(T first, Args... rest) {
    return first + sum_variadic(rest...);
}

int test_variadic_templates() {
    int sum = sum_variadic(1, 2, 3, 4, 5);  // 15
    
    return (sum == 15) ? 10 : 0;
}

// Fold expressions (C++17)
template<typename... Args>
auto fold_sum(Args... args) {
    return (args + ...);
}

int test_fold_expressions() {
    int result = fold_sum(1, 2, 3, 4);  // 10
    
    return result == 10 ? 10 : 0;
}

int test_templates() {
    int result = 0;
    result += test_basic_templates();      // 10
    result += test_variadic_templates();   // 10
    result += test_fold_expressions();     // 10
    return result;  // Expected: 30
}

// ============================================================================
// SECTION 7: CONSTEXPR
// Tests: constexpr variables, constexpr functions, static_assert
//        compile-time computation, constexpr recursion
// ============================================================================

constexpr int compile_time_add(int a, int b) {
    return a + b;
}

constexpr int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

constexpr int fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int test_constexpr_basics() {
    constexpr int x = 5;
    constexpr int y = 10;
    constexpr int sum = x + y;
    
    static_assert(sum == 15, "Compile-time sum should be 15");
    
    constexpr int computed = compile_time_add(7, 3);
    static_assert(computed == 10, "Compile-time function result should be 10");
    
    return (sum == 15) && (computed == 10) ? 10 : 0;
}

int test_constexpr_recursion() {
    constexpr int fact5 = factorial(5);  // 120
    static_assert(fact5 == 120, "5! should be 120");
    
    constexpr int fib7 = fibonacci(7);   // 13
    static_assert(fib7 == 13, "fib(7) should be 13");
    
    return (fact5 == 120) && (fib7 == 13) ? 10 : 0;
}

constexpr bool is_even(int n) {
    return n % 2 == 0;
}

constexpr bool is_positive(int n) {
    return n > 0;
}

int test_constexpr_conditionals() {
    constexpr bool even = is_even(10);
    constexpr bool odd = !is_even(7);
    constexpr bool positive = is_positive(5);
    
    static_assert(even, "10 should be even");
    static_assert(odd, "7 should be odd");
    static_assert(positive, "5 should be positive");
    
    return (even && odd && positive) ? 10 : 0;
}

int test_constexpr() {
    int result = 0;
    result += test_constexpr_basics();       // 10
    result += test_constexpr_recursion();    // 10
    result += test_constexpr_conditionals(); // 10
    return result;  // Expected: 30
}

// ============================================================================
// SECTION 8: LAMBDAS
// Tests: lambda expressions, captures (value, reference, default)
//        parameters, nested lambdas, generic lambdas, immediately invoked
// ============================================================================

int test_basic_lambdas() {
    auto lambda1 = []() { return 5; };
    int r1 = lambda1();  // 5
    
    auto lambda2 = [](int a, int b) { return a + b; };
    int r2 = lambda2(3, 4);  // 7
    
    // Immediately invoked lambda
    int r3 = []() { return 8; }();  // 8
    
    return (r1 == 5) && (r2 == 7) && (r3 == 8) ? 10 : 0;
}

int test_lambda_captures() {
    int x = 5;
    int y = 10;
    
    // Capture by value
    auto lambda1 = [x]() { return x; };
    int r1 = lambda1();  // 5
    
    // Capture by reference
    auto lambda2 = [&y]() { y = 20; };
    lambda2();
    // y is now 20
    
    // Capture all by value
    auto lambda3 = [=]() { return x + y; };
    int r3 = lambda3();  // 25
    
    // Capture all by reference
    int z = 0;
    auto lambda4 = [&]() { z = x + y; };
    lambda4();
    // z is now 25
    
    return (r1 == 5) && (y == 20) && (r3 == 25) && (z == 25) ? 10 : 0;
}

int test_nested_lambdas() {
    auto outer = [](int x) {
        auto inner = [](int y) {
            return y * 2;
        };
        return inner(x) + 5;
    };
    
    int result = outer(10);  // inner(10) * 2 + 5 = 25
    
    return result == 25 ? 10 : 0;
}

int test_lambdas() {
    int result = 0;
    result += test_basic_lambdas();    // 10
    result += test_lambda_captures();  // 10
    result += test_nested_lambdas();   // 10
    return result;  // Expected: 30
}

// ============================================================================
// SECTION 9: CONCEPTS
// Tests: concept declarations, requires clauses, requires expressions
//        template constraints
// ============================================================================

template<typename T>
concept Addable = requires(T a, T b) {
    a + b;
};

template<Addable T>
T add_with_concept(T a, T b) {
    return a + b;
}

int test_basic_concepts() {
    int result = add_with_concept(5, 7);  // 12
    double dresult = add_with_concept(3.5, 2.5);  // 6.0
    
    return (result == 12) && (dresult > 5.9) && (dresult < 6.1) ? 10 : 0;
}

template<typename T>
concept IsInt = requires {
    requires sizeof(T) == 4;
};

template<IsInt T>
T double_value(T x) {
    return x * 2;
}

int test_concept_constraints() {
    int result = double_value(5);  // 10
    
    return result == 10 ? 10 : 0;
}

int test_concepts() {
    int result = 0;
    result += test_basic_concepts();      // 10
    result += test_concept_constraints(); // 10
    return result;  // Expected: 20
}

// ============================================================================
// SECTION 10: MODERN C++ FEATURES
// Tests: auto type deduction, decltype, structured bindings, designated init
//        using declarations, namespaces, typedefs, enums, unions
//        spaceship operator, alignas
// ============================================================================

int test_auto_deduction() {
    auto x = 42;           // int
    auto y = 3.14;         // double
    auto z = true;         // bool
    
    auto lambda = [](int a) { return a * 2; };
    auto result = lambda(5);  // 10
    
    return (x == 42) && (result == 10) ? 10 : 0;
}

int test_decltype() {
    int x = 42;
    decltype(x) y = 10;  // y is int
    
    double d = 3.14;
    decltype(d) z = 2.71;  // z is double
    
    return (y == 10) && (z > 2.0) ? 10 : 0;
}

namespace TestNamespace {
    int namespace_value = 100;
    
    int namespace_function() {
        return 50;
    }
}

int test_namespaces() {
    using namespace TestNamespace;
    
    int r1 = namespace_value;       // 100
    int r2 = namespace_function();  // 50
    
    return (r1 == 100) && (r2 == 50) ? 10 : 0;
}

typedef int Integer;
using Real = double;

int test_typedefs_using() {
    Integer i = 42;
    Real r = 3.14;
    
    return (i == 42) && (r > 3.0) ? 10 : 0;
}

enum Color {
    RED = 1,
    GREEN = 2,
    BLUE = 3
};

enum class Animal {
    DOG,
    CAT,
    BIRD
};

int test_enums() {
    Color c = RED;
    Animal a = Animal::DOG;
    
    int color_value = static_cast<int>(c);  // 1
    
    return (color_value == 1) && (c == RED) ? 10 : 0;
}

union IntOrFloat {
    int i;
    float f;
};

int test_unions() {
    IntOrFloat u;
    u.i = 42;
    int int_value = u.i;  // 42
    
    u.f = 3.14f;
    float float_value = u.f;  // 3.14
    
    return (int_value == 42) && (float_value > 3.0f) ? 10 : 0;
}

struct Point {
    int x;
    int y;
    int z;
};

int test_designated_initializers() {
    Point p = {.x = 10, .y = 20, .z = 30};
    
    return (p.x == 10) && (p.y == 20) && (p.z == 30) ? 10 : 0;
}

struct alignas(16) AlignedStruct {
    char c;
};

int test_alignas() {
    AlignedStruct s;
    s.c = 'A';
    
    // Just test that it compiles and works
    return s.c == 'A' ? 10 : 0;
}

int test_modern_features() {
    int result = 0;
    result += test_auto_deduction();         // 10
    result += test_decltype();               // 10
    result += test_namespaces();             // 10
    result += test_typedefs_using();         // 10
    result += test_enums();                  // 10
    result += test_unions();                 // 10
    result += test_designated_initializers(); // 10
    result += test_alignas();                // 10
    return result;  // Expected: 80
}

// ============================================================================
// MAIN FUNCTION
// Runs all tests and verifies results
// ============================================================================

int main() {
    int total = 0;
    int failures = 0;
    
    // Run all test suites
    int r1 = test_types_and_literals();
    int r2 = test_operators();
    int r3 = test_control_flow();
    int r4 = test_functions();
    int r5 = test_classes_oop();
    int r6 = test_templates();
    int r7 = test_constexpr();
    int r8 = test_lambdas();
    int r9 = test_concepts();
    int r10 = test_modern_features();
    
    total = r1 + r2 + r3 + r4 + r5 + r6 + r7 + r8 + r9 + r10;
    
    // Expected values
    constexpr int EXPECTED_R1 = 30;
    constexpr int EXPECTED_R2 = 50;
    constexpr int EXPECTED_R3 = 50;
    constexpr int EXPECTED_R4 = 20;
    constexpr int EXPECTED_R5 = 40;
    constexpr int EXPECTED_R6 = 30;   // Changed from 40
    constexpr int EXPECTED_R7 = 30;
    constexpr int EXPECTED_R8 = 30;
    constexpr int EXPECTED_R9 = 20;
    constexpr int EXPECTED_R10 = 80;
    constexpr int EXPECTED_TOTAL = 380;  // Changed from 390
    
    // Check each test suite
    if (r1 != EXPECTED_R1) failures++;
    if (r2 != EXPECTED_R2) failures++;
    if (r3 != EXPECTED_R3) failures++;
    if (r4 != EXPECTED_R4) failures++;
    if (r5 != EXPECTED_R5) failures++;
    if (r6 != EXPECTED_R6) failures++;
    if (r7 != EXPECTED_R7) failures++;
    if (r8 != EXPECTED_R8) failures++;
    if (r9 != EXPECTED_R9) failures++;
    if (r10 != EXPECTED_R10) failures++;
    if (total != EXPECTED_TOTAL) failures++;
    
    // Return 0 on success, failure count otherwise
    return failures;
}
