template <class To, class From>
constexpr To bitCast(const From& value) {
	return __builtin_bit_cast(To, value);
}

int main() {
	if constexpr (sizeof(long) == 4) {
		static_assert(sizeof(long double) == sizeof(double));
		const unsigned long long bits = bitCast<unsigned long long>(1.0L);
		if (bits != 0x3ff0000000000000ull) {
			return 1;
		}
		const long double value = bitCast<long double>(bits);
		return bitCast<unsigned long long>(value) == bits ? 0 : 2;
	}
	return 0;
}
