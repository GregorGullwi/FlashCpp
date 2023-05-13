#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

enum class IrOpcode {
  Add,
  Sub,
  Return,
  Function,
};

using IrOperand = std::variant<int, double, bool, char, std::string>;

class IrInstruction {
public:
  IrInstruction(IrOpcode opcode, std::optional<IrOperand> op1 = std::nullopt,
                std::optional<IrOperand> op2 = std::nullopt)
      : opcode_(opcode), operand1_(std::move(op1)), operand2_(std::move(op2)) {}

  IrOpcode getOpcode() const { return opcode_; }
  const std::optional<IrOperand> &getFirstOperand() const { return operand1_; }
  const std::optional<IrOperand> &getSecondOperand() const { return operand2_; }

private:
  IrOpcode opcode_;
  std::optional<IrOperand> operand1_;
  std::optional<IrOperand> operand2_;
};

class Ir {
public:
  void addInstruction(const IrInstruction &instruction) {
    instructions.push_back(instruction);
  }
  void addInstruction(IrOpcode &&opcode,
                      std::optional<IrOperand> &&op1 = std::nullopt,
                      std::optional<IrOperand> &&op2 = std::nullopt) {
    instructions.emplace_back(opcode, op1, op2);
  }
  const std::vector<IrInstruction> &getInstructions() const {
    return instructions;
  }

private:
  std::vector<IrInstruction> instructions;
};