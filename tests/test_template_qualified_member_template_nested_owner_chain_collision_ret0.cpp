// Regression: explicit qualified member-template calls must preserve
// multi-segment nested owner identity inside the current instantiation.
// A global template owner with the same spelled owner chain must not steal it.

template <typename T>
struct Bridge {
	struct Ops {
		template <typename U>
		static int read(int& value) {
			value += 100;
			return value + static_cast<int>(sizeof(U));
		}
	};
};

template <typename T>
struct Runner {
	struct Bridge {
		struct Ops {
			template <typename U>
			static int read(int value) {
				return value + 37 + static_cast<int>(sizeof(U));
			}
		};
	};

	int run() {
		int value = 1;
		return Bridge::Ops::template read<int>(value);
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
	if (Bridge<int>::Ops::template read<char>(global_value) != 102) {
		return 3;
	}

	if (global_value != 101) {
		return 4;
	}

	return 0;
}
