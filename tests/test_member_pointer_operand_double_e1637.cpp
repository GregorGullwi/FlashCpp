// A floating-point right operand is never a pointer-to-member; the operator
// must diagnose at the operator token rather than fail during IR conversion.
struct S {
	double value;
};

int read(S& object, double operand) {
	return static_cast<int>(object.*operand);
}

int main() {
	S object{};
	return read(object, 1.5);
}
