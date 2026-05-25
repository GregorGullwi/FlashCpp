// Expected-fail regression: primary-template plain out-of-line constructor replay
// attachment no longer accepts unresolved-signature single-candidate fallback.

struct NulloptCtorTag {
	template <class U>
	struct AddPtr {
		using type = U*;
	};
};

template <class T>
struct PlainCtorNulloptReplay {
	int which = 0;

	PlainCtorNulloptReplay(typename T::template AddPtr<int>::type);
};

template <class T>
PlainCtorNulloptReplay<T>::PlainCtorNulloptReplay(typename T::template AddPtr<int>::type)
	: which(99) {}

int main() {
	int value = 0;
	PlainCtorNulloptReplay<NulloptCtorTag> instance(&value);
	return instance.which == 99 ? 0 : 1;
}
