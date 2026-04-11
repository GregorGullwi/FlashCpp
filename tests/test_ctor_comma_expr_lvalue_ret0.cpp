// Regression: the built-in comma operator preserves the RHS value category, so
// sema-backed constructor overload resolution must keep lvalue RHS operands as
// lvalues.
struct Sink {
	int selected;

	Sink(int&)
		: selected(1) {}

	Sink(int&&)
		: selected(2) {}
};

int main() {
	int value = 42;
	Sink sink((0, value));
	return sink.selected == 1 ? 0 : 1;
}
