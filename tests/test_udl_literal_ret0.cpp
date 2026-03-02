// Test user-defined literal (UDL) suffix lexing
// Validates that numeric literals with UDL suffixes (e.g., 128_ms)
// are lexed as single tokens instead of separate number+identifier tokens
// Pattern from <chrono>/<thread>: if (__elapsed > 128ms)

struct Duration {
    long long val;
};

constexpr Duration operator""_ms(unsigned long long v) {
    return Duration{static_cast<long long>(v)};
}

constexpr Duration operator""_us(unsigned long long v) {
    return Duration{static_cast<long long>(v)};
}

// Test that UDL literals are lexed as single tokens
constexpr Duration d1 = 128_ms;
constexpr Duration d2 = 64_us;

int main() {
    return 0;
}
