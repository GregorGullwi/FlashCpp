#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <any>
#include <sstream>
#include <cassert>

#include "AstNodeTypes.h"
#include "Log.h"

// Forward declare IrInstruction for circular dependency resolution
class IrInstruction;

enum class IrOpcode : int_fast16_t {
	Add,
	Subtract,
	Multiply,
	Divide,
	UnsignedDivide,
	Modulo,
	// Floating-point arithmetic
	FloatAdd,
	FloatSubtract,
	FloatMultiply,
	FloatDivide,
	ShiftLeft,
	ShiftRight,
	UnsignedShiftRight,
	BitwiseAnd,
	BitwiseOr,
	BitwiseXor,
	BitwiseNot,
	Negate,
	// Comparison operators
	Equal,
	NotEqual,
	LessThan,
	LessEqual,
	GreaterThan,
	GreaterEqual,
	UnsignedLessThan,
	UnsignedLessEqual,
	UnsignedGreaterThan,
	UnsignedGreaterEqual,
	// Floating-point comparisons
	FloatEqual,
	FloatNotEqual,
	FloatLessThan,
	FloatLessEqual,
	FloatGreaterThan,
	FloatGreaterEqual,
	// Logical operators
	LogicalAnd,
	LogicalOr,
	LogicalNot,
	// Type conversions
	IntToFloat,
	FloatToInt,
	FloatToFloat,
	// Assignment operators
	AddAssign,
	SubAssign,
	MulAssign,
	DivAssign,
	ModAssign,
	AndAssign,
	OrAssign,
	XorAssign,
	ShlAssign,
	ShrAssign,
	// Increment/Decrement
	PreIncrement,
	PostIncrement,
	PreDecrement,
	PostDecrement,
	SignExtend,
	ZeroExtend,
	Truncate,
	Return,
	FunctionDecl,
	VariableDecl,
	FunctionCall,
	Assignment,
	StackAlloc,
	Store,
	// Control flow
	Branch,
	ConditionalBranch,
	Label,
	// Loop control
	LoopBegin,
	LoopEnd,
	Break,
	Continue,
	// Scope control
	ScopeBegin,
	ScopeEnd,
	// Array operations
	ArrayAccess,
	ArrayStore,
	ArrayElementAddress,  // Calculate address of array element without loading value
	// Pointer operations
	AddressOf,
	Dereference,
	DereferenceStore,    // Store through a pointer: *ptr = value
	// Struct operations
	MemberAccess,
	MemberStore,
	// Constructor/Destructor operations
	ConstructorCall,
	DestructorCall,
	// Virtual function call
	VirtualCall,
	// String literal
	StringLiteral,
	// Heap allocation/deallocation (new/delete)
	HeapAlloc,       // new Type or new Type(args)
	HeapAllocArray,  // new Type[size]
	HeapFree,        // delete ptr
	HeapFreeArray,   // delete[] ptr
	PlacementNew,    // new (address) Type or new (address) Type(args)
	// RTTI operations
	Typeid,          // typeid(expr) or typeid(Type) - returns pointer to type_info
	DynamicCast,     // dynamic_cast<Type>(expr) - runtime type checking cast
	// Static storage duration
	GlobalVariableDecl,  // Global variable declaration: [type, size, name, is_initialized, init_value?]
	GlobalLoad,          // Load from global variable: [result_temp, global_name]
	GlobalStore,         // Store to global variable: [global_name, source_temp]
	// Lambda support
	FunctionAddress,     // Get address of a function: [result_temp, function_name]
	IndirectCall,        // Call through function pointer: [result_temp, func_ptr, arg1, arg2, ...]
	// Exception handling
	TryBegin,            // Begin try block: [label_for_handlers]
	TryEnd,              // End try block
	CatchBegin,          // Begin catch handler: [exception_var_temp, type_index, catch_end_label]
	CatchEnd,            // End catch handler
	Throw,               // Throw exception: [exception_temp, type_index]
	Rethrow,             // Rethrow current exception (throw; with no argument)
};

// ============================================================================
// FunctionDecl IR Instruction Layout Constants
// ============================================================================
// These constants define the operand layout for FunctionDecl instructions.
// This prevents bugs from operand index mismatches when the layout changes.
//
// FunctionDecl operand layout:
//   [0] return_type (Type)
//   [1] return_size (int)
//   [2] return_pointer_depth (int)
//   [3] function_name (string_view)
//   [4] struct_name (string_view) - empty for non-member functions
//   [5] linkage (int) - Linkage enum value
//   [6] is_variadic (bool)
//   [7] mangled_name (string_view) - pre-computed mangled name with full CV-qualifier info
//   [8+] parameters - each parameter has 7 operands:
//        [0] param_type (Type)
//        [1] param_size (int)
//        [2] param_pointer_depth (int)
//        [3] param_name (string_view)
//        [4] is_reference (bool)
//        [5] is_rvalue_reference (bool)
//        [6] cv_qualifier (int) - CVQualifier enum value
//
namespace FunctionDeclLayout {
	// Fixed operand indices
	constexpr size_t RETURN_TYPE = 0;
	constexpr size_t RETURN_SIZE = 1;
	constexpr size_t RETURN_POINTER_DEPTH = 2;
	constexpr size_t FUNCTION_NAME = 3;
	constexpr size_t STRUCT_NAME = 4;
	constexpr size_t LINKAGE = 5;
	constexpr size_t IS_VARIADIC = 6;
	constexpr size_t MANGLED_NAME = 7;

	// First parameter starts after the fixed operands
	constexpr size_t FIRST_PARAM_INDEX = 8;

	// Each parameter has this many operands
	constexpr size_t OPERANDS_PER_PARAM = 7;

	// Parameter operand offsets (relative to parameter start)
	constexpr size_t PARAM_TYPE = 0;
	constexpr size_t PARAM_SIZE = 1;
	constexpr size_t PARAM_POINTER_DEPTH = 2;
	constexpr size_t PARAM_NAME = 3;
	constexpr size_t PARAM_IS_REFERENCE = 4;
	constexpr size_t PARAM_IS_RVALUE_REFERENCE = 5;
	constexpr size_t PARAM_CV_QUALIFIER = 6;

	// Helper function to get the index of a specific parameter's operand
	constexpr size_t getParamOperandIndex(size_t param_number, size_t operand_offset) {
		return FIRST_PARAM_INDEX + (param_number * OPERANDS_PER_PARAM) + operand_offset;
	}

	// Helper function to calculate the number of parameters from operand count
	constexpr size_t getParamCount(size_t total_operand_count) {
		if (total_operand_count < FIRST_PARAM_INDEX) return 0;
		return (total_operand_count - FIRST_PARAM_INDEX) / OPERANDS_PER_PARAM;
	}

	// Helper function to validate that operand count is correct for given parameter count
	constexpr bool isValidOperandCount(size_t total_operand_count) {
		if (total_operand_count < FIRST_PARAM_INDEX) return false;
		return (total_operand_count - FIRST_PARAM_INDEX) % OPERANDS_PER_PARAM == 0;
	}
}

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

constexpr auto raw_temp_names = make_temp_array<64>();

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
		return temp_name_array[var_number - 1];
	}
	size_t var_number = 1;  // 1-based: first temp var is number 1
};

using IrOperand = std::variant<int, unsigned long long, double, bool, char, std::string, std::string_view, Type, TempVar>;

// ============================================================================
// OperandStorage - Abstraction for storing IR instruction operands
// ============================================================================
// Define to switch between storage strategies
// Uncomment to use chunked storage instead of std::vector
#define USE_GLOBAL_OPERAND_STORAGE

#ifndef USE_GLOBAL_OPERAND_STORAGE

// Vector-based storage (current implementation)
class OperandStorage {
public:
	OperandStorage() = default;

	// Constructor from vector (move semantics) - for backward compatibility
	explicit OperandStorage(std::vector<IrOperand>&& operands)
		: operands_(std::move(operands)) {}

	// Add operand directly (for builder pattern)
	void addOperand(IrOperand&& operand) {
		operands_.push_back(std::move(operand));
	}

	// Reserve space for operands (optimization)
	void reserve(size_t capacity) {
		operands_.reserve(capacity);
	}

	// Get operand count
	size_t size() const {
		return operands_.size();
	}

	// Access operand by index
	const IrOperand& operator[](size_t index) const {
		return operands_[index];
	}

	// Safe access with optional
	std::optional<IrOperand> getSafe(size_t index) const {
		return index < operands_.size()
			? std::optional<IrOperand>{ operands_[index] }
			: std::optional<IrOperand>{};
	}

private:
	std::vector<IrOperand> operands_;
};

#else // USE_GLOBAL_OPERAND_STORAGE

// Chunked storage (optimized implementation)
#include <vector>

// Global operand storage - all operands from all instructions stored here
class GlobalOperandStorage {
public:
	static GlobalOperandStorage& instance() {
		static GlobalOperandStorage storage;
		return storage;
	}

	// Reserve space for expected number of operands (optimization)
	void reserve(size_t capacity) {
		operands_.reserve(capacity);
		reserved_capacity_ = capacity;
	}

	// Add operands from vector and return the start index (for backward compatibility)
	size_t addOperands(std::vector<IrOperand>&& operands) {
		size_t start_index = operands_.size();
		for (auto&& op : operands) {
			operands_.push_back(std::move(op));
		}
		return start_index;
	}

	// Add single operand and return its index (for builder pattern)
	size_t addOperand(IrOperand&& operand) {
		size_t index = operands_.size();
		operands_.push_back(std::move(operand));
		return index;
	}

	// Get operand by global index
	const IrOperand& getOperand(size_t index) const {
		return operands_[index];
	}

	// Get total number of operands stored
	size_t totalOperands() const {
		return operands_.size();
	}

	// Get reserved capacity
	size_t reservedCapacity() const {
		return reserved_capacity_;
	}

	// Get actual capacity
	size_t actualCapacity() const {
		return operands_.capacity();
	}

	// Clear all operands (useful for testing)
	void clear() {
		operands_.clear();
		reserved_capacity_ = 0;
	}

	// Print statistics about operand storage
	void printStats() const {
		printf("\n=== GlobalOperandStorage Statistics ===\n");
		printf("Reserved capacity: %zu operands\n", reserved_capacity_);
		printf("Actual used:       %zu operands\n", operands_.size());
		printf("Vector capacity:   %zu operands\n", operands_.capacity());
		if (reserved_capacity_ > 0) {
			double usage_percent = (operands_.size() * 100.0) / reserved_capacity_;
			printf("Usage:             %.1f%% of reserved\n", usage_percent);
			if (operands_.size() > reserved_capacity_) {
				printf("WARNING: Exceeded reserved capacity by %zu operands\n",
				       operands_.size() - reserved_capacity_);
			}
		}
		printf("========================================\n\n");
	}

private:
	GlobalOperandStorage() = default;

	// Using vector for better performance with reserve()
	std::vector<IrOperand> operands_;
	size_t reserved_capacity_ = 0;
};

class OperandStorage {
public:
	OperandStorage() : start_index_(0), count_(0) {}

	// Constructor from vector (move semantics) - for backward compatibility
	explicit OperandStorage(std::vector<IrOperand>&& operands)
		: count_(operands.size()) {
		if (count_ > 0) {
			start_index_ = GlobalOperandStorage::instance().addOperands(std::move(operands));
		} else {
			start_index_ = 0;
		}
	}

	// Add operand directly (for builder pattern)
	void addOperand(IrOperand&& operand) {
		if (count_ == 0) {
			// First operand - record the start index
			start_index_ = GlobalOperandStorage::instance().addOperand(std::move(operand));
		} else {
			// Subsequent operands - they should be contiguous
			GlobalOperandStorage::instance().addOperand(std::move(operand));
		}
		++count_;
	}

	// Reserve space (no-op for chunked storage, but kept for API compatibility)
	void reserve(size_t capacity) {
		// No-op: deque doesn't need reservation
	}

	// Get operand count
	size_t size() const {
		return count_;
	}

	// Access operand by index
	const IrOperand& operator[](size_t index) const {
		return GlobalOperandStorage::instance().getOperand(start_index_ + index);
	}

	// Safe access with optional
	std::optional<IrOperand> getSafe(size_t index) const {
		return index < count_
			? std::optional<IrOperand>{ (*this)[index] }
			: std::optional<IrOperand>{};
	}

private:
	size_t start_index_;  // Index into global storage
	size_t count_;        // Number of operands
};

#endif

// ============================================================================
// Typed IR Operand Structures
// ============================================================================

// Type alias for operand values (subset of IrOperand variant)
using IrValue = std::variant<unsigned long long, double, TempVar, std::string_view>;

// Typed value - combines IrValue with its type information
struct TypedValue {
	Type type = Type::Void;	// 4 bytes (enum)
	int size_in_bits = 0;	// 4 bytes
	IrValue value;          // 32 bytes (variant)
	bool is_reference = false;  // True if this should be passed by reference (address)
	bool is_signed = false;     // True for signed types (use MOVSX), false for unsigned (use MOVZX)
	TypeIndex type_index = 0;   // Index into gTypeInfo for struct/enum types (0 = not set)
	int pointer_depth = 0;      // Number of pointer indirection levels (0 = not a pointer, 1 = T*, 2 = T**, etc.)
	CVQualifier cv_qualifier = CVQualifier::None;  // CV qualifier for references (const, volatile, etc.)
};

// Helper function to print TypedValue
inline void printTypedValue(std::ostringstream& oss, const TypedValue& typedValue) {
	if (std::holds_alternative<unsigned long long>(typedValue.value))
		oss << std::get<unsigned long long>(typedValue.value);
	else if (std::holds_alternative<double>(typedValue.value))
		oss << std::get<double>(typedValue.value);
	else if (std::holds_alternative<TempVar>(typedValue.value))
		oss << '%' << std::get<TempVar>(typedValue.value).var_number;
	else if (std::holds_alternative<std::string_view>(typedValue.value))
		oss << '%' << std::get<std::string_view>(typedValue.value);
	else
		assert(false && "unsupported typed value");
}

// Binary operations (Add, Subtract, Multiply, Divide, comparisons, etc.)
struct BinaryOp {
	TypedValue lhs;     // 40 bytes (value + type)
	TypedValue rhs;     // 40 bytes
	IrValue result;     // 32 bytes (changed from TempVar to support both temps and variables)
};

// Conditional branch (If)
struct CondBranchOp {
	std::string_view label_true;     // 16 bytes
	std::string_view label_false;    // 16 bytes
	TypedValue condition;            // 40 bytes (value + type)
};

// Function call
struct CallOp {
	std::string function_name;            // 32 bytes
	std::vector<TypedValue> args;         // 24 bytes (using TypedValue instead of CallArg)
	TempVar result;                       // 4 bytes
	Type return_type;                     // 4 bytes
	int return_size_in_bits;              // 4 bytes
	bool is_member_function = false;      // 1 byte
	bool is_variadic = false;             // 1 byte (+ 2 bytes padding)
};

// Member access (load member from struct/class)
struct MemberLoadOp {
	TypedValue result;                              // The loaded member value (type, size, result_var)
	std::variant<std::string_view, TempVar> object; // Base object instance
	std::string_view member_name;                   // Which member to access
	int offset;                                     // Byte offset in struct
	const TypeInfo* struct_type_info;               // Parent struct type (nullptr if not available)
	bool is_reference;                              // True if member is declared as T& (describes member declaration, not access)
	bool is_rvalue_reference;                       // True if member is declared as T&& (describes member declaration, not access)
};

// Member store (store value to struct/class member)
struct MemberStoreOp {
	TypedValue value;                               // Value to store (type, size, value_var)
	std::variant<std::string_view, TempVar> object; // Target object instance
	std::string_view member_name;                   // Which member to store to
	int offset;                                     // Byte offset in struct
	const TypeInfo* struct_type_info;               // Parent struct type (nullptr if not available)
	bool is_reference;                              // True if member is declared as T& (describes member declaration, not access)
	bool is_rvalue_reference;                       // True if member is declared as T&& (describes member declaration, not access)
	std::string_view vtable_symbol;                 // For vptr initialization - stores vtable symbol name
};

// Label definition
struct LabelOp {
	std::string_view label_name;
};

// Unconditional branch
struct BranchOp {
	std::string_view target_label;
};

// Return statement
struct ReturnOp {
	std::optional<IrValue> return_value;    // ~40 bytes
	std::optional<Type> return_type;        // ~8 bytes
	int return_size = 0;                    // 4 bytes
};

// Array access (load element from array)
struct ArrayAccessOp {
	TempVar result;                                  // Result temp var
	Type element_type;                               // Element type
	int element_size_in_bits;                        // Element size
	std::variant<std::string, std::string_view, TempVar> array;   // Array (owned string for member arrays, view for simple, or temp)
	TypedValue index;                                // Index value (type + value)
	int64_t member_offset;                           // Offset in bytes for member arrays (0 for non-member)
	bool is_pointer_to_array;                        // True if 'array' is a pointer (int* arr), false if actual array (int arr[])
};

// Array store (store value to array element)
struct ArrayStoreOp {
	Type element_type;                               // Element type
	int element_size_in_bits;                        // Element size
	std::variant<std::string, std::string_view, TempVar> array;   // Array (owned string for member arrays, view for simple, or temp)
	TypedValue index;                                // Index value (type + value)
	TypedValue value;                                // Value to store
	int64_t member_offset;                           // Offset in bytes for member arrays (0 for non-member)
	bool is_pointer_to_array;                        // True if 'array' is a pointer (int* arr), false if actual array (int arr[])
};

// Array element address (get address without loading)
struct ArrayElementAddressOp {
	TempVar result;                                  // Result temp var (pointer to element)
	Type element_type;                               // Element type
	int element_size_in_bits;                        // Element size
	std::variant<std::string_view, TempVar> array;   // Array (variable name or temp)
	TypedValue index;                                // Index value (type + value)
};

// Address-of operator (&x)
struct AddressOfOp {
	TempVar result;                                  // Result temp var (pointer)
	Type pointee_type;                               // Type of the variable we're taking address of
	int pointee_size_in_bits;                        // Size of pointee
	std::variant<std::string, std::string_view, TempVar> operand;  // Variable or temp to take address of
};

// Dereference operator (*ptr)
struct DereferenceOp {
	TempVar result;                                  // Result temp var
	Type pointee_type;                               // Type being dereferenced to
	int pointee_size_in_bits;                        // Size of dereferenced value
	std::variant<std::string_view, TempVar> pointer; // Pointer to dereference
};

// Dereference store operator (*ptr = value)
struct DereferenceStoreOp {
	TypedValue value;                                // Value to store
	Type pointee_type;                               // Type being stored to
	int pointee_size_in_bits;                        // Size of value
	std::variant<std::string_view, TempVar> pointer; // Pointer to store through
};

// Constructor call (invoke constructor on object)
struct ConstructorCallOp {
	std::string struct_name;                         // Name of struct/class
	std::variant<std::string_view, TempVar> object;  // Object instance ('this' or temp)
	std::vector<TypedValue> arguments;               // Constructor arguments
};

// Destructor call (invoke destructor on object)
struct DestructorCallOp {
	std::string struct_name;                         // Name of struct/class
	std::variant<std::string, TempVar> object;       // Object instance ('this' or temp)
};

// Virtual function call through vtable
struct VirtualCallOp {
	TypedValue result;                               // Return value (type, size, and result temp var)
	Type object_type;                                // Type of the object
	int object_size;                                 // Size of object in bits
	std::variant<std::string_view, TempVar> object;  // Object instance ('this')
	int vtable_index;                                // Index into vtable
	std::vector<TypedValue> arguments;               // Call arguments
};

// String literal
struct StringLiteralOp {
	std::variant<std::string_view, TempVar> result;  // Result variable
	std::string_view content;                         // String content
};

// Stack allocation
struct StackAllocOp {
	std::variant<std::string_view, TempVar> result;  // Variable name
	Type type = Type::Void;                           // Type being allocated
	int size_in_bits = 0;                             // Size in bits
};

// Assignment operation
// NOTE: There is no separate StoreOp - AssignmentOp handles both direct assignment and
// indirect stores (assignment through pointers). Use is_pointer_store=true for indirect stores.
// This design keeps the IR simpler while supporting:
// 1. Direct assignment: x = 5 (lhs is variable/tempvar)
// 2. Assignment through pointer: *ptr = 5 (lhs is pointer, is_pointer_store=true)
// 3. Reference member assignment: obj.ref = 5 (loads ref pointer, then stores through it)
struct AssignmentOp {
	std::variant<std::string_view, TempVar> result;  // Result variable (usually same as lhs)
	TypedValue lhs;                                   // Left-hand side (destination)
	TypedValue rhs;                                   // Right-hand side (source)
	bool is_pointer_store = false;                    // True if lhs is a pointer and we should store through it
};

// Loop begin (marks loop start with labels for break/continue)
struct LoopBeginOp {
	std::string_view loop_start_label;                    // Label for loop start
	std::string_view loop_end_label;                      // Label for break
	std::string_view loop_increment_label;                // Label for continue
};

// Function parameter information
struct FunctionParam {
	Type type = Type::Void;
	int size_in_bits = 0;
	int pointer_depth = 0;
	std::string name;
	bool is_reference = false;
	bool is_rvalue_reference = false;
	CVQualifier cv_qualifier = CVQualifier::None;
};

// Function declaration
struct FunctionDeclOp {
	Type return_type = Type::Void;
	int return_size_in_bits = 0;
	int return_pointer_depth = 0;
	std::string function_name;
	std::string struct_name;  // Empty for non-member functions
	Linkage linkage = Linkage::None;
	bool is_variadic = false;
	std::string_view mangled_name;
	std::vector<FunctionParam> parameters;
	int temp_var_stack_bytes = 0;  // Total stack space needed for TempVars (set after function body is processed)
};

// Unary operations (Negate, LogicalNot, BitwiseNot)
struct UnaryOp {
	TypedValue value;    // 40 bytes
	TempVar result;      // 4 bytes
};

// Helper function to format unary operations for IR output
inline std::string formatUnaryOp(const char* op_name, const UnaryOp& op) {
	std::ostringstream oss;
	
	// Result variable
	oss << '%' << op.result.var_number << " = " << op_name << " ";
	
	// Type and size
	auto type_info = gNativeTypes.find(op.value.type);
	if (type_info != gNativeTypes.end()) {
		oss << type_info->second->name_;
	}
	oss << op.value.size_in_bits << " ";
	
	// Operand value
	if (std::holds_alternative<TempVar>(op.value.value)) {
		oss << '%' << std::get<TempVar>(op.value.value).var_number;
	} else if (std::holds_alternative<std::string_view>(op.value.value)) {
		oss << '%' << std::get<std::string_view>(op.value.value);
	} else if (std::holds_alternative<unsigned long long>(op.value.value)) {
		oss << std::get<unsigned long long>(op.value.value);
	}
	
	return oss.str();
}

// Type conversion operations (SignExtend, ZeroExtend, Truncate)
struct ConversionOp {
	TypedValue from;     // 40 bytes (source type, size, and value)
	Type to_type = Type::Void;  // 4 bytes
	int to_size = 0;     // 4 bytes
	TempVar result;      // 4 bytes
};

// Global variable load
struct GlobalLoadOp {
	TypedValue result;           // Result with type, size, and temp var
	std::string_view global_name;  // Name of global variable (from source token or StringBuilder)
	bool is_array = false;       // If true, load address (LEA) instead of value (MOV)
};

// Function address (get address of a function)
struct FunctionAddressOp {
	TypedValue result;           // Result with type, size, and temp var (function pointer)
	std::string_view function_name;  // Function name
	std::string_view mangled_name;   // Pre-computed mangled name (optional, for lambdas)
};

// Variable declaration (local)
struct VariableDeclOp {
	Type type = Type::Void;
	int size_in_bits = 0;
	std::variant<std::string_view, std::string> var_name;
	unsigned long long custom_alignment = 0;
	bool is_reference = false;
	bool is_rvalue_reference = false;
	bool is_array = false;
	// Array info (if is_array)
	std::optional<Type> array_element_type;
	std::optional<int> array_element_size;
	std::optional<size_t> array_count;
	// Initializer (if present)
	std::optional<TypedValue> initializer;
};

// Global variable declaration
struct GlobalVariableDeclOp {
	Type type = Type::Void;
	int size_in_bits = 0;          // Size of one element in bits
	std::string_view var_name;
	bool is_initialized = false;
	size_t element_count = 1;       // Number of elements (1 for scalars, N for arrays)
	std::vector<char> init_data;    // Raw bytes for initialized data
};

// Heap allocation (new operator)
struct HeapAllocOp {
	TempVar result;              // Result pointer variable
	Type type = Type::Void;
	int size_in_bytes = 0;
	int pointer_depth = 0;
};

// Heap array allocation (new[] operator)
struct HeapAllocArrayOp {
	TempVar result;              // Result pointer variable
	Type type = Type::Void;
	int size_in_bytes = 0;
	int pointer_depth = 0;
	IrValue count;               // Array element count (TempVar or constant)
};

// Heap free (delete operator)
struct HeapFreeOp {
	IrValue pointer;             // Pointer to free (TempVar or string_view)
};

// Heap array free (delete[] operator)
struct HeapFreeArrayOp {
	IrValue pointer;             // Pointer to free (TempVar or string_view)
};

// Placement new operator
struct PlacementNewOp {
	TempVar result;              // Result pointer variable
	Type type = Type::Void;
	int size_in_bytes = 0;
	int pointer_depth = 0;
	IrValue address;             // Placement address (TempVar, string_view, or constant)
};

// Type conversion operations (FloatToInt, IntToFloat, FloatToFloat)
struct TypeConversionOp {
	TempVar result;              // Result variable
	TypedValue from;             // Source value with type information
	Type to_type = Type::Void;   // Target type
	int to_size_in_bits = 0;     // Target size
};

// RTTI: typeid operation
struct TypeidOp {
	TempVar result;              // Result variable (pointer to type_info)
	std::variant<std::string_view, TempVar> operand;  // Type name (string_view) or expression (TempVar)
	bool is_type = false;        // true if typeid(Type), false if typeid(expr)
};

// RTTI: dynamic_cast operation
struct DynamicCastOp {
	TempVar result;              // Result variable
	TempVar source;              // Source pointer/reference
	std::string target_type_name;  // Target type name
	bool is_reference = false;   // true for references (throws on failure), false for pointers (returns nullptr)
};

// Function pointer call
struct IndirectCallOp {
	TempVar result;                      // Result variable
	std::variant<std::string_view, TempVar> function_pointer;  // Function pointer variable
	std::vector<TypedValue> arguments;   // Arguments with type information
};

// Catch block begin marker
struct CatchBeginOp {
	TempVar exception_temp;       // Temporary holding the exception object
	TypeIndex type_index;         // Type to catch (0 for catch-all)
	std::string_view catch_end_label;  // Label to jump to if not matched
	bool is_const;                // True if caught by const
	bool is_reference;            // True if caught by lvalue reference  
	bool is_rvalue_reference;     // True if caught by rvalue reference
};

// Throw exception operation
struct ThrowOp {
	TypeIndex type_index;         // Type of exception being thrown
	size_t size_in_bytes;         // Size of exception object in bytes
	TempVar value;                // Temporary or value to throw
	bool is_rvalue;               // True if throwing an rvalue (can be moved)
};

// Helper function to format conversion operations for IR output
inline std::string formatConversionOp(const char* op_name, const ConversionOp& op) {
	std::ostringstream oss;
	
	// Result variable
	oss << '%' << op.result.var_number << " = " << op_name << " ";
	
	// From type and size
	auto from_type_info = gNativeTypes.find(op.from.type);
	if (from_type_info != gNativeTypes.end()) {
		oss << from_type_info->second->name_;
	}
	oss << op.from.size_in_bits << " ";
	
	// Source value
	if (std::holds_alternative<TempVar>(op.from.value)) {
		oss << '%' << std::get<TempVar>(op.from.value).var_number;
	} else if (std::holds_alternative<unsigned long long>(op.from.value)) {
		oss << std::get<unsigned long long>(op.from.value);
	} else if (std::holds_alternative<std::string_view>(op.from.value)) {
		oss << '%' << std::get<std::string_view>(op.from.value);
	}
	
	oss << " to ";
	
	// To type and size
	auto to_type_info = gNativeTypes.find(op.to_type);
	if (to_type_info != gNativeTypes.end()) {
		oss << to_type_info->second->name_;
	}
	oss << op.to_size;
	
	return oss.str();
}

// Helper function to format binary operations for IR output
inline std::string formatBinaryOp(const char* op_name, const BinaryOp& op) {
	std::ostringstream oss;
	
	// Result variable (now an IrValue that could be TempVar or string_view)
	oss << '%';
	if (std::holds_alternative<TempVar>(op.result)) {
		oss << std::get<TempVar>(op.result).var_number;
	} else if (std::holds_alternative<std::string_view>(op.result)) {
		oss << std::get<std::string_view>(op.result);
	}
	oss << " = " << op_name << " ";
	
	// Type and size (from LHS, but both sides should be same after type promotion)
	auto type_info = gNativeTypes.find(op.lhs.type);
	if (type_info != gNativeTypes.end()) {
		oss << type_info->second->name_;
	}
	oss << op.lhs.size_in_bits << " ";
	
	// LHS value
	if (std::holds_alternative<unsigned long long>(op.lhs.value)) {
		oss << std::get<unsigned long long>(op.lhs.value);
	} else if (std::holds_alternative<double>(op.lhs.value)) {
		oss << std::get<double>(op.lhs.value);
	} else if (std::holds_alternative<TempVar>(op.lhs.value)) {
		oss << '%' << std::get<TempVar>(op.lhs.value).var_number;
	} else if (std::holds_alternative<std::string_view>(op.lhs.value)) {
		oss << '%' << std::get<std::string_view>(op.lhs.value);
	}
	
	oss << ", ";
	
	// RHS value
	if (std::holds_alternative<unsigned long long>(op.rhs.value)) {
		oss << std::get<unsigned long long>(op.rhs.value);
	} else if (std::holds_alternative<double>(op.rhs.value)) {
		oss << std::get<double>(op.rhs.value);
	} else if (std::holds_alternative<TempVar>(op.rhs.value)) {
		oss << '%' << std::get<TempVar>(op.rhs.value).var_number;
	} else if (std::holds_alternative<std::string_view>(op.rhs.value)) {
		oss << '%' << std::get<std::string_view>(op.rhs.value);
	}
	
	return oss.str();
}

// Helper functions for converting enums to strings (for IR printing)
inline std::string linkageToString(Linkage linkage) {
	switch (linkage) {
		case Linkage::None: return "";
		case Linkage::C: return "extern \"C\"";
		case Linkage::CPlusPlus: return "";  // Default C++ linkage
		case Linkage::DllImport: return "dllimport";
		case Linkage::DllExport: return "dllexport";
		default: return "";
	}
}

inline std::string cvQualifierToString(CVQualifier cv) {
	switch (cv) {
		case CVQualifier::None: return "";
		case CVQualifier::Const: return "const";
		case CVQualifier::Volatile: return "volatile";
		case CVQualifier::ConstVolatile: return "const volatile";
		default: return "";
	}
}

// Helper function to format call operations for IR output
// ============================================================================
// Typed IR Operand Payload - Optional typed alternative to vector operands
// ============================================================================
// Use std::any to store typed payloads (handles incomplete types)

class IrInstruction {
public:
	// Constructor from vector (backward compatibility)
	IrInstruction(IrOpcode opcode, std::vector<IrOperand>&& operands, Token first_token)
		: opcode_(opcode), operands_(std::move(operands)), first_token_(first_token) {}

	// Builder-style constructor (no temporary vector allocation)
	IrInstruction(IrOpcode opcode, Token first_token, size_t expected_operand_count = 0)
		: opcode_(opcode), operands_(), first_token_(first_token) {
		if (expected_operand_count > 0) {
			operands_.reserve(expected_operand_count);
		}
	}

	// Typed constructor - defined in IROperandHelpers.h where types are complete
	template<typename PayloadType>
	IrInstruction(IrOpcode opcode, PayloadType&& payload, Token first_token);

	// Add operand (builder pattern)
	void addOperand(IrOperand&& operand) {
		operands_.addOperand(std::move(operand));
	}

	// Convenience template for adding operands
	template<typename T>
	void addOperand(T&& value) {
		operands_.addOperand(IrOperand(std::forward<T>(value)));
	}

	IrOpcode getOpcode() const { return opcode_; }
	size_t getOperandCount() const { return operands_.size(); }
	size_t getLineNumber() const { return first_token_.line(); }

	std::optional<IrOperand> getOperandSafe(size_t index) const {
		return operands_.getSafe(index);
	}

	const IrOperand& getOperand(size_t index) const {
		return operands_[index];
	}

	template<class TClass>
	const TClass& getOperandAs(size_t index) const {
		return std::get<TClass>(operands_[index]);
	}

	// Safe version of getOperandAs for int - returns default value if not found or wrong type
	int getOperandAsIntSafe(size_t index, int default_value = 0) const {
		if (index >= operands_.size())
			return default_value;
		if (isOperandType<int>(index))
			return getOperandAs<int>(index);
		return default_value;
	}

	std::string_view getOperandAsTypeString(size_t index) const {
		if (index >= operands_.size())
			return "";

		if (!isOperandType<Type>(index))
			return "<not-a-type>";

		const Type type = getOperandAs<Type>(index);
		auto type_info = gNativeTypes.find(type);
		if (type_info == gNativeTypes.end())
			return "";

		return type_info->second->name_;
	}

	template<class TClass>
	bool isOperandType(size_t index) const {
		return std::holds_alternative<TClass>(operands_[index]);
	}

	std::string getReadableString() const {
		std::ostringstream oss{};

		switch (opcode_) {
		case IrOpcode::Add:
			oss << formatBinaryOp("add", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::Subtract:
			oss << formatBinaryOp("sub", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::Multiply:
			oss << formatBinaryOp("mul", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::Divide:
			oss << formatBinaryOp("div", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedDivide:
			oss << formatBinaryOp("udiv", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::ShiftLeft:
			oss << formatBinaryOp("shl", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::ShiftRight:
			oss << formatBinaryOp("shr", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedShiftRight:
			oss << formatBinaryOp("lshr", getTypedPayload<BinaryOp>());
			break;

		case IrOpcode::Modulo:
			oss << formatBinaryOp("srem", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::BitwiseAnd:
			oss << formatBinaryOp("and", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::BitwiseOr:
			oss << formatBinaryOp("or", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::BitwiseXor:
			oss << formatBinaryOp("xor", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::BitwiseNot:
			oss << formatUnaryOp("not", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::Equal:
			oss << formatBinaryOp("icmp eq", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::NotEqual:
			oss << formatBinaryOp("icmp ne", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::LessThan:
			oss << formatBinaryOp("icmp slt", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::LessEqual:
			oss << formatBinaryOp("icmp sle", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::GreaterThan:
			oss << formatBinaryOp("icmp sgt", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::GreaterEqual:
			oss << formatBinaryOp("icmp sge", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedLessThan:
			oss << formatBinaryOp("icmp ult", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedLessEqual:
			oss << formatBinaryOp("icmp ule", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedGreaterThan:
			oss << formatBinaryOp("icmp ugt", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedGreaterEqual:
			oss << formatBinaryOp("icmp uge", getTypedPayload<BinaryOp>());
			break;

		case IrOpcode::LogicalAnd:
			oss << formatBinaryOp("and i1", getTypedPayload<BinaryOp>());
			break;

		case IrOpcode::LogicalOr:
			oss << formatBinaryOp("or i1", getTypedPayload<BinaryOp>());
			break;

		case IrOpcode::LogicalNot:
			oss << formatUnaryOp("lnot", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::Negate:
			oss << formatUnaryOp("neg", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::SignExtend:
			oss << formatConversionOp("sext", getTypedPayload<ConversionOp>());
			break;
		case IrOpcode::ZeroExtend:
			oss << formatConversionOp("zext", getTypedPayload<ConversionOp>());
			break;
		case IrOpcode::Truncate:
			oss << formatConversionOp("trunc", getTypedPayload<ConversionOp>());
			break;
		
		case IrOpcode::Return:
		{
			const auto& op = getTypedPayload<ReturnOp>();
			oss << "ret ";
		
			if (op.return_value.has_value() && op.return_type.has_value()) {
				// Return with value
				auto type_info = gNativeTypes.find(op.return_type.value());
				if (type_info != gNativeTypes.end()) {
					oss << type_info->second->name_;
				}
				oss << op.return_size << " ";
			
				const auto& val = op.return_value.value();
				if (std::holds_alternative<unsigned long long>(val)) {
					oss << std::get<unsigned long long>(val);
				} else if (std::holds_alternative<TempVar>(val)) {
					oss << '%' << std::get<TempVar>(val).var_number;
				} else if (std::holds_alternative<std::string_view>(val)) {
					oss << '%' << std::get<std::string_view>(val);
				} else if (std::holds_alternative<double>(val)) {
					oss << std::get<double>(val);
				}
			} else {
				// Void return
				oss << "void";
			}
		}
		break;
		
		case IrOpcode::FunctionDecl:
		{
			const auto& op = getTypedPayload<FunctionDeclOp>();
		
			// Linkage
			oss << "define ";
			if (op.linkage != Linkage::None && op.linkage != Linkage::CPlusPlus) {
				oss << linkageToString(op.linkage) << " ";
			}
		
			// Return type
			auto ret_type_info = gNativeTypes.find(op.return_type);
			if (ret_type_info != gNativeTypes.end()) {
				oss << ret_type_info->second->name_;
			}
			for (size_t i = 0; i < op.return_pointer_depth; ++i) {
				oss << "*";
			}
			oss << op.return_size_in_bits << " ";
		
			// Function name
			oss << "@";
			if (!op.mangled_name.empty()) {
				oss << op.mangled_name;
			} else {
				oss << op.function_name;
			}
			oss << "(";
		
			// Parameters
			for (size_t i = 0; i < op.parameters.size(); ++i) {
				if (i > 0) oss << ", ";
			
				const auto& param = op.parameters[i];
			
				// Type
				auto param_type_info = gNativeTypes.find(param.type);
				if (param_type_info != gNativeTypes.end()) {
					oss << param_type_info->second->name_;
				}
				for (size_t j = 0; j < param.pointer_depth; ++j) {
					oss << "*";
				}
				oss << param.size_in_bits;
			
				// Reference qualifiers
				if (param.is_rvalue_reference) {
					oss << "&&";
				} else if (param.is_reference) {
					oss << "&";
				}
			
				// CV qualifiers
				if (param.cv_qualifier != CVQualifier::None) {
					oss << " " << cvQualifierToString(param.cv_qualifier);
				}
			
				// Name
				if (!param.name.empty()) {
					oss << " %" << param.name;
				}
			}
		
			if (op.is_variadic) {
				if (!op.parameters.empty()) oss << ", ";
				oss << "...";
			}
		
			oss << ")";
		
			// Struct context
			if (!op.struct_name.empty()) {
				oss << " [" << op.struct_name << "]";
			}
		}
		break;
		
		case IrOpcode::FunctionCall:
		{
			const auto& op = getTypedPayload<CallOp>();
	
			// Result variable
			oss << '%' << op.result.var_number << " = call @" << op.function_name << "(";
	
			// Arguments
			for (size_t i = 0; i < op.args.size(); ++i) {
				if (i > 0) oss << ", ";
		
				const auto& arg = op.args[i];
		
				// Type and size
				auto type_info = gNativeTypes.find(arg.type);
				if (type_info != gNativeTypes.end()) {
					oss << type_info->second->name_;
				}
				oss << arg.size_in_bits << " ";
		
				// Value - use the helper function that handles all types including double
				printTypedValue(oss, arg);
			}
	
			oss << ")";
		}
		break;
		
		case IrOpcode::StackAlloc:
		{
			const StackAllocOp& op = getTypedPayload<StackAllocOp>();
			// %name = alloca [Type][SizeInBits]
			oss << '%';
			if (std::holds_alternative<std::string_view>(op.result))
				oss << std::get<std::string_view>(op.result);
			else
				oss << std::get<TempVar>(op.result).var_number;
			oss << " = alloca ";
			auto type_info = gNativeTypes.find(op.type);
			if (type_info != gNativeTypes.end())
				oss << type_info->second->name_;
			oss << op.size_in_bits;
		}
		break;

		case IrOpcode::Branch:
		{
			const auto& op = getTypedPayload<BranchOp>();
			oss << "br label %" << op.target_label;
		}
		break;
		
		case IrOpcode::ConditionalBranch:
		{
			const auto& op = getTypedPayload<CondBranchOp>();
			oss << "br i1 ";
		
			// Condition value
			const auto& val = op.condition.value;
			if (std::holds_alternative<unsigned long long>(val)) {
				oss << std::get<unsigned long long>(val);
			} else if (std::holds_alternative<TempVar>(val)) {
				oss << '%' << std::get<TempVar>(val).var_number;
			} else if (std::holds_alternative<std::string_view>(val)) {
				oss << '%' << std::get<std::string_view>(val);
			}
		
			oss << ", label %" << op.label_true;
			oss << ", label %" << op.label_false;
		}
		break;
		
		case IrOpcode::Label:
		{
			const auto& op = getTypedPayload<LabelOp>();
			oss << op.label_name << ":";
		}
		break;
		
		case IrOpcode::LoopBegin:
		{
			assert(hasTypedPayload() && "LoopBegin instruction must use typed payload");
			const auto& op = getTypedPayload<LoopBeginOp>();
			oss << "loop_begin %" << op.loop_start_label
				<< " %" << op.loop_end_label
				<< " %" << op.loop_increment_label;
		}
		break;

		case IrOpcode::LoopEnd:
		{
			// loop_end (no operands)
			assert(getOperandCount() == 0 && "LoopEnd instruction must have exactly 0 operands");
			oss << "loop_end";
		}
		break;

		case IrOpcode::ScopeBegin:
		{
			// scope_begin (no operands)
			assert(getOperandCount() == 0 && "ScopeBegin instruction must have exactly 0 operands");
			oss << "scope_begin";
		}
		break;

		case IrOpcode::ScopeEnd:
		{
			// scope_end (no operands)
			assert(getOperandCount() == 0 && "ScopeEnd instruction must have exactly 0 operands");
			oss << "scope_end";
		}
		break;

		case IrOpcode::Break:
		{
			// break (no operands - uses loop context stack)
			assert(getOperandCount() == 0 && "Break instruction must have exactly 0 operands");
			oss << "break";
		}
		break;

		case IrOpcode::Continue:
		{
			// continue (no operands - uses loop context stack)
			assert(getOperandCount() == 0 && "Continue instruction must have exactly 0 operands");
			oss << "continue";
		}
		break;

		case IrOpcode::ArrayAccess:
		{
			assert (hasTypedPayload() && "expected ArrayAccess to have typed payload");
			const ArrayAccessOp& op = std::any_cast<const ArrayAccessOp&>(getTypedPayload());
			oss << '%' << op.result.var_number << " = array_access ";
			oss << "[" << static_cast<int>(op.element_type) << "][" << op.element_size_in_bits << "] ";
				
			if (std::holds_alternative<std::string>(op.array)) {
				std::cerr << "Error: ArrayAccessOp.array should use string_view (via StringBuilder), not std::string\n";
				assert(false && "ArrayAccessOp.array should use string_view (via StringBuilder), not std::string");
			}
			else if (std::holds_alternative<std::string_view>(op.array))
				oss << '%' << std::get<std::string_view>(op.array);
			else
				oss << '%' << std::get<TempVar>(op.array).var_number;
				
			oss << ", [" << static_cast<int>(op.index.type) << "][" << op.index.size_in_bits << "] ";
				
			if (std::holds_alternative<unsigned long long>(op.index.value))
				oss << std::get<unsigned long long>(op.index.value);
			else if (std::holds_alternative<TempVar>(op.index.value))
				oss << '%' << std::get<TempVar>(op.index.value).var_number;
			else if (std::holds_alternative<std::string_view>(op.index.value))
				oss << '%' << std::get<std::string_view>(op.index.value);
		}
		break;

		case IrOpcode::ArrayStore:
		{
			assert (hasTypedPayload() && "expected ArrayStore to have typed payload");
			const ArrayStoreOp& op = std::any_cast<const ArrayStoreOp&>(getTypedPayload());
			oss << "array_store [" << static_cast<int>(op.element_type) << "][" << op.element_size_in_bits << "] ";
				
			if (std::holds_alternative<std::string>(op.array)) {
				std::cerr << "Error: ArrayStoreOp.array should use string_view (via StringBuilder), not std::string\n";
				assert(false && "ArrayStoreOp.array should use string_view (via StringBuilder), not std::string");
			}
			else if (std::holds_alternative<std::string_view>(op.array))
				oss << '%' << std::get<std::string_view>(op.array);
			else
				oss << '%' << std::get<TempVar>(op.array).var_number;
				
			oss << ", [" << static_cast<int>(op.index.type) << "][" << op.index.size_in_bits << "] ";
			
			printTypedValue(oss, op.index);
				
			oss << ", [" << static_cast<int>(op.value.type) << "][" << op.value.size_in_bits << "] ";
			
			printTypedValue(oss, op.value);
			break;
		}
		break;

		case IrOpcode::ArrayElementAddress:
		{
			assert(hasTypedPayload() && "ArrayElementAddress instruction must use typed payload");
			const auto& op = getTypedPayload<ArrayElementAddressOp>();
			oss << '%' << op.result.var_number << " = array_element_address ";
			oss << "[" << static_cast<int>(op.element_type) << "]" << op.element_size_in_bits << " ";
			
			// Array
			if (std::holds_alternative<std::string_view>(op.array))
				oss << '%' << std::get<std::string_view>(op.array);
			else if (std::holds_alternative<TempVar>(op.array))
				oss << '%' << std::get<TempVar>(op.array).var_number;
			
			oss << "[";
			printTypedValue(oss, op.index);
			oss << "]";
		}
		break;

		case IrOpcode::AddressOf:
		{
			assert(hasTypedPayload() && "AddressOf instruction must use typed payload");
			const auto& op = getTypedPayload<AddressOfOp>();
			oss << '%' << op.result.var_number << " = addressof ";
			oss << "[" << static_cast<int>(op.pointee_type) << "]" << op.pointee_size_in_bits << " ";
		
			if (std::holds_alternative<std::string>(op.operand))
				oss << '%' << std::get<std::string>(op.operand);
			else if (std::holds_alternative<std::string_view>(op.operand))
				oss << '%' << std::get<std::string_view>(op.operand);
			else if (std::holds_alternative<TempVar>(op.operand))
				oss << '%' << std::get<TempVar>(op.operand).var_number;
		}
		break;
	
		case IrOpcode::Dereference:
		{
			assert(hasTypedPayload() && "Dereference instruction must use typed payload");
			const auto& op = getTypedPayload<DereferenceOp>();
			oss << '%' << op.result.var_number << " = dereference ";
			oss << "[" << static_cast<int>(op.pointee_type) << "]" << op.pointee_size_in_bits << " ";
		
			if (std::holds_alternative<std::string_view>(op.pointer))
				oss << '%' << std::get<std::string_view>(op.pointer);
			else if (std::holds_alternative<TempVar>(op.pointer))
				oss << '%' << std::get<TempVar>(op.pointer).var_number;
		}
		break;

		case IrOpcode::DereferenceStore:
		{
			assert(hasTypedPayload() && "DereferenceStore instruction must use typed payload");
			const auto& op = getTypedPayload<DereferenceStoreOp>();
			oss << "store_through_ptr [" << static_cast<int>(op.pointee_type) << "]" << op.pointee_size_in_bits << " ";
		
			if (std::holds_alternative<std::string_view>(op.pointer))
				oss << "%" << std::get<std::string_view>(op.pointer);
			else if (std::holds_alternative<TempVar>(op.pointer))
				oss << "%" << std::get<TempVar>(op.pointer).var_number;
			
			oss << ", ";
			
			// Value being stored
			if (std::holds_alternative<unsigned long long>(op.value.value))
				oss << std::get<unsigned long long>(op.value.value);
			else if (std::holds_alternative<TempVar>(op.value.value))
				oss << "%" << std::get<TempVar>(op.value.value).var_number;
			else if (std::holds_alternative<std::string_view>(op.value.value))
				oss << "%" << std::get<std::string_view>(op.value.value);
		}
		break;
		
		case IrOpcode::MemberAccess:
		{
			// %result = member_access [MemberType][MemberSize] %object.member_name (offset: N) [ref]
			assert(hasTypedPayload() && "MemberAccess instruction must use typed payload");
			const auto& op = getTypedPayload<MemberLoadOp>();
		
			oss << '%';
			if (std::holds_alternative<TempVar>(op.result.value))
				oss << std::get<TempVar>(op.result.value).var_number;
			else if (std::holds_alternative<std::string_view>(op.result.value))
				oss << std::get<std::string_view>(op.result.value);

			oss << " = member_access ";
		
			// Type and size
			auto type_info = gNativeTypes.find(op.result.type);
			if (type_info != gNativeTypes.end()) {
				oss << type_info->second->name_;
			}
			oss << op.result.size_in_bits << " ";

			// Object
			if (std::holds_alternative<TempVar>(op.object))
				oss << '%' << std::get<TempVar>(op.object).var_number;
			else if (std::holds_alternative<std::string_view>(op.object))
				oss << '%' << std::get<std::string_view>(op.object);

			oss << "." << op.member_name;
			oss << " (offset: " << op.offset << ")";
			if (op.is_reference) {
				oss << " [ref]";
			}
			if (op.is_rvalue_reference) {
				oss << " [rvalue_ref]";
			}
		}
		break;
		
		case IrOpcode::MemberStore:
		{
			// member_store [MemberType][MemberSize] %object.member_name (offset: N) [ref], %value
			assert(hasTypedPayload() && "MemberStore instruction must use typed payload");
			const auto& op = getTypedPayload<MemberStoreOp>();
		
			oss << "member_store ";
		
			// Type and size
			auto type_info = gNativeTypes.find(op.value.type);
			if (type_info != gNativeTypes.end()) {
				oss << type_info->second->name_;
			}
			oss << op.value.size_in_bits << " ";

			// Object
			if (std::holds_alternative<TempVar>(op.object))
				oss << '%' << std::get<TempVar>(op.object).var_number;
			else if (std::holds_alternative<std::string_view>(op.object))
				oss << '%' << std::get<std::string_view>(op.object);

			oss << "." << op.member_name;
			oss << " (offset: " << op.offset << ")";
			if (op.is_reference) {
				oss << " [ref]";
			}
			if (op.is_rvalue_reference) {
				oss << " [rvalue_ref]";
			}
			oss << ", ";

			// Value - use printTypedValue helper
			printTypedValue(oss, op.value);
		}
		break;
		
		case IrOpcode::ConstructorCall:
		{
			// constructor_call StructName %object_var [param1_type, param1_size, param1_value, ...]
			const ConstructorCallOp& op = getTypedPayload<ConstructorCallOp>();
			oss << "constructor_call " << op.struct_name << " %";

			// Object can be either string_view or TempVar
			if (std::holds_alternative<std::string_view>(op.object))
				oss << std::get<std::string_view>(op.object);
			else if (std::holds_alternative<TempVar>(op.object))
				oss << std::get<TempVar>(op.object).var_number;

			// Add constructor arguments
			for (const auto& arg : op.arguments) {
				auto type_it = gNativeTypes.find(arg.type);
				oss << " ";
				if (type_it != gNativeTypes.end()) {
					oss << type_it->second;
				} else if (arg.type == Type::Struct || arg.type == Type::Enum) {
					// Try to get the type name from gTypeInfo using type_index
					if (arg.type_index > 0 && arg.type_index < gTypeInfo.size()) {
						oss << gTypeInfo[arg.type_index].name_;
					} else {
						oss << (arg.type == Type::Struct ? "struct" : "enum");
					}
				}
				oss << arg.size_in_bits << " ";
				// Print the IrValue directly (not TypedValue)
				if (std::holds_alternative<TempVar>(arg.value))
					oss << '%' << std::get<TempVar>(arg.value).var_number;
				else if (std::holds_alternative<std::string_view>(arg.value))
					oss << '%' << std::get<std::string_view>(arg.value);
				else if (std::holds_alternative<unsigned long long>(arg.value))
					oss << std::get<unsigned long long>(arg.value);
				else if (std::holds_alternative<double>(arg.value))
					oss << std::get<double>(arg.value);
			}
		}
		break;

		case IrOpcode::DestructorCall:
		{
			// destructor_call StructName %object_var
			const DestructorCallOp& op = getTypedPayload<DestructorCallOp>();
			oss << "destructor_call " << op.struct_name << " %";

			// Object can be either string or TempVar
			if (std::holds_alternative<std::string>(op.object))
				oss << std::get<std::string>(op.object);
			else if (std::holds_alternative<TempVar>(op.object))
				oss << std::get<TempVar>(op.object).var_number;
		}
		break;

		case IrOpcode::VirtualCall:
		{
			// %result = virtual_call %object, vtable_index, [args...]
			const VirtualCallOp& op = getTypedPayload<VirtualCallOp>();
			assert(std::holds_alternative<TempVar>(op.result.value) && "VirtualCallOp result must be a TempVar");
			oss << '%' << std::get<TempVar>(op.result.value).var_number << " = virtual_call ";

			// Object type and size
			auto type_info = gNativeTypes.find(op.object_type);
			if (type_info != gNativeTypes.end()) {
				oss << type_info->second->name_;
			}
			oss << op.object_size << " %";
			
			// Object (this pointer)
			if (std::holds_alternative<TempVar>(op.object))
				oss << std::get<TempVar>(op.object).var_number;
			else if (std::holds_alternative<std::string_view>(op.object))
				oss << std::get<std::string_view>(op.object);

			// VTable index
			oss << ", vtable[" << op.vtable_index << "]";

			// Arguments (if any)
			if (!op.arguments.empty()) {
				oss << "(";
				for (size_t i = 0; i < op.arguments.size(); ++i) {
					if (i > 0) oss << ", ";
				
					const auto& arg = op.arguments[i];
				
					// Type and size
					auto arg_type_info = gNativeTypes.find(arg.type);
					if (arg_type_info != gNativeTypes.end()) {
						oss << arg_type_info->second->name_;
					}
					oss << arg.size_in_bits << " ";
				
					// Value
					if (std::holds_alternative<unsigned long long>(arg.value))
						oss << std::get<unsigned long long>(arg.value);
					else if (std::holds_alternative<TempVar>(arg.value))
						oss << '%' << std::get<TempVar>(arg.value).var_number;
					else if (std::holds_alternative<std::string_view>(arg.value))
						oss << '%' << std::get<std::string_view>(arg.value);
				}
				oss << ")";
			}
		}
		break;

		case IrOpcode::StringLiteral:
		{
			// %result = string_literal "content"
			const StringLiteralOp& op = getTypedPayload<StringLiteralOp>();
			oss << '%';
		
			if (std::holds_alternative<TempVar>(op.result))
				oss << std::get<TempVar>(op.result).var_number;
			else if (std::holds_alternative<std::string_view>(op.result))
				oss << std::get<std::string_view>(op.result);

			oss << " = string_literal " << op.content;
		}
		break;
		
		case IrOpcode::HeapAlloc:
		{
			// %result = heap_alloc [Type][Size][PointerDepth]
			const HeapAllocOp& op = getTypedPayload<HeapAllocOp>();
			oss << '%' << op.result.var_number << " = heap_alloc [" 
				<< static_cast<int>(op.type) << "][" 
				<< op.size_in_bytes << "][" << op.pointer_depth << "]";
		}
		break;
		
		case IrOpcode::HeapAllocArray:
		{
			// %result = heap_alloc_array [Type][Size][PointerDepth] %count
			const HeapAllocArrayOp& op = getTypedPayload<HeapAllocArrayOp>();
			oss << '%' << op.result.var_number << " = heap_alloc_array [" 
				<< static_cast<int>(op.type) << "][" 
				<< op.size_in_bytes << "][" << op.pointer_depth << "] ";
		
			if (std::holds_alternative<TempVar>(op.count))
				oss << '%' << std::get<TempVar>(op.count).var_number;
			else if (std::holds_alternative<unsigned long long>(op.count))
				oss << std::get<unsigned long long>(op.count);
			else if (std::holds_alternative<std::string_view>(op.count))
				oss << '%' << std::get<std::string_view>(op.count);
		}
		break;
		
		case IrOpcode::HeapFree:
		{
			// heap_free %ptr
			const HeapFreeOp& op = getTypedPayload<HeapFreeOp>();
			oss << "heap_free ";
			if (std::holds_alternative<TempVar>(op.pointer))
				oss << '%' << std::get<TempVar>(op.pointer).var_number;
			else if (std::holds_alternative<std::string_view>(op.pointer))
				oss << '%' << std::get<std::string_view>(op.pointer);
		}
		break;
		
		case IrOpcode::HeapFreeArray:
		{
			// heap_free_array %ptr
			const HeapFreeArrayOp& op = getTypedPayload<HeapFreeArrayOp>();
			oss << "heap_free_array ";
			if (std::holds_alternative<TempVar>(op.pointer))
				oss << '%' << std::get<TempVar>(op.pointer).var_number;
			else if (std::holds_alternative<std::string_view>(op.pointer))
				oss << '%' << std::get<std::string_view>(op.pointer);
		}
		break;
		
		case IrOpcode::PlacementNew:
		{
			// %result = placement_new %address [Type][Size]
			const PlacementNewOp& op = getTypedPayload<PlacementNewOp>();
			oss << '%' << op.result.var_number << " = placement_new ";
			if (std::holds_alternative<TempVar>(op.address))
				oss << '%' << std::get<TempVar>(op.address).var_number;
			else if (std::holds_alternative<std::string_view>(op.address))
				oss << '%' << std::get<std::string_view>(op.address);
			else if (std::holds_alternative<unsigned long long>(op.address))
				oss << std::get<unsigned long long>(op.address);
			oss << " [" << static_cast<int>(op.type) << "][" << op.size_in_bytes << "]";
		}
		break;
		
		case IrOpcode::Typeid:
		{
			// %result = typeid [type_name_or_expr] [is_type]
			auto& op = getTypedPayload<TypeidOp>();
			oss << '%' << op.result.var_number << " = typeid ";
			if (std::holds_alternative<std::string_view>(op.operand)) {
				oss << std::get<std::string_view>(op.operand);
			} else {
				oss << '%' << std::get<TempVar>(op.operand).var_number;
			}
			oss << " [is_type=" << (op.is_type ? "true" : "false") << "]";
		}
		break;

		case IrOpcode::DynamicCast:
		{
			// %result = dynamic_cast %source_ptr [target_type] [is_reference]
			auto& op = getTypedPayload<DynamicCastOp>();
			oss << '%' << op.result.var_number << " = dynamic_cast %" << op.source.var_number;
			oss << " [" << op.target_type_name << "]";
			oss << " [is_ref=" << (op.is_reference ? "true" : "false") << "]";
		}
		break;

		case IrOpcode::PreIncrement:
			oss << formatUnaryOp("pre_inc", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::PostIncrement:
			oss << formatUnaryOp("post_inc", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::PreDecrement:
			oss << formatUnaryOp("pre_dec", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::PostDecrement:
			oss << formatUnaryOp("post_dec", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::AddAssign:
			oss << formatBinaryOp("add", getTypedPayload<BinaryOp>());
			break;	case IrOpcode::SubAssign:
			oss << formatBinaryOp("sub", getTypedPayload<BinaryOp>());
			break;	case IrOpcode::MulAssign:
			oss << formatBinaryOp("mul", getTypedPayload<BinaryOp>());
			break;		case IrOpcode::DivAssign:
				oss << formatBinaryOp("sdiv", getTypedPayload<BinaryOp>());
				break;

		case IrOpcode::ModAssign:
			oss << formatBinaryOp("srem", getTypedPayload<BinaryOp>());
			break;	case IrOpcode::AndAssign:
			oss << formatBinaryOp("and", getTypedPayload<BinaryOp>());
			break;	case IrOpcode::OrAssign:
			oss << formatBinaryOp("or", getTypedPayload<BinaryOp>());
			break;	case IrOpcode::XorAssign:
			oss << formatBinaryOp("xor", getTypedPayload<BinaryOp>());
			break;	case IrOpcode::ShlAssign:
			oss << formatBinaryOp("shl", getTypedPayload<BinaryOp>());
			break;
			
		case IrOpcode::ShrAssign:
			oss << formatBinaryOp("ashr", getTypedPayload<BinaryOp>());
			break;

		// Float arithmetic operations
		case IrOpcode::FloatAdd:
			oss << formatBinaryOp("fadd", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatSubtract:
			oss << formatBinaryOp("fsub", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatMultiply:
			oss << formatBinaryOp("fmul", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatDivide:
			oss << formatBinaryOp("fdiv", getTypedPayload<BinaryOp>());
			break;

		// Float comparison operations
		case IrOpcode::FloatEqual:
			oss << formatBinaryOp("fcmp oeq", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatNotEqual:
			oss << formatBinaryOp("fcmp one", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatLessThan:
			oss << formatBinaryOp("fcmp olt", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatLessEqual:
			oss << formatBinaryOp("fcmp ole", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatGreaterThan:
			oss << formatBinaryOp("fcmp ogt", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatGreaterEqual:
			oss << formatBinaryOp("fcmp oge", getTypedPayload<BinaryOp>());
			break;

		case IrOpcode::Assignment:
		{
			const AssignmentOp& op = getTypedPayload<AssignmentOp>();
			// assign %lhs = %rhs (simple assignment a = b)
			oss << "assign %";

			// Print LHS
			if (std::holds_alternative<TempVar>(op.lhs.value))
				oss << std::get<TempVar>(op.lhs.value).var_number;
			else if (std::holds_alternative<std::string_view>(op.lhs.value))
				oss << std::get<std::string_view>(op.lhs.value);
			else if (std::holds_alternative<unsigned long long>(op.lhs.value))
				oss << std::get<unsigned long long>(op.lhs.value);

			oss << " = ";

			// Print RHS
			if (std::holds_alternative<unsigned long long>(op.rhs.value))
				oss << std::get<unsigned long long>(op.rhs.value);
			else if (std::holds_alternative<TempVar>(op.rhs.value))
				oss << '%' << std::get<TempVar>(op.rhs.value).var_number;
			else if (std::holds_alternative<std::string_view>(op.rhs.value))
				oss << '%' << std::get<std::string_view>(op.rhs.value);
			else if (std::holds_alternative<double>(op.rhs.value))
				oss << std::get<double>(op.rhs.value);
		}
		break;
		
		case IrOpcode::VariableDecl:
		{
			const VariableDeclOp& op = getTypedPayload<VariableDeclOp>();
			oss << "%";
			if (std::holds_alternative<std::string_view>(op.var_name))
				oss << std::get<std::string_view>(op.var_name);
			else
				oss << std::get<std::string>(op.var_name);
			oss << " = alloc ";
			
			if (op.is_array && op.array_count.has_value()) {
				// For arrays, print element type and count: int32[5]
				auto type_info = gNativeTypes.find(op.type);
				if (type_info != gNativeTypes.end())
					oss << type_info->second->name_;
				oss << op.size_in_bits << "[" << op.array_count.value() << "]";
			} else {
				// For scalars, print type and size: int32
				auto type_info = gNativeTypes.find(op.type);
				if (type_info != gNativeTypes.end())
					oss << type_info->second->name_;
				oss << op.size_in_bits;
			}
			
			if (op.custom_alignment > 0) {
				oss << " alignas(" << op.custom_alignment << ")";
			}
			oss << (op.is_reference ? " [&]" : "");
			if (op.initializer.has_value()) {
				oss << "\nassign %";
				if (std::holds_alternative<std::string_view>(op.var_name))
					oss << std::get<std::string_view>(op.var_name);
				else
					oss << std::get<std::string>(op.var_name);
				oss << " = ";
				const auto& init = op.initializer.value();
				// Check if operand is a literal value or a variable/TempVar
				if (std::holds_alternative<unsigned long long>(init.value))
					oss << std::get<unsigned long long>(init.value);
				else if (std::holds_alternative<double>(init.value))
					oss << std::get<double>(init.value);
				else if (std::holds_alternative<TempVar>(init.value))
					oss << '%' << std::get<TempVar>(init.value).var_number;
				else if (std::holds_alternative<std::string_view>(init.value))
					oss << '%' << std::get<std::string_view>(init.value);
			}
			break;
		}
	
		case IrOpcode::GlobalVariableDecl:
		{
			const GlobalVariableDeclOp& op = getTypedPayload<GlobalVariableDeclOp>();
			std::string_view var_name = op.var_name;
			
			oss << "global_var ";
			auto type_info = gNativeTypes.find(op.type);
			if (type_info != gNativeTypes.end())
				oss << type_info->second->name_;
			oss << op.size_in_bits << " @" << std::string(var_name);
			if (op.element_count > 1) {
				oss << "[" << op.element_count << "]";
			}
			oss << " " << (op.is_initialized ? "initialized" : "uninitialized");
		}
		break;
		
		case IrOpcode::GlobalLoad:
		{
			const GlobalLoadOp& op = getTypedPayload<GlobalLoadOp>();
			// %result = global_load @global_name
			if (std::holds_alternative<TempVar>(op.result.value)) {
				oss << '%' << std::get<TempVar>(op.result.value).var_number;
			} else if (std::holds_alternative<std::string_view>(op.result.value)) {
				oss << '%' << std::get<std::string_view>(op.result.value);
			}
			oss << " = global_load @" << op.global_name;
		}
		break;
		
		case IrOpcode::GlobalStore:
		{
			// global_store @global_name, %value
			// Format: [global_name, value]
			assert(getOperandCount() == 2 && "GlobalStore must have exactly 2 operands");
			oss << "global_store @" << getOperandAs<std::string_view>(0) << ", %" << getOperandAs<TempVar>(1).var_number;
		}
		break;

		case IrOpcode::FunctionAddress:
		{
			// %result = function_address @function_name
			auto& op = getTypedPayload<FunctionAddressOp>();
			if (std::holds_alternative<TempVar>(op.result.value)) {
				oss << '%' << std::get<TempVar>(op.result.value).var_number;
			} else if (std::holds_alternative<std::string_view>(op.result.value)) {
				oss << '%' << std::get<std::string_view>(op.result.value);
			}
			oss << " = function_address @" << op.function_name;
		}
		break;
		
		case IrOpcode::IndirectCall:
		{
			// %result = indirect_call %func_ptr, arg1, arg2, ...
			auto& op = getTypedPayload<IndirectCallOp>();
			oss << '%' << op.result.var_number << " = indirect_call ";

			// Function pointer can be either a TempVar or a variable name (string_view)
			if (std::holds_alternative<TempVar>(op.function_pointer)) {
				oss << '%' << std::get<TempVar>(op.function_pointer).var_number;
			} else {
				oss << '%' << std::get<std::string_view>(op.function_pointer);
			}

			// Arguments with type information
			for (const auto& arg : op.arguments) {
				oss << ", ";
				auto type_info = gNativeTypes.find(arg.type);
				if (type_info != gNativeTypes.end()) {
					oss << type_info->second->name_;
				}
				oss << arg.size_in_bits << " ";
				if (std::holds_alternative<TempVar>(arg.value)) {
					oss << '%' << std::get<TempVar>(arg.value).var_number;
				} else if (std::holds_alternative<std::string_view>(arg.value)) {
					oss << '%' << std::get<std::string_view>(arg.value);
				} else if (std::holds_alternative<unsigned long long>(arg.value)) {
					oss << std::get<unsigned long long>(arg.value);
				} else if (std::holds_alternative<double>(arg.value)) {
					oss << std::get<double>(arg.value);
				}
			}
		}
		break;
		
		case IrOpcode::FloatToInt:
		case IrOpcode::IntToFloat:
		case IrOpcode::FloatToFloat:
		{
			// %result = opcode from_val : from_type -> to_type
			auto& op = getTypedPayload<TypeConversionOp>();
			oss << '%' << op.result.var_number << " = ";
			switch (opcode_) {
				case IrOpcode::FloatToInt: oss << "float_to_int "; break;
				case IrOpcode::IntToFloat: oss << "int_to_float "; break;
				case IrOpcode::FloatToFloat: oss << "float_to_float "; break;
				default: break;
			}
			// Format: from_type from_size from_value to to_type to_size
			auto from_type_info = gNativeTypes.find(op.from.type);
			if (from_type_info != gNativeTypes.end()) {
				oss << from_type_info->second->name_;
			}
			oss << op.from.size_in_bits << " ";
			if (std::holds_alternative<TempVar>(op.from.value)) {
				oss << '%' << std::get<TempVar>(op.from.value).var_number;
			} else if (std::holds_alternative<std::string_view>(op.from.value)) {
				oss << '%' << std::get<std::string_view>(op.from.value);
			} else if (std::holds_alternative<unsigned long long>(op.from.value)) {
				oss << std::get<unsigned long long>(op.from.value);
			} else if (std::holds_alternative<double>(op.from.value)) {
				oss << std::get<double>(op.from.value);
			}
			oss << " to ";
			auto to_type_info = gNativeTypes.find(op.to_type);
			if (to_type_info != gNativeTypes.end()) {
				oss << to_type_info->second->name_;
			}
			oss << op.to_size_in_bits;
		}
		break;

		// Exception handling opcodes
		case IrOpcode::TryBegin:
		{
			const auto& op = getTypedPayload<BranchOp>();
			oss << "try_begin @" << op.target_label;
		}
		break;

		case IrOpcode::TryEnd:
			oss << "try_end";
			break;

		case IrOpcode::CatchBegin:
		{
			const auto& op = getTypedPayload<CatchBeginOp>();
			oss << "catch_begin ";
			if (op.type_index == 0) {
				oss << "...";  // catch-all
			} else {
				oss << "type_" << op.type_index;
			}
			oss << " %" << op.exception_temp.var_number;
			if (op.is_const) oss << " const";
			if (op.is_reference) oss << "&";
			if (op.is_rvalue_reference) oss << "&&";
			oss << " -> @" << op.catch_end_label;
		}
		break;

		case IrOpcode::CatchEnd:
			oss << "catch_end";
			break;

		case IrOpcode::Throw:
		{
			const auto& op = getTypedPayload<ThrowOp>();
			oss << "throw %" << op.value.var_number << " : type_" << op.type_index << " (" << op.size_in_bytes << " bytes)";
			if (op.is_rvalue) oss << " rvalue";
		}
		break;

		case IrOpcode::Rethrow:
			oss << "rethrow";
			break;

		default:
			FLASH_LOG(Codegen, Error, "Unhandled opcode: ", static_cast<std::underlying_type_t<IrOpcode>>(opcode_));
			assert(false && "Unhandled opcode");
			break;
		}

		return oss.str();
	}

	// Check if instruction has typed payload
	bool hasTypedPayload() const {
		return typed_payload_.has_value();
	}

	// Get typed payload (for helpers in IROperandHelpers.h)
	const std::any& getTypedPayload() const {
		assert(typed_payload_.has_value() && "Instruction must have typed payload");
		return typed_payload_;
	}

	std::any& getTypedPayload() {
		assert(typed_payload_.has_value() && "Instruction must have typed payload");
		return typed_payload_;
	}

	// Template version that casts to the requested type
	template<typename T>
	const T& getTypedPayload() const {
		assert(typed_payload_.has_value() && "Instruction must have typed payload");
		const T* ptr = std::any_cast<T>(&typed_payload_);
		assert(ptr && "Typed payload has wrong type");
		return *ptr;
	}

	template<typename T>
	T& getTypedPayload() {
		assert(typed_payload_.has_value() && "Instruction must have typed payload");
		T* ptr = std::any_cast<T>(&typed_payload_);
		assert(ptr && "Typed payload has wrong type");
		return *ptr;
	}

private:
	IrOpcode opcode_;
	OperandStorage operands_;
	Token first_token_;
	std::any typed_payload_;  // Optional typed payload
};

class Ir {
public:
	void addInstruction(const IrInstruction& instruction) {
		instructions.push_back(instruction);
	}

	void addInstruction(IrInstruction&& instruction) {
		instructions.push_back(std::move(instruction));
	}

	// Backward compatibility
	void addInstruction(IrOpcode&& opcode,
		std::vector<IrOperand>&& operands, Token first_token) {
		instructions.emplace_back(opcode, std::move(operands), first_token);
	}

	// Add instruction with typed payload (template for any payload type)
	template<typename PayloadType>
	void addInstruction(IrOpcode&& opcode, PayloadType&& payload, Token first_token) {
		instructions.emplace_back(opcode, std::forward<PayloadType>(payload), first_token);
	}

	// Builder-style: start building an instruction
	IrInstruction& beginInstruction(IrOpcode opcode, Token first_token, size_t expected_operand_count = 0) {
		instructions.emplace_back(opcode, first_token, expected_operand_count);
		return instructions.back();
	}

	const std::vector<IrInstruction>& getInstructions() const {
		return instructions;
	}

	// Reserve space for instructions (optimization)
	void reserve(size_t capacity) {
		instructions.reserve(capacity);
		reserved_capacity_ = capacity;
	}

	// Get statistics
	size_t instructionCount() const {
		return instructions.size();
	}

	size_t reservedCapacity() const {
		return reserved_capacity_;
	}

	size_t actualCapacity() const {
		return instructions.capacity();
	}

	// Print statistics
	void printStats() const {
		printf("\n=== IR Instruction Storage Statistics ===\n");
		printf("Reserved capacity: %zu instructions\n", reserved_capacity_);
		printf("Actual used:       %zu instructions\n", instructions.size());
		printf("Vector capacity:   %zu instructions\n", instructions.capacity());
		if (reserved_capacity_ > 0) {
			double usage_percent = (instructions.size() * 100.0) / reserved_capacity_;
			printf("Usage:             %.1f%% of reserved\n", usage_percent);
			if (instructions.size() > reserved_capacity_) {
				printf("WARNING: Exceeded reserved capacity by %zu instructions\n",
				       instructions.size() - reserved_capacity_);
			}
		}
		printf("==========================================\n\n");
	}

private:
	std::vector<IrInstruction> instructions;
	size_t reserved_capacity_ = 0;
};

// Include helper functions now that all types are defined
#include "IROperandHelpers.h"


