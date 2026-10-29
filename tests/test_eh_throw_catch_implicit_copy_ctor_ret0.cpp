// Throwing and catching a class by value ODR-uses its implicit copy
// constructor. Neither path emits a ConstructorCall instruction, so the
// definition must be published by semantic analysis rather than discovered
// during codegen.
struct Payload {
	int first;
	int second;
};

int main() {
	try {
		Payload thrown;
		thrown.first = 21;
		thrown.second = 24;
		throw thrown;
	} catch (Payload caught) {
		return caught.first + caught.second - 45;
	}
	return 1;
}
