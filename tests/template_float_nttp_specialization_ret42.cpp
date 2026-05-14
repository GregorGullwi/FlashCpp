template<double V>
struct Box {
	static constexpr int value = 1;
};

template<>
struct Box<1.5> {
	static constexpr int value = 42;
};

int main() {
	return Box<1.5>::value;
}
