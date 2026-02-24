int main() {
	try {
		int x = 1;
		x += 2;
	} catch (...) {
		return 1;
	}
	return 0;
}