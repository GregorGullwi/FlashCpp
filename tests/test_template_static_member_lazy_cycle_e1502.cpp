template <int N>
struct Box {
	static constexpr int a = b;
	static constexpr int b = a + N;
};

int main() {
	return Box<1>::a;
}
