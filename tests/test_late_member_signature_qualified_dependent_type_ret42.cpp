template <typename T>
struct Box {
	using value_type = T;

	T value;

	constexpr Box(T v) : value(v) {}

	value_type get() const { return value; }
};

template <typename T>
struct Holder {
	typename Box<T>::value_type run() {
		Box<T> box{42};
		return box.get();
	}
};

int main() {
	Holder<int> h;
	return h.run();
}
