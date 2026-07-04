enum class byte_like : unsigned char {};

template <class T>
void generic_swap(T& left, T& right) {
	T tmp = static_cast<T&&>(left);
	left = static_cast<T&&>(right);
	right = static_cast<T&&>(tmp);
}

int main() {
	byte_like left = static_cast<byte_like>(1);
	byte_like right = static_cast<byte_like>(2);
	generic_swap(left, right);
	return static_cast<unsigned int>(left) == 2 &&
		static_cast<unsigned int>(right) == 1 ? 0 : 1;
}
