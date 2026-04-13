template<typename... Args>
int sum_pack_count(Args... args) {
	return static_cast<int>(sizeof...(Args)) + static_cast<int>(sizeof...(args)) - static_cast<int>(sizeof...(Args));
}

template<typename T, typename... Rest>
T multiply_first(T a, Rest... rest) {
	return a + static_cast<T>(sizeof...(rest));
}

struct Counter {
	int value;
	Counter(int v) : value(v) {}
};

template<typename T, typename... Rest>
int count_rest_mixed(T, Rest... rest) {
	return static_cast<int>(sizeof...(rest));
}

int main() {
	int c1 = sum_pack_count<int, double, char>(1, 2.0, 'a');
	if (c1 != 3) {
		return 1;
	}

	int c2 = multiply_first<int>(10, 20, 30);
	if (c2 != 12) {
		return 2;
	}

	int c3 = count_rest_mixed<Counter>(Counter(1), Counter(2), Counter(3));
	if (c3 != 2) {
		return 3;
	}

	int c4 = count_rest_mixed<int>(1);
	if (c4 != 0) {
		return 4;
	}

	return 0;
}
