enum class byte_like : unsigned char {};

template <class T>
T&& move_like(T& value) {
	return static_cast<T&&>(value);
}

template <class T>
void generic_swap(T& left, T& right) {
	T tmp = move_like(left);
	left = move_like(right);
	right = move_like(tmp);
}

int main() {
	byte_like left = static_cast<byte_like>(1);
	byte_like right = static_cast<byte_like>(2);
	generic_swap(left, right);
	return static_cast<unsigned int>(left) == 2 &&
		static_cast<unsigned int>(right) == 1 ? 0 : 1;
}
