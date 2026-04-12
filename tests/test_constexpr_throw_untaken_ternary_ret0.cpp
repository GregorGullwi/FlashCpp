// Test: constexpr throw expressions are permitted in untaken branches.

constexpr int top_level_value = true ? 42 : throw 1;
static_assert(top_level_value == 42);

constexpr int choose_local_branch() {
	int cond = 1;
	return cond ? 17 : throw 2;
}

static_assert(choose_local_branch() == 17);

int main() {
	return (top_level_value == 42 && choose_local_branch() == 17) ? 0 : 1;
}
