template <typename T>
struct Box {
	static constexpr int values[2] = {40, int(sizeof(T)) + 38};

	static constexpr const int* helper() {
		return values;
	}

	static constexpr int value = helper()[1];
};

int main() {
	return Box<int>::value;
}
