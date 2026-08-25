// Win64 and SysV virtual calls returning hidden-slot aggregates.
//
// A virtual function whose aggregate return needs an indirect return must pass a
// hidden slot pointer in argument position zero and shift 'this' to position one
// on both ABIs. Mixed scalar arguments then start at position two, so the call
// exercises RCX/RDI (slot), RDX/RSI ('this'), R8, XMM3, and overflow stack slots.

struct BigResult {
	long long whole;
	double fraction;
	long long sum;
};

struct IntPair {
	int first;
	int second;
};

struct Provider {
	virtual BigResult makeBig(int i0, double d1, int i2, double d3) {
		BigResult value;
		value.whole = i0 + static_cast<long long>(d1);
		value.fraction = d3 + i2;
		value.sum = i0 + i2 + static_cast<long long>(d1 + d3);
		return value;
	}

	virtual IntPair makeSmall(int seed) {
		IntPair value;
		value.first = seed;
		value.second = seed * 2;
		return value;
	}
};

struct EnhancedProvider : Provider {
	BigResult makeBig(int i0, double d1, int i2, double d3) override {
		BigResult value = Provider::makeBig(i0, d1, i2, d3);
		value.whole += 100;
		return value;
	}
};

int main() {
	EnhancedProvider derived;
	Provider* polymorphic = &derived;

	// Test 1: hidden-slot aggregate through virtual dispatch with mixed
	// positional arguments. The override adds 100 to 'whole'.
	// whole = 11+22+100 = 133; fraction = 44.5+33 = 77.5; sum = 11+33+67 = 111
	BigResult big = polymorphic->makeBig(11, 22.5, 33, 44.5);
	if (big.whole != 133 || big.fraction != 77.5 || big.sum != 111) {
		return 1;
	}

	// Test 2: the same virtual method called on the concrete object keeps
	// producing correct field layout for the returned aggregate.
	// whole = 1+2+100 = 103; fraction = 4.0+3 = 7.0; sum = 1+3+6 = 10
	BigResult direct = derived.makeBig(1, 2.0, 3, 4.0);
	if (direct.whole != 103 || direct.fraction != 7.0 || direct.sum != 10) {
		return 2;
	}

	// Test 3: register-returned small aggregate still comes back through RAX
	// after the positional changes.
	IntPair small = polymorphic->makeSmall(21);
	if (small.first != 21 || small.second != 42) {
		return 3;
	}

	// Test 4: repeated hidden-slot calls must not corrupt earlier results.
	BigResult first = polymorphic->makeBig(5, 1.5, 6, 2.5);
	if (first.whole != 106 || first.fraction != 8.5 || first.sum != 15) {
		return 4;
	}
	BigResult second = polymorphic->makeBig(50, 10.25, 20, 30.75);
	if (second.whole != 160 || second.fraction != 50.75 || second.sum != 111) {
		return 5;
	}
	if (first.whole != 106 || first.fraction != 8.5) {
		return 6;
	}

	return 0;
}
