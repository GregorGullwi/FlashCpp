// C++20 [expr.cond]/3: an array branch of a conditional expression decays to a
// pointer before the common type is chosen. A non-projectable ordered array
// whose elements are all null still designates a non-null address; testing the
// array object's bytes instead of the decayed pointer made the conditional
// false. The result must also equal the array's address.
long zero_block[3] = {0, 0, 0};

int main() {
	int (*(*ordered)[3])[4] =
		reinterpret_cast<int (*(*)[3])[4]>(zero_block);
	void* result = (1 == 1) ? *ordered : *ordered;
	const bool true_address = (1 == 1) ? *ordered : *ordered;
	const bool false_address = (1 == 0) ? *ordered : *ordered;
	return (result == static_cast<void*>(zero_block) && true_address &&
			false_address)
		? 42
		: 1;
}
