// Test capture-all with explicit this
struct TestStruct {
    int value = 5;
    
    int test() {
        auto lambda = [=, this]() { return this->value; };
        return lambda();
    }
};

int main() {
    TestStruct ts;
    return ts.test();
}
