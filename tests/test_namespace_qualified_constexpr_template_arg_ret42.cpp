namespace config {
	constexpr int value = 42;
}

template <int N>
struct Holder {
	static constexpr int value = N;
};

int main() {
	return Holder<config::value>::value;
}
