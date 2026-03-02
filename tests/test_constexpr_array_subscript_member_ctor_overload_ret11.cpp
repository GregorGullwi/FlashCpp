struct S {
	int value;
	constexpr S(int) : value(11) {}
	constexpr S(double) : value(22) {}
};

constexpr S arr[1] = {S(10)};
constexpr int selected = arr[0].value;

int main() {
	return selected;
}
