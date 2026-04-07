// Regression test for template alias function-pointer signature loss.
//
// Bug: In resolve_dependent_member_alias (Parser_Templates_Inst_Deduction.cpp),
// the second copy_indirection_from(ts) at line 782 unconditionally overwrites
// the function_signature that was just copied from the alias's type specifier
// on lines 777-780.  When the use-site TypeSpecifierNode (ts) is an unresolved
// UserDefined/TypeAlias placeholder it typically carries no function_signature,
// so the alias's signature is silently destroyed.
//
// This test mixes a function-pointer member alias (transform_fn) with a plain
// value-type member alias (value_type) inside the same template struct, then
// uses both as parameter types in a template function.  If the function_signature
// is lost, the compiler will either fail to compile, mismangle the instantiation,
// or generate an incorrect call through the function pointer.

template<typename T>
struct Traits {
	using value_type = T;
	using transform_fn = T (*)(T);
};

template<typename T>
T apply_transform(typename Traits<T>::transform_fn fn, typename Traits<T>::value_type val) {
	return fn(val);
}

int double_it(int x) { return x * 2; }

int main() {
	int result = apply_transform<int>(&double_it, 21);
	if (result != 42)
		return 1;
	return 0;
}
