#pragma once

#include "IRTypes.h"

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
	{"+=", IrOpcode::Add},
	{"-=", IrOpcode::Subtract},
	{"*=", IrOpcode::Multiply},
	{"/=", IrOpcode::Divide},
	{"%=", IrOpcode::Modulo},
	{"&=", IrOpcode::BitwiseAnd},
	{"|=", IrOpcode::BitwiseOr},
	{"^=", IrOpcode::BitwiseXor},
	{"<<=", IrOpcode::ShiftLeft},
	{">>=", IrOpcode::ShiftRight},
}};

/// Returns the base binary IrOpcode for a compound-assignment operator string,
/// or std::nullopt if the string is not a recognized compound-assignment op.
inline std::optional<IrOpcode> compoundOpToBaseOpcode(std::string_view op) {
	for (const auto& entry : kCompoundOpTable) {
		if (entry.op == op)
			return entry.base_opcode;
	}
	return std::nullopt;
}

/// Returns true iff \p op is a compound-assignment operator.
inline bool isCompoundAssignmentOp(std::string_view op) {
	return compoundOpToBaseOpcode(op).has_value();
}

// Convert value-bearing operands into the IR value representation. Integer-like
// literals share the unsigned carrier; their TypedValue retains the type and
// width needed to interpret or normalize the bits later.
inline IrValue toIrValue(const IrOperand& operand) {
	if (const auto* value = std::get_if<int>(&operand)) {
		return static_cast<unsigned long long>(*value);
	}
	if (const auto* value = std::get_if<unsigned long long>(&operand)) {
		return *value;
	}
	if (const auto* value = std::get_if<double>(&operand)) {
		return *value;
	}
	if (const auto* value = std::get_if<bool>(&operand)) {
		return static_cast<unsigned long long>(*value);
	}
	if (const auto* value = std::get_if<char>(&operand)) {
		return static_cast<unsigned long long>(static_cast<unsigned char>(*value));
	}
	if (const auto* value = std::get_if<TempVar>(&operand)) {
		return *value;
	}
	if (const auto* value = std::get_if<StringHandle>(&operand)) {
		return *value;
	}
	if (const auto* value = std::get_if<LocalVarId>(&operand)) {
		return *value;
	}
	throw InternalError("IrOperand does not contain a value compatible with IrValue");
}

struct ExprResult {
	SizeInBits size_in_bits;	 // was: int size_in_bits = 0
	IrOperand value{};
	TypeIndex type_index{};
	PointerDepth pointer_depth;	// was: int pointer_depth = 0
	IrType ir_type = IrType::Void;  // Runtime representation type (authoritative for IR/codegen)
	ValueStorage storage = ValueStorage::ContainsData;  // must be set explicitly at every construction site

	// Returns the effective runtime representation type.
	// Mirrors TypedValue::effectiveIrType() — duplicated here because ExprResult
	// and TypedValue are independent structs during the transition period.
	// Both will be unified when ExprResult's type field is replaced by IrType
	// (Phase 5).
	IrType effectiveIrType() const {
		if (ir_type != IrType::Void || category() == TypeCategory::Void)
			return ir_type;
		return toIrType(category());
	}

	// The expression's TypeCategory is embedded in type_index.category() so that
	// the gTypeInfo slot (index) and the expression-level category (e.g. Pointer
	// vs the pointed-to Struct) are stored together without a separate field.
	TypeCategory category() const { return type_index.category(); }
	TypeCategory typeEnum() const { return type_index.category(); }
};

// All five arguments are required; pass nativeTypeIndex(cat) / PointerDepth{} explicitly when unused.
// The TypeCategory is embedded in type_index — use TypeIndex{slot, cat} or nativeTypeIndex(cat).
inline ExprResult makeExprResult(TypeIndex type_index, SizeInBits size_in_bits, IrOperand value, PointerDepth pointer_depth, ValueStorage storage) {
	return {
		.size_in_bits = size_in_bits,
		.value = std::move(value),
		.type_index = type_index,
		.pointer_depth = pointer_depth,
		.ir_type = toIrType(type_index.category()),
		.storage = storage};
}

/// Returns a copy of \p tv with the storage discriminator set to \p storage.
/// Mirrors the ExprResult overload above for TypedValue construction sites.
inline TypedValue withStorage(TypedValue tv, ValueStorage storage) {
	tv.storage = storage;
	return tv;
}

// ============================================================================
// TypedValue factory helpers
//
// These replace direct aggregate initializations like TypedValue{type, size, value}
// to ensure ir_type is always populated from the semantic type at construction time.
// ============================================================================

/// Basic TypedValue factory — sets ir_type, is_signed, and TypeCategory in type_index automatically.
inline TypedValue makeTypedValue(TypeCategory type, SizeInBits size_in_bits, IrValue value) {
	TypedValue tv;
	tv.ir_type = toIrType(type);
	tv.is_signed = isSignedType(type);
	tv.size_in_bits = size_in_bits;
	tv.value = std::move(value);
	tv.type_index = nativeTypeIndex(type);
	return tv;
}

/// TypedValue factory with TypeIndex — for Struct/Enum/UserDefined types that carry
/// a type_index for layout and identity. Category is read from type_index.category().
inline TypedValue makeTypedValue(TypeIndex type_index, SizeInBits size_in_bits, IrValue value) {
	TypedValue tv;
	tv.ir_type = toIrType(type_index.category());
	tv.is_signed = isSignedType(type_index.category());
	tv.size_in_bits = size_in_bits;
	tv.value = std::move(value);
	tv.type_index = type_index;
	return tv;
}

/// TypedValue factory with TypeIndex and pointer_depth.
inline TypedValue makeTypedValue(TypeIndex type_index, SizeInBits size_in_bits, IrValue value, PointerDepth pointer_depth) {
	TypedValue tv = makeTypedValue(type_index, size_in_bits, std::move(value));
	tv.pointer_depth = pointer_depth;
	return tv;
}

/// TypedValue factory with ReferenceQualifier — for reference-typed values
/// (e.g. by-reference function arguments). Sets ir_type automatically.
inline TypedValue makeTypedValue(TypeCategory type, SizeInBits size_in_bits, IrValue value, ReferenceQualifier ref_qual) {
	TypedValue tv = makeTypedValue(type, size_in_bits, std::move(value));
	tv.ref_qualifier = ref_qual;
	return tv;
}

/// TypedValue factory with TypeIndex and ReferenceQualifier for reference-typed
/// values that still need concrete type identity.
inline TypedValue makeTypedValue(TypeIndex type_index, SizeInBits size_in_bits, IrValue value, ReferenceQualifier ref_qual) {
	TypedValue tv = makeTypedValue(type_index, size_in_bits, std::move(value));
	tv.ref_qualifier = ref_qual;
	return tv;
}

/// Build the TypedValue for an ordinary (non-this) indirect-call argument. By-value
/// aggregates must carry their canonical TypeIndex so the SysV backend can classify
/// the eightbytes; primitives keep their native type identity. Reference/pointer
/// storage flows through unchanged so the backend loads the address rather than the
/// aggregate contents.
inline TypedValue makeIndirectCallArgument(const ExprResult& argument_result) {
	const TypeCategory arg_type = argument_result.typeEnum();
	IrValue arg_value = toIrValue(argument_result.value);
	TypedValue arg_tv =
		(isIrStructType(toIrType(arg_type)) && argument_result.type_index.is_valid())
			? makeTypedValue(argument_result.type_index.withCategory(arg_type),
							 argument_result.size_in_bits, std::move(arg_value))
			: makeTypedValue(arg_type, argument_result.size_in_bits, std::move(arg_value));
	arg_tv.pointer_depth = argument_result.pointer_depth;
	arg_tv.storage = argument_result.storage;
	return arg_tv;
}

inline TypedValue toTypedValue(std::span<const IrOperand> operands) {
	assert(operands.size() >= 3 && "Expected operand order [type][size_in_bits][value][metadata]");
	assert(std::holds_alternative<TypeCategory>(operands[0]) && "Expected operand order [type][size_in_bits][value][metadata]");
	assert(std::holds_alternative<int>(operands[1]) && "Expected operand order [type][size_in_bits][value][metadata]");

	TypedValue result;
	const TypeCategory cat = std::get<TypeCategory>(operands[0]);
	result.ir_type = toIrType(cat);
	result.is_signed = isSignedType(cat);
	result.size_in_bits = SizeInBits{std::get<int>(operands[1])};
	result.value = toIrValue(operands[2]);
	result.type_index = nativeTypeIndex(cat);
	result.pointer_depth = PointerDepth{};
	// Optional 4th element: storage discriminator (ValueStorage cast to int)
	if (operands.size() >= 4) {
		result.storage = static_cast<ValueStorage>(std::get<int>(operands[3]));
	}

	return result;
}

inline TypedValue toTypedValue(const ExprResult& result) {
	TypedValue tv;
	tv.ir_type = result.ir_type;
	tv.is_signed = isSignedType(result.typeEnum());
	tv.size_in_bits = result.size_in_bits;
	tv.value = toIrValue(result.value);
	tv.type_index = result.type_index.withCategory(result.typeEnum());
	tv.pointer_depth = result.pointer_depth;
	tv.storage = result.storage;
	return tv;
}

// ============================================================================
// Typed Payload Helper Functions
// ============================================================================

// Helper to get typed payload using std::any
template <typename T>
inline const T* getTypedPayload(const IrInstruction& inst) {
	if (!inst.hasTypedPayload())
		return nullptr;
	return std::any_cast<T>(&inst.getTypedPayload());
}

// Typed constructor implementation
template <typename PayloadType>
inline IrInstruction::IrInstruction(IrOpcode opcode, PayloadType&& payload, Token first_token)
	: opcode_(opcode), operands_(), first_token_(first_token),
	  typed_payload_(std::forward<PayloadType>(payload)) {
}
