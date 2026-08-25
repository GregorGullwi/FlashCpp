int check_mixed_positions(
	int i0, double d1, int i2, double d3, int i4, double d5) {
	int mismatch = 0;
	if (i0 != 11) {
		mismatch |= 1;
	}
	if (d1 != 22.5) {
		mismatch |= 2;
	}
	if (i2 != 33) {
		mismatch |= 4;
	}
	if (d3 != 44.5) {
		mismatch |= 8;
	}
	if (i4 != 55) {
		mismatch |= 16;
	}
	if (d5 != 66.5) {
		mismatch |= 32;
	}
	return mismatch;
}

struct ConstructorPath {
	int mismatch;

	ConstructorPath(int i0, double d1, int i2, double d3, int i4, double d5)
		: mismatch(check_mixed_positions(i0, d1, i2, d3, i4, d5)) {}
};

struct DelegatingPath {
	int mismatch;

	DelegatingPath(int i0, double d1, int i2, double d3, int i4, double d5)
		: mismatch(check_mixed_positions(i0, d1, i2, d3, i4, d5)) {}

	DelegatingPath() : DelegatingPath(11, 22.5, 33, 44.5, 55, 66.5) {}
};

struct MemberPath {
	virtual int virtualCheck(
		int i0, double d1, int i2, double d3, int i4, double d5) {
		return check_mixed_positions(i0, d1, i2, d3, i4, d5);
	}

	int directCheck(
		int i0, double d1, int i2, double d3, int i4, double d5) {
		return check_mixed_positions(i0, d1, i2, d3, i4, d5);
	}
};

using MixedFunction = int (*)(int, double, int, double, int, double);

int main() {
	if (check_mixed_positions(11, 22.5, 33, 44.5, 55, 66.5) != 0) {
		return 1;
	}

	MixedFunction indirect = check_mixed_positions;
	if (indirect(11, 22.5, 33, 44.5, 55, 66.5) != 0) {
		return 2;
	}

	ConstructorPath constructed(11, 22.5, 33, 44.5, 55, 66.5);
	if (constructed.mismatch != 0) {
		return 3;
	}

	DelegatingPath delegated;
	if (delegated.mismatch != 0) {
		return 4;
	}

	MemberPath member;
	if (member.directCheck(11, 22.5, 33, 44.5, 55, 66.5) != 0) {
		return 5;
	}

#ifndef __ELF__
	MemberPath* polymorphic = &member;
	if (polymorphic->virtualCheck(11, 22.5, 33, 44.5, 55, 66.5) != 0) {
		return 6;
	}
#endif

	return 0;
}
