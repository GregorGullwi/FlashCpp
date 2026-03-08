struct ValueHolder {
	int seed;
	int value = 42;

	constexpr ValueHolder(int s) : seed(s) {}
};

constexpr ValueHolder holder(7);
constexpr int result = holder.value;

int main() {
	return result;
}
