// flash_limits.h - Minimal numeric_limits for FlashCpp
// Provides compile-time information about numeric types

#ifndef FLASH_LIMITS_H
#define FLASH_LIMITS_H

namespace flash_std {

// Base template for numeric_limits
template<typename T>
struct numeric_limits {
	static constexpr bool is_specialized = false;
	static constexpr bool is_signed = false;
	static constexpr bool is_integer = false;
	static constexpr bool is_exact = false;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = false;
	static constexpr bool is_modulo = false;
	static constexpr int digits = 0;
	static constexpr int digits10 = 0;
};

// bool specialization
template<>
struct numeric_limits<bool> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = false;
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = false;
	static constexpr int digits = 1;
	static constexpr int digits10 = 0;
	
	static constexpr bool min() noexcept { return false; }
	static constexpr bool max() noexcept { return true; }
	static constexpr bool lowest() noexcept { return false; }
};

// char specialization
template<>
struct numeric_limits<char> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = true;  // Implementation-defined, assuming signed
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = false;
	static constexpr int digits = 7;
	static constexpr int digits10 = 2;
	
	static constexpr char min() noexcept { return -128; }
	static constexpr char max() noexcept { return 127; }
	static constexpr char lowest() noexcept { return -128; }
};

// signed char specialization
template<>
struct numeric_limits<signed char> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = true;
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = false;
	static constexpr int digits = 7;
	static constexpr int digits10 = 2;
	
	static constexpr signed char min() noexcept { return -128; }
	static constexpr signed char max() noexcept { return 127; }
	static constexpr signed char lowest() noexcept { return -128; }
};

// unsigned char specialization
template<>
struct numeric_limits<unsigned char> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = false;
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = true;
	static constexpr int digits = 8;
	static constexpr int digits10 = 2;
	
	static constexpr unsigned char min() noexcept { return 0; }
	static constexpr unsigned char max() noexcept { return 255; }
	static constexpr unsigned char lowest() noexcept { return 0; }
};

// short specialization
template<>
struct numeric_limits<short> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = true;
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = false;
	static constexpr int digits = 15;
	static constexpr int digits10 = 4;
	
	static constexpr short min() noexcept { return -32768; }
	static constexpr short max() noexcept { return 32767; }
	static constexpr short lowest() noexcept { return -32768; }
};

// unsigned short specialization
template<>
struct numeric_limits<unsigned short> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = false;
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = true;
	static constexpr int digits = 16;
	static constexpr int digits10 = 4;
	
	static constexpr unsigned short min() noexcept { return 0; }
	static constexpr unsigned short max() noexcept { return 65535; }
	static constexpr unsigned short lowest() noexcept { return 0; }
};

// int specialization
template<>
struct numeric_limits<int> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = true;
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = false;
	static constexpr int digits = 31;
	static constexpr int digits10 = 9;
	
	static constexpr int min() noexcept { return -2147483648; }
	static constexpr int max() noexcept { return 2147483647; }
	static constexpr int lowest() noexcept { return -2147483648; }
};

// unsigned int specialization
template<>
struct numeric_limits<unsigned int> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = false;
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = true;
	static constexpr int digits = 32;
	static constexpr int digits10 = 9;
	
	static constexpr unsigned int min() noexcept { return 0; }
	static constexpr unsigned int max() noexcept { return 4294967295u; }
	static constexpr unsigned int lowest() noexcept { return 0; }
};

// long specialization
template<>
struct numeric_limits<long> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = true;
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = false;
	static constexpr int digits = 63;  // 64-bit on most platforms
	static constexpr int digits10 = 18;
	
	static constexpr long min() noexcept { return -9223372036854775807L - 1; }
	static constexpr long max() noexcept { return 9223372036854775807L; }
	static constexpr long lowest() noexcept { return -9223372036854775807L - 1; }
};

// unsigned long specialization
template<>
struct numeric_limits<unsigned long> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = false;
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = true;
	static constexpr int digits = 64;
	static constexpr int digits10 = 19;
	
	static constexpr unsigned long min() noexcept { return 0; }
	static constexpr unsigned long max() noexcept { return 18446744073709551615UL; }
	static constexpr unsigned long lowest() noexcept { return 0; }
};

// long long specialization
template<>
struct numeric_limits<long long> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = true;
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = false;
	static constexpr int digits = 63;
	static constexpr int digits10 = 18;
	
	static constexpr long long min() noexcept { return -9223372036854775807LL - 1; }
	static constexpr long long max() noexcept { return 9223372036854775807LL; }
	static constexpr long long lowest() noexcept { return -9223372036854775807LL - 1; }
};

// unsigned long long specialization
template<>
struct numeric_limits<unsigned long long> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = false;
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = true;
	static constexpr int digits = 64;
	static constexpr int digits10 = 19;
	
	static constexpr unsigned long long min() noexcept { return 0; }
	static constexpr unsigned long long max() noexcept { return 18446744073709551615ULL; }
	static constexpr unsigned long long lowest() noexcept { return 0; }
};

// float specialization
template<>
struct numeric_limits<float> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = true;
	static constexpr bool is_integer = false;
	static constexpr bool is_exact = false;
	static constexpr bool has_infinity = true;
	static constexpr bool has_quiet_NaN = true;
	static constexpr bool has_signaling_NaN = true;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = false;
	static constexpr int digits = 24;
	static constexpr int digits10 = 6;
	
	static constexpr float min() noexcept { return 1.17549435e-38F; }
	static constexpr float max() noexcept { return 3.40282347e+38F; }
	static constexpr float lowest() noexcept { return -3.40282347e+38F; }
};

// double specialization
template<>
struct numeric_limits<double> {
	static constexpr bool is_specialized = true;
	static constexpr bool is_signed = true;
	static constexpr bool is_integer = false;
	static constexpr bool is_exact = false;
	static constexpr bool has_infinity = true;
	static constexpr bool has_quiet_NaN = true;
	static constexpr bool has_signaling_NaN = true;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = false;
	static constexpr int digits = 53;
	static constexpr int digits10 = 15;
	
	static constexpr double min() noexcept { return 2.2250738585072014e-308; }
	static constexpr double max() noexcept { return 1.7976931348623157e+308; }
	static constexpr double lowest() noexcept { return -1.7976931348623157e+308; }
};

} // namespace flash_std

#endif // FLASH_LIMITS_H
