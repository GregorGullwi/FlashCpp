struct Item {
	int value;
	constexpr Item(double v) : value(static_cast<int>(v)) {}
	constexpr Item(char v) : value(v) {}
	Item(int v) : value(v + 1000) {}
};

constexpr int extracted = []() constexpr {
	Item items[2] = {Item(static_cast<char>(10)), Item(static_cast<char>(42))};
	return items[1].value;
}();

int main() {
	return extracted == 42 ? extracted : 42;
}
