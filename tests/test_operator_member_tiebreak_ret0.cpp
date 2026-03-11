// Exact member/free ties should still prefer the member operator.

struct A {
	int value;

	A(int v) : value(v) {}

	A operator+(const A& rhs) const {
		return A(value + rhs.value + 1);
	}
};

A operator+(const A& lhs, const A& rhs) {
	return A(lhs.value + rhs.value + 2);
}

int main() {
	A lhs(10);
	A rhs(20);
	A result = lhs + rhs;
	return result.value == 31 ? 0 : 1;
}