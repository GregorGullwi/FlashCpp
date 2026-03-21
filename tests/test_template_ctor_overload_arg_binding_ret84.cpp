struct Box {
	int value;

	Box(int x) : value(x + 35) {}
	Box(double) : value(3) {}
};

int readBox(Box box) {
	return box.value;
}

template<int N>
int testParenInit() {
	return readBox(Box(N));
}

template<int N>
int testBraceInit() {
	return readBox(Box{N});
}

int main() {
	return testParenInit<7>() + testBraceInit<7>();
}
