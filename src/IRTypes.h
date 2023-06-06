#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <any>
#include <sstream>

#include "AstNodeTypes.h"

enum class IrOpcode {
	Add,
	Sub,
	Return,
	FunctionDecl,
	FunctionCall,
	Assignment,
};

struct TempVar
{
	TempVar next() {
		return { ++index };
	}
	size_t index;
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

		const Type type = getOperandAs<Type>(0);
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
		case IrOpcode::Return:
		{
			// ret [Type][SizeInBits] [Value]
			oss << "ret ";

			if (getOperandCount() == 3) {
				oss << getOperandAsTypeString(0) << getOperandAs<int>(1) << " ";

				if (isOperandType<unsigned long long>(2))
					oss << getOperandAs<unsigned long long>(2);
				else if (isOperandType<TempVar>(2))
					oss << '%' << getOperandAs<TempVar>(2).index;
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
			oss << '%';
			if (isOperandType<TempVar>(0))
				oss << getOperandAs<TempVar>(0).index;
			else if (isOperandType<std::string_view>(0))
				oss << getOperandAs<std::string_view>(0);

			oss << " = call ";
			oss << "@" << getOperandAs<std::string_view>(1) << "()";

			const size_t funcSymbolIndex = getOperandCount() - 1;
			for (size_t i = 2; i < funcSymbolIndex; i += 2) {
				oss << getOperandAsTypeString(i) << getOperandAs<int>(i + 1) << " ";
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
