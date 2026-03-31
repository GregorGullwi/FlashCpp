// Test that a failing for-loop init statement propagates the error
// instead of silently continuing with an unbound variable.
// This is a _fail test — compilation should fail because the
// for-loop init calls a non-constexpr function.

int runtime_value() { return 0; }

constexpr int bad_for_init() {
	int sum = 0;
	for (int i = runtime_value(); i < 3; i++) {
		sum += i;
	}
	return sum;
}

static_assert(bad_for_init() == 3);

int main() { return 0; }
