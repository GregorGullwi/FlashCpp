#pragma once

#include <string>
#include <numeric>
#include <vector>
#include <array>
#include <variant>
#include <string_view>
#include <assert.h>
#include <unordered_map>

#include "IRTypes.h"
#include "ObjFileWriter.h"

// Win64 calling convention register mapping
constexpr std::array<X64Register, 4> INT_PARAM_REGS = {
	X64Register::RCX,  // First integer/pointer argument
	X64Register::RDX,  // Second integer/pointer argument
	X64Register::R8,   // Third integer/pointer argument
	X64Register::R9    // Fourth integer/pointer argument
};

constexpr std::array<X64Register, 4> FLOAT_PARAM_REGS = {
	X64Register::XMM0, // First floating point argument
	X64Register::XMM1, // Second floating point argument
	X64Register::XMM2, // Third floating point argument
	X64Register::XMM3  // Fourth floating point argument
};

struct RegisterAllocator
{
	static constexpr uint8_t REGISTER_COUNT = static_cast<uint8_t>(X64Register::Count);
	struct AllocatedRegister {
		X64Register reg;
		bool isAllocated = false;
	};
	std::array<AllocatedRegister, REGISTER_COUNT> registers;

	RegisterAllocator() {
		for (size_t i = 0; i < REGISTER_COUNT; ++i) {
			registers[i].reg = static_cast<X64Register>(i);
		}
		registers[0].isAllocated = true;	// assume RAX is always allocated
	}

	void reset() {
		for (auto& reg : registers) {
			reg.isAllocated = false;
		}
	}

	X64Register allocate() {
		for (auto& reg : registers) {
			if (!reg.isAllocated) {
				reg.isAllocated = true;
				return reg.reg;
			}
		}
		throw std::runtime_error("No registers available");
	}

	bool is_allocated(X64Register reg) const {
		return registers[static_cast<size_t>(reg)].isAllocated;
	}

	void reserve_arguments(size_t count) {
		for (size_t i = 0; i < count && i < INT_PARAM_REGS.size(); ++i) {
			registers[static_cast<int>(INT_PARAM_REGS[i])].isAllocated = true;
		}
	}

	struct OpCodeWithSize {
		std::array<uint8_t, 4> op_codes;
		size_t size_in_bytes = 0;
	};
	OpCodeWithSize get_reg_reg_move_op_code(X64Register dst_reg, X64Register src_reg, size_t size_in_bytes) {
		OpCodeWithSize result;
		/*if (dst_reg == src_reg) {	// removed for now, since this is an optimization
			return result;
		}*/

		assert(size_in_bytes >= 1 && size_in_bytes <= 8);

		// For 64-bit moves, we need the REX prefix (0x48)
		if (size_in_bytes == 8) {
			result.op_codes[0] = 0x48;
			result.op_codes[1] = 0x89;  // MOV r64, r64
			result.op_codes[2] = 0xC0 + (static_cast<uint8_t>(dst_reg) << 3) + static_cast<uint8_t>(src_reg);
			result.size_in_bytes = 3;
		}
		// For 32-bit moves, we don't need REX prefix
		else if (size_in_bytes == 4) {
			result.op_codes[0] = 0x89;  // MOV r32, r32
			result.op_codes[1] = 0xC0 + (static_cast<uint8_t>(dst_reg) << 3) + static_cast<uint8_t>(src_reg);
			result.size_in_bytes = 2;
		}
		// For 16-bit moves, we need the 66 prefix
		else if (size_in_bytes == 2) {
			result.op_codes[0] = 0x66;
			result.op_codes[1] = 0x89;  // MOV r16, r16
			result.op_codes[2] = 0xC0 + (static_cast<uint8_t>(dst_reg) << 3) + static_cast<uint8_t>(src_reg);
			result.size_in_bytes = 3;
		}
		// For 8-bit moves, we need special handling for high registers
		else if (size_in_bytes == 1) {
			result.op_codes[0] = 0x88;  // MOV r8, r8
			result.op_codes[1] = 0xC0 + (static_cast<uint8_t>(dst_reg) << 3) + static_cast<uint8_t>(src_reg);
			result.size_in_bytes = 2;
		}

		return result;
	}
};

// Helper function to get register code for Win64 calling convention
static uint8_t getWin64RegCode(int paramIndex) {
	switch (paramIndex) {
		case 0:
			return 0x4C;  // RCX
		case 1:
			return 0x54;  // RDX
		case 2:
			return 0x44;  // R8
		case 3:
			return 0x4C;  // R9
		default:
			return 0x4C;  // Default to RCX for safety
	}
}

template<class TWriterClass = ObjectFileWriter>
class IrToObjConverter {
public:
	IrToObjConverter() = default;

	void convert(const Ir& ir, const char* filename) {

		for (const auto& instruction : ir.getInstructions()) {
			switch (instruction.getOpcode()) {
			case IrOpcode::FunctionDecl:
				handleFunctionDecl(instruction);
				break;
			case IrOpcode::Return:
				handleReturn(instruction);
				break;
			case IrOpcode::FunctionCall:
				handleFunctionCall(instruction);
				break;
			case IrOpcode::StackAlloc:
				handleStackAlloc(instruction);
				break;
			case IrOpcode::Store:
				handleStore(instruction);
				break;
			case IrOpcode::Add:
				handleAdd(instruction);
				break;
			case IrOpcode::Subtract:
				handleSubtract(instruction);
				break;
			case IrOpcode::Multiply:
				handleMultiply(instruction);
				break;
			case IrOpcode::Divide:
				handleDivide(instruction);
				break;
			default:
				assert(false && "Not implemented yet");
				break;
			}
		}
		finalizeSections();
		writer.write(filename);
	}

private:
	void handleFunctionCall(const IrInstruction& instruction) {
		// Function call should have at least 2 operands (return var and function name)
		assert(instruction.getOperandCount() >= 2);

		// First, handle any arguments by moving them to the correct registers
		// In x64 calling convention, first 4 args go to RCX, RDX, R8, R9
		const size_t first_arg_index = 2; // Start after return var and function name
		const size_t arg_stride = 3; // type, size, value
		const size_t num_args = (instruction.getOperandCount() - first_arg_index) / arg_stride;
		for (size_t i = 0; i < num_args; ++i) {
			X64Register argReg = INT_PARAM_REGS[i];

			size_t argIndex = first_arg_index + i * arg_stride;
			if (instruction.isOperandType<Type>(argIndex)) {
				Type type = instruction.getOperandAs<Type>(argIndex);
				int size = instruction.getOperandAs<int>(argIndex + 1);

				// Handle immediate values
				if (instruction.isOperandType<unsigned long long>(argIndex + 2)) {
					unsigned long long value = instruction.getOperandAs<unsigned long long>(argIndex + 2);

					switch (type) {
						case Type::Int:
							if (size != 32) {
								assert(false && "Int type must be 32 bits");
								return;
							}
							{
								std::array<uint8_t, 5> movInst = { 0xB8, 0, 0, 0, 0 };
								movInst[0] = 0xB8 + static_cast<uint8_t>(argReg);
								for (size_t j = 0; j < 4; ++j) {
									movInst[j + 1] = (value >> (8 * j)) & 0xFF;
								}
								textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
							}
							break;
						case Type::Char:
							if (size != 8) {
								assert(false && "Char type must be 8 bits");
								return;
							}
							{
								std::array<uint8_t, 3> movInst = { 0xB0, 0, 0 };
								movInst[0] = 0xB0 + static_cast<uint8_t>(argReg);
								movInst[1] = value & 0xFF;
								textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.begin() + 2);
							}
							break;
						case Type::Void:
							assert(false && "Void type not allowed as argument");
							return;
						default:
							assert(false && "Unsupported type");
							return;
					}
				}
				// Handle local variables
				else if (instruction.isOperandType<std::string_view>(argIndex + 2)) {
					auto var_name = instruction.getOperandAs<std::string_view>(argIndex + 2);
					
					// Find the variable's stack offset
					const StackVariableScope& current_scope = variable_scopes.back();
					auto it = current_scope.identifier_offset.find(var_name);
					if (it != current_scope.identifier_offset.end()) {
						// mov reg, [rsp+offset]
						std::array<uint8_t, 5> movInst = { 0x48, 0x8B, 0x44, 0x24, 0x00 };
						movInst[2] = 0x84 + static_cast<uint8_t>(argReg);
						movInst[4] = static_cast<uint8_t>(it->second);
						textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
					}
				}
			}
		}

		// call [function name] instruction is 5 bytes
		auto function_name = instruction.getOperandAs<std::string_view>(1);
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());

		writer.add_relocation(textSectionData.size() - 4, function_name);
	}

	void handleFunctionDecl(const IrInstruction& instruction) {
		auto func_name = instruction.getOperandAs<std::string_view>(2);
		
		// align the function to 16 bytes
		static constexpr char nop = static_cast<char>(0x90);
		const uint32_t nop_count = 16 - (textSectionData.size() % 16);
		if (nop_count < 16)
			textSectionData.insert(textSectionData.end(), nop_count, nop);
		uint32_t func_offset = static_cast<uint32_t>(textSectionData.size());

		writer.add_function_symbol(std::string(func_name), func_offset);
		functionSymbols[func_name] = func_offset;

		// Create a new function scope
		regAlloc.reset();
		regAlloc.reserve_arguments(instruction.getOperandCount() - 2);

		// MSVC-style prologue: push rbp; mov rbp, rsp; sub rsp, 0x20 (32 bytes shadow space)
		textSectionData.push_back(0x55); // push rbp
		textSectionData.push_back(0x48); textSectionData.push_back(0x8B); textSectionData.push_back(0xEC); // mov rbp, rsp
		textSectionData.push_back(0x48); textSectionData.push_back(0x83); textSectionData.push_back(0xEC); textSectionData.push_back(0x20); // sub rsp, 0x20

		variable_scopes.emplace_back();

		// Handle parameters
		constexpr int numOperandsPerArg = 3;
		size_t operandIndex = 3;  // Start after return type, size, and function name
		while (operandIndex + numOperandsPerArg <= instruction.getOperandCount()) {  // Need at least type, size, and name
			auto param_type = instruction.getOperandAs<Type>(operandIndex);
			auto param_size = instruction.getOperandAs<int>(operandIndex + 1);
			auto param_name = instruction.getOperandAs<std::string_view>(operandIndex + 2);

			// Store parameter at [rsp+8] for first parameter, [rsp+16] for second, etc.
			int paramNumber = (operandIndex / numOperandsPerArg) - 1;
			int offset = (paramNumber + 1) * 8;
			if (paramNumber <= 4) {
				// Get the register for this parameter based on Win64 calling convention
				uint8_t reg_code = getWin64RegCode(paramNumber);
				textSectionData.push_back(0x48); textSectionData.push_back(0x89); textSectionData.push_back(reg_code); textSectionData.push_back(0x24); textSectionData.push_back(offset);
			}

			variable_scopes.back().identifier_offset[param_name] = offset;
			operandIndex += numOperandsPerArg;  // Skip type, size, and name
		}
	}

	void handleReturn(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() >= 3);
		if (instruction.isOperandType<unsigned long long>(2)) {
			unsigned long long returnValue = instruction.getOperandAs<unsigned long long>(2);
			if (returnValue > std::numeric_limits<uint32_t>::max()) {
				throw std::runtime_error("Return value exceeds 32-bit limit");
			}

			// mov eax, immediate instruction has a fixed size of 5 bytes
			std::array<uint8_t, 5> movEaxImmedInst = { 0xB8, 0, 0, 0, 0 };

			// Fill in the return value
			for (size_t i = 0; i < 4; ++i) {
				movEaxImmedInst[i + 1] = (returnValue >> (8 * i)) & 0xFF;
			}

			textSectionData.insert(textSectionData.end(), movEaxImmedInst.begin(), movEaxImmedInst.end());
		}
		else if (instruction.isOperandType<TempVar>(2)) {
			// Do register allocation
			auto size_it_bits = instruction.getOperandAs<int>(1);
			auto return_var = instruction.getOperandAs<TempVar>(2);
			
			// For function call results, we need to move from the result register to RAX
			if (return_var.index > 0) {  // If this is a function call result
				// mov eax, eax (no-op since function result is already in eax)
				// We don't need to do anything since the function call result is already in eax
			} else {
				// For other cases, allocate a register and move the value
				auto src_reg = regAlloc.allocate();
				auto dst_reg = X64Register::RAX;	// all return values are stored in RAX
				if (src_reg != dst_reg) {
					auto op_codes = regAlloc.get_reg_reg_move_op_code(dst_reg, src_reg, size_it_bits / 8);
					if (op_codes.size_in_bytes > 0)
					{
						textSectionData.insert(textSectionData.end(), op_codes.op_codes.begin(), op_codes.op_codes.begin() + op_codes.size_in_bytes);
					}
				}
			}
		}
		else if (instruction.isOperandType<std::string_view>(2)) {
			// Handle local variable access
			auto var_name = instruction.getOperandAs<std::string_view>(2);
			auto size_in_bits = instruction.getOperandAs<int>(1);

			// Find the variable's stack offset
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.identifier_offset.find(var_name);
			if (it != current_scope.identifier_offset.end()) {
				// mov eax, [rsp+8]
				std::array<uint8_t, 5> movInst = { 0x48, 0x8B, 0x44, 0x24, 0x08 };
				textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
			}
		}

		// Function epilogue
		// mov rsp, rbp (restore stack pointer)
		textSectionData.push_back(0x48);
		textSectionData.push_back(0x89);
		textSectionData.push_back(0xEC);

		// pop rbp (restore caller's base pointer)
		textSectionData.push_back(0x5D);

		// ret (return to caller)
		textSectionData.push_back(0xC3);

		variable_scopes.pop_back();
	}

	void handleStackAlloc(const IrInstruction& instruction) {
		// Get the size of the allocation
		auto sizeInBytes = instruction.getOperandAs<int>(1) / 8;

		// Ensure the stack remains aligned to 16 bytes
		sizeInBytes = (sizeInBytes + 15) & -16;

		// Generate the opcode for `sub rsp, imm32`
		std::array<uint8_t, 7> subRspInst = { 0x48, 0x81, 0xEC };
		std::memcpy(subRspInst.data() + 3, &sizeInBytes, sizeof(sizeInBytes));

		// Add the instruction to the .text section
		textSectionData.insert(textSectionData.end(), subRspInst.begin(), subRspInst.end());

		// Add the identifier and its stack offset to the current scope
		StackVariableScope& current_scope = variable_scopes.back();
		current_scope.identifier_offset[instruction.getOperandAs<std::string_view>(2)] = current_scope.current_stack_offset;
		current_scope.current_stack_offset += sizeInBytes;
	}

	void handleStore(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() >= 4);  // type, size, dest, src

		auto var_name = instruction.getOperandAs<std::string_view>(2);
		auto src_reg = static_cast<X64Register>(instruction.getOperandAs<int>(3));

		// Find the variable's stack offset
		const StackVariableScope& current_scope = variable_scopes.back();
		auto it = current_scope.identifier_offset.find(var_name);
		if (it != current_scope.identifier_offset.end()) {
			// mov [rbp + offset], reg
			std::array<uint8_t, 7> movInst = { 0x48, 0x89, 0x85, 0, 0, 0, 0 };
			int32_t offset = it->second;
			std::memcpy(movInst.data() + 3, &offset, sizeof(offset));
			movInst[1] = 0x89;  // mov [mem], reg
			movInst[2] = 0x85;  // [rbp + disp32]
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		}
	}

	void handleAdd(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() == 7 && "Add instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val");

		// Get type and size (from LHS)
		auto type = instruction.getOperandAs<Type>(1);
		auto size = instruction.getOperandAs<int>(2);

		// For now, we only support integer addition
		if (type != Type::Int) {
			assert(false && "Only integer addition is supported");
			return;
		}

		// Get source and destination from IR
		auto result_var = instruction.getOperand(0);
		auto lhs_val = instruction.getOperandAs<std::string_view>(3);
		auto rhs_val = instruction.getOperandAs<std::string_view>(6);

		// Get offsets for variables
		int result_offset;
		bool is_register = std::holds_alternative<TempVar>(result_var);
		if (is_register) {
			// It's a register index
			result_offset = std::get<TempVar>(result_var).index;
		} else {
			// It's a variable name
			result_offset = variable_scopes.back().identifier_offset[std::get<std::string_view>(result_var)];
		}

		// Load LHS into rcx from [rsp+8]
		std::array<uint8_t, 5> loadLhsInst = { 0x48, 0x8B, 0x4C, 0x24, 0x08 };  // mov rcx, [rsp+8]
		textSectionData.insert(textSectionData.end(), loadLhsInst.begin(), loadLhsInst.end());

		// Load RHS into rdx from [rsp+16]
		std::array<uint8_t, 5> loadRhsInst = { 0x48, 0x8B, 0x54, 0x24, 0x10 };  // mov rdx, [rsp+16]
		textSectionData.insert(textSectionData.end(), loadRhsInst.begin(), loadRhsInst.end());

		// Add rcx and rdx, store in rcx
		std::array<uint8_t, 3> addInst = { 0x48, 0x01, 0xD1 };  // add rcx, rdx
		textSectionData.insert(textSectionData.end(), addInst.begin(), addInst.end());

		if (is_register) {
			// Move result to the correct register
			X64Register reg = static_cast<X64Register>(result_offset);
			std::array<uint8_t, 3> movRegInst = { 0x48, 0x89, 0xC0 };  // mov rax, rcx
			movRegInst[2] = 0xC0 + (static_cast<uint8_t>(reg) << 3);  // Set destination register
			textSectionData.insert(textSectionData.end(), movRegInst.begin(), movRegInst.end());
		} else {
			// Store result in memory
			std::array<uint8_t, 7> storeResultInst = { 0x48, 0x89, 0x8D, 0x00, 0x00, 0x00, 0x00 };  // mov [rbp+offset], rcx
			storeResultInst[3] = static_cast<uint8_t>(result_offset & 0xFF);
			storeResultInst[4] = static_cast<uint8_t>((result_offset >> 8) & 0xFF);
			storeResultInst[5] = static_cast<uint8_t>((result_offset >> 16) & 0xFF);
			storeResultInst[6] = static_cast<uint8_t>((result_offset >> 24) & 0xFF);
			textSectionData.insert(textSectionData.end(), storeResultInst.begin(), storeResultInst.end());
		}
	}

	void handleSubtract(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() == 7 && "Subtract instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val");

		// Get type and size (from LHS)
		auto type = instruction.getOperandAs<Type>(1);
		auto sizeInBits = instruction.getOperandAs<int>(2);

		// For now, we only support integer subtraction
		if (type != Type::Int) {
			assert(false && "Only integer subtraction is supported");
			return;
		}

		// Allocate registers for operands and result
		auto result_reg = regAlloc.allocate();
		auto lhs_reg = regAlloc.allocate();
		auto rhs_reg = regAlloc.allocate();

		// Load parameters into registers
		// mov lhs_reg, [rsp+8] for first parameter
		std::array<uint8_t, 5> movLhsInst = { 0x48, 0x8B, 0x44, 0x24, 0x08 };
		movLhsInst[2] = 0x84 + static_cast<uint8_t>(lhs_reg);
		textSectionData.insert(textSectionData.end(), movLhsInst.begin(), movLhsInst.end());

		// mov rhs_reg, [rsp+16] for second parameter
		std::array<uint8_t, 5> movRhsInst = { 0x48, 0x8B, 0x54, 0x24, 0x10 };
		movRhsInst[2] = 0x84 + static_cast<uint8_t>(rhs_reg);
		textSectionData.insert(textSectionData.end(), movRhsInst.begin(), movRhsInst.end());

		// mov result_reg, lhs_reg
		auto movResultLhs = regAlloc.get_reg_reg_move_op_code(result_reg, lhs_reg, sizeInBits / 8);
		textSectionData.insert(textSectionData.end(), movResultLhs.op_codes.begin(), movResultLhs.op_codes.begin() + movResultLhs.size_in_bytes);

		// sub result_reg, rhs_reg
		std::array<uint8_t, 3> subInst = { 0x48, 0x29, 0xC0 };
		subInst[2] = 0xC0 + (static_cast<uint8_t>(result_reg) << 3) + static_cast<uint8_t>(rhs_reg);
		textSectionData.insert(textSectionData.end(), subInst.begin(), subInst.end());

		// Store result in shadow space
		std::array<uint8_t, 5> storeResultInst = { 0x48, 0x89, 0x44, 0x24, 0x08 };
		storeResultInst[2] = 0x84 + static_cast<uint8_t>(result_reg);
		textSectionData.insert(textSectionData.end(), storeResultInst.begin(), storeResultInst.end());
	}

	void handleMultiply(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() == 7 && "Multiply instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val");

		// Get type and size (from LHS)
		auto type = instruction.getOperandAs<Type>(1);
		auto sizeInBits = instruction.getOperandAs<int>(2);

		// For now, we only support integer multiplication
		if (type != Type::Int) {
			assert(false && "Only integer multiplication is supported");
			return;
		}

		// Allocate registers for operands and result
		auto result_reg = regAlloc.allocate();
		auto lhs_reg = regAlloc.allocate();
		auto rhs_reg = regAlloc.allocate();

		// Load parameters into registers
		// mov lhs_reg, [rsp+8] for first parameter
		std::array<uint8_t, 5> movLhsInst = { 0x48, 0x8B, 0x44, 0x24, 0x08 };
		movLhsInst[2] = 0x84 + static_cast<uint8_t>(lhs_reg);
		textSectionData.insert(textSectionData.end(), movLhsInst.begin(), movLhsInst.end());

		// mov rhs_reg, [rsp+16] for second parameter
		std::array<uint8_t, 5> movRhsInst = { 0x48, 0x8B, 0x54, 0x24, 0x10 };
		movRhsInst[2] = 0x84 + static_cast<uint8_t>(rhs_reg);
		textSectionData.insert(textSectionData.end(), movRhsInst.begin(), movRhsInst.end());

		// mov result_reg, lhs_reg
		auto movResultLhs = regAlloc.get_reg_reg_move_op_code(result_reg, lhs_reg, sizeInBits / 8);
		textSectionData.insert(textSectionData.end(), movResultLhs.op_codes.begin(), movResultLhs.op_codes.begin() + movResultLhs.size_in_bytes);

		// imul result_reg, rhs_reg
		std::array<uint8_t, 3> mulInst = { 0x48, 0x0F, 0xAF };
		mulInst[2] = 0xC0 + (static_cast<uint8_t>(result_reg) << 3) + static_cast<uint8_t>(rhs_reg);
		textSectionData.insert(textSectionData.end(), mulInst.begin(), mulInst.end());

		// Store result in shadow space
		std::array<uint8_t, 5> storeResultInst = { 0x48, 0x89, 0x44, 0x24, 0x08 };
		storeResultInst[2] = 0x84 + static_cast<uint8_t>(result_reg);
		textSectionData.insert(textSectionData.end(), storeResultInst.begin(), storeResultInst.end());
	}

	void handleDivide(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() == 7 && "Divide instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val");

		// Get type and size (from LHS)
		auto type = instruction.getOperandAs<Type>(1);
		auto sizeInBits = instruction.getOperandAs<int>(2);

		// For now, we only support integer division
		if (type != Type::Int) {
			assert(false && "Only integer division is supported");
			return;
		}

		// Allocate registers for operands and result
		auto result_reg = regAlloc.allocate();
		auto lhs_reg = regAlloc.allocate();
		auto rhs_reg = regAlloc.allocate();

		// Load parameters into registers
		// mov lhs_reg, [rsp+8] for first parameter (dividend)
		std::array<uint8_t, 5> movLhsInst = { 0x48, 0x8B, 0x44, 0x24, 0x08 };
		movLhsInst[2] = 0x84 + static_cast<uint8_t>(lhs_reg);
		textSectionData.insert(textSectionData.end(), movLhsInst.begin(), movLhsInst.end());

		// mov rhs_reg, [rsp+16] for second parameter (divisor)
		std::array<uint8_t, 5> movRhsInst = { 0x48, 0x8B, 0x54, 0x24, 0x10 };
		movRhsInst[2] = 0x84 + static_cast<uint8_t>(rhs_reg);
		textSectionData.insert(textSectionData.end(), movRhsInst.begin(), movRhsInst.end());

		// mov rax, lhs_reg (dividend must be in rax)
		auto movRaxLhs = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, lhs_reg, sizeInBits / 8);
		textSectionData.insert(textSectionData.end(), movRaxLhs.op_codes.begin(), movRaxLhs.op_codes.begin() + movRaxLhs.size_in_bytes);

		// cdq - sign extend eax into edx:eax
		std::array<uint8_t, 1> cdqInst = { 0x99 };
		textSectionData.insert(textSectionData.end(), cdqInst.begin(), cdqInst.end());

		// idiv rhs_reg
		std::array<uint8_t, 2> divInst = { 0x48, 0xF7 };
		textSectionData.insert(textSectionData.end(), divInst.begin(), divInst.end());
		textSectionData.push_back(0xF8 + static_cast<uint8_t>(rhs_reg));

		// mov result_reg, rax (quotient is in rax)
		auto movResultRax = regAlloc.get_reg_reg_move_op_code(result_reg, X64Register::RAX, 8);
		textSectionData.insert(textSectionData.end(), movResultRax.op_codes.begin(), movResultRax.op_codes.begin() + movResultRax.size_in_bytes);

		// Store result in shadow space
		std::array<uint8_t, 5> storeResultInst = { 0x48, 0x89, 0x44, 0x24, 0x08 };
		storeResultInst[2] = 0x84 + static_cast<uint8_t>(result_reg);
		textSectionData.insert(textSectionData.end(), storeResultInst.begin(), storeResultInst.end());
	}

	void finalizeSections() {
		writer.add_data(textSectionData, SectionType::TEXT);
	}

	TWriterClass writer;
	std::vector<char> textSectionData;
	std::unordered_map<std::string_view, uint32_t> functionSymbols;
	RegisterAllocator regAlloc;

	struct StackVariableScope
	{
		int current_stack_offset = 0;
		std::unordered_map<std::string_view, int> identifier_offset;
	};
	std::vector<StackVariableScope> variable_scopes;
};
