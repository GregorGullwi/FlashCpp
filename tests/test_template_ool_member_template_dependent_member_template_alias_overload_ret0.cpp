// Regression: overloaded OOL member-function-template replay must not treat
// owner-only artifacts from `typename T::template AddPtr<...>::type`
// substitution as an early signature match. The `int` and `long` alias-target
// overloads must each attach to their own out-of-line body.
struct OolMemberTemplateDependentMemberTemplateAliasOverloadTag {
	template<class U>
	struct AddPtr {
		using type = U*;
	};
};

template<class T>
struct OolMemberTemplateDependentMemberTemplateAliasOverload {
	template<class U>
	int pick(U seed, typename T::template AddPtr<int>::type value);

	template<class U>
	int pick(U seed, typename T::template AddPtr<long>::type value);
};

template<class T>
template<class U>
int OolMemberTemplateDependentMemberTemplateAliasOverload<T>::pick(
	U seed,
	typename T::template AddPtr<long>::type value) {
	return static_cast<int>(sizeof(*value)) + seed + 10;
}

template<class T>
template<class U>
int OolMemberTemplateDependentMemberTemplateAliasOverload<T>::pick(
	U seed,
	typename T::template AddPtr<int>::type value) {
	return static_cast<int>(sizeof(*value)) + seed + 20;
}

int main() {
	int int_value = 0;
	long long_value = 0;
	OolMemberTemplateDependentMemberTemplateAliasOverload<
		OolMemberTemplateDependentMemberTemplateAliasOverloadTag> value;
	if (value.pick(1, &int_value) != 25) {
		return 1;
	}
	if (value.pick(2, &long_value) != static_cast<int>(sizeof(long_value)) + 12) {
		return 2;
	}
	return 0;
}
