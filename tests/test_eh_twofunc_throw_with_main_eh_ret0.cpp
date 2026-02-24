int f() {
	try {
		throw 7;
	} catch (int v) {
		return v;
	}
	return 0;
}

int main() {
	try {
		return f() == 7 ? 0 : 1;
	} catch (...) {
		return 2;
	}
}