#pragma once

#include <optional>
#include <string>
#include <format>
#include <variant>
#include <vector>
#include <unordered_map>
#include <any>
#include <sstream>
#include <cassert>

#include "AstNodeTypes.h"
#include "IrType.h"
#include "Log.h"
#include "SizeTypes.h"

// Forward declare IrInstruction for circular dependency resolution
class IrInstruction;

// ============================================================================
// Strong wrapper for pointer indirection depth.
//
// Design intent:
//   - Explicit construction prevents accidentally passing a bare integer literal
//     or TypeIndex value where pointer_depth is expected.
//   - No implicit conversion to int; use .value explicitly at read sites.
//   - Defined here (IRTypes_Core.h) so both TypedValue (IRTypes_Ops.h) and
//     ExprResult (IROperandHelpers.h) can use the same type.
// ============================================================================
struct PointerDepth {
	int value = 0;
	// Default construction is non-explicit so aggregate-init with omitted
	// fields (e.g. TypedValue{}, ExprResult{}) keeps working.
	constexpr PointerDepth() noexcept = default;
	// Single-arg construction is explicit to prevent int→PointerDepth
	// implicit conversion at construction sites.
	constexpr explicit PointerDepth(int v) noexcept : value(v) {}
	constexpr auto operator<=>(const PointerDepth&) const noexcept = default;
	// True when pointer_depth > 0 (i.e., this is a pointer or reference type).
	constexpr bool is_pointer() const noexcept { return value > 0; }
};

// Allow PointerDepth to be used directly in std::format / FLASH_LOG_FORMAT.
// The value is formatted as the underlying int.
template <>
struct std::formatter<PointerDepth, char> : std::formatter<int, char> {
	auto format(const PointerDepth& pd, std::format_context& ctx) const {
		return std::formatter<int, char>::format(pd.value, ctx);
	}
};

enum class IrOpcode : int_fast16_t {
	Add,
	Subtract,
	Multiply,
	Divide,
	UnsignedDivide,
	Modulo,
	UnsignedModulo,
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
	ArrayElementAddress,	 // Calculate address of array element without loading value
	// Pointer operations
	AddressOf,
	AddressOfMember,	 // Calculate address of struct member: &obj.member
	ComputeAddress,		// One-pass address computation for complex expressions: &arr[i].member1.member2
	Dereference,
	DereferenceStore,	  // Store through a pointer: *ptr = value
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
	HeapAlloc,	   // new Type or new Type(args)
	HeapAllocArray,	// new Type[size]
	HeapFree,		  // delete ptr
	HeapFreeArray,   // delete[] ptr
	PlacementNew,	  // new (address) Type or new (address) Type(args)
	// RTTI operations
	Typeid,			// typeid(expr) or typeid(Type) - returns pointer to type_info
	DynamicCast,	 // dynamic_cast<Type>(expr) - runtime type checking cast
	// Static storage duration
	GlobalVariableDecl,	// Global variable declaration: [type, size, name, is_initialized, init_value?]
	GlobalLoad,			// Load from global variable: [result_temp, global_name]
	GlobalStore,		 // Store to global variable: [global_name, source_temp]
	// Lambda support
	FunctionAddress,	 // Get address of a function: [result_temp, function_name]
	IndirectCall,		  // Call through function pointer: [result_temp, func_ptr, arg1, arg2, ...]
	// Exception handling
	TryBegin,			  // Begin try block: [label_for_handlers]
	TryEnd,			  // End try block
	CatchBegin,			// Begin catch handler: [exception_var_temp, type_index, catch_end_label]
	CatchEnd,			  // End catch handler
	Throw,			   // Throw exception: [exception_temp, type_index]
	Rethrow,			 // Rethrow current exception (throw; with no argument)
	FunctionCleanupLP,   // Function-level cleanup landing pad (ELF/Linux only): destructor calls + unwind resume / terminate
	ElfCatchNoMatch,	 // ELF-only: emitted before handlers_end_label; jumps to cleanup LP when no catch matched
	// Windows SEH (Structured Exception Handling)
	SehTryBegin,		 // Begin __try block: [label_for_handlers]
	SehTryEnd,		   // End __try block
	SehExceptBegin,		// Begin __except handler: [filter_result_temp, except_end_label]
	SehExceptEnd,		  // End __except handler
	SehFinallyBegin,	 // Begin __finally handler (funclet entry point)
	SehFinallyEnd,	   // End __finally handler (funclet return)
	SehFinallyCall,		// Call __finally funclet for normal flow
	SehFilterBegin,		// Begin filter funclet (RCX=EXCEPTION_POINTERS*, RDX=EstablisherFrame)
	SehFilterEnd,		  // End filter funclet (return filter result in EAX)
	SehLeave,			  // __leave statement: jump to end of __try block
	SehGetExceptionCode, // GetExceptionCode() intrinsic - reads ExceptionCode from RCX in filter funclet
	SehGetExceptionInfo, // GetExceptionInformation() intrinsic - returns EXCEPTION_POINTERS* (RCX) in filter funclet
	SehSaveExceptionCode,	  // Save ExceptionCode from filter's [rsp+8] to a parent-frame slot
	SehGetExceptionCodeBody,	 // Read ExceptionCode from parent-frame slot (in __except body)
	SehAbnormalTermination,	// _abnormal_termination() intrinsic - reads ECX saved in finally funclet prologue
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
	if (total_operand_count < FIRST_PARAM_INDEX)
		return 0;
	return (total_operand_count - FIRST_PARAM_INDEX) / OPERANDS_PER_PARAM;
}

	// Helper function to validate that operand count is correct for given parameter count
constexpr bool isValidOperandCount(size_t total_operand_count) {
	if (total_operand_count < FIRST_PARAM_INDEX)
		return false;
	return (total_operand_count - FIRST_PARAM_INDEX) % OPERANDS_PER_PARAM == 0;
}
} // namespace FunctionDeclLayout
