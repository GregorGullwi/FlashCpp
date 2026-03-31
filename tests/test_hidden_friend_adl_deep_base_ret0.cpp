// ADL with deeply nested inheritance: hidden friend defined in the
// grandparent's namespace should be found via ADL when the argument
// type is a grandchild class.
//
// Per C++20 [basic.lookup.argdep]/2, the associated classes of a class
// type include "all of its base classes" — not just direct bases.
// A hidden friend in the grandparent's enclosing namespace must be
// reachable when the argument is of the derived (grandchild) type.
//
// Return value is 0 on success.
namespace lib {
struct Base {
	int value;
	friend int get_value(Base& b) { return b.value; }
};
} // namespace lib

namespace mid {
struct Middle : lib::Base {};
} // namespace mid

struct Derived : mid::Middle {};

int main() {
	Derived d;
	d.value = 10;
 // ADL should walk: Derived -> mid::Middle -> lib::Base
 // and find lib::get_value via the associated namespace "lib".
	return get_value(d) - 10;  // 10 - 10 == 0
}
