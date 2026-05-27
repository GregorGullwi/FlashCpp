// Regression: OOL member-function-template body replay must parse parameters
// whose concrete type is recovered from a type-parameter-qualified member
// template type with the instantiated parameter metadata, not a placeholder
// definition-side type.  Direct dereference and forwarding both require the
// body parameter to behave as the resolved int*.

struct OolMemberTemplateDependentMemberTemplateTypeForwardDerefTag {
	template <class U>
	struct AddPtr {
		using type = U*;
	};
};

int ool_member_template_forward_deref_read(int* value) {
	return *value;
}

template <class T>
struct OolMemberTemplateDependentMemberTemplateTypeForwardDerefReplay {
	template <class U>
	int pick(U seed, typename T::template AddPtr<int>::type value);
};

template <class T>
template <class U>
int OolMemberTemplateDependentMemberTemplateTypeForwardDerefReplay<T>::pick(
	U seed,
	typename T::template AddPtr<int>::type value) {
	return static_cast<int>(sizeof(*value)) +
		static_cast<int>(sizeof(ool_member_template_forward_deref_read(value))) -
		8 + seed - seed;
}

int main() {
	int int_value = 41;
	OolMemberTemplateDependentMemberTemplateTypeForwardDerefReplay<
		OolMemberTemplateDependentMemberTemplateTypeForwardDerefTag> replay;
	return replay.pick(3, &int_value);
}
