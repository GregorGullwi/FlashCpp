enum class byte_like : unsigned char {};

int main() {
	byte_like value = static_cast<byte_like>(2);
	byte_like moved = static_cast<byte_like&&>(value);
	return static_cast<unsigned int>(moved) == 2 ? 0 : 1;
}
