// Regression test: a normal inner-scope destructor must not force the
// Windows FH3-style prologue when the function has no try blocks and no
// function-level cleanup actions. Throwing through the frame must unwind cleanly.

int destroyed = 0;

struct Guard {
	~Guard() {
		destroyed = 1;
	}
};

[[noreturn]] void throwValue() {
	throw 42;
}

void throwThroughInnerScope() {
	{
		Guard guard;
	}

	throwValue();
}

int main() {
	try {
		throwThroughInnerScope();
		return 1;
	} catch (int value) {
		if (value != 42) {
			return 2;
		}
		if (destroyed != 1) {
			return 3;
		}
	}

	return 0;
}