// Test case for __underlying_type intrinsic in type specifier context
// This tests the pattern: using type = __underlying_type(_Tp);
// Expected return value: 42

enum MyEnum : int { A = 10, B = 20, C = 12 };

template<typename _Tp>
struct underlying_type_impl {
    using type = __underlying_type(_Tp);
};

// Verify the underlying type works correctly
int main() {
    underlying_type_impl<MyEnum>::type x = A + B + C;  // 10 + 20 + 12 = 42
    return x;
}
