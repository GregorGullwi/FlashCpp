template <class T>
struct Base {
	template <class U>
	static char pick() {
		return 0;
	}
};

template <class T>
struct IsExactLong {
	static constexpr int value = 0;
};

template <>
struct IsExactLong<long> {
	static constexpr int value = 1;
};

template <class T>
struct Outer {
	template <class U>
	struct Base {
		template <class V>
		static long pick() {
			return 0;
		}
	};

	using PickType = decltype(Base<T>::template pick<T>());

	static int run() {
		return IsExactLong<PickType>::value ? 0 : 1;
	}
};

int main() {
	return Outer<int>::run();
}
