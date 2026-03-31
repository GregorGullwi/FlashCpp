// Phase 20: test implicit arithmetic conversions in member function call arguments.
// Every member call below requires an implicit arithmetic conversion on one or
// more arguments (C++20 [conv.arith]).  The program must return 0.
//
// NOTE: Compound-assignment (+=) inside member bodies is intentionally avoided
// because that is a separate, pre-existing feature gap.  Each test uses plain
// assignment inside the method.

struct Setter {
	float fval;
	int ival;
	void setF(float f) { fval = f; }
	void setI(int i) { ival = i; }
	float getF() const { return fval; }
	int getI() const { return ival; }
};

struct DblHolder {
	double val;
	void set(double v) { val = v; }
	double get() const { return val; }
};

struct IntHolder {
	int count;
	void set(int n) { count = n; }
	int val() const { return count; }
};

int main() {
	// 1. int literal -> float parameter
	Setter s1;
	s1.fval = 0.0f;
	s1.setF(4);			// int -> float
	if ((int)s1.getF() != 4)
		return 1;

	// 2. float variable -> double parameter
	DblHolder d1;
	d1.val = 0.0;
	float fv = 1.5f;
	d1.set(fv);			// float -> double
	// 1.5 * 2 = 3.0
	if ((int)(d1.get() * 2.0 + 0.5) != 3)
		return 2;

	// 3. char variable -> int parameter
	IntHolder c1;
	c1.count = 0;
	char ch = 7;
	c1.set(ch);			// char -> int
	if (c1.val() != 7)
		return 3;

	// 4. short variable -> int parameter
	IntHolder c2;
	c2.count = 0;
	short sh = 10;
	c2.set(sh);			// short -> int
	if (c2.val() != 10)
		return 4;

	// 5. int variable -> float parameter
	Setter s2;
	s2.fval = 0.0f;
	int iv = 9;
	s2.setF(iv);			 // int -> float
	if ((int)s2.getF() != 9)
		return 5;

	// 6. double variable -> float parameter
	Setter s3;
	s3.fval = 0.0f;
	double dv = 2.0;
	s3.setF(dv);			 // double -> float
	if ((int)s3.getF() != 2)
		return 6;

	// 7. float variable -> int parameter (truncation)
	Setter s4;
	s4.ival = 0;
	float fv2 = 9.0f;
	s4.setI(fv2);		  // float -> int
	if (s4.getI() != 9)
		return 7;

	// 8. int literal -> double parameter
	DblHolder d2;
	d2.val = 0.0;
	d2.set(3);			   // int -> double
	if ((int)d2.get() != 3)
		return 8;

	// 9. int literal -> double, multiple calls
	DblHolder d3;
	d3.val = 0.0;
	d3.set(7);			   // int -> double
	if ((int)d3.get() != 7)
		return 9;

	return 0;
}
