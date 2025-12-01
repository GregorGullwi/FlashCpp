#pragma once

#include <optional>
#include <vector>
#include <span>

// Note: IrValue and all struct definitions (BinaryOp, etc.) are now in IRTypes.h
// This file only contains helper functions for working with those types

// Helper function to extract IrValue from IrOperand using index-based mapping
// IrOperand = std::variant<int, unsigned long long, double, bool, char, std::string, std::string_view, Type, TempVar>
// IrValue   = std::variant<unsigned long long, double, TempVar, std::string_view>
// Index mapping: IrOperand[1] -> IrValue[0], IrOperand[2] -> IrValue[1], IrOperand[6] -> IrValue[3], IrOperand[8] -> IrValue[2]
inline IrValue toIrValue(const IrOperand& operand) {
	// Map IrOperand variant indices to IrValue variant indices
	switch (operand.index()) {
		case 1:  // IrOperand[1] = unsigned long long -> IrValue[0] = unsigned long long
			assert(std::holds_alternative<unsigned long long>(operand) && "Expected unsigned long long");
			return std::get<1>(operand);
		case 2:  // IrOperand[2] = double -> IrValue[1] = double
			assert(std::holds_alternative<double>(operand) && "Expected double");
			return std::get<2>(operand);
		case 6:  // IrOperand[6] = std::string_view -> IrValue[3] = std::string_view
			assert(std::holds_alternative<std::string_view>(operand) && "Expected std::string_view");
			return std::get<6>(operand);
		case 8:  // IrOperand[8] = TempVar -> IrValue[2] = TempVar
			assert(std::holds_alternative<TempVar>(operand) && "Expected TempVar");
			return std::get<8>(operand);
		default:
			assert(false && "IrOperand does not contain a value type compatible with IrValue");
			return static_cast<unsigned long long>(0);  // Unreachable, but prevents warning
	}
}

inline TypedValue toTypedValue(std::span<const IrOperand> operands) {
	assert(operands.size() >= 3 && "Expected operand order [type][size_in_bits][value][optional type_index]");
	assert(std::holds_alternative<Type>(operands[0]) && "Expected operand order [type][size_in_bits][value][optional type_index]");
	assert(std::holds_alternative<int>(operands[1]) && "Expected operand order [type][size_in_bits][value][optional type_index]");
	
	TypedValue result;
	result.type = std::get<Type>(operands[0]);
	result.size_in_bits = std::get<int>(operands[1]);
	result.value = toIrValue(operands[2]);
	
	// Optional 4th element: type_index for struct types
	if (operands.size() >= 4 && std::holds_alternative<unsigned long long>(operands[3])) {
		result.type_index = static_cast<TypeIndex>(std::get<unsigned long long>(operands[3]));
	}
	
	return result;
}

inline TypedValue toTypedValue(const std::vector<IrOperand>& operands) {
	return toTypedValue(std::span<const IrOperand>(operands));
}

// ============================================================================
// Parse Helpers - Extract typed structs from IrInstruction
// ============================================================================

// Parse binary integer/comparison/float operation
// Returns true if parsing succeeded, false if operand layout doesn't match
inline bool parseBinaryOp(const IrInstruction& inst, BinaryOp& out) {
	// Binary ops have exactly 7 operands
	if (inst.getOperandCount() != 7) {
		return false;
	}

	// Extract and validate operands
	if (!inst.isOperandType<TempVar>(0)) return false;
	out.result = inst.getOperandAs<TempVar>(0);

	if (!inst.isOperandType<Type>(1)) return false;
	out.lhs.type = inst.getOperandAs<Type>(1);

	if (!inst.isOperandType<int>(2)) return false;
	out.lhs.size_in_bits = inst.getOperandAs<int>(2);

	// lhs_value can be unsigned long long, TempVar, or string_view
	out.lhs.value = toIrValue(inst.getOperand(3));

	if (!inst.isOperandType<Type>(4)) return false;
	out.rhs.type = inst.getOperandAs<Type>(4);

	if (!inst.isOperandType<int>(5)) return false;
	out.rhs.size_in_bits = inst.getOperandAs<int>(5);

	// rhs_value can be unsigned long long, TempVar, or string_view
	out.rhs.value = toIrValue(inst.getOperand(6));

	// Basic sanity checks
	assert(out.lhs.size_in_bits > 0 && "Invalid lhs.size_in_bits in binary op");
	assert(out.rhs.size_in_bits > 0 && "Invalid rhs.size_in_bits in binary op");

	return true;
}

// Parse conditional branch operation
inline bool parseCondBranchOp(const IrInstruction& inst, CondBranchOp& out) {
	// Conditional branch has exactly 5 operands
	if (inst.getOperandCount() != 5) {
		return false;
	}

	if (!inst.isOperandType<Type>(0)) return false;
	out.condition.type = inst.getOperandAs<Type>(0);

	if (!inst.isOperandType<int>(1)) return false;
	out.condition.size_in_bits = inst.getOperandAs<int>(1);

	// condition_value can be unsigned long long, TempVar, or string_view
	out.condition.value = toIrValue(inst.getOperand(2));

	if (!inst.isOperandType<std::string>(3)) return false;
	out.label_true = inst.getOperandAs<std::string>(3);

	if (!inst.isOperandType<std::string>(4)) return false;
	out.label_false = inst.getOperandAs<std::string>(4);

	return true;
}

// Parse function call operation
inline bool parseCallOp(const IrInstruction& inst, CallOp& out) {
	// Call must have at least 3 operands: result, function_name, symbol_index
	if (inst.getOperandCount() < 3) {
		return false;
	}

	if (!inst.isOperandType<TempVar>(0)) return false;
	out.result = inst.getOperandAs<TempVar>(0);

	if (!inst.isOperandType<std::string>(1)) return false;
	out.function_name = inst.getOperandAs<std::string>(1);

	// Parse arguments (triples of type, size, value)
	size_t operand_count = inst.getOperandCount();
	size_t arg_count = (operand_count - 3) / 3;
	out.args.clear();
	out.args.reserve(arg_count);

	for (size_t i = 0; i < arg_count; ++i) {
		size_t base = 2 + i * 3;
		
		if (!inst.isOperandType<Type>(base)) return false;
		Type arg_type = inst.getOperandAs<Type>(base);

		if (!inst.isOperandType<int>(base + 1)) return false;
		int arg_size_in_bits = inst.getOperandAs<int>(base + 1);

		// arg_value can be unsigned long long, TempVar, or string_view
		IrValue arg_value = toIrValue(inst.getOperand(base + 2));

		out.args.push_back(TypedValue{ .type = arg_type, .size_in_bits = arg_size_in_bits, .value = arg_value });
	}

	return true;
}

// Parse label definition
inline bool parseLabelOp(const IrInstruction& inst, LabelOp& out) {
	if (inst.getOperandCount() != 1) return false;

	// Label name can be string or string_view
	const auto& label_operand = inst.getOperand(0);
	if (std::holds_alternative<std::string>(label_operand)) {
		out.label_name = std::get<std::string>(label_operand);
	} else if (std::holds_alternative<std::string_view>(label_operand)) {
		out.label_name = std::string(std::get<std::string_view>(label_operand));
	} else {
		return false;
	}

	return true;
}

// Parse unconditional branch
inline bool parseBranchOp(const IrInstruction& inst, BranchOp& out) {
	if (inst.getOperandCount() != 1) return false;

	// Target label can be string or string_view
	const auto& label_operand = inst.getOperand(0);
	if (std::holds_alternative<std::string>(label_operand)) {
		out.target_label = std::get<std::string>(label_operand);
	} else if (std::holds_alternative<std::string_view>(label_operand)) {
		out.target_label = std::string(std::get<std::string_view>(label_operand));
	} else {
		return false;
	}

	return true;
}

// Parse return statement
inline bool parseReturnOp(const IrInstruction& inst, ReturnOp& out) {
	// Return can have 0 operands (void) or 3 operands (value)
	if (inst.getOperandCount() == 0) {
		// Void return
		out.return_type = std::nullopt;
		out.return_size = 0;
		out.return_value = std::nullopt;
		return true;
	}

	if (inst.getOperandCount() != 3) return false;

	// Extract return type
	if (!inst.isOperandType<Type>(0)) return false;
	out.return_type = inst.getOperandAs<Type>(0);

	// Extract return size
	if (!inst.isOperandType<int>(1)) return false;
	out.return_size = inst.getOperandAs<int>(1);

	// Extract return value (can be unsigned long long, TempVar, or string_view)
	const auto& value_operand = inst.getOperand(2);
	if (std::holds_alternative<unsigned long long>(value_operand)) {
		out.return_value = std::get<unsigned long long>(value_operand);
	} else if (std::holds_alternative<TempVar>(value_operand)) {
		out.return_value = std::get<TempVar>(value_operand);
	} else if (std::holds_alternative<std::string_view>(value_operand)) {
		out.return_value = std::get<std::string_view>(value_operand);
	} else {
		return false;
	}

	return true;
}

// ============================================================================
// Build Helpers - Create operand vectors from typed structs
// ============================================================================

// ============================================================================
// Runtime dispatch version for when opcode is not known at compile time
inline bool parseOperands(const IrInstruction& inst, BinaryOp& out) {
	return parseBinaryOp(inst, out);
}

inline bool parseOperands(const IrInstruction& inst, CondBranchOp& out) {
	return parseCondBranchOp(inst, out);
}

inline bool parseOperands(const IrInstruction& inst, CallOp& out) {
	return parseCallOp(inst, out);
}

inline bool parseOperands(const IrInstruction& inst, LabelOp& out) {
	return parseLabelOp(inst, out);
}

inline bool parseOperands(const IrInstruction& inst, BranchOp& out) {
	return parseBranchOp(inst, out);
}

inline bool parseOperands(const IrInstruction& inst, ReturnOp& out) {
	return parseReturnOp(inst, out);
}

// Parse member load operation
inline bool parseMemberLoadOp(const IrInstruction& inst, MemberLoadOp& out) {
	// Member load must have at least 6 operands
	if (inst.getOperandCount() < 6) {
		return false;
	}

	if (!inst.isOperandType<TempVar>(0)) return false;
	out.result.value = inst.getOperandAs<TempVar>(0);

	if (!inst.isOperandType<Type>(1)) return false;
	out.result.type = inst.getOperandAs<Type>(1);

	if (!inst.isOperandType<int>(2)) return false;
	out.result.size_in_bits = inst.getOperandAs<int>(2);

	// Object can be string_view or TempVar
	if (inst.isOperandType<std::string_view>(3)) {
		out.object = inst.getOperandAs<std::string_view>(3);
	} else if (inst.isOperandType<TempVar>(3)) {
		out.object = inst.getOperandAs<TempVar>(3);
	} else {
		return false;
	}

	if (!inst.isOperandType<std::string_view>(4)) return false;
	out.member_name = inst.getOperandAs<std::string_view>(4);

	if (!inst.isOperandType<int>(5)) return false;
	out.offset = inst.getOperandAs<int>(5);

	// Optional reference metadata (must have all 3 or none)
	if (inst.getOperandCount() >= 9) {
		if (!inst.isOperandType<bool>(6)) return false;
		out.is_reference = inst.getOperandAs<bool>(6);
		
		if (!inst.isOperandType<bool>(7)) return false;
		out.is_rvalue_reference = inst.getOperandAs<bool>(7);
	} else {
		out.is_reference = false;
		out.is_rvalue_reference = false;
	}
	out.struct_type_info = nullptr;

	return true;
}

inline bool parseOperands(const IrInstruction& inst, MemberLoadOp& out) {
	return parseMemberLoadOp(inst, out);
}

// Parse string literal operation
inline bool parseStringLiteralOp(const IrInstruction& inst, StringLiteralOp& out) {
	// StringLiteral must have exactly 2 operands (legacy format)
	if (inst.getOperandCount() != 2) {
		return false;
	}

	if (!inst.isOperandType<TempVar>(0)) return false;
	out.result = inst.getOperandAs<TempVar>(0);

	if (!inst.isOperandType<std::string_view>(1)) return false;
	out.content = inst.getOperandAs<std::string_view>(1);

	return true;
}

inline bool parseOperands(const IrInstruction& inst, StringLiteralOp& out) {
	return parseStringLiteralOp(inst, out);
}

// Parse global load operation
inline bool parseGlobalLoadOp(const IrInstruction& inst, GlobalLoadOp& out) {
	// GlobalLoad must have exactly 2 operands
	if (inst.getOperandCount() != 2) {
		return false;
	}

	if (!inst.isOperandType<TempVar>(0)) return false;
	out.result.value = inst.getOperandAs<TempVar>(0);

	// Global name can be string or string_view
	if (inst.isOperandType<std::string>(1)) {
		out.global_name = inst.getOperandAs<std::string>(1);
	} else if (inst.isOperandType<std::string_view>(1)) {
		out.global_name = inst.getOperandAs<std::string_view>(1);
	} else {
		return false;
	}

	return true;
}

inline bool parseOperands(const IrInstruction& inst, GlobalLoadOp& out) {
	return parseGlobalLoadOp(inst, out);
}

// Parse function address operation
inline bool parseFunctionAddressOp(const IrInstruction& inst, FunctionAddressOp& out) {
	// FunctionAddress must have exactly 2 operands
	if (inst.getOperandCount() != 2) {
		return false;
	}

	if (!inst.isOperandType<TempVar>(0)) return false;
	out.result.value = inst.getOperandAs<TempVar>(0);

	if (!inst.isOperandType<std::string_view>(1)) return false;
	out.function_name = inst.getOperandAs<std::string_view>(1);

	return true;
}

inline bool parseOperands(const IrInstruction& inst, FunctionAddressOp& out) {
	return parseFunctionAddressOp(inst, out);
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

// ============================================================================
// Legacy/Typed Instruction Adapters for IRConverter
// ============================================================================

// Extract BinaryOp from either typed or legacy instruction
inline std::optional<BinaryOp> extractBinaryOp(const IrInstruction& inst) {
	// Try typed payload first
	if (const auto* typed = getTypedPayload<BinaryOp>(inst)) {
		return *typed;
	}
	
	// Fall back to legacy operand vector
	if (inst.getOperandCount() == 7) {
		BinaryOp result;
		if (parseBinaryOp(inst, result)) {
			return result;
		}
	}
	
	return std::nullopt;
}
