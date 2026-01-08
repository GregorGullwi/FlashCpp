// Test that numeric_limits works without including standard headers
// As of January 8, 2026, numeric_limits functionality works!
// This test manually implements numeric_limits to avoid depending on <limits> header

namespace std {
    template<typename T>
    struct numeric_limits {
        static constexpr T max() { return T(); }
        static constexpr T min() { return T(); }
        static constexpr T lowest() { return T(); }
    };
    
    // Specialization for int
    template<>
    struct numeric_limits<int> {
        static constexpr int max() { return 2147483647; }
        static constexpr int min() { return -2147483648; }
        static constexpr int lowest() { return -2147483648; }
    };
    
    // Specialization for float
    template<>
    struct numeric_limits<float> {
        static constexpr float max() { return 3.40282347e+38f; }
        static constexpr float min() { return 1.17549435e-38f; }
        static constexpr float lowest() { return -3.40282347e+38f; }
    };
}

int main() {
    // Test numeric_limits for int
    int max_int = std::numeric_limits<int>::max();
    int min_int = std::numeric_limits<int>::min();
    
    // Test numeric_limits for float
    float max_float = std::numeric_limits<float>::max();
    float min_float = std::numeric_limits<float>::lowest();
    
    // Basic sanity checks
    if (max_int <= 0) return 1;
    if (min_int >= 0) return 2;
    if (max_float <= 0.0f) return 3;
    if (min_float >= 0.0f) return 4;
    
    // Success!
    return 0;
}
