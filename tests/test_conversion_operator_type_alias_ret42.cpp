// Test conversion operator with type alias return type
// This tests the fix for operator value_type() where using value_type = T;

template<typename T, T v>
struct integral_constant {
static constexpr T value = v;
using value_type = T;

// Conversion operator using type alias as return type
constexpr operator value_type() const noexcept { 
return value; 
}

// Call operator using type alias as return type
constexpr value_type operator()() const noexcept { 
return value; 
}
};

int main() {
// Test with int
integral_constant<int, 42> ic;

// Test conversion operator (operator value_type() -> operator int())
int value1 = ic;  // Should use conversion operator
if (value1 != 42) return 1;

// Test call operator (operator() -> int)
int value2 = ic();  // Should use call operator
if (value2 != 42) return 2;

return 42;  // Success - return 42
}
