template <typename T>
struct Box {
	constexpr int method(int x) const { return x + 1; }

	static constexpr int helper() {
		return static_cast<int>(sizeof(T)) + 38;
	}

	static constexpr int value = Box{}.method(helper());
};

int main() {
	return Box<int>::value == 43 ? 0 : 1;
}
