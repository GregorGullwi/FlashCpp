// Regression test: locals created inside a catch body must be destroyed when
// the catch falls through normally, even if the catch also contains a return path.

int g_destroyed = 0;

struct Guard {
	~Guard() {
		g_destroyed++;
	}
};

int main() {
	bool caught = false;

	try {
		throw 42;
	} catch (int value) {
		Guard guard;
		if (value != 42) return 1;
		caught = true;
	}

	if (!caught) return 2;
	if (g_destroyed != 1) return 3;
	return 0;
}