template <typename U, class C, int N>
struct Outer {
	struct Inner {
		int value = N + sizeof(C) - sizeof(U);
		static constexpr int staticValue = N + sizeof(C) - sizeof(U);
	};
};

int main() {
	Outer<char, int, 39>::Inner inner{};
	return inner.value + Outer<char, int, 39>::Inner::staticValue;
}
