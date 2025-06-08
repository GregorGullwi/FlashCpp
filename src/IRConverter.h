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

// Maximum possible size for 'mov destination_register, [rsp + offset]' instruction:
// REX (1 byte) + Opcode (1 byte) + ModR/M (1 byte) + SIB (1 byte) + Disp32 (4 bytes) = 8 bytes
constexpr size_t MAX_MOV_INSTRUCTION_SIZE = 8;

struct OpCodeWithSize {
	std::array<uint8_t, MAX_MOV_INSTRUCTION_SIZE> op_codes;
	size_t size_in_bytes = 0;
};

/**
 * @brief Generates x86-64 binary opcodes for 'mov destination_register, [rsp + offset]'.
 *
 * This function creates the byte sequence for moving a 64-bit value from a
 * stack-relative address (RSP + offset) into a general-purpose 64-bit register.
 * It handles REX prefixes, ModR/M, SIB, and 8-bit/32-bit displacements.
 * The generated opcodes are placed into a stack-allocated `std::array` within
 * the returned `OpCodeWithSize` struct.
 *
 * @param destinationRegister The target register (e.g., RAX, RCX, R8).
 * @param offset The signed byte offset from RSP. Can be 0, 8-bit, or 32-bit.
 * @return An `OpCodeWithSize` struct containing a stack-allocated `std::array`
 *         of `uint8_t` and the actual number of bytes generated.
 */
OpCodeWithSize generateMovFromStack(X64Register destinationRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0; // Initialize count to 0

	// Use a pointer to fill the array sequentially.
	uint8_t* current_byte_ptr = result.op_codes.data();

	// --- REX Prefix (0x40 | W | R | X | B) ---
	uint8_t rex_prefix = 0x48; // Base: REX.W = 0100_1000b

	// If destination register is R8-R15 (enum values >= 8), set REX.R bit.
	if (static_cast<uint8_t>(destinationRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= (1 << 2); // Set R bit (0b0100)
	}
	*current_byte_ptr++ = rex_prefix;
	result.size_in_bytes++;

	// --- Opcode for MOV r64, r/m64 ---
	*current_byte_ptr++ = 0x8B;
	result.size_in_bytes++;

	// --- ModR/M byte (Mod | Reg | R/M) ---
	uint8_t modrm_byte;
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(destinationRegister) & 0x07;

	uint8_t mod_field;
	if (offset == 0) {
		mod_field = 0x00; // Mod = 00b (no displacement)
	}
	else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01; // Mod = 01b (8-bit displacement)
	}
	else {
		mod_field = 0x02; // Mod = 10b (32-bit displacement)
	}

	modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x04; // 0x04 for R/M indicates SIB
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- SIB byte (Scale | Index | Base) ---
	uint8_t sib_byte = 0x24; // 00_100_100b
	*current_byte_ptr++ = sib_byte;
	result.size_in_bytes++;

	// --- Displacement ---
	if (offset != 0) {
		if (offset >= -128 && offset <= 127) {
			// 8-bit signed displacement
			*current_byte_ptr++ = static_cast<uint8_t>(offset);
			result.size_in_bytes++;
		}
		else {
			// 32-bit signed displacement (little-endian format)
			*current_byte_ptr++ = static_cast<uint8_t>(offset & 0xFF);
			*current_byte_ptr++ = static_cast<uint8_t>((offset >> 8) & 0xFF);
			*current_byte_ptr++ = static_cast<uint8_t>((offset >> 16) & 0xFF);
			*current_byte_ptr++ = static_cast<uint8_t>((offset >> 24) & 0xFF);
			result.size_in_bytes += 4;
		}
	}

	return result;
}

/**
 * @brief Generates x86-64 binary opcodes for 'mov [rsp + offset], source_register'.
 *
 * This function creates the byte sequence for moving a 64-bit value from a
 * general-purpose 64-bit register to a stack-relative address (RSP + offset).
 * It handles REX prefixes, ModR/M, SIB, and 8-bit/32-bit displacements.
 * The generated opcodes are placed into a stack-allocated `std::array` within
 * the returned `OpCodeWithSize` struct.
 *
 * @param sourceRegister The source register (e.g., RAX, RCX, R8).
 * @param offset The signed byte offset from RSP. Can be 0, 8-bit, or 32-bit.
 * @return An `OpCodeWithSize` struct containing a stack-allocated `std::array`
 *         of `uint8_t` and the actual number of bytes generated.
 */
OpCodeWithSize generateMovToStack(X64Register sourceRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;

	uint8_t* current_byte_ptr = result.op_codes.data();

	// --- REX Prefix (0x40 | W | R | X | B) ---
	uint8_t rex_prefix = 0x48; // Base: REX.W = 0100_1000b

	// If source register is R8-R15 (enum values >= 8), set REX.B bit.
	if (static_cast<uint8_t>(sourceRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= (1 << 0); // Set B bit (0b0001)
	}
	*current_byte_ptr++ = rex_prefix;
	result.size_in_bytes++;

	// --- Opcode for MOV r/m64, r64 ---
	*current_byte_ptr++ = 0x89;
	result.size_in_bytes++;

	// --- ModR/M byte (Mod | Reg | R/M) ---
	uint8_t modrm_byte;
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(sourceRegister) & 0x07;

	uint8_t mod_field;
	if (offset == 0) {
		mod_field = 0x00; // Mod = 00b (no displacement)
	} else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01; // Mod = 01b (8-bit displacement)
	} else {
		mod_field = 0x02; // Mod = 10b (32-bit displacement)
	}

	modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x04; // 0x04 for R/M indicates SIB
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- SIB byte (Scale | Index | Base) ---
	uint8_t sib_byte = 0x24; // 00_100_100b (Scale=0, Index=RSP(4), Base=RSP(4))
	*current_byte_ptr++ = sib_byte;
	result.size_in_bytes++;

	// --- Displacement ---
	if (offset != 0) {
		if (offset >= -128 && offset <= 127) {
			// 8-bit signed displacement
			*current_byte_ptr++ = static_cast<uint8_t>(offset);
			result.size_in_bytes++;
		} else {
			// 32-bit signed displacement (little-endian format)
			*current_byte_ptr++ = static_cast<uint8_t>(offset & 0xFF);
			*current_byte_ptr++ = static_cast<uint8_t>((offset >> 8) & 0xFF);
			*current_byte_ptr++ = static_cast<uint8_t>((offset >> 16) & 0xFF);
			*current_byte_ptr++ = static_cast<uint8_t>((offset >> 24) & 0xFF);
			result.size_in_bytes += 4;
		}
	}

	return result;
}

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
		registers[static_cast<int>(X64Register::RSP)].isAllocated = true;	// assume RSP is always allocated
		registers[static_cast<int>(X64Register::RBP)].isAllocated = true;	// assume RBP is always allocated
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

	void release(X64Register reg) {
		registers[static_cast<int>(reg)].isAllocated = false;
	}

	bool is_allocated(X64Register reg) const {
		return registers[static_cast<size_t>(reg)].isAllocated;
	}

	void reserve_arguments(size_t count) {
		for (size_t i = 0; i < count && i < INT_PARAM_REGS.size(); ++i) {
			registers[static_cast<int>(INT_PARAM_REGS[i])].isAllocated = true;
		}
	}

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
			// ModR/M: Mod=11 (register-to-register), Reg=src_reg, R/M=dst_reg
			result.op_codes[2] = 0xC0 + (static_cast<uint8_t>(src_reg) << 3) + static_cast<uint8_t>(dst_reg);
			result.size_in_bytes = 3;
		}
		// For 32-bit moves, we don't need REX prefix
		else if (size_in_bytes == 4) {
			result.op_codes[0] = 0x89;  // MOV r32, r32
			// ModR/M: Mod=11 (register-to-register), Reg=src_reg, R/M=dst_reg
			result.op_codes[1] = 0xC0 + (static_cast<uint8_t>(src_reg) << 3) + static_cast<uint8_t>(dst_reg);
			result.size_in_bytes = 2;
		}
		// For 16-bit moves, we need the 66 prefix
		else if (size_in_bytes == 2) {
			result.op_codes[0] = 0x66;
			result.op_codes[1] = 0x89;  // MOV r16, r16
			// ModR/M: Mod=11 (register-to-register), Reg=src_reg, R/M=dst_reg
			result.op_codes[2] = 0xC0 + (static_cast<uint8_t>(src_reg) << 3) + static_cast<uint8_t>(dst_reg);
			result.size_in_bytes = 3;
		}
		// For 8-bit moves, we need special handling for high registers
		else if (size_in_bytes == 1) {
			result.op_codes[0] = 0x88;  // MOV r8, r8
			// ModR/M: Mod=11 (register-to-register), Reg=src_reg, R/M=dst_reg
			result.op_codes[1] = 0xC0 + (static_cast<uint8_t>(src_reg) << 3) + static_cast<uint8_t>(dst_reg);
			result.size_in_bytes = 2;
		}

		return result;
	}
};

struct RegCode {
	uint8_t code;
	bool use_rex;
};

RegCode getWin64RegCode(int paramNumber) {
	switch (paramNumber) {
		case 0: return {0x4C, false};  // RCX
		case 1: return {0x54, false};  // RDX
		case 2: return {0x44, true};   // R8
		case 3: return {0x4C, true};   // R9
		default: return {0, false};
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
		assert(instruction.getOperandCount() >= 2 && "Function call must have at least 2 operands (result, function name)");

		// Get result variable and function name
		auto result_var = instruction.getOperand(0);
		auto funcName = instruction.getOperandAs<std::string_view>(1);

		// Get result offset
		int result_offset = 0;
		if (std::holds_alternative<TempVar>(result_var)) {
			result_offset = std::get<TempVar>(result_var).index;
		} else {
			result_offset = variable_scopes.back().identifier_offset[std::get<std::string_view>(result_var)];
		}

		// Process arguments (if any)
		const size_t first_arg_index = 2; // Start after result and function name
		const size_t arg_stride = 3; // type, size, value
		const size_t num_args = (instruction.getOperandCount() - first_arg_index) / arg_stride;
		for (size_t i = 0; i < num_args; ++i) {
			size_t argIndex = first_arg_index + i * arg_stride;
			Type argType = instruction.getOperandAs<Type>(argIndex);
			// int argSize = instruction.getOperandAs<int>(argIndex + 1); // unused for now
			auto argValue = instruction.getOperand(argIndex + 2); // could be immediate or variable

			if (argType != Type::Int) {
				assert(false && "Only integer arguments are supported");
				return;
			}

			// Determine the target register for the argument
			X64Register target_reg = INT_PARAM_REGS[i];

			// Construct the REX prefix based on target_reg for destination (Reg field).
			// REX.W is always needed for 64-bit operations (0x48).
			// REX.R (bit 2) is set if the target_reg is R8-R15 (as it appears in the Reg field of ModR/M or is directly encoded).
			uint8_t rex_prefix = 0x48;
			if (static_cast<uint8_t>(target_reg) >= static_cast<uint8_t>(X64Register::R8)) {
				rex_prefix |= (1 << 2); // Set REX.R
			}
			// Push the REX prefix. It might be modified later for REX.B or REX.X if needed.
			textSectionData.push_back(rex_prefix);

			if (std::holds_alternative<unsigned long long>(argValue)) {
				// Immediate value
				// MOV r64, imm64 (REX.W/B + Opcode B8+rd)
				if (static_cast<uint8_t>(target_reg) >= static_cast<uint8_t>(X64Register::R8)) {
					textSectionData.back() |= (1 << 0); // Set REX.B bit on the already pushed REX prefix (for rd in opcode)
				}
				textSectionData.push_back(0xB8 + (static_cast<uint8_t>(target_reg) & 0x07)); // Opcode B8 + lower 3 bits of target_reg
				unsigned long long value = std::get<unsigned long long>(argValue);
				for (size_t j = 0; j < 8; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(value & 0xFF));
					value >>= 8;
				}
			} else if (std::holds_alternative<std::string_view>(argValue)) {
				// Local variable
				std::string_view var_name = std::get<std::string_view>(argValue);
				int var_offset = variable_scopes.back().identifier_offset[var_name];
				// Load from stack: MOV r64, [rbp+offset] (REX.W/R + Opcode 8B + ModR/M + disp32)
				textSectionData.push_back(0x8B);  // MOV r64, r/m64
				// ModR/M: Mod=10 (disp32), Reg=target_reg_encoding, R/M=101 (RBP)
				textSectionData.push_back((0x02 << 6) | ((static_cast<uint8_t>(target_reg) & 0x07) << 3) | 0x05);
				
				// Displacement (32-bit)
				for (size_t j = 0; j < 4; ++j) {
					textSectionData.push_back(static_cast<uint8_t>((var_offset >> (8 * j)) & 0xFF));
				}
			} else if (std::holds_alternative<TempVar>(argValue)) {
				// Register value
				auto src_reg_temp = std::get<TempVar>(argValue);
				X64Register src_physical_reg = static_cast<X64Register>(src_reg_temp.index);

				// MOV r64, r64 (REX.W/R/B + Opcode 89 + ModR/M)
				if (static_cast<uint8_t>(src_physical_reg) >= static_cast<uint8_t>(X64Register::R8)) {
					textSectionData.back() |= (1 << 0); // Set REX.B bit on the already pushed REX prefix (for R/M field)
				}
				textSectionData.push_back(0x89);  // MOV r/m64, r64 (Reg to R/M)
				// ModR/M: Mod=11 (reg-to-reg), Reg=target_reg_encoding, R/M=src_physical_reg_encoding
				textSectionData.push_back((0x03 << 6) | ((static_cast<uint8_t>(target_reg) & 0x07) << 3) | (static_cast<uint8_t>(src_physical_reg) & 0x07));
			} else {
				assert(false && "Unsupported argument value type");
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
		size_t paramIndex = 3;  // Start after return type, size, and function name
		while (paramIndex + 2 < instruction.getOperandCount()) {  // Need at least type, size, and name
			auto param_type = instruction.getOperandAs<Type>(paramIndex);
			auto param_size = instruction.getOperandAs<int>(paramIndex + 1);
			auto param_name = instruction.getOperandAs<std::string_view>(paramIndex + 2);

			// Store parameter at [rsp+8] for first parameter, [rsp+16] for second, etc.
			int paramNumber = paramIndex / 3 - 1;
			int offset = 8 + (paramNumber * 8);

			if (paramNumber < INT_PARAM_REGS.size()) {
				X64Register src_reg = INT_PARAM_REGS[paramNumber];

				// Generate the MOV [rsp+offset], reg instruction
				auto mov_opcodes = generateMovToStack(src_reg, offset);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
			}

			variable_scopes.back().identifier_offset[param_name] = offset;
			paramIndex += 3;  // Skip type, size, and name
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

		// Retrieve the original result operand from the IR instruction
		auto ir_result_operand = instruction.getOperand(0);

		// Allocate physical registers for operands. The result will be stored in lhs_physical_reg
		X64Register result_physical_reg = regAlloc.allocate(); // This register will hold LHS initially and then the computed result
		X64Register rhs_physical_reg = regAlloc.allocate();

		// Load LHS into result_physical_reg from [rsp+8] (first parameter)
		auto lhs_opcodes = generateMovFromStack(result_physical_reg, 8);
		textSectionData.insert(textSectionData.end(), lhs_opcodes.op_codes.begin(), lhs_opcodes.op_codes.begin() + lhs_opcodes.size_in_bytes);

		// Load RHS into rhs_physical_reg from [rsp+16] (second parameter)
		auto rhs_opcodes = generateMovFromStack(rhs_physical_reg, 16);
		textSectionData.insert(textSectionData.end(), rhs_opcodes.op_codes.begin(), rhs_opcodes.op_codes.begin() + rhs_opcodes.size_in_bytes);

		// Add rhs_physical_reg to result_physical_reg
		std::array<uint8_t, 3> addInst = { 0x48, 0x01, 0xC0 }; // add r/m64, r64. 0x01 is opcode, 0xC0 is ModR/M for reg, reg
		addInst[2] = 0xC0 + (static_cast<uint8_t>(rhs_physical_reg) << 3) + static_cast<uint8_t>(result_physical_reg);
		textSectionData.insert(textSectionData.end(), addInst.begin(), addInst.end());

		// Determine the final destination of the result (register or memory)
		int final_result_offset;
		bool is_final_destination_register = std::holds_alternative<TempVar>(ir_result_operand);

		if (is_final_destination_register) {
			// If the result is a temporary variable, its index is the target register
			final_result_offset = std::get<TempVar>(ir_result_operand).index;
			X64Register target_reg = static_cast<X64Register>(final_result_offset);

			// Move the computed result from result_physical_reg to the target register
			auto movResultToTarget = regAlloc.get_reg_reg_move_op_code(target_reg, result_physical_reg, size / 8);
			if (movResultToTarget.size_in_bytes > 0) { // Only insert if a move is actually needed
				textSectionData.insert(textSectionData.end(), movResultToTarget.op_codes.begin(), movResultToTarget.op_codes.begin() + movResultToTarget.size_in_bytes);
			}
		} else {
			// If the result is a named variable, find its stack offset
			final_result_offset = variable_scopes.back().identifier_offset[std::get<std::string_view>(ir_result_operand)];

			// Store the computed result from result_physical_reg to memory
			// mov [rbp + offset], result_physical_reg
			std::array<uint8_t, 7> storeResultInst = { 0x48, 0x89, 0x00, 0, 0, 0, 0 }; // 0x48 for REX.W, 0x89 for mov r/m64, r64
			// ModR/M byte: Mod=10 (disp32), Reg=result_physical_reg, R/M=101 (RBP)
			storeResultInst[2] = (0x02 << 6) | (static_cast<uint8_t>(result_physical_reg) << 3) | 0x05;
			
			std::memcpy(storeResultInst.data() + 3, &final_result_offset, sizeof(final_result_offset));
			textSectionData.insert(textSectionData.end(), storeResultInst.begin(), storeResultInst.end());
		}
		
		// Release the allocated registers
		regAlloc.release(rhs_physical_reg);
		regAlloc.release(result_physical_reg);
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

		// Retrieve the original result operand from the IR instruction
		auto ir_result_operand = instruction.getOperand(0);

		// Allocate physical registers for operands. The result will be stored in lhs_physical_reg
		X64Register result_physical_reg = regAlloc.allocate(); // This register will hold LHS initially and then the computed result
		X64Register rhs_physical_reg = regAlloc.allocate();

		// Load LHS into result_physical_reg from [rsp+8] (first parameter)
		auto lhs_opcodes = generateMovFromStack(result_physical_reg, 8);
		textSectionData.insert(textSectionData.end(), lhs_opcodes.op_codes.begin(), lhs_opcodes.op_codes.begin() + lhs_opcodes.size_in_bytes);

		// Load RHS into rhs_physical_reg from [rsp+16] (second parameter)
		auto rhs_opcodes = generateMovFromStack(rhs_physical_reg, 16);
		textSectionData.insert(textSectionData.end(), rhs_opcodes.op_codes.begin(), rhs_opcodes.op_codes.begin() + rhs_opcodes.size_in_bytes);

		// Subtract rhs_physical_reg from result_physical_reg
		std::array<uint8_t, 3> subInst = { 0x48, 0x29, 0xC0 }; // sub r/m64, r64. 0x29 is opcode, 0xC0 is ModR/M for reg, reg
		subInst[2] = 0xC0 + (static_cast<uint8_t>(rhs_physical_reg) << 3) + static_cast<uint8_t>(result_physical_reg);
		textSectionData.insert(textSectionData.end(), subInst.begin(), subInst.end());

		// Determine the final destination of the result (register or memory)
		int final_result_offset;
		bool is_final_destination_register = std::holds_alternative<TempVar>(ir_result_operand);

		if (is_final_destination_register) {
			// If the result is a temporary variable, its index is the target register
			final_result_offset = std::get<TempVar>(ir_result_operand).index;
			X64Register target_reg = static_cast<X64Register>(final_result_offset);

			// Move the computed result from result_physical_reg to the target register
			auto movResultToTarget = regAlloc.get_reg_reg_move_op_code(target_reg, result_physical_reg, sizeInBits / 8);
			if (movResultToTarget.size_in_bytes > 0) { // Only insert if a move is actually needed
				textSectionData.insert(textSectionData.end(), movResultToTarget.op_codes.begin(), movResultToTarget.op_codes.begin() + movResultToTarget.size_in_bytes);
			}
		} else {
			// If the result is a named variable, find its stack offset
			final_result_offset = variable_scopes.back().identifier_offset[std::get<std::string_view>(ir_result_operand)];

			// Store the computed result from result_physical_reg to memory
			// mov [rbp + offset], result_physical_reg
			std::array<uint8_t, 7> storeResultInst = { 0x48, 0x89, 0x00, 0, 0, 0, 0 }; // 0x48 for REX.W, 0x89 for mov r/m64, r64
			// ModR/M byte: Mod=10 (disp32), Reg=result_physical_reg, R/M=101 (RBP)
			storeResultInst[2] = (0x02 << 6) | (static_cast<uint8_t>(result_physical_reg) << 3) | 0x05;
			
			std::memcpy(storeResultInst.data() + 3, &final_result_offset, sizeof(final_result_offset));
			textSectionData.insert(textSectionData.end(), storeResultInst.begin(), storeResultInst.end());
		}
		
		// Release the allocated registers
		regAlloc.release(rhs_physical_reg);
		regAlloc.release(result_physical_reg);
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

		// Retrieve the original result operand from the IR instruction
		auto ir_result_operand = instruction.getOperand(0);

		// Allocate physical registers for operands. The result will be stored in lhs_physical_reg
		X64Register result_physical_reg = regAlloc.allocate(); // This register will hold LHS initially and then the computed result
		X64Register rhs_physical_reg = regAlloc.allocate();

		// Load LHS into result_physical_reg from [rsp+8] (first parameter)
		auto lhs_opcodes = generateMovFromStack(result_physical_reg, 8);
		textSectionData.insert(textSectionData.end(), lhs_opcodes.op_codes.begin(), lhs_opcodes.op_codes.begin() + lhs_opcodes.size_in_bytes);

		// Load RHS into rhs_physical_reg from [rsp+16] (second parameter)
		auto rhs_opcodes = generateMovFromStack(rhs_physical_reg, 16);
		textSectionData.insert(textSectionData.end(), rhs_opcodes.op_codes.begin(), rhs_opcodes.op_codes.begin() + rhs_opcodes.size_in_bytes);

		// Multiply result_physical_reg by rhs_physical_reg
		std::array<uint8_t, 4> mulInst = { 0x48, 0x0F, 0xAF, 0x00 }; // REX.W (0x48) + Opcode (0x0F 0xAF) + ModR/M (initially 0x00)
		// ModR/M: Mod=11 (register-to-register), Reg=result_physical_reg, R/M=rhs_physical_reg
		mulInst[3] = 0xC0 + (static_cast<uint8_t>(result_physical_reg) << 3) + static_cast<uint8_t>(rhs_physical_reg);
		textSectionData.insert(textSectionData.end(), mulInst.begin(), mulInst.end());

		// Determine the final destination of the result (register or memory)
		int final_result_offset;
		bool is_final_destination_register = std::holds_alternative<TempVar>(ir_result_operand);

		if (is_final_destination_register) {
			// If the result is a temporary variable, its index is the target register
			final_result_offset = std::get<TempVar>(ir_result_operand).index;
			X64Register target_reg = static_cast<X64Register>(final_result_offset);

			// Move the computed result from result_physical_reg to the target register
			auto movResultToTarget = regAlloc.get_reg_reg_move_op_code(target_reg, result_physical_reg, sizeInBits / 8);
			if (movResultToTarget.size_in_bytes > 0) { // Only insert if a move is actually needed
				textSectionData.insert(textSectionData.end(), movResultToTarget.op_codes.begin(), movResultToTarget.op_codes.begin() + movResultToTarget.size_in_bytes);
			}
		} else {
			// If the result is a named variable, find its stack offset
			final_result_offset = variable_scopes.back().identifier_offset[std::get<std::string_view>(ir_result_operand)];

			// Store the computed result from result_physical_reg to memory
			// mov [rbp + offset], result_physical_reg
			std::array<uint8_t, 7> storeResultInst = { 0x48, 0x89, 0x00, 0, 0, 0, 0 }; // 0x48 for REX.W, 0x89 for mov r/m64, r64
			// ModR/M byte: Mod=10 (disp32), Reg=result_physical_reg, R/M=101 (RBP)
			storeResultInst[2] = (0x02 << 6) | (static_cast<uint8_t>(result_physical_reg) << 3) | 0x05;
			
			std::memcpy(storeResultInst.data() + 3, &final_result_offset, sizeof(final_result_offset));
			textSectionData.insert(textSectionData.end(), storeResultInst.begin(), storeResultInst.end());
		}
		
		// Release the allocated registers
		regAlloc.release(rhs_physical_reg);
		regAlloc.release(result_physical_reg);
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

		// Retrieve the original result operand from the IR instruction
		auto ir_result_operand = instruction.getOperand(0);

		// Allocate physical registers for operands. The result will be stored in lhs_physical_reg
		X64Register result_physical_reg = regAlloc.allocate(); // This register will hold LHS initially and then the computed result
		X64Register rhs_physical_reg = regAlloc.allocate();

		// Load LHS into result_physical_reg from [rsp+8] (first parameter)
		auto lhs_opcodes = generateMovFromStack(result_physical_reg, 8);
		textSectionData.insert(textSectionData.end(), lhs_opcodes.op_codes.begin(), lhs_opcodes.op_codes.begin() + lhs_opcodes.size_in_bytes);

		// Load RHS into rhs_physical_reg from [rsp+16] (second parameter)
		auto rhs_opcodes = generateMovFromStack(rhs_physical_reg, 16);
		textSectionData.insert(textSectionData.end(), rhs_opcodes.op_codes.begin(), rhs_opcodes.op_codes.begin() + rhs_opcodes.size_in_bytes);

		// Move result_physical_reg to RAX (dividend must be in RAX for idiv)
		auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, result_physical_reg, sizeInBits / 8);
		textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);

		// cdq - sign extend eax into edx:eax
		std::array<uint8_t, 1> cdqInst = { 0x99 };
		textSectionData.insert(textSectionData.end(), cdqInst.begin(), cdqInst.end());

		// idiv rhs_physical_reg
		std::array<uint8_t, 3> divInst = { 0x48, 0xF7, 0x00 }; // REX.W (0x48) + Opcode (0xF7) + ModR/M (initially 0x00)
		// ModR/M: Mod=11 (register-to-register), Reg=7 (opcode extension for idiv), R/M=rhs_physical_reg
		divInst[2] = 0xC0 + (0x07 << 3) + static_cast<uint8_t>(rhs_physical_reg);
		textSectionData.insert(textSectionData.end(), divInst.begin(), divInst.end());

		// Determine the final destination of the result (register or memory)
		int final_result_offset;
		bool is_final_destination_register = std::holds_alternative<TempVar>(ir_result_operand);

		if (is_final_destination_register) {
			// If the result is a temporary variable, its index is the target register
			final_result_offset = std::get<TempVar>(ir_result_operand).index;
			X64Register target_reg = static_cast<X64Register>(final_result_offset);

			// Move the quotient from RAX to the target register (if not already RAX)
			if (target_reg != X64Register::RAX) {
				auto movResultToTarget = regAlloc.get_reg_reg_move_op_code(target_reg, X64Register::RAX, sizeInBits / 8);
				if (movResultToTarget.size_in_bytes > 0) {
					textSectionData.insert(textSectionData.end(), movResultToTarget.op_codes.begin(), movResultToTarget.op_codes.begin() + movResultToTarget.size_in_bytes);
				}
			}
		} else {
			// If the result is a named variable, find its stack offset
			final_result_offset = variable_scopes.back().identifier_offset[std::get<std::string_view>(ir_result_operand)];

			// Store the computed quotient from RAX to memory
			// mov [rbp + offset], RAX
			std::array<uint8_t, 7> storeResultInst = { 0x48, 0x89, 0x00, 0, 0, 0, 0 }; // 0x48 for REX.W, 0x89 for mov r/m64, r64
			// ModR/M byte: Mod=10 (disp32), Reg=RAX, R/M=101 (RBP)
			storeResultInst[2] = (0x02 << 6) | (static_cast<uint8_t>(X64Register::RAX) << 3) | 0x05;
			
			std::memcpy(storeResultInst.data() + 3, &final_result_offset, sizeof(final_result_offset));
			textSectionData.insert(textSectionData.end(), storeResultInst.begin(), storeResultInst.end());
		}
		
		// Release the allocated registers
		regAlloc.release(rhs_physical_reg);
		// RAX (result_physical_reg initially) is not released here if it's the target reg or return value.
		// Only release if it's a temporary that's not the final destination.
		if (!is_final_destination_register || (is_final_destination_register && static_cast<X64Register>(final_result_offset) != X64Register::RAX)) {
			regAlloc.release(result_physical_reg);
		}
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
