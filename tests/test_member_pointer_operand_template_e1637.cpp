// A dependent right operand defers in the template pattern, but the
// instantiated operand type must be validated at the instantiated operator.
struct S {
	int value;
};

template <class T>
int read(S& object, T operand) {
	return object.*operand;
}

int main() {
	S object{};
	return read(object, 7);
}
