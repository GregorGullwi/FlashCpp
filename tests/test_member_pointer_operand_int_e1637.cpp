// A pointer-to-member operator requires a pointer-to-member right operand.
// An int operand has no member identity at all, so sema must report the
// operand diagnostic instead of letting lowering fail on a missing pointee.
struct S {
	int value;
};

int read(S& object, int operand) {
	return object.*operand;
}

int main() {
	S object{};
	return read(object, 3);
}
