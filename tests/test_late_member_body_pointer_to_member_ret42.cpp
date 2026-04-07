template <typename T>
struct Box {
	T value;

	constexpr Box(T v) : value(v) {}
};

template <typename T>
struct Holder {
	int run() {
		auto member = &Box<T>::value;
		return sizeof(decltype((Box<T>(42)).*member)) == sizeof(T) ? 42 : 0;
	}
};

int main() {
	Holder<int> holder;
	return holder.run();
}
