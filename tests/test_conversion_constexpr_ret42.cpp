// Very simple test for conversion operator with constexpr
// Testing conversion operator returning a literal value

struct IntWrapper {
	constexpr operator int() const noexcept { 
		return 42; 
	}
};

int main() {
	IntWrapper w;
	int value = w;  // Should use conversion operator
	return value;  // Expected: 42
}
