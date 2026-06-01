// Regression: out-of-line constructor-template replay and StructTypeInfo sync
// must not treat dependent member-template alias overloads as shape-equivalent.
// The int and long alias-target overloads must each retain their own body.
struct CtorTemplateDependentMemberTemplateAliasOverloadTag {
	template<class U>
	struct AddPtr {
		using type = U*;
	};
};

template<class T>
struct CtorTemplateDependentMemberTemplateAliasOverload {
	int which = 0;

	template<class U>
	CtorTemplateDependentMemberTemplateAliasOverload(
		U seed,
		typename T::template AddPtr<int>::type value);

	template<class U>
	CtorTemplateDependentMemberTemplateAliasOverload(
		U seed,
		typename T::template AddPtr<long>::type value);
};

template<class T>
template<class U>
CtorTemplateDependentMemberTemplateAliasOverload<T>::
	CtorTemplateDependentMemberTemplateAliasOverload(
		U seed,
		typename T::template AddPtr<long>::type value)
	: which(static_cast<int>(sizeof(*value)) + seed + 10) {}

template<class T>
template<class U>
CtorTemplateDependentMemberTemplateAliasOverload<T>::
	CtorTemplateDependentMemberTemplateAliasOverload(
		U seed,
		typename T::template AddPtr<int>::type value)
	: which(static_cast<int>(sizeof(*value)) + seed + 20) {}

int main() {
	int int_value = 0;
	long long_value = 0;
	CtorTemplateDependentMemberTemplateAliasOverload<
		CtorTemplateDependentMemberTemplateAliasOverloadTag>
		from_int_ptr(1, &int_value);
	if (from_int_ptr.which != 25) {
		return 1;
	}
	CtorTemplateDependentMemberTemplateAliasOverload<
		CtorTemplateDependentMemberTemplateAliasOverloadTag>
		from_long_ptr(2, &long_value);
	if (from_long_ptr.which != static_cast<int>(sizeof(long_value)) + 12) {
		return 2;
	}
	return 0;
}
