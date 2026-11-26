// Pointer leaves inside a brace-elided base and in the derived part must each
// receive a relocation at their own layout offset. The base pointer sits at
// offset zero, the derived pointer after the base subobject.

int x = 41;
int y = 43;

struct PointerBase {
	int* pointer;
	int tag;
};

struct PointerDerived : PointerBase {
	int* tail_pointer;
	int tail;
};

PointerDerived pointers{&x, 51, &y, 53};

int main() {
	if (pointers.pointer != &x) return 1;
	if (*pointers.pointer != 41) return 2;
	if (pointers.tag != 51) return 3;
	if (pointers.tail_pointer != &y) return 4;
	if (*pointers.tail_pointer != 43) return 5;
	if (pointers.tail != 53) return 6;
	*pointers.pointer = 47;
	*pointers.tail_pointer = 49;
	if (x != 47) return 7;
	if (y != 49) return 8;
	return 0;
}
