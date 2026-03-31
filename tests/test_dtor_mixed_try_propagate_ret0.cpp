// Test: destructor of function-scope variable called when exception propagates
// through a function that ALSO has a try/catch block (mixed case).
// Bug: cleanup could be skipped when no typed catch matched.

struct Counter {
	int* ptr;
	Counter(int* p) : ptr(p) { (*ptr)++; }
	~Counter() { (*ptr)--; }
};

int g_alive = 0;

void thrower() {
	throw 1.0f;	// float — not matched by catch(int) below
}

void middle() {
	Counter c(&g_alive);	 // must be destroyed when exception propagates
	try {
		thrower();
	} catch (int) {
	// doesn't match float — exception escapes middle()
	}
}

int main() {
	try {
		middle();
	} catch (...) {
	// catch the float
	}
	return g_alive;	// 0 if ~Counter was called, 1 if not
}