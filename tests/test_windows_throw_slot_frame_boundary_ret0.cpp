// A function that only throws needs a real exception-object slot below the
// Windows callee home area, even when it has no ordinary locals.
int throwValue() {
	throw 0x12345678;
}

int main() {
	try {
		throwValue();
	} catch (int value) {
		return value == 0x12345678 ? 0 : 1;
	}
	return 2;
}
