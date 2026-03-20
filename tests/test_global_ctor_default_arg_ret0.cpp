struct Pair {
	int first;
	int second;

	constexpr Pair(int a, int b = 7) : first(a), second(b) {}
};

Pair g_pair = Pair(5);

int main() {
	return (g_pair.first == 5 && g_pair.second == 7) ? 0 : 1;
}
