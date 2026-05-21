template <typename T>
struct MetaFactory {
	template <typename U>
	struct Wrap {
		template <int N>
		struct Step {
			static constexpr int value = static_cast<int>(sizeof(T) + sizeof(U)) + N;
		};
	};
};

template <typename T, int N>
struct Holder {
	static int value;
};

template <typename T, int N>
int Holder<T, N>::value =
	MetaFactory<T>::template Wrap<char>::template Step<N>::value;

int main() {
	if (Holder<int, 5>::value != 10) {
		return 1;
	}
	if (Holder<long long, 2>::value != 11) {
		return 2;
	}
	return 0;
}
