// [dcl.array]/1 and [dcl.ref]/1: a reference to an array of unknown bound
// (`int(&)[]`) is a valid type. This checks that it parses as a type-id and a
// template argument, and that a reference to a global or local array aliases
// the array for reads and writes. Previously the type did not parse at all,
// and a reference to a global array bound to the temporary's own slot.

template <class T> struct Kind { static constexpr int value = 0; };
template <class T> struct Kind<T&> { static constexpr int value = 1; };
template <class T> struct Kind<T&&> { static constexpr int value = 2; };

int global_array[5] = {0, 1, 2, 3, 4};

using RefUnbounded = int(&)[];
using RefOuterUnboundedInner = int(&)[][3];

int main() {
	int local_array[5] = {0, 1, 2, 3, 4};

	int (&global_ref)[] = global_array;
	int (&local_ref)[] = local_array;
	int (&bounded_ref)[5] = global_array;

	static_assert(Kind<int(&)[]>::value == 1, "an unbounded array lvalue reference matches T&");
	static_assert(Kind<int(&)[3]>::value == 1, "a bounded array lvalue reference matches T&");

	global_ref[1] = 42;
	local_ref[2] = 24;
	bounded_ref[3] = 7;
	if (global_array[1] != 42 || local_array[2] != 24 || global_array[3] != 7)
		return 1;
	return 0;
}
