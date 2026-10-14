// A derived-to-base pointer cast is only representable at a single pointer
// level, so casting a derived pointer to a pointer-to-pointer-to-base must
// report DerivedToBasePointerCastDepthUnsupported.

struct Base {};

struct Derived : Base {};

int main() {
	Derived value;
	Base** pointer = static_cast<Base**>(&value);
	(void)pointer;
	return 0;
}
