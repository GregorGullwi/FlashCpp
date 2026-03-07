// Phase 5 regression: constexpr array subscript should respect parser-bound
// static member arrays before falling back to unqualified lookup.

constexpr int values[2] = {5, 6};

struct Outer {
	static constexpr int values[2] = {41, 42};
	static constexpr int selected = values[1];
	static_assert(selected == 42, "values[1] should use Outer::values");
};

int main() {
	return 42;
}
