// Regression test: brace-init constructor matching must not hard-fail when a
// rebuilt dependent binary operator argument lacks an exact sema slot.

struct Number {
	int value;
};

enum Offset {
	AddThree = 3
};

Number operator+(const Number& lhs, Offset rhs) {
	return Number{lhs.value + static_cast<int>(rhs)};
}

template <typename T>
int apply(T rhs) {
	Number lhs{2};
	Number result{lhs + rhs};
	return result.value;
}

int main() {
	return apply(AddThree);
}
