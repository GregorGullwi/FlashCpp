// Test variable template with static and inline specifiers
template<typename T>
static constexpr T static_val = T(33);

template<typename T>
inline constexpr T inline_val = T(44);

template<typename T>
static inline constexpr T static_inline_val = T(55);

int main() {
    int a = static_val<int>;
    int b = inline_val<int>;
    int c = static_inline_val<int>;
    return a + b + c;  // Should be 132 (33 + 44 + 55)
}
