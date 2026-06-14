// Regression: constexpr qualified direct calls inside the current instantiation
// must resolve the nested type owner semantically instead of rebinding through
// an unrelated global template owner with the same simple name.

template <typename T>
struct Ops {
	static constexpr int read(int value) {
		return value + 100;
	}
};

template <typename T>
struct Runner {
	struct Ops {
		static constexpr int read(int value) {
			return value + 41;
		}
	};

	static constexpr int value = Ops::read(1);
};

char verify_runner_int[(Runner<int>::value == 42) ? 1 : -1];
char verify_runner_char[(Runner<char>::value == 42) ? 1 : -1];
char verify_global_ops[(Ops<int>::read(1) == 101) ? 1 : -1];

int main() {
	return sizeof(verify_runner_int) +
		sizeof(verify_runner_char) +
		sizeof(verify_global_ops) - 3;
}
