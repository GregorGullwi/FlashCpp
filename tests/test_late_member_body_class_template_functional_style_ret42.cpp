template <typename T>
struct Box {
	T value;

	constexpr Box(T v) : value(v) {}

	T get() const { return value; }
};

template <typename T>
struct Holder {
	T run() {
		return Box<T>(42).get();
	}
};

int main() {
	Holder<int> h;
	return h.run();
}
