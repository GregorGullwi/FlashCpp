template <typename T>
struct Box {
	T value;

	Box()
		: value() {}

	template <typename U>
	int assignFrom(const Box<U>& other) {
		LaterType converted = static_cast<LaterType>(other.value);
		value = converted;
		return static_cast<int>(value);
	}

	using LaterType = T;
};

int main() {
	Box<int> src;
	src.value = 7;
	Box<long> dst;
	return dst.assignFrom(src) == 7 ? 0 : 1;
}
