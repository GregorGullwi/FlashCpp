// C++20 [conv.func]: a function whose return type is a non-projectable
// pointer/array interleaving is a function object. Using it as a value decays
// to a pointer to that function. The flat function-pointer category cannot
// represent the return spine, so the decay has to prepend a pointer on the
// ordered declarator.
struct Payload {
	int value;
	short tag;
};

int int_anchor = 1;
Payload payload_anchor = {7, 1};

int (*(*make_int(int scale))[2])[3] {
	return reinterpret_cast<int (*(*)[2])[3]>(scale == 0 ? &int_anchor : &int_anchor);
}

Payload (*(*make_payload(int scale))[2])[3] {
	return reinterpret_cast<Payload (*(*)[2])[3]>(scale == 0 ? &payload_anchor : &payload_anchor);
}

int main() {
	int (*(*(*int_fn)(int))[2])[3] = make_int;
	Payload (*(*(*payload_fn)(int))[2])[3] = make_payload;
	int (*(*(*null_fn)(int))[2])[3] = nullptr;

	const bool int_fn_true = int_fn;
	const bool payload_fn_true = payload_fn;
	const bool null_fn_false = null_fn;
	const bool int_function_true = make_int;
	const bool payload_function_true = make_payload;

	int (*(*direct_int)[2])[3] = make_int(4);
	Payload (*(*direct_payload)[2])[3] = make_payload(4);
	const bool direct_int_true = direct_int;
	const bool direct_payload_true = direct_payload;

	if (!int_fn_true || !payload_fn_true || null_fn_false ||
		!int_function_true || !payload_function_true ||
		!direct_int_true || !direct_payload_true) {
		return 1;
	}
	return 42;
}
