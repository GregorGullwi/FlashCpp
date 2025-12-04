// Minimal failing test case
struct Test {
    auto get_lambda() {
        return []() { return 5; };
    }
};

int main() {
    Test obj;
    auto lambda = obj.get_lambda();
    return lambda();  // This should call lambda's operator()
}
