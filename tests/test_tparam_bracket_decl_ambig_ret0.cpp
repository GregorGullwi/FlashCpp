template <typename T>
struct Test {
	static int run() {
		T(x)
		[3];
		T(y);
		x[0] = 1;
		x[1] = 2;
		x[2] = 3;
		y = 36;
		return x[0] + x[1] + x[2] + y;
	}
};

int main() {
	return Test<int>::run() - 42;
}
