// Phase 5 regression: constexpr identifier evaluation should respect
// parser-bound static members before falling back to unqualified lookup.

constexpr int value = 5;

struct Box {
	static constexpr int value = 42;
	static constexpr int selected = value;
	static_assert(selected == 42, "selected should use Box::value");
};

int main() {
	return 42;
}