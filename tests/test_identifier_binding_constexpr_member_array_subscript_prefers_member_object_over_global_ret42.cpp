// Phase 5 regression coverage: constexpr member-array subscripts should use
// the intended member object in same-name global/member scenarios.

struct Holder {
	int data[2];
};

constexpr Holder box = {{5, 6}};

struct Outer {
	static constexpr Holder box = {{41, 42}};
	static constexpr int selected = box.data[1];
	static_assert(selected == 42, "box.data[1] should use Outer::box");
};

int main() {
	return 42;
}
