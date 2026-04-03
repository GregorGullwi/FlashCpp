constexpr int quotient = 84 / 2;
constexpr int remainder = 85 % 43;
constexpr int shifted = 1 << 5;

int main() {
	if (quotient != 42)
		return 1;
	if (remainder != 42)
		return 2;
	if (shifted != 32)
		return 3;
	return 0;
}
