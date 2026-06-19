template <class T>
struct Host {
	template <class U>
	static long value() {
		return static_cast<long>(sizeof(U) + sizeof(T));
	}
};

template <class T>
using HostValue = decltype(Host<T>::template value<T>());

int main() {
	return sizeof(HostValue<int>) == sizeof(long) ? 0 : 1;
}
