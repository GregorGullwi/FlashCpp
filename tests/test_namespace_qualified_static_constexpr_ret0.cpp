namespace config {
	constexpr int base = 5;
}

template <int N>
struct Add {
	static constexpr int value = config::base + N;
};

int main() {
	return Add<37>::value == 42 ? 0 : 1;
}
