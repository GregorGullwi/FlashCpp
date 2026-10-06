// [dcl.type.decltype]: dereferencing a pointer yields an lvalue of the pointee
// type, so `decltype(*p)` is `T&` even without parentheses, and the same value
// category must reach overload ranking. This mixes native scalar and record
// pointees, pointer depth, and pointer-to-array.

struct Record {
	int value;
};

// Precise reference-kind discriminators. `__is_same` cannot separate two
// reference types, so a partial specialization decides the kind directly.
template <class T> struct IsLValueReference { static constexpr bool value = false; };
template <class T> struct IsLValueReference<T&> { static constexpr bool value = true; };
template <class T> struct IsRValueReference { static constexpr bool value = false; };
template <class T> struct IsRValueReference<T&&> { static constexpr bool value = true; };

int choose(int&) { return 1; }
int choose(int&&) { return 2; }

int main() {
	int value = 0;
	int* pointer = &value;
	int** pointer_pointer = &pointer;
	Record record{0};
	Record* record_pointer = &record;

	static_assert(IsLValueReference<decltype(*pointer)>::value,
		"a scalar dereference is an lvalue reference");
	static_assert(!IsRValueReference<decltype(*pointer)>::value,
		"a scalar dereference is not an rvalue reference");
	static_assert(IsLValueReference<decltype(*pointer_pointer)>::value,
		"a nested dereference is an lvalue reference");
	static_assert(IsLValueReference<decltype(*record_pointer)>::value,
		"a record dereference is an lvalue reference");
	static_assert(IsLValueReference<decltype((*pointer))>::value,
		"a parenthesized dereference keeps the lvalue reference");

	if (choose(*pointer) != 1)
		return 1;
	*pointer = 7;
	return value == 7 ? 0 : 1;
}
