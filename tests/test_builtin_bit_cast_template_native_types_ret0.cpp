template <class To, class From>
constexpr To bitCast(const From& value) {
	return __builtin_bit_cast(To, value);
}

int main() {
	const unsigned int floatBits = bitCast<unsigned int>(1.0f);
	const float floatValue = bitCast<float>(0x40000000u);
	const unsigned long long doubleBits = bitCast<unsigned long long>(1.0);
	const double doubleValue = bitCast<double>(0x4000000000000000ull);
	return floatBits == 0x3f800000u && floatValue == 2.0f
		&& doubleBits == 0x3ff0000000000000ull && doubleValue == 2.0 ? 0 : 1;
}
