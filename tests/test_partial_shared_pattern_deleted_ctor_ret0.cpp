// Distinct class-template partial specializations can synthesize the same
// internal pattern name because the name only records argument modifiers. Each
// pattern must publish its own deleted constructor schema, otherwise a deleted
// exact match is lost or leaks into the other partial's instantiation.

template <class T, class U> struct SharedPattern {
	SharedPattern(int);
};

template <class T> struct SharedPattern<T*, int> {
	SharedPattern(int) = delete;
	SharedPattern(char);
};

template <class T> struct SharedPattern<T*, char> {
	SharedPattern(int);
};

static_assert(!__is_constructible(SharedPattern<int*, int>, int));
static_assert(__is_constructible(SharedPattern<int*, int>, char));
static_assert(__is_constructible(SharedPattern<int*, char>, int));

int main() {
	return 0;
}
