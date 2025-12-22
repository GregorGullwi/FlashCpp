// Test for [*this] capture with implicit member access
// STATUS: WORKING!

struct MyClass {
    int value = 10;
    int other = 3;
    
    int test_implicit_access() {
        // [*this] with implicit member access (no this->)
        auto lambda = [*this]() { return value; };
        return lambda();
    }
    
    int test_implicit_multiple() {
        // [*this] accessing multiple members implicitly
        auto lambda = [*this]() { return value + other; };
        return lambda();
    }
    
    int test_mixed_access() {
        int x = 2;
        // Mix of implicit member access and local capture
        auto lambda = [*this, x]() { return value + x; };
        return lambda();
    }
};

int main() {
    MyClass obj;
    int result = 0;
    result += obj.test_implicit_access();      // 10
    result += obj.test_implicit_multiple();    // 13
    result += obj.test_mixed_access();         // 12
    return result;  // Expected: 35
}
