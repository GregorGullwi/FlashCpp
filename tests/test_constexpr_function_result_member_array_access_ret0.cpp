struct CtorBox {
	int data[2];

	constexpr CtorBox(int first, int second)
		: data{first, second} {}
};

constexpr CtorBox makeCtorBox() {
	return CtorBox(40, 2);
}

constexpr int read_function_result_member_array() {
	return makeCtorBox().data[0] + makeCtorBox().data[1];
}

constexpr int read_temporary_member_array() {
	return CtorBox(40, 2).data[0] + CtorBox(0, 2).data[1];
}

static_assert(read_function_result_member_array() == 42);
static_assert(read_temporary_member_array() == 42);
static_assert(CtorBox{7, 9}.data[1] == 9);

int main() {
	return 0;
}
