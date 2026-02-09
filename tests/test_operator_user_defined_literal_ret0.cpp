// Test user-defined literal operator parsing and invocation
// "hello"_len calls operator""_len("hello", 5) returning the length
inline constexpr int operator""_len(const char* str, unsigned long len) noexcept {
    return static_cast<int>(len);
}

int main() {
    int n = "hello"_len;
    // "hello" has length 5, subtract 5 to get 0 for success
    return n - 5;
}
