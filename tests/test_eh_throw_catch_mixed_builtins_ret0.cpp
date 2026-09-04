int main() {
	int failures = 0;
	try {
		throw true;
	} catch (bool value) {
		if (!value) {
			failures |= 1;
		}
	}
	try {
		throw 3.0;
	} catch (double value) {
		if (value != 3.0) {
			failures |= 2;
		}
	}
	try {
		throw static_cast<short>(7);
	} catch (short value) {
		if (value != 7) {
			failures |= 4;
		}
	}
	try {
		throw 9u;
	} catch (unsigned value) {
		if (value != 9u) {
			failures |= 8;
		}
	}
	try {
		throw 4ll;
	} catch (long long value) {
		if (value != 4ll) {
			failures |= 16;
		}
	}
	try {
		throw 'Q';
	} catch (char value) {
		if (value != 'Q') {
			failures |= 32;
		}
	}
	return failures;
}
