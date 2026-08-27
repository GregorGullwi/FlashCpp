// C++20 [conv.prom]: sizeof observes the int result type of unary +.

int main() {
	unsigned char value = 200;
	return sizeof(+value) == sizeof(int) ? 0 : 1;
}
