// C++20 [expr.unary.op]: the built-in address-of operator requires an lvalue.

int main() {
	int left = 1;
	int right = 2;
	int* address = &(true
		? static_cast<int&&>(left)
		: static_cast<int&&>(right));
	return *address;
}
