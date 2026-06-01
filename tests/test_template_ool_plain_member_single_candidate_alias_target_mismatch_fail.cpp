struct PlainSingleCandidateAliasMismatchTag {
	template<class U>
	struct AddPtr {
		using type = U*;
	};
};

template<class T>
struct PlainSingleCandidateAliasMismatch {
	int pick(typename T::template AddPtr<int>::type value);
};

template<class T>
int PlainSingleCandidateAliasMismatch<T>::pick(
	typename T::template AddPtr<long>::type value) {
	return static_cast<int>(sizeof(*value));
}

int main() {
	int value = 0;
	PlainSingleCandidateAliasMismatch<PlainSingleCandidateAliasMismatchTag> instance;
	return instance.pick(&value);
}
