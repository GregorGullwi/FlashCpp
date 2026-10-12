// Reject constant evaluation before the enclosing class definition is complete.
// Accessing a static result member still evaluates the helper() call.
struct Result {
	static constexpr int value = 42;
};

struct Outer {
	static constexpr Result helper() {
		return {};
	}

	static constexpr int selected = helper().value;
};
