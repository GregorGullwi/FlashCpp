// Regression: static data members of a partial-specialization class were not
// classified as ValueLike NTTPs by the explicit-template-argument fast path, so
// expressions of the shape `ns::callee<sizeof(_Tp), required_alignment>()`
// inside an eagerly-parsed member function body were rejected with
// "Missing semicolon(;)".  Mirrors libstdc++ <atomic>'s
// __atomic_ref<_Tp, false, false>::is_lock_free() on line 1523 of bits/atomic_base.h.

namespace ns {
	template<unsigned long _Size, unsigned long _Align>
	bool callee() noexcept;
}

template<typename T, bool = false, bool = false>
struct Probe;

template<typename T>
struct Probe<T, false, false> {
	static constexpr unsigned long required_alignment = alignof(T);

	// The dependent-NTTP fast-path used to reject the templated call inside
	// `decltype(...)` below because `required_alignment` (a static data
	// member of the surrounding partial specialization) was classified as
	// Unknown rather than ValueLike, so the parser tried to interpret it as
	// a type and produced "Missing semicolon(;)".  Using `decltype` keeps
	// the test free of template-call codegen (which would otherwise produce
	// link-time references to the un-instantiated `ns::callee`).
	using ProbeReturn = decltype(ns::callee<sizeof(T), required_alignment>());
};

int main() {
	// Probe is intentionally not instantiated; the bug was a parse-time error
	// in the partial specialization's nested decltype expression, before any
	// instantiation occurs.
	return 0;
}





int main() {
	// The probe() call materializes the dependent-NTTP partial-specialization
	// template `ns::Sum<sizeof(int), required_alignment>` whose `value` is 8.
	// The point of this test is that the call below parses successfully — the
	// fast-path classifier used to reject `required_alignment` as a non-type
	// template argument when the body was reparsed inside the partial
	// specialization's member function context.
	Probe<int> p;
	(void)p;
	return 0;
}



