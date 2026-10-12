// __builtin_bit_cast requires the source and destination types to have the same
// size, so a 32-bit source with a 64-bit destination reports BitCastSizeMismatch.

struct ThirtyTwoBits {
	int value;
};

int main() {
	ThirtyTwoBits source{1};
	unsigned long long value = __builtin_bit_cast(unsigned long long, source);
	return value == 0 ? 0 : 1;
}
