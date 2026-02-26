// Test: constexpr evaluation of binary expressions over variable template references
// Pattern: fold result stored in local constexpr variable, used in if constexpr
template<typename T> inline constexpr unsigned type_id = 1;
template<> inline constexpr unsigned type_id<int> = 2;
template<> inline constexpr unsigned type_id<float> = 40;

template<typename... Ts>
constexpr unsigned combined_id() {
    constexpr unsigned result = (type_id<Ts> | ...);
    if constexpr (result > 10) {
        return result;
    } else {
        return 0;
    }
}

int main() {
    return combined_id<int, float>();  // 2 | 40 = 42
}
