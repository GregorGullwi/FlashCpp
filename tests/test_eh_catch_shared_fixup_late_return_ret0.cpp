// Regression: when multiple catch handlers share one continuation fixup stub,
// a later handler's direct return must still use the catch-return flag/value path.

int classify(int tag) {
	try {
		if (tag == 0) {
			throw 1;
		}
		throw 2.0;
	} catch (int) {
		// Fall through normally so the first CatchEnd emits the shared fixup stub.
	} catch (double) {
		return 42;
	}

	return 0;
}

int main() {
	if (classify(0) != 0) return 1;
	if (classify(1) != 42) return 2;
	return 0;
}