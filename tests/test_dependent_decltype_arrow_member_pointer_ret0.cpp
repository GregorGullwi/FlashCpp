// Edge case: decltype with ->* (arrow pointer-to-member dereference) where
// the pointer operand has a dependent type.
// The token scanner must identify 'ptr' as a symbol whose declared type (C*)
// is an incomplete instantiation of a template-parameter-dependent type, and
// therefore defer the decltype instead of hard-failing.
//
// Also exercises the parser fix for trailing return types on static member
// function declarations (no body) inside a struct template.

template <class C, class M>
struct ArrowInvoker {
	// 'ptr' has type C* — C is an active template parameter.
	// The scanner looks up 'ptr', finds its type spec (C*) has
	// is_incomplete_instantiation_=true, and defers the decltype.
	static auto call(M C::* member, C* ptr) -> decltype(ptr->*member);
};

struct Node {
	int data;
};

// Resolve the type alias through an unevaluated decltype call so we exercise
// the full dependency-detection path without executing ->* at runtime
// (pre-existing pointer-to-member codegen limitation).
using NodeIntResult = decltype(ArrowInvoker<Node, int>::call(nullptr, nullptr));

int main() {
	NodeIntResult* unused = nullptr;
	return unused == nullptr ? 0 : 1;
}
