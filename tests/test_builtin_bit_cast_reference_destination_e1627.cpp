// A reference destination type for __builtin_bit_cast is rejected with
// BitCastReferenceDestination. The reference is introduced through decltype so
// the destination is not written as an explicit reference declarator.

int main() {
	int source = 0;
	int& reference = source;
	auto value = __builtin_bit_cast(decltype(reference), source);
	return static_cast<int>(value);
}
