// Regression: built-in assignment expressions are lvalues, so sema-backed
// constructor overload resolution must preserve that category.
struct Sink {
	int selected;

	Sink(int&)
		: selected(1) {}

	Sink(int&&)
		: selected(2) {}
};

int main() {
	int lhs = 0;
	int rhs = 7;
	Sink sink((lhs = rhs));
	return sink.selected == 1 ? 0 : 1;
}
