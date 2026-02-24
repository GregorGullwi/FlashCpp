int f() {
	int result = 0;
	try {
		throw 7;
	} catch (int v) {
		result = v;
	}
	return result;
}

int main() {
	return f() == 7 ? 0 : 1;
}
