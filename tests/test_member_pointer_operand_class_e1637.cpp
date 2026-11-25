// A class without a conversion function to a pointer-to-member is not a valid
// right operand, including through the arrow form.
struct S {
	int value;
};

struct NoConversion {
	int payload;
};

int read(S* object, NoConversion operand) {
	return object->*operand;
}

int main() {
	S object{};
	return read(&object, NoConversion{4});
}
