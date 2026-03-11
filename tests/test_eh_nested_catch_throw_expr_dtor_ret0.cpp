// Regression test: throw <expr> from inside a nested catch body must
// materialize the thrown value before running catch-local destructors,
// and must only destroy the innermost catch's locals before the outer
// catch unwinds normally.

int g_outer_guard_dtor = 0;
int g_inner_guard_dtor = 0;
int g_payload_dtor = 0;
int g_copy_ctor = 0;

struct OuterGuard {
	~OuterGuard() {
		g_outer_guard_dtor += 1;
	}
};

struct InnerGuard {
	~InnerGuard() {
		g_inner_guard_dtor += 1;
	}
};

struct Payload {
	int value;
	Payload(int v) {
		value = v;
	}
	Payload(Payload& other) {
		g_copy_ctor += 1;
		value = other.value;
	}
	~Payload() {
		g_payload_dtor += 1;
		value = -999;
	}
};

int main() {
	int catch_result = 0;
	try {
		try {
			throw 1;
		} catch (int) {
			OuterGuard og;
			try {
				throw 2;
			} catch (int) {
				InnerGuard ig;
				Payload payload(42);
				throw payload;
			}
		}
	} catch (Payload& caught) {
		if (caught.value != 42) catch_result = 1;
		if (g_inner_guard_dtor != 1) catch_result = 2;
		if (g_payload_dtor != 1) catch_result = 3;
		if (g_copy_ctor != 1) catch_result = 4;
		if (g_outer_guard_dtor != 1) catch_result = 5;
	}

	if (catch_result != 0) return catch_result;
	if (g_payload_dtor != 2) return 6;
	return 0;
}