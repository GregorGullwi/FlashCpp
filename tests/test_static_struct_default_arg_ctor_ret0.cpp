struct Pair {
	int first;
	int second;

	constexpr Pair(int a, int b = 7) : first(a), second(b) {}
};

struct Holder {
	static const Pair value;
};

const Pair Holder::value = Pair(5);

int main() {
	Pair p = Holder::value;
	return p.first + p.second - 12;
}
