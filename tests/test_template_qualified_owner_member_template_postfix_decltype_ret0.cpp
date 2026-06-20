template <class T>
struct Box {
	T value;

	T operator()() const;

	template <class U>
	U read() const;
};

template <class T>
struct Base {
	template <class U>
	static Box<U> make();
};

template <class T>
struct Holder {
	using ValueType = decltype(Base<T>::template make<T>().value);
	using CallType = decltype(Base<T>::template make<T>()());
	using ReadType = decltype(Base<T>::template make<T>().template read<T>());

	static int run() {
		return sizeof(ValueType) == sizeof(T) &&
				sizeof(CallType) == sizeof(T) &&
				sizeof(ReadType) == sizeof(T)
			? 0
			: 1;
	}
};

int main() {
	return Holder<int>::run();
}
