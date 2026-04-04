struct S {
	int x;

	constexpr S(int value)
		: x(value) {}
};

constexpr int S::* member = &S::x;
constexpr S object(42);
constexpr int result = object.*member;

int main() {
	return result - 42;
}
