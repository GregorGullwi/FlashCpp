template <typename T>
struct MemberBox {
	struct Payload {
		int value;
	};

	static constexpr Payload payload = {int(sizeof(T)) + 38};

	static constexpr const Payload& helper() {
		return payload;
	}

	static constexpr int value = helper().value;
};

template <typename T>
struct ArrayBox {
	static constexpr int values[2] = {40, int(sizeof(T)) + 38};

	static constexpr const int* helper() {
		return values;
	}

	static constexpr int value = helper()[1];
};

int main() {
	if (MemberBox<int>::value != 42) {
		return 1;
	}
	if (MemberBox<char>::value != 39) {
		return 2;
	}
	if (ArrayBox<int>::value != 42) {
		return 3;
	}
	if (ArrayBox<char>::value != 39) {
		return 4;
	}
	return 42;
}
