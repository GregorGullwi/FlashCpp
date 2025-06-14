#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <any>
#include <sstream>
#include <cassert>

#include "AstNodeTypes.h"

enum class IrOpcode {
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
};

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
	TempVar() = default;
	explicit TempVar(size_t idx) : index(idx) {}

	TempVar next() {
		return TempVar(++index);
	}
	std::string_view name() const {
		return temp_name_array[index];
	}
	size_t index = 0;
};

using IrOperand = std::variant<int, unsigned long long, double, bool, char, std::string, std::string_view, Type, TempVar>;

class IrInstruction {
public:
	IrInstruction(IrOpcode opcode, std::vector<IrOperand>&& operands)
		: opcode_(opcode), operands_(std::move(operands)), line_number_(0) {}

	IrInstruction(IrOpcode opcode, std::vector<IrOperand>&& operands, uint32_t line_number)
		: opcode_(opcode), operands_(std::move(operands)), line_number_(line_number) {}

	IrOpcode getOpcode() const { return opcode_; }
	size_t getOperandCount() const { return operands_.size(); }
	uint32_t getLineNumber() const { return line_number_; }

	std::optional<IrOperand> getOperandSafe(size_t index) const {
		return index < operands_.size() ? std::optional<IrOperand>{ operands_[index] } : std::optional<IrOperand>{};
	}

	const IrOperand& getOperand(size_t index) const {
		return operands_[index];
	}

	template<class TClass>
	const TClass& getOperandAs(size_t index) const {
		return std::get<TClass>(operands_[index]);
	}

	std::string_view getOperandAsTypeString(size_t index) const {
		if (index >= operands_.size())
			return "";

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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = add ";
				if (getOperandCount() > 1) {
					oss << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";
					
					if (isOperandType<unsigned long long>(3))
						oss << getOperandAs<unsigned long long>(3);
					else if (isOperandType<TempVar>(3))
						oss << '%' << getOperandAs<TempVar>(3).index;
					else if (isOperandType<std::string_view>(3))
						oss << '%' << getOperandAs<std::string_view>(3);

					oss << ", ";

					if (isOperandType<unsigned long long>(6))
						oss << getOperandAs<unsigned long long>(6);
					else if (isOperandType<TempVar>(6))
						oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = sub ";
				if (getOperandCount() > 1) {
					oss << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";
					
					if (isOperandType<unsigned long long>(3))
						oss << getOperandAs<unsigned long long>(3);
					else if (isOperandType<TempVar>(3))
						oss << '%' << getOperandAs<TempVar>(3).index;
					else if (isOperandType<std::string_view>(3))
						oss << '%' << getOperandAs<std::string_view>(3);

					oss << ", ";

					if (isOperandType<unsigned long long>(6))
						oss << getOperandAs<unsigned long long>(6);
					else if (isOperandType<TempVar>(6))
						oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = mul ";
				if (getOperandCount() > 1) {
					oss << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";
					
					if (isOperandType<unsigned long long>(3))
						oss << getOperandAs<unsigned long long>(3);
					else if (isOperandType<TempVar>(3))
						oss << '%' << getOperandAs<TempVar>(3).index;
					else if (isOperandType<std::string_view>(3))
						oss << '%' << getOperandAs<std::string_view>(3);

					oss << ", ";

					if (isOperandType<unsigned long long>(6))
						oss << getOperandAs<unsigned long long>(6);
					else if (isOperandType<TempVar>(6))
						oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = div ";
				if (getOperandCount() > 1) {
					oss << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

					if (isOperandType<unsigned long long>(3))
						oss << getOperandAs<unsigned long long>(3);
					else if (isOperandType<TempVar>(3))
						oss << '%' << getOperandAs<TempVar>(3).index;
					else if (isOperandType<std::string_view>(3))
						oss << '%' << getOperandAs<std::string_view>(3);

					oss << ", ";

					if (isOperandType<unsigned long long>(6))
						oss << getOperandAs<unsigned long long>(6);
					else if (isOperandType<TempVar>(6))
						oss << '%' << getOperandAs<TempVar>(6).index;
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
						oss << getOperandAs<TempVar>(0).index;
					else if (isOperandType<std::string_view>(0))
						oss << getOperandAs<std::string_view>(0);

					oss << " = shl " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

					if (isOperandType<unsigned long long>(3))
						oss << getOperandAs<unsigned long long>(3);
					else if (isOperandType<TempVar>(3))
						oss << '%' << getOperandAs<TempVar>(3).index;
					else if (isOperandType<std::string_view>(3))
						oss << '%' << getOperandAs<std::string_view>(3);

					oss << ", ";

					if (isOperandType<unsigned long long>(6))
						oss << getOperandAs<unsigned long long>(6);
					else if (isOperandType<TempVar>(6))
						oss << '%' << getOperandAs<TempVar>(6).index;
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
						oss << getOperandAs<TempVar>(0).index;
					else if (isOperandType<std::string_view>(0))
						oss << getOperandAs<std::string_view>(0);

					oss << " = shr " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

					if (isOperandType<unsigned long long>(3))
						oss << getOperandAs<unsigned long long>(3);
					else if (isOperandType<TempVar>(3))
						oss << '%' << getOperandAs<TempVar>(3).index;
					else if (isOperandType<std::string_view>(3))
						oss << '%' << getOperandAs<std::string_view>(3);

					oss << ", ";

					if (isOperandType<unsigned long long>(6))
						oss << getOperandAs<unsigned long long>(6);
					else if (isOperandType<TempVar>(6))
						oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = udiv " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = lshr " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fadd " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fsub " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fmul " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fdiv " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = and " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = or " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = xor " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = srem " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = xor " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp eq " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp ne " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp slt " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp ult " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp sle " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp sgt " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = icmp sge " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fcmp oeq " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fcmp one " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fcmp olt " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fcmp ole " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fcmp ogt " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = fcmp oge " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = and i1 ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = or i1 ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
				else if (isOperandType<std::string_view>(3))
					oss << '%' << getOperandAs<std::string_view>(3);

				oss << ", ";

				if (isOperandType<unsigned long long>(6))
					oss << getOperandAs<unsigned long long>(6);
				else if (isOperandType<TempVar>(6))
					oss << '%' << getOperandAs<TempVar>(6).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = xor i1 ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = sext " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = zext " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
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
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = trunc " << getOperandAsTypeString(1) << getOperandAs<int>(2) << " ";

				if (isOperandType<unsigned long long>(3))
					oss << getOperandAs<unsigned long long>(3);
				else if (isOperandType<TempVar>(3))
					oss << '%' << getOperandAs<TempVar>(3).index;
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
					oss << '%' << getOperandAs<TempVar>(2).index;
				else if (isOperandType<std::string_view>(2))
					oss << '%' << getOperandAs<std::string_view>(2);
			}
		}
		break;

		case IrOpcode::FunctionDecl:
		{
			// define [Type][SizeInBits] [Name]
			oss << "define "
				<< getOperandAsTypeString(0) << getOperandAs<int>(1) << " "
				<< getOperandAs<std::string_view>(2);
		}
		break;

		case IrOpcode::FunctionCall:
		{
			// % = call [Type][SizeInBits] @[FuncName]()
			if (getOperandCount() > 0) {
				oss << '%';
				if (isOperandType<TempVar>(0))
					oss << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);

				oss << " = call ";
				if (getOperandCount() > 1) {
					oss << "@" << getOperandAs<std::string_view>(1) << "(";

					const size_t funcSymbolIndex = getOperandCount() - 1;
					for (size_t i = 2; i < funcSymbolIndex; i += 3) {
						if (i > 2) oss << ", ";
						oss << getOperandAsTypeString(i) << getOperandAs<int>(i + 1) << " ";
						
						if (isOperandType<unsigned long long>(i + 2))
							oss << getOperandAs<unsigned long long>(i + 2);
						else if (isOperandType<TempVar>(i + 2))
							oss << '%' << getOperandAs<TempVar>(i + 2).index;
						else if (isOperandType<std::string_view>(i + 2))
							oss << '%' << getOperandAs<std::string_view>(i + 2);
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
			oss << getOperandAs<std::string_view>(2);
			oss << " = alloca ";
			oss << getOperandAsTypeString(0) << getOperandAs<int>(1);
		}
		break;

		case IrOpcode::Store:
		{
			// store [Type][SizeInBits] [Value] to [Dest]
			oss << "store ";
			oss << getOperandAsTypeString(0) << getOperandAs<int>(1) << " ";
			
			// Source register
			X64Register srcReg = static_cast<X64Register>(getOperandAs<int>(3));
			switch (srcReg) {
			case X64Register::RCX: oss << "RCX"; break;
			case X64Register::RDX: oss << "RDX"; break;
			case X64Register::R8: oss << "R8"; break;
			case X64Register::R9: oss << "R9"; break;
			default: oss << "R" << static_cast<int>(srcReg); break;
			}
			
			oss << " to %" << getOperandAs<std::string_view>(2);
		}
		break;

		case IrOpcode::Branch:
		{
			// br label %label
			assert(getOperandCount() == 1 && "Branch instruction must have exactly 1 operand");
			if (getOperandCount() > 0) {
				oss << "br label %";
				if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);
			}
		}
		break;

		case IrOpcode::ConditionalBranch:
		{
			// br i1 %condition, label %true_label, label %false_label
			assert(getOperandCount() == 3 && "ConditionalBranch instruction must have exactly 3 operands");
			if (getOperandCount() > 0) {
				oss << "br i1 ";
				if (isOperandType<TempVar>(0))
					oss << '%' << getOperandAs<TempVar>(0).index;
				else if (isOperandType<std::string_view>(0))
					oss << '%' << getOperandAs<std::string_view>(0);

				oss << ", label %";
				if (isOperandType<std::string_view>(1))
					oss << getOperandAs<std::string_view>(1);

				oss << ", label %";
				if (isOperandType<std::string_view>(2))
					oss << getOperandAs<std::string_view>(2);
			}
		}
		break;

		case IrOpcode::Label:
		{
			// label_name:
			assert(getOperandCount() == 1 && "Label instruction must have exactly 1 operand");
			if (getOperandCount() > 0) {
				if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0) << ":";
			}
		}
		break;

		case IrOpcode::LoopBegin:
		{
			// loop_begin %loop_label
			assert(getOperandCount() == 1 && "LoopBegin instruction must have exactly 1 operand");
			if (getOperandCount() > 0) {
				oss << "loop_begin %";
				if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);
			}
		}
		break;

		case IrOpcode::LoopEnd:
		{
			// loop_end %loop_label
			assert(getOperandCount() == 1 && "LoopEnd instruction must have exactly 1 operand");
			if (getOperandCount() > 0) {
				oss << "loop_end %";
				if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);
			}
		}
		break;

		case IrOpcode::Break:
		{
			// br label %break_label
			assert(getOperandCount() == 1 && "Break instruction must have exactly 1 operand");
			if (getOperandCount() > 0) {
				oss << "br label %";
				if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);
			}
		}
		break;

		case IrOpcode::Continue:
		{
			// br label %continue_label
			assert(getOperandCount() == 1 && "Continue instruction must have exactly 1 operand");
			if (getOperandCount() > 0) {
				oss << "br label %";
				if (isOperandType<std::string_view>(0))
					oss << getOperandAs<std::string_view>(0);
			}
		}
		break;

		default:
			break;
		}

		return oss.str();
	}

private:
	IrOpcode opcode_;
	std::vector<IrOperand> operands_;
	uint32_t line_number_;
};

class Ir {
public:
	void addInstruction(const IrInstruction& instruction) {
		instructions.push_back(instruction);
	}
	void addInstruction(IrOpcode&& opcode,
		std::vector<IrOperand>&& operands = {}) {
		instructions.emplace_back(opcode, std::move(operands));
	}
	void addInstruction(IrOpcode&& opcode,
		std::vector<IrOperand>&& operands, uint32_t line_number) {
		instructions.emplace_back(opcode, std::move(operands), line_number);
	}
	const std::vector<IrInstruction>& getInstructions() const {
		return instructions;
	}

private:
	std::vector<IrInstruction> instructions;
};
