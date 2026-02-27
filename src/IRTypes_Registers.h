#pragma once
#include "IRTypes_Core.h"

enum class X64Register : uint8_t {
	RAX,
	RCX,
	RDX,
	RBX,
	RSP,
	RBP,
	RSI,
	RDI,
	R8,
	R9,
	R10,
	R11,
	R12,
	R13,
	R14,
	R15,
	XMM0,
	XMM1,
	XMM2,
	XMM3,
	XMM4,
	XMM5,
	XMM6,
	XMM7,
	XMM8,
	XMM9,
	XMM10,
	XMM11,
	XMM12,
	XMM13,
	XMM14,
	XMM15,
	Count
};

/// Bundles a register with its operational size and signedness.
/// Use this instead of bare X64Register when emitting MOV instructions
/// to ensure correct operand size encoding.
struct SizedRegister {
	X64Register reg;
	int size_in_bits;    // 8, 16, 32, or 64
	bool is_signed;      // true = use MOVSX, false = use MOVZX for loads < 64-bit
	
	SizedRegister(X64Register r, int size, bool sign = false)
		: reg(r), size_in_bits(size), is_signed(sign) {}
	
	// Convenience constructors for common cases
	static SizedRegister ptr(X64Register r) { return {r, 64, false}; }
	static SizedRegister i64(X64Register r) { return {r, 64, true}; }
	static SizedRegister i32(X64Register r) { return {r, 32, true}; }
	static SizedRegister i16(X64Register r) { return {r, 16, true}; }
	static SizedRegister i8(X64Register r) { return {r, 8, true}; }
	static SizedRegister u64(X64Register r) { return {r, 64, false}; }
	static SizedRegister u32(X64Register r) { return {r, 32, false}; }
	static SizedRegister u16(X64Register r) { return {r, 16, false}; }
	static SizedRegister u8(X64Register r) { return {r, 8, false}; }
};

/// Bundles a stack slot offset with its size and signedness.
/// Use this to specify the source operand when loading from stack.
struct SizedStackSlot {
	int32_t offset;       // Offset from RBP
	int size_in_bits;     // 8, 16, 32, or 64
	bool is_signed;       // true = value is signed, false = unsigned
	
	SizedStackSlot(int32_t off, int size, bool sign = false)
		: offset(off), size_in_bits(size), is_signed(sign) {}
	
	// Convenience constructors for common cases
	static SizedStackSlot ptr(int32_t off) { return {off, 64, false}; }
	static SizedStackSlot i64(int32_t off) { return {off, 64, true}; }
	static SizedStackSlot i32(int32_t off) { return {off, 32, true}; }
	static SizedStackSlot i16(int32_t off) { return {off, 16, true}; }
	static SizedStackSlot i8(int32_t off) { return {off, 8, true}; }
	static SizedStackSlot u64(int32_t off) { return {off, 64, false}; }
	static SizedStackSlot u32(int32_t off) { return {off, 32, false}; }
	static SizedStackSlot u16(int32_t off) { return {off, 16, false}; }
	static SizedStackSlot u8(int32_t off) { return {off, 8, false}; }
};

template <size_t N>
constexpr auto make_temp_string() {
	// Max: "temp_" + 3-digit number + '\0'
	std::array<char, 10> buf{};
	buf[0] = 't'; buf[1] = 'e'; buf[2] = 'm'; buf[3] = 'p'; buf[4] = '_';

	size_t i = 5;
	size_t n = N;

	char digits[4] = {};
	int d = 0;
	do {
		digits[d++] = '0' + (n % 10);
		n /= 10;
	} while (n > 0);

	for (int j = d - 1; j >= 0; --j) {
		buf[i++] = digits[j];
	}
	buf[i] = '\0'; // null terminate for safety
	return buf;
}

template <size_t... Is>
constexpr auto make_temp_array(std::index_sequence<Is...>) {
	return std::array<std::array<char, 10>, sizeof...(Is)>{
		make_temp_string<Is>()...
	};
}

template <size_t N>
constexpr auto make_temp_array() {
	return make_temp_array(std::make_index_sequence<N>{});
}

constexpr auto raw_temp_names = make_temp_array<256>();

// Create string_view version from raw names
constexpr auto make_view_array() {
	std::array<std::string_view, raw_temp_names.size()> result{};
	for (size_t i = 0; i < raw_temp_names.size(); ++i) {
		// Views into raw array
		result[i] = std::string_view{ raw_temp_names[i].data() };
	}
	return result;
}

constexpr auto temp_name_array = make_view_array();

struct TempVar
{
	TempVar() : var_number(1) {}  // Start at 1, not 0
	explicit TempVar(size_t num) : var_number(num) {}

	TempVar next() {
		return TempVar(++var_number);
	}
	std::string_view name() const {
		// temp_name_array is 0-indexed, indexed by var_number - 1
		// var_number=0 is a sentinel (invalid/uninitialized), return empty string
		if (var_number == 0) {
			return ""; // Sentinel value - no valid name
		}
		// Bounds check - temp_name_array has 256 entries (0-255)
		if (var_number - 1 >= 256) {
			FLASH_LOG(General, Error, "TempVar::name() - var_number out of bounds: ", var_number, " (max is 256)");
			return "temp_INVALID"; // Return a safe fallback
		}
		return temp_name_array[var_number - 1];
	}
	size_t var_number = 1;  // 1-based: first temp var is number 1
};

#include "StringTable.h"  // For StringHandle support

// ============================================================================
// Value Category Tracking (C++20 Compliance - Option 2 Implementation)
// ============================================================================
// C++20 defines three primary value categories:
// - lvalue: expression that designates an object (has identity, can take address)
// - xvalue: expiring value (rvalue reference, std::move result)
// - prvalue: pure rvalue (temporary, literal, function return by value)
//
// This system enables:
// - Copy elision (RVO/NRVO)
// - Move semantics optimization
// - Dead store elimination
// - Proper reference binding
// ============================================================================

enum class ValueCategory {
	// lvalue - has identity and cannot be moved from
	// Examples: variables, array elements, struct members, dereferenced pointers
	LValue,
	
	// xvalue - has identity and can be moved from (expiring value)
	// Examples: std::move(x), a.m where a is rvalue, array[i] where array is rvalue
	XValue,
	
	// prvalue - pure rvalue, no identity
	// Examples: literals (42, 3.14), function returns by value, arithmetic operations
	PRValue
};

// Type alias for operand values (used in LValueInfo and elsewhere)
using IrValue = std::variant<unsigned long long, double, TempVar, StringHandle>;

// Information about an lvalue's storage location
struct LValueInfo {
	enum class Kind {
		Direct,        // Direct variable access: x
		Indirect,      // Through pointer dereference: *ptr
		Member,        // Struct member access: obj.member
		ArrayElement,  // Array element access: arr[i]
		Temporary,     // Temporary materialization
		Global         // Global variable: base = StringHandle (global name)
	};
	
	Kind kind;
	
	// Base object (variable name or temp var)
	std::variant<StringHandle, TempVar> base;
	
	// Offset in bytes from base (for members, array elements)
	int offset;
	
	// For nested access (e.g., arr[i].member), pointer to parent lvalue info
	// Using raw pointer to avoid circular dependency and keep it lightweight
	const LValueInfo* parent = nullptr;
	
	// Additional metadata for specific kinds (optional to keep structure lightweight)
	// For Member: the member name
	std::optional<StringHandle> member_name;
	
	// For ArrayElement: the computed index value
	// Can be a constant (unsigned long long), TempVar, or StringHandle
	std::optional<IrValue> array_index;
	
	// For ArrayElement: whether the array base is a pointer (int* arr) or array (int arr[])
	bool is_pointer_to_array = false;
	
	// For Member: whether the base object is a pointer (ptr->member) or direct object (obj.member)
	// When true, handleMemberStore should dereference the pointer before accessing the member
	bool is_pointer_to_member = false;
	
	// For bitfield members: width in bits and bit offset within storage unit
	std::optional<size_t> bitfield_width;
	size_t bitfield_bit_offset = 0;
	
	// Constructor for simple cases
	LValueInfo(Kind k, std::variant<StringHandle, TempVar> b, int off = 0)
		: kind(k), base(b), offset(off) {}
};

// Metadata attached to TempVar for value category tracking
struct TempVarMetadata {
	// Value category of this temporary
	ValueCategory category = ValueCategory::PRValue;
	
	// If this is an lvalue or xvalue, information about its storage location
	std::optional<LValueInfo> lvalue_info;
	
	// Whether this temp represents an address (pointer) rather than a value
	// Helps distinguish between &x (address-of) vs x (value)
	bool is_address = false;
	
	// Whether this temp is the result of std::move or similar
	bool is_move_result = false;
	
	// RVO/NRVO (Return Value Optimization) tracking
	// C++17 mandates copy elision for prvalues used to initialize objects of the same type,
	// which includes function returns, direct initialization, and other contexts
	bool is_return_value = false;        // True if this is a return value (for RVO detection)
	bool eligible_for_rvo = false;       // True if this prvalue can be constructed directly in destination
	bool eligible_for_nrvo = false;      // True if this named variable can use NRVO
	
	// Fields previously tracked in ReferenceInfo (for reference/pointer dereferencing)
	// These are used by IRConverter when loading values through references
	Type value_type = Type::Invalid;
	int value_size_bits = 0;
	bool is_rvalue_reference = false;
	
	// Constructor
	TempVarMetadata() = default;
	
	// Helper to create lvalue metadata
	static TempVarMetadata makeLValue(LValueInfo lv_info, Type type = Type::Invalid, int size_bits = 0) {
		TempVarMetadata meta;
		meta.category = ValueCategory::LValue;
		meta.lvalue_info = lv_info;
		meta.value_type = type;
		meta.value_size_bits = size_bits;
		return meta;
	}
	
	// Helper to create xvalue metadata
	static TempVarMetadata makeXValue(LValueInfo lv_info, Type type = Type::Invalid, int size_bits = 0) {
		TempVarMetadata meta;
		meta.category = ValueCategory::XValue;
		meta.lvalue_info = lv_info;
		meta.is_move_result = true;
		meta.value_type = type;
		meta.value_size_bits = size_bits;
		return meta;
	}
	
	// Helper to create prvalue metadata
	static TempVarMetadata makePRValue() {
		TempVarMetadata meta;
		meta.category = ValueCategory::PRValue;
		return meta;
	}
	
	// Helper to create prvalue metadata eligible for RVO (C++17 mandatory copy elision)
	static TempVarMetadata makeRVOEligiblePRValue() {
		TempVarMetadata meta;
		meta.category = ValueCategory::PRValue;
		meta.eligible_for_rvo = true;
		return meta;
	}
	
	// Helper to create metadata for named return value (NRVO candidate)
	static TempVarMetadata makeNRVOCandidate(LValueInfo lv_info) {
		TempVarMetadata meta;
		meta.category = ValueCategory::LValue;
		meta.lvalue_info = lv_info;
		meta.eligible_for_nrvo = true;
		return meta;
	}
	
	// Helper to create reference metadata (for compatibility with old ReferenceInfo usage)
	static TempVarMetadata makeReference(Type type, int size_bits, bool is_rvalue_ref = false) {
		TempVarMetadata meta;
		meta.category = is_rvalue_ref ? ValueCategory::XValue : ValueCategory::LValue;
		meta.is_address = true;  // References hold addresses
		meta.value_type = type;
		meta.value_size_bits = size_bits;
		meta.is_rvalue_reference = is_rvalue_ref;
		return meta;
	}
};

using IrOperand = std::variant<int, unsigned long long, double, bool, char, Type, TempVar, StringHandle>;

// ============================================================================
// OperandStorage - Abstraction for storing IR instruction operands
// ============================================================================
// Define to switch between storage strategies
// Uncomment to use chunked storage instead of std::vector
