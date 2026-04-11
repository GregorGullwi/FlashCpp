namespace demo {
	template <typename T, int N = sizeof(T) + 38>
	struct Box {
		int value() const;
	};

	template <>
	int Box<int>::value() const {
		return 42;
	}
}

int main() {
	demo::Box<int> box;
	return box.value();
}
