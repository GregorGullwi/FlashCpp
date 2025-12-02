// Test for [this] capture in lambdas
// Currently NOT WORKING - parser recognizes [this] but codegen needs work

struct MyClass {
    int value = 5;
    
    int test_capture_this() {
        // TODO: Implement [this] capture support in codegen
        // The captured 'this' pointer needs to be loaded from closure
        // when accessing members inside the lambda
        auto lambda = [this]() { return this->value; };
        return lambda();
    }
};

int main() {
    MyClass obj;
    return obj.test_capture_this();  // Expected: 5
}
