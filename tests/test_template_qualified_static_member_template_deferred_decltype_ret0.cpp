template <class T>
struct Host {
	template <class U>
	static long value() {
		return static_cast<long>(sizeof(U) + sizeof(T));
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
using HostValue = decltype(Host<T>::template value<T>());

int main() {
	return IsExactLong<HostValue<int>>::value ? 0 : 1;
}
