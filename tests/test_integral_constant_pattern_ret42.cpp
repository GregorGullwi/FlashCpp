// Test for std::integral_constant pattern
// This is a fundamental building block for type traits in C++

// Simplified version of std::integral_constant
template<typename T, T v>
struct integral_constant {
	static constexpr T value = v;
	
	using value_type = T;
	using type = integral_constant;
	
	// Conversion operator - allows implicit conversion to T
	constexpr operator value_type() const noexcept { 
		return value; 
	}
	
	// Call operator - C++14 feature
	constexpr value_type operator()() const noexcept { 
		return value; 
	}
};

// Type aliases for common cases
template<bool B>
using bool_constant = integral_constant<bool, B>;

using true_type = bool_constant<true>;
using false_type = bool_constant<false>;

// Simple type trait using integral_constant
template<typename T, typename U>
struct is_same : false_type {};

template<typename T>
struct is_same<T, T> : true_type {};

// Test the pattern
int main() {
	// Test integral_constant with int
	integral_constant<int, 42> ic;
	int value1 = ic;  // Should use conversion operator
	int value2 = ic();  // Should use call operator
	
	// Test bool_constant
	true_type t;
	false_type f;
	
	bool b1 = t;  // Should be true
	bool b2 = f;  // Should be false
	
	// Test is_same
	bool same1 = is_same<int, int>::value;  // Should be true
	bool same2 = is_same<int, double>::value;  // Should be false
	
	// Calculate result
	int result = 0;
	if (value1 == 42) result += 10;  // +10
	if (value2 == 42) result += 10;  // +10
	if (b1) result += 10;  // +10
	if (!b2) result += 10;  // +10
	if (same1) result += 1;  // +1
	if (!same2) result += 1;  // +1
	
	return result;  // Expected: 42
}
