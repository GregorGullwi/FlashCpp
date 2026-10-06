// The conversion temporary is kept alive across enough later temporaries to
// place it next to a fifth argument unless both regions are in the frame plan.
int writeThroughReference(int first, int second, int third, int fourth, int&& value) {
	int old_value = value;
	value = 456;
	return old_value;
}

int testConvertedOutgoingFrame() {
	volatile float source = 123.0f;
	volatile unsigned long long wide_source = 123;
	int prefix = 0;
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	prefix += static_cast<int>(source);
	prefix += static_cast<int>(wide_source);
	int&& converted = static_cast<int>(source);
	int padding = 0;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	padding += 1;
	int observed = writeThroughReference(1, 2, 3, 4, static_cast<int&&>(converted));
	if (prefix != 5904 || padding != 41) {
		return 1;
	}
	return observed - 123 + converted - 456;
}

int testLiteralReference() {
	int&& literal = 789;
	int observed = writeThroughReference(1, 2, 3, 4, static_cast<int&&>(literal));
	return observed - 789 + literal - 456;
}

int main() {
	return testConvertedOutgoingFrame() + testLiteralReference();
}
