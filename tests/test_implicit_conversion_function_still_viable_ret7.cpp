// A non-explicit conversion function remains viable for an implicit conversion;
// only explicit ones are excluded.
struct S {
	operator int() const { return 7; }
};

int take(int value) { return value; }

int main() { return take(S{}); }
