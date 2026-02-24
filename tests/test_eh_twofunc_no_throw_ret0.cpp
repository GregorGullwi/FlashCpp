int f() {
	try {
	} catch (...) {
	}
	return 42;
}

int main() {
	return f() == 42 ? 0 : 1;
}
