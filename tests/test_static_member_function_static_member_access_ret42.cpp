// A static member function may name a static data member directly: only
// non-static data members require an implicit object. This guards the
// implicit-this rejection against firing on the valid static-member case.

struct StaticAccess {
	static int value;
	static int get() {
		return value;
	}
};

int StaticAccess::value = 42;

int main() {
	return StaticAccess::get();
}
