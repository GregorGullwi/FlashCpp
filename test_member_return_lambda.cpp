// Minimal test for member function returning lambda
struct TestCopyThis {
    int value = 5;
    
    auto get_lambda() {
        return [*this]() { return this->value; };
    }
};

int main() {
    TestCopyThis obj;
    auto lambda = obj.get_lambda();
    return lambda();  // Should return 5
}
