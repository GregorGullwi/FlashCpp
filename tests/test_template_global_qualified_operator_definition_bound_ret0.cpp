// Regression: global-qualified operator function-id calls inside template
// bodies must preserve their definition-bound target instead of rebinding to a
// later better-match overload.

struct Box {};

int operator^(Box, Box) {
	return 1;
}

template <class T>
int callBoundGlobalQualifiedOperator() {
	return ::operator^(Box{}, Box{}) - 1;
}

int operator^(Box&&, Box&&) {
	return 100;
}

int main() {
	return callBoundGlobalQualifiedOperator<int>();
}
