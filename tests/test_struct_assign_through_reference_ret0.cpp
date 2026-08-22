// Reduced regression for "Struct assignment through reference parameters is
// wrong for types larger than 8 bytes" (see docs/KNOWN_ISSUES.md):
// implicit copy-assignment whose operands are reference parameters must
// dereference both sides and copy every member, for any struct size.
// A correct C++ compiler returns 0; each violated expectation returns a
// distinct nonzero code so the exact failure mode (direct vs through-ref,
// and which size) is visible from the exit code alone.

struct Bytes2 {
	short a;
};

struct Bytes4 {
	int a;
};

struct Bytes8 {
	long long a;
};

struct Bytes9 {
	long long a;
	char b;
};

struct Bytes12 {
	int v0;
	int v1;
	int v2;
};

struct Bytes16 {
	int v0;
	int v1;
	int v2;
	int v3;
};

template <class Type>
void assignThroughRef(Type& left, const Type& right) {
	left = right;
}

template <class Type>
void swapThroughRef(Type& left, Type& right) {
	Type tmp = left;
	left = right;
	right = tmp;
}

int main() {
	// Native scalar sanity: assignment through reference parameters.
	{
		long long left = 11;
		const long long right = 22;
		assignThroughRef(left, right);
		if (left != 22 || right != 22) return 1;
	}
	{
		short left = 3;
		const short right = 4;
		assignThroughRef(left, right);
		if (left != 4 || right != 4) return 2;
	}
	{
		int left = 5;
		const int right = 6;
		assignThroughRef(left, right);
		if (left != 6 || right != 6) return 3;
	}

	// Direct object-to-object struct assignment (baseline bisect).
	{
		Bytes2 left{7};
		const Bytes2 right{8};
		left = right;
		if (left.a != 8 || right.a != 8) return 10;
	}
	{
		Bytes4 left{9};
		const Bytes4 right{10};
		left = right;
		if (left.a != 10 || right.a != 10) return 11;
	}
	{
		Bytes8 left{12};
		const Bytes8 right{13};
		left = right;
		if (left.a != 13 || right.a != 13) return 12;
	}
	{
		Bytes9 left{14, 'x'};
		const Bytes9 right{15, 'y'};
		left = right;
		if (left.a != 15 || left.b != 'y' || right.a != 15 || right.b != 'y') return 13;
	}
	{
		Bytes12 left{16, 17, 18};
		const Bytes12 right{19, 20, 21};
		left = right;
		if (left.v0 != 19 || left.v1 != 20 || left.v2 != 21 ||
			right.v0 != 19 || right.v1 != 20 || right.v2 != 21) return 14;
	}
	{
		Bytes16 left{22, 23, 24, 25};
		const Bytes16 right{26, 27, 28, 29};
		left = right;
		if (left.v0 != 26 || left.v1 != 27 || left.v2 != 28 || left.v3 != 29 ||
			right.v0 != 26 || right.v1 != 27 || right.v2 != 28 || right.v3 != 29) return 15;
	}

	// Struct assignment through reference parameters (the reported bug).
	{
		Bytes2 left{30};
		const Bytes2 right{31};
		assignThroughRef(left, right);
		if (left.a != 31 || right.a != 31) return 20;
	}
	{
		Bytes4 left{32};
		const Bytes4 right{33};
		assignThroughRef(left, right);
		if (left.a != 33 || right.a != 33) return 21;
	}
	{
		Bytes8 left{34};
		const Bytes8 right{35};
		assignThroughRef(left, right);
		if (left.a != 35 || right.a != 35) return 22;
	}
	{
		Bytes9 left{36, 'a'};
		const Bytes9 right{37, 'b'};
		assignThroughRef(left, right);
		if (left.a != 37 || left.b != 'b' || right.a != 37 || right.b != 'b') return 23;
	}
	{
		Bytes12 left{38, 39, 40};
		const Bytes12 right{41, 42, 43};
		assignThroughRef(left, right);
		if (left.v0 != 41 || left.v1 != 42 || left.v2 != 43 ||
			right.v0 != 41 || right.v1 != 42 || right.v2 != 43) return 24;
	}
	{
		Bytes16 left{44, 45, 46, 47};
		const Bytes16 right{48, 49, 50, 51};
		assignThroughRef(left, right);
		if (left.v0 != 48 || left.v1 != 49 || left.v2 != 50 || left.v3 != 51 ||
			right.v0 != 48 || right.v1 != 49 || right.v2 != 50 || right.v3 != 51) return 25;
	}

	// Full swap pattern through references (the original failing shape:
	// Type tmp = left; left = right; right = tmp;).
	{
		Bytes8 left{52};
		Bytes8 right{53};
		swapThroughRef(left, right);
		if (left.a != 53 || right.a != 52) return 30;
	}
	{
		Bytes9 left{54, 'c'};
		Bytes9 right{55, 'd'};
		swapThroughRef(left, right);
		if (left.a != 55 || left.b != 'd' || right.a != 54 || right.b != 'c') return 31;
	}
	{
		Bytes16 left{56, 57, 58, 59};
		Bytes16 right{60, 61, 62, 63};
		swapThroughRef(left, right);
		if (left.v0 != 60 || left.v1 != 61 || left.v2 != 62 || left.v3 != 63 ||
			right.v0 != 56 || right.v1 != 57 || right.v2 != 58 || right.v3 != 59) return 32;
	}

	return 0;
}
