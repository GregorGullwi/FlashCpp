struct Inner {
	Inner(double d) : x(static_cast<int>(d) + 1) {}
	constexpr Inner(int v) : x(v) {}
	int x;
};

constexpr Inner arr[] = {Inner(42), Inner(1)};
constexpr int value() { return arr[0].x; }
constexpr int result = value();

int main() {
	static_assert(result == 42);
	return result;
}
