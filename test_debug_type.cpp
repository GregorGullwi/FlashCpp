// Debug test - print type of lambda variable
struct Test {
    auto get_lambda() {
        return []() { return 5; };
    }
};

int main() {
    Test obj;
    auto lambda = obj.get_lambda();
    // Don't call lambda() yet - just return
    return 0;
}
