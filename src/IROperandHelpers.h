#pragma once

#include <vector>
#include <span>
#include <array>
#include <string_view>
#include <optional>

// Note: IrValue and all struct definitions (BinaryOp, etc.) are now in IRTypes.h
// This file only contains helper functions for working with those types
// PointerDepth is defined in IRTypes_Core.h (included transitively via IRTypes.h)

// ============================================================================
// Compound assignment operator → base binary opcode mapping.
//
// This table is shared by:
//   - IrGenerator_Expr_Operators.cpp  (cross-type path, global-assign path,
//     handleLValueCompoundAssignment)
//   - SemanticAnalysis.cpp            (is_compound_assign classification)
//
// The base opcode returned is the signed/generic variant; callers are
// responsible for upgrading to an unsigned variant when the operand type is
// unsigned (e.g. UnsignedDivide, UnsignedShiftRight, UnsignedModulo).
// ============================================================================
struct CompoundOpEntry {
	std::string_view op;
	IrOpcode base_opcode;
};

inline constexpr std::array<CompoundOpEntry, 10> kCompoundOpTable = {{
	{"+=",  IrOpcode::Add},
	{"-=",  IrOpcode::Subtract},
	{"*=",  IrOpcode::Multiply},
	{"/=",  IrOpcode::Divide},
	{"%=",  IrOpcode::Modulo},
	{"&=",  IrOpcode::BitwiseAnd},
	{"|=",  IrOpcode::BitwiseOr},
	{"^=",  IrOpcode::BitwiseXor},
	{"<<=", IrOpcode::ShiftLeft},
	{">>=", IrOpcode::ShiftRight},
}};

/// Returns the base binary IrOpcode for a compound-assignment operator string,
/// or std::nullopt if the string is not a recognized compound-assignment op.
inline std::optional<IrOpcode> compoundOpToBaseOpcode(std::string_view op) {
	for (const auto& entry : kCompoundOpTable) {
		if (entry.op == op) return entry.base_opcode;
	}
	return std::nullopt;
}

/// Returns true iff \p op is a compound-assignment operator.
inline bool isCompoundAssignmentOp(std::string_view op) {
	return compoundOpToBaseOpcode(op).has_value();
}

// Helper function to extract IrValue from IrOperand using index-based mapping
// IrOperand = std::variant<int, unsigned long long, double, bool, char, Type, TempVar, StringHandle>
// IrValue   = std::variant<unsigned long long, double, TempVar, StringHandle>
// Index mapping: IrOperand[1] -> IrValue[0], IrOperand[2] -> IrValue[1], IrOperand[6] -> IrValue[2], IrOperand[7] -> IrValue[3]
inline IrValue toIrValue(const IrOperand& operand) {
	// Map IrOperand variant indices to IrValue variant indices
	switch (operand.index()) {
		case 1:  // IrOperand[1] = unsigned long long -> IrValue[0] = unsigned long long
			assert(std::holds_alternative<unsigned long long>(operand) && "Expected unsigned long long");
			return std::get<1>(operand);
		case 2:  // IrOperand[2] = double -> IrValue[1] = double
			assert(std::holds_alternative<double>(operand) && "Expected double");
			return std::get<2>(operand);
		case 6:  // IrOperand[6] = TempVar -> IrValue[2] = TempVar
			assert(std::holds_alternative<TempVar>(operand) && "Expected TempVar");
			return std::get<6>(operand);
		case 7:  // IrOperand[7] = StringHandle -> IrValue[3] = StringHandle
			assert(std::holds_alternative<StringHandle>(operand) && "Expected StringHandle");
			return std::get<7>(operand);
		default:
			assert(false && "IrOperand does not contain a value type compatible with IrValue");
			return static_cast<unsigned long long>(0);  // Unreachable, but prevents warning
	}
}

struct ExprResult {
	Type type = Type::Void;
	SizeInBits size_in_bits;  // was: int size_in_bits = 0
	IrOperand value{};
	TypeIndex type_index {};
	PointerDepth pointer_depth;  // was: int pointer_depth = 0
	IrType ir_type = IrType::Void;  // Runtime representation type (authoritative for IR/codegen)

	// Returns the effective runtime representation type.
	// Mirrors TypedValue::effectiveIrType() — duplicated here because ExprResult
	// and TypedValue are independent structs during the transition period.
	// Both will be unified when ExprResult's type field is replaced by IrType
	// (Phase 5).
	IrType effectiveIrType() const {
		if (ir_type != IrType::Void || type == Type::Void)
			return ir_type;
		return toIrType(type);
	}
};

inline ExprResult makeExprResultImpl(
	Type type,
	SizeInBits size_in_bits,
	IrOperand value,
	TypeIndex type_index,
	PointerDepth pointer_depth
) {
	return {
		.type = type,
		.size_in_bits = size_in_bits,
		.value = std::move(value),
		.type_index = type_index,
		.pointer_depth = pointer_depth,
		.ir_type = toIrType(type)
	};
}

inline ExprResult makeExprResult(Type type, SizeInBits size_in_bits, IrOperand value) {
	return makeExprResultImpl(type, size_in_bits, std::move(value), TypeIndex{}, PointerDepth{});
}

inline ExprResult makeExprResult(Type type, SizeInBits size_in_bits, IrOperand value, TypeIndex type_index) {
	return makeExprResultImpl(type, size_in_bits, std::move(value), type_index, PointerDepth{});
}

// Requires PointerDepth to prevent accidental type_index/pointer_depth argument swap.
inline ExprResult makeExprResult(Type type, SizeInBits size_in_bits, IrOperand value, TypeIndex type_index, PointerDepth pointer_depth) {
	return makeExprResultImpl(type, size_in_bits, std::move(value), type_index, pointer_depth);
}

// ============================================================================
// TypedValue factory helpers
//
// These replace direct aggregate initializations like TypedValue{type, size, value}
// to ensure ir_type is always populated from the semantic type at construction time.
// This is Phase 4 preparation: once all construction sites use makeTypedValue and
// all read sites use effectiveIrType(), the semantic `type` field can be removed.
// ============================================================================

/// Basic TypedValue factory — sets ir_type from semantic type automatically.
inline TypedValue makeTypedValue(Type type, SizeInBits size_in_bits, IrValue value) {
	TypedValue tv;
	tv.type = type;
	tv.ir_type = toIrType(type);
	tv.size_in_bits = size_in_bits;
	tv.value = std::move(value);
	return tv;
}

/// TypedValue factory with type_index — for Struct/Enum/UserDefined types that
/// carry a type_index for layout and identity information.
inline TypedValue makeTypedValue(Type type, SizeInBits size_in_bits, IrValue value, TypeIndex type_index) {
	TypedValue tv = makeTypedValue(type, size_in_bits, std::move(value));
	tv.type_index = type_index;
	return tv;
}

/// TypedValue factory with type_index and pointer_depth.
inline TypedValue makeTypedValue(Type type, SizeInBits size_in_bits, IrValue value, TypeIndex type_index, PointerDepth pointer_depth) {
	TypedValue tv = makeTypedValue(type, size_in_bits, std::move(value), type_index);
	tv.pointer_depth = pointer_depth;
	return tv;
}

/// TypedValue factory with ReferenceQualifier — for reference-typed values
/// (e.g. by-reference function arguments). Sets ir_type automatically.
inline TypedValue makeTypedValue(Type type, SizeInBits size_in_bits, IrValue value, ReferenceQualifier ref_qual) {
	TypedValue tv = makeTypedValue(type, size_in_bits, std::move(value));
	tv.ref_qualifier = ref_qual;
	return tv;
}

inline TypedValue toTypedValue(std::span<const IrOperand> operands) {
	assert(operands.size() >= 3 && "Expected operand order [type][size_in_bits][value][metadata]");
	assert(std::holds_alternative<Type>(operands[0]) && "Expected operand order [type][size_in_bits][value][metadata]");
	assert(std::holds_alternative<int>(operands[1]) && "Expected operand order [type][size_in_bits][value][metadata]");
	
	TypedValue result;
	result.type = std::get<Type>(operands[0]);
	result.ir_type = toIrType(result.type);
	result.size_in_bits = SizeInBits{std::get<int>(operands[1])};
	result.value = toIrValue(operands[2]);
	result.type_index = TypeIndex{};
	result.pointer_depth = PointerDepth{};
	
	return result;
}

inline TypedValue toTypedValue(const std::vector<IrOperand>& operands) {
	return toTypedValue(std::span<const IrOperand>(operands));
}

inline TypedValue toTypedValue(const ExprResult& result) {
	TypedValue tv;
	tv.type = result.type;
	tv.ir_type = result.ir_type;
	tv.size_in_bits = result.size_in_bits;
	tv.value = toIrValue(result.value);
	tv.type_index = result.type_index;
	tv.pointer_depth = result.pointer_depth;
	return tv;
}

// ============================================================================
// Typed Payload Helper Functions
// ============================================================================

// Helper to get typed payload using std::any
template<typename T>
inline const T* getTypedPayload(const IrInstruction& inst) {
	if (!inst.hasTypedPayload()) return nullptr;
	return std::any_cast<T>(&inst.getTypedPayload());
}

// Typed constructor implementation
template<typename PayloadType>
inline IrInstruction::IrInstruction(IrOpcode opcode, PayloadType&& payload, Token first_token)
	: opcode_(opcode), operands_(), first_token_(first_token),
	  typed_payload_(std::forward<PayloadType>(payload)) {
}
