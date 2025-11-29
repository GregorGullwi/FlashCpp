// Simple test - just return a constant from a variable template
template<typename T>
constexpr T val = T(42);

int main() {
    int x = val<int>;
    return x;
}
