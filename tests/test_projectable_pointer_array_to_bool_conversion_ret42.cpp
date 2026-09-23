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

bool ret_payload(Payload* p) {
	return p;
}

void no_op() {
}

int main() {
	int value = 0;
	Payload payload = {7, 3};
	int* value_ptr = &value;
	// Struct pointers carry the Struct category and pointer depth 0, exactly
	// like a struct object, so the conversion must come from the sema cast
	// kind, not operand metadata. Low byte is zero here.
	Payload* payload_ptr = reinterpret_cast<Payload*>(0x100);
	Payload* null_payload = nullptr;
	int rows[3] = {1, 2, 3};

	const bool int_pointer_true = value_ptr;
	const bool payload_pointer_true = payload_ptr;
	const bool null_payload_false = null_payload;
	const bool payload_ret_true = ret_payload(payload_ptr);
	const bool null_payload_ret_false = !ret_payload(null_payload);
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
	bool assigned_payload = false;
	assigned_payload = payload_ptr;

	// A projectable pointer-to-array dereference yields an array lvalue whose
	// address is the pointer value; its low byte is zero here.
	int (*array_rows)[3] = reinterpret_cast<int (*)[3]>(0x100);
	const bool pointed_array_true = *array_rows;

	const bool pointer_argument_true = consume_bool(value_ptr) == 1;
	const bool payload_argument_true = consume_bool(payload_ptr) == 1;
	const bool payload_null_argument_false = consume_bool(null_payload) == 0;
	const bool array_argument_true = consume_bool(rows) == 1;
	const bool pointed_array_argument_true = consume_bool(*array_rows) == 1;
	const bool null_argument_false = consume_bool(static_cast<int*>(nullptr)) == 0;

	if (!int_pointer_true || !payload_pointer_true || null_payload_false ||
		!payload_ret_true || !null_payload_ret_false || !array_true ||
		!literal_true || !low_byte_zero_true || null_false ||
		!function_pointer_true || null_function_pointer_false ||
		assigned_false || !assigned_true || !assigned_payload ||
		!pointed_array_true ||
		!pointer_argument_true || !payload_argument_true ||
		!payload_null_argument_false || !array_argument_true ||
		!pointed_array_argument_true || !null_argument_false) {
		return 1;
	}
	return 42;
}
