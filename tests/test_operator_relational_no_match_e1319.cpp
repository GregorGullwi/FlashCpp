// A relational operator with no viable overload reaches the rewritten-spaceship
// path in codegen. That path must report OperatorOverloadNotFound like the
// arithmetic no-match path instead of an unlocated string.

struct NoLess {
	int value;
};

int main() {
	NoLess lhs{1};
	NoLess rhs{2};
	return lhs < rhs ? 1 : 0;
}
