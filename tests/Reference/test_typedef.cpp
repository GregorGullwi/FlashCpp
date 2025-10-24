// Test: typedef support

// Basic typedef for primitive types
typedef int Integer;

// Pointer typedef
typedef int* IntPtr;

// Typedef chaining
typedef Integer MyInt;

// Function using typedef'd types
Integer add(Integer a, Integer b) {
    return a + b;
}

int main() {
    // Test basic typedef usage
    Integer x = 10;
    Integer y = 20;
    Integer sum = add(x, y);

    // Test pointer typedef
    IntPtr ptr = &x;

    // Test typedef chaining
    MyInt z = 30;

    return sum + z;
}

