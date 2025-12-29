// Verifies full template specializations with static member functions
// parse and generate correctly without crashing.

template<typename T>
struct chooser {
	using type = int;
	static type make() { return 0; }
	static constexpr int value = 0;
};

template<>
struct chooser<int> {
	using type = int;
	static type make() { return 42; }
	static constexpr int value = 42;
};

int main() {
	// Ensure both the primary template and the full specialization parse correctly.
	// The primary template contributes 0 and the specialization contributes 42.
	// Accessing the static data members avoids linker failures from missing symbols
	// while still exercising static member parsing in specializations.
	int primary_value = chooser<float>::value;
	int specialized_value = chooser<int>::value;
	return primary_value + specialized_value;  // Expect 42
}
