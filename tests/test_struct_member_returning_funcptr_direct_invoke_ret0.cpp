int plus1(int x) {
	return x + 1;
}

struct Adder {
	int (*get())(int) {
		return plus1;
	}
};

int main() {
	Adder adder;
	return adder.get()(41) == 42 ? 0 : 1;
}
