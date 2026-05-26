// Regression: replayed primary-template plain out-of-line member bodies with
// dependent qualified member types must be synchronized into StructTypeInfo so
// codegen emits the attached definition.

struct PlainMemberDependentQualifiedTypeTag {
	template <class U>
	struct AddPtr {
		using type = U*;
	};
};

template <class T>
struct PlainMemberDependentQualifiedTypeReplay {
	int pick(typename T::template AddPtr<int>::type);
};

template <class T>
int PlainMemberDependentQualifiedTypeReplay<T>::pick(
	typename T::template AddPtr<int>::type) {
	return 99;
}

int main() {
	int value = 0;
	PlainMemberDependentQualifiedTypeReplay<PlainMemberDependentQualifiedTypeTag> instance;
	return instance.pick(&value) == 99 ? 0 : 1;
}
