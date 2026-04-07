struct Pair {
	int first;
	int second;

	constexpr Pair(int a, int b = 7) : first(a), second(b) {}
};

constexpr int read_second() {
	Pair items[2] = {Pair(1), Pair(5)};
	return items[1].second;
}

static_assert(read_second() == 7);

int main() {
	return read_second() == 7 ? 0 : 1;
}
