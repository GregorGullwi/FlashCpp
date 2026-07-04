enum class byte_like : unsigned char {};

int main() {
	byte_like value = static_cast<byte_like>(1);
	return static_cast<int>(value << 1);
}
