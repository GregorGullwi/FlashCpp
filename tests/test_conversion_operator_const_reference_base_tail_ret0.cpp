// A conversion operator returning Derived& followed by a derived-to-base tail
// must bind a const Base& to the converted lvalue subobject. The base sits at a
// nonzero offset so the binding consumes the adjusted address rather than
// materializing a temporary from the converted object value.

struct Pad {
	char byte;
};

struct Base {
	int value;
	long long wide;
};

struct Derived : Pad, Base {
	short tag;
};

struct Source {
	Derived derived;
	operator Derived&() { return derived; }
};

int take_value(const Base& value) {
	return value.value;
}

long long take_wide(const Base& value) {
	return value.wide;
}

int main() {
	Source source;
	source.derived.value = 41;
	source.derived.wide = 91;
	source.derived.tag = 3;

	const Base& reference = source;
	if (reference.value != 41)
		return 1;
	if (reference.wide != 91)
		return 2;
	if (take_value(source) != 41)
		return 3;
	if (take_wide(source) != 91)
		return 4;
	if (source.derived.tag != 3)
		return 5;
	return 0;
}
