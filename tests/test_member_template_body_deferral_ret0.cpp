template <typename T>
struct Box {
	T value;

	Box()
		: value() {}

	template <typename U>
	int assignFrom(const Box<U>& other) {
		this->emplace(static_cast<T>(other.value));
		return static_cast<int>(value);
	}

	template <typename... Args>
	void emplace(Args&&... args) {
		value = T(args...);
	}
};

int main() {
	Box<int> src;
	src.value = 7;
	Box<long> dst;
	return dst.assignFrom(src) == 7 ? 0 : 1;
}
