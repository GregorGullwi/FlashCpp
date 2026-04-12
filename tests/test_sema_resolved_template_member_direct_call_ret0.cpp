// Test: sema-resolved ordinary direct calls inside instantiated template members
// should not fall back to codegen name-based recovery.

int finalizeValue(int value) {
	return value + 7;
}

template <typename T>
struct Runner {
	int run(T value) {
		return finalizeValue(static_cast<int>(value));
	}
};

int main() {
	Runner<int> int_runner;
	if (int_runner.run(35) != 42)
		return 1;

	Runner<long> long_runner;
	if (long_runner.run(35) != 42)
		return 2;

	return 0;
}
