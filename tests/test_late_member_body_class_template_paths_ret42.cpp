template <typename T>
struct Box {
	T value;

	constexpr Box(T v) : value(v) {}

	T get() const { return value; }
};

template <typename T>
struct Holder {
	T run() {
		Box<T> box{42};
		return box.get();
	}
};

int main() {
	Holder<int> h;
	return h.run();
}
