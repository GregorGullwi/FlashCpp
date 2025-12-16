#pragma once

#include <optional>
#include <vector>
#include <span>

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

inline TypedValue toTypedValue(std::span<const IrOperand> operands) {
	assert(operands.size() >= 3 && "Expected operand order [type][size_in_bits][value][type_index]");
	assert(std::holds_alternative<Type>(operands[0]) && "Expected operand order [type][size_in_bits][value][type_index]");
	assert(std::holds_alternative<int>(operands[1]) && "Expected operand order [type][size_in_bits][value][type_index]");
	
	TypedValue result;
	result.type = std::get<Type>(operands[0]);
	result.size_in_bits = std::get<int>(operands[1]);
	result.value = toIrValue(operands[2]);
	
	// type_index is required for struct types to enable proper name mangling.
	// For primitive types, it defaults to 0 if not provided.
	// Struct type producers (like generateConstructorCallIr) should always provide type_index.
	if (operands.size() >= 4 && std::holds_alternative<unsigned long long>(operands[3])) {
		result.type_index = static_cast<TypeIndex>(std::get<unsigned long long>(operands[3]));
	} else {
		result.type_index = 0;
	}
	
	return result;
}

inline TypedValue toTypedValue(const std::vector<IrOperand>& operands) {
	return toTypedValue(std::span<const IrOperand>(operands));
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
