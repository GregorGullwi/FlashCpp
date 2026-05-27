// Regression: ordinary direct-call target recovery must treat defaulted
// parameters as viable when resolving zero-argument calls out of an overload set.

int pick(int value = 42) {
	return value;
}

long pick(long value, long extra = 1) {
	return value + extra + 1000;
}

int main() {
	return pick();
}
