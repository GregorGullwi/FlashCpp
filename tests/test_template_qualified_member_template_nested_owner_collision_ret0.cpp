// Regression: explicit qualified member-template calls inside the current
// instantiation must preserve the full nested owner identity. A global
// template owner with the same simple nested name must not steal the call.

template <typename T>
struct Ops {
	template <typename U>
	static int read(int& value) {
		value += 100;
		return value + static_cast<int>(sizeof(U));
	}
};

template <typename T>
struct Runner {
	struct Ops {
		template <typename U>
		static int read(int value) {
			return value + 37 + static_cast<int>(sizeof(U));
		}
	};

	int run() {
		int value = 1;
		return Ops::template read<int>(value);
	}
};

int main() {
	Runner<int> int_runner;
	if (int_runner.run() != 42) {
		return 1;
	}

	Runner<char> char_runner;
	if (char_runner.run() != 42) {
		return 2;
	}

	int global_value = 1;
	if (Ops<int>::template read<char>(global_value) != 102) {
		return 3;
	}

	if (global_value != 101) {
		return 4;
	}

	return 0;
}
