#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <any>

enum class IrOpcode {
  Add,
  Sub,
  Return,
  Function,
};

using IrOperand = std::variant<int, double, bool, char, std::string, std::string_view>;

class IrInstruction {
public:
  IrInstruction(IrOpcode opcode, std::vector<IrOperand>&& operands)
      : opcode_(opcode), operands_(std::move(operands)) {}

  IrOpcode getOpcode() const { return opcode_; }
	size_t getOperandCount() const { return operands_.size(); }
	
  std::optional<IrOperand> getOperand(size_t index) const {
	  return index < operands_.size() ? std::optional<IrOperand>{ operands_[index] } : std::optional<IrOperand>{};
  }

private:
  IrOpcode opcode_;
  std::vector<IrOperand> operands_;
};

class Ir {
public:
  void addInstruction(const IrInstruction &instruction) {
    instructions.push_back(instruction);
  }
  void addInstruction(IrOpcode &&opcode,
					  std::vector<IrOperand>&& operands = {}) {
    instructions.emplace_back(opcode, std::move(operands));
  }
  const std::vector<IrInstruction> &getInstructions() const {
    return instructions;
  }

private:
  std::vector<IrInstruction> instructions;
};