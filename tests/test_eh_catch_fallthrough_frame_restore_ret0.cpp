// Regression test: a catch funclet that falls through normally must resume
// the parent function with a usable frame for subsequent local-variable access.

int main() {
	volatile int a = 10;
	volatile int b = 20;
	volatile int c = 30;
	volatile int d = 40;

	try {
		throw 7;
	} catch (int value) {
		b = a + value;
		d = c + value;
	}

	a = a + 1;
	b = b + 2;
	c = c + 3;
	d = d + 4;

	if (a != 11) return 1;
	if (b != 19) return 2;
	if (c != 33) return 3;
	if (d != 41) return 4;
	return 0;
}