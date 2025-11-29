// Test variable template with integer values only
template<typename T>
constexpr T val = T(42);

int main() {
    int a = val<int>;
    int b = val<int>;
    return a + b;  // Should be 84 (42 + 42)
}
