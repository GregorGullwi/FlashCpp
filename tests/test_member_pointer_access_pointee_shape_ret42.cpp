// [expr.mptr.oper]: object.*pointer designates the member, so the access type is
// the member's type with the member-pointer level removed. These shapes pin that
// peel: a byte member keeps its own size, a scalar member is writable through the
// access, and a pointer member keeps its pointee so a reference can bind to it.
struct Box {
	char c;
	double d;
	int* p;
};

int main() {
	Box box{};
	box.c = 5;
	box.d = 2.5;
	if (sizeof(box.*(&Box::c)) != 1) return 1;
	if (sizeof(box.*(&Box::d)) != 8) return 2;
	box.*(&Box::d) = 4.5;
	if (box.d != 4.5) return 3;
	int x = 9;
	box.p = &x;
	int*& ref = box.*(&Box::p);
	if (*ref != 9) return 4;
	return 42;
}
