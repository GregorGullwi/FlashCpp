// Regression: builtin floating calls in return statements still perform the
// required arithmetic conversion to the function's declared long double return type.
extern "C" double __builtin_huge_val();
extern "C" double __builtin_nan(const char*);
extern "C" double __builtin_nans(const char*);

long double builtinInfinity() {
	return __builtin_huge_val();
}

long double builtinQuietNan() {
	return __builtin_nan("0");
}

long double builtinSignalingNan() {
	return __builtin_nans("1");
}

int main() {
	const bool infinity_ok = builtinInfinity() > 1.0L;
	const bool quiet_nan_ok = builtinQuietNan() != 0.0L;
	const bool signaling_nan_ok = builtinSignalingNan() != 0.0L;
	return (infinity_ok && quiet_nan_ok && signaling_nan_ok) ? 0 : 1;
}
