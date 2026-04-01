template <typename T>
struct Box {
	static constexpr int helper() {
		return static_cast<int>(sizeof(T)) + 38;
	}

	static constexpr int value = static_cast<int>(helper());
};

int main() {
	return Box<int>::value;
}
