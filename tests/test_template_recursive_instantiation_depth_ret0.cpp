// Regression test for template instantiation depth guard.
// A mutually recursive pair of struct templates that refer to each other
// through member using-aliases would previously cause a stack overflow
// (SIGSEGV) in try_instantiate_class_template when the compiler eagerly
// resolved default template arguments or member alias templates during
// class-body parsing.  The nesting depth guard (MAX_INSTANTIATION_NESTING_DEPTH)
// ensures the compiler returns gracefully instead of exhausting the stack.
//
// Neither A nor B is instantiated here; the test verifies that the compiler
// can parse and codegen a TU that *contains* such mutually-recursive patterns
// without crashing.

namespace detail {
	template<typename T> struct B;

	template<typename T>
	struct A {
		using type = typename B<A<T>>::type;
	};

	template<typename T>
	struct B {
		using type = typename A<B<T>>::type;
	};
} // namespace detail

int main() {
	return 0;
}
