// A partial specialization whose pattern base is a bare deduced type parameter
// and whose own cv or array shape is absent absorbs those from the concrete
// argument: `T&` matches `const int&` (T = const int) and `int(&)[3]`
// (T = int[3]), while `T[3]` keeps its bound structural. This pins the
// reference, cv, and array-reference deductions that the pattern matcher
// previously rejected for a deduced base.

template <class T> struct Kind { static constexpr int value = 0; };
template <class T> struct Kind<T&> { static constexpr int value = 1; };
template <class T> struct Kind<T&&> { static constexpr int value = 2; };
template <class T> struct Kind<T*> { static constexpr int value = 3; };
template <class T> struct Kind<T[3]> { static constexpr int value = 4; };

int main() {
	static_assert(Kind<int&>::value == 1, "int& matches T&");
	static_assert(Kind<const int&>::value == 1, "const int& matches T&");
	static_assert(Kind<volatile int&>::value == 1, "volatile int& matches T&");
	static_assert(Kind<int(&)[3]>::value == 1, "int(&)[3] matches T&");
	static_assert(Kind<const int(&)[3]>::value == 1, "const int(&)[3] matches T&");
	static_assert(Kind<int&&>::value == 2, "int&& matches T&&");
	static_assert(Kind<int*>::value == 3, "int* matches T*");
	static_assert(Kind<int[3]>::value == 4, "int[3] matches T[3]");
	static_assert(Kind<int>::value == 0, "int uses the primary");
	return 0;
}
