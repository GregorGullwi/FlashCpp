// The arrow form has the same right-operand requirement as the dot form. A
// plain pointer parameter is conclusively not a pointer-to-member.
struct S {
	int value;
};

int read(S* object, int* operand) {
	return object->*operand;
}

int main() {
	S object{};
	int value = 0;
	return read(&object, &value);
}
