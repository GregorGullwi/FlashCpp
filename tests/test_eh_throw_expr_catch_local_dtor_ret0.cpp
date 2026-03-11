// Regression test: throw <expr> from inside a catch body on Windows must
// materialize the thrown value before cleaning catch-local objects, then run
// those catch-local destructors before the outer handler observes control.

int g_cleanup = 0;

struct Guard {
	~Guard() {
		g_cleanup += 100;
	}
};

struct Payload {
	int value;

	Payload(int v) {
		value = v;
	}

	Payload(Payload& other) {
		value = other.value + g_cleanup;
	}

	~Payload() {
		g_cleanup += 1;
		value = -999;
	}
};

void throwFromCatch() {
	try {
		throw 1;
	} catch (int) {
		Payload payload(7);
		Guard guard;
		throw payload;
	}
}

int main() {
	try {
		throwFromCatch();
		return 1;
	} catch (Payload& caught) {
		if (caught.value != 7) return 2;
		if (g_cleanup != 101) return 3;
		return 0;
	}

	return 4;
}