// Simpler test for integral_constant pattern
// Testing just the basic structure without conversion operators initially

template<typename T, T v>
struct integral_constant {
	static constexpr T value = v;
};

// Type aliases for common cases
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

int main() {
	// Test accessing static constexpr value
	constexpr int val = integral_constant<int, 42>::value;
	constexpr bool t = true_type::value;
	constexpr bool f = false_type::value;
	
	int result = 0;
	if (val == 42) result += 10;
	if (t) result += 10;
	if (!f) result += 10;
	
	return result;  // Expected: 30
}
