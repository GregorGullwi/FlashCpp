// operator() and static __invoke are emitted with different temporary numbering.
// The static entry must handle a local reference from the shared lambda body.
int main() {
	int (*invoke_function)() = []() {
		int value = 42;
		int& reference = value;
		return reference;
	};
	return invoke_function() - 42;
}