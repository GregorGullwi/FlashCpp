// KNOWN_ISSUES: "Declarator parsing re-checks the opening parenthesis instead of
// recursing". A parenthesized declarator that follows a pointer or reference
// return type must be owned by the recursive declarator grammar. The temporary
// re-dispatch probe only recognized '*' and 'identifier::*' after '(', so a
// reference to a function whose return type carries a pointer or reference
// (`int* (&p)()` / `int& (&p)()`) was rejected with "Expected identifier token".
//
// The three declarations below pin the lvalue-reference interleavings; a plain
// function-reference and a function-pointer control case are included so the
// fix cannot regress the shapes that already worked.

int pick_int() {
	return 7;
}

int* pick_ptr() {
	static int value = 10;
	return &value;
}

int& pick_ref() {
	static int value = 11;
	return value;
}

int main() {
	int (&plain)() = pick_int;		// reference to function returning int
	int* (&ptr_ret)() = pick_ptr;	// reference to function returning int*
	int& (&ref_ret)() = pick_ref;	// reference to function returning int&
	int* (*ptr_ptr)() = pick_ptr;	// pointer to function returning int*

	if (plain() != 7) {
		return 1;
	}
	if (*ptr_ret() != 10) {
		return 2;
	}
	if (ref_ret() != 11) {
		return 3;
	}
	if (*ptr_ptr() != 10) {
		return 4;
	}
	return 42;
}
