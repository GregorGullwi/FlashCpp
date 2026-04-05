// Test: unqualified enumerators remain prvalues for constructor overload resolution.
// If overload typing treats Seven as an lvalue, the deleted const-reference
// overload wins instead of the rvalue-reference overload.
enum Number {
	Seven = 7
};

struct Sink {
	int value;

	Sink(const Number&) = delete;
	Sink(Number&& number)
		: value((int)number) {}
};

int main() {
	Sink sink(Seven);
	return sink.value - 7;
}
