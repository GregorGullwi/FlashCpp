struct Pad {
	char byte;
};

struct Base {
	static int copies;
	long long value;
	Base() : value(0) {}
	Base(const Base& other) : value(other.value) { ++copies; }
};

int Base::copies = 0;

struct Derived : Pad, Base {
	short tag;
};

struct Source {
	Derived derived;
	operator Derived() const { return derived; }
};

struct ReferenceSource {
	Derived derived;
	operator const Derived&() const { return derived; }
};

long long read(Base object) {
	return object.value;
}

long long read_reference(const Base& object) {
	return object.value;
}

int main() {
	Source source;
	source.derived.value = 42;
	source.derived.tag = 7;
	if (read(source) != 42)
		return 1;
	if (read_reference(source) != 42)
		return 2;
	if (source.derived.tag != 7)
		return 3;

	ReferenceSource reference_source;
	reference_source.derived.value = 42;
	Base::copies = 0;
	if (read(reference_source) != 42)
		return 4;
	if (Base::copies == 0)
		return 5;
	return 0;
}
