// A lambda returning `int*` on one path and `int* const` on another returns one
// type: top-level cv does not distinguish the operands. `int* const` is a
// different type from `int*` for identity purposes, but not for the value-type
// compatibility rule. The canonical compare keeps per-level cv and drops only
// top-level cv and references, matching `__is_same` for the identity part.

// The canonical identity query this consumer must agree with.
static_assert(!__is_same(int* const, int*), "top-level pointer cv is identity");
static_assert(!__is_same(int*[3], int*), "an array is not a pointer");

int value = 7;
int* plain = &value;
int* const qualified = &value;

int use_top_level_cv(int flag) {
	auto select = [=]() {
		if (flag) {
			return plain;
		}
		return qualified;
	};
	return *select();
}

int main() {
	if (use_top_level_cv(1) != 7) {
		return 1;
	}
	return 42;
}
