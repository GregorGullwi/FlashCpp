// Phase 5 regression: constexpr member-function calls should respect
// parser-bound member objects before falling back to unqualified lookup.

struct Box {
	int value;
	constexpr Box(int v) : value(v) {}
	constexpr int get() const { return value; }
};

constexpr Box box(5);

struct Outer {
	static constexpr Box box{42};
	static constexpr int selected = box.get();
	static_assert(selected == 42, "box.get() should use Outer::box");
};

int main() {
	return 42;
}
