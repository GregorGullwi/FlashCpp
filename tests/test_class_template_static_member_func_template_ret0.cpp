// Regression test for Phase 6 known-issue:
// Static member function templates on class templates were not instantiated
// when called with `S<T1,T2>::f(args...)` because two bugs combined:
//
// 1. `parse_template_function_declaration_body` parsed `static` via
//    `parse_declaration_specifiers()` but only propagated constexpr/eval/init —
//    the `StorageClass::Static` was silently dropped, so the template pattern's
//    `is_static()` was false.  As a result `copy_function_properties` never set
//    `is_static` on the instantiated function, causing codegen to emit an
//    implicit `this` register save and shifting all real parameters by one.
//
// 2. `try_parse_member_template_function_call` never called
//    `try_instantiate_member_function_template` (argument-deduction path) —
//    it only tried `try_instantiate_member_function_template_explicit` and the
//    lazy instantiation fallback.  When args were present and no explicit
//    template args were given, neither path was taken for templates.
//
// Both fixes live in:
//   src/Parser_Templates_Function.cpp (propagate storage_class=Static)
//   src/Parser_Expr_QualLookup.cpp    (deduction path for static calls)

template<typename... Ts>
struct MultiStore {
	template<typename... Us>
	static int sum_inner(Us... args) {
		return (0 + ... + args);
	}

	template<typename U>
	static U identity(U v) { return v; }
};

template<typename T>
struct Box {
	template<typename... Args>
	static T make_sum(Args... args) { return (T{} + ... + T(args)); }
};

// Static method on a non-variadic outer class template
template<typename T>
struct Holder {
	T value;
	template<typename V>
	static V scale(V v, T factor) { return V(v * factor); }
};

int main() {
	// Variadic class template, variadic inner static function template
	int a = MultiStore<int, double>::sum_inner(14, 14, 14);  // 42

	// Variadic class template, non-variadic inner static function template
	int b = MultiStore<char>::identity(99);  // 99

	// Non-variadic class template, variadic inner static function template
	int c = Box<int>::make_sum(10, 20, 12);  // 42

	// Non-variadic class template, non-variadic static method with multiple params
	int d = Holder<int>::scale(3, 4);  // 12

	return (a - 42) + (b - 99) + (c - 42) + (d - 12);
}
