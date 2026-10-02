// Regression: a local's numeric identity must survive throw lowering.
int main() {
	try {
		int value = 17;
		throw value;
	} catch (int value) {
		return value == 17 ? 0 : 1;
	}
	return 2;
}
