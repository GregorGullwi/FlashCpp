// An explicit conversion function is not viable for a copy-initialization
// ([class.conv.fct]/2).
struct S {
	explicit operator int() const { return 7; }
};

int main() { int x = S{}; return x; }
