// Test enhanced using directives, using declarations, and namespace aliases

namespace Math {
    int add(int a, int b) {
        return a + b;
    }
    
    int multiply(int a, int b) {
        return a * b;
    }
    
    int value = 100;
}

namespace Utils {
    int helper() {
        return 42;
    }
}

// Test 1: Using directive
int test_using_directive() {
    using namespace Math;
    int result = add(10, 20);
    return result;  // Should return 30
}

// Test 2: Using declaration
int test_using_declaration() {
    using Math::multiply;
    int result = multiply(5, 6);
    return result;  // Should return 30
}

// Test 3: Namespace alias
int test_namespace_alias() {
    namespace M = Math;
    int result = M::add(15, 25);
    return result;  // Should return 40
}

// Test 4: Multiple using directives
int test_multiple_using() {
    using namespace Math;
    using namespace Utils;
    int x = add(10, 20);
    int y = helper();
    return x + y;  // Should return 72
}

// Test 5: Using directive with qualified access
int test_using_with_qualified() {
    using namespace Math;
    int x = add(5, 10);
    int y = Math::multiply(2, 3);
    return x + y;  // Should return 21
}


int main() {
    return test_using_directive();
}
