namespace constants {
	constexpr int base = 11;
}

template <int N>
struct Add {
	static constexpr int value = constants::base + N;
};

int main() {
	return Add<6>::value;
}
