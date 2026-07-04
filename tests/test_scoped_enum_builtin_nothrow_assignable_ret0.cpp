enum class byte_like : unsigned char {};

int main() {
	return __is_nothrow_assignable(byte_like&, byte_like) ? 0 : 1;
}
