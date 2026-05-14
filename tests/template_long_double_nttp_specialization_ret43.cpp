template<long double V>
struct Box {
	static constexpr int value = 1;
};

template<>
struct Box<1.5L> {
	static constexpr int value = 43;
};

int main() {
	return Box<1.5L>::value;
}
