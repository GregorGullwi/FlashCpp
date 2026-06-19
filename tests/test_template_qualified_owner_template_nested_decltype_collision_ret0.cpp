template <class T>
struct Base {
	template <class U>
	static char pick() {
		return 0;
	}
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
		return sizeof(PickType) == sizeof(long) ? 0 : 1;
	}
};

int main() {
	return Outer<int>::run();
}
