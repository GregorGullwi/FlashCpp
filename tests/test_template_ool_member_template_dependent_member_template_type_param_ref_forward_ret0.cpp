// Regression: OOL member-function-template body replay must preserve both
// alias-carried pointer metadata and an outer reference qualifier when a
// type-parameter-qualified member-template type is forwarded from the body.

struct OolMemberTemplateDependentMemberTemplateTypeRefTag {
	template <class U>
	struct AddPtr {
		using type = U*;
	};
};

int ool_member_template_ref_read(int* value) {
	return *value;
}

template <class T>
struct OolMemberTemplateDependentMemberTemplateTypeRefReplay {
	template <class U>
	int pick(U seed, typename T::template AddPtr<int>::type& value);
};

template <class T>
template <class U>
int OolMemberTemplateDependentMemberTemplateTypeRefReplay<T>::pick(
	U seed,
	typename T::template AddPtr<int>::type& value) {
	return ool_member_template_ref_read(value) + seed - seed;
}

int main() {
	int int_value = 42;
	int* pointer = &int_value;
	OolMemberTemplateDependentMemberTemplateTypeRefReplay<
		OolMemberTemplateDependentMemberTemplateTypeRefTag> replay;
	return replay.pick(3, pointer) == 42 ? 0 : 1;
}
