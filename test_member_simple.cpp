// Simpler test - member function returning lambda without captures
struct Test {
    auto get_lambda() {
        return []() { return 5; };
    }
};

int main() {
    Test obj;
    auto lambda = obj.get_lambda();
    return lambda();
}
