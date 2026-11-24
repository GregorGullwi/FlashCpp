// C++20 [expr.const.cast]/3 still requires the same pointer components: `int*` and
// `int**` are not similar object-pointer types, so a const_cast between them is a
// type change, not a cv change, and the constant evaluator must reject it.

constexpr int pointerDepthChange() {
	int value = 40;
	int* pointer = &value;
	int** changed = const_cast<int**>(pointer);
	(void)changed;
	return 0;
}

static_assert(pointerDepthChange() == 0);

int main() {
	return 0;
}
