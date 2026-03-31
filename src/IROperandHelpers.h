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

// Helper function to extract IrValue from IrOperand using index-based mapping
// IrOperand = std::variant<int, unsigned long long, double, bool, char, Type, TempVar, StringHandle>
// IrValue   = std::variant<unsigned long long, double, TempVar, StringHandle>
// Index mapping: IrOperand[1] -> IrValue[0], IrOperand[2] -> IrValue[1], IrOperand[6] -> IrValue[2], IrOperand[7] -> IrValue[3]
inline IrValue toIrValue(const IrOperand& operand) {
	// Map IrOperand variant indices to IrValue variant indices
	switch (operand.index()) {
	case 1:	// IrOperand[1] = unsigned long long -> IrValue[0] = unsigned long long
		assert(std::holds_alternative<unsigned long long>(operand) && "Expected unsigned long long");
		return std::get<1>(operand);
	case 2:	// IrOperand[2] = double -> IrValue[1] = double
		assert(std::holds_alternative<double>(operand) && "Expected double");
		return std::get<2>(operand);
	case 6:	// IrOperand[6] = TempVar -> IrValue[2] = TempVar
		assert(std::holds_alternative<TempVar>(operand) && "Expected TempVar");
		return std::get<6>(operand);
	case 7:	// IrOperand[7] = StringHandle -> IrValue[3] = StringHandle
		assert(std::holds_alternative<StringHandle>(operand) && "Expected StringHandle");
		return std::get<7>(operand);
	default:
		assert(false && "IrOperand does not contain a value type compatible with IrValue");
		return static_cast<unsigned long long>(0);  // Unreachable, but prevents warning
	}
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

inline TypedValue toTypedValue(const std::vector<IrOperand>& operands) {
	return toTypedValue(std::span<const IrOperand>(operands));
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
