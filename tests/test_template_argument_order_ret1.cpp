// Test Phase 7: Different argument orders should produce different cache entries
// This verifies that the ordered identity correctly distinguishes between
// templates instantiated with arguments in different source order.

// Test: Argument order matters for cache identity
// Template with three mixed parameter types
template <typename T1, int N1, typename T2>
struct ThreeParams {
	static int getOrder() {
		// Return a value that encodes the parameter positions
		// This helps verify that the right specialization was selected
		return 1;  // Primary template
	}
};

// Specialization 1: T1=int, N1=10, T2=double
template <>
struct ThreeParams<int, 10, double> {
	static int getOrder() {
		return 111;  // Specialization 1
	}
};

// Specialization 2: T1=double, N1=10, T2=int (reversed type parameters)
template <>
struct ThreeParams<double, 10, int> {
	static int getOrder() {
		return 222;  // Specialization 2 (different order)
	}
};

// Specialization 3: T1=float, N1=20, T2=char (different N1)
template <>
struct ThreeParams<float, 20, char> {
	static int getOrder() {
		return 333;  // Specialization 3 (different N1)
	}
};

int main() {
	// Test that each specialization is correctly distinguished by argument order

	// Should match Specialization 1 (int, 10, double)
	int val1 = ThreeParams<int, 10, double>::getOrder();
	if (val1 != 111) {
		return 1;  // FAIL: Got {0}, expected 111 from first specialization
	}

	// Should match Specialization 2 (double, 10, int) - different order matters!
	int val2 = ThreeParams<double, 10, int>::getOrder();
	if (val2 != 222) {
		return 2;  // FAIL: Got {0}, expected 222 from second specialization
	}

	// Should match Specialization 3 (float, 20, char)
	int val3 = ThreeParams<float, 20, char>::getOrder();
	if (val3 != 333) {
		return 3;  // FAIL: Got {0}, expected 333 from third specialization
	}

	// Should use primary template (no matching specialization)
	int val4 = ThreeParams<long, 30, short>::getOrder();
	if (val4 != 1) {
		return 4;  // FAIL: Got {0}, expected 1 from primary template
	}

	return 1;  // SUCCESS (ret1)
}
