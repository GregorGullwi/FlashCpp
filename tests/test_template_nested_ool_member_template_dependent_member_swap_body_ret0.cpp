template<class T>
struct NestedDependentMemberTemplateSwap {
	struct Inner {
		template<class U>
		int pick(U, typename T::first_type);

		template<class U>
		int pick(U, typename T::second_type);
	};
};

template<class T>
template<class U>
int NestedDependentMemberTemplateSwap<T>::Inner::pick(U, typename T::second_type marker) {
	return sizeof(marker) == sizeof(long) ? 20 : 21;
}

template<class T>
template<class U>
int NestedDependentMemberTemplateSwap<T>::Inner::pick(U, typename T::first_type marker) {
	return sizeof(marker) == sizeof(int) ? 10 : 11;
}

struct NestedMemberTemplateSwapArgs {
	using first_type = int;
	using second_type = long;
};

int main() {
	NestedDependentMemberTemplateSwap<NestedMemberTemplateSwapArgs>::Inner inner;
	if (inner.pick(0, 1) != 10) {
		return 1;
	}
	if (inner.pick(0, 1L) != 20) {
		return 2;
	}
	return 0;
}
