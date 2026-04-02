template <typename T>
struct Box {
	static constexpr int payload{int(sizeof(T)) + 38};

	static constexpr int helper() {
		return payload;
	}

	static constexpr int value = helper();
};

int main() {
	if (Box<int>::value != 42) {
		return 1;
	}
	if (Box<char>::value != 39) {
		return 2;
	}
	return 42;
}
