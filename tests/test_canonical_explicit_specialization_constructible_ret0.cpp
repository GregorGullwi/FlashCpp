// An explicit (full) class-template specialization is a distinct class whose
// constructors are its own, and it now shares the primary template's canonical
// TemplateSpecialization identity, so its constructor schema is the authority for
// the constructibility traits below. Return 0 only when every answer follows the
// specialization, not the primary.
template <class T>
struct Wrap {
	Wrap(int) noexcept(false) {}
	int tag = 0;
};

template <>
struct Wrap<int> {
	Wrap(int) noexcept(true) {}
	int tag = 0;
};

template <>
struct Wrap<char> {
	Wrap(long) noexcept(true) {}
	int tag = 0;
};

template <>
struct Wrap<short> {
	short tag = 0;
};

int main() {
	int r = 0;
	// Exact match against the specialization's nothrow constructor.
	r |= __is_constructible(Wrap<int>, int) ? 0 : 1;
	r |= __is_nothrow_constructible(Wrap<int>, int) ? 0 : 2;
	// The primary's matching constructor throws, so its instantiation is not nothrow.
	r |= !__is_nothrow_constructible(Wrap<double>, int) ? 0 : 4;
	// Converting arguments still resolve through the specialization's constructor.
	r |= __is_constructible(Wrap<char>, int) ? 0 : 8;
	r |= __is_constructible(Wrap<char>, double) ? 0 : 16;
	// A specialization with no user constructor is default constructible.
	r |= __is_constructible(Wrap<short>) ? 0 : 32;
	return r;
}
