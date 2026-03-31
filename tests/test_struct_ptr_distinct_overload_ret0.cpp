// Test: Foo* and Bar* are distinct overloads — Foo* must not match Bar* overload.
// Both resolve to Type::Struct, so the type_index must be consulted to tell them apart.
struct Foo {
	int x;
};
struct Bar {
	int y;
};

int dispatch(Foo*) { return 0; }
int dispatch(Bar*) { return 1; }

int main() {
	Foo f{};
	Bar b{};
	if (dispatch(&f) != 0)
		return 1;
	if (dispatch(&b) != 1)
		return 1;
	return 0;
}
