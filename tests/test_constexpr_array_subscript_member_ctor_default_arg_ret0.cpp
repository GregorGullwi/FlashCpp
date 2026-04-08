struct Pair {
	int first;
	int second;

	constexpr Pair(int a, int b = 7) : first(a), second(b) {}
};

constexpr int extracted = []() constexpr {
	Pair items[2] = {Pair(1), Pair(5)};
	return items[1].second;
}();

static_assert(extracted == 7);

int main() {
	return extracted == 7 ? 0 : 1;
}
