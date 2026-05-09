// Test Phase 7: Mixed type and value template arguments
// This test verifies that value arguments (NTTP) are correctly distinguished
// from type arguments when building ordered instantiation identities.

// Test: NTTP with various types and values
template <typename T, int N, bool Flag>
struct MixedNTTP {
	static T getDefault() {
		return static_cast<T>(N);
	}
};

// Explicit specialization: int type, value 5, flag true
template <>
struct MixedNTTP<int, 5, true> {
	static int getDefault() {
		return 1000;
	}
};

// Explicit specialization: int type, value 5, flag false (different bool)
template <>
struct MixedNTTP<int, 5, false> {
	static int getDefault() {
		return 2000;
	}
};

// Explicit specialization: int type, value 10, flag true (different NTTP)
template <>
struct MixedNTTP<int, 10, true> {
	static int getDefault() {
		return 3000;
	}
};

// Explicit specialization: double type, value 5, flag true (different type)
template <>
struct MixedNTTP<double, 5, true> {
	static double getDefault() {
		return 4000.0;
	}
};

int main() {
	// Verify exact specializations based on complete argument tuple

	// int, 5, true
	int val1 = MixedNTTP<int, 5, true>::getDefault();
	if (val1 != 1000) {
		return 1;  // FAIL: Expected 1000
	}

	// int, 5, false - different bool value
	int val2 = MixedNTTP<int, 5, false>::getDefault();
	if (val2 != 2000) {
		return 2;  // FAIL: Expected 2000
	}

	// int, 10, true - different NTTP value
	int val3 = MixedNTTP<int, 10, true>::getDefault();
	if (val3 != 3000) {
		return 3;  // FAIL: Expected 3000
	}

	// double, 5, true - different type
	double val4 = MixedNTTP<double, 5, true>::getDefault();
	if (val4 != 4000.0) {
		return 4;  // FAIL: Expected 4000.0
	}

	// Verify primary template is used when no specialization matches
	// char, 5, true should use primary template
	char val5 = MixedNTTP<char, 5, true>::getDefault();
	if (val5 != 5) {
		return 5;  // FAIL: Expected primary template result (5)
	}

	// Verify primary template with different N
	// int, 99, false should use primary template
	int val6 = MixedNTTP<int, 99, false>::getDefault();
	if (val6 != 99) {
		return 6;  // FAIL: Expected primary template result (99)
	}

	return 1;  // SUCCESS (ret1)
}
