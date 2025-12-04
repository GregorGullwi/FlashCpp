// Test 22: C++20 Capture-all with explicit this
struct TestStruct {
    int value = 5;
    
    int test_capture_all_with_this() {
        auto lambda = [=, this]() { return this->value; };
        return lambda();  // 5
    }
};

int main() { TestStruct ts; return ts.test_capture_all_with_this(); }
