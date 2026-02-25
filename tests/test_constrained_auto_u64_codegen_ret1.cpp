template <typename T>
concept AnyType = true;

unsigned long long high_word(AnyType auto value) {
	return value >> 32;
}

int main() {
	unsigned long long input = 0x0000000100000000ULL;
	return static_cast<int>(high_word(input));
}
