// Regression: zero-argument direct calls must still participate in ordinary
// overload resolution, so overlapping no-arg/default-arg overloads remain
// ambiguous per C++20.

void pick() {}
void pick(int value = 0) {}

int main() {
	pick();
	return 0;
}
