// Test: static_cast to comparison category type (used in standard library <compare>)
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

// Mimics __char_traits_cmp_cat from libstdc++
std::strong_ordering char_traits_cmp_cat(int __cmp) {
    return static_cast<std::strong_ordering>(__cmp <=> 0);
}

int main() {
    // char_traits_cmp_cat(positive) should return greater
    std::strong_ordering r1 = char_traits_cmp_cat(5);
    // char_traits_cmp_cat(0) should return equal
    std::strong_ordering r2 = char_traits_cmp_cat(0);
    // char_traits_cmp_cat(negative) should return less
    std::strong_ordering r3 = char_traits_cmp_cat(-3);

    int ret = 0;
    if (r1 > 0) ret += 10;   // greater → +10
    if (r2 == 0) ret += 20;  // equal → +20
    if (r3 < 0) ret += 12;   // less → +12
    
    return ret;  // 10 + 20 + 12 = 42
}
