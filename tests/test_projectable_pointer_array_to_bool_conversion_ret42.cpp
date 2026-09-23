// C++20 [conv.bool] and [conv.array]: a projectable pointer or array lvalue
// converts to bool in initialization, assignment, and function arguments. The
// value must be tested against zero; reinterpreting the pointer as a 64-bit
// bool silently used the low address byte, so a non-null pointer whose low
// byte is zero (0x100) evaluated to false. `nullptr` is deliberately not a
// [conv.bool] source and stays rejected.
struct Payload {
	int value;
	short tag;
};

int consume_bool(bool flag) {
	return flag ? 1 : 0;
}

void no_op() {
}

int main() {
	int value = 0;
	Payload payload = {7, 3};
	int* value_ptr = &value;
	Payload* payload_ptr = &payload;
	int rows[3] = {1, 2, 3};

	const bool int_pointer_true = value_ptr;
	const bool payload_pointer_true = payload_ptr;
	const bool array_true = rows;
	const bool literal_true = "text";
	const bool low_byte_zero_true = reinterpret_cast<int*>(0x100);
	const bool null_false = static_cast<int*>(nullptr);
	const bool function_pointer_true = &no_op;
	const bool null_function_pointer_false = static_cast<void (*)()>(nullptr);

	bool assigned_false = true;
	assigned_false = static_cast<int*>(nullptr);
	bool assigned_true = false;
	assigned_true = rows;

	const bool pointer_argument_true = consume_bool(value_ptr) == 1;
	const bool payload_argument_true = consume_bool(payload_ptr) == 1;
	const bool array_argument_true = consume_bool(rows) == 1;
	const bool null_argument_false = consume_bool(static_cast<int*>(nullptr)) == 0;

	if (!int_pointer_true || !payload_pointer_true || !array_true ||
		!literal_true || !low_byte_zero_true || null_false ||
		!function_pointer_true || null_function_pointer_false ||
		assigned_false || !assigned_true ||
		!pointer_argument_true || !payload_argument_true ||
		!array_argument_true || !null_argument_false) {
		return 1;
	}
	return 42;
}
