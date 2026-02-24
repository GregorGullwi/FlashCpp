int f() {
	throw 7;
	return 0;
}

int main() {
	try {
		return f();
	} catch (int v) {
		return v == 7 ? 0 : 1;
	}
	return 1;
}
