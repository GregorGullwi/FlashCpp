// C++20 [expr.cond]/3: an array branch of a conditional expression decays to a
// pointer before the common type is chosen. A non-projectable ordered array
// whose elements are all null still designates a non-null address; testing the
// array object's bytes instead of the decayed pointer made the conditional
// false. The decayed result must also equal the source address.
long zero_block[3] = {0, 0, 0};

int main() {
	// Use a scalar anchor address so the reinterpret source is a pointer, not
	// an array lvalue that first undergoes array-to-pointer decay.
	long* anchor = &zero_block[0];
	int (*(*ordered)[3])[4] = reinterpret_cast<int (*(*)[3])[4]>(anchor);

	// Condition false: still one of the two array branches, decayed to anchor.
	if (!((1 == 0) ? *ordered : *ordered)) {
		return 1;
	}
	// Condition true: decayed to anchor.
	if (!((1 == 1) ? *ordered : *ordered)) {
		return 2;
	}
	// The decayed pointer is the array address, not the array's first element.
	void* result = (1 == 1) ? *ordered : *ordered;
	if (result != static_cast<void*>(anchor)) {
		return 3;
	}
	return 42;
}
