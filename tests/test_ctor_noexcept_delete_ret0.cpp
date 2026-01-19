// Test constructor with noexcept = delete
struct Test {
    constexpr Test() noexcept = delete;
};

int main() {
    return 0;
}
