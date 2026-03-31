auto safe_value(bool fail) try {
	if (fail) {
		throw 7;
	}
	return 42;
} catch (int value) {
	return value;
}

int main() {
	return (safe_value(false) == 42 && safe_value(true) == 7) ? 0 : 1;
}
