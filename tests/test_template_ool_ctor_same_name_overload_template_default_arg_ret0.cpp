// Regression: out-of-line constructor-template replay attachment must not
// accept non-template constructor stubs by shape/signature fallback when
// matching in same-name overload sets.
template<class T>
struct CtorTemplateDefaultArgReplay {
	int which;

	template<class U = void>
	CtorTemplateDefaultArgReplay(T*);

	CtorTemplateDefaultArgReplay(T*)
		: which(22) {}

	CtorTemplateDefaultArgReplay(long*)
		: which(33) {}
};

template<class T>
template<class U>
CtorTemplateDefaultArgReplay<T>::CtorTemplateDefaultArgReplay(T*)
	: which(0) {
	which = 11;
}

int main() {
	int value = 0;
	long long_value = 0;

	CtorTemplateDefaultArgReplay<int> instance(&value);
	if (instance.which != 22) {
		return 1;
	}

	CtorTemplateDefaultArgReplay<int> from_long_ptr(&long_value);
	if (from_long_ptr.which != 33) {
		return 2;
	}

	return 0;
}
