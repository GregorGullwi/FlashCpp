// Regression: postfix address-of function calls inside template bodies must
// preserve their definition-bound target instead of rebinding to a later
// overload with the same name.

struct Box {};

int target(Box) {
	return 1;
}

template <class T>
int callBoundAddress() {
	return (&target)(Box{}) - 1;
}

int target(Box&&) {
	return 100;
}

int main() {
	return callBoundAddress<int>();
}
