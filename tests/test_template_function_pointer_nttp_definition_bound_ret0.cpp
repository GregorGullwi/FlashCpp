// Regression: NTTP function-pointer calls inside template bodies must preserve
// their concrete target instead of being rebound through a later same-name
// overload.

int target(int) {
	return 1;
}

template <int (*F)(int)>
int callThroughPointerTemplate() {
	return F(0) - 1;
}

long target(long) {
	return 100;
}

int main() {
	return callThroughPointerTemplate<&target>();
}
