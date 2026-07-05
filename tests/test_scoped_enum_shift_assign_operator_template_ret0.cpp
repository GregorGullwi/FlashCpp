enum class byte_like : unsigned char {};

template <bool B, class T>
struct enable_if {};

template <class T>
struct enable_if<true, T> {
	using type = T;
};

template <bool B, class T>
using enable_if_t = typename enable_if<B, T>::type;

template <class>
constexpr bool is_integral_v = true;

template <class Int, enable_if_t<is_integral_v<Int>, int> = 0>
constexpr byte_like operator<<(byte_like arg, Int shift) {
	return static_cast<byte_like>(static_cast<unsigned int>(arg) << shift);
}

template <class Int, enable_if_t<is_integral_v<Int>, int> = 0>
constexpr byte_like& operator<<=(byte_like& arg, Int shift) {
	return arg = arg << shift;
}

int main() {
	byte_like value = static_cast<byte_like>(1);
	value <<= 1;
	return static_cast<unsigned int>(value) == 2 ? 0 : 1;
}
