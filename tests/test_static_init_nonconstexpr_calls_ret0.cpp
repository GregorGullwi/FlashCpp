// Regression: non-constexpr globals may still use the permissive static-storage
// evaluator path for non-constexpr function bodies whose definitions are available.

int nested_leaf() {
	return 19;
}

int nested_outer() {
	return nested_leaf() + 23;
}

struct Box {
	int value;

	int get() {
		return value;
	}
};

int build_member_value() {
	Box b{17};
	return b.get() + 2;
}

int free_global = nested_outer();
int member_global = build_member_value();

int main() {
	if (free_global != 42)
		return 1;
	if (member_global != 19)
		return 2;
	return 0;
}
