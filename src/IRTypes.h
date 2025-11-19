#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <any>
#include <sstream>
#include <cassert>

#include "AstNodeTypes.h"

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
	// Pointer operations
	AddressOf,
	Dereference,
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
		// temp_name_array is 0-indexed, so subtract 1
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
		{
			// % = add [Type][SizeInBits] %[LHS], %[RHS]
			assert(getOperandCount() == 7 && "Add instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = add ";
				if (getOperandCount() > 1) {
					oss << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";
					
					if (isOperandType<unsigned long long>(3))
						oss << getOperandAs<unsigned long long>(3);
					else if (isOperandType<TempVar>(3))
						oss << '%' << getOperandAs<TempVar>(3).var_number;
					else if (isOperandType<std::string_view>(3))
						oss << '%' << getOperandAs<std::string_view>(3);

					oss << ", ";

					if (isOperandType<unsigned long long>(6))
						oss << getOperandAs<unsigned long long>(6);
					else if (isOperandType<TempVar>(6))
						oss << '%' << getOperandAs<TempVar>(6).var_number;
					else if (isOperandType<std::string_view>(6))
						oss << '%' << getOperandAs<std::string_view>(6);
				}
			}
		}
		break;

		case IrOpcode::Subtract:
		{
			// % = sub [Type][SizeInBits] %[LHS], %[RHS]
			assert(getOperandCount() == 7 && "Subtract instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = sub ";
				if (getOperandCount() > 1) {
					oss << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";
					
					if (isOperandType<unsigned long long>(3))
						oss << getOperandAs<unsigned long long>(3);
					else if (isOperandType<TempVar>(3))
						oss << '%' << getOperandAs<TempVar>(3).var_number;
					else if (isOperandType<std::string_view>(3))
						oss << '%' << getOperandAs<std::string_view>(3);

					oss << ", ";

					if (isOperandType<unsigned long long>(6))
						oss << getOperandAs<unsigned long long>(6);
					else if (isOperandType<TempVar>(6))
						oss << '%' << getOperandAs<TempVar>(6).var_number;
					else if (isOperandType<std::string_view>(6))
						oss << '%' << getOperandAs<std::string_view>(6);
				}
			}
		}
		break;

		case IrOpcode::Multiply:
		{
			// % = mul [Type][SizeInBits] %[LHS], %[RHS]
			assert(getOperandCount() == 7 && "Multiply instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = mul ";
				if (getOperandCount() > 1) {
					oss << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";
					
					if (isOperandType<unsigned long long>(3))
						oss << getOperandAs<unsigned long long>(3);
					else if (isOperandType<TempVar>(3))
						oss << '%' << getOperandAs<TempVar>(3).var_number;
					else if (isOperandType<std::string_view>(3))
						oss << '%' << getOperandAs<std::string_view>(3);

					oss << ", ";

					if (isOperandType<unsigned long long>(6))
						oss << getOperandAs<unsigned long long>(6);
					else if (isOperandType<TempVar>(6))
						oss << '%' << getOperandAs<TempVar>(6).var_number;
					else if (isOperandType<std::string_view>(6))
						oss << '%' << getOperandAs<std::string_view>(6);
				}
			}
		}
		break;

		case IrOpcode::Divide:
		{
			// % = div [Type][SizeInBits] %[LHS], %[RHS]
			assert(getOperandCount() == 7 && "Divide instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = div ";
				if (getOperandCount() > 1) {
					oss << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

					if (isOperandType<unsigned long long>(3))
						oss << getOperandAs<unsigned long long>(3);
					else if (isOperandType<TempVar>(3))
						oss << '%' << getOperandAs<TempVar>(3).var_number;
					else if (isOperandType<std::string_view>(3))
						oss << '%' << getOperandAs<std::string_view>(3);

					oss << ", ";

					if (isOperandType<unsigned long long>(6))
						oss << getOperandAs<unsigned long long>(6);
					else if (isOperandType<TempVar>(6))
						oss << '%' << getOperandAs<TempVar>(6).var_number;
					else if (isOperandType<std::string_view>(6))
						oss << '%' << getOperandAs<std::string_view>(6);
				}
			}
		}
		break;

			case IrOpcode::ShiftLeft:
			{
				// % = shl [Type][SizeInBits] %[LHS], %[RHS]
				assert(getOperandCount() == 7 && "ShiftLeft instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val");
				if (getOperandCount() > 0) {
					oss << '%';
					if (isOperandType<TempVar>(0))
						oss << getOperandAs<TempVar>(0).var_number;
					else if (isOperandType<std::string_view>(0))
						oss << getOperandAs<std::string_view>(0);

					oss << " = shl " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

					if (isOperandType<unsigned long long>(3))
						oss << getOperandAs<unsigned long long>(3);
					else if (isOperandType<TempVar>(3))
						oss << '%' << getOperandAs<TempVar>(3).var_number;
					else if (isOperandType<std::string_view>(3))
						oss << '%' << getOperandAs<std::string_view>(3);

					oss << ", ";

					if (isOperandType<unsigned long long>(6))
						oss << getOperandAs<unsigned long long>(6);
					else if (isOperandType<TempVar>(6))
						oss << '%' << getOperandAs<TempVar>(6).var_number;
					else if (isOperandType<std::string_view>(6))
						oss << '%' << getOperandAs<std::string_view>(6);
				}
			}
			break;

			case IrOpcode::ShiftRight:
			{
				// % = shr [Type][SizeInBits] %[LHS], %[RHS]
				assert(getOperandCount() == 7 && "ShiftRight instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val");
				if (getOperandCount() > 0) {
					oss << '%';
					if (isOperandType<TempVar>(0))
						oss << getOperandAs<TempVar>(0).var_number;
					else if (isOperandType<std::string_view>(0))
						oss << getOperandAs<std::string_view>(0);

					oss << " = shr " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

					if (isOperandType<unsigned long long>(3))
						oss << getOperandAs<unsigned long long>(3);
					else if (isOperandType<TempVar>(3))
						oss << '%' << getOperandAs<TempVar>(3).var_number;
					else if (isOperandType<std::string_view>(3))
						oss << '%' << getOperandAs<std::string_view>(3);

					oss << ", ";

					if (isOperandType<unsigned long long>(6))
						oss << getOperandAs<unsigned long long>(6);
					else if (isOperandType<TempVar>(6))
						oss << '%' << getOperandAs<TempVar>(6).var_number;
					else if (isOperandType<std::string_view>(6))
						oss << '%' << getOperandAs<std::string_view>(6);
				}
			}
			break;

		case IrOpcode::UnsignedDivide:
		{
			// % = udiv [Type][SizeInBits] %[LHS], %[RHS]
			assert(getOperandCount() == 7 && "UnsignedDivide instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = udiv " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::UnsignedShiftRight:
		{
			// % = lshr [Type][SizeInBits] %[LHS], %[RHS]
			assert(getOperandCount() == 7 && "UnsignedShiftRight instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = lshr " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::FloatAdd:
		{
			// %result = fadd [Type][Size] %lhs, %rhs
			assert(getOperandCount() == 7 && "FloatAdd instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fadd " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::FloatSubtract:
		{
			// %result = fsub [Type][Size] %lhs, %rhs
			assert(getOperandCount() == 7 && "FloatSubtract instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fsub " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::FloatMultiply:
		{
			// %result = fmul [Type][Size] %lhs, %rhs
			assert(getOperandCount() == 7 && "FloatMultiply instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fmul " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::FloatDivide:
		{
			// %result = fdiv [Type][Size] %lhs, %rhs
			assert(getOperandCount() == 7 && "FloatDivide instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fdiv " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::BitwiseAnd:
		{
			// %result = and [Type][Size] %lhs, %rhs
			assert(getOperandCount() == 7 && "BitwiseAnd instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = and " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::BitwiseOr:
		{
			// %result = or [Type][Size] %lhs, %rhs
			assert(getOperandCount() == 7 && "BitwiseOr instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = or " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::BitwiseXor:
		{
			// %result = xor [Type][Size] %lhs, %rhs
			assert(getOperandCount() == 7 && "BitwiseXor instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = xor " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::Modulo:
		{
			// %result = srem [Type][Size] %lhs, %rhs (signed remainder)
			assert(getOperandCount() == 7 && "Modulo instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = srem " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::BitwiseNot:
		{
			// %result = xor [Type][Size] %operand, -1 (bitwise NOT implemented as XOR with all 1s)
			assert(getOperandCount() == 4 && "BitwiseNot instruction must have exactly 4 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = xor " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", -1";
			}
		}
		break;

		case IrOpcode::Equal:
		{
			// %result = icmp eq [Type][Size] %lhs, %rhs
			assert(getOperandCount() == 7 && "Equal instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp eq " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::NotEqual:
		{
			// %result = icmp ne [Type][Size] %lhs, %rhs
			assert(getOperandCount() == 7 && "NotEqual instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp ne " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::LessThan:
		{
			// %result = icmp slt [Type][Size] %lhs, %rhs (signed less than)
			assert(getOperandCount() == 7 && "LessThan instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp slt " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::UnsignedLessThan:
		{
			// %result = icmp ult [Type][Size] %lhs, %rhs (unsigned less than)
			assert(getOperandCount() == 7 && "UnsignedLessThan instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp ult " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::UnsignedLessEqual:
		{
			// %result = icmp ule [Type][Size] %lhs, %rhs (unsigned less than or equal)
			assert(getOperandCount() == 7 && "UnsignedLessEqual instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp ule " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::LessEqual:
		{
			// %result = icmp sle [Type][Size] %lhs, %rhs (signed less than or equal)
			assert(getOperandCount() == 7 && "LessEqual instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp sle " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::GreaterThan:
		{
			// %result = icmp sgt [Type][Size] %lhs, %rhs (signed greater than)
			assert(getOperandCount() == 7 && "GreaterThan instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp sgt " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::GreaterEqual:
		{
			// %result = icmp sge [Type][Size] %lhs, %rhs (signed greater than or equal)
			assert(getOperandCount() == 7 && "GreaterEqual instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp sge " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::UnsignedGreaterThan:
		{
			// %result = icmp ugt [Type][Size] %lhs, %rhs (unsigned greater than)
			assert(getOperandCount() == 7 && "UnsignedGreaterThan instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp ugt " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::UnsignedGreaterEqual:
		{
			// %result = icmp uge [Type][Size] %lhs, %rhs (unsigned greater than or equal)
			assert(getOperandCount() == 7 && "UnsignedGreaterEqual instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp uge " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::FloatEqual:
		{
			// %result = fcmp oeq [Type][Size] %lhs, %rhs (ordered equal)
			assert(getOperandCount() == 7 && "FloatEqual instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fcmp oeq " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::FloatNotEqual:
		{
			// %result = fcmp one [Type][Size] %lhs, %rhs (ordered not equal)
			assert(getOperandCount() == 7 && "FloatNotEqual instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fcmp one " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::FloatLessThan:
		{
			// %result = fcmp olt [Type][Size] %lhs, %rhs (ordered less than)
			assert(getOperandCount() == 7 && "FloatLessThan instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fcmp olt " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::FloatLessEqual:
		{
			// %result = fcmp ole [Type][Size] %lhs, %rhs (ordered less than or equal)
			assert(getOperandCount() == 7 && "FloatLessEqual instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fcmp ole " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::FloatGreaterThan:
		{
			// %result = fcmp ogt [Type][Size] %lhs, %rhs (ordered greater than)
			assert(getOperandCount() == 7 && "FloatGreaterThan instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fcmp ogt " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::FloatGreaterEqual:
		{
			// %result = fcmp oge [Type][Size] %lhs, %rhs (ordered greater than or equal)
			assert(getOperandCount() == 7 && "FloatGreaterEqual instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fcmp oge " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::LogicalAnd:
		{
			// %result = and i1 %lhs, %rhs (logical AND on boolean values)
			assert(getOperandCount() == 7 && "LogicalAnd instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = and i1 ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::LogicalOr:
		{
			// %result = or i1 %lhs, %rhs (logical OR on boolean values)
			assert(getOperandCount() == 7 && "LogicalOr instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = or i1 ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::LogicalNot:
		{
			// %result = xor i1 %operand, true (logical NOT implemented as XOR with true)
			assert(getOperandCount() == 4 && "LogicalNot instruction must have exactly 4 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = xor i1 ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", true";
			}
		}
		break;

		case IrOpcode::SignExtend:
		{
			// % = sext [FromType][FromSize] %[Value] to [ToType][ToSize]
			assert(getOperandCount() == 5 && "SignExtend instruction must have exactly 5 operands: result_var, from_type, from_size, value, to_size");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = sext " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << " to " << getOperandAsTypeString(1) << getOperandAs<int>(4);
			}
		}
		break;

		case IrOpcode::ZeroExtend:
		{
			// % = zext [FromType][FromSize] %[Value] to [ToType][ToSize]
			assert(getOperandCount() == 5 && "ZeroExtend instruction must have exactly 5 operands: result_var, from_type, from_size, value, to_size");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = zext " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << " to " << getOperandAsTypeString(1) << getOperandAs<int>(4);
			}
		}
		break;

		case IrOpcode::Truncate:
		{
			// % = trunc [FromType][FromSize] %[Value] to [ToType][ToSize]
			assert(getOperandCount() == 5 && "Truncate instruction must have exactly 5 operands: result_var, from_type, from_size, value, to_size");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = trunc " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << " to " << getOperandAsTypeString(1) << getOperandAs<int>(4);
			}
		}
		break;

		case IrOpcode::Return:
		{
			// ret [Type][SizeInBits] [Value]
			oss << "ret";
			if (getOperandCount() >= 3) {
				oss << " " << getOperandAsTypeString(0) << getOperandAs<int>(1) << " ";

				if (isOperandType<unsigned long long>(2))
					oss << getOperandAs<unsigned long long>(2);
				else if (isOperandType<TempVar>(2))
					oss << '%' << getOperandAs<TempVar>(2).var_number;
				else if (isOperandType<std::string_view>(2))
					oss << '%' << getOperandAs<std::string_view>(2);
			}
		}
		break;

		case IrOpcode::FunctionDecl:
		{
			// define [Type][SizeInBits][PointerDepth] [Name]
			oss << "define "
				<< getOperandAsTypeString(0) << getOperandAsIntSafe(1);

			// Add pointer depth indicator if present
			// Safely get pointer depth - it should be an int at index 2
			int pointer_depth = getOperandAsIntSafe(2);
			for (int i = 0; i < pointer_depth; ++i) {
				oss << "*";
			}
			oss << " ";

			// Function name can be either std::string or std::string_view (now at index 3)
			if (getOperandCount() > 3) {
				if (isOperandType<std::string>(3))
					oss << getOperandAs<std::string>(3);
				else if (isOperandType<std::string_view>(3))
					oss << getOperandAs<std::string_view>(3);
			}
		}
		break;

		case IrOpcode::FunctionCall:
		{
			// % = call [Type][SizeInBits] @[FuncName]()
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = call ";
				if (getOperandCount() > 1) {
					oss << "@";
					// Function name can be either std::string or std::string_view
					if (isOperandType<std::string>(1))
						oss << getOperandAs<std::string>(1);
					else if (isOperandType<std::string_view>(1))
						oss << getOperandAs<std::string_view>(1);
					oss << "(";

					const size_t funcSymbolIndex = getOperandCount() - 1;
					for (size_t i = 2; i < funcSymbolIndex; i += 3) {
						if (i > 2) oss << ", ";
						oss << getOperandAsTypeString(i);
						// Safely get size - should be int at i+1
						if (i + 1 < getOperandCount() && isOperandType<int>(i + 1)) {
							oss << getOperandAs<int>(i + 1);
						}
						oss << " ";

						if (i + 2 < getOperandCount()) {
							if (isOperandType<unsigned long long>(i + 2))
								oss << getOperandAs<unsigned long long>(i + 2);
							else if (isOperandType<TempVar>(i + 2))
								oss << '%' << getOperandAs<TempVar>(i + 2).var_number;
							else if (isOperandType<std::string_view>(i + 2))
								oss << '%' << getOperandAs<std::string_view>(i + 2);
						}
					}

					oss << ")";
				}
			}
		}
		break;

		case IrOpcode::StackAlloc:
		{
			// %name = alloca [Type][SizeInBits]
			oss << '%';
			if (getOperandCount() > 2 && isOperandType<std::string_view>(2))
				oss << getOperandAs<std::string_view>(2);
			oss << " = alloca ";
			oss << getOperandAsTypeString(0);
			if (getOperandCount() > 1 && isOperandType<int>(1))
				oss << getOperandAs<int>(1);
		}
		break;

		case IrOpcode::Store:
		{
			// store [Type][SizeInBits] [Value] to [Dest]
			oss << "store ";
			oss << getOperandAsTypeString(0);
			if (getOperandCount() > 1 && isOperandType<int>(1))
				oss << getOperandAs<int>(1);
			oss << " ";

			// Source register
			if (getOperandCount() > 3 && isOperandType<int>(3)) {
				X64Register srcReg = static_cast<X64Register>(getOperandAs<int>(3));
				switch (srcReg) {
				case X64Register::RCX: oss << "RCX"; break;
				case X64Register::RDX: oss << "RDX"; break;
				case X64Register::R8: oss << "R8"; break;
				case X64Register::R9: oss << "R9"; break;
				default: oss << "R" << static_cast<int>(srcReg); break;
				}
			}

			oss << " to %";
			if (getOperandCount() > 2 && isOperandType<std::string_view>(2))
				oss << getOperandAs<std::string_view>(2);
		}
		break;

		case IrOpcode::Branch:
		{
			// br label %label
			assert(getOperandCount() == 1 && "Branch instruction must have exactly 1 operand");
			if (getOperandCount() > 0) {
				oss << "br label %";
				if (isOperandType<std::string>(0))
					oss << getOperandAs<std::string>(0);
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);
			}
		}
		break;

		case IrOpcode::ConditionalBranch:
		{
			// br i1 %condition, label %true_label, label %false_label
			// Format: [type, size, condition, then_label, else_label]
			assert(getOperandCount() == 5 && "ConditionalBranch instruction must have exactly 5 operands");
			if (getOperandCount() >= 5) {
				oss << "br i1 ";
				// Operand 2 is the condition value
				if (isOperandType<TempVar>(2))
					oss << '%' << getOperandAs<TempVar>(2).var_number;
				else if (isOperandType<std::string_view>(2))
					oss << '%' << getOperandAs<std::string_view>(2);
				else if (isOperandType<unsigned long long>(2))
					oss << getOperandAs<unsigned long long>(2);

				oss << ", label %";
				// Operand 3 is the then_label
				if (isOperandType<std::string>(3))
					oss << getOperandAs<std::string>(3);
				else if (isOperandType<std::string_view>(3))
					oss << getOperandAs<std::string_view>(3);

				oss << ", label %";
				// Operand 4 is the else_label
				if (isOperandType<std::string>(4))
					oss << getOperandAs<std::string>(4);
				else if (isOperandType<std::string_view>(4))
					oss << getOperandAs<std::string_view>(4);
			}
		}
		break;

		case IrOpcode::Label:
		{
			// label_name:
			assert(getOperandCount() == 1 && "Label instruction must have exactly 1 operand");
			if (getOperandCount() > 0) {
				if (isOperandType<std::string>(0))
					oss << getOperandAs<std::string>(0) << ":";
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0) << ":";
			}
		}
		break;

		case IrOpcode::LoopBegin:
		{
			// loop_begin %loop_start_label %loop_end_label %loop_increment_label
			assert(getOperandCount() == 3 && "LoopBegin instruction must have exactly 3 operands");
			if (getOperandCount() >= 3) {
				oss << "loop_begin %";
				if (isOperandType<std::string>(0))
					oss << getOperandAs<std::string>(0);
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);
				oss << " %";
				if (isOperandType<std::string>(1))
					oss << getOperandAs<std::string>(1);
				else if (isOperandType<std::string_view>(1))
					oss << getOperandAs<std::string_view>(1);
				oss << " %";
				if (isOperandType<std::string>(2))
					oss << getOperandAs<std::string>(2);
				else if (isOperandType<std::string_view>(2))
					oss << getOperandAs<std::string_view>(2);
			}
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
			// %result = array_access [ArrayType][ElementSize] %array, [IndexType][IndexSize] %index
			// Format: [result_var, array_type, element_size, array_name, index_type, index_size, index_value]
			assert(getOperandCount() == 7 && "ArrayAccess instruction must have exactly 7 operands");
			if (getOperandCount() >= 7) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = array_access " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				// Array operand
				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", " << getOperandAsTypeString(4) << getOperandAs<int>(5) << " ";

				// Index operand
				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::ArrayStore:
		{
			// array_store [ElementType][ElementSize] %array, [IndexType][IndexSize] %index, %value
			// Format: [element_type, element_size, array_name, index_type, index_size, index_value, value]
			assert(getOperandCount() == 7 && "ArrayStore instruction must have exactly 7 operands");
			if (getOperandCount() >= 7) {
				oss << "array_store " << getOperandAsTypeString(0) << getOperandAs<int>(1) << " ";

				// Array operand
				if (isOperandType<unsigned long long>(2))
					oss << getOperandAs<unsigned long long>(2);
				else if (isOperandType<TempVar>(2))
					oss << '%' << getOperandAs<TempVar>(2).var_number;
				else if (isOperandType<std::string_view>(2))
					oss << '%' << getOperandAs<std::string_view>(2);

				oss << ", " << getOperandAsTypeString(3) << getOperandAs<int>(4) << " ";

				// Index operand
				if (isOperandType<unsigned long long>(5))
					oss << getOperandAs<unsigned long long>(5);
				else if (isOperandType<TempVar>(5))
					oss << '%' << getOperandAs<TempVar>(5).var_number;
				else if (isOperandType<std::string_view>(5))
					oss << '%' << getOperandAs<std::string_view>(5);

				oss << ", ";

				// Value operand
				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::AddressOf:
		{
			// %result = addressof [Type][Size] %operand
			// Format: [result_var, type, size, operand]
			assert(getOperandCount() == 4 && "AddressOf instruction must have exactly 4 operands");
			if (getOperandCount() >= 4) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = addressof " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);
			}
		}
		break;

		case IrOpcode::Dereference:
		{
			// %result = dereference [Type][Size] %operand
			// Format: [result_var, type, size, operand]
			assert(getOperandCount() == 4 && "Dereference instruction must have exactly 4 operands");
			if (getOperandCount() >= 4) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = dereference " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);
			}
		}
		break;

		case IrOpcode::MemberAccess:
		{
			// %result = member_access [MemberType][MemberSize] %object, member_name, offset
			// Format: [result_var, member_type, member_size, object_name, member_name, offset, is_ref?, is_rvalue_ref?, referenced_bits?]
			assert(getOperandCount() >= 6 && "MemberAccess instruction must have at least 6 operands");
			if (getOperandCount() >= 6) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else
					oss << getOperandAs<std::string_view>(0);

				oss << " = member_access ";
				oss << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				// Object
				if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << "." << getOperandAs<std::string_view>(4);
				oss << " (offset: " << getOperandAs<int>(5) << ")";
				if (getOperandCount() >= 9 && getOperandAs<bool>(6)) {
					oss << " [ref, size=" << getOperandAs<int>(8) << "]";
				}
			}
		}
		break;

		case IrOpcode::MemberStore:
		{
			// member_store [MemberType][MemberSize] %object, member_name, offset, %value
			// Format: [member_type, member_size, object_name, member_name, offset, is_ref?, is_rvalue_ref?, referenced_bits?, value]
			assert(getOperandCount() >= 6 && "MemberStore instruction must have at least 6 operands");
			if (getOperandCount() >= 6) {
				oss << "member_store ";
				oss << getOperandAsTypeString(0) << getOperandAsIntSafe(1) << " ";

				// Object
				if (isOperandType<TempVar>(2))
					oss << '%' << getOperandAs<TempVar>(2).var_number;
				else if (isOperandType<std::string_view>(2))
					oss << '%' << getOperandAs<std::string_view>(2);

				oss << ".";
				if (getOperandCount() > 3 && isOperandType<std::string_view>(3))
					oss << getOperandAs<std::string_view>(3);
				oss << " (offset: " << getOperandAsIntSafe(4) << ")";
				if (getOperandCount() >= 9 && getOperandAs<bool>(5)) {
					oss << " [ref, size=" << getOperandAsIntSafe(7) << "]";
				}
				oss << ", ";

				// Value
				size_t value_index = (getOperandCount() >= 9) ? 8 : 5;
				if (isOperandType<TempVar>(value_index))
					oss << '%' << getOperandAs<TempVar>(value_index).var_number;
				else if (isOperandType<std::string_view>(value_index))
					oss << '%' << getOperandAs<std::string_view>(value_index);
				else if (isOperandType<int>(value_index))
					oss << getOperandAs<int>(value_index);
				else if (isOperandType<unsigned long long>(value_index))
					oss << getOperandAs<unsigned long long>(value_index);
			}
		}
		break;

		case IrOpcode::ConstructorCall:
		{
			// constructor_call StructName %object_var [param1_type, param1_size, param1_value, ...]
			// Format: [struct_name, object_var, param1_type, param1_size, param1_value, ...]
			assert(getOperandCount() >= 2 && "ConstructorCall instruction must have at least 2 operands");
			if (getOperandCount() >= 2) {
				oss << "constructor_call ";

				// Struct name can be either std::string or std::string_view
				if (isOperandType<std::string>(0))
					oss << getOperandAs<std::string>(0);
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " %";

				if (isOperandType<TempVar>(1))
					oss << getOperandAs<TempVar>(1).var_number;
				else if (isOperandType<std::string_view>(1))
					oss << getOperandAs<std::string_view>(1);
				else if (isOperandType<std::string>(1))
					oss << getOperandAs<std::string>(1);

				// Add parameters if any
				for (size_t i = 2; i < getOperandCount(); i += 3) {
					if (i + 2 < getOperandCount()) {
						oss << " " << getOperandAsTypeString(i) << getOperandAs<int>(i + 1) << " ";

						if (isOperandType<TempVar>(i + 2))
							oss << '%' << getOperandAs<TempVar>(i + 2).var_number;
						else if (isOperandType<std::string_view>(i + 2))
							oss << '%' << getOperandAs<std::string_view>(i + 2);
						else if (isOperandType<unsigned long long>(i + 2))
							oss << getOperandAs<unsigned long long>(i + 2);
					}
				}
			}
		}
		break;

		case IrOpcode::DestructorCall:
		{
			// destructor_call StructName %object_var
			// Format: [struct_name, object_var]
			assert(getOperandCount() == 2 && "DestructorCall instruction must have exactly 2 operands");
			if (getOperandCount() >= 2) {
				oss << "destructor_call ";

				// Struct name can be either std::string or std::string_view
				if (isOperandType<std::string>(0))
					oss << getOperandAs<std::string>(0);
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " %";

				if (isOperandType<TempVar>(1))
					oss << getOperandAs<TempVar>(1).var_number;
				else if (isOperandType<std::string_view>(1))
					oss << getOperandAs<std::string_view>(1);
				else if (isOperandType<std::string>(1))
					oss << getOperandAs<std::string>(1);
			}
		}
		break;

		case IrOpcode::VirtualCall:
		{
			// %result = virtual_call %object, vtable_index, [args...]
			// Format: [result_var, object_type, object_size, object_name, vtable_index, arg1_type, arg1_size, arg1_value, ...]
			if (getOperandCount() >= 5) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = virtual_call ";

				// Object (this pointer)
				oss << getOperandAsTypeString(1) << getOperandAs<int>(2) << " %";
				if (isOperandType<TempVar>(3))
					oss << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << getOperandAs<std::string_view>(3);

				// VTable index
				oss << ", vtable[" << getOperandAs<int>(4) << "]";

				// Arguments (if any)
				if (getOperandCount() > 5) {
					oss << "(";
					for (size_t i = 5; i < getOperandCount(); i += 3) {
						if (i > 5) oss << ", ";
						oss << getOperandAsTypeString(i) << getOperandAs<int>(i + 1) << " ";

						if (isOperandType<unsigned long long>(i + 2))
							oss << getOperandAs<unsigned long long>(i + 2);
						else if (isOperandType<TempVar>(i + 2))
							oss << '%' << getOperandAs<TempVar>(i + 2).var_number;
						else if (isOperandType<std::string_view>(i + 2))
							oss << '%' << getOperandAs<std::string_view>(i + 2);
					}
					oss << ")";
				}
			}
		}
		break;

		case IrOpcode::StringLiteral:
		{
			// %result = string_literal "content"
			// Format: [result_var, string_content]
			assert(getOperandCount() == 2 && "StringLiteral instruction must have exactly 2 operands");
			if (getOperandCount() >= 2) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = string_literal ";
				oss << getOperandAs<std::string_view>(1);
			}
		}
		break;

		case IrOpcode::HeapAlloc:
		{
			// %result = heap_alloc [Type][Size][PointerDepth]
			// Format: [result_var, type, size_in_bytes, pointer_depth]
			assert(getOperandCount() == 4 && "HeapAlloc instruction must have exactly 4 operands");
			if (getOperandCount() >= 4) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;

				oss << " = heap_alloc [" << static_cast<int>(getOperandAs<Type>(1)) << "]["
				    << getOperandAs<int>(2) << "][" << getOperandAs<int>(3) << "]";
			}
		}
		break;

		case IrOpcode::HeapAllocArray:
		{
			// %result = heap_alloc_array [Type][Size][PointerDepth] %count
			// Format: [result_var, type, size_in_bytes, pointer_depth, count_var]
			assert(getOperandCount() == 5 && "HeapAllocArray instruction must have exactly 5 operands");
			if (getOperandCount() >= 5) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;

				oss << " = heap_alloc_array [" << static_cast<int>(getOperandAs<Type>(1)) << "]["
				    << getOperandAs<int>(2) << "][" << getOperandAs<int>(3) << "] %";

				if (isOperandType<TempVar>(4))
					oss << getOperandAs<TempVar>(4).var_number;
			}
		}
		break;

		case IrOpcode::HeapFree:
		{
			// heap_free %ptr
			// Format: [ptr_var]
			assert(getOperandCount() == 1 && "HeapFree instruction must have exactly 1 operand");
			if (getOperandCount() >= 1) {
				oss << "heap_free %";
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
			}
		}
		break;

		case IrOpcode::HeapFreeArray:
		{
			// heap_free_array %ptr
			// Format: [ptr_var]
			assert(getOperandCount() == 1 && "HeapFreeArray instruction must have exactly 1 operand");
			if (getOperandCount() >= 1) {
				oss << "heap_free_array %";
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
			}
		}
		break;

		case IrOpcode::PlacementNew:
		{
			// %result = placement_new %address [Type][Size]
			// Format: [result_var, address_var, type, size_in_bytes]
			assert(getOperandCount() >= 4 && "PlacementNew instruction must have at least 4 operands");
			if (getOperandCount() >= 4) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				oss << " = placement_new %";
				if (isOperandType<TempVar>(1))
					oss << getOperandAs<TempVar>(1).var_number;
				oss << " [" << getOperandAsTypeString(2) << "][" << getOperandAs<int>(3) << "]";
			}
		}
		break;

		case IrOpcode::Typeid:
		{
			// %result = typeid [type_name_or_expr] [is_type]
			// Format: [result_var, type_name_or_expr, is_type]
			assert(getOperandCount() == 3 && "Typeid instruction must have exactly 3 operands");
			if (getOperandCount() >= 3) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				oss << " = typeid ";
				if (isOperandType<std::string>(1))
					oss << getOperandAs<std::string>(1);
				else if (isOperandType<std::string_view>(1))
					oss << getOperandAs<std::string_view>(1);
				oss << " [is_type=" << (getOperandAs<bool>(2) ? "true" : "false") << "]";
			}
		}
		break;

		case IrOpcode::DynamicCast:
		{
			// %result = dynamic_cast %source_ptr [target_type] [is_reference]
			// Format: [result_var, source_ptr, target_type_name, is_reference]
			assert(getOperandCount() == 4 && "DynamicCast instruction must have exactly 4 operands");
			if (getOperandCount() >= 4) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				oss << " = dynamic_cast %";
				if (isOperandType<TempVar>(1))
					oss << getOperandAs<TempVar>(1).var_number;
				oss << " [" << getOperandAs<std::string>(2) << "]";
				oss << " [is_ref=" << (getOperandAs<bool>(3) ? "true" : "false") << "]";
			}
		}
		break;

		case IrOpcode::PreIncrement:
		{
			// %result = pre_inc [Type][Size] %operand
			assert(getOperandCount() == 4 && "PreIncrement instruction must have exactly 4 operands");
			if (getOperandCount() >= 4) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = pre_inc " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);
			}
		}
		break;

		case IrOpcode::PostIncrement:
		{
			// %result = post_inc [Type][Size] %operand
			assert(getOperandCount() == 4 && "PostIncrement instruction must have exactly 4 operands");
			if (getOperandCount() >= 4) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = post_inc " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);
			}
		}
		break;

		case IrOpcode::PreDecrement:
		{
			// %result = pre_dec [Type][Size] %operand
			assert(getOperandCount() == 4 && "PreDecrement instruction must have exactly 4 operands");
			if (getOperandCount() >= 4) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = pre_dec " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);
			}
		}
		break;

		case IrOpcode::PostDecrement:
		{
			// %result = post_dec [Type][Size] %operand
			assert(getOperandCount() == 4 && "PostDecrement instruction must have exactly 4 operands");
			if (getOperandCount() >= 4) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = post_dec " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);
			}
		}
		break;

		case IrOpcode::AddAssign:
		{
			// %var = add %var, %rhs (compound assignment a += b)
			assert(getOperandCount() == 7 && "AddAssign instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = add " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::SubAssign:
		{
			// %var = sub %var, %rhs (compound assignment a -= b)
			assert(getOperandCount() == 7 && "SubAssign instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = sub " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::MulAssign:
		{
			// %var = mul %var, %rhs (compound assignment a *= b)
			assert(getOperandCount() == 7 && "MulAssign instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = mul " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::DivAssign:
		{
			// %var = sdiv %var, %rhs (compound assignment a /= b)
			assert(getOperandCount() == 7 && "DivAssign instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = sdiv " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::ModAssign:
		{
			// %var = srem %var, %rhs (compound assignment a %= b)
			assert(getOperandCount() == 7 && "ModAssign instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = srem " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::AndAssign:
		{
			// %var = and %var, %rhs (compound assignment a &= b)
			assert(getOperandCount() == 7 && "AndAssign instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = and " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::OrAssign:
		{
			// %var = or %var, %rhs (compound assignment a |= b)
			assert(getOperandCount() == 7 && "OrAssign instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = or " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::XorAssign:
		{
			// %var = xor %var, %rhs (compound assignment a ^= b)
			assert(getOperandCount() == 7 && "XorAssign instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = xor " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::ShlAssign:
		{
			// %var = shl %var, %rhs (compound assignment a <<= b)
			assert(getOperandCount() == 7 && "ShlAssign instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = shl " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::ShrAssign:
		{
			// %var = ashr %var, %rhs (compound assignment a >>= b)
			assert(getOperandCount() == 7 && "ShrAssign instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).var_number;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = ashr " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::Assignment:
		{
			// assign %lhs = %rhs (simple assignment a = b)
			// Format: [result_var, lhs_type, lhs_size, lhs_value, rhs_type, rhs_size, rhs_value]
			assert(getOperandCount() == 7 && "Assignment instruction must have exactly 7 operands");
			if (getOperandCount() > 0) {
				oss << "assign ";

				// Print LHS (operand 3)
				if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).var_number;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << " = ";

				// Print RHS (operand 6)
				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).var_number;
				else if (isOperandType<std::string_view>(6))
					oss << '%' << getOperandAs<std::string_view>(6);
			}
		}
		break;

		case IrOpcode::VariableDecl:
		{
			// Format: [type, size, name, custom_alignment, is_ref, is_rvalue_ref, is_array, (array_size?)?, (initializer?)?]
			assert(getOperandCount() >= 7 && (getOperandCount() - 7) % 3 == 0 && getOperandCount() <= 13 &&
			       "VariableDecl instruction must have 7 operands plus optional 3-operand blocks for arrays/initializers");
			oss << "%";
			if (getOperandCount() > 2 && isOperandType<std::string_view>(2))
				oss << getOperandAs<std::string_view>(2);
			oss << " = alloc " << getOperandAsTypeString(0) << getOperandAsIntSafe(1);
			if (getOperandCount() > 3 && isOperandType<unsigned long long>(3)) {
				unsigned long long alignment = getOperandAs<unsigned long long>(3);
				if (alignment > 0) {
					oss << " alignas(" << alignment << ")";
				}
			}
			oss << (getOperandAs<bool>(4) ? " [&]" : "");
			const size_t array_info_index = 6;
			bool has_array_info = getOperandCount() >= array_info_index + 3 &&
			                     isOperandType<Type>(array_info_index);
			size_t initializer_index = has_array_info ? array_info_index + 3 : array_info_index;
			bool has_initializer = getOperandCount() >= initializer_index + 3;
			if (has_initializer) {
				size_t init_value_index = initializer_index + 2;
				oss << "\nassign %";
				if (isOperandType<std::string_view>(2))
					oss << getOperandAs<std::string_view>(2);
				oss << " = ";
				// Check if operand is a literal value or a variable/TempVar
				if (isOperandType<unsigned long long>(init_value_index))
					oss << getOperandAs<unsigned long long>(init_value_index);
				else if (isOperandType<int>(init_value_index))
					oss << getOperandAs<int>(init_value_index);
				else if (isOperandType<double>(init_value_index))
					oss << getOperandAs<double>(init_value_index);
				else if (isOperandType<TempVar>(init_value_index))
					oss << '%' << getOperandAs<TempVar>(init_value_index).var_number;
				else if (isOperandType<std::string_view>(init_value_index))
					oss << '%' << getOperandAs<std::string_view>(init_value_index);
			}
			break;
		}

		case IrOpcode::GlobalVariableDecl:
		{
			// global_var [Type][Size] @name [is_initialized] [init_value?]
			// Format: [type, size_in_bits, var_name, is_initialized, init_value?]
			assert((getOperandCount() == 4 || getOperandCount() == 5) && "GlobalVariableDecl must have 4 or 5 operands");
			// var_name can be either std::string (for static locals) or std::string_view (for regular globals)
			std::string var_name;
			if (std::holds_alternative<std::string>(operands_[2])) {
				var_name = std::get<std::string>(operands_[2]);
			} else {
				var_name = std::string(std::get<std::string_view>(operands_[2]));
			}
			oss << "global_var " << getOperandAsTypeString(0) << getOperandAs<int>(1) << " @" << var_name;
			if (getOperandCount() >= 4) {
				oss << " " << (getOperandAs<bool>(3) ? "initialized" : "uninitialized");
				if (getOperandCount() == 5 && getOperandAs<bool>(3)) {
					oss << " = " << getOperandAs<unsigned long long>(4);
				}
			}
		}
		break;

		case IrOpcode::GlobalLoad:
		{
			// %result = global_load @global_name
			// Format: [result_temp, global_name]
			assert(getOperandCount() == 2 && "GlobalLoad must have exactly 2 operands");
			// global_name can be either std::string (for static locals) or std::string_view (for regular globals)
			std::string global_name;
			if (std::holds_alternative<std::string>(operands_[1])) {
				global_name = std::get<std::string>(operands_[1]);
			} else {
				global_name = std::string(std::get<std::string_view>(operands_[1]));
			}
			oss << '%' << getOperandAs<TempVar>(0).var_number << " = global_load @" << global_name;
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
			// Format: [result_temp, function_name]
			assert(getOperandCount() == 2 && "FunctionAddress must have exactly 2 operands");
			oss << '%' << getOperandAs<TempVar>(0).var_number << " = function_address @" << getOperandAs<std::string_view>(1);
		}
		break;

		case IrOpcode::IndirectCall:
		{
			// %result = indirect_call %func_ptr, arg1_type, arg1_size, arg1_value, ...
			// Format: [result_temp, func_ptr_var, arg1_type, arg1_size, arg1_value, ...]
			assert(getOperandCount() >= 2 && "IndirectCall must have at least 2 operands");
			oss << '%' << getOperandAs<TempVar>(0).var_number << " = indirect_call ";

			// Function pointer can be either a TempVar or a variable name (string_view)
			if (isOperandType<TempVar>(1)) {
				oss << '%' << getOperandAs<TempVar>(1).var_number;
			} else if (isOperandType<std::string_view>(1)) {
				oss << '%' << getOperandAs<std::string_view>(1);
			}

			// Arguments come in groups of 3: type, size, value
			for (size_t i = 2; i + 2 < getOperandCount(); i += 3) {
				oss << ", " << getOperandAsTypeString(i) << getOperandAs<int>(i + 1) << " ";
				if (isOperandType<TempVar>(i + 2)) {
					oss << '%' << getOperandAs<TempVar>(i + 2).var_number;
				} else if (isOperandType<std::string_view>(i + 2)) {
					oss << '%' << getOperandAs<std::string_view>(i + 2);
				} else if (isOperandType<unsigned long long>(i + 2)) {
					oss << getOperandAs<unsigned long long>(i + 2);
				}
			}
		}
		break;

		default:
			std::cerr << "Unhandled opcode: " << static_cast<std::underlying_type_t<IrOpcode>>(opcode_);
			assert(false && "Unhandled opcode");
			break;
		}

		return oss.str();
	}

private:
	IrOpcode opcode_;
	OperandStorage operands_;
	Token first_token_;
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


