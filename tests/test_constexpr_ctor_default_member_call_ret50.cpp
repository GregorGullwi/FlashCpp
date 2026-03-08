struct Box {
	int seed;
	int value = 42;

	constexpr Box(int s) : seed(s) {}
	constexpr int get() const { return seed + value; }
};

constexpr Box box(8);
constexpr int result = box.get();
static_assert(result == 50, "Constructor-backed member call should include default member initializer");

int main() {
	return result;
}
