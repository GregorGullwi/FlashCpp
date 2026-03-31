struct Wrap {
	constexpr Wrap(int x) : value(x) {}
	int value;
};

template <int N>
struct Box {
	Wrap value = N + 0;
	int data[N + 0];
	static constexpr int mirror = N + 0;
};

int main() {
	Box<21> box{};
	return box.value.value + Box<21>::mirror;
}
