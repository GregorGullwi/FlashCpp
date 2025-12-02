// FlashCpp C++20 Integration Test
// Comprehensive test of supported C++20 features
// Excludes: coroutines, modules, multithreading
//
// This test is designed to work with FlashCpp's current feature set
// and focuses on features explicitly listed as working in the README

// ============================================================================
// SECTION 1: BASIC TYPES AND LITERALS
// ============================================================================

int test_integer_types() {
    char c = 65;  // 'A'
    short s = 100;
    int i = 1000;
    long l = 10000;
    
    unsigned char uc = 200;
    unsigned int ui = 4000;
    
    int hex = 0xFF;  // 255
    
    return (c == 65) && (s == 100) && (hex == 255) ? 10 : 0;
}

int test_floating_point() {
    float f = 3.14f;
    double d = 2.718;
    
    float sum = f + 1.0f;
    double product = d * 2.0;
    
    return (sum > 4.0f) && (product > 5.0) ? 10 : 0;
}

int test_bool_nullptr() {
    bool b1 = true;
    bool b2 = false;
    
    int* ptr = nullptr;
    bool is_null = (ptr == nullptr);
    
    return (b1 && !b2 && is_null) ? 10 : 0;
}

// ============================================================================
// SECTION 2: OPERATORS
// ============================================================================

int test_arithmetic() {
    int a = 10;
    int b = 3;
    
    int add = a + b;
    int sub = a - b;
    int mul = a * b;
    int div = a / b;
    int mod = a % b;
    
    return (add == 13) && (sub == 7) && (mul == 30) && (div == 3) && (mod == 1) ? 10 : 0;
}

int test_bitwise() {
    int a = 12;
    int b = 10;
    
    int and_op = a & b;
    int or_op = a | b;
    int xor_op = a ^ b;
    int shl = 1 << 4;
    int shr = 32 >> 2;
    
    return (and_op == 8) && (or_op == 14) && (xor_op == 6) && (shl == 16) && (shr == 8) ? 10 : 0;
}

int test_logical() {
    bool t = true;
    bool f = false;
    
    bool and_op = t && t;
    bool or_op = f || t;
    bool not_op = !f;
    
    return (and_op && or_op && not_op) ? 10 : 0;
}

int test_comparison() {
    int a = 10;
    int b = 20;
    
    bool eq = (a == 10);
    bool ne = (a != b);
    bool lt = (a < b);
    bool gt = (b > a);
    
    return (eq && ne && lt && gt) ? 10 : 0;
}

int test_compound_assign() {
    int x = 5;
    x += 3;
    x -= 2;
    x *= 2;
    x /= 3;
    
    int y = 10;
    y++;
    int z = ++y;
    
    return (x == 4) && (y == 12) && (z == 12) ? 10 : 0;
}

// ============================================================================
// SECTION 3: CONTROL FLOW
// ============================================================================

int test_if_else() {
    int x = 10;
    int result = 0;
    
    if (x > 5) {
        result = 5;
    }
    
    if (x < 5) {
        result = 0;
    } else {
        result += 5;
    }
    
    return result == 10 ? 10 : 0;
}

int test_for_while() {
    int sum = 0;
    
    for (int i = 0; i < 5; i++) {
        sum += i;
    }
    
    int j = 0;
    while (j < 5) {
        sum += 1;
        j++;
    }
    
    return (sum == 15) ? 10 : 0;
}

int test_do_while() {
    int count = 0;
    int i = 0;
    
    do {
        count++;
        i++;
    } while (i < 5);
    
    return count == 5 ? 10 : 0;
}

int test_switch() {
    int x = 2;
    int result = 0;
    
    switch (x) {
        case 1:
            result = 1;
            break;
        case 2:
            result = 10;
            break;
        default:
            result = 0;
    }
    
    return result == 10 ? 10 : 0;
}

int test_break_continue() {
    int sum = 0;
    
    for (int i = 0; i < 10; i++) {
        if (i == 5) break;
        sum++;
    }
    
    for (int i = 0; i < 5; i++) {
        if (i == 2) continue;
        sum++;
    }
    
    return sum == 9 ? 10 : 0;
}

// ============================================================================
// SECTION 4: FUNCTIONS
// ============================================================================

int simple_func() {
    return 42;
}

int add_func(int a, int b) {
    return a + b;
}

auto trailing_ret(int x) -> int {
    return x * 2;
}

int overload_int(int x) {
    return x;
}

double overload_double(double x) {
    return x;
}

int test_functions() {
    int r1 = simple_func();
    int r2 = add_func(3, 7);
    int r3 = trailing_ret(5);
    int r4 = overload_int(20);
    
    return (r1 == 42) && (r2 == 10) && (r3 == 10) && (r4 == 20) ? 10 : 0;
}

int add_two(int a, int b) {
    return a + b;
}

int test_func_pointers() {
    int (*fptr)(int, int) = add_two;
    int result = fptr(5, 5);
    
    return result == 10 ? 10 : 0;
}

// ============================================================================
// SECTION 5: CLASSES AND OOP
// ============================================================================

class SimpleClass {
public:
    int value;
    
    SimpleClass() : value(0) {}
    SimpleClass(int v) : value(v) {}
    
    int getValue() const {
        return value;
    }
};

class Counter {
public:
    int count;
    
    Counter() : count(0) {}
    
    void increment() {
        count++;
    }
};

int test_basic_classes() {
    SimpleClass obj1;
    obj1.value = 10;
    
    SimpleClass obj2(20);
    
    Counter c;
    c.increment();
    c.increment();
    
    return (obj1.getValue() == 10) && (obj2.getValue() == 20) && (c.count == 2) ? 10 : 0;
}

class Base {
public:
    int base_val;
    
    Base() : base_val(5) {}
    
    virtual int get() {
        return base_val;
    }
    
    virtual ~Base() {}
};

class Derived : public Base {
public:
    int derived_val;
    
    Derived() : derived_val(10) {}
    
    int get() override {
        return base_val + derived_val;
    }
};

int test_inheritance() {
    Derived d;
    Base* ptr = &d;
    int result = ptr->get();
    
    return result == 15 ? 10 : 0;
}

int test_new_delete() {
    int* p = new int(42);
    int val = *p;
    delete p;
    
    int* arr = new int[5];
    arr[0] = 10;
    arr[1] = 20;
    int sum = arr[0] + arr[1];
    delete[] arr;
    
    return (val == 42) && (sum == 30) ? 10 : 0;
}

// ============================================================================
// SECTION 6: TEMPLATES
// ============================================================================

template<typename T>
T template_add(T a, T b) {
    return a + b;
}

template<typename T>
class Box {
public:
    T value;
    
    Box(T v) : value(v) {}
    
    T get() const {
        return value;
    }
};

int test_templates() {
    int i_sum = template_add(5, 7);
    double d_sum = template_add(3.5, 2.5);
    
    Box<int> ibox(42);
    Box<double> dbox(3.14);
    
    Box ctad_box(100);
    
    return (i_sum == 12) && (ibox.get() == 42) && (ctad_box.get() == 100) ? 10 : 0;
}

template<typename T>
T var_sum(T val) {
    return val;
}

template<typename T, typename... Rest>
T var_sum(T first, Rest... rest) {
    return first + var_sum(rest...);
}

int test_variadic() {
    int result = var_sum(1, 2, 3, 4);
    return result == 10 ? 10 : 0;
}

template<typename... Args>
auto fold_add(Args... args) {
    return (args + ...);
}

int test_fold() {
    int result = fold_add(1, 2, 3, 4);
    return result == 10 ? 10 : 0;
}

// ============================================================================
// SECTION 7: CONSTEXPR
// ============================================================================

constexpr int const_val = 42;

constexpr int const_add(int a, int b) {
    return a + b;
}

constexpr int factorial(int n) {
    return (n <= 1) ? 1 : n * factorial(n - 1);
}

int test_constexpr() {
    constexpr int x = 10;
    constexpr int y = 20;
    constexpr int sum = x + y;
    
    static_assert(sum == 30, "Sum should be 30");
    
    constexpr int result = const_add(5, 5);
    static_assert(result == 10, "Result should be 10");
    
    constexpr int fact5 = factorial(5);
    static_assert(fact5 == 120, "5! should be 120");
    
    return (sum == 30) && (result == 10) && (fact5 == 120) ? 10 : 0;
}

// ============================================================================
// SECTION 8: LAMBDAS
// ============================================================================

int test_lambdas() {
    auto lambda1 = []() { return 5; };
    int r1 = lambda1();
    
    auto lambda2 = [](int a, int b) { return a + b; };
    int r2 = lambda2(3, 4);
    
    int x = 10;
    auto lambda3 = [x]() { return x; };
    int r3 = lambda3();
    
    int y = 0;
    auto lambda4 = [&y]() { y = 20; };
    lambda4();
    
    auto lambda5 = []() { return 8; }();
    
    return (r1 == 5) && (r2 == 7) && (r3 == 10) && (y == 20) && (lambda5 == 8) ? 10 : 0;
}

// ============================================================================
// SECTION 9: MODERN FEATURES
// ============================================================================

int test_auto() {
    auto x = 42;
    auto y = 3.14;
    auto z = true;
    
    return (x == 42) && (z == true) ? 10 : 0;
}

int test_decltype() {
    int x = 42;
    decltype(x) y = 10;
    
    return y == 10 ? 10 : 0;
}

typedef int Integer;
using Real = double;

int test_typedef() {
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
    
    return (c == RED) ? 10 : 0;
}

union Data {
    int i;
    float f;
};

int test_union() {
    Data d;
    d.i = 42;
    int val = d.i;
    
    return val == 42 ? 10 : 0;
}

struct Point {
    int x;
    int y;
    int z;
};

int test_designated_init() {
    Point p = {.x = 10, .y = 20, .z = 30};
    
    return (p.x == 10) && (p.y == 20) && (p.z == 30) ? 10 : 0;
}

// ============================================================================
// SECTION 10: ADVANCED FEATURES
// ============================================================================

// Test string literals
int test_string_literals() {
    const char* str1 = "Hello";
    const char* str2 = "World";
    
    // Test that strings are stored correctly
    char c1 = str1[0];
    char c2 = str2[0];
    
    return (c1 == 'H') && (c2 == 'W') ? 10 : 0;
}

// Test multi-dimensional arrays
int test_multidim_arrays() {
    int matrix[2][3];
    matrix[0][0] = 1;
    matrix[0][1] = 2;
    matrix[1][0] = 3;
    matrix[1][1] = 4;
    
    int sum = matrix[0][0] + matrix[0][1] + matrix[1][0] + matrix[1][1];
    
    return sum == 10 ? 10 : 0;
}

// Test pointer to pointer
int test_pointer_to_pointer() {
    int value = 42;
    int* ptr = &value;
    int** pptr = &ptr;
    
    int result = **pptr;
    
    return result == 42 ? 10 : 0;
}

// Test struct with complex initialization
struct ComplexStruct {
    int a;
    int b;
    int c;
};

int test_struct_init() {
    ComplexStruct s1 = {1, 2, 3};
    ComplexStruct s2;
    s2.a = 4;
    s2.b = 5;
    s2.c = 6;
    
    int sum = s1.a + s1.b + s1.c + s2.a + s2.b + s2.c;
    
    return sum == 21 ? 10 : 0;
}

// Test references
int test_references() {
    int x = 10;
    int& ref = x;
    
    ref = 20;
    
    return x == 20 ? 10 : 0;
}

// Test const references
int test_const_references() {
    int x = 42;
    const int& cref = x;
    
    int y = cref;
    
    return y == 42 ? 10 : 0;
}

// Test ternary operator
int test_ternary() {
    int a = 5;
    int b = 10;
    
    int max = (a > b) ? a : b;
    int min = (a < b) ? a : b;
    
    return (max == 10) && (min == 5) ? 10 : 0;
}

// Test nested structures
struct Outer {
    int x;
    struct Inner {
        int y;
    };
    Inner inner;
};

int test_nested_struct() {
    Outer o;
    o.x = 5;
    o.inner.y = 7;
    
    int sum = o.x + o.inner.y;
    
    return sum == 12 ? 10 : 0;
}

// Test static variables
int test_static_vars() {
    static int counter = 0;
    counter++;
    counter++;
    counter++;
    
    return counter == 3 ? 10 : 0;
}

// Test global variables
int global_test_var = 100;

int test_global_vars() {
    int local = global_test_var;
    global_test_var = 200;
    
    return (local == 100) && (global_test_var == 200) ? 10 : 0;
}

// ============================================================================
// SECTION 11: ALTERNATIVE TOKENS AND C++20 EXTRAS
// ============================================================================

// Test alternative operator representations (ISO 646 keywords)
int test_alternative_operators() {
    int a = 12;  // 1100
    int b = 10;  // 1010
    
    // Alternative keywords for bitwise operators
    int and_result = a bitand b;   // Same as a & b = 8 (1000)
    int or_result = a bitor b;     // Same as a | b = 14 (1110)
    int xor_result = a xor b;      // Same as a ^ b = 6 (0110)
    int compl_result = compl a;    // Same as ~a = -13
    
    // Alternative keywords for logical operators
    bool x = true;
    bool y = false;
    bool and_logical = x and y;    // Same as x && y = false
    bool or_logical = x or y;      // Same as x || y = true
    bool not_logical = not y;      // Same as !y = true
    
    return (and_result == 8) and (or_result == 14) and (xor_result == 6) and 
           (not_logical) and (or_logical) ? 10 : 0;
}

// Test sizeof with various types
int test_sizeof_operator() {
    int i = 42;
    char c = 'A';
    double d = 3.14;
    
    int size_int = sizeof(i);
    int size_char = sizeof(c);
    int size_double = sizeof(d);
    int size_int_type = sizeof(int);
    
    return (size_int == 4) && (size_char == 1) && (size_double == 8) ? 10 : 0;
}

// Test comma operator
int test_comma_operator() {
    int a = 1;
    int b = 2;
    int c;
    
    c = (a = 5, b = 10, a + b);  // c = 15
    
    return c == 15 ? 10 : 0;
}

// Test nullptr comparisons
int test_nullptr_advanced() {
    int* p1 = nullptr;
    int* p2 = nullptr;
    int x = 42;
    int* p3 = &x;
    
    bool both_null = (p1 == p2);
    bool not_null = (p3 != nullptr);
    
    return (both_null && not_null) ? 10 : 0;
}

// Test explicit type conversions
int test_explicit_casts() {
    double d = 3.7;
    int i = static_cast<int>(d);  // 3
    
    float f = 2.5f;
    int j = static_cast<int>(f);  // 2
    
    bool b = static_cast<bool>(42);  // true
    
    return (i == 3) && (j == 2) && b ? 10 : 0;
}

// Test address-of and dereference operators
int test_address_and_deref() {
    int x = 42;
    int* ptr = &x;
    int value = *ptr;
    
    *ptr = 100;
    
    return (value == 42) && (x == 100) && (*ptr == 100) ? 10 : 0;
}

// Test array subscript operator
int test_array_subscript() {
    int arr[5] = {10, 20, 30, 40, 50};
    
    int first = arr[0];
    int third = arr[2];
    int last = arr[4];
    
    arr[1] = 25;
    
    return (first == 10) && (third == 30) && (arr[1] == 25) && (last == 50) ? 10 : 0;
}

// Test octal literals
int test_octal_literals() {
    int oct1 = 010;   // 8 in decimal
    int oct2 = 077;   // 63 in decimal
    int oct3 = 0100;  // 64 in decimal
    
    return (oct1 == 8) && (oct2 == 63) && (oct3 == 64) ? 10 : 0;
}

// Test binary literals (C++14 feature, but widely supported)
int test_binary_literals() {
    int bin1 = 0b1010;     // 10 in decimal
    int bin2 = 0b11111111; // 255 in decimal
    
    return (bin1 == 10) && (bin2 == 255) ? 10 : 0;
}

// Test digit separators (C++14 feature)
int test_digit_separators() {
    int large = 1'000'000;        // 1 million
    int hex = 0xFF'FF;            // 65535
    long long big = 1'000'000'000; // 1 billion
    
    return (large == 1000000) && (hex == 65535) && (big == 1000000000) ? 10 : 0;
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main() {
    int total = 0;
    int expected = 0;
    
    // Section 1: Types (30 points)
    total += test_integer_types();      // 10
    total += test_floating_point();     // 10
    total += test_bool_nullptr();       // 10
    expected += 30;
    
    // Section 2: Operators (50 points)
    total += test_arithmetic();         // 10
    total += test_bitwise();            // 10
    total += test_logical();            // 10
    total += test_comparison();         // 10
    total += test_compound_assign();    // 10
    expected += 50;
    
    // Section 3: Control Flow (50 points)
    total += test_if_else();            // 10
    total += test_for_while();          // 10
    total += test_do_while();           // 10
    total += test_switch();             // 10
    total += test_break_continue();     // 10
    expected += 50;
    
    // Section 4: Functions (20 points)
    total += test_functions();          // 10
    total += test_func_pointers();      // 10
    expected += 20;
    
    // Section 5: Classes/OOP (30 points)
    total += test_basic_classes();      // 10
    total += test_inheritance();        // 10
    total += test_new_delete();         // 10
    expected += 30;
    
    // Section 6: Templates (30 points)
    total += test_templates();          // 10
    total += test_variadic();           // 10
    total += test_fold();               // 10
    expected += 30;
    
    // Section 7: Constexpr (10 points)
    total += test_constexpr();          // 10
    expected += 10;
    
    // Section 8: Lambdas (10 points)
    total += test_lambdas();            // 10
    expected += 10;
    
    // Section 9: Modern Features (60 points)
    total += test_auto();               // 10
    total += test_decltype();           // 10
    total += test_typedef();            // 10
    total += test_enums();              // 10
    total += test_union();              // 10
    total += test_designated_init();    // 10
    expected += 60;
    
    // Section 10: Advanced Features (100 points)
    total += test_string_literals();    // 10
    total += test_multidim_arrays();    // 10
    total += test_pointer_to_pointer(); // 10
    total += test_struct_init();        // 10
    total += test_references();         // 10
    total += test_const_references();   // 10
    total += test_ternary();            // 10
    total += test_nested_struct();      // 10
    total += test_static_vars();        // 10
    total += test_global_vars();        // 10
    expected += 100;
    
    // Section 11: Alternative Tokens and C++20 Extras (100 points)
    total += test_alternative_operators(); // 10
    total += test_sizeof_operator();       // 10
    total += test_comma_operator();        // 10
    total += test_nullptr_advanced();      // 10
    total += test_explicit_casts();        // 10
    total += test_address_and_deref();     // 10
    total += test_array_subscript();       // 10
    total += test_octal_literals();        // 10
    total += test_binary_literals();       // 10
    total += test_digit_separators();      // 10
    expected += 100;
    
    // Return 0 if all tests pass, otherwise return number of missing points
    return expected - total;
}
