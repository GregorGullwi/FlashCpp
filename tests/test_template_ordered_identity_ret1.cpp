// Test Phase 7: Ordered Template Instantiation Identity
// This test verifies that template instantiation cache/specialization identity
// matches ordered C++ template argument lists instead of split type/value/template grouping.

// Test 1: Mixed ordered arguments - template<typename T, int N, template<typename> class C>
template <typename T, int N, template<typename> class C>
struct MixedArgs {
	static int getValue() {
		return N;
	}
};

// Container to use as template-template argument
template <typename U>
struct Container {
	U value;
};

// Test 2: Instantiate with specific arguments
// This should create a different cache entry than if arguments were reordered
typedef MixedArgs<int, 42, Container> SpecializedMixed;

// Test 3: Exact specialization with mixed type/value args
template <>
struct MixedArgs<double, 100, Container> {
	static int getValue() {
		return 200;
	}
};

// Test 4: Partial specialization with mixed ordered args
// Partial spec with fixed int N=50
template <typename T, template<typename> class C>
struct MixedArgs<T, 50, C> {
	static int getValue() {
		return 350;
	}
};

int main() {
	// Verify exact specialization works
	int val1 = MixedArgs<int, 42, Container>::getValue();
	if (val1 != 42) {
		return 1;  // FAIL: Expected 42
	}

	// Verify template template argument was handled correctly
	int val2 = MixedArgs<double, 100, Container>::getValue();
	if (val2 != 200) {
		return 2;  // FAIL: Expected 200 from exact specialization
	}

	// Verify partial specialization with fixed N=50
	int val3 = MixedArgs<float, 50, Container>::getValue();
	if (val3 != 350) {
		return 3;  // FAIL: Expected 350 from partial specialization
	}

	// Verify that a different N value uses the primary template
	int val4 = MixedArgs<char, 75, Container>::getValue();
	if (val4 != 75) {
		return 4;  // FAIL: Expected 75 from primary template
	}

	return 1;  // SUCCESS (ret1)
}
