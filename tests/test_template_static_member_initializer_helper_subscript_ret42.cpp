template <typename T>
struct Box {
	static constexpr int values[2] = {40, int(sizeof(T)) + 38};

	static constexpr const int* helper() {
		return values;
	}

	static constexpr int value = helper()[1];
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
