// Test nested if constexpr chains with sizeof... evaluation
// Verifies that multi-level else-if constexpr chains are correctly
// eliminated at compile time during template body re-parsing.

template<typename... Args>
int classify(Args... args) {
	if constexpr (sizeof...(args) == 0) {
		return 0;
	} else if constexpr (sizeof...(args) == 1) {
		return 1;
	} else if constexpr (sizeof...(args) == 2) {
		return 2;
	} else {
		return 3;
	}
}

int main() {
	int result = 0;

	// sizeof...(args) == 0 → branch 0
	result += classify();        // 0

	// sizeof...(args) == 1 → branch 1
	result += classify(42);      // 1

	// sizeof...(args) == 2 → branch 2
	result += classify(1, 2);    // 2

	// sizeof...(args) == 3 → branch 3 (else)
	result += classify(1, 2, 3); // 3

	// sizeof...(args) == 4 → branch 3 (else)
	result += classify(1, 2, 3, 4); // 3

	// Another single → branch 1
	result += classify(99);      // 1

	// 0 + 1 + 2 + 3 + 3 + 1 = 10
	return result;
}
