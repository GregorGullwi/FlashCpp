// `int* C::*` is a member pointer whose pointee is `int*`, not a pointer to a
// member pointer. The member offset and the pointee indirection are both
// exercised, so a declarator that binds the `*` head wrongly reads or writes a
// different member.
struct C {
	int* first;
	int* second;
	int tail;
};

int* C::* pointer_second = &C::second;
int C::* tail_member = &C::tail;
typedef int* IntPointer;
IntPointer C::* alias_pointer_second = &C::second;

int main() {
	int alpha = 11;
	int beta = 22;
	C object = {&alpha, &beta, 7};
	int* C::* local_second = &C::second;

	if (*(object.*pointer_second) != 22) {
		return 1;
	}
	if (*(object.*local_second) != 22) {
		return 2;
	}
	if (*(object.*alias_pointer_second) != 22) {
		return 3;
	}
	if ((object.*tail_member) != 7) {
		return 4;
	}
	*(object.*pointer_second) = 44;
	if (beta != 44) {
		return 5;
	}
	object.*tail_member = 9;
	if ((object.*tail_member) != 9) {
		return 6;
	}
	object.*alias_pointer_second = &alpha;
	if (*(object.*alias_pointer_second) != 11) {
		return 7;
	}
	*(object.*alias_pointer_second) = 55;
	if (alpha != 55) {
		return 8;
	}
	return (object.*tail_member) == 9 ? 0 : 9;
}
