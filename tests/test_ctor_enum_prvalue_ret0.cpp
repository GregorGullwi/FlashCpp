// Test: enum constants stay prvalues in sema-owned constructor argument typing.
// If sema marks the enumerator as an lvalue, overload resolution picks the
// deleted const-reference overload instead of the rvalue-reference overload.
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
