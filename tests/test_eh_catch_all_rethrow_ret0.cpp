// T18/T19/T21: Test catch-all, typed catch, rethrow, and mix of exception types
// Validates that TypeIndex::Invalid in catch(...) CatchBeginOp doesn't break codegen

int g_val = 0;

struct MyError {
	int code;
};

// T18: catch-all handler
int test_catch_all() {
	try {
		throw 42;
	} catch (...) {
		return 0;  // catch-all should work
	}
	return 1;
}

// T18: catch-all after typed catch
int test_catch_all_fallthrough() {
	try {
		throw 99.0;
	} catch (int) {
		return 1;  // should not catch double
	} catch (...) {
		return 0;  // catch-all should fire
	}
	return 2;
}

// T19: rethrow (throw; with no expression) - exercises IrOpcode::Rethrow path
int test_rethrow() {
	try {
		try {
			throw MyError{7};
		} catch (...) {
			throw;  // rethrow
		}
	} catch (MyError& e) {
		return e.code == 7 ? 0 : 1;
	}
	return 2;
}

// Mix: typed + catch-all in same function, different types
int test_typed_then_catchall() {
	int r = 0;
	try {
		throw 5;
	} catch (int v) {
		r = v;
	} catch (...) {
		r = -1;
	}
	return r == 5 ? 0 : 1;
}

int main() {
	if (test_catch_all() != 0)
		return 1;
	if (test_catch_all_fallthrough() != 0)
		return 2;
	if (test_rethrow() != 0)
		return 3;
	if (test_typed_then_catchall() != 0)
		return 4;
	return 0;
}
