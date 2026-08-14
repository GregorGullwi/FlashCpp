template <class To, class From>
constexpr To bitCast(const From& value) {
	return __builtin_bit_cast(To, value);
}

template <class Type>
struct FloatingTraits;

template <>
struct FloatingTraits<float> {
	using UInt = unsigned int;
	static constexpr UInt Mask = 0x7fffffffu;
};

template <>
struct FloatingTraits<double> {
	using UInt = unsigned long long;
	static constexpr UInt Mask = 0x7fffffffffffffffull;
};

template <class Type>
auto bits(const Type& value) {
	using Traits = FloatingTraits<Type>;
	using UInt = typename Traits::UInt;
	const auto rawBits = bitCast<UInt>(value);
	return rawBits & Traits::Mask;
}

template <class Type>
bool positiveBits(Type value) {
	return bits(value) != 0;
}

bool floatBits() {
	return positiveBits(1.0f);
}

bool doubleBits() {
	return positiveBits(1.0);
}

int main() {
	return floatBits() && doubleBits() ? 0 : 1;
}
