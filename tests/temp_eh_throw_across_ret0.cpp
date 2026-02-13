void f() {
	throw 7;
}

int main() {
	try {
		f();
	} catch (...) {
		return 0;
	}
	return 1;
}
