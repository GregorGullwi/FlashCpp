// Regression: replayed plain out-of-line member sync into StructTypeInfo must
// resolve by source declaration identity, not by same-name/same-arity shape.
// The int and long dependent alias-target overloads must each keep their own body.
struct OolPlainMemberDependentMemberTemplateAliasOverloadTag {
	template<class U>
	struct AddPtr {
		using type = U*;
	};
};

template<class T>
struct OolPlainMemberDependentMemberTemplateAliasOverload {
	int pick(typename T::template AddPtr<int>::type value);
	int pick(typename T::template AddPtr<long>::type value);
};

template<class T>
int OolPlainMemberDependentMemberTemplateAliasOverload<T>::pick(
	typename T::template AddPtr<long>::type value) {
	return static_cast<int>(sizeof(*value)) + 10;
}

template<class T>
int OolPlainMemberDependentMemberTemplateAliasOverload<T>::pick(
	typename T::template AddPtr<int>::type value) {
	return static_cast<int>(sizeof(*value)) + 20;
}

int main() {
	int int_value = 0;
	long long_value = 0;
	OolPlainMemberDependentMemberTemplateAliasOverload<
		OolPlainMemberDependentMemberTemplateAliasOverloadTag> value;
	if (value.pick(&int_value) != 24) {
		return 1;
	}
	if (value.pick(&long_value) != static_cast<int>(sizeof(long_value)) + 10) {
		return 2;
	}
	return 0;
}
