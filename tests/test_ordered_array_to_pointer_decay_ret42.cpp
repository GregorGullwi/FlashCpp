// C++20 [conv.array]: a non-projectable array lvalue decays to a pointer to its
// element type. The array is the pointee of an ordered pointer, so the test
// does not declare an array object that IR still refuses to lower.
struct Payload {
	int value;
	short tag;
};

int consume_int(int (*(*(*param))[3])[4]) {
	return param == nullptr ? 1 : 0;
}

int consume_double(double (*(*(*param))[2])[5]) {
	return param == nullptr ? 2 : 0;
}

int consume_payload(Payload (*(*(*param))[3])[4]) {
	void* bits = param;
	return bits == nullptr ? 4 : 0;
}

int consume_void(void* param) {
	return param == nullptr ? 8 : 0;
}

int consume_outer_const(int (*(*(* const param))[3])[4]) {
	return param == nullptr ? 16 : 0;
}

int main() {
	int (*(*(*int_rows)[2])[3])[4] = nullptr;
	double (*(*(*double_rows)[4])[2])[5] = nullptr;
	Payload (*(*(*payload_rows)[2])[3])[4] = nullptr;
	const int score = consume_int(*int_rows) + consume_double(*double_rows) +
		consume_payload(*payload_rows) + consume_void(*int_rows) +
		consume_outer_const(*int_rows);
	return score == 31 ? 42 : score;
}
