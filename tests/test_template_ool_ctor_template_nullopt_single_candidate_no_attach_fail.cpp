// Expected-fail regression: out-of-line constructor-template replay attachment
// no longer accepts unresolved-signature single-candidate fallback.

struct NulloptCtorTemplateTag {
	using type = int;
};

template <class T>
struct CtorTemplateNulloptReplay {
	int which = 0;

	template <class U>
	CtorTemplateNulloptReplay(U, typename T::type*)
		: which(11) {}
};

template <class T>
template <class U>
CtorTemplateNulloptReplay<T>::CtorTemplateNulloptReplay(U, typename T::type*)
	: which(99) {}

int main() {
	int value = 0;
	CtorTemplateNulloptReplay<NulloptCtorTemplateTag> instance(0, &value);
	return instance.which == 11 ? 0 : 1;
}
