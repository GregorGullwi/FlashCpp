// Test variable template brace initialization
// Pattern from c++config.h: inline constexpr in_place_type_t<_Tp> in_place_type{};
// This is a key blocker for <utility> header

template<typename T>
struct in_place_type_t {
    explicit in_place_type_t() = default;
};

// Variable template with brace initialization (empty braces)
template<typename T>
inline constexpr in_place_type_t<T> in_place_type{};

// Variable template with scalar type brace initialization
template<typename T>
inline constexpr int zero_value{};

// Variable template with dependent type brace initialization  
template<typename T>
inline constexpr T default_value{};

int main() {
    // Use the variable templates to verify they compile
    auto x = in_place_type<int>;
    int y = zero_value<double>;
    int z = default_value<int>;
    
    return 0;
}
