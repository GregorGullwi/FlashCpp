int takeInt(int x) {
	return x;
}

long takeLong(long x) {
	return x;
}

unsigned takeUnsigned(unsigned x) {
	return x;
}

double takeDouble(double x) {
	return x;
}

int addInts(int a, int b) {
	return a + b;
}

int main() {
	int a = 10;
	int b = 3;

	if (takeInt(a + b) != 13)
		return 1;
	if (takeInt(a - b) != 7)
		return 2;
	if (takeInt(a * b) != 30)
		return 3;
	if (takeInt(a / b) != 3)
		return 4;
	if (takeInt(a % b) != 1)
		return 5;

	if (takeInt(a & b) != 2)
		return 6;
	if (takeInt(a | b) != 11)
		return 7;
	if (takeInt(a ^ b) != 9)
		return 8;
	if (takeInt(a << 1) != 20)
		return 9;
	if (takeInt(a >> 1) != 5)
		return 10;

	if (takeLong(a + b) != 13L)
		return 11;
	if (takeInt(-a) != -10)
		return 12;
	if (takeInt(+a) != 10)
		return 13;
	if (takeInt(~b) != ~3)
		return 14;
	if (addInts(a + 1, b + 1) != 15)
		return 15;
	if (takeDouble(a + b) != 13.0)
		return 16;

	unsigned ua = 8u;
	unsigned ub = 3u;
	if (takeUnsigned(ua + ub) != 11u)
		return 17;

	return 0;
}
