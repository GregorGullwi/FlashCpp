// C++20 [conv.ptr]: an ordered object pointer converts to cv void*, with the
// outer pointee cv (arrays are transparent) required to be a subset of void's
// cv. [conv.qual] qualification over the ordered chain is also exercised.
int (*(*int_value)[3])[4] = nullptr;
double (*(*double_value)[2])[3] = nullptr;
int (* const (*inner_const_value)[3])[4] = nullptr;

int consume_void(void* p) { return p == nullptr ? 1 : 0; }
int consume_cvoid(const void* p) { return p == nullptr ? 1 : 0; }
int consume_vvoid(volatile void* p) { return p == nullptr ? 1 : 0; }
int consume_inner_const(int (* const (*)[3])[4]) { return 1; }
int consume_both_const(const int (* const (*)[3])[4]) { return 1; }

int main() {
	if (consume_void(int_value) != 1) {
		return 1;
	}
	if (consume_cvoid(int_value) != 1) {
		return 2;
	}
	if (consume_vvoid(double_value) != 1) {
		return 3;
	}
	if (consume_inner_const(int_value) != 1) {
		return 4;
	}
	if (consume_both_const(int_value) != 1) {
		return 5;
	}
	if (consume_cvoid(inner_const_value) != 1) {
		return 6;
	}
	return 42;
}
