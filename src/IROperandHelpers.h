#pragma once

#include <vector>
#include <span>

// Note: IrValue and all struct definitions (BinaryOp, etc.) are now in IRTypes.h
// This file only contains helper functions for working with those types

// ============================================================================
// Strong wrapper for pointer indirection depth.
//
// Design intent:
//   - Explicit construction prevents accidentally passing a bare integer literal
//     or TypeIndex value where pointer_depth is expected (the primary bug class).
//   - operator int() is provided for backward-compatible reads at existing
//     comparison and assignment sites; it will be narrowed once migration is done.
//   - The makeExprResult() 5-arg overload requires PointerDepth so that
//     swapping the type_index and pointer_depth arguments is a compile error.
// ============================================================================
struct PointerDepth {
	int value = 0;
	// Default construction is non-explicit so ExprResult{} aggregate init compiles.
	constexpr PointerDepth() noexcept = default;
	// Single-arg construction is explicit to prevent int→PointerDepth implicit conversion.
	constexpr explicit PointerDepth(int v) noexcept : value(v) {}
	// Implicit conversion to int for backward-compatible reads.
	constexpr operator int() const noexcept { return value; }
	constexpr bool operator==(PointerDepth o) const noexcept { return value == o.value; }
	constexpr bool operator!=(PointerDepth o) const noexcept { return value != o.value; }
	constexpr bool operator<(PointerDepth o)  const noexcept { return value <  o.value; }
	constexpr bool operator<=(PointerDepth o) const noexcept { return value <= o.value; }
	constexpr bool operator>(PointerDepth o)  const noexcept { return value >  o.value; }
	constexpr bool operator>=(PointerDepth o) const noexcept { return value >= o.value; }
};

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
	int size_in_bits = 0;
	IrOperand value{};
	TypeIndex type_index = 0;
	PointerDepth pointer_depth;  // was: int pointer_depth = 0
};

inline ExprResult makeExprResultImpl(
	Type type,
	int size_in_bits,
	IrOperand value,
	TypeIndex type_index,
	PointerDepth pointer_depth
) {
	return {
		.type = type,
		.size_in_bits = size_in_bits,
		.value = std::move(value),
		.type_index = type_index,
		.pointer_depth = pointer_depth
	};
}

inline ExprResult makeExprResult(Type type, int size_in_bits, IrOperand value) {
	return makeExprResultImpl(type, size_in_bits, std::move(value), 0, PointerDepth{});
}

inline ExprResult makeExprResult(Type type, int size_in_bits, IrOperand value, TypeIndex type_index) {
	return makeExprResultImpl(type, size_in_bits, std::move(value), type_index, PointerDepth{});
}

// Requires PointerDepth to prevent accidental type_index/pointer_depth argument swap.
inline ExprResult makeExprResult(Type type, int size_in_bits, IrOperand value, TypeIndex type_index, PointerDepth pointer_depth) {
	return makeExprResultImpl(type, size_in_bits, std::move(value), type_index, pointer_depth);
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
	
	return result;
}

inline TypedValue toTypedValue(const std::vector<IrOperand>& operands) {
	return toTypedValue(std::span<const IrOperand>(operands));
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
