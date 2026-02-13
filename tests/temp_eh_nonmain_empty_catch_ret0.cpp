void f() {
	try {
		throw 7;
	} catch (...) {
	}
}

int main() {
	f();
	return 0;
}
