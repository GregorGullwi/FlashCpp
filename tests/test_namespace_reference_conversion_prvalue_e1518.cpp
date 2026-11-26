// A namespace-scope reference cannot store the referent address produced by a
// conversion function at run time. Before this diagnostic the initializer
// materialized a zero-valued temporary, so reads through `r` missed `storage`.
// The runtime form is diagnosed instead; a local reference stays supported.

int storage = 5;

struct A {
	operator int&() {
		return storage;
	}
};

int& r = A();

int main() {
	r = 7;
	return storage == 7 ? 0 : 1;
}
