namespace left {
	template <typename T>
	struct box {
		static constexpr int value = 3;
	};
}

namespace right {
	template <typename T>
	struct box {
		static constexpr int value = 9;
	};
}

template <typename T>
int read_left_box() {
	return left::box<T>::value;
}

int main() {
	return read_left_box<int>() - 3;
}
