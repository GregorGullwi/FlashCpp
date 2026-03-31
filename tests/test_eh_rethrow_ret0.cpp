// test_eh_rethrow_ret0.cpp
// Regression test: throw; (rethrow) must propagate to the outer catch handler.
// Previously, the LSDA did not cover the catch handler body and emitSubRSP(8)
// before __cxa_rethrow misaligned the stack, causing SIGSEGV / std::terminate.

// Test 1: cross-function rethrow via catch-all
void rethrower_catchall() {
	try {
		throw 42;
	} catch (...) {
		throw;
	}
}

// Test 2: typed inner catch rethrows, outer catch receives it
void rethrower_typed() {
	try {
		throw 42;
	} catch (int) {
		throw;
	}
}

// Test 3: nested try in a single function - inner catch rethrows, outer catches
int nested_rethrow() {
	try {
		try {
			throw 42;
		} catch (int) {
			throw;
		}
	} catch (int e) {
		return e;
	}
	return -1;
}

int main() {
	// Test 1
	int r1 = -1;
	try {
		rethrower_catchall();
	} catch (...) {
		r1 = 0;
	}
	if (r1 != 0)
		return 1;

	// Test 2
	int r2 = -1;
	try {
		rethrower_typed();
	} catch (int e) {
		r2 = (e == 42) ? 0 : 2;
	}
	if (r2 != 0)
		return 2;

	// Test 3
	if (nested_rethrow() != 42)
		return 3;

	return 0;
}
