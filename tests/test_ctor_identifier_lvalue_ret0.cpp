// Test: constructor overload resolution must preserve identifier arguments as
// lvalues through the sema-owned overload-resolution argument-type path.
struct Sink {
	int selected;

	Sink(const int&)
		: selected(1) {}

	Sink(int&&)
		: selected(2) {}
};

int main() {
	int value = 42;
	Sink sink(value);
	return sink.selected == 1 ? 0 : 1;
}
