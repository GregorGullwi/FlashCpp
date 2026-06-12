// Regression: a definition-bound current-member static call inside a class
// template must preserve its selected member target through replay. The hidden
// base overload must not re-enter the candidate set later and steal the call.

template <typename T>
struct Base {
	static int select() {
		return 7;
	}
};

template <typename T>
struct Derived : Base<T> {
	static int select(int value = 0) {
		return value + static_cast<int>(sizeof(T)) + 38;
	}

	static int run() {
		return select();
	}
};

int main() {
	if (Derived<int>::run() != 42) {
		return 1;
	}
	if (Derived<char>::run() != 39) {
		return 2;
	}
	return 0;
}
