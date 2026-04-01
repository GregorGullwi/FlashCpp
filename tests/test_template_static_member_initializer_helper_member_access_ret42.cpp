template <typename T>
struct Box {
	struct Payload {
		int value;
	};

	static constexpr Payload payload = {int(sizeof(T)) + 38};

	static constexpr const Payload& helper() {
		return payload;
	}

	static constexpr int value = helper().value;
};

int main() {
	return Box<int>::value;
}
