// Regression test: a catch body may contain a return path but still continue
// normally through CatchEnd when that return is not taken.

int runSequence() {
	bool caught = false;

	try {
		throw 7;
	} catch (int value) {
		if (value != 7) return 1;
		caught = true;
	}
	if (!caught) return 2;

	caught = false;
	try {
		throw 8;
	} catch (int value) {
		if (value != 8) return 3;
		caught = true;
	}
	if (!caught) return 4;

	return 0;
}

int main() {
	return runSequence();
}