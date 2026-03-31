// Test: Built-in three-way comparison (<=>) for integer types
// Returns 42 if comparisons produce correct ordering values
namespace std {
    class strong_ordering {
        signed char _M_value;
    public:
        constexpr strong_ordering(signed char v) noexcept : _M_value(v) {}

        static const strong_ordering less;
        static const strong_ordering equal;
        static const strong_ordering equivalent;
        static const strong_ordering greater;

        friend constexpr bool operator>(strong_ordering __v, int) noexcept { return __v._M_value > 0; }
        friend constexpr bool operator<(strong_ordering __v, int) noexcept { return __v._M_value < 0; }
        friend constexpr bool operator==(strong_ordering __v, int) noexcept { return __v._M_value == 0; }
    };

    inline constexpr strong_ordering strong_ordering::less{static_cast<signed char>(-1)};
    inline constexpr strong_ordering strong_ordering::equal{static_cast<signed char>(0)};
    inline constexpr strong_ordering strong_ordering::equivalent{static_cast<signed char>(0)};
    inline constexpr strong_ordering strong_ordering::greater{static_cast<signed char>(1)};
}

int main() {
    int a = 5;
    int b = 3;
    std::strong_ordering result = a <=> b;
    
    int ret = 0;
    if (result > 0) ret += 10;  // 5 > 3 → greater → +10
    
    int c = 7;
    int d = 7;
    std::strong_ordering eq_result = c <=> d;
    if (eq_result == 0) ret += 20;  // 7 == 7 → equal → +20
    
    int e = 2;
    int f = 9;
    std::strong_ordering lt_result = e <=> f;
    if (lt_result < 0) ret += 12;  // 2 < 9 → less → +12
    
    return ret;  // Should be 10 + 20 + 12 = 42
}
