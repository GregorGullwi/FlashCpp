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
		} else if ((type == Type::Enum || type == Type::UserDefined) && pointer_depth > 0) {
			metadata = static_cast<unsigned long long>(pointer_depth);
		} else if (type == Type::Struct || type == Type::Enum || type == Type::UserDefined) {
			metadata = static_cast<unsigned long long>(type_index);
		} else if (pointer_depth > 0) {
			metadata = static_cast<unsigned long long>(pointer_depth);
		}
		return { type, size_in_bits, value, metadata };
	}
};

// Explicit wrapper for the temporary legacy slot-4 bridge so callers must opt in
// when they need to preserve raw encoded metadata during the ExprResult migration.
struct EncodedExprMetadata {
	std::optional<unsigned long long> value;
};

inline EncodedExprMetadata preserveEncodedExprMetadata(unsigned long long value) {
	return EncodedExprMetadata{value};
}

inline EncodedExprMetadata preserveEncodedExprMetadata(std::optional<unsigned long long> value) {
	return EncodedExprMetadata{value};
}

inline ExprResult makeExprResultImpl(
	Type type,
	int size_in_bits,
	IrOperand value,
	TypeIndex type_index,
	int pointer_depth,
	std::optional<unsigned long long> encoded_metadata
) {
	return {
		.type = type,
		.size_in_bits = size_in_bits,
		.value = std::move(value),
		.type_index = type_index,
		.pointer_depth = pointer_depth,
		.encoded_metadata = encoded_metadata
	};
}

inline ExprResult makeExprResult(Type type, int size_in_bits, IrOperand value) {
	return makeExprResultImpl(type, size_in_bits, std::move(value), 0, 0, std::nullopt);
}

inline ExprResult makeExprResult(Type type, int size_in_bits, IrOperand value, TypeIndex type_index) {
	return makeExprResultImpl(type, size_in_bits, std::move(value), type_index, 0, std::nullopt);
}

inline ExprResult makeExprResult(Type type, int size_in_bits, IrOperand value, TypeIndex type_index, int pointer_depth) {
	return makeExprResultImpl(type, size_in_bits, std::move(value), type_index, pointer_depth, std::nullopt);
}

// Tiny aggregate builder for migrated Phase 2 sites that want named ExprResult
// fields without open-coding member assignments. The legacy slot-4 escape hatch
// is intentionally explicit so callers cannot accidentally pass raw metadata in
// the wrong positional slot.
inline ExprResult makeExprResult(
	Type type,
	int size_in_bits,
	IrOperand value,
	TypeIndex type_index,
	int pointer_depth,
	EncodedExprMetadata encoded_metadata
) {
	return makeExprResultImpl(type, size_in_bits, std::move(value), type_index, pointer_depth, std::move(encoded_metadata.value));
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
	TypedValue tv;
	tv.type = result.type;
	tv.size_in_bits = result.size_in_bits;
	tv.value = toIrValue(result.value);
	tv.type_index = result.type_index;
	tv.pointer_depth = result.pointer_depth;
	return tv;
}

// Temporary Phase 2 bridge: decode positional expression operands into named
// ExprResult fields while also preserving the raw slot-4 payload in
// `encoded_metadata`. This lets migrated helpers use named fields internally
// but round-trip back to legacy positional consumers without changing the
// compatibility encoding yet.
inline ExprResult toExprResult(std::span<const IrOperand> operands) {
	assert(operands.size() >= 3 && "Expected operand order [type][size_in_bits][value][metadata]");
	assert(std::holds_alternative<Type>(operands[0]) && "Expected operand order [type][size_in_bits][value][metadata]");
	assert(std::holds_alternative<int>(operands[1]) && "Expected operand order [type][size_in_bits][value][metadata]");

	ExprResult result;
	result.type = std::get<Type>(operands[0]);
	result.size_in_bits = std::get<int>(operands[1]);
	result.value = operands[2];

	// Decode the optional 4th slot into named fields.
	//
	// The encoding rule used by tryBuildIdentifierOperand / generateIdentifierIr is:
	//   Type::Struct                        → type_index
	//   non-Struct with pointer_depth > 0   → pointer_depth   (includes Enum pointers!)
	//   otherwise                           → 0
	//
	// toTypedValue uses a DIFFERENT rule (Struct|Enum|UserDefined → type_index),
	// which is correct for non-pointer enum *values* but wrong for enum *pointers*.
	// We must match the encoder's rule here so that ExprResult named fields are
	// correct for all callers.
	if (operands.size() >= 4 && std::holds_alternative<unsigned long long>(operands[3])) {
		unsigned long long metadata = std::get<unsigned long long>(operands[3]);
		result.encoded_metadata = metadata;

		if (result.type == Type::Struct) {
			// Struct slot-4 is always type_index (even for struct pointers,
			// the encoder stores type_index, not pointer_depth).
			result.type_index = static_cast<TypeIndex>(metadata);
		} else if (result.size_in_bits == 64 && metadata > 0 && result.type == Type::Enum) {
			// Enum with 64-bit size and nonzero metadata:
			// under the current producer contract, non-pointer enum values are
			// lowered to their underlying runtime type before reaching this bridge,
			// so a 64-bit Type::Enum result here represents an enum pointer.
			result.pointer_depth = static_cast<int>(metadata);
		} else if (result.type == Type::Enum || result.type == Type::UserDefined) {
			// Non-pointer enum/UserDefined: slot-4 is type_index.
			result.type_index = static_cast<TypeIndex>(metadata);
		} else {
			// All other types: slot-4 is pointer_depth.
			result.pointer_depth = static_cast<int>(metadata);
		}
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
