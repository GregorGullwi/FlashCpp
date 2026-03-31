// Verify: const and non-const conversion operator overloads work for multiple template instantiations.
template <typename T>
struct Box {
	T val;
	Box(T v) : val(v) {}
	operator T() const { return val; }
	operator T() { return val + 1; }
};

int main() {
	Box<int> bi(10);
	const int ci = bi;	   // non-const: 11
	if (ci != 11)
		return 1;

	const Box<int> cbi(10);
	int ni = cbi;			  // const: 10
	if (ni != 10)
		return 2;

	Box<double> bd(1.5);
	const double cd = bd;	  // non-const: 2.5
	if (cd < 2.4 || cd > 2.6)
		return 3;

	const Box<double> cbd(1.5);
	double nd = cbd;		 // const: 1.5
	if (nd < 1.4 || nd > 1.6)
		return 4;

	return 0;
}
