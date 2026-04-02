#pragma once

#include <cassert>
#include <cstddef>
#include <format>
#include <limits>
#include <ostream>

// Strong wrapper for a size expressed in bits (e.g. 8, 16, 32, 64).
struct SizeInBits {
	int value = 0;
	constexpr SizeInBits() noexcept = default;
	constexpr explicit SizeInBits(int v) noexcept : value(v) {}
	constexpr auto operator<=>(const SizeInBits&) const noexcept = default;
	constexpr bool is_set() const noexcept { return value != 0; }
};

template <>
struct std::formatter<SizeInBits, char> : std::formatter<int, char> {
	auto format(const SizeInBits& s, std::format_context& ctx) const {
		return std::formatter<int, char>::format(s.value, ctx);
	}
};

inline std::ostream& operator<<(std::ostream& os, const SizeInBits& s) {
	return os << s.value;
}

// Strong wrapper for a size expressed in bytes (e.g. 1, 2, 4, 8).
struct SizeInBytes {
	int value = 0;
	constexpr SizeInBytes() noexcept = default;
	constexpr explicit SizeInBytes(int v) noexcept : value(v) {}
	constexpr auto operator<=>(const SizeInBytes&) const noexcept = default;
	constexpr bool is_set() const noexcept { return value != 0; }
};

template <>
struct std::formatter<SizeInBytes, char> : std::formatter<int, char> {
	auto format(const SizeInBytes& s, std::format_context& ctx) const {
		return std::formatter<int, char>::format(s.value, ctx);
	}
};

inline std::ostream& operator<<(std::ostream& os, const SizeInBytes& s) {
	return os << s.value;
}

constexpr SizeInBits toBits(SizeInBytes size_bytes) noexcept {
	return SizeInBits{size_bytes.value * 8};
}

constexpr SizeInBytes toBytesCeil(SizeInBits size_bits) noexcept {
	return SizeInBytes{(size_bits.value + 7) / 8};
}

inline SizeInBytes toBytesExact(SizeInBits size_bits) {
	assert(size_bits.value % 8 == 0);
	return SizeInBytes{size_bits.value / 8};
}

constexpr size_t toSizeT(SizeInBytes size_bytes) noexcept {
	return static_cast<size_t>(size_bytes.value);
}

inline SizeInBytes toSizeInBytes(size_t value) {
	assert(value <= static_cast<size_t>(std::numeric_limits<int>::max()));
	return SizeInBytes{static_cast<int>(value)};
}
