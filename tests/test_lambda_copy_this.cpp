// Test for [*this] capture in lambdas (C++17)
// STATUS: WORKING with explicit this->member access

struct MyClass {
    int value = 10;
    
    int test_copy_this() {
        // [*this] captures a copy of the entire object
        auto lambda = [*this]() { return this->value; };
        return lambda();
    }
    
    int test_copy_this_modified() {
        int x = 5;
        // [*this] with other captures
        auto lambda = [*this, x]() { return this->value + x; };
        return lambda();
    }
};

int main() {
    MyClass obj;
    int result = 0;
    result += obj.test_copy_this();           // 10
    result += obj.test_copy_this_modified();  // 15
    return result;  // Expected: 25
}
