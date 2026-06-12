// Regression: out-of-line class-template member-template operator() overloads
// must sync the StructTypeInfo copy through source-member identity instead of
// replay-source-key recovery after the source declaration is already known.
struct OolMemberTemplateOperatorIdentityTag {
	template<class U>
	struct AddPtr {
		using type = U*;
	};
};

template<class T>
struct OolMemberTemplateOperatorIdentity {
	template<class U>
	int operator()(U seed, typename T::template AddPtr<int>::type value) const;

	template<class U>
	int operator()(U seed, typename T::template AddPtr<long>::type value) const;
};

template<class T>
template<class U>
int OolMemberTemplateOperatorIdentity<T>::operator()(
	U seed,
	typename T::template AddPtr<long>::type value) const {
	return static_cast<int>(sizeof(*value)) + seed + 10;
}

template<class T>
template<class U>
int OolMemberTemplateOperatorIdentity<T>::operator()(
	U seed,
	typename T::template AddPtr<int>::type value) const {
	return static_cast<int>(sizeof(*value)) + seed + 20;
}

int main() {
	int int_value = 0;
	long long_value = 0;
	OolMemberTemplateOperatorIdentity<
		OolMemberTemplateOperatorIdentityTag> value;
	if (value(1, &int_value) != 25) {
		return 1;
	}
	if (value(2, &long_value) != static_cast<int>(sizeof(long_value)) + 12) {
		return 2;
	}
	return 0;
}
