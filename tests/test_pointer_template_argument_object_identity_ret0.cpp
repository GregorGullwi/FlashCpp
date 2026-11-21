// C++20 [temp.arg.nontype]: a pointer non-type template argument identifies the
// object it points to. Two distinct objects must therefore instantiate different
// specializations, including when the address is written with a namespace
// qualifier that the parser does not resolve before constant evaluation.
namespace scope_a {
inline int first = 1;
inline int second = 2;
}

int global_first = 5;
int global_second = 6;

template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };

template<int* Pointer> struct Tag { int marker; };

int main() {
	int result = 0;
	if (!Same<Tag<&scope_a::first>, Tag<&scope_a::first>>::value) {
		result |= 1;
	}
	if (Same<Tag<&scope_a::first>, Tag<&scope_a::second>>::value) {
		result |= 2;
	}
	if (Same<Tag<&scope_a::second>, Tag<&scope_a::first>>::value) {
		result |= 4;
	}
	if (Same<Tag<&global_first>, Tag<&global_second>>::value) {
		result |= 8;
	}
	if (!Same<Tag<&global_first>, Tag<&global_first>>::value) {
		result |= 16;
	}
	return result;
}
