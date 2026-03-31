// Test: Built-in three-way comparison (<=>) for unsigned integer types
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
    unsigned int a = 100u;
    unsigned int b = 50u;
    std::strong_ordering r1 = a <=> b;

    unsigned long long c = 0ull;
    unsigned long long d = 1ull;
    std::strong_ordering r2 = c <=> d;

    short e = -5;
    short f = -5;
    std::strong_ordering r3 = e <=> f;

    int ret = 0;
    if (r1 > 0) ret += 1;   // 100 > 50 → greater → +1
    if (r2 < 0) ret += 2;   // 0 < 1 → less → +2
    if (r3 == 0) ret += 4;  // -5 == -5 → equal → +4
    
    return ret;  // 1 + 2 + 4 = 7
}
