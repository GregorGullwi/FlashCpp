// Test 1: Basic typedef
typedef unsigned long long uintptr_t;

// Basic typedef for primitive types
typedef int Integer;
// Test 2: typedef with inline struct definition (anonymous struct)
typedef struct {
    int x;
    int y;
} Point;

// Pointer typedef
typedef int* IntPtr;

// Typedef chaining
typedef Integer MyInt;

// Function using typedef'd types
Integer add(Integer a, Integer b) {
    return a + b;
}
// Test 3: typedef with inline struct definition (named struct)
typedef struct __name_example {
    int a;
    int b;
} _name_example;

int main() {
    // Test basic typedef usage
    Integer x = 10;
    Integer y = 20;
    Integer sum = add(x, y);

    // Test pointer typedef
    IntPtr ptr = &x;

    // Test typedef chaining
    MyInt z = 30;
    // Test basic typedef
    uintptr_t ptr = 42;

    // Test typedef struct (anonymous)
    Point p;
    p.x = 10;
    p.y = 20;

    // Test typedef struct (named)
    _name_example ex;
    ex.a = 5;
    ex.b = 15;

    // Calculate sums separately to avoid expression complexity issues
    int sum1 = p.x + p.y;   // 30
    int sum2 = ex.a + ex.b; // 20
    return sum1 + sum2;      // Should return 50
}
