struct Box {
	int value;

	constexpr Box(int v) : value(v) {}
	constexpr int get() const { return value; }
};

constexpr Box box(42);
constexpr int result = box.get();

int main() {
	return result;
}
