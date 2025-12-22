// Test namespace features

// Test namespace
namespace TestNamespace {
    int getValue() {
        return 42;
    }

    int add(int a, int b) {
        return a + b;
    }
}

// Test nested namespace
namespace Outer {
    namespace Inner {
        int getInnerValue() {
            return 100;
        }
    }
}

// Test 1: Basic namespace access
int test_basic_namespace() {
    return TestNamespace::getValue();
}

// Test 2: Nested namespace access
int test_nested_namespace() {
    return Outer::Inner::getInnerValue();
}

// Test 3: Namespace function call
int test_namespace_function() {
    return TestNamespace::add(10, 20);
}


int main() {
    return test_basic_namespace();
}
