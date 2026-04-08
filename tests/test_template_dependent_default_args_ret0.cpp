template<typename T>
struct decay_like {
	using type = T;
};

template<typename T, typename U,
		 typename D1 = typename decay_like<T>::type,
		 typename D2 = typename decay_like<U>::type>
struct common_impl {
	static constexpr int value = 0;
};

template<typename T, typename U>
struct common_impl<T, U, T, U> {
	static constexpr int value = 42;
};

template<int N>
struct ratio_num {
	static constexpr int value = N;
};

template<typename A, typename B,
		 bool LeftZero = (A::value == 0 || B::value == 0),
		 bool BothNeg = (A::value < 0 && B::value < 0)>
struct ratio_impl {
	static constexpr int value = 1;
};

template<typename A, typename B>
struct ratio_impl<A, B, true, false> {
	static constexpr int value = 7;
};

int main() {
	if (common_impl<int, int>::value != 42)
		return 1;

	if (ratio_impl<ratio_num<-1>, ratio_num<5>>::value != 1)
		return 2;

	if (ratio_impl<ratio_num<0>, ratio_num<5>>::value != 7)
		return 3;

	return 0;
}
