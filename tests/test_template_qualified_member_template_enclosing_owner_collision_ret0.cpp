// Regression: explicit qualified member-template calls inside a nested class
// must still resolve owners declared on an enclosing current-instantiation
// class, rather than falling through to an unrelated global template owner.

template <typename T>
struct Ops {
	template <typename U>
	static constexpr int read(int value) {
		return value + 100 + static_cast<int>(sizeof(U));
	}
};

template <typename T>
struct Runner {
	struct Ops {
		template <typename U>
		static constexpr int read(int value) {
			return value + 37 + static_cast<int>(sizeof(U));
		}
	};

	struct Bridge {
		static int run() {
			int value = 1;
			return Ops::template read<int>(value);
		}
	};
};

int main() {
	if (Runner<int>::Bridge::run() != 42) {
		return 1;
	}

	if (Runner<char>::Bridge::run() != 42) {
		return 2;
	}

	int global_value = 1;
	if (Ops<int>::template read<char>(global_value) != 102) {
		return 3;
	}

	return 0;
}
