#pragma once

#include <optional>
#include <algorithm>
#include <vector>
#include <span>

#include "InlineVector.h"

// Note: IrValue and all struct definitions (BinaryOp, etc.) are now in IRTypes.h
// This file only contains helper functions for working with those types

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

using ExprOperands = InlineVector<IrOperand, 4>;

struct ExprResult {
	Type type = Type::Void;
	int size_in_bits = 0;
	IrOperand value{};
	TypeIndex type_index = 0;
	int pointer_depth = 0;
	// Legacy slot-4 compatibility for migrated sites whose encoded metadata depends on
	// something other than the outward result Type (for example enum identifiers keeping
	// type_index when lowered as integers, or enum pointers preserving pointer_depth).
	std::optional<unsigned long long> encoded_metadata;

	operator ExprOperands() const {
		unsigned long long metadata = 0;
		if (encoded_metadata.has_value()) {
			metadata = *encoded_metadata;
		} else if (type == Type::Struct || type == Type::Enum || type == Type::UserDefined) {
			metadata = static_cast<unsigned long long>(type_index);
		} else if (pointer_depth > 0) {
			metadata = static_cast<unsigned long long>(pointer_depth);
		}
		return { type, size_in_bits, value, metadata };
	}
};

inline ExprResult makeExprResult(
	Type type,
	int size_in_bits,
	IrOperand value,
	TypeIndex type_index = 0,
	int pointer_depth = 0
) {
	ExprResult result;
	result.type = type;
	result.size_in_bits = size_in_bits;
	result.value = std::move(value);
	result.type_index = type_index;
	result.pointer_depth = pointer_depth;
	return result;
}

inline void preserveLegacyEnumPointerDepthEncoding(ExprResult& result) {
	if (result.type == Type::Enum && result.pointer_depth > 0) {
		result.encoded_metadata = static_cast<unsigned long long>(result.pointer_depth);
	}
}

inline TypedValue toTypedValue(std::span<const IrOperand> operands) {
	assert(operands.size() >= 3 && "Expected operand order [type][size_in_bits][value][metadata]");
	assert(std::holds_alternative<Type>(operands[0]) && "Expected operand order [type][size_in_bits][value][metadata]");
	assert(std::holds_alternative<int>(operands[1]) && "Expected operand order [type][size_in_bits][value][metadata]");
	
	TypedValue result;
	result.type = std::get<Type>(operands[0]);
	result.size_in_bits = std::get<int>(operands[1]);
	result.value = toIrValue(operands[2]);
	result.type_index = 0;
	result.pointer_depth = 0;
	
	// The optional 4th operand is overloaded:
	// - struct/enum values use it for type_index
	// - non-struct values use it for pointer_depth
	if (operands.size() >= 4 && std::holds_alternative<unsigned long long>(operands[3])) {
		unsigned long long metadata = std::get<unsigned long long>(operands[3]);
		if (result.type == Type::Struct || result.type == Type::Enum || result.type == Type::UserDefined) {
			result.type_index = static_cast<TypeIndex>(metadata);
		} else {
			result.pointer_depth = static_cast<int>(metadata);
		}
	}
	
	return result;
}

inline TypedValue toTypedValue(const std::vector<IrOperand>& operands) {
	return toTypedValue(std::span<const IrOperand>(operands));
}

inline TypedValue toTypedValue(const ExprOperands& operands) {
	assert(operands.size() >= 3 && operands.size() <= 4 && "ExprOperands must contain exactly 3 or 4 operands");
	std::array<IrOperand, 4> inline_copy{};
	std::copy_n(operands.begin(), operands.size(), inline_copy.begin());
	return toTypedValue(std::span<const IrOperand>(inline_copy.data(), operands.size()));
}

inline TypedValue toTypedValue(const ExprResult& result) {
	return toTypedValue(static_cast<ExprOperands>(result));
}

inline ExprResult toExprResult(std::span<const IrOperand> operands) {
	assert(operands.size() >= 3 && "Expected operand order [type][size_in_bits][value][metadata]");
	assert(std::holds_alternative<Type>(operands[0]) && "Expected operand order [type][size_in_bits][value][metadata]");
	assert(std::holds_alternative<int>(operands[1]) && "Expected operand order [type][size_in_bits][value][metadata]");

	ExprResult result;
	result.type = std::get<Type>(operands[0]);
	result.size_in_bits = std::get<int>(operands[1]);
	result.value = operands[2];

	TypedValue typed_value = toTypedValue(operands);
	result.type_index = typed_value.type_index;
	result.pointer_depth = typed_value.pointer_depth;

	if (operands.size() >= 4 && std::holds_alternative<unsigned long long>(operands[3])) {
		result.encoded_metadata = std::get<unsigned long long>(operands[3]);
	}

	return result;
}

inline ExprResult toExprResult(const std::vector<IrOperand>& operands) {
	return toExprResult(std::span<const IrOperand>(operands));
}

inline ExprResult toExprResult(const ExprOperands& operands) {
	assert(operands.size() >= 3 && operands.size() <= 4 && "ExprOperands must contain exactly 3 or 4 operands");
	std::array<IrOperand, 4> inline_copy{};
	std::copy_n(operands.begin(), operands.size(), inline_copy.begin());
	return toExprResult(std::span<const IrOperand>(inline_copy.data(), operands.size()));
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
