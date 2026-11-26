// A conversion function that returns a value still needs a runtime temporary
// for a namespace-scope const reference. The constant path previously accepted
// the empty source object and stored a zero-valued temporary, so the runtime
// form is diagnosed instead.

int storage = 7;

struct A {
	operator int() {
		return storage;
	}
};

const int& r = A();

int main() {
	return r == 7 ? 0 : 1;
}
