// Test complex fold expression: (__cmp_cat_id<_Ts> | ...)
// Variable template specializations combined via fold expression
template<typename T> inline constexpr unsigned __cmp_cat_id = 1;
template<> inline constexpr unsigned __cmp_cat_id<int> = 2;
template<> inline constexpr unsigned __cmp_cat_id<float> = 40;

template<typename... _Ts>
constexpr unsigned __common_cmp_cat() {
    return (__cmp_cat_id<_Ts> | ...);
}

int main() {
    // 2 | 40 = 42
    return __common_cmp_cat<int, float>();
}
