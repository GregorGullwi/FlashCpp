// Regression: direct operator function-id calls inside template bodies must
// preserve their definition-bound target instead of being rebound to a later
// better-match overload.

struct Box {};

int operator^(Box, Box) {
	return 1;
}

template <class T>
int callBoundOperator() {
	return operator^(Box{}, Box{}) - 1;
}

int operator^(Box&&, Box&&) {
	return 100;
}

int main() {
	return callBoundOperator<int>();
}
