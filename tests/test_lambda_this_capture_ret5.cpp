// Test for [this] capture in lambdas
// STATUS: WORKING!

struct MyClass {
    int value = 5;
    
    int test_capture_this() {
        auto lambda = [this]() { return this->value; };
        return lambda();
    }
};

int main() {
    MyClass obj;
    return obj.test_capture_this();  // Expected: 5
}
