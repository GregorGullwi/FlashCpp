// Phase 5 regression: constexpr function-call member access should respect
// the current struct's static member function over a same-named global function.

struct GlobalResult {
	static constexpr int value = 5;
};

constexpr GlobalResult helper() {
	return {};
}

struct MemberResult {
	static constexpr int value = 42;
};

struct Outer {
	static constexpr MemberResult helper() {
		return {};
	}

	static constexpr int selected = helper().value;
	static_assert(selected == 42, "helper().value should use Outer::helper");
};

int main() {
	return 42;
}
