template<typename... Args>
int sum_pack_count(Args... args) {
	return static_cast<int>(sizeof...(Args));
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

int helper_sig(double, int) {
	return 50;
}

int helper_sig(int, int) {
	return 5;
}

int tail_kind(long) {
	return 9;
}

int tail_kind(short) {
	return 3;
}

template<typename... Args>
int explicit_pack_signature(Args... args) {
	return helper_sig(args...);
}

template<typename T, typename... Rest, typename U>
int probe_post_pack(T, Rest..., U value) {
	return tail_kind(value);
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

	auto fn = &explicit_pack_signature<double, int>;
	if (fn(1, 2) != 50) {
		return 5;
	}

	int c6 = probe_post_pack<int, char, short>(1, 'a', static_cast<short>(2), 0L);
	if (c6 != 9) {
		return 6;
	}

	return 0;
}
