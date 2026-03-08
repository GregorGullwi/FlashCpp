// Phase 5 regression: constexpr array-subscript member access should respect
// the current struct's static member array over a same-named global array.

struct Item {
	int value;
	constexpr Item(int v) : value(v) {}
};

constexpr Item items[2] = {Item(5), Item(6)};

struct Outer {
	static constexpr Item items[2] = {Item(41), Item(42)};
	static constexpr int selected = items[1].value;
	static_assert(selected == 42, "items[1].value should use Outer::items");
};

int main() {
	return 42;
}
