// Regression: OOL member-function-template overloads that differ only by
// swapped dependent member parameter types must attach and instantiate the
// corresponding body for each overload.
template<class T>
struct DependentMemberTemplateSwap {
	template<class U>
	int pick(U, typename T::first_type);

	template<class U>
	int pick(U, typename T::second_type);
};

template<class T>
template<class U>
int DependentMemberTemplateSwap<T>::pick(U, typename T::second_type) {
	return 20;
}

template<class T>
template<class U>
int DependentMemberTemplateSwap<T>::pick(U, typename T::first_type) {
	return 10;
}

struct MemberTemplateSwapArgs {
	using first_type = int;
	using second_type = long;
};

int main() {
	DependentMemberTemplateSwap<MemberTemplateSwapArgs> value;
	if (value.pick(0, 1) != 10) {
		return 1;
	}
	if (value.pick(0, 1L) != 20) {
		return 2;
	}
	return 0;
}
