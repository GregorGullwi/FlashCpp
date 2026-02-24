int f() {
	try {
		throw 42;
	} catch (...) {
	}
	return 0;
}

int main() {
	f();
	return 0;
}
