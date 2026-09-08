// Dependent member template-ids keep argument identity on the working
// out-of-line function-parameter instantiation path.
struct DependentTemplateMemberTag {
	template <class U>
	struct AddPtr {
		using type = U*;
	};
};
template <class T>
struct DependentTemplateMemberHold {
	int pick(typename T::template AddPtr<int>::type);
};
template <class T>
int DependentTemplateMemberHold<T>::pick(typename T::template AddPtr<int>::type value) {
	return *value;
}
int main() {
	int value = 7;
	DependentTemplateMemberHold<DependentTemplateMemberTag> hold;
	return hold.pick(&value) - 7;
}
