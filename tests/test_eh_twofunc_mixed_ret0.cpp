int f() {
	try {
		throw 7;
	} catch (int v) {
		return v;
	}
	return 0;
}

int g() {
	try {
		return 100;
	} catch (int e) {
		return e;
	}
	return 0;
}

int main() {
	int a = f();
	int b = g();
	return (a == 7 && b == 100) ? 0 : 1;
}