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
	SizeInBits size_in_bits;	 // 8, 16, 32, or 64
	bool is_signed;		// true = use MOVSX, false = use MOVZX for loads < 64-bit

	SizedRegister(X64Register r, int size, bool sign = false)
		: reg(r), size_in_bits(SizeInBits{size}), is_signed(sign) {}
	SizedRegister(X64Register r, SizeInBits size, bool sign = false)
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
	int32_t offset;		// Offset from RBP
	SizeInBits size_in_bits;	 // 8, 16, 32, or 64
	bool is_signed;		// true = value is signed, false = unsigned

	SizedStackSlot(int32_t off, int size, bool sign = false)
		: offset(off), size_in_bits(SizeInBits{size}), is_signed(sign) {}
	SizedStackSlot(int32_t off, SizeInBits size, bool sign = false)
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

struct TempVar {
	TempVar() : var_number(1) {}	 // Start at 1, not 0
	explicit TempVar(size_t num) : var_number(num) {}

	TempVar next() {
		return TempVar(++var_number);
	}
	size_t var_number = 1;  // 1-based: first temp var is number 1
};

struct LocalVarId {
	uint32_t value = 0;
	constexpr LocalVarId() = default;
	explicit constexpr LocalVarId(uint32_t raw_value) : value(raw_value) {}
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(LocalVarId, LocalVarId) = default;
};

#include "StringTable.h"	 // For StringHandle support

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

enum class ValueCategory : uint8_t {
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

// A name for a declaration's storage: either a non-local entity (parameter,
// 'this', global, static local, label, function/type symbol) identified by its
// spelling, or a named function-local identified by its numeric LocalVarId.
// Locals use the id so two declarations that share a spelling (shadowing) stay
// distinct; non-locals have no per-function id and keep the spelling.
using VariableKey = std::variant<StringHandle, LocalVarId>;

// Type alias for operand values (used in LValueInfo and elsewhere)
using IrValue = std::variant<unsigned long long, double, TempVar, StringHandle, LocalVarId>;

// Information about an lvalue's storage location
struct LValueInfo {
	enum class Kind {
		Direct,		    // Direct variable access: x
		Indirect,	    // Through pointer dereference: *ptr
		ReferenceDeref, // Dereference of a reference variable: const T& cd = d; cd used as lvalue
		Member,		    // Struct member access: obj.member
		ArrayElement,   // Array element access: arr[i]
		Temporary,	    // Temporary materialization
		Global		    // Global variable: base = StringHandle (global name)
	};

	Kind kind;

	// Base object (variable name or temp var)
	std::variant<StringHandle, TempVar, LocalVarId> base;

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

	// For ArrayElement: true when the base operand holds an address; false when it names inline storage.
	bool base_holds_address = false;

	// For Member: whether the base object is a pointer (ptr->member) or direct object (obj.member)
	// When true, handleMemberStore should dereference the pointer before accessing the member
	bool is_pointer_to_member = false;

	// For bitfield members: width in bits and bit offset within storage unit
	std::optional<size_t> bitfield_width;
	size_t bitfield_bit_offset = 0;

	// Constructor for simple cases
	LValueInfo(Kind k, std::variant<StringHandle, TempVar, LocalVarId> b, int off = 0)
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

	// Whether this temp holds an address-only value (from AddressOf/AddressOfMember)
	// Unlike true references, address-only values should NOT be implicitly dereferenced
	bool holds_address_only = false;

	// Whether this temp is the result of std::move or similar
	bool is_move_result = false;

	// RVO/NRVO (Return Value Optimization) tracking
	// C++17 mandates copy elision for prvalues used to initialize objects of the same type,
	// which includes function returns, direct initialization, and other contexts
	bool is_return_value = false;		  // True if this is a return value (for RVO detection)
	bool eligible_for_rvo = false;	   // True if this prvalue can be constructed directly in destination
	bool eligible_for_nrvo = false;		// True if this named variable can use NRVO

	// Fields previously tracked in IndirectStorageInfo (for reference/pointer dereferencing)
	// These are used by IRConverter when loading values through references
	TypeIndex value_type_index{};
	IrType ir_type = IrType::Integer;  // Phase 4: parallel ir_type field for TempVar metadata
	SizeInBits value_size_bits;
	bool is_rvalue_reference = false;

	// Accessors for the value type
	TypeCategory valueType() const { return value_type_index.category(); }
	TypeCategory valueCategory() const { return value_type_index.category(); }

	// Constructor
	TempVarMetadata() = default;

	// Helper to create lvalue metadata
	static TempVarMetadata makeLValue(LValueInfo lv_info, TypeCategory cat, int size_bits) {
		TempVarMetadata meta;
		meta.category = ValueCategory::LValue;
		meta.lvalue_info = lv_info;
		meta.value_type_index = TypeIndex{0, cat};
		meta.ir_type = toIrType(cat);
		meta.value_size_bits = SizeInBits{size_bits};
		return meta;
	}

	// Helper to create xvalue metadata
	static TempVarMetadata makeXValue(LValueInfo lv_info, TypeCategory cat, int size_bits) {
		TempVarMetadata meta;
		meta.category = ValueCategory::XValue;
		meta.lvalue_info = lv_info;
		meta.is_move_result = true;
		meta.value_type_index = TypeIndex{0, cat};
		meta.ir_type = toIrType(cat);
		meta.value_size_bits = SizeInBits{size_bits};
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

	// Helper to create reference metadata (for compatibility with IndirectStorageInfo)
	static TempVarMetadata makeReference(TypeIndex value_type_index_param, SizeInBits size_bits, ValueCategory value_category) {
		TempVarMetadata meta;
		meta.category = value_category;
		meta.is_address = true;	// References hold addresses
		meta.holds_address_only = false;
		meta.value_type_index = value_type_index_param;
		meta.ir_type = toIrType(value_type_index_param.category());
		meta.value_size_bits = size_bits;
		meta.is_rvalue_reference = (value_category == ValueCategory::XValue);
		return meta;
	}

	// Helper to create address-only metadata (for AddressOf/AddressOfMember results)
	// Address-only values should NOT be implicitly dereferenced.
	// Pass ValueCategory::XValue for rvalue-reference address-only values (e.g. function returning T&&)
	static TempVarMetadata makeAddressOnly(TypeIndex value_type_index_param, SizeInBits size_bits, ValueCategory value_category) {
		TempVarMetadata meta;
		meta.category = value_category;
		meta.is_address = true;	// It's an address/pointer
		meta.holds_address_only = true;	// But not a true reference
		meta.value_type_index = value_type_index_param;
		meta.ir_type = toIrType(value_type_index_param.category());
		meta.value_size_bits = size_bits;
		meta.is_rvalue_reference = (value_category == ValueCategory::XValue);
		return meta;
	}
};

using IrOperand = std::variant<int, unsigned long long, double, bool, char, TypeCategory, TempVar, StringHandle, LocalVarId>;

// Lift a declaration-storage key into an IrValue / IrOperand. Both target
// variants carry StringHandle and LocalVarId, so this only selects the active
// alternative rather than re-encoding identity.
inline IrValue toIrValue(const VariableKey& key) {
	if (const auto* string_ptr = std::get_if<StringHandle>(&key)) {
		return IrValue(*string_ptr);
	}
	return IrValue(std::get<LocalVarId>(key));
}

inline IrOperand toIrOperand(const VariableKey& key) {
	if (const auto* string_ptr = std::get_if<StringHandle>(&key)) {
		return IrOperand(*string_ptr);
	}
	return IrOperand(std::get<LocalVarId>(key));
}

// Base-object variant used by LValueInfo; same alternatives as VariableKey plus
// TempVar, which a storage key never carries.
inline std::variant<StringHandle, TempVar, LocalVarId> toVariableBase(const VariableKey& key) {
	if (const auto* string_ptr = std::get_if<StringHandle>(&key)) {
		return std::variant<StringHandle, TempVar, LocalVarId>(*string_ptr);
	}
	return std::variant<StringHandle, TempVar, LocalVarId>(std::get<LocalVarId>(key));
}

// ============================================================================
// OperandStorage - Abstraction for storing IR instruction operands
// ============================================================================
// Define to switch between storage strategies
// Uncomment to use chunked storage instead of std::vector
