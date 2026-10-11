struct SingleCandidateAliasMismatchTag {
	template<class U>
	struct AddPtr {
		using type = U*;
	};
};

template<class T>
struct SingleCandidateAliasMismatch {
	template<class U>
	int pick(typename T::template AddPtr<int>::type value);
};

template<class T>
template<class U>
int SingleCandidateAliasMismatch<T>::pick(
	typename T::template AddPtr<long>::type value) {
	return static_cast<int>(sizeof(*value));
}

int main() {
	int value = 0;
	SingleCandidateAliasMismatch<SingleCandidateAliasMismatchTag> instance;
	return instance.pick<int>(&value);
}
