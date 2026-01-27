struct TestStruct {
    static const int static_value = 42;
    int instance_value;
};

int main() {
    TestStruct t;
    t.instance_value = 10;

    // Test static member access
    int result = TestStruct::static_value + t.instance_value;
    return result - 52;  // Should return 0 if working correctly
}