// Regression test: codegen fallback for a rebuilt dependent binary operator must
// propagate enum rhs_type_index into free-function overload resolution.

struct Number {
	int value;
};

enum Offset {
	AddThree = 3
};

Number operator+(const Number& lhs, Offset rhs) {
	return Number{lhs.value + static_cast<int>(rhs)};
}

template<typename T>
int apply(T rhs) {
	Number lhs{2};
	Number result = lhs + rhs;
	return result.value;
}

int main() {
	return apply(AddThree);
}