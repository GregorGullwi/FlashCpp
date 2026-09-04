#pragma once

#include <cstdint>

// CV-qualifiers (const/volatile) - separate from sign qualifiers
// These can be combined with type qualifiers using bitwise operations
enum class CVQualifier : uint8_t {
	None = 0,
	Const = 1 << 0,
	Volatile = 1 << 1,
	ConstVolatile = Const | Volatile
};

inline CVQualifier operator|(CVQualifier a, CVQualifier b) {
	return static_cast<CVQualifier>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline CVQualifier& operator|=(CVQualifier& a, CVQualifier b) {
	return a = a | b;
}
inline bool hasCVQualifier(CVQualifier cv, CVQualifier flag) {
	return (static_cast<uint8_t>(cv) & static_cast<uint8_t>(flag)) != 0;
}

// Reference qualifiers - mutually exclusive enum (not a bitmask)
enum class ReferenceQualifier : uint8_t {
	None = 0,
	LValueReference = 1 << 0,  // &
	RValueReference = 1 << 1,  // &&
};

using CVReferenceQualifier = ReferenceQualifier;
