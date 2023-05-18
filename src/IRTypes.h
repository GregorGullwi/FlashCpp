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
};

using IrOperand = std::variant<int, unsigned long long, double, bool, char, std::string, std::string_view, Type>;

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

	std::string getReadableString() const {
		std::ostringstream oss{};

		switch (opcode_) {
		case IrOpcode::Return:
		{
			// [Type][SizeInBits] [Value]
			oss << "ret ";
			oss << getOperandAsTypeString(0) << getOperandAs<int>(1) << " ";
			oss << getOperandAs<unsigned long long>(2);
		}
		break;

		case IrOpcode::FunctionDecl:
		{
			// [Type][SizeInBits] [Name]
			oss << "define ";
			oss << getOperandAsTypeString(0) << getOperandAs<int>(1) << " ";
			oss << getOperandAs<std::string_view>(2);
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
