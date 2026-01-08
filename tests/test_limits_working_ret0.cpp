// Test that <limits> header compiles and works
// As of January 8, 2026, this header compiles successfully!
#include <limits>

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
