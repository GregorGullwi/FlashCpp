int plus1(int x) {
	return x + 1;
}

template <typename T>
struct Adder {
	T (*get())(T) {
		return plus1;
	}
};

int main() {
	Adder<int> adder;
	return adder.get()(41) == 42 ? 0 : 1;
}
