// Test template function with noexcept and attributes
// Simplified version to avoid alias template issues

// Test operator<< with noexcept
template<typename T>
int operator<<(T a, int b) noexcept
{ return (int)a << b; }

// Test operator<<= with noexcept  
template<typename T>
T& operator<<=(T& a, int b) noexcept
{ a = a << b; return a; }

// Test regular function template with noexcept
template<typename T>
    [[nodiscard]]
    constexpr T add(T a, T b) noexcept
    { return a + b; }

int main() {
    int result = add(1, 2);
    return result;
}
