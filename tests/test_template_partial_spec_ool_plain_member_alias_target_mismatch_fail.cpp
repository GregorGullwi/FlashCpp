struct PartialSpecPlainMismatchTag {
	template<class U>
	struct AddPtr {
		using type = U*;
	};
};

template<class T>
struct PartialSpecPlainMismatch;

template<class T>
struct PartialSpecPlainMismatch<T*> {
	int pick(typename T::template AddPtr<int>::type value);
};

template<class T>
int PartialSpecPlainMismatch<T*>::pick(
	typename T::template AddPtr<long>::type value) {
	return static_cast<int>(sizeof(*value));
}

int main() {
	int value = 0;
	PartialSpecPlainMismatch<PartialSpecPlainMismatchTag*> instance;
	return instance.pick(&value);
}
