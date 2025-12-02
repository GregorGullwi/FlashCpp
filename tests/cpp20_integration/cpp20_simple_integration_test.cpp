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
    
    // Return 0 if all tests pass, otherwise return number of missing points
    return expected - total;
}
