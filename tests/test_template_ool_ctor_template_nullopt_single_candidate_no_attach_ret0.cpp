// Regression: replay-first out-of-line constructor-template attachment should
// canonicalize equivalent dependent member-type placeholders instead of
// rejecting single-candidate matches.

struct NulloptCtorTemplateTag {
	template <class U>
	struct AddPtr {
		using type = U*;
	};
};

template <class T>
struct CtorTemplateNulloptReplay {
	int which = 0;

	template <class U>
	CtorTemplateNulloptReplay(U, typename T::template AddPtr<int>::type);
};

template <class T>
template <class U>
CtorTemplateNulloptReplay<T>::CtorTemplateNulloptReplay(U, typename T::template AddPtr<int>::type)
	: which(99) {}

int main() {
	int value = 0;
	CtorTemplateNulloptReplay<NulloptCtorTemplateTag> instance(0, &value);
	return instance.which == 99 ? 0 : 1;
}
