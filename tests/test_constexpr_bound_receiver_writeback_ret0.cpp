// Tests that mutating member functions called on a ternary-selected receiver
// correctly write back the modified state to the chosen local variable.

struct Box {
	int a;
	int b;
	constexpr void add(int v) { a += v; }
	constexpr int sum() const { return a + b; }
};

// Ternary selects x: x.a should become 6
constexpr int mutate_first() {
	Box x{5, 0};
	Box y{10, 0};
	(true ? x : y).add(1);
	return x.a;
}
static_assert(mutate_first() == 6);

// Ternary selects y: y.a should become 11
constexpr int mutate_second() {
	Box x{5, 0};
	Box y{10, 0};
	(false ? x : y).add(1);
	return y.a;
}
static_assert(mutate_second() == 11);

// Runtime-parametric: the selected variable is mutated
constexpr int mutate_selected(bool pick_first) {
	Box x{1, 0};
	Box y{2, 0};
	(pick_first ? x : y).add(10);
	return pick_first ? x.a : y.a;
}
static_assert(mutate_selected(true)  == 11);
static_assert(mutate_selected(false) == 12);

// Nested ternary: selects among three variables
constexpr int mutate_nested(bool pick1, bool pick2) {
	Box a{1, 0};
	Box b{2, 0};
	Box c{3, 0};
	(pick1 ? (pick2 ? a : b) : c).add(100);
	return pick1 ? (pick2 ? a.a : b.a) : c.a;
}
static_assert(mutate_nested(true,  true)  == 101);
static_assert(mutate_nested(true,  false) == 102);
static_assert(mutate_nested(false, true)  == 103);

int main() {
	return mutate_first() == 6 &&
		   mutate_second() == 11 &&
		   mutate_selected(true) == 11 &&
		   mutate_selected(false) == 12
		   ? 0 : 1;
}
