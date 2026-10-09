// An explicit conversion function is not viable for an implicit conversion
// ([class.conv.fct]/2), so passing the object to a non-explicit parameter must
// not select it.
struct S {
	explicit operator int() const { return 7; }
};

int take(int);

int main() { return take(S{}); }
