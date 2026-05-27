// Regression: parenthesized static member function names remain callable.
struct LimitsLike {
	static int max() {
		return 7;
	}

	static int lowest() {
		return (max)();
	}
};

int main() {
	return LimitsLike::lowest() - 7;
}
