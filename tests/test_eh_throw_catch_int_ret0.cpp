int main() {
	try {
		throw 7;
	} catch (int v) {
		return v == 7 ? 0 : 2;
	}
	return 1;
}