// Test template functions with various trailing specifiers
// This test verifies the fix for parsing namespace-scope template functions
// with trailing specifiers like noexcept

// Test 1: Template function with noexcept
template<typename T>
constexpr T add(T a, T b) noexcept
{ return a + b; }

// Test 2: Template function with attributes and noexcept
template<typename T>
    [[nodiscard]]
    constexpr T multiply(T a, T b) noexcept
    { return a * b; }

// Test 3: Template operator with noexcept
template<typename T>
int operator+(T a, int b) noexcept
{ return (int)a + b; }

// Test 4: Template operator with const noexcept (member-style signature)
template<typename T>
T getValue(const T* ptr) noexcept
{ return *ptr; }

// Test 5: Compound assignment operator
template<typename T>
T& operator+=(T& a, const T& b) noexcept
{ a = a + b; return a; }

int main() {
    // Test basic template function
    int sum = add(1, 2);
    int product = multiply(3, 4);
    
    // Test template operator
    int val = 5 + 10;
    
    return sum + product;
}
