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
	ShiftLeft,
	ShiftRight,
	Return,
	FunctionDecl,
	FunctionCall,
	Assignment,
	StackAlloc,
	Store,
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
		: opcode_(opcode), operands_(std::move(operands)) {}

	IrOpcode getOpcode() const { return opcode_; }
	size_t getOperandCount() const { return operands_.size(); }

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
						oss << getOperandAs<std::string_view>(3);

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
						oss << getOperandAs<std::string_view>(3);

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

		default:
			break;
		}

		return oss.str();
	}

private:
	IrOpcode opcode_;
	std::vector<IrOperand> operands_;
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
	const std::vector<IrInstruction>& getInstructions() const {
		return instructions;
	}

private:
	std::vector<IrInstruction> instructions;
};
