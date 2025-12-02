// Test for [this] capture in lambdas
// STATUS: Mostly working, but crashes due to type_index tracking issue
// 
// ISSUE: When accessing this->member in a lambda with [this] capture,
// the codegen can't determine the type_index of the enclosing class.
// Need to store enclosing_struct_type_index in LambdaInfo.

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
