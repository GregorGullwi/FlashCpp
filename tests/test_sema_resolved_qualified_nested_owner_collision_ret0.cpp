// Regression: sema's qualified call-target recovery should resolve the owner
// through the current member context before considering broader name matches.
// A global template with the same name must not steal nested-owner recovery.

template <typename T>
struct Ops {
	static int read(int& value) {
		value += 100;
		return value;
	}
};

template <typename T>
struct Runner {
	struct Ops {
		static int read(int value) {
			return value + static_cast<int>(sizeof(T)) + 40;
		}
	};

	int run() {
		int value = 1;
		return Ops::read(value);
	}
};

int main() {
	Runner<int> int_runner;
	if (int_runner.run() != 45) {
		return 1;
	}

	Runner<char> char_runner;
	if (char_runner.run() != 42) {
		return 2;
	}

	int global_value = 1;
	if (Ops<int>::read(global_value) != 101) {
		return 3;
	}

	return 0;
}
