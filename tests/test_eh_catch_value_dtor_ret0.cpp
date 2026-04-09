// Regression test: a catch-by-value parameter must be destroyed when the
// handler exits, in addition to destroying the exception object itself.

int g_copy_count = 0;
int g_dtor_count = 0;

struct Payload {
	int value;

	Payload(int v) : value(v) {}

	Payload(Payload& other) : value(other.value + 10) {
		g_copy_count++;
	}

	~Payload() {
		g_dtor_count++;
	}
};

int main() {
	Payload payload(7);
	bool caught = false;

	try {
		throw payload;
	} catch (Payload caught_value) {
		caught = true;
		if (g_copy_count != 2)
			return 1;
		if (caught_value.value != 27)
			return 2;
		if (g_dtor_count != 0)
			return 3;
	}

	if (!caught)
		return 4;

	// The catch parameter and the exception object should both be destroyed
	// before control leaves the handler. The original source object remains alive
	// until main() returns.
	return g_dtor_count == 2 ? 0 : 5;
}
