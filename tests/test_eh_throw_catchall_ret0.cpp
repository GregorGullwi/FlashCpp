int main() {
	try {
		throw 7;
	} catch (...) {
		return 0;
	}
	return 1;
}