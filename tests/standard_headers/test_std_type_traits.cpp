// Test standard <type_traits> header
#include <type_traits>

int main() {
    // Test basic type traits
    static_assert(std::is_integral<int>::value);
    static_assert(!std::is_integral<float>::value);
    static_assert(std::is_pointer<int*>::value);
    static_assert(!std::is_pointer<int>::value);
    
    return 0;
}
