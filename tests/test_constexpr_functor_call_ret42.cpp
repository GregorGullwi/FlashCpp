struct Add {
	constexpr Add(int) {}

	// Place a double overload first to verify we don't blindly pick it
	// when only a single arity match exists (the int overload has different arity).
	constexpr int operator()(double a, double b, double c) const {
		return 0;
	}

	constexpr int operator()(int lhs, int rhs) const {
		return lhs + rhs;
	}
};

constexpr Add add(0);
constinit int result = add(40, 2);

int main() {
	return result;
}
