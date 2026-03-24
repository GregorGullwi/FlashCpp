// Regression test: const reference binding to prvalue (temporary materialisation)
// Bug: 64-bit function return value (long long) was misidentified as a pointer because
// isIrIntegerType matched and size==64 → MOV was emitted, treating the value as an address
// and causing a SIGSEGV when the reference was later dereferenced.
// Also covers: double, nested scopes, and loops.

long long get_ll() { return 42LL; }
int get_int() { return 17; }
double get_double() { return 3.5; }

int main() {
	// ---- long long: const ref binds to materialised temporary ----
	const long long& r_ll = get_ll();
	if (r_ll != 42LL) return 1;

	// ---- int: const ref binds to materialised temporary ----
	const int& r_int = get_int();
	if (r_int != 17) return 2;

	// ---- double: const ref binds to materialised temporary ----
	const double& r_dbl = get_double();
	if (r_dbl != 3.5) return 3;

	// ---- chained ref-to-ref must share the same object ----
	int x = 55;
	const int& r1 = x;
	const int& r2 = r1;
	if (r2 != 55) return 6;
	x = 66;
	if (r1 != 66) return 7;
	if (r2 != 66) return 8;

	// ---- double chained ref-to-ref ----
	double dv = 9.9;
	const double& dr1 = dv;
	const double& dr2 = dr1;
	if (dr2 != 9.9) return 9;
	dv = 1.1;
	if (dr2 != 1.1) return 10;

	// ---- nested code block: prvalue binding ----
	{
		const long long& r_block = get_ll();
		if (r_block != 42LL) return 11;
		const double& rd_block = get_double();
		if (rd_block != 3.5) return 12;
	}

	// ---- for loop: prvalue binding inside loop ----
	int loop_sum = 0;
	for (int n = 0; n < 3; ++n) {
		const int& r_loop = get_int();   // prvalue temp each iteration
		loop_sum += r_loop;
	}
	if (loop_sum != 51) return 13;  // 3 * 17

	// ---- while loop: prvalue double binding inside loop ----
	double dsum = 0.0;
	int wcount = 3;
	while (wcount > 0) {
		const double& rd_loop = get_double();
		dsum += rd_loop;
		--wcount;
	}
	if (dsum < 10.4 || dsum > 10.6) return 14;  // 3 * 3.5 == 10.5

	return 0;
}
