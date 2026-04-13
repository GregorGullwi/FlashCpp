// Test: sema-normalized direct calls to pure-expression template instantiations
// should use sema-owned resolved targets and avoid falling back to symbol-table
// lookup in codegen.

template <typename T>
int addTwo(T value) {
	return static_cast<int>(value) + 2;
}

namespace math {
	template <typename T>
	int multiplyBySix(T value) {
		return static_cast<int>(value) * 6;
	}
}

int callAdd(int value) {
	return addTwo<int>(value);
}

int callMultiply(int value) {
	return math::multiplyBySix<int>(value);
}

int main() {
	if (callAdd(40) != 42)
		return 1;
	if (callMultiply(7) != 42)
		return 2;
	return 0;
}
