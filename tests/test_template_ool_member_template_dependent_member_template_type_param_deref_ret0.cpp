// Regression: out-of-line member-function-template replay must match
// type-parameter-qualified member-template type chains without broad fallback
// attachment.  The dependent parameter is used in the replayed body in a way
// that checks pointer substitution, so an unsubstituted placeholder/codegen
// path cannot pass accidentally.

struct OolMemberTemplateDependentMemberTemplateTypeTag {
	template <class U>
	struct AddPtr {
		using type = U*;
	};
};

template <class T>
struct OolMemberTemplateDependentMemberTemplateTypeReplay {
	template <class U>
	int pick(U seed, typename T::template AddPtr<int>::type value);
};

template <class T>
template <class U>
int OolMemberTemplateDependentMemberTemplateTypeReplay<T>::pick(
	U seed,
	typename T::template AddPtr<int>::type value) {
	return static_cast<int>(sizeof(value)) + seed - seed;
}

int main() {
	int int_value = 40;
	OolMemberTemplateDependentMemberTemplateTypeReplay<
		OolMemberTemplateDependentMemberTemplateTypeTag> replay;
	if (replay.pick(2, &int_value) != 8) {
		return 1;
	}
	return 0;
}
