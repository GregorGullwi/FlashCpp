template <typename T>
struct Pair {
	int a;
	int b;
};

template <typename T>
struct Box {
	static constexpr Pair<T> values[2] = {{1, 2}, {int(sizeof(T)) + 38, 7}};

	static constexpr const Pair<T>* helper() {
		return values;
	}

	static constexpr int value = helper()[1].a;
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
