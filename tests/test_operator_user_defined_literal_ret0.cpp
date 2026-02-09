// Test user-defined literal operator parsing
struct MyString {
    const char* data;
    unsigned long len;
};

inline constexpr MyString operator""_ms(const char* str, unsigned long len) noexcept {
    return MyString{str, len};
}

int main() {
    return 0;
}
