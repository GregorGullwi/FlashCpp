// Test __complex__, __real__, __imag__, and _Imaginary keyword support
// These are GCC extensions used in glibc headers and libstdc++ <complex>

__complex__ double z1;
_Complex double z2;
_Imaginary double z3;

__complex__ float f1;
_Complex float f2;

int main() {
	// Test that these declarations parse correctly
	z1 = 1.0;
	z2 = 2.0;
	z3 = 3.0;

	// Test __real__ and __imag__ operators (used in libstdc++ <complex>)
	double r = __real__ z1;
	double i = __imag__ z1;

	// FlashCpp doesn't support complex arithmetic yet, so just return success
	return 0;
}
