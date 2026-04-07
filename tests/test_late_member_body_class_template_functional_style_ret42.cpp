template <typename T>
struct Box {
	T value;

	constexpr Box(T v) : value(v) {}

	T get() const { return value; }
};

template <typename T>
struct Holder {
	T run(T value) {
		return Box<T>(value).get();
	}
};

int main() {
	Holder<int> ints;
	Holder<long long> longs;
	Holder<char> chars;

	return ints.run(20) + static_cast<int>(longs.run(20)) + static_cast<int>(chars.run(2));
}
