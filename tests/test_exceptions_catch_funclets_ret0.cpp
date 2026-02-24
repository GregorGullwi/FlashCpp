// Validates C++ EH metadata with multiple concrete catch funclet entries.
int classify(int v) {
	try {
		if (v == 0) {
			throw 1;
		}
		if (v == 1) {
			throw 2.0;
		}
		throw 'x';
	} catch (int) {
		return 10;
	} catch (double) {
		return 20;
	} catch (...) {
		return 30;
	}
}

int main() {
	int a = classify(0);
	int b = classify(1);
	int c = classify(2);
	return (a + b + c == 60) ? 0 : 1;
}
