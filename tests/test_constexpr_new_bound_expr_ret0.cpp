// Test: constexpr new/delete should honor bound local expressions in both
// const-binding and mutable-binding evaluation paths.

constexpr int takeHeapValue(int* p) {
	int v = *p;
	delete p;
	return v;
}

constexpr int new_through_call_arg() {
	int x = 5;
	return takeHeapValue(new int(x));
}
static_assert(new_through_call_arg() == 5);

struct Box {
	int value;
	constexpr Box(int v) : value(v) {}
};

constexpr int new_side_effect_args() {
	int x = 1;
	int* scalar = new int(++x);
	Box* box = new Box(++x);
	int total = x + *scalar + box->value;
	delete scalar;
	delete box;
	return total;
}
static_assert(new_side_effect_args() == 8);

int main() {
	if (new_through_call_arg() != 5) return 1;
	if (new_side_effect_args() != 8) return 2;
	return 0;
}
