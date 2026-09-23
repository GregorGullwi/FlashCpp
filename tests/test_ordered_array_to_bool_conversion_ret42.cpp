// C++20 [conv.array] followed by [conv.bool]: a non-projectable array lvalue
// decays to a pointer and then converts to bool, and a non-projectable ordered
// pointer object converts to bool directly. The ordered spine has no flat
// pointer or array fields, so the conversion plan used to fail closed and
// reject both the initialization and the call. The boolean conversion must test
// the decoded address against zero at pointer width: an ordered array
// dereference carries the element size, so a numeric truncation took only the
// low address byte and made a non-null address with a zero low byte false.
struct Payload {
	int value;
	short tag;
};

int storage_int = 0;
Payload storage_payload = {7, 3};

int consume_bool(bool flag) {
	return flag ? 1 : 0;
}

int main() {
	int (*(*int_rows)[3])[4] =
		reinterpret_cast<int (*(*)[3])[4]>(&storage_int);
	Payload (*(*payload_rows)[2])[5] =
		reinterpret_cast<Payload (*(*)[2])[5]>(&storage_payload);
	int (*(*null_rows)[3])[4] = nullptr;
	// Low address byte is zero while the pointer is non-null.
	int (*(*low_byte_zero)[3])[4] =
		reinterpret_cast<int (*(*)[3])[4]>(0x100);

	const bool int_pointer_true = int_rows;
	const bool int_array_true = *int_rows;
	const bool payload_pointer_true = payload_rows;
	const bool payload_array_true = *payload_rows;
	const bool null_pointer_false = null_rows;
	const bool null_array_false = *null_rows;
	const bool low_byte_zero_pointer_true = low_byte_zero;
	const bool low_byte_zero_array_true = *low_byte_zero;

	const bool int_argument_true = consume_bool(int_rows) == 1;
	const bool array_argument_true = consume_bool(*int_rows) == 1;
	const bool null_argument_false = consume_bool(null_rows) == 0;
	const bool low_byte_zero_pointer_argument_true = consume_bool(low_byte_zero) == 1;
	const bool low_byte_zero_array_argument_true = consume_bool(*low_byte_zero) == 1;

	if (!int_pointer_true || !int_array_true || !payload_pointer_true ||
		!payload_array_true || null_pointer_false || null_array_false ||
		!low_byte_zero_pointer_true || !low_byte_zero_array_true ||
		!int_argument_true || !array_argument_true || !null_argument_false ||
		!low_byte_zero_pointer_argument_true ||
		!low_byte_zero_array_argument_true) {
		return 1;
	}
	return 42;
}
