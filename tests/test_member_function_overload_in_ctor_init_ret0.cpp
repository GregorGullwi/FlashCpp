// Regression: a class's member constructor *template* uses an unqualified
// call to a *static* member function that has multiple overloads (e.g.
// const/non-const) from within its member-initializer list.  The parser's
// complete-class member-function lookup used to register only the first
// matching overload, so overload resolution would fail with
// "No matching function for call to ..." even though a valid overload
// existed.  This is the shape used by libstdc++'s <tuple>, <list>,
// <functional>, and <memory> headers via `_M_tail(__in)` in
// `_Tuple_impl`'s allocator-extended copy constructor.
//
// This test must return 0.

struct Holder {
	int value;

	// Two overloads differing only in const-qualification of the parameter.
	static constexpr Holder& select(Holder& t) noexcept { return t; }
	static constexpr const Holder& select(const Holder& t) noexcept { return t; }

	Holder() : value(0) {}

	// Member constructor template that exercises complete-class lookup of
	// an overloaded static member function from a member-initializer list.
	template<typename U>
	Holder(U, const Holder& other) : value(select(other).value) {}
};

int main() {
	Holder a;
	a.value = 0;
	Holder b(0, a);
	return b.value;
}
