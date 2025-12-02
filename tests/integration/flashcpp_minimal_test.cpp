// FlashCpp-Compatible Integration Test
// Minimal version that works within FlashCpp's current limitations
// This tests features that are documented as working in the README

// Basic arithmetic function
int add_nums(int a, int b) {
    return a + b;
}

// Test basic integer operations
int test_integers() {
    int x = 10;
    int y = 20;
    int sum = x + y;
    int diff = y - x;
    int prod = x * 2;
    
    if (sum == 30 && diff == 10 && prod == 20) {
        return 10;
    }
    return 0;
}

// Test floating point
int test_floats() {
    float a = 5.0f;
    float b = 2.0f;
    float result = a + b;
    
    if (result > 6.9f && result < 7.1f) {
        return 10;
    }
    return 0;
}

// Test comparison
int test_comparison() {
    int a = 10;
    int b = 20;
    
    if (a < b && b > a) {
        return 10;
    }
    return 0;
}

// Test control flow
int test_loops() {
    int sum = 0;
    
    // For loop
    for (int i = 0; i < 5; i++) {
        sum = sum + 1;
    }
    
    // While loop
    int j = 0;
    while (j < 5) {
        sum = sum + 1;
        j = j + 1;
    }
    
    if (sum == 10) {
        return 10;
    }
    return 0;
}

// Test switch statement
int test_switch() {
    int value = 2;
    int result = 0;
    
    switch (value) {
        case 1:
            result = 100;
            break;
        case 2:
            result = 200;
            break;
        default:
            result = 0;
    }
    
    if (result == 200) {
        return 10;
    }
    return 0;
}

// Simple class
class Counter {
public:
    int count;
    
    Counter() {
        count = 0;
    }
    
    void increment() {
        count = count + 1;
    }
    
    int get() {
        return count;
    }
};

// Test classes
int test_classes() {
    Counter c;
    c.increment();
    c.increment();
    c.increment();
    
    int val = c.get();
    
    if (val == 3) {
        return 10;
    }
    return 0;
}

// Base class
class Animal {
public:
    int legs;
    
    Animal() {
        legs = 4;
    }
    
    virtual int getLegs() {
        return legs;
    }
};

// Derived class
class Bird : public Animal {
public:
    Bird() {
        legs = 2;
    }
    
    int getLegs() override {
        return legs;
    }
};

// Test inheritance
int test_inheritance() {
    Bird b;
    Animal* ptr = &b;
    int result = ptr->getLegs();
    
    if (result == 2) {
        return 10;
    }
    return 0;
}

// Template function
template<typename T>
T add_template(T a, T b) {
    return a + b;
}

// Test templates
int test_templates() {
    int int_result = add_template(5, 7);
    float float_result = add_template(3.0f, 4.0f);
    
    if (int_result == 12 && float_result > 6.9f && float_result < 7.1f) {
        return 10;
    }
    return 0;
}

// Template class
template<typename T>
class Box {
public:
    T value;
    
    Box(T v) {
        value = v;
    }
    
    T getValue() {
        return value;
    }
};

// Test template classes
int test_template_classes() {
    Box<int> int_box(42);
    Box<float> float_box(3.14f);
    
    int i = int_box.getValue();
    float f = float_box.getValue();
    
    if (i == 42 && f > 3.0f && f < 3.2f) {
        return 10;
    }
    return 0;
}

// Constexpr function
constexpr int square(int x) {
    return x * x;
}

// Test constexpr
int test_constexpr() {
    constexpr int a = 5;
    constexpr int b = square(a);
    
    if (b == 25) {
        return 10;
    }
    return 0;
}

// Test lambda
int test_lambda() {
    auto add_lambda = [](int a, int b) {
        return a + b;
    };
    
    int result = add_lambda(10, 20);
    
    if (result == 30) {
        return 10;
    }
    return 0;
}

// Test lambda with capture
int test_lambda_capture() {
    int x = 10;
    auto get_x = [x]() {
        return x;
    };
    
    int result = get_x();
    
    if (result == 10) {
        return 10;
    }
    return 0;
}

// Test auto
int test_auto() {
    auto x = 42;
    auto y = 3.14f;
    auto z = true;
    
    if (x == 42 && y > 3.0f && z) {
        return 10;
    }
    return 0;
}

// Test pointer operations
int test_pointers() {
    int value = 42;
    int* ptr = &value;
    int result = *ptr;
    
    if (result == 42) {
        return 10;
    }
    return 0;
}

// Test arrays
int test_arrays() {
    int arr[5];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    
    int sum = arr[0] + arr[1] + arr[2];
    
    if (sum == 60) {
        return 10;
    }
    return 0;
}

// Main function - runs all tests
int main() {
    int total = 0;
    
    total = total + test_integers();
    total = total + test_floats();
    total = total + test_comparison();
    total = total + test_loops();
    total = total + test_switch();
    total = total + test_classes();
    total = total + test_inheritance();
    total = total + test_templates();
    total = total + test_template_classes();
    total = total + test_constexpr();
    total = total + test_lambda();
    total = total + test_lambda_capture();
    total = total + test_auto();
    total = total + test_pointers();
    total = total + test_arrays();
    
    // Expected: 15 tests * 10 points = 150
    int expected = 150;
    
    // Return 0 if all pass, otherwise return missing points
    return expected - total;
}
