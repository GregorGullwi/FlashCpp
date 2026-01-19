// Test standard <ratio> header
#include <ratio>

int main() {
    // Test basic ratio types
    using half = std::ratio<1, 2>;
    using third = std::ratio<1, 3>;
    
    // Test predefined ratios
    using kilo = std::kilo;
    using mega = std::mega;
    using milli = std::milli;
    
    // Test ratio arithmetic
    using sum = std::ratio_add<half, third>;
    using diff = std::ratio_subtract<half, third>;
    using prod = std::ratio_multiply<half, third>;
    using quot = std::ratio_divide<half, third>;
    
    // Test ratio comparison
    static_assert(std::ratio_equal<std::ratio<1, 2>, std::ratio<2, 4>>::value);
    static_assert(std::ratio_less<third, half>::value);
    
    return 0;
}
