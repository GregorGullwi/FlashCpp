namespace ns {
inline constexpr short value = 42;
}

struct Box {
	int value;

	Box(int x)
		: value(x) {
	}
};

int main() {
	Box box(ns::value);
	int branch = true ? ns::value : 0;
	return (box.value == 42 && branch == 42) ? 0 : 1;
}
