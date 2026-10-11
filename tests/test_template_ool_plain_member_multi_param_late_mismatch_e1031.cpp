struct PlainMultiParamLateMismatchTag {
	template<class U>
	struct AddPtr {
		using type = U*;
	};
};

template<class T>
struct PlainMultiParamLateMismatch {
	int pick(int seed, typename T::template AddPtr<int>::type value);
};

template<class T>
int PlainMultiParamLateMismatch<T>::pick(
	int seed,
	typename T::template AddPtr<long>::type value) {
	return seed + static_cast<int>(sizeof(*value));
}

int main() {
	int value = 0;
	PlainMultiParamLateMismatch<PlainMultiParamLateMismatchTag> instance;
	return instance.pick(1, &value);
}
