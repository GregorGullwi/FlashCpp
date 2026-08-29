template <typename T, int N = T::missing>
struct S {
	static constexpr int value = N;
};

int main() {
	return S<int>::value;
}
