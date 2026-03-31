// Regression test: function pointer as a direct parameter of a lazily
// instantiated member function.  The non-pack substitution path in
// instantiateLazyMemberFunction must propagate function_signature from the
// resolved TemplateTypeArg (not from the original unsubstituted
// param_type_spec, which is just "F" / UserDefined and has no signature).
//
// This exercises the gap described in the PR review flag:
//   "Inconsistent function_signature source in non-pack vs pack parameter
//    substitution" (Parser_Templates_Lazy.cpp, non-pack path).

template <typename F>
struct Invoker {
	// F appears as a direct parameter — not a stored member.
	int apply(F fn, int arg);
};

template <typename F>
int Invoker<F>::apply(F fn, int arg) {
	return fn(arg);
}

int double_it(int x) {
	return x * 2;
}

int main() {
	Invoker<int (*)(int)> inv;
	return inv.apply(double_it, 21) == 42 ? 0 : 1;
}
