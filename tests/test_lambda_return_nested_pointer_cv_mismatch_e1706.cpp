// The first pointer level's cv is part of the pointee type. A lambda cannot
// return both `const int**` and `int**`: the nested pointee `const int` differs
// from `int`, so the deduced return type is inconsistent. The flat compare used
// to accept any pair with the same pointer-level count.

int pick(int flag, const int** left, int** right) {
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
	const int** left = const_cast<const int**>(&inner);
	int** right = &inner;
	return pick(1, left, right) - 7;
}
