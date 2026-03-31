// Phase 23 regression: user-defined operator bool() in contextual bool contexts.
// Tests that applyConditionBoolConversion calls operator bool() via sema UserDefined
// annotation rather than testing raw struct bits.
// Key case: value 0xDEAD is truthy raw but operator bool() returns false.

struct Flag {
	int value;
	operator bool() const { return value == 42; }
};

struct BoxedDouble {
	double val;
 // Non-zero double that should be "false" logically
	operator bool() const { return val > 100.0; }
};

static int test_if_condition() {
	Flag truthy{42};
	Flag falsy{0xDEAD};	// raw int is truthy but operator bool() => false

	int result = 0;
	if (truthy)
		result += 1;	 // should execute
	if (!falsy)
		result += 2;	 // !false = true, should execute
	if (falsy)
		return 10;	   // should NOT execute
	if (!truthy)
		return 20;	   // should NOT execute
	return result - 3;		   // expect 0
}

static int test_while_condition() {
	Flag flag{42};
	int count = 0;
	while (flag) {
		count++;
		flag.value = 0;	// operator bool() now returns false
	}
	return count - 1;  // expect 0 (loop runs once)
}

static int test_ternary_condition() {
	Flag truthy{42};
	Flag falsy{0xDEAD};
	int a = truthy ? 1 : 0;	// expect 1
	int b = falsy ? 1 : 0;   // expect 0
	return (a - 1) + b;		// expect 0
}

static int test_logical_not() {
	Flag truthy{42};
	Flag falsy{0xDEAD};
	bool a = !truthy;  // expect false (0)
	bool b = !falsy;	 // expect true  (1)
	return (a ? 1 : 0) + (b ? 0 : 1);  // expect 0
}

static int test_double_struct_cond() {
	BoxedDouble lo{0.5};	 // 0.5 > 100 = false
	BoxedDouble hi{200.0}; // 200 > 100 = true
	int result = 0;
	if (hi)
		result += 1;
	if (!lo)
		result += 2;
	return result - 3;  // expect 0
}

int main() {
	int r;
	r = test_if_condition();
	if (r != 0)
		return r + 100;
	r = test_while_condition();
	if (r != 0)
		return r + 200;
	r = test_ternary_condition();
	if (r != 0)
		return r + 300;
	r = test_logical_not();
	if (r != 0)
		return r + 400;
	r = test_double_struct_cond();
	if (r != 0)
		return r + 500;
	return 0;
}
