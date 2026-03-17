// Test: Destructors are implicitly noexcept(true) per C++11 [class.dtor]/3.
// A noexcept destructor that would throw should cause std::terminate,
// but here we verify the flag is set correctly by checking noexcept(T{}.~T())
// returns true for a class with no explicit noexcept specifier.

struct Widget {
	int value;
	~Widget() {}  // implicitly noexcept(true)
};

struct Explicit {
	int value;
	~Explicit() noexcept {}  // explicitly noexcept(true)
};

struct DefaultDtor {
	int value;
	// No user-defined destructor — implicitly noexcept(true)
};

int main() {
	// All three should evaluate noexcept to true
	int result = 0;

	if (!noexcept(Widget{}.~Widget()))
		result |= 1;

	if (!noexcept(Explicit{}.~Explicit()))
		result |= 2;

	if (!noexcept(DefaultDtor{}.~DefaultDtor()))
		result |= 4;

	return result;
}
