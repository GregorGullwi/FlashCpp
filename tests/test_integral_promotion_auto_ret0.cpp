// C++20 [conv.prom]: unary + promotes unsigned char to int before auto deduction.

int classify(int) {
	return 0;
}

int classify(unsigned char) {
	return 1;
}

int main() {
	unsigned char value = 200;
	auto promoted = +value;
	return classify(promoted);
}
