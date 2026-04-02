// Regression test: free-function template substitution must preserve
// function_signature for function-pointer template arguments in both
// deduced and explicit instantiation paths. This intentionally avoids
// calling the function-pointer parameter because that still hits a
// separate lowered-call bug documented in KNOWN_ISSUES.md.

int square(int x) {
	return x * x;
}

int double_it(int x) {
	return x * 2;
}

template <typename F>
int apply(F fn, int x) {
	return fn ? x : 0;
}

template <typename F>
F identity(F fn) {
	return fn;
}

int main() {
	int deduced = apply(square, 3);
	int explicit_param = apply<int (*)(int)>(double_it, 5);
	int (*rebound)(int) = identity<int (*)(int)>(square);
	return (deduced == 3 && explicit_param == 5 && rebound == square) ? 0 : 1;
}
