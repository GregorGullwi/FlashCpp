enum class byte_like : unsigned char {};

int main() {
	byte_like value = static_cast<byte_like>(2);
	return static_cast<unsigned int>(value) == 2 ? 0 : 1;
}
