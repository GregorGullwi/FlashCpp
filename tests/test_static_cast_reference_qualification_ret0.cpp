// [expr.static.cast]/5 binds the reference by [dcl.init.ref], so adding cv to
// the referent is always valid, an lvalue may bind an rvalue reference
// ([expr.static.cast]/3), and a const lvalue reference may bind an xvalue.
// Reference aliases collapse before the rule is applied. The constexpr case
// pins that a reference cast which was already valid keeps its value.

constexpr int constexprReferenceAddition() {
	int value = 7;
	const int& reference = static_cast<const int&>(value);
	return reference;
}

static_assert(constexprReferenceAddition() == 7);

int main() {
	int value = 11;
	const int const_value = 13;
	int* pointer = &value;

	const int& const_added = static_cast<const int&>(value);
	const volatile int& const_volatile_added = static_cast<const volatile int&>(value);
	int&& rvalue_reference = static_cast<int&&>(value);
	const int&& const_rvalue_reference = static_cast<const int&&>(value);
	const int& xvalue_bound = static_cast<const int&>(static_cast<int&&>(value));
	int* const& pointer_const_added = static_cast<int* const&>(pointer);

	using LValueAlias = int&;
	using RValueAlias = int&&;
	LValueAlias& collapsed_from_lvalue = static_cast<LValueAlias&>(value);
	LValueAlias&& collapsed_from_rvalue_alias = static_cast<LValueAlias&&>(value);
	RValueAlias& collapsed_rvalue_alias = static_cast<RValueAlias&>(value);

	// [expr.cast]/4: a C-style cast may perform a const_cast-like conversion,
	// so removing the referent const stays accepted for that spelling.
	int& c_style_const_removed = (int&)const_value;

	if (const_added != 11 || const_volatile_added != 11) {
		return 1;
	}
	if (rvalue_reference != 11 || const_rvalue_reference != 11) {
		return 2;
	}
	if (xvalue_bound != 11 || *pointer_const_added != 11) {
		return 3;
	}
	if (collapsed_from_lvalue != 11 || collapsed_from_rvalue_alias != 11 || collapsed_rvalue_alias != 11) {
		return 4;
	}
	c_style_const_removed = 19;
	if (const_value != 19) {
		return 5;
	}
	return 0;
}
