// Regression test: calling a member function on a temporary (brace-init or paren-init)
// should produce correct results. Previously triggered codegen error:
//   "Register call argument marked pass-by-address is not addressable"
// because ConstructorCallNode was not handled in the object-expression dispatch of
// generateMemberFunctionCallIr, and the struct's member function bodies were not
// generated because try_instantiate_class_template's result was discarded.

template<typename... Args>
struct Counter {
	int size() {
		return static_cast<int>(sizeof...(Args));
	}
};

struct Box {
	int value;
	explicit Box(int v) : value(v) {}
	int get() const { return value; }
};

Box makeBox(int v) { return Box(v); }

int main() {
	// Brace-init temporary: Template<T>{}
	if (Counter<int, int, int>{}.size() != 3) return 1;
	if (Counter<int>{}.size() != 1) return 2;
	if (Counter<int, int>{}.size() != 2) return 3;

	// Paren-init temporary: Template<T>()
	if (Counter<int, int, int>().size() != 3) return 4;

	// Function-return temporary
	if (makeBox(42).get() != 42) return 5;

	return 0;
}
