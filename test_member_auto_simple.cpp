// Test auto return type deduction in member function
struct Test {
    auto get_value() {
        return 42;
    }
};

int main() {
    Test obj;
    int x = obj.get_value();  // Should work if auto is deduced as int
    return x;
}
