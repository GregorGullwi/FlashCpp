// Cross-platform regression test: throw <expr> from inside a catch body must
// materialize the thrown value before cleaning catch-local objects, and each
// destructor must run exactly once. This validates that frontend-emitted
// catch-scope destructors (emitActiveCatchScopeDestructors) do not cause
// double destruction on any platform (Linux ELF does not emit cleanup LPs
// for catch-body locals, so the frontend call is the only cleanup path).

int g_guard_dtor_count = 0;
int g_payload_dtor_count = 0;
int g_copy_ctor_count = 0;

struct Guard {
	int id;

	Guard(int i) {
		id = i;
	}

	~Guard() {
		g_guard_dtor_count += 1;
	}
};

struct Payload {
	int value;

	Payload(int v) {
		value = v;
	}

	Payload(Payload& other) {
		g_copy_ctor_count += 1;
		value = other.value;
	}

	~Payload() {
		g_payload_dtor_count += 1;
		value = -999;
	}
};

void throwFromCatch() {
	try {
		throw 42;
	} catch (int) {
		Guard g1(1);
		Guard g2(2);
		Payload payload(10);
		throw payload;
		// Expected order before throw propagates:
		//   1. copy-construct materialized value from payload (value=10)
		//   2. ~Payload for payload
		//   3. ~Guard for g2
		//   4. ~Guard for g1
	}
}

int main() {
	try {
		throwFromCatch();
		return 1; // should not reach
	} catch (Payload& caught) {
		// Verify value was materialized before destructors ran
		if (caught.value != 10) return 2;

		// Verify each Guard destructor ran exactly once (2 guards)
		if (g_guard_dtor_count != 2) return 3;

		// Verify Payload destructor ran exactly once (the catch-local copy)
		// Note: the materialized temporary is consumed by the throw mechanism
		if (g_payload_dtor_count != 1) return 4;

		// Verify copy constructor ran exactly once (materialization)
		if (g_copy_ctor_count != 1) return 5;

		return 0;
	}

	return 6;
}
