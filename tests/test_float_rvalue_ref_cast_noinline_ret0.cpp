// Regression: out-of-line float&& cast return (no template / inline_always) must still
// return the reference address correctly via GPR, not SSE.
float&& cast_to_xvalue(float& x) {
	return static_cast<float&&>(x);
}

double&& cast_double_xvalue(double& x) {
	return static_cast<double&&>(x);
}

int main() {
	float f = 2.5f;
	double d = 7.25;
	float&& fr = cast_to_xvalue(f);
	double&& dr = cast_double_xvalue(d);
	fr = 1.5f;
	dr = 9.5;
	return (f == 1.5f && d == 9.5) ? 0 : 1;
}
