// KNOWN_ISSUES: "Declarator parsing re-checks the opening parenthesis instead of
// recursing", nested parenthesized groups. The recursive declarator grammar owns
// a redundant inner group wherever `parse_type_and_name` used to re-dispatch the
// first `(`. The probe previously recognized only a single ptr-operator after
// `(`, so `int* ((*p))()` and a redundant group around a name such as
// `int* ((getVal))()` were rejected with "Expected identifier token".
//
// These declarations pin both the nested pointer-to-function object and the
// redundant-name function declaration, with and without a pointer return type.

int pick_int() {
	return 7;
}

int* pick_ptr() {
	static int value = 10;
	return &value;
}

int ((named_fn))() {
	return 3;
}

int* ((named_ptr_fn))() {
	static int value = 5;
	return &value;
}

int main() {
	int ((*nested_fn))() = pick_int;		// redundant group around a function pointer
	int* ((*nested_ptr_fn))() = pick_ptr;	// ... with a pointer return type

	if (nested_fn() != 7) {
		return 1;
	}
	if (*nested_ptr_fn() != 10) {
		return 2;
	}
	if (named_fn() != 3) {
		return 3;
	}
	if (*named_ptr_fn() != 5) {
		return 4;
	}
	return 42;
}
