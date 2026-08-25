// A double member store whose value is a floating-point temporary must read the
// temporary from its frame slot, not from a cached register. The register
// allocator tracks floating-point temporaries in XMM slots; reusing such an
// entry as a general-purpose register encodes the wrong base register and
// stores garbage into the member.
//
// The function returns an aggregate through a hidden return slot and computes
// each member from a different argument kind (register int, register double,
// overflow-stack int, overflow-stack double), so every storage class feeds a
// member of the returned struct.

struct MixedFields {
	long long whole;
	double fraction;
	long long sum;
};

static MixedFields makeMixed(int i0, double d1, int i2, double d3) {
	MixedFields value;
	value.whole = i0 + static_cast<long long>(d1);
	value.fraction = d3 + i2;
	value.sum = i0 + i2 + static_cast<long long>(d1 + d3);
	return value;
}

int main() {
	// Positions on Win64: slot=RCX, i0=R8/position 2, d1=XMM3/position 3,
	// i2=stack position 4, d3=stack position 5.
	MixedFields value = makeMixed(11, 22.5, 33, 44.5);
	if (value.whole != 33 || value.fraction != 77.5 || value.sum != 111) {
		return 1;
	}

	MixedFields again = makeMixed(5, 1.5, 6, 2.5);
	if (again.whole != 6 || again.fraction != 8.5 || again.sum != 15) {
		return 2;
	}

	if (value.whole != 33 || value.fraction != 77.5) {
		return 3;
	}

	return 0;
}
