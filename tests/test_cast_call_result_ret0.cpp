// Test: postfix () and [] after C++ cast expressions (parsing)
// Verifies: static_cast<T>(x)(args...) and static_cast<T*>(p)[i] patterns
// These patterns appear in MSVC <type_traits> at line 1576:
//   decltype(static_cast<C&&>(obj)(static_cast<Ts&&>(args)...))

// Test decltype with cast-then-member-call pattern (the MSVC header blocker)
template<typename T>
struct Wrapper {
	T val;
	T get() { return val; }
};

// This pattern triggers apply_postfix_operators with '.' after a cast
template<typename T>
using wrap_get_t = decltype(static_cast<Wrapper<T>&&>(*(Wrapper<T>*)nullptr).get());

// Test parsing of cast-then-subscript in unevaluated context
template<typename T>
using ptr_deref_t = decltype(static_cast<T*>(nullptr)[0]);

int main() {
	// The templates above verify parsing; if we get here, parsing succeeded
	Wrapper<int> w{42};
	wrap_get_t<int> r = w.get();
	if (r != 42) return 1;
	return 0;
}
