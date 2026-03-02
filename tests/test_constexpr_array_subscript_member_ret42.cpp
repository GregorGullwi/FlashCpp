struct Item {
	int value;
	constexpr Item(int v) : value(v) {}
};

constexpr Item items[2] = {Item(10), Item(42)};
constexpr int extracted = items[1].value;

int main() {
	return extracted;
}
