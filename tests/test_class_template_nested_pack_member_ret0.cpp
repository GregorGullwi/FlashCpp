// Regression test for nested wrapped pack deduction in class-template member
// function templates, including static qualified calls.

template<typename T>
struct Box {
	T value;
};

template<typename T, typename U>
struct Pair {
	T first;
	U second;
};

template<typename T>
struct Wrap {
	T value;
};

template<typename Tag>
struct Host {
	template<typename... Ts>
	int sum_boxes(Wrap<Box<Ts>>... xs) {
		return (0 + ... + static_cast<int>(xs.value.value));
	}

	template<typename... Ts, typename... Us>
	static int sum_pairs(Wrap<Pair<Ts, Us>>... xs) {
		return (0 + ... + (static_cast<int>(xs.value.first) + static_cast<int>(xs.value.second)));
	}
};

int main() {
	Host<int> host;
	Wrap<Box<int>> a{{1}};
	Wrap<Box<double>> b{{2.0}};
	Wrap<Pair<int, double>> c{{1, 2.0}};
	Wrap<Pair<char, int>> d{{3, 4}};

	if (host.sum_boxes(a, b) != 3) return 1;
	if (Host<int>::sum_pairs(c, d) != 10) return 2;
	return 0;
}
