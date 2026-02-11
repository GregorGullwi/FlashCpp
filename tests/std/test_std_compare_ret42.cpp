// Test: std::strong_ordering with manual comparison category types
// This tests the <compare> header's core functionality using FlashCpp-compatible definitions

namespace std {
    class strong_ordering {
        int _M_value;
        constexpr explicit strong_ordering(int v) : _M_value(v) {}
    public:
        static const strong_ordering less;
        static const strong_ordering equal;
        static const strong_ordering equivalent;
        static const strong_ordering greater;

        friend constexpr bool operator==(strong_ordering v, int) {
            return v._M_value == 0;
        }
        friend constexpr bool operator<(strong_ordering v, int) {
            return v._M_value < 0;
        }
        friend constexpr bool operator>(strong_ordering v, int) {
            return v._M_value > 0;
        }
        friend constexpr bool operator<=(strong_ordering v, int) {
            return v._M_value <= 0;
        }
        friend constexpr bool operator>=(strong_ordering v, int) {
            return v._M_value >= 0;
        }
        friend constexpr bool operator!=(strong_ordering v, int) {
            return v._M_value != 0;
        }
    };
    
    const strong_ordering strong_ordering::less = strong_ordering(-1);
    const strong_ordering strong_ordering::equal = strong_ordering(0);
    const strong_ordering strong_ordering::equivalent = strong_ordering(0);
    const strong_ordering strong_ordering::greater = strong_ordering(1);
}

struct Point {
    int x, y;
    std::strong_ordering operator<=>(const Point& other) const {
        if (x < other.x) return std::strong_ordering::less;
        if (x > other.x) return std::strong_ordering::greater;
        if (y < other.y) return std::strong_ordering::less;
        if (y > other.y) return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }
};

int main() {
    Point a{1, 2};
    Point b{1, 3};
    Point c{2, 1};
    Point d{1, 2};
    
    int result = 0;
    
    // Test less
    std::strong_ordering r1 = a <=> b;
    if (r1 < 0) result += 1;
    
    // Test greater
    std::strong_ordering r2 = c <=> a;
    if (r2 > 0) result += 2;
    
    // Test equal
    std::strong_ordering r3 = a <=> d;
    if (r3 == 0) result += 4;
    
    // Test not-equal
    if (r1 != 0) result += 8;
    
    // Test less-or-equal
    if (r1 <= 0) result += 16;
    
    // Test greater-or-equal
    if (r2 >= 0) result += 32;
    
    // result should be 1+2+4+8+16+32 = 63
    return result == 63 ? 42 : result;
}
