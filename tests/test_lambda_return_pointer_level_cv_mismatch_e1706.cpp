// A lambda deduces one return type from every return operand. `int* const*`
// and `int**` are different types because the per-level pointer cv differs, so
// the second return must be rejected. The flat compatibility compare used to
// count pointer levels only and accepted this code.

int pick(int flag, int* const* left, int** right) {
	auto select = [=]() {
		if (flag) {
			return left;
		}
		return right;
	};
	return **select();
}

int main() {
	int value = 7;
	int* inner = &value;
	int* const* left = &inner;
	int** right = &inner;
	return pick(1, left, right) - 7;
}
