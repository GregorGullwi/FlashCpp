template <class T>
struct Base {
	template <class U>
	static long pick() {
		return static_cast<long>(sizeof(T) + sizeof(U));
	}
};

template <class T>
struct Holder : Base<T> {
	using PickType = decltype(Base<T>::template pick<T>());

	static int run() {
		return sizeof(PickType) == sizeof(long) ? 0 : 1;
	}
};

int main() {
	return Holder<int>::run();
}
