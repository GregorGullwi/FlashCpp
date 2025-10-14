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

/*
	+ ---------------- +
	| Parameter 2	| [rbp + 24] < -Positive offsets
	| Parameter 1	| [rbp + 16] < -Positive offsets
	| Return Address| [rbp + 8]
	| Saved RBP		| [rbp + 0] < -RBP points here
	| Local Var 1	| [rbp - 8] < -Negative offsets
	| Local Var 2	| [rbp - 16] < -Negative offsets
	| Temp Var 1	| [rbp - 24] < -Negative offsets
	+ ---------------- +
*/

// Maximum possible size for 'mov destination_register, [rbp + offset]' instruction:
// REX (1 byte) + Opcode (1 byte) + ModR/M (1 byte) + SIB (1 byte) + Disp32 (4 bytes) = 8 bytes
constexpr size_t MAX_MOV_INSTRUCTION_SIZE = 8;

struct OpCodeWithSize {
	std::array<uint8_t, MAX_MOV_INSTRUCTION_SIZE> op_codes;
	size_t size_in_bytes = 0;
};

/**
 * @brief Generates x86-64 binary opcodes for 'mov destination_register, [rbp + offset]'.
 *
 * This function creates the byte sequence for moving a 64-bit value from a
 * frame-relative address (RBP + offset) into a general-purpose 64-bit register.
 * It handles REX prefixes, ModR/M, and 8-bit/32-bit displacements.
 * The generated opcodes are placed into a stack-allocated `std::array` within
 * the returned `OpCodeWithSize` struct.
 *
 * @param destinationRegister The target register (e.g., RAX, RCX, R8).
 * @param offset The signed byte offset from RBP. Negative for locals, positive for parameters.
 * @return An `OpCodeWithSize` struct containing a stack-allocated `std::array`
 *         of `uint8_t` and the actual number of bytes generated.
 */
OpCodeWithSize generateMovFromFrame(X64Register destinationRegister, int32_t offset) {
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
		mod_field = 0x01; // Mod = 01b (8-bit displacement) - RBP always needs displacement even for 0
	}
	else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01; // Mod = 01b (8-bit displacement)
	}
	else {
		mod_field = 0x02; // Mod = 10b (32-bit displacement)
	}

	// RBP encoding is 0x05, no SIB needed for RBP-relative addressing
	modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05; // 0x05 for RBP
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- Displacement ---
	if (offset == 0 || (offset >= -128 && offset <= 127)) {
		// 8-bit signed displacement (even for offset 0 with RBP)
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

	return result;
}

/**
 * @brief Generates x86-64 binary opcodes for 'mov [rbp + offset], source_register'.
 *
 * This function creates the byte sequence for moving a 64-bit value from a
 * general-purpose 64-bit register to a frame-relative address (RBP + offset).
 * It handles REX prefixes, ModR/M, and 8-bit/32-bit displacements.
 * The generated opcodes are placed into a stack-allocated `std::array` within
 * the returned `OpCodeWithSize` struct.
 *
 * @param sourceRegister The source register (e.g., RAX, RCX, R8).
 * @param offset The signed byte offset from RBP. Negative for locals, positive for parameters.
 * @return An `OpCodeWithSize` struct containing a stack-allocated `std::array`
 *         of `uint8_t` and the actual number of bytes generated.
 */
OpCodeWithSize generateMovToFrame(X64Register sourceRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;

	uint8_t* current_byte_ptr = result.op_codes.data();

	// --- REX Prefix (0x40 | W | R | X | B) ---
	uint8_t rex_prefix = 0x48; // Base: REX.W = 0100_1000b

	// If source register (Reg field) is R8-R15, set REX.R bit.
	if (static_cast<uint8_t>(sourceRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= (1 << 2); // Set R bit (0b0100)
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
		mod_field = 0x01; // Mod = 01b (8-bit displacement) - RBP always needs displacement even for 0
	} else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01; // Mod = 01b (8-bit displacement)
	} else {
		mod_field = 0x02; // Mod = 10b (32-bit displacement)
	}

	// RBP encoding is 0x05, no SIB needed for RBP-relative addressing
	modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05; // 0x05 for RBP
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- Displacement ---
	if (offset == 0 || (offset >= -128 && offset <= 127)) {
		// 8-bit signed displacement (even for offset 0 with RBP)
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

	return result;
}

// CLANG COMPATIBILITY: Generate MOV [rsp+offset], reg instruction for RSP-relative addressing
OpCodeWithSize generateMovToRsp(X64Register sourceRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;

	uint8_t* current_byte_ptr = result.op_codes.data();

	// --- REX Prefix (0x40 | W | R | X | B) ---
	uint8_t rex_prefix = 0x48; // Base: REX.W = 0100_1000b

	// If source register (Reg field) is R8-R15, set REX.R bit.
	if (static_cast<uint8_t>(sourceRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= (1 << 2); // Set R bit (0b0100)
	}

	*current_byte_ptr++ = rex_prefix;
	result.size_in_bytes++;

	// --- Opcode ---
	*current_byte_ptr++ = 0x89; // MOV r/m64, r64
	result.size_in_bytes++;

	// --- ModR/M Byte ---
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(sourceRegister) & 0x07;
	uint8_t modrm_byte;

	// For RSP-relative addressing with displacement
	uint8_t mod_field = (offset == 0) ? 0x00 : ((offset >= -128 && offset <= 127) ? 0x01 : 0x02);

	// RSP encoding is 0x04, requires SIB byte
	modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x04; // 0x04 for RSP (requires SIB)
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- SIB Byte (required for RSP) ---
	// Scale=00 (no scale), Index=100 (no index), Base=100 (RSP)
	*current_byte_ptr++ = 0x24; // SIB: scale=00, index=100, base=100 (RSP)
	result.size_in_bytes++;

	// --- Displacement ---
	if (offset != 0) {
		if (offset >= -128 && offset <= 127) {
			// 8-bit signed displacement
			*current_byte_ptr++ = static_cast<uint8_t>(offset);
			result.size_in_bytes++;
		} else {
			// 32-bit signed displacement
			for (int i = 0; i < 4; ++i) {
				*current_byte_ptr++ = static_cast<uint8_t>((offset >> (8 * i)) & 0xFF);
				result.size_in_bytes++;
			}
		}
	}

	return result;
}

// CLANG COMPATIBILITY: Generate 32-bit MOV [rsp+offset], reg instruction (like Clang)
OpCodeWithSize generateMovToRsp32(X64Register sourceRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;

	uint8_t* current_byte_ptr = result.op_codes.data();

	// NO REX prefix for 32-bit operations (unlike 64-bit version)
	// --- Opcode ---
	*current_byte_ptr++ = 0x89; // MOV r/m32, r32
	result.size_in_bytes++;

	// --- ModR/M Byte ---
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(sourceRegister) & 0x07;
	uint8_t modrm_byte;

	// For RSP-relative addressing with displacement
	uint8_t mod_field = (offset == 0) ? 0x00 : ((offset >= -128 && offset <= 127) ? 0x01 : 0x02);

	// RSP encoding is 0x04, requires SIB byte
	modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x04; // 0x04 for RSP (requires SIB)
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- SIB Byte (required for RSP) ---
	// Scale=00 (no scale), Index=100 (no index), Base=100 (RSP)
	*current_byte_ptr++ = 0x24; // SIB: scale=00, index=100, base=100 (RSP)
	result.size_in_bytes++;

	// --- Displacement ---
	if (offset != 0) {
		if (offset >= -128 && offset <= 127) {
			// 8-bit signed displacement
			*current_byte_ptr++ = static_cast<uint8_t>(offset);
			result.size_in_bytes++;
		} else {
			// 32-bit signed displacement
			for (int i = 0; i < 4; ++i) {
				*current_byte_ptr++ = static_cast<uint8_t>((offset >> (8 * i)) & 0xFF);
				result.size_in_bytes++;
			}
		}
	}

	return result;
}

// CLANG COMPATIBILITY: Generate 32-bit MOV reg, [rsp+offset] instruction (like Clang)
OpCodeWithSize generateMovFromRsp32(X64Register destinationRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;

	uint8_t* current_byte_ptr = result.op_codes.data();

	// NO REX prefix for 32-bit operations (unlike 64-bit version)
	// --- Opcode ---
	*current_byte_ptr++ = 0x8B; // MOV r32, r/m32
	result.size_in_bytes++;

	// --- ModR/M Byte ---
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(destinationRegister) & 0x07;
	uint8_t modrm_byte;

	// For RSP-relative addressing with displacement
	uint8_t mod_field = (offset == 0) ? 0x00 : ((offset >= -128 && offset <= 127) ? 0x01 : 0x02);

	// RSP encoding is 0x04, requires SIB byte
	modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x04; // 0x04 for RSP (requires SIB)
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- SIB Byte (required for RSP) ---
	// Scale=00 (no scale), Index=100 (no index), Base=100 (RSP)
	*current_byte_ptr++ = 0x24; // SIB: scale=00, index=100, base=100 (RSP)
	result.size_in_bytes++;

	// --- Displacement ---
	if (offset != 0) {
		if (offset >= -128 && offset <= 127) {
			// 8-bit signed displacement
			*current_byte_ptr++ = static_cast<uint8_t>(offset);
			result.size_in_bytes++;
		} else {
			// 32-bit signed displacement
			for (int i = 0; i < 4; ++i) {
				*current_byte_ptr++ = static_cast<uint8_t>((offset >> (8 * i)) & 0xFF);
				result.size_in_bytes++;
			}
		}
	}

	return result;
}

// CLANG COMPATIBILITY: Generate MOV reg, [rsp+offset] instruction for RSP-relative addressing
OpCodeWithSize generateMovFromRsp(X64Register destinationRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;

	uint8_t* current_byte_ptr = result.op_codes.data();

	// --- REX Prefix (0x40 | W | R | X | B) ---
	uint8_t rex_prefix = 0x48; // Base: REX.W = 0100_1000b

	// If destination register is R8-R15 (enum values >= 8), set REX.R bit.
	if (static_cast<uint8_t>(destinationRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= (1 << 2); // Set R bit (0b0100)
	}

	*current_byte_ptr++ = rex_prefix;
	result.size_in_bytes++;

	// --- Opcode ---
	*current_byte_ptr++ = 0x8B; // MOV r64, r/m64
	result.size_in_bytes++;

	// --- ModR/M Byte ---
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(destinationRegister) & 0x07;
	uint8_t modrm_byte;

	// For RSP-relative addressing with displacement
	uint8_t mod_field = (offset == 0) ? 0x00 : ((offset >= -128 && offset <= 127) ? 0x01 : 0x02);

	// RSP encoding is 0x04, requires SIB byte
	modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x04; // 0x04 for RSP (requires SIB)
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- SIB Byte (required for RSP) ---
	// Scale=00 (no scale), Index=100 (no index), Base=100 (RSP)
	*current_byte_ptr++ = 0x24; // SIB: scale=00, index=100, base=100 (RSP)
	result.size_in_bytes++;

	// --- Displacement ---
	if (offset != 0) {
		if (offset >= -128 && offset <= 127) {
			// 8-bit signed displacement
			*current_byte_ptr++ = static_cast<uint8_t>(offset);
			result.size_in_bytes++;
		} else {
			// 32-bit signed displacement
			for (int i = 0; i < 4; ++i) {
				*current_byte_ptr++ = static_cast<uint8_t>((offset >> (8 * i)) & 0xFF);
				result.size_in_bytes++;
			}
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

// Converts an X64Register enum to its corresponding CodeView register code.
uint16_t getX64RegisterCodeViewCode(X64Register reg) {
    switch (reg) {
        case X64Register::RAX: return 328;
        case X64Register::RCX: return 329;
        case X64Register::RDX: return 330;
        case X64Register::RBX: return 331;
        case X64Register::RSP: return 332;
        case X64Register::RBP: return 333;
        case X64Register::RSI: return 334;
        case X64Register::RDI: return 335;
        case X64Register::R8:  return 336;
        case X64Register::R9:  return 337;
        case X64Register::R10: return 338;
        case X64Register::R11: return 339;
        case X64Register::R12: return 340;
        case X64Register::R13: return 341;
        case X64Register::R14: return 342;
        case X64Register::R15: return 343;
        default: return 0; // Should not happen for general purpose registers
    }
}

std::optional<TempVar> getTempVarFromOffset(int32_t stackVariableOffset) {
	// For RBP-relative addressing, temporary variables have negative offsets
	// TempVar 0 is at offset -8, TempVar 1 is at offset -16, etc.
	if (stackVariableOffset < 0 && (stackVariableOffset % 8) == 0) {
		size_t index = static_cast<size_t>((-stackVariableOffset / 8) - 1);
		return TempVar(index);
	}

	return std::nullopt;
}

int32_t getStackOffsetFromTempVar(TempVar tempVar) {
	// For RBP-relative addressing, temporary variables use negative offsets
	// TempVar 0 is at [rbp-8], TempVar 1 is at [rbp-16], etc.
	return static_cast<int32_t>((tempVar.index) * -8);
}

struct RegisterAllocator
{
	static constexpr uint8_t REGISTER_COUNT = static_cast<uint8_t>(X64Register::Count) - 1;
	struct AllocatedRegister {
		X64Register reg = X64Register::Count;
		bool isAllocated = false;
		bool isDirty = false;	// Does the stack variable need to be updated on a flush
		int32_t stackVariableOffset = INT_MIN;
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
			reg = AllocatedRegister{ .reg = reg.reg };
		}
		registers[static_cast<int>(X64Register::RSP)].isAllocated = true;	// assume RSP is always allocated
		registers[static_cast<int>(X64Register::RBP)].isAllocated = true;	// assume RBP is always allocated
	}

	template<typename Func>
	void flushAllDirtyRegisters(Func func) {
		for (auto& reg : registers) {
			if (reg.isDirty) {
				func(reg.reg, reg.stackVariableOffset);
				reg.isDirty = false;
			}
		}
	}

	void flushSingleDirtyRegister(X64Register reg) {
		registers[static_cast<int>(reg)].isDirty = false;
	}

	AllocatedRegister& allocate() {
		for (auto& reg : registers) {
			if (!reg.isAllocated) {
				reg.isAllocated = true;
				return reg;
			}
		}
		throw std::runtime_error("No registers available");
	}

	void allocateSpecific(X64Register reg, int32_t stackVariableOffset) {
		assert(!registers[static_cast<int>(reg)].isAllocated);
		registers[static_cast<int>(reg)].isAllocated = true;
		registers[static_cast<int>(reg)].stackVariableOffset = stackVariableOffset;
	}

	void release(X64Register reg) {
		registers[static_cast<int>(reg)] = AllocatedRegister{ .reg = reg };
	}

	bool is_allocated(X64Register reg) const {
		return registers[static_cast<size_t>(reg)].isAllocated;
	}

	void mark_reg_dirty(X64Register reg) {
		assert(registers[static_cast<int>(reg)].isAllocated);
		registers[static_cast<int>(reg)].isDirty = true;
	}

	std::optional<X64Register> tryGetStackVariableRegister(int32_t stackVariableOffset) const {
		for (auto& reg : registers) {
			if (reg.stackVariableOffset == stackVariableOffset) {
				return reg.reg;
			}
		}
		return std::nullopt;
	}

	void set_stack_variable_offset(X64Register reg, int32_t stackVariableOffset) {
		assert(registers[static_cast<int>(reg)].isAllocated);
		registers[static_cast<int>(reg)].stackVariableOffset = stackVariableOffset;
		registers[static_cast<int>(reg)].isDirty = true;
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

	void convert(const Ir& ir, const std::string_view filename, const std::string_view source_filename = "") {
		// Group instructions by function for stack space calculation
		groupInstructionsByFunction(ir);

		for (const auto& instruction : ir.getInstructions()) {
			// Add line mapping for debug information if line number is available
			if (instruction.getOpcode() != IrOpcode::FunctionDecl && instruction.getOpcode() != IrOpcode::Return && instruction.getLineNumber() > 0) {
				addLineMapping(instruction.getLineNumber());
			}

			switch (instruction.getOpcode()) {
			case IrOpcode::FunctionDecl:
				handleFunctionDecl(instruction);
				break;
			case IrOpcode::VariableDecl:
				handleVariableDecl(instruction);
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
			case IrOpcode::UnsignedDivide:
				handleUnsignedDivide(instruction);
				break;
			case IrOpcode::ShiftLeft:
				handleShiftLeft(instruction);
				break;
			case IrOpcode::ShiftRight:
				handleShiftRight(instruction);
				break;
			case IrOpcode::UnsignedShiftRight:
				handleUnsignedShiftRight(instruction);
				break;
			case IrOpcode::BitwiseAnd:
				handleBitwiseAnd(instruction);
				break;
			case IrOpcode::BitwiseOr:
				handleBitwiseOr(instruction);
				break;
			case IrOpcode::BitwiseXor:
				handleBitwiseXor(instruction);
				break;
			case IrOpcode::Modulo:
				handleModulo(instruction);
				break;
			case IrOpcode::FloatAdd:
				handleFloatAdd(instruction);
				break;
			case IrOpcode::FloatSubtract:
				handleFloatSubtract(instruction);
				break;
			case IrOpcode::FloatMultiply:
				handleFloatMultiply(instruction);
				break;
			case IrOpcode::FloatDivide:
				handleFloatDivide(instruction);
				break;
			case IrOpcode::Equal:
				handleEqual(instruction);
				break;
			case IrOpcode::NotEqual:
				handleNotEqual(instruction);
				break;
			case IrOpcode::LessThan:
				handleLessThan(instruction);
				break;
			case IrOpcode::LessEqual:
				handleLessEqual(instruction);
				break;
			case IrOpcode::GreaterThan:
				handleGreaterThan(instruction);
				break;
			case IrOpcode::GreaterEqual:
				handleGreaterEqual(instruction);
				break;
			case IrOpcode::UnsignedLessThan:
				handleUnsignedLessThan(instruction);
				break;
			case IrOpcode::UnsignedLessEqual:
				handleUnsignedLessEqual(instruction);
				break;
			case IrOpcode::UnsignedGreaterThan:
				handleUnsignedGreaterThan(instruction);
				break;
			case IrOpcode::UnsignedGreaterEqual:
				handleUnsignedGreaterEqual(instruction);
				break;
			case IrOpcode::FloatEqual:
				handleFloatEqual(instruction);
				break;
			case IrOpcode::FloatNotEqual:
				handleFloatNotEqual(instruction);
				break;
			case IrOpcode::FloatLessThan:
				handleFloatLessThan(instruction);
				break;
			case IrOpcode::FloatLessEqual:
				handleFloatLessEqual(instruction);
				break;
			case IrOpcode::FloatGreaterThan:
				handleFloatGreaterThan(instruction);
				break;
			case IrOpcode::FloatGreaterEqual:
				handleFloatGreaterEqual(instruction);
				break;
			case IrOpcode::LogicalAnd:
				handleLogicalAnd(instruction);
				break;
			case IrOpcode::LogicalOr:
				handleLogicalOr(instruction);
				break;
			case IrOpcode::LogicalNot:
				handleLogicalNot(instruction);
				break;
			case IrOpcode::BitwiseNot:
				handleBitwiseNot(instruction);
				break;
			case IrOpcode::Negate:
				handleNegate(instruction);
				break;
			case IrOpcode::SignExtend:
				handleSignExtend(instruction);
				break;
			case IrOpcode::ZeroExtend:
				handleZeroExtend(instruction);
				break;
			case IrOpcode::Truncate:
				handleTruncate(instruction);
				break;
			case IrOpcode::AddAssign:
				handleAddAssign(instruction);
				break;
			case IrOpcode::SubAssign:
				handleSubAssign(instruction);
				break;
			case IrOpcode::MulAssign:
				handleMulAssign(instruction);
				break;
			case IrOpcode::DivAssign:
				handleDivAssign(instruction);
				break;
			case IrOpcode::ModAssign:
				handleModAssign(instruction);
				break;
			case IrOpcode::AndAssign:
				handleAndAssign(instruction);
				break;
			case IrOpcode::OrAssign:
				handleOrAssign(instruction);
				break;
			case IrOpcode::XorAssign:
				handleXorAssign(instruction);
				break;
			case IrOpcode::ShlAssign:
				handleShlAssign(instruction);
				break;
			case IrOpcode::ShrAssign:
				handleShrAssign(instruction);
				break;
			case IrOpcode::Assignment:
				handleAssignment(instruction);
				break;
			case IrOpcode::Label:
				handleLabel(instruction);
				break;
			case IrOpcode::Branch:
				handleBranch(instruction);
				break;
			case IrOpcode::ConditionalBranch:
				handleConditionalBranch(instruction);
				break;
			case IrOpcode::LoopBegin:
				handleLoopBegin(instruction);
				break;
			case IrOpcode::LoopEnd:
				handleLoopEnd(instruction);
				break;
			case IrOpcode::Break:
				handleBreak(instruction);
				break;
			case IrOpcode::Continue:
				handleContinue(instruction);
				break;
			case IrOpcode::ArrayAccess:
				handleArrayAccess(instruction);
				break;
			case IrOpcode::PreIncrement:
				handlePreIncrement(instruction);
				break;
			case IrOpcode::PostIncrement:
				handlePostIncrement(instruction);
				break;
			case IrOpcode::PreDecrement:
				handlePreDecrement(instruction);
				break;
			case IrOpcode::PostDecrement:
				handlePostDecrement(instruction);
				break;
			case IrOpcode::AddressOf:
				handleAddressOf(instruction);
				break;
			case IrOpcode::Dereference:
				handleDereference(instruction);
				break;
			case IrOpcode::MemberAccess:
				handleMemberAccess(instruction);
				break;
			case IrOpcode::MemberStore:
				handleMemberStore(instruction);
				break;
			default:
				assert(false && "Not implemented yet");
				break;
			}
		}

		// Use the provided source filename, or fall back to a default if not provided
		std::string actual_source_file = source_filename.empty() ? "test_debug.cpp" : std::string(source_filename);
		writer.add_source_file(actual_source_file);

		finalizeSections();
		writer.write(std::string(filename));
	}

private:
	// Shared arithmetic operation context
	struct ArithmeticOperationContext {
		Type type = Type::Void;
		int size_in_bits{};
		const IrOperand& result_operand;
		X64Register result_physical_reg;
		X64Register rhs_physical_reg;
	};

	// Setup and load operands for arithmetic operations - validates operands, extracts common data, and loads into registers
	ArithmeticOperationContext setupAndLoadArithmeticOperation(const IrInstruction& instruction, const char* operation_name) {
		assert(instruction.getOperandCount() == 7 &&
			   (std::string(operation_name) + " instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val").c_str());

		// Get type and size (from LHS)
		ArithmeticOperationContext ctx = {
			.type = instruction.getOperandAs<Type>(1),
			.size_in_bits = instruction.getOperandAs<int>(2),
			.result_operand = instruction.getOperand(0),
		};

		// Support integer, boolean, and floating-point operations
		if (!is_integer_type(ctx.type) && !is_bool_type(ctx.type) && !is_floating_point_type(ctx.type)) {
			assert(false && (std::string("Only integer/boolean/floating-point ") + operation_name + " is supported").c_str());
		}

		ctx.result_physical_reg = X64Register::Count;
		if (instruction.isOperandType<std::string_view>(3)) {
			auto lhs_var_op = instruction.getOperandAs<std::string_view>(3);
			auto lhs_var_id = variable_scopes.back().identifier_offset.find(lhs_var_op);
			if (lhs_var_id != variable_scopes.back().identifier_offset.end()) {
				if (auto var_reg = regAlloc.tryGetStackVariableRegister(lhs_var_id->second); var_reg.has_value()) {
					ctx.result_physical_reg = var_reg.value();	// value is already in a register, we can use it without a move!
				}
				else {
					assert(variable_scopes.back().scope_stack_space <= lhs_var_id->second);

					ctx.result_physical_reg = regAlloc.allocate().reg;
					auto mov_opcodes = generateMovFromFrame(ctx.result_physical_reg, lhs_var_id->second);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(ctx.result_physical_reg);
				}
			}
			else {
				assert(false && "Missing variable name"); // TODO: Error handling
			}
		}
		else if (instruction.isOperandType<TempVar>(3)) {
			auto lhs_var_op = instruction.getOperandAs<TempVar>(3);
			auto lhs_stack_var_addr = getStackOffsetFromTempVar(lhs_var_op);
			if (auto lhs_reg = regAlloc.tryGetStackVariableRegister(lhs_stack_var_addr); lhs_reg.has_value()) {
				ctx.result_physical_reg = lhs_reg.value();
			}
			else {
				assert(variable_scopes.back().scope_stack_space <= lhs_stack_var_addr);

				ctx.result_physical_reg = regAlloc.allocate().reg;
				auto mov_opcodes = generateMovFromFrame(ctx.result_physical_reg, lhs_stack_var_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				regAlloc.flushSingleDirtyRegister(ctx.result_physical_reg);
			}
		}
		else if (instruction.isOperandType<unsigned long long>(3)) {
			// LHS is a literal value
			auto lhs_value = instruction.getOperandAs<unsigned long long>(3);
			ctx.result_physical_reg = regAlloc.allocate().reg;

			// Load the literal value into the register
			// mov reg, imm64
			std::array<uint8_t, 10> movInst = { 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0 }; // movabs rax, imm64
			movInst[1] = 0xB8 + static_cast<uint8_t>(ctx.result_physical_reg);
			std::memcpy(&movInst[2], &lhs_value, sizeof(lhs_value));
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		}
		else if (instruction.isOperandType<double>(3)) {
			// LHS is a floating-point literal value
			auto lhs_value = instruction.getOperandAs<double>(3);
			ctx.result_physical_reg = regAlloc.allocate().reg;

			// For floating-point, we need to load the value into an XMM register
			// Strategy: Load the bit pattern as integer into a GPR, then move to XMM
			// 1. Load double bits into a GPR using movabs
			// 2. Move from GPR to XMM using movq

			uint64_t bits;
			std::memcpy(&bits, &lhs_value, sizeof(bits));

			// Allocate a temporary GPR for the bit pattern
			X64Register temp_gpr = regAlloc.allocate().reg;

			// movabs temp_gpr, imm64 (load bit pattern)
			std::array<uint8_t, 10> movInst = { 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0 };
			movInst[1] = 0xB8 + static_cast<uint8_t>(temp_gpr);
			std::memcpy(&movInst[2], &bits, sizeof(bits));
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());

			// movq xmm, r64 (66 REX.W 0F 6E /r) - move from GPR to XMM
			std::array<uint8_t, 5> movqInst = { 0x66, 0x48, 0x0F, 0x6E, 0xC0 };
			movqInst[4] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(temp_gpr);
			textSectionData.insert(textSectionData.end(), movqInst.begin(), movqInst.end());

			// Release the temporary GPR
			regAlloc.release(temp_gpr);
		}

		ctx.rhs_physical_reg = X64Register::Count;
		if (instruction.isOperandType<std::string_view>(6)) {
			auto rhs_var_op = instruction.getOperandAs<std::string_view>(6);
			auto rhs_var_id = variable_scopes.back().identifier_offset.find(rhs_var_op);
			if (rhs_var_id != variable_scopes.back().identifier_offset.end()) {
				if (auto var_reg = regAlloc.tryGetStackVariableRegister(rhs_var_id->second); var_reg.has_value()) {
					ctx.rhs_physical_reg = var_reg.value();	// value is already in a register, we can use it without a move!
				}
				else {
					assert(variable_scopes.back().scope_stack_space <= rhs_var_id->second);

					ctx.rhs_physical_reg = regAlloc.allocate().reg;
					auto mov_opcodes = generateMovFromFrame(ctx.rhs_physical_reg, rhs_var_id->second);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(ctx.rhs_physical_reg);
				}
			}
			else {
				assert(false && "Missing variable name"); // TODO: Error handling
			}
		}
		else if (instruction.isOperandType<TempVar>(6)) {
			auto rhs_var_op = instruction.getOperandAs<TempVar>(6);
			auto rhs_stack_var_addr = getStackOffsetFromTempVar(rhs_var_op);
			if (auto rhs_reg = regAlloc.tryGetStackVariableRegister(rhs_stack_var_addr); rhs_reg.has_value()) {
				ctx.rhs_physical_reg = rhs_reg.value();
			}
			else {
				assert(variable_scopes.back().scope_stack_space <= rhs_stack_var_addr);

				ctx.rhs_physical_reg = regAlloc.allocate().reg;
				auto mov_opcodes = generateMovFromFrame(ctx.rhs_physical_reg, rhs_stack_var_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				regAlloc.flushSingleDirtyRegister(ctx.rhs_physical_reg);
			}
		}
		else if (instruction.isOperandType<unsigned long long>(6)) {
			// RHS is a literal value
			auto rhs_value = instruction.getOperandAs<unsigned long long>(6);
			ctx.rhs_physical_reg = regAlloc.allocate().reg;

			// Load the literal value into the register
			// mov reg, imm64
			std::array<uint8_t, 10> movInst = { 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0 }; // movabs rax, imm64
			movInst[1] = 0xB8 + static_cast<uint8_t>(ctx.rhs_physical_reg);
			std::memcpy(&movInst[2], &rhs_value, sizeof(rhs_value));
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		}
		else if (instruction.isOperandType<double>(6)) {
			// RHS is a floating-point literal value
			auto rhs_value = instruction.getOperandAs<double>(6);
			ctx.rhs_physical_reg = regAlloc.allocate().reg;

			// For floating-point, we need to load the value into an XMM register
			// Strategy: Load the bit pattern as integer into a GPR, then move to XMM
			// 1. Load double bits into a GPR using movabs
			// 2. Move from GPR to XMM using movq

			uint64_t bits;
			std::memcpy(&bits, &rhs_value, sizeof(bits));

			// Allocate a temporary GPR for the bit pattern
			X64Register temp_gpr = regAlloc.allocate().reg;

			// movabs temp_gpr, imm64 (load bit pattern)
			std::array<uint8_t, 10> movInst = { 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0 };
			movInst[1] = 0xB8 + static_cast<uint8_t>(temp_gpr);
			std::memcpy(&movInst[2], &bits, sizeof(bits));
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());

			// movq xmm, r64 (66 REX.W 0F 6E /r) - move from GPR to XMM
			std::array<uint8_t, 5> movqInst = { 0x66, 0x48, 0x0F, 0x6E, 0xC0 };
			movqInst[4] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(temp_gpr);
			textSectionData.insert(textSectionData.end(), movqInst.begin(), movqInst.end());

			// Release the temporary GPR
			regAlloc.release(temp_gpr);
		}

		// If result register hasn't been allocated yet (e.g., LHS is a literal), allocate one now
		if (ctx.result_physical_reg == X64Register::Count) {
			ctx.result_physical_reg = regAlloc.allocate().reg;
		}

		if (std::holds_alternative<TempVar>(ctx.result_operand)) {
			const TempVar temp_var = std::get<TempVar>(ctx.result_operand);
			const int32_t stack_offset = getStackOffsetFromTempVar(temp_var);
			variable_scopes.back().identifier_offset[temp_var.name()] = stack_offset;
			regAlloc.set_stack_variable_offset(ctx.result_physical_reg, stack_offset);
		}

		return ctx;
	}

	// Store the result of arithmetic operations to the appropriate destination
	void storeArithmeticResult(const ArithmeticOperationContext& ctx, X64Register source_reg = X64Register::Count) {
		// Use the result register by default, or the specified source register (e.g., RAX for division)
		X64Register actual_source_reg = (source_reg == X64Register::Count) ? ctx.result_physical_reg : source_reg;

		// Determine the final destination of the result (register or memory)
		if (std::holds_alternative<std::string_view>(ctx.result_operand)) {
			// If the result is a named variable, find its stack offset
			int final_result_offset = variable_scopes.back().identifier_offset[std::get<std::string_view>(ctx.result_operand)];

			// Store the computed result from actual_source_reg to memory
			auto store_opcodes = generateMovToFrame(actual_source_reg, final_result_offset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(), store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
		}
		else if (std::holds_alternative<TempVar>(ctx.result_operand)) {
			auto res_var_op = std::get<TempVar>(ctx.result_operand);
			auto res_stack_var_addr = getStackOffsetFromTempVar(res_var_op);
			if (auto res_reg = regAlloc.tryGetStackVariableRegister(res_stack_var_addr); res_reg.has_value()) {
				if (res_reg != actual_source_reg) {
					auto moveFromRax = regAlloc.get_reg_reg_move_op_code(res_reg.value(), actual_source_reg, ctx.size_in_bits / 8);
					textSectionData.insert(textSectionData.end(), moveFromRax.op_codes.begin(), moveFromRax.op_codes.begin() + moveFromRax.size_in_bytes);
				}
			}
			else {
				assert(variable_scopes.back().scope_stack_space <= res_stack_var_addr);

				auto mov_opcodes = generateMovToFrame(actual_source_reg, res_stack_var_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
			}

		}
		else {
			assert(false && "Unhandled destination type");
		}

		if (source_reg != X64Register::Count) {
			regAlloc.release(source_reg);
		}
	}

	// Group IR instructions by function for analysis
	void groupInstructionsByFunction(const Ir& ir) {
		function_instructions.clear();
		std::string_view current_func_name;

		for (const auto& instruction : ir.getInstructions()) {
			if (instruction.getOpcode() == IrOpcode::FunctionDecl) {
				current_func_name = instruction.getOperandAs<std::string_view>(2);
				function_instructions[current_func_name] = std::vector<IrInstruction>();
			} else if (!current_func_name.empty()) {
				function_instructions[current_func_name].push_back(instruction);
			}
		}
	}

	// Calculate the total stack space needed for a function by analyzing its IR instructions
	struct StackSpaceSize {
		uint16_t temp_vars_size = 0;
		uint16_t named_vars_size = 0;
		uint16_t shadow_stack_space = 0;
	};
	struct StackVariableScope
	{
		int scope_stack_space = 0;
		std::unordered_map<std::string_view, int> identifier_offset;
	};
	StackSpaceSize calculateFunctionStackSpace(std::string_view func_name, StackVariableScope& var_scope) {
		StackSpaceSize func_stack_space{};

		auto it = function_instructions.find(func_name);
		if (it == function_instructions.end()) {
			return func_stack_space; // No instructions found for this function
		}

		// Find the maximum TempVar index used in this function
		int_fast32_t min_stack_offset = 0;

		struct VarDecl {
			std::string_view var_name{};
			int size_in_bits{};
			size_t alignment{};  // Custom alignment from alignas(n), 0 = use natural alignment
		};
		std::vector<VarDecl> local_vars;

		for (const auto& instruction : it->second) {
			// Look for TempVar operands in the instruction
			func_stack_space.shadow_stack_space |= (0x20 * !(instruction.getOpcode() != IrOpcode::FunctionCall));

			if (instruction.getOpcode() == IrOpcode::VariableDecl) {
				auto size_in_bits = instruction.getOperandAs<int>(1);
				auto var_name = instruction.getOperandAs<std::string_view>(2);
				size_t custom_alignment = instruction.getOperandAs<unsigned long long>(3);

				// Check if this is an array declaration (has array size operand)
				// Format: [type, size, name, custom_alignment, array_size_type, array_size_bits, array_size_value]
				int total_size_bits = size_in_bits;
				if (instruction.getOperandCount() >= 7 &&
				    instruction.isOperandType<Type>(4)) {
					// This is an array - get the array size
					if (instruction.isOperandType<unsigned long long>(6)) {
						uint64_t array_size = instruction.getOperandAs<unsigned long long>(6);
						total_size_bits = size_in_bits * static_cast<int>(array_size);
					}
				}

				func_stack_space.named_vars_size += (total_size_bits / 8);
				local_vars.push_back(VarDecl{ .var_name = var_name, .size_in_bits = total_size_bits, .alignment = custom_alignment });
			}
			else {
				for (size_t i = 0; i < instruction.getOperandCount(); ++i) {
					if (instruction.isOperandType<TempVar>(i)) {
						auto temp_var = instruction.getOperandAs<TempVar>(i);
						auto stack_offset = getStackOffsetFromTempVar(temp_var);
						min_stack_offset = std::min(min_stack_offset, stack_offset);
						var_scope.identifier_offset[temp_var.name()] = stack_offset;
					}
				}
			}
		}

		int_fast32_t stack_offset = min_stack_offset;
		for (const VarDecl& local_var : local_vars) {
			// Apply alignment if specified, otherwise use natural alignment (8 bytes for x64)
			size_t var_alignment = local_var.alignment > 0 ? local_var.alignment : 8;

			// Align the stack offset down to the required alignment
			// Stack grows downward, so we need to align down (toward more negative values)
			int_fast32_t aligned_offset = stack_offset;
			if (var_alignment > 1) {
				// Round down to nearest multiple of alignment
				// For negative offsets: (-16 & ~15) = -16, (-15 & ~15) = -16, (-17 & ~15) = -32
				aligned_offset = (stack_offset - static_cast<int_fast32_t>(var_alignment) + 1) & ~(static_cast<int_fast32_t>(var_alignment) - 1);
			}

			// Allocate space for the variable
			stack_offset = aligned_offset - (local_var.size_in_bits / 8);
			var_scope.identifier_offset.insert_or_assign(local_var.var_name, stack_offset);
		}

		func_stack_space.temp_vars_size = -min_stack_offset;
		func_stack_space.named_vars_size = min_stack_offset - stack_offset;

		// if we are a leaf function (don't call other functions), we can get by with just register if we don't have more than 8 * 64 bytes of values to store
		//if (shadow_stack_space == 0 && max_temp_var_index <= 8) {
			//return 0;
		//}

		return func_stack_space;
	}

	// Helper function to allocate stack space for a temporary variable
	int allocateStackSlotForTempVar(int32_t index) {
		auto stack_offset = getStackOffsetFromTempVar(TempVar(index));
		// Note: stack_offset should be within allocated space (scope_stack_space <= stack_offset <= 0)
		assert(variable_scopes.back().scope_stack_space <= stack_offset && stack_offset <= 0);
		return stack_offset;
	}

	void flushAllDirtyRegisters()
	{
		regAlloc.flushAllDirtyRegisters([this](X64Register reg, int32_t stackVariableOffset)
			{
				auto tempVarIndex = getTempVarFromOffset(stackVariableOffset);

				if (tempVarIndex.has_value()) {
					// Note: stackVariableOffset should be within allocated space (scope_stack_space <= stackVariableOffset <= 0)
					assert(variable_scopes.back().scope_stack_space <= stackVariableOffset && stackVariableOffset <= 0);

					// Store the computed result from register to stack
					auto store_opcodes = generateMovToFrame(reg, stackVariableOffset);
					textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(), store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
				}
			});
	}

	void handleFunctionCall(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() >= 2 && "Function call must have at least 2 ope rands (result, function name)");

		flushAllDirtyRegisters();

		// Get result variable and function name
		auto result_var = instruction.getOperand(0);
		auto funcName = instruction.getOperandAs<std::string_view>(1);

		// Get result offset
		int result_offset = 0;
		if (std::holds_alternative<TempVar>(result_var)) {
			const TempVar temp_var = std::get<TempVar>(result_var);
			result_offset = getStackOffsetFromTempVar(temp_var);
			variable_scopes.back().identifier_offset[temp_var.name()] = result_offset;
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

			// Determine if this is a floating-point argument
			bool is_float_arg = is_floating_point_type(argType);

			// Determine the target register for the argument
			X64Register target_reg = is_float_arg ? FLOAT_PARAM_REGS[i] : INT_PARAM_REGS[i];

			// Handle floating-point immediate values
			if (is_float_arg && std::holds_alternative<double>(argValue)) {
				// Load floating-point literal into XMM register
				double float_value = std::get<double>(argValue);

				// Convert double to bit pattern
				uint64_t bits;
				std::memcpy(&bits, &float_value, sizeof(bits));

				// Allocate a temporary GPR for the bit pattern
				X64Register temp_gpr = regAlloc.allocate().reg;

				// movabs temp_gpr, imm64 (load bit pattern)
				std::array<uint8_t, 10> movInst = { 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0 };
				movInst[1] = 0xB8 + static_cast<uint8_t>(temp_gpr);
				std::memcpy(&movInst[2], &bits, sizeof(bits));
				textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());

				// movq xmm, r64 (66 REX.W 0F 6E /r) - move from GPR to XMM
				std::array<uint8_t, 5> movqInst = { 0x66, 0x48, 0x0F, 0x6E, 0xC0 };
				movqInst[4] = 0xC0 + (static_cast<uint8_t>(target_reg) << 3) + static_cast<uint8_t>(temp_gpr);
				textSectionData.insert(textSectionData.end(), movqInst.begin(), movqInst.end());

				// Release the temporary GPR
				regAlloc.release(temp_gpr);
			} else if (std::holds_alternative<unsigned long long>(argValue)) {
				// Construct the REX prefix based on target_reg for destination (Reg field).
				// REX.W is always needed for 64-bit operations (0x48).
				// REX.R (bit 2) is set if the target_reg is R8-R15 (as it appears in the Reg field of ModR/M or is directly encoded).
				uint8_t rex_prefix = 0x48;
				if (static_cast<uint8_t>(target_reg) >= static_cast<uint8_t>(X64Register::R8)) {
					rex_prefix |= (1 << 2); // Set REX.R
				}
				// Push the REX prefix. It might be modified later for REX.B or REX.X if needed.
				textSectionData.push_back(rex_prefix);

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
				// Local variable - use RBP-relative addressing consistently
				std::string_view var_name = std::get<std::string_view>(argValue);
				int var_offset = variable_scopes.back().identifier_offset[var_name];

				if (is_float_arg) {
					// For floating-point arguments, use movsd xmm, [rbp+offset] (F2 0F 10 /r)
					// or movss for float (F3 0F 10 /r)
					uint8_t prefix = (argType == Type::Float) ? 0xF3 : 0xF2;

					// Build the instruction: prefix 0F 10 ModR/M [SIB] [disp]
					textSectionData.push_back(prefix);
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0x10);

					// ModR/M byte: Mod=01/10 (disp8/disp32), Reg=target_xmm, R/M=101 (RBP)
					uint8_t reg_bits = static_cast<uint8_t>(target_reg) & 0x07;
					uint8_t modrm;
					if (var_offset >= -128 && var_offset <= 127) {
						modrm = 0x45 | (reg_bits << 3); // Mod=01, R/M=101 (RBP)
						textSectionData.push_back(modrm);
						textSectionData.push_back(static_cast<uint8_t>(var_offset));
					} else {
						modrm = 0x85 | (reg_bits << 3); // Mod=10, R/M=101 (RBP)
						textSectionData.push_back(modrm);
						for (int j = 0; j < 4; ++j) {
							textSectionData.push_back(static_cast<uint8_t>(var_offset & 0xFF));
							var_offset >>= 8;
						}
					}
				} else {
					if (auto reg_var = regAlloc.tryGetStackVariableRegister(var_offset); reg_var.has_value()) {
						if (reg_var.value() != target_reg) {
							auto movResultToRax = regAlloc.get_reg_reg_move_op_code(target_reg, reg_var.value(), 4);	// TODO: reg size
							textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
						}
					}
					else {
						// Load from stack using RBP-relative addressing
						auto load_opcodes = generateMovFromFrame(target_reg, var_offset);
						textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
						regAlloc.flushSingleDirtyRegister(target_reg);
					}
				}
			} else if (std::holds_alternative<TempVar>(argValue)) {
				// Temporary variable value (stored on stack)
				auto src_reg_temp = std::get<TempVar>(argValue);
				const std::string_view temp_var_name = src_reg_temp.name();

				// Find the stack offset for this temporary variable
				const StackVariableScope& current_scope = variable_scopes.back();
				auto it = current_scope.identifier_offset.find(temp_var_name);
				if (it != current_scope.identifier_offset.end()) {
					int var_offset = it->second;

					if (is_float_arg) {
						// For floating-point arguments, use movsd xmm, [rbp+offset] (F2 0F 10 /r)
						// or movss for float (F3 0F 10 /r)
						uint8_t prefix = (argType == Type::Float) ? 0xF3 : 0xF2;

						// Build the instruction: prefix 0F 10 ModR/M [SIB] [disp]
						textSectionData.push_back(prefix);
						textSectionData.push_back(0x0F);
						textSectionData.push_back(0x10);

						// ModR/M byte: Mod=01/10 (disp8/disp32), Reg=target_xmm, R/M=101 (RBP)
						uint8_t reg_bits = static_cast<uint8_t>(target_reg) & 0x07;
						uint8_t modrm;
						if (var_offset >= -128 && var_offset <= 127) {
							modrm = 0x45 | (reg_bits << 3); // Mod=01, R/M=101 (RBP)
							textSectionData.push_back(modrm);
							textSectionData.push_back(static_cast<uint8_t>(var_offset));
						} else {
							modrm = 0x85 | (reg_bits << 3); // Mod=10, R/M=101 (RBP)
							textSectionData.push_back(modrm);
							for (int j = 0; j < 4; ++j) {
								textSectionData.push_back(static_cast<uint8_t>(var_offset & 0xFF));
								var_offset >>= 8;
							}
						}
					} else {
						if (auto reg_var = regAlloc.tryGetStackVariableRegister(var_offset); reg_var.has_value()) {
							if (reg_var.value() != target_reg) {
								auto movResultToRax = regAlloc.get_reg_reg_move_op_code(target_reg, reg_var.value(), 4);	// TODO: reg size
								textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
							}
						}
						// Use the existing generateMovFromFrame function for consistency
						auto mov_opcodes = generateMovFromFrame(target_reg, var_offset);
						textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
						regAlloc.flushSingleDirtyRegister(target_reg);
					}
				} else {
					// Temporary variable not found in stack. This indicates a problem with our
					// TempVar management. Let's allocate a stack slot for it now and assume
					// the value is currently in RAX (from the most recent operation).

					// Allocate stack slot for this TempVar
					int stack_offset = allocateStackSlotForTempVar(src_reg_temp.index);

					if (is_float_arg) {
						// For floating-point, assume value is in XMM0 and store it
						// movsd [rbp+offset], xmm0 (F2 0F 11 /r)
						uint8_t prefix = (argType == Type::Float) ? 0xF3 : 0xF2;
						textSectionData.push_back(prefix);
						textSectionData.push_back(0x0F);
						textSectionData.push_back(0x11); // Store variant

						uint8_t modrm;
						if (stack_offset >= -128 && stack_offset <= 127) {
							modrm = 0x45; // Mod=01, Reg=XMM0, R/M=101 (RBP)
							textSectionData.push_back(modrm);
							textSectionData.push_back(static_cast<uint8_t>(stack_offset));
						} else {
							modrm = 0x85; // Mod=10, Reg=XMM0, R/M=101 (RBP)
							textSectionData.push_back(modrm);
							for (int j = 0; j < 4; ++j) {
								textSectionData.push_back(static_cast<uint8_t>(stack_offset & 0xFF));
								stack_offset >>= 8;
							}
						}

						// Now load from stack to target register
						prefix = (argType == Type::Float) ? 0xF3 : 0xF2;
						int load_offset = allocateStackSlotForTempVar(src_reg_temp.index);
						textSectionData.push_back(prefix);
						textSectionData.push_back(0x0F);
						textSectionData.push_back(0x10);

						uint8_t reg_bits = static_cast<uint8_t>(target_reg) & 0x07;
						if (load_offset >= -128 && load_offset <= 127) {
							modrm = 0x45 | (reg_bits << 3);
							textSectionData.push_back(modrm);
							textSectionData.push_back(static_cast<uint8_t>(load_offset));
						} else {
							modrm = 0x85 | (reg_bits << 3);
							textSectionData.push_back(modrm);
							for (int j = 0; j < 4; ++j) {
								textSectionData.push_back(static_cast<uint8_t>(load_offset & 0xFF));
								load_offset >>= 8;
							}
						}
					} else {
						// Store RAX to the newly allocated stack slot first
						auto store_opcodes = generateMovToFrame(X64Register::RAX, stack_offset);
						textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(), store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

						// Now load from stack to target register
						auto load_opcodes = generateMovFromFrame(target_reg, stack_offset);
						textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
						regAlloc.flushSingleDirtyRegister(target_reg);
					}
				}
			} else {
				assert(false && "Unsupported argument value type");
			}
		}

		// call [function name] instruction is 5 bytes
		auto function_name = instruction.getOperandAs<std::string_view>(1);
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());

		writer.add_relocation(textSectionData.size() - 4, function_name);

		// REFERENCE COMPATIBILITY: Don't add closing brace line mappings
		// The reference compiler (clang) only maps opening brace and actual statements
		// Closing braces are not mapped in the line information

		regAlloc.reset();
		regAlloc.allocateSpecific(X64Register::RAX, result_offset);
		regAlloc.mark_reg_dirty(X64Register::RAX);
	}

	void handleVariableDecl(const IrInstruction& instruction) {
		auto var_type = instruction.getOperandAs<Type>(0);
		auto var_name = instruction.getOperandAs<std::string_view>(2);
		// Operand 3 is custom_alignment (added for alignas support)
		StackVariableScope& current_scope = variable_scopes.back();
		auto var_it = current_scope.identifier_offset.find(var_name);
		assert(var_it != current_scope.identifier_offset.end());

		// Format: [type, size, name, custom_alignment] for uninitialized
		//         [type, size, name, custom_alignment, init_type, init_size, init_value] for initialized
		bool is_initialized = instruction.getOperandCount() > 4;
		X64Register allocated_reg_val = X64Register::RAX; // Default

		if (is_initialized) {

			auto allocated_reg = regAlloc.allocate();
			allocated_reg_val = allocated_reg.reg;
			auto dst_offset = var_it->second;
			regAlloc.set_stack_variable_offset(allocated_reg.reg, dst_offset);

			// Check if the initializer is a literal value
			bool is_literal = instruction.isOperandType<unsigned long long>(6) ||
			                  instruction.isOperandType<int>(6) ||
			                  instruction.isOperandType<double>(6);

			if (is_literal) {
				// Load immediate value into register
				if (instruction.isOperandType<unsigned long long>(6)) {
					uint64_t value = instruction.getOperandAs<unsigned long long>(6);

					// MOV reg, imm64
					std::array<uint8_t, 10> movInst = { 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0 };
					movInst[0] = 0x48 | (static_cast<uint8_t>(allocated_reg.reg) >> 3);
					movInst[1] = 0xB8 + (static_cast<uint8_t>(allocated_reg.reg) & 0x7);
					std::memcpy(&movInst[2], &value, sizeof(value));
					textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
				} else if (instruction.isOperandType<int>(6)) {
					int value = instruction.getOperandAs<int>(6);

					// MOV reg, imm32 (sign-extended to 64-bit)
					std::array<uint8_t, 7> movInst = { 0x48, 0xC7, 0xC0, 0, 0, 0, 0 };
					movInst[0] = 0x48 | (static_cast<uint8_t>(allocated_reg.reg) >> 3);
					movInst[2] = 0xC0 + (static_cast<uint8_t>(allocated_reg.reg) & 0x7);
					std::memcpy(&movInst[3], &value, sizeof(value));
					textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
				}
				// TODO: Handle double literals
			} else {
				// Load from memory (TempVar or variable)
				int src_offset = 0;
				if (instruction.isOperandType<TempVar>(6)) {
					auto temp_var = instruction.getOperandAs<TempVar>(6);
					src_offset = getStackOffsetFromTempVar(temp_var);
				} else if (instruction.isOperandType<std::string_view>(6)) {
					auto rvalue_var_name = instruction.getOperandAs<std::string_view>(6);
					auto src_it = current_scope.identifier_offset.find(rvalue_var_name);
					assert(src_it != current_scope.identifier_offset.end());
					src_offset = src_it->second;
				}

				if (auto src_reg = regAlloc.tryGetStackVariableRegister(src_offset); src_reg.has_value()) {
					// Source value is already in a register (e.g., from function return)
					// For struct types, we need to store the entire value to the stack
					// For other types, we can keep it in a register
					auto size_in_bits = instruction.getOperandAs<int>(1);
					int size_bytes = size_in_bits / 8;

					if (var_type == Type::Struct) {
						// For structs, store directly from source register to destination stack location
						// This handles the x64 Windows calling convention where small structs (≤8 bytes)
						// are returned in RAX and need to be stored to the stack for member access
						auto store_opcodes = generateMovToFrame(src_reg.value(), dst_offset);
						textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
						                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
						// Release the allocated register since we're not using it
						regAlloc.release(allocated_reg.reg);
					} else {
						// For non-struct types, move to allocated register
						auto mov_opcodes = regAlloc.get_reg_reg_move_op_code(allocated_reg.reg, src_reg.value(), size_bytes);
						textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(),
						                       mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					}
				} else {
					auto load_opcodes = generateMovFromFrame(allocated_reg.reg, src_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				}
			} // end else (not literal)
		} // end if (is_initialized)

		// Add debug information for the local variable
		if (!current_function_name_.empty()) {
			uint32_t type_index;
			switch (var_type) {
				case Type::Int: type_index = 0x74; break;
				case Type::Float: type_index = 0x40; break;
				case Type::Double: type_index = 0x41; break;
				case Type::Char: type_index = 0x10; break;
				case Type::Bool: type_index = 0x30; break;
				default: type_index = 0x74; break;
			}

			std::vector<CodeView::VariableLocation> locations;
			uint32_t start_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;

			if (is_initialized) {
				CodeView::VariableLocation loc;
				loc.type = CodeView::VariableLocation::REGISTER;
				loc.offset = 0;
				loc.start_offset = start_offset;
				loc.length = 100; // Placeholder until lifetime analysis is implemented
				loc.register_code = getX64RegisterCodeViewCode(allocated_reg_val);
				locations.push_back(loc);
			} else {
				CodeView::VariableLocation loc;
				loc.type = CodeView::VariableLocation::STACK_RELATIVE;
				loc.offset = var_it->second;
				loc.start_offset = start_offset;
				loc.length = 100; // Placeholder
				loc.register_code = 0;
				locations.push_back(loc);
			}

			uint16_t flags = 0;
			writer.add_local_variable(std::string(var_name), type_index, flags, locations);
		}
	}

	uint16_t getX64RegisterCodeViewCode(X64Register reg) {
		switch (reg) {
			case X64Register::RAX: return 0;
			case X64Register::RCX: return 1;
			case X64Register::RDX: return 2;
			case X64Register::RBX: return 3;
			case X64Register::RSP: return 4;
			case X64Register::RBP: return 5;
			case X64Register::RSI: return 6;
			case X64Register::RDI: return 7;
			case X64Register::R8: return 8;
			case X64Register::R9: return 9;
			case X64Register::R10: return 10;
			case X64Register::R11: return 11;
			case X64Register::R12: return 12;
			case X64Register::R13: return 13;
			case X64Register::R14: return 14;
			case X64Register::R15: return 15;
			// XMM registers (SSE/AVX)
			case X64Register::XMM0: return 154;  // CV_AMD64_XMM0
			case X64Register::XMM1: return 155;  // CV_AMD64_XMM1
			case X64Register::XMM2: return 156;  // CV_AMD64_XMM2
			case X64Register::XMM3: return 157;  // CV_AMD64_XMM3
			default: assert(false && "Unsupported X64Register");
		}
	}

	void handleFunctionDecl(const IrInstruction& instruction) {
		auto func_name = instruction.getOperandAs<std::string_view>(2);

		// Extract function signature information for proper C++20 name mangling
		auto return_type = instruction.getOperandAs<Type>(0);
		std::vector<Type> parameter_types;

		// Extract parameter types from the instruction
		size_t paramIndex = 3;  // Start after return type, size, and function name
		while (paramIndex + 2 < instruction.getOperandCount()) {  // Need at least type, size, and name
			auto param_type = instruction.getOperandAs<Type>(paramIndex);
			parameter_types.push_back(param_type);
			paramIndex += 3;  // Skip type, size, and name
		}

		// Add function signature to the object file writer for proper mangling
		writer.addFunctionSignature(std::string(func_name), return_type, parameter_types);

		// Finalize previous function before starting new one
		if (!current_function_name_.empty()) {
			uint32_t function_length = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;

			// Update function length
			writer.update_function_length(current_function_name_, function_length);
			writer.set_function_debug_range(current_function_name_, 0, 0);	// doesn't seem needed

			// Add exception handling information (required for x64) - once per function
			writer.add_function_exception_info(current_function_name_, current_function_offset_, function_length);
		}

		// align the function to 16 bytes
		static constexpr char nop = static_cast<char>(0x90);
		const uint32_t nop_count = 16 - (textSectionData.size() % 16);
		if (nop_count < 16)
			textSectionData.insert(textSectionData.end(), nop_count, nop);

		// Function debug info is now added in add_function_symbol() with length 0
		StackVariableScope& var_scope = variable_scopes.emplace_back();
		const auto func_stack_space = calculateFunctionStackSpace(func_name, var_scope);
		uint32_t total_stack_space = func_stack_space.named_vars_size + func_stack_space.shadow_stack_space + func_stack_space.temp_vars_size;
		// Ensure stack alignment to 16 bytes
		total_stack_space = (total_stack_space + 15) & -16;

		uint32_t func_offset = static_cast<uint32_t>(textSectionData.size());
		writer.add_function_symbol(std::string(func_name), func_offset, total_stack_space);
		functionSymbols[func_name] = func_offset;

		// Track function for debug information
		current_function_name_ = std::string(func_name);
		current_function_offset_ = func_offset;

		// Clear control flow tracking for new function
		label_positions_.clear();
		pending_branches_.clear();

		// Set up debug information for this function
		// For now, use file ID 0 (first source file)
		writer.set_current_function_for_debug(std::string(func_name), 0);

		// Add line mapping for function declaration (now that current function is set)
		if (instruction.getLineNumber() > 0) {
			// Also add line mapping for function opening brace (next line)
			addLineMapping(instruction.getLineNumber() + 1);
		}

		// Create a new function scope
		regAlloc.reset();

		// MSVC-style prologue: push rbp; mov rbp, rsp; sub rsp, total_stack_space
		if (total_stack_space > 0) {

			textSectionData.push_back(0x55); // push rbp
			textSectionData.push_back(0x48); textSectionData.push_back(0x8B); textSectionData.push_back(0xEC); // mov rbp, rsp

			// Generate stack allocation instruction
			if (total_stack_space <= 127) {
				// Use 8-bit immediate: sub rsp, imm8
				textSectionData.push_back(0x48); textSectionData.push_back(0x83); textSectionData.push_back(0xEC);
				textSectionData.push_back(static_cast<uint8_t>(total_stack_space));
			} else {
				// Use 32-bit immediate: sub rsp, imm32
				textSectionData.push_back(0x48); textSectionData.push_back(0x81); textSectionData.push_back(0xEC);
				for (int i = 0; i < 4; ++i) {
					textSectionData.push_back(static_cast<uint8_t>((total_stack_space >> (8 * i)) & 0xFF));
				}
			}
		}

		// For RBP-relative addressing, we start with negative offset after total allocated space
		variable_scopes.back().scope_stack_space = -total_stack_space;

		// Handle parameters
		struct ParameterInfo {
			Type param_type;
			int param_size;
			std::string_view param_name;
			int paramNumber;
			int offset;
			X64Register src_reg;
		};
		std::vector<ParameterInfo> parameters;

		// First pass: collect all parameter information
		paramIndex = 3;  // Start after return type, size, and function name
		while (paramIndex + 2 < instruction.getOperandCount()) {  // Need at least type, size, and name
			auto param_type = instruction.getOperandAs<Type>(paramIndex);
			auto param_size = instruction.getOperandAs<int>(paramIndex + 1);

			// Store parameter location based on addressing mode
			int paramNumber = paramIndex / 3 - 1;
			int offset = (paramNumber + 1) * -8;

			auto param_name = instruction.getOperandAs<std::string_view>(paramIndex + 2);
			variable_scopes.back().identifier_offset[param_name] = offset;

			// Add parameter to debug information
			uint32_t param_type_index = 0x74; // T_INT4 for int parameters
			switch (param_type) {
				case Type::Int: param_type_index = 0x74; break;  // T_INT4
				case Type::Float: param_type_index = 0x40; break; // T_REAL32
				case Type::Double: param_type_index = 0x41; break; // T_REAL64
				case Type::Char: param_type_index = 0x10; break;  // T_CHAR
				case Type::Bool: param_type_index = 0x30; break;  // T_BOOL08
				default: param_type_index = 0x74; break;
			}
			writer.add_function_parameter(std::string(param_name), param_type_index, offset);

			if (paramNumber < INT_PARAM_REGS.size()) {
				X64Register src_reg = INT_PARAM_REGS[paramNumber];
				regAlloc.allocateSpecific(src_reg, offset);

				// Store parameter info for later processing
				parameters.push_back({param_type, param_size, param_name, paramNumber, offset, src_reg});
			}

			paramIndex += 3;  // Skip type, size, and name
		}

		// Second pass: generate parameter storage code in the correct order
		
		// Standard order for other functions
		for (const auto& param : parameters) {
			// MSVC-STYLE: Store parameters using RBP-relative addressing
			auto mov_opcodes = generateMovToFrame(param.src_reg, param.offset);
			textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
		}
	}

	void handleReturn(const IrInstruction& instruction) {
		// Add line mapping for the return statement itself (only for functions without function calls)
		// For functions with function calls (like main), the closing brace is already mapped in handleFunctionCall
		if (instruction.getLineNumber() > 0 && current_function_name_ != "main") {
			addLineMapping(instruction.getLineNumber());
		}

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
			// Handle temporary variable (stored on stack)
			auto size_it_bits = instruction.getOperandAs<int>(1);
			auto return_var = instruction.getOperandAs<TempVar>(2);

			// Load the temporary variable from stack to RAX
			auto temp_var_name = return_var.name();
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.identifier_offset.find(temp_var_name);
			if (it != current_scope.identifier_offset.end()) {
				int var_offset = it->second;
				if (auto reg_var = regAlloc.tryGetStackVariableRegister(var_offset); reg_var.has_value()) {
					if (reg_var.value() != X64Register::RAX) {
						auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, reg_var.value(), 4);	// TODO: reg size
						textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
					}
				}
				else {
					// Load from stack using RBP-relative addressing
					auto load_opcodes = generateMovFromFrame(X64Register::RAX, var_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(X64Register::RAX);
				}
			} else {
				// Temporary variable not found in stack. This can happen in a few scenarios:
				// 1. Function call result is immediately returned (value still in RAX)
				// 2. TempVar was never actually stored (shouldn't happen with our current logic)
				// 3. There's a bug in our TempVar indexing

				// For now, we'll assume the value is still in RAX from the most recent operation
				// This is a reasonable assumption for simple cases like "return function_call()"
				// where the function call result goes directly to the return without intermediate operations

				// In a more sophisticated compiler, we'd track register liveness and know
				// whether the value is still in RAX or needs to be loaded from somewhere else
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
				int var_offset = it->second;
				if (auto stackRegister = regAlloc.tryGetStackVariableRegister(var_offset); stackRegister.has_value()) {
					if (stackRegister.value() != X64Register::RAX) {	// Value is already in register, move if it's not in RAX already
						regAlloc.get_reg_reg_move_op_code(X64Register::RAX, stackRegister.value(), 4);	// TODO: What size should we use here?
					}
				}
				else {
					assert(variable_scopes.back().scope_stack_space <= var_offset);
					// Load from stack using RBP-relative addressing
					auto load_opcodes = generateMovFromFrame(X64Register::RAX, var_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(X64Register::RAX);
				}
			}
		}

		// MSVC-style epilogue
		int32_t total_stack_space = variable_scopes.back().scope_stack_space;

		if (total_stack_space != 0) {
			// Function had a prologue, use MSVC-style epilogue
			// mov rsp, rbp (restore stack pointer)
			textSectionData.push_back(0x48);
			textSectionData.push_back(0x89);
			textSectionData.push_back(0xEC);

			// pop rbp (restore caller's base pointer)
			textSectionData.push_back(0x5D);
		}

		// ret (return to caller)
		textSectionData.push_back(0xC3);
	}

	void handleStackAlloc(const IrInstruction& instruction) {
		assert(false && "Not implemented");
		// Get the size of the allocation
		/*auto sizeInBytes = instruction.getOperandAs<int>(1) / 8;

		// Ensure the stack remains aligned to 16 bytes
		sizeInBytes = (sizeInBytes + 15) & -16;

		// Generate the opcode for `sub rsp, imm32`
		std::array<uint8_t, 7> subRspInst = { 0x48, 0x81, 0xEC };
		std::memcpy(subRspInst.data() + 3, &sizeInBytes, sizeof(sizeInBytes));

		// Add the instruction to the .text section
		textSectionData.insert(textSectionData.end(), subRspInst.begin(), subRspInst.end());

		// Add the identifier and its stack offset to the current scope
		// With RBP-relative addressing, local variables use NEGATIVE offsets
		StackVariableScope& current_scope = variable_scopes.back();
		current_scope.current_stack_offset -= sizeInBytes;  // Move to next slot (going more negative)
		int stack_offset = current_scope.current_stack_offset;
		current_scope.identifier_offset[instruction.getOperandAs<std::string_view>(2)] = stack_offset;*/
	}

	void handleStore(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() >= 4);  // type, size, dest, src

		auto var_name = instruction.getOperandAs<std::string_view>(2);
		auto src_reg = static_cast<X64Register>(instruction.getOperandAs<int>(3));

		// Find the variable's stack offset
		const StackVariableScope& current_scope = variable_scopes.back();
		auto it = current_scope.identifier_offset.find(var_name);
		if (it != current_scope.identifier_offset.end()) {
			// Store to stack using RBP-relative addressing
			int32_t offset = it->second;
			auto store_opcodes = generateMovToFrame(src_reg, offset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(), store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
		}
	}

	void handleAdd(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "addition");

		// Perform the addition operation
		std::array<uint8_t, 3> addInst = { 0x48, 0x01, 0xC0 }; // add r/m64, r64. 0x01 is opcode, 0xC0 is ModR/M for reg, reg
		addInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), addInst.begin(), addInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleSubtract(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "subtraction");

		// Perform the subtraction operation
		std::array<uint8_t, 3> subInst = { 0x48, 0x29, 0xC0 }; // sub r/m64, r64. 0x29 is opcode, 0xC0 is ModR/M for reg, reg
		subInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), subInst.begin(), subInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleMultiply(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "multiplication");

		// Perform the multiplication operation
		std::array<uint8_t, 4> mulInst = { 0x48, 0x0F, 0xAF, 0x00 }; // REX.W (0x48) + Opcode (0x0F 0xAF) + ModR/M (initially 0x00)
		// ModR/M: Mod=11 (register-to-register), Reg=result_physical_reg, R/M=rhs_physical_reg
		mulInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
		textSectionData.insert(textSectionData.end(), mulInst.begin(), mulInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleDivide(const IrInstruction& instruction) {
		flushAllDirtyRegisters();	// we do this so that RDX is free to use

		regAlloc.release(X64Register::RAX);
		regAlloc.allocateSpecific(X64Register::RAX, INT_MIN);

		regAlloc.release(X64Register::RDX);
		regAlloc.allocateSpecific(X64Register::RDX, INT_MIN);

		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "division");

		// Division requires special handling: dividend must be in RAX
		// Move result_physical_reg to RAX (dividend must be in RAX for idiv)
		auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, ctx.result_physical_reg, ctx.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);

		// Sign extend RAX into RDX:RAX (CQO for 64-bit)
		if (ctx.size_in_bits == 64) {
			// CQO - sign extend RAX into RDX:RAX (fills RDX with 0 or -1)
			std::array<uint8_t, 2> cqoInst = { 0x48, 0x99 }; // REX.W + CQO
			textSectionData.insert(textSectionData.end(), cqoInst.begin(), cqoInst.end());
		} else {
			// CDQ - sign extend EAX into EDX:EAX (for 32-bit)
			std::array<uint8_t, 1> cdqInst = { 0x99 };
			textSectionData.insert(textSectionData.end(), cdqInst.begin(), cdqInst.end());
		}

	    // idiv rhs_physical_reg
		uint8_t rex = 0x40; // Base REX prefix
		if (ctx.size_in_bits == 64) {
			rex |= 0x08; // Set REX.W for 64-bit operation
		}

		// Check if we need REX.B for the divisor register
		if (static_cast<uint8_t>(ctx.rhs_physical_reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex |= 0x01; // Set REX.B
		}

		std::array<uint8_t, 3> divInst = {
			rex,
			0xF7,  // Opcode for IDIV
			static_cast<uint8_t>(0xF8 + (static_cast<uint8_t>(ctx.rhs_physical_reg) & 0x07))  // ModR/M: 11 111 reg (opcode extension 7 for IDIV)
		};
		textSectionData.insert(textSectionData.end(), divInst.begin(), divInst.end());

		// Store the result from RAX (quotient) to the appropriate destination
		storeArithmeticResult(ctx, X64Register::RAX);

		regAlloc.release(X64Register::RDX);
	}

	void handleShiftLeft(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift left");

		// Shift operations require the shift count to be in CL (lower 8 bits of RCX)
		// Move rhs_physical_reg to RCX
		auto movRhsToCx = regAlloc.get_reg_reg_move_op_code(X64Register::RCX, ctx.rhs_physical_reg, ctx.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movRhsToCx.op_codes.begin(), movRhsToCx.op_codes.begin() + movRhsToCx.size_in_bytes);

		// Perform the shift left operation: shl r/m64, cl
		std::array<uint8_t, 3> shlInst = { 0x48, 0xD3, 0x00 }; // REX.W (0x48) + Opcode (0xD3) + ModR/M (initially 0x00)
		// ModR/M: Mod=11 (register-to-register), Reg=4 (opcode extension for shl), R/M=result_physical_reg
		shlInst[2] = 0xC0 + (0x04 << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), shlInst.begin(), shlInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleShiftRight(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift right");

		// Shift operations require the shift count to be in CL (lower 8 bits of RCX)
		// Move rhs_physical_reg to RCX
		auto movRhsToCx = regAlloc.get_reg_reg_move_op_code(X64Register::RCX, ctx.rhs_physical_reg, ctx.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movRhsToCx.op_codes.begin(), movRhsToCx.op_codes.begin() + movRhsToCx.size_in_bytes);

		// Perform the shift right operation: sar r/m64, cl (arithmetic right shift)
		// Note: Using SAR (arithmetic) instead of SHR (logical) to preserve sign for signed integers
		std::array<uint8_t, 3> sarInst = { 0x48, 0xD3, 0x00 }; // REX.W (0x48) + Opcode (0xD3) + ModR/M (initially 0x00)
		// ModR/M: Mod=11 (register-to-register), Reg=7 (opcode extension for sar), R/M=result_physical_reg
		sarInst[2] = 0xC0 + (0x07 << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), sarInst.begin(), sarInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleUnsignedDivide(const IrInstruction& instruction) {
		regAlloc.allocateSpecific(X64Register::RAX, INT_MIN);
		regAlloc.allocateSpecific(X64Register::RDX, INT_MIN);

		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned division");

		// Division requires special handling: dividend must be in RAX
		// Move result_physical_reg to RAX (dividend must be in RAX for div)
		auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, ctx.result_physical_reg, ctx.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);

		// xor edx, edx - clear upper 32 bits of dividend for unsigned division
		std::array<uint8_t, 2> xorEdxInst = { 0x31, 0xD2 };
		textSectionData.insert(textSectionData.end(), xorEdxInst.begin(), xorEdxInst.end());

		// div rhs_physical_reg (unsigned division)
		std::array<uint8_t, 3> divInst = { 0x48, 0xF7, 0x00 }; // REX.W (0x48) + Opcode (0xF7) + ModR/M (initially 0x00)
		// ModR/M: Mod=11 (register-to-register), Reg=6 (opcode extension for div), R/M=rhs_physical_reg
		divInst[2] = 0xC0 + (0x06 << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
		textSectionData.insert(textSectionData.end(), divInst.begin(), divInst.end());

		// Store the result from RAX (quotient) to the appropriate destination
		storeArithmeticResult(ctx, X64Register::RAX);
	}

	void handleUnsignedShiftRight(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned shift right");

		// Shift operations require the shift count to be in CL (lower 8 bits of RCX)
		// Move rhs_physical_reg to RCX
		auto movRhsToCx = regAlloc.get_reg_reg_move_op_code(X64Register::RCX, ctx.rhs_physical_reg, ctx.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movRhsToCx.op_codes.begin(), movRhsToCx.op_codes.begin() + movRhsToCx.size_in_bytes);

		// Perform the unsigned shift right operation: shr r/m64, cl (logical right shift)
		// Note: Using SHR (logical) instead of SAR (arithmetic) for unsigned integers
		std::array<uint8_t, 3> shrInst = { 0x48, 0xD3, 0x00 }; // REX.W (0x48) + Opcode (0xD3) + ModR/M (initially 0x00)
		// ModR/M: Mod=11 (register-to-register), Reg=5 (opcode extension for shr), R/M=result_physical_reg
		shrInst[2] = 0xC0 + (0x05 << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), shrInst.begin(), shrInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseAnd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise AND");

		// Perform the bitwise AND operation
		std::array<uint8_t, 3> andInst = { 0x48, 0x21, 0xC0 }; // and r/m64, r64. 0x21 is opcode, 0xC0 is ModR/M for reg, reg
		andInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), andInst.begin(), andInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseOr(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise OR");

		// Perform the bitwise OR operation
		std::array<uint8_t, 3> orInst = { 0x48, 0x09, 0xC0 }; // or r/m64, r64. 0x09 is opcode, 0xC0 is ModR/M for reg, reg
		orInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), orInst.begin(), orInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseXor(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise XOR");

		// Perform the bitwise XOR operation
		std::array<uint8_t, 3> xorInst = { 0x48, 0x31, 0xC0 }; // xor r/m64, r64. 0x31 is opcode, 0xC0 is ModR/M for reg, reg
		xorInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), xorInst.begin(), xorInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleModulo(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "modulo");

		// For x86-64, modulo is implemented using division
		// idiv instruction computes both quotient (RAX) and remainder (RDX)
		// We need the remainder in RDX

		// Move dividend to RAX, divisor to a register
		// Sign extend RAX to RDX:RAX for signed division
		std::array<uint8_t, 3> cqoInst = { 0x48, 0x99, 0x00 }; // cqo (sign extend RAX to RDX:RAX)
		textSectionData.insert(textSectionData.end(), cqoInst.begin(), cqoInst.end() - 1);

		// Perform signed division: idiv r/m64
		std::array<uint8_t, 3> idivInst = { 0x48, 0xF7, 0xF8 }; // idiv r64
		idivInst[2] = 0xF8 + static_cast<uint8_t>(ctx.rhs_physical_reg);
		textSectionData.insert(textSectionData.end(), idivInst.begin(), idivInst.end());

		// Move remainder from RDX to result register
		if (ctx.result_physical_reg != X64Register::RDX) {
			std::array<uint8_t, 3> movInst = { 0x48, 0x89, 0xD0 }; // mov r64, rdx
			movInst[2] = 0xD0 + static_cast<uint8_t>(ctx.result_physical_reg);
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "equal comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on zero flag: sete r8
		std::array<uint8_t, 3> seteInst = { 0x0F, 0x94, 0xC0 }; // sete r8
		seteInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), seteInst.begin(), seteInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleNotEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "not equal comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on zero flag: setne r8
		std::array<uint8_t, 3> setneInst = { 0x0F, 0x95, 0xC0 }; // setne r8
		setneInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setneInst.begin(), setneInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLessThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "less than comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on sign flag: setl r8 (signed less than)
		std::array<uint8_t, 3> setlInst = { 0x0F, 0x9C, 0xC0 }; // setl r8
		setlInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setlInst.begin(), setlInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLessEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "less than or equal comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on flags: setle r8 (signed less than or equal)
		std::array<uint8_t, 3> setleInst = { 0x0F, 0x9E, 0xC0 }; // setle r8
		setleInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setleInst.begin(), setleInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleGreaterThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "greater than comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on flags: setg r8 (signed greater than)
		std::array<uint8_t, 3> setgInst = { 0x0F, 0x9F, 0xC0 }; // setg r8
		setgInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setgInst.begin(), setgInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleGreaterEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "greater than or equal comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on flags: setge r8 (signed greater than or equal)
		std::array<uint8_t, 3> setgeInst = { 0x0F, 0x9D, 0xC0 }; // setge r8
		setgeInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setgeInst.begin(), setgeInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleUnsignedLessThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned less than comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on carry flag: setb r8 (unsigned less than)
		std::array<uint8_t, 3> setbInst = { 0x0F, 0x92, 0xC0 }; // setb r8
		setbInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setbInst.begin(), setbInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleUnsignedLessEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned less than or equal comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on flags: setbe r8 (unsigned less than or equal)
		std::array<uint8_t, 3> setbeInst = { 0x0F, 0x96, 0xC0 }; // setbe r8
		setbeInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setbeInst.begin(), setbeInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleUnsignedGreaterThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned greater than comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on flags: seta r8 (unsigned greater than)
		std::array<uint8_t, 3> setaInst = { 0x0F, 0x97, 0xC0 }; // seta r8
		setaInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setaInst.begin(), setaInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleUnsignedGreaterEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned greater than or equal comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on flags: setae r8 (unsigned greater than or equal)
		std::array<uint8_t, 3> setaeInst = { 0x0F, 0x93, 0xC0 }; // setae r8
		setaeInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setaeInst.begin(), setaeInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLogicalAnd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "logical AND");

		// For logical AND, we need to implement short-circuit evaluation
		// For now, implement as bitwise AND on boolean values
		std::array<uint8_t, 3> andInst = { 0x48, 0x21, 0xC0 }; // and r/m64, r64
		andInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), andInst.begin(), andInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLogicalOr(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "logical OR");

		// For logical OR, we need to implement short-circuit evaluation
		// For now, implement as bitwise OR on boolean values
		std::array<uint8_t, 3> orInst = { 0x48, 0x09, 0xC0 }; // or r/m64, r64
		orInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), orInst.begin(), orInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLogicalNot(const IrInstruction& instruction) {
		// Logical NOT instruction has 4 operands: result_var, operand_type, operand_size, operand_val
		assert(instruction.getOperandCount() == 4 && "LogicalNot instruction must have exactly 4 operands");

		Type type = instruction.getOperandAs<Type>(1);
		int size_in_bits = instruction.getOperandAs<int>(2);

		// Allocate a register for the result
		X64Register result_physical_reg = X64Register::Count;

		// Load the operand into the result register
		if (instruction.isOperandType<std::string_view>(3)) {
			auto operand_var = instruction.getOperandAs<std::string_view>(3);
			auto var_id = variable_scopes.back().identifier_offset.find(operand_var);
			if (var_id != variable_scopes.back().identifier_offset.end()) {
				if (auto var_reg = regAlloc.tryGetStackVariableRegister(var_id->second); var_reg.has_value()) {
					result_physical_reg = var_reg.value();
				} else {
					result_physical_reg = regAlloc.allocate().reg;
					auto mov_opcodes = generateMovFromFrame(result_physical_reg, var_id->second);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(result_physical_reg);
				}
			}
		} else if (instruction.isOperandType<TempVar>(3)) {
			auto temp_var = instruction.getOperandAs<TempVar>(3);
			auto stack_var_addr = getStackOffsetFromTempVar(temp_var);
			if (auto var_reg = regAlloc.tryGetStackVariableRegister(stack_var_addr); var_reg.has_value()) {
				result_physical_reg = var_reg.value();
			} else {
				result_physical_reg = regAlloc.allocate().reg;
				auto mov_opcodes = generateMovFromFrame(result_physical_reg, stack_var_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				regAlloc.flushSingleDirtyRegister(result_physical_reg);
			}
		}

		// Compare with 0: cmp reg, 0
		std::array<uint8_t, 4> cmpInst = { 0x48, 0x83, 0xF8, 0x00 }; // cmp r64, imm8
		cmpInst[2] = 0xF8 + static_cast<uint8_t>(result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result to 1 if zero (sete), 0 otherwise
		std::array<uint8_t, 3> seteInst = { 0x0F, 0x94, 0xC0 }; // sete r8
		seteInst[2] = 0xC0 + static_cast<uint8_t>(result_physical_reg);
		textSectionData.insert(textSectionData.end(), seteInst.begin(), seteInst.end());

		// Store the result
		storeUnaryResult(instruction.getOperand(0), result_physical_reg, size_in_bits);
	}

	void handleBitwiseNot(const IrInstruction& instruction) {
		// Bitwise NOT instruction has 4 operands: result_var, operand_type, operand_size, operand_val
		assert(instruction.getOperandCount() == 4 && "BitwiseNot instruction must have exactly 4 operands");

		Type type = instruction.getOperandAs<Type>(1);
		int size_in_bits = instruction.getOperandAs<int>(2);

		// Allocate a register for the result
		X64Register result_physical_reg = X64Register::Count;

		// Load the operand into the result register
		if (instruction.isOperandType<std::string_view>(3)) {
			auto operand_var = instruction.getOperandAs<std::string_view>(3);
			auto var_id = variable_scopes.back().identifier_offset.find(operand_var);
			if (var_id != variable_scopes.back().identifier_offset.end()) {
				if (auto var_reg = regAlloc.tryGetStackVariableRegister(var_id->second); var_reg.has_value()) {
					result_physical_reg = var_reg.value();
				} else {
					result_physical_reg = regAlloc.allocate().reg;
					auto mov_opcodes = generateMovFromFrame(result_physical_reg, var_id->second);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(result_physical_reg);
				}
			}
		} else if (instruction.isOperandType<TempVar>(3)) {
			auto temp_var = instruction.getOperandAs<TempVar>(3);
			auto stack_var_addr = getStackOffsetFromTempVar(temp_var);
			if (auto var_reg = regAlloc.tryGetStackVariableRegister(stack_var_addr); var_reg.has_value()) {
				result_physical_reg = var_reg.value();
			} else {
				result_physical_reg = regAlloc.allocate().reg;
				auto mov_opcodes = generateMovFromFrame(result_physical_reg, stack_var_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				regAlloc.flushSingleDirtyRegister(result_physical_reg);
			}
		}

		// NOT instruction: not r64
		std::array<uint8_t, 3> notInst = { 0x48, 0xF7, 0xD0 }; // not r64
		notInst[2] = 0xD0 + static_cast<uint8_t>(result_physical_reg);
		textSectionData.insert(textSectionData.end(), notInst.begin(), notInst.end());

		// Store the result
		storeUnaryResult(instruction.getOperand(0), result_physical_reg, size_in_bits);
	}

	void handleNegate(const IrInstruction& instruction) {
		// Negate instruction has 4 operands: result_var, operand_type, operand_size, operand_val
		assert(instruction.getOperandCount() == 4 && "Negate instruction must have exactly 4 operands");

		Type type = instruction.getOperandAs<Type>(1);
		int size_in_bits = instruction.getOperandAs<int>(2);

		// Allocate a register for the result
		X64Register result_physical_reg = X64Register::Count;

		// Load the operand into the result register
		if (instruction.isOperandType<std::string_view>(3)) {
			auto operand_var = instruction.getOperandAs<std::string_view>(3);
			auto var_id = variable_scopes.back().identifier_offset.find(operand_var);
			if (var_id != variable_scopes.back().identifier_offset.end()) {
				if (auto var_reg = regAlloc.tryGetStackVariableRegister(var_id->second); var_reg.has_value()) {
					result_physical_reg = var_reg.value();
				} else {
					result_physical_reg = regAlloc.allocate().reg;
					auto mov_opcodes = generateMovFromFrame(result_physical_reg, var_id->second);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(result_physical_reg);
				}
			}
		} else if (instruction.isOperandType<TempVar>(3)) {
			auto temp_var = instruction.getOperandAs<TempVar>(3);
			auto stack_var_addr = getStackOffsetFromTempVar(temp_var);
			if (auto var_reg = regAlloc.tryGetStackVariableRegister(stack_var_addr); var_reg.has_value()) {
				result_physical_reg = var_reg.value();
			} else {
				result_physical_reg = regAlloc.allocate().reg;
				auto mov_opcodes = generateMovFromFrame(result_physical_reg, stack_var_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				regAlloc.flushSingleDirtyRegister(result_physical_reg);
			}
		}

		// NEG instruction: neg r64
		std::array<uint8_t, 3> negInst = { 0x48, 0xF7, 0xD8 }; // neg r64
		negInst[2] = 0xD8 + static_cast<uint8_t>(result_physical_reg);
		textSectionData.insert(textSectionData.end(), negInst.begin(), negInst.end());

		// Store the result
		storeUnaryResult(instruction.getOperand(0), result_physical_reg, size_in_bits);
	}

	void storeUnaryResult(const IrOperand& result_operand, X64Register result_physical_reg, int size_in_bits) {
		if (std::holds_alternative<TempVar>(result_operand)) {
			auto result_var = std::get<TempVar>(result_operand);
			auto result_stack_var_addr = getStackOffsetFromTempVar(result_var);
			if (auto res_reg = regAlloc.tryGetStackVariableRegister(result_stack_var_addr); res_reg.has_value()) {
				if (res_reg != result_physical_reg) {
					auto moveOp = regAlloc.get_reg_reg_move_op_code(res_reg.value(), result_physical_reg, size_in_bits / 8);
					textSectionData.insert(textSectionData.end(), moveOp.op_codes.begin(), moveOp.op_codes.begin() + moveOp.size_in_bytes);
				}
			} else {
				auto mov_opcodes = generateMovToFrame(result_physical_reg, result_stack_var_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
			}
		} else if (std::holds_alternative<std::string_view>(result_operand)) {
			auto result_var_name = std::get<std::string_view>(result_operand);
			auto var_id = variable_scopes.back().identifier_offset.find(result_var_name);
			if (var_id != variable_scopes.back().identifier_offset.end()) {
				auto store_opcodes = generateMovToFrame(result_physical_reg, var_id->second);
				textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(), store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
			}
		}
	}

	void handleFloatAdd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point addition");

		// Use SSE addss (scalar single-precision) or addsd (scalar double-precision)
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// addss xmm_dst, xmm_src (F3 0F 58 /r)
			std::array<uint8_t, 4> addssInst = { 0xF3, 0x0F, 0x58, 0xC0 };
			addssInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), addssInst.begin(), addssInst.end());
		} else if (type == Type::Double) {
			// addsd xmm_dst, xmm_src (F2 0F 58 /r)
			std::array<uint8_t, 4> addsdInst = { 0xF2, 0x0F, 0x58, 0xC0 };
			addsdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), addsdInst.begin(), addsdInst.end());
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatSubtract(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point subtraction");

		// Use SSE subss (scalar single-precision) or subsd (scalar double-precision)
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// subss xmm_dst, xmm_src (F3 0F 5C /r)
			std::array<uint8_t, 4> subssInst = { 0xF3, 0x0F, 0x5C, 0xC0 };
			subssInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), subssInst.begin(), subssInst.end());
		} else if (type == Type::Double) {
			// subsd xmm_dst, xmm_src (F2 0F 5C /r)
			std::array<uint8_t, 4> subsdInst = { 0xF2, 0x0F, 0x5C, 0xC0 };
			subsdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), subsdInst.begin(), subsdInst.end());
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatMultiply(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point multiplication");

		// Use SSE mulss (scalar single-precision) or mulsd (scalar double-precision)
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// mulss xmm_dst, xmm_src (F3 0F 59 /r)
			std::array<uint8_t, 4> mulssInst = { 0xF3, 0x0F, 0x59, 0xC0 };
			mulssInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), mulssInst.begin(), mulssInst.end());
		} else if (type == Type::Double) {
			// mulsd xmm_dst, xmm_src (F2 0F 59 /r)
			std::array<uint8_t, 4> mulsdInst = { 0xF2, 0x0F, 0x59, 0xC0 };
			mulsdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), mulsdInst.begin(), mulsdInst.end());
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatDivide(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point division");

		// Use SSE divss (scalar single-precision) or divsd (scalar double-precision)
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// divss xmm_dst, xmm_src (F3 0F 5E /r)
			std::array<uint8_t, 4> divssInst = { 0xF3, 0x0F, 0x5E, 0xC0 };
			divssInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), divssInst.begin(), divssInst.end());
		} else if (type == Type::Double) {
			// divsd xmm_dst, xmm_src (F2 0F 5E /r)
			std::array<uint8_t, 4> divsdInst = { 0xF2, 0x0F, 0x5E, 0xC0 };
			divsdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), divsdInst.begin(), divsdInst.end());
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point equal comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Set result based on zero flag: sete r8
		std::array<uint8_t, 3> seteInst = { 0x0F, 0x94, 0xC0 };
		seteInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), seteInst.begin(), seteInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatNotEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point not equal comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Set result based on zero flag: setne r8
		std::array<uint8_t, 3> setneInst = { 0x0F, 0x95, 0xC0 };
		setneInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setneInst.begin(), setneInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatLessThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point less than comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Set result based on carry flag: setb r8 (below = less than for floating-point)
		std::array<uint8_t, 3> setbInst = { 0x0F, 0x92, 0xC0 };
		setbInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setbInst.begin(), setbInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatLessEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point less than or equal comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Set result based on flags: setbe r8 (below or equal)
		std::array<uint8_t, 3> setbeInst = { 0x0F, 0x96, 0xC0 };
		setbeInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setbeInst.begin(), setbeInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatGreaterThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point greater than comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Set result based on flags: seta r8 (above = greater than for floating-point)
		std::array<uint8_t, 3> setaInst = { 0x0F, 0x97, 0xC0 };
		setaInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setaInst.begin(), setaInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatGreaterEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point greater than or equal comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Set result based on flags: setae r8 (above or equal)
		std::array<uint8_t, 3> setaeInst = { 0x0F, 0x93, 0xC0 };
		setaeInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setaeInst.begin(), setaeInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleSignExtend(const IrInstruction& instruction) {
		// Sign extension: movsx dest, src
		// For now, implement a simple version that handles common cases

		// Get operands: result_var, from_type, from_size, value, to_size
		Type fromType = instruction.getOperandAs<Type>(1);
		int fromSize = instruction.getOperandAs<int>(2);
		int toSize = instruction.getOperandAs<int>(4);

		// For simplicity, assume we're extending to a register
		// In a full implementation, we'd need to handle different size combinations
		// This is a placeholder that would need proper x86-64 movsx instructions

		// TODO: Implement proper sign extension instructions based on sizes
		// movsx r64, r32 (32->64), movsx r32, r16 (16->32), movsx r32, r8 (8->32), etc.
	}

	void handleZeroExtend(const IrInstruction& instruction) {
		// Zero extension: movzx dest, src
		// For now, implement a simple version that handles common cases

		// Get operands: result_var, from_type, from_size, value, to_size
		Type fromType = instruction.getOperandAs<Type>(1);
		int fromSize = instruction.getOperandAs<int>(2);
		int toSize = instruction.getOperandAs<int>(4);

		// For simplicity, assume we're extending to a register
		// In a full implementation, we'd need to handle different size combinations
		// This is a placeholder that would need proper x86-64 movzx instructions

		// TODO: Implement proper zero extension instructions based on sizes
		// movzx r64, r32 (32->64), movzx r32, r16 (16->32), movzx r32, r8 (8->32), etc.
	}

	void handleTruncate(const IrInstruction& instruction) {
		// Truncation: just use the lower bits
		// For now, implement a simple version that handles common cases

		// Get operands: result_var, from_type, from_size, value, to_size
		Type fromType = instruction.getOperandAs<Type>(1);
		int fromSize = instruction.getOperandAs<int>(2);
		int toSize = instruction.getOperandAs<int>(4);

		// For simplicity, assume we're truncating in a register
		// In a full implementation, we'd need to handle different size combinations
		// This is a placeholder that would need proper x86-64 instructions

		// TODO: Implement proper truncation (often just using smaller register names)
		// e.g., using eax instead of rax for 32-bit, ax for 16-bit, al for 8-bit
	}

	void handleAddAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "add assignment");
		std::array<uint8_t, 3> addInst = { 0x48, 0x01, 0xC0 };
		addInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), addInst.begin(), addInst.end());
		storeArithmeticResult(ctx);
	}

	void handleSubAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "subtract assignment");
		std::array<uint8_t, 3> subInst = { 0x48, 0x29, 0xC0 };
		subInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), subInst.begin(), subInst.end());
		storeArithmeticResult(ctx);
	}

	void handleMulAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "multiply assignment");
		std::array<uint8_t, 4> imulInst = { 0x48, 0x0F, 0xAF, 0xC0 };
		imulInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
		textSectionData.insert(textSectionData.end(), imulInst.begin(), imulInst.end());
		storeArithmeticResult(ctx);
	}

	void handleDivAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "divide assignment");
		std::array<uint8_t, 3> movInst = { 0x48, 0x89, 0xC0 };
		movInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(X64Register::RAX);
		textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		std::array<uint8_t, 3> cqoInst = { 0x48, 0x99 };
		textSectionData.insert(textSectionData.end(), cqoInst.begin(), cqoInst.begin() + 2);
		std::array<uint8_t, 3> idivInst = { 0x48, 0xF7, 0xF8 };
		idivInst[2] = 0xF8 + static_cast<uint8_t>(ctx.rhs_physical_reg);
		textSectionData.insert(textSectionData.end(), idivInst.begin(), idivInst.end());
		std::array<uint8_t, 3> movResultInst = { 0x48, 0x89, 0xC0 };
		movResultInst[2] = 0xC0 + (static_cast<uint8_t>(X64Register::RAX) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), movResultInst.begin(), movResultInst.end());
		storeArithmeticResult(ctx);
	}

	void handleModAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "modulo assignment");
		std::array<uint8_t, 3> movInst = { 0x48, 0x89, 0xC0 };
		movInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(X64Register::RAX);
		textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		std::array<uint8_t, 3> cqoInst = { 0x48, 0x99 };
		textSectionData.insert(textSectionData.end(), cqoInst.begin(), cqoInst.begin() + 2);
		std::array<uint8_t, 3> idivInst = { 0x48, 0xF7, 0xF8 };
		idivInst[2] = 0xF8 + static_cast<uint8_t>(ctx.rhs_physical_reg);
		textSectionData.insert(textSectionData.end(), idivInst.begin(), idivInst.end());
		std::array<uint8_t, 3> movResultInst = { 0x48, 0x89, 0xC0 };
		movResultInst[2] = 0xC0 + (static_cast<uint8_t>(X64Register::RDX) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), movResultInst.begin(), movResultInst.end());
		storeArithmeticResult(ctx);
	}

	void handleAndAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise and assignment");
		std::array<uint8_t, 3> andInst = { 0x48, 0x21, 0xC0 };
		andInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), andInst.begin(), andInst.end());
		storeArithmeticResult(ctx);
	}

	void handleOrAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise or assignment");
		std::array<uint8_t, 3> orInst = { 0x48, 0x09, 0xC0 };
		orInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), orInst.begin(), orInst.end());
		storeArithmeticResult(ctx);
	}

	void handleXorAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise xor assignment");
		std::array<uint8_t, 3> xorInst = { 0x48, 0x31, 0xC0 };
		xorInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), xorInst.begin(), xorInst.end());
		storeArithmeticResult(ctx);
	}

	void handleShlAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift left assignment");
		std::array<uint8_t, 3> movInst = { 0x48, 0x89, 0xC1 };
		movInst[2] = 0xC1 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3);
		textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		std::array<uint8_t, 3> shlInst = { 0x48, 0xD3, 0xE0 };
		shlInst[2] = 0xE0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), shlInst.begin(), shlInst.end());
		storeArithmeticResult(ctx);
	}

	void handleShrAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift right assignment");
		std::array<uint8_t, 3> movInst = { 0x48, 0x89, 0xC1 };
		movInst[2] = 0xC1 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3);
		textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		std::array<uint8_t, 3> sarInst = { 0x48, 0xD3, 0xF8 };
		sarInst[2] = 0xF8 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), sarInst.begin(), sarInst.end());
		storeArithmeticResult(ctx);
	}

	void handleAssignment(const IrInstruction& instruction) {
		// Assignment instruction has 7 operands: [result_var, lhs_type, lhs_size, lhs_value, rhs_type, rhs_size, rhs_value]
		assert(instruction.getOperandCount() == 7 && "Assignment instruction must have exactly 7 operands");

		Type lhs_type = instruction.getOperandAs<Type>(1);
		int lhs_size_bits = instruction.getOperandAs<int>(2);

		// Special handling for struct assignment
		if (lhs_type == Type::Struct) {
			// For struct assignment, we need to copy the entire struct value
			// LHS is the destination (should be a variable name or TempVar)
			// RHS is the source (should be a TempVar from function return, or another variable)

			// Get LHS destination
			const IrOperand& lhs_operand = instruction.getOperand(3);
			int32_t lhs_offset = -1;

			if (std::holds_alternative<std::string_view>(lhs_operand)) {
				std::string_view lhs_var_name = std::get<std::string_view>(lhs_operand);
				auto it = variable_scopes.back().identifier_offset.find(lhs_var_name);
				if (it != variable_scopes.back().identifier_offset.end()) {
					lhs_offset = it->second;
				}
			} else if (std::holds_alternative<TempVar>(lhs_operand)) {
				TempVar lhs_var = std::get<TempVar>(lhs_operand);
				lhs_offset = getStackOffsetFromTempVar(lhs_var);
			}

			if (lhs_offset == -1) {
				assert(false && "LHS variable not found in struct assignment");
				return;
			}

			// Get RHS source
			const IrOperand& rhs_operand = instruction.getOperand(6);
			X64Register source_reg = X64Register::RAX;

			if (std::holds_alternative<std::string_view>(rhs_operand)) {
				std::string_view rhs_var_name = std::get<std::string_view>(rhs_operand);
				auto it = variable_scopes.back().identifier_offset.find(rhs_var_name);
				if (it != variable_scopes.back().identifier_offset.end()) {
					int32_t rhs_offset = it->second;
					// Load struct from RHS stack location into RAX (8 bytes for small structs)
					auto load_opcodes = generateMovFromFrame(source_reg, rhs_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				}
			} else if (std::holds_alternative<TempVar>(rhs_operand)) {
				TempVar rhs_var = std::get<TempVar>(rhs_operand);
				int32_t rhs_offset = getStackOffsetFromTempVar(rhs_var);

				// Check if the value is already in RAX (e.g., from function return)
				if (auto rhs_reg = regAlloc.tryGetStackVariableRegister(rhs_offset); rhs_reg.has_value()) {
					source_reg = rhs_reg.value();
					if (source_reg != X64Register::RAX) {
						// Move from source register to RAX
						auto move_opcodes = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, source_reg, 8);
						textSectionData.insert(textSectionData.end(), move_opcodes.op_codes.begin(),
						                       move_opcodes.op_codes.begin() + move_opcodes.size_in_bytes);
						source_reg = X64Register::RAX;
					}
				} else {
					// Load struct from RHS stack location into RAX (8 bytes for small structs)
					auto load_opcodes = generateMovFromFrame(source_reg, rhs_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				}
			}

			// Store RAX to LHS stack location (8 bytes for small structs)
			auto store_opcodes = generateMovToFrame(source_reg, lhs_offset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
			                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

			return;
		}

		// For non-struct types, use the standard arithmetic operation handling
		auto ctx = setupAndLoadArithmeticOperation(instruction, "assignment");
		// The value is already in the result register from setupAndLoadArithmeticOperation
		// We just need to store it
		storeArithmeticResult(ctx);
	}

	void handleLabel(const IrInstruction& instruction) {
		// Label instruction: mark a position in code for jumps
		assert(instruction.getOperandCount() == 1 && "Label instruction must have exactly 1 operand (label name)");

		auto label_name = instruction.getOperandAs<std::string>(0);

		// Store the current code offset for this label
		uint32_t label_offset = static_cast<uint32_t>(textSectionData.size());

		// Track label positions for later resolution
		if (label_positions_.find(label_name) == label_positions_.end()) {
			label_positions_[label_name] = label_offset;
		}

		// Flush all dirty registers at label boundaries to ensure correct state
		flushAllDirtyRegisters();
	}

	void handleBranch(const IrInstruction& instruction) {
		// Unconditional branch: jmp label
		assert(instruction.getOperandCount() == 1 && "Branch instruction must have exactly 1 operand (label name)");

		auto target_label = instruction.getOperandAs<std::string>(0);

		// Flush all dirty registers before branching
		flushAllDirtyRegisters();

		// Generate JMP instruction (E9 + 32-bit relative offset)
		// We'll use a placeholder offset and fix it up later
		textSectionData.push_back(0xE9); // JMP rel32

		// Store position where we need to patch the offset
		uint32_t patch_position = static_cast<uint32_t>(textSectionData.size());

		// Add placeholder offset (will be patched later)
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Record this branch for later patching
		pending_branches_.push_back({target_label, patch_position});
	}

	void handleLoopBegin(const IrInstruction& instruction) {
		// LoopBegin marks the start of a loop and provides labels for break/continue
		// Operands: [loop_start_label, loop_end_label, loop_increment_label]
		assert(instruction.getOperandCount() == 3 && "LoopBegin must have 3 operands");

		auto loop_start_label = instruction.getOperandAs<std::string>(0);
		auto loop_end_label = instruction.getOperandAs<std::string>(1);
		auto loop_increment_label = instruction.getOperandAs<std::string>(2);

		// Push loop context onto stack for break/continue handling
		loop_context_stack_.push_back({loop_end_label, loop_increment_label});

		// Flush all dirty registers at loop boundaries
		flushAllDirtyRegisters();
	}

	void handleLoopEnd(const IrInstruction& instruction) {
		// LoopEnd marks the end of a loop
		assert(instruction.getOperandCount() == 0 && "LoopEnd must have 0 operands");

		// Pop loop context from stack
		if (!loop_context_stack_.empty()) {
			loop_context_stack_.pop_back();
		}

		// Flush all dirty registers at loop boundaries
		flushAllDirtyRegisters();
	}

	void handleBreak(const IrInstruction& instruction) {
		// Break: unconditional jump to loop end label
		assert(instruction.getOperandCount() == 0 && "Break must have 0 operands");
		assert(!loop_context_stack_.empty() && "Break must be inside a loop");

		auto& loop_context = loop_context_stack_.back();
		auto target_label = loop_context.loop_end_label;

		// Flush all dirty registers before branching
		flushAllDirtyRegisters();

		// Generate JMP instruction to loop end
		textSectionData.push_back(0xE9); // JMP rel32

		// Store position where we need to patch the offset
		uint32_t patch_position = static_cast<uint32_t>(textSectionData.size());

		// Add placeholder offset (will be patched later)
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Record this branch for later patching
		pending_branches_.push_back({target_label, patch_position});
	}

	void handleContinue(const IrInstruction& instruction) {
		// Continue: unconditional jump to loop increment label
		assert(instruction.getOperandCount() == 0 && "Continue must have 0 operands");
		assert(!loop_context_stack_.empty() && "Continue must be inside a loop");

		auto& loop_context = loop_context_stack_.back();
		auto target_label = loop_context.loop_increment_label;

		// Flush all dirty registers before branching
		flushAllDirtyRegisters();

		// Generate JMP instruction to loop increment
		textSectionData.push_back(0xE9); // JMP rel32

		// Store position where we need to patch the offset
		uint32_t patch_position = static_cast<uint32_t>(textSectionData.size());

		// Add placeholder offset (will be patched later)
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Record this branch for later patching
		pending_branches_.push_back({target_label, patch_position});
	}

	void handleArrayAccess(const IrInstruction& instruction) {
		// ArrayAccess: %result = array_access [ArrayType][ElementSize] %array, [IndexType][IndexSize] %index
		// Format: [result_var, array_type, element_size, array_name, index_type, index_size, index_value]
		assert(instruction.getOperandCount() == 7 && "ArrayAccess must have 7 operands");

		auto result_var = instruction.getOperandAs<TempVar>(0);
		auto array_type = instruction.getOperandAs<Type>(1);
		auto element_size_bits = instruction.getOperandAs<int>(2);
		auto index_type = instruction.getOperandAs<Type>(4);
		auto index_size_bits = instruction.getOperandAs<int>(5);

		// Get the array base address (from stack)
		std::string array_name;
		if (instruction.isOperandType<std::string_view>(3)) {
			array_name = std::string(instruction.getOperandAs<std::string_view>(3));
		} else {
			assert(false && "Array must be an identifier for now");
		}

		// Get the index value (could be a constant or a temp var)
		int64_t index_offset = getStackOffsetFromTempVar(result_var);

		// Calculate element size in bytes
		int element_size_bytes = element_size_bits / 8;

		// Load index into a register (RAX)
		if (instruction.isOperandType<unsigned long long>(6)) {
			// Constant index
			uint64_t index_value = instruction.getOperandAs<unsigned long long>(6);

			// Calculate offset directly: base_offset + (index * element_size)
			int64_t array_base_offset = variable_scopes.back().identifier_offset[array_name];
			int64_t element_offset = array_base_offset - (index_value * element_size_bytes);

			// Load from [RBP + offset] into RAX
			// MOV RAX, [RBP + offset]
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x8B); // MOV r64, r/m64

			if (element_offset >= -128 && element_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
				textSectionData.push_back(static_cast<uint8_t>(element_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
				uint32_t offset_u32 = static_cast<uint32_t>(element_offset);
				textSectionData.push_back(offset_u32 & 0xFF);
				textSectionData.push_back((offset_u32 >> 8) & 0xFF);
				textSectionData.push_back((offset_u32 >> 16) & 0xFF);
				textSectionData.push_back((offset_u32 >> 24) & 0xFF);
			}
		} else if (instruction.isOperandType<TempVar>(6)) {
			// Variable index - need to compute address at runtime
			TempVar index_var = instruction.getOperandAs<TempVar>(6);
			int64_t index_var_offset = getStackOffsetFromTempVar(index_var);
			int64_t array_base_offset = variable_scopes.back().identifier_offset[array_name];

			// Load index into RCX: MOV RCX, [RBP + index_offset]
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x8B); // MOV r64, r/m64
			if (index_var_offset >= -128 && index_var_offset <= 127) {
				textSectionData.push_back(0x4D); // ModR/M: [RBP + disp8], RCX
				textSectionData.push_back(static_cast<uint8_t>(index_var_offset));
			} else {
				textSectionData.push_back(0x8D); // ModR/M: [RBP + disp32], RCX
				uint32_t offset_u32 = static_cast<uint32_t>(index_var_offset);
				textSectionData.push_back(offset_u32 & 0xFF);
				textSectionData.push_back((offset_u32 >> 8) & 0xFF);
				textSectionData.push_back((offset_u32 >> 16) & 0xFF);
				textSectionData.push_back((offset_u32 >> 24) & 0xFF);
			}

			// Multiply index by element size: IMUL RCX, element_size
			if (element_size_bytes == 1) {
				// No multiplication needed
			} else if (element_size_bytes == 2 || element_size_bytes == 4 || element_size_bytes == 8) {
				// Use shift for powers of 2
				int shift_amount = (element_size_bytes == 2) ? 1 : (element_size_bytes == 4) ? 2 : 3;
				// SHL RCX, shift_amount
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0xC1); // SHL r/m64, imm8
				textSectionData.push_back(0xE1); // ModR/M: RCX
				textSectionData.push_back(shift_amount);
			} else {
				// General multiplication: IMUL RCX, RCX, element_size
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0x69); // IMUL r64, r/m64, imm32
				textSectionData.push_back(0xC9); // ModR/M: RCX, RCX
				uint32_t size_u32 = static_cast<uint32_t>(element_size_bytes);
				textSectionData.push_back(size_u32 & 0xFF);
				textSectionData.push_back((size_u32 >> 8) & 0xFF);
				textSectionData.push_back((size_u32 >> 16) & 0xFF);
				textSectionData.push_back((size_u32 >> 24) & 0xFF);
			}

			// Load array base address into RAX: LEA RAX, [RBP + array_base_offset]
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x8D); // LEA r64, m
			if (array_base_offset >= -128 && array_base_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
				textSectionData.push_back(static_cast<uint8_t>(array_base_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
				uint32_t offset_u32 = static_cast<uint32_t>(array_base_offset);
				textSectionData.push_back(offset_u32 & 0xFF);
				textSectionData.push_back((offset_u32 >> 8) & 0xFF);
				textSectionData.push_back((offset_u32 >> 16) & 0xFF);
				textSectionData.push_back((offset_u32 >> 24) & 0xFF);
			}

			// Subtract index offset from base: SUB RAX, RCX
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x29); // SUB r/m64, r64
			textSectionData.push_back(0xC8); // ModR/M: RAX, RCX

			// Load value from computed address: MOV RAX, [RAX]
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x8B); // MOV r64, r/m64
			textSectionData.push_back(0x00); // ModR/M: [RAX], RAX
		}

		// Store result to stack: MOV [RBP + result_offset], RAX
		textSectionData.push_back(0x48); // REX.W
		textSectionData.push_back(0x89); // MOV r/m64, r64
		if (index_offset >= -128 && index_offset <= 127) {
			textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
			textSectionData.push_back(static_cast<uint8_t>(index_offset));
		} else {
			textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
			uint32_t offset_u32 = static_cast<uint32_t>(index_offset);
			textSectionData.push_back(offset_u32 & 0xFF);
			textSectionData.push_back((offset_u32 >> 8) & 0xFF);
			textSectionData.push_back((offset_u32 >> 16) & 0xFF);
			textSectionData.push_back((offset_u32 >> 24) & 0xFF);
		}
	}

	void handleMemberAccess(const IrInstruction& instruction) {
		// MemberAccess: %result = member_access [MemberType][MemberSize] %object, member_name, offset
		// Format: [result_var, member_type, member_size, object_name/temp_var, member_name, offset]
		assert(instruction.getOperandCount() == 6 && "MemberAccess must have 6 operands");

		auto result_var = instruction.getOperandAs<TempVar>(0);
		auto member_type = instruction.getOperandAs<Type>(1);
		auto member_size_bits = instruction.getOperandAs<int>(2);
		// Operand 3 can be either a string_view (variable name) or TempVar (nested access result)
		auto member_name = instruction.getOperandAs<std::string_view>(4);
		auto member_offset = instruction.getOperandAs<int>(5);

		// Get the object's base stack offset
		int32_t object_base_offset = 0;
		const StackVariableScope& current_scope = variable_scopes.back();

		// Check if operand 3 is a string_view (variable name) or TempVar (nested access)
		if (instruction.isOperandType<std::string_view>(3)) {
			// Simple case: object is a variable name
			auto object_name = std::string(instruction.getOperandAs<std::string_view>(3));
			auto it = current_scope.identifier_offset.find(object_name);
			if (it == current_scope.identifier_offset.end()) {
				assert(false && "Struct object not found in scope");
				return;
			}
			object_base_offset = it->second;
		} else if (instruction.isOperandType<TempVar>(3)) {
			// Nested case: object is the result of a previous member access
			auto object_temp = instruction.getOperandAs<TempVar>(3);
			object_base_offset = getStackOffsetFromTempVar(object_temp);
		} else {
			assert(false && "MemberAccess operand 3 must be string_view or TempVar");
			return;
		}

		// Calculate the member's actual stack offset
		// The object is at [RBP + object_base_offset]
		// The member is at offset 'member_offset' bytes from the object's base
		// For a struct at [RBP - 8] with member at offset 4:
		//   member is at [RBP - 8 + 4] = [RBP - 4]
		int32_t member_stack_offset = object_base_offset + member_offset;

		// Get the result variable's stack offset
		int32_t result_offset = getStackOffsetFromTempVar(result_var);

		// Calculate member size in bytes
		int member_size_bytes = member_size_bits / 8;

		// Load the member value into a register based on size
		X64Register temp_reg = X64Register::RAX;

		if (member_size_bytes == 8) {
			// 64-bit member: MOV RAX, [RBP + member_stack_offset]
			auto load_opcodes = generateMovFromFrame(temp_reg, member_stack_offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
		} else if (member_size_bytes == 4) {
			// 32-bit member: MOV EAX, [RBP + member_stack_offset]
			textSectionData.push_back(0x8B); // MOV r32, r/m32
			if (member_stack_offset >= -128 && member_stack_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], EAX
				textSectionData.push_back(static_cast<uint8_t>(member_stack_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], EAX
				uint32_t offset_u32 = static_cast<uint32_t>(member_stack_offset);
				textSectionData.push_back(offset_u32 & 0xFF);
				textSectionData.push_back((offset_u32 >> 8) & 0xFF);
				textSectionData.push_back((offset_u32 >> 16) & 0xFF);
				textSectionData.push_back((offset_u32 >> 24) & 0xFF);
			}
		} else if (member_size_bytes == 2) {
			// 16-bit member: MOVZX RAX, WORD PTR [RBP + member_stack_offset]
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x0F); // Two-byte opcode prefix
			textSectionData.push_back(0xB7); // MOVZX r64, r/m16
			if (member_stack_offset >= -128 && member_stack_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
				textSectionData.push_back(static_cast<uint8_t>(member_stack_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
				uint32_t offset_u32 = static_cast<uint32_t>(member_stack_offset);
				textSectionData.push_back(offset_u32 & 0xFF);
				textSectionData.push_back((offset_u32 >> 8) & 0xFF);
				textSectionData.push_back((offset_u32 >> 16) & 0xFF);
				textSectionData.push_back((offset_u32 >> 24) & 0xFF);
			}
		} else if (member_size_bytes == 1) {
			// 8-bit member: MOVZX RAX, BYTE PTR [RBP + member_stack_offset]
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x0F); // Two-byte opcode prefix
			textSectionData.push_back(0xB6); // MOVZX r64, r/m8
			if (member_stack_offset >= -128 && member_stack_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
				textSectionData.push_back(static_cast<uint8_t>(member_stack_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
				uint32_t offset_u32 = static_cast<uint32_t>(member_stack_offset);
				textSectionData.push_back(offset_u32 & 0xFF);
				textSectionData.push_back((offset_u32 >> 8) & 0xFF);
				textSectionData.push_back((offset_u32 >> 16) & 0xFF);
				textSectionData.push_back((offset_u32 >> 24) & 0xFF);
			}
		} else {
			assert(false && "Unsupported member size");
			return;
		}

		// Store the result to the result variable's stack location
		auto store_opcodes = generateMovToFrame(temp_reg, result_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
	}

	void handleMemberStore(const IrInstruction& instruction) {
		// MemberStore: member_store [MemberType][MemberSize] %object, member_name, offset, %value
		// Format: [member_type, member_size, object_name/temp_var, member_name, offset, value]
		assert(instruction.getOperandCount() == 6 && "MemberStore must have 6 operands");

		auto member_type = instruction.getOperandAs<Type>(0);
		auto member_size_bits = instruction.getOperandAs<int>(1);
		// Operand 2 can be either a string_view (variable name) or TempVar (nested access result)
		auto member_name = instruction.getOperandAs<std::string_view>(3);
		auto member_offset = instruction.getOperandAs<int>(4);

		// Get the value - it could be a TempVar, a literal (int or unsigned long long), or a string_view (variable name)
		TempVar value_var;
		bool is_literal = false;
		int64_t literal_value = 0;
		bool is_variable = false;
		std::string variable_name;

		if (instruction.isOperandType<TempVar>(5)) {
			value_var = instruction.getOperandAs<TempVar>(5);
		} else if (instruction.isOperandType<int>(5)) {
			is_literal = true;
			literal_value = instruction.getOperandAs<int>(5);
		} else if (instruction.isOperandType<unsigned long long>(5)) {
			is_literal = true;
			literal_value = static_cast<int64_t>(instruction.getOperandAs<unsigned long long>(5));
		} else if (instruction.isOperandType<std::string_view>(5)) {
			is_variable = true;
			variable_name = std::string(instruction.getOperandAs<std::string_view>(5));
		} else {
			assert(false && "Value must be TempVar, int/unsigned long long literal, or string_view");
			return;
		}

		// Get the object's base stack offset
		int32_t object_base_offset = 0;
		const StackVariableScope& current_scope = variable_scopes.back();

		// Check if operand 2 is a string_view (variable name) or TempVar (nested access)
		if (instruction.isOperandType<std::string_view>(2)) {
			// Simple case: object is a variable name
			auto object_name = std::string(instruction.getOperandAs<std::string_view>(2));
			auto it = current_scope.identifier_offset.find(object_name);
			if (it == current_scope.identifier_offset.end()) {
				assert(false && "Struct object not found in scope");
				return;
			}
			object_base_offset = it->second;
		} else if (instruction.isOperandType<TempVar>(2)) {
			// Nested case: object is the result of a previous member access
			auto object_temp = instruction.getOperandAs<TempVar>(2);
			object_base_offset = getStackOffsetFromTempVar(object_temp);
		} else {
			assert(false && "MemberStore operand 2 must be string_view or TempVar");
			return;
		}

		// Calculate the member's actual stack offset
		// For a struct at [RBP - 8] with member at offset 4:
		//   member is at [RBP - 8 + 4] = [RBP - 4]
		int32_t member_stack_offset = object_base_offset + member_offset;

		// Calculate member size in bytes
		int member_size_bytes = member_size_bits / 8;

		// Load the value into a register
		X64Register value_reg = X64Register::RAX;

		if (is_literal) {
			// MOV RAX, immediate
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0xB8 + static_cast<uint8_t>(value_reg)); // MOV RAX, imm64
			// For simplicity, use 64-bit immediate even for smaller values
			uint64_t imm64 = static_cast<uint64_t>(literal_value);
			for (int i = 0; i < 8; i++) {
				textSectionData.push_back((imm64 >> (i * 8)) & 0xFF);
			}
		} else if (is_variable) {
			// Load from variable's stack location
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.identifier_offset.find(variable_name);
			if (it == current_scope.identifier_offset.end()) {
				assert(false && "Variable not found in scope");
				return;
			}
			int32_t value_offset = it->second;
			auto load_opcodes = generateMovFromFrame(value_reg, value_offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
		} else {
			// Load from value variable's stack location (TempVar)
			int32_t value_offset = getStackOffsetFromTempVar(value_var);
			auto load_opcodes = generateMovFromFrame(value_reg, value_offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
		}

		// Store the value to the member's stack location
		if (member_size_bytes == 8) {
			// 64-bit member: MOV [RBP + member_stack_offset], RAX
			auto store_opcodes = generateMovToFrame(value_reg, member_stack_offset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
			                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
		} else if (member_size_bytes == 4) {
			// 32-bit member: MOV [RBP + member_stack_offset], EAX
			textSectionData.push_back(0x89); // MOV r/m32, r32
			if (member_stack_offset >= -128 && member_stack_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], EAX
				textSectionData.push_back(static_cast<uint8_t>(member_stack_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], EAX
				uint32_t offset_u32 = static_cast<uint32_t>(member_stack_offset);
				textSectionData.push_back(offset_u32 & 0xFF);
				textSectionData.push_back((offset_u32 >> 8) & 0xFF);
				textSectionData.push_back((offset_u32 >> 16) & 0xFF);
				textSectionData.push_back((offset_u32 >> 24) & 0xFF);
			}
		} else if (member_size_bytes == 2) {
			// 16-bit member: MOV [RBP + member_stack_offset], AX
			textSectionData.push_back(0x66); // Operand-size override prefix
			textSectionData.push_back(0x89); // MOV r/m16, r16
			if (member_stack_offset >= -128 && member_stack_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], AX
				textSectionData.push_back(static_cast<uint8_t>(member_stack_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], AX
				uint32_t offset_u32 = static_cast<uint32_t>(member_stack_offset);
				textSectionData.push_back(offset_u32 & 0xFF);
				textSectionData.push_back((offset_u32 >> 8) & 0xFF);
				textSectionData.push_back((offset_u32 >> 16) & 0xFF);
				textSectionData.push_back((offset_u32 >> 24) & 0xFF);
			}
		} else if (member_size_bytes == 1) {
			// 8-bit member: MOV [RBP + member_stack_offset], AL
			textSectionData.push_back(0x88); // MOV r/m8, r8
			if (member_stack_offset >= -128 && member_stack_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], AL
				textSectionData.push_back(static_cast<uint8_t>(member_stack_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], AL
				uint32_t offset_u32 = static_cast<uint32_t>(member_stack_offset);
				textSectionData.push_back(offset_u32 & 0xFF);
				textSectionData.push_back((offset_u32 >> 8) & 0xFF);
				textSectionData.push_back((offset_u32 >> 16) & 0xFF);
				textSectionData.push_back((offset_u32 >> 24) & 0xFF);
			}
		} else {
			assert(false && "Unsupported member size");
			return;
		}
	}

	void handlePreIncrement(const IrInstruction& instruction) {
		// Pre-increment: ++x
		// Operands: [result_var, type, size, operand]
		assert(instruction.getOperandCount() == 4 && "PreIncrement must have 4 operands");

		// Get operand (variable to increment)
		auto operand = instruction.getOperandAs<std::string_view>(3);
		const StackVariableScope& current_scope = variable_scopes.back();
		auto it = current_scope.identifier_offset.find(operand);
		if (it == current_scope.identifier_offset.end()) {
			assert(false && "Variable not found in scope");
			return;
		}
		int32_t var_offset = it->second;

		// Load the variable into a register
		X64Register target_reg = X64Register::RAX;
		auto load_opcodes = generateMovFromFrame(target_reg, var_offset);
		textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
		                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);

		// Increment the value: add rax, 1
		// REX.W + ADD r/m64, imm8 (opcode 0x83 /0)
		std::array<uint8_t, 4> incInst = { 0x48, 0x83, 0xC0, 0x01 };
		incInst[2] = 0xC0 + static_cast<uint8_t>(target_reg); // ModR/M byte for ADD
		textSectionData.insert(textSectionData.end(), incInst.begin(), incInst.end());

		// Store the incremented value back to the variable
		auto store_opcodes = generateMovToFrame(target_reg, var_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

		// For pre-increment, the result is the new value (already in RAX)
		// Store result to result_var
		auto result_var = instruction.getOperandAs<TempVar>(0);
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		auto result_store = generateMovToFrame(target_reg, result_offset);
		textSectionData.insert(textSectionData.end(), result_store.op_codes.begin(),
		                       result_store.op_codes.begin() + result_store.size_in_bytes);
	}

	void handlePostIncrement(const IrInstruction& instruction) {
		// Post-increment: x++
		// Operands: [result_var, type, size, operand]
		assert(instruction.getOperandCount() == 4 && "PostIncrement must have 4 operands");

		// Get operand (variable to increment)
		auto operand = instruction.getOperandAs<std::string_view>(3);
		const StackVariableScope& current_scope = variable_scopes.back();
		auto it = current_scope.identifier_offset.find(operand);
		if (it == current_scope.identifier_offset.end()) {
			assert(false && "Variable not found in scope");
			return;
		}
		int32_t var_offset = it->second;

		// Load the variable into RAX
		X64Register target_reg = X64Register::RAX;
		auto load_opcodes = generateMovFromFrame(target_reg, var_offset);
		textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
		                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);

		// Save the old value to result_var (post-increment returns old value)
		auto result_var = instruction.getOperandAs<TempVar>(0);
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		auto result_store = generateMovToFrame(target_reg, result_offset);
		textSectionData.insert(textSectionData.end(), result_store.op_codes.begin(),
		                       result_store.op_codes.begin() + result_store.size_in_bytes);

		// Increment the value: add rax, 1
		std::array<uint8_t, 4> incInst = { 0x48, 0x83, 0xC0, 0x01 };
		incInst[2] = 0xC0 + static_cast<uint8_t>(target_reg);
		textSectionData.insert(textSectionData.end(), incInst.begin(), incInst.end());

		// Store the incremented value back to the variable
		auto store_opcodes = generateMovToFrame(target_reg, var_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
	}

	void handlePreDecrement(const IrInstruction& instruction) {
		// Pre-decrement: --x
		// Operands: [result_var, type, size, operand]
		assert(instruction.getOperandCount() == 4 && "PreDecrement must have 4 operands");

		// Get operand (variable to decrement)
		auto operand = instruction.getOperandAs<std::string_view>(3);
		const StackVariableScope& current_scope = variable_scopes.back();
		auto it = current_scope.identifier_offset.find(operand);
		if (it == current_scope.identifier_offset.end()) {
			assert(false && "Variable not found in scope");
			return;
		}
		int32_t var_offset = it->second;

		// Load the variable into a register
		X64Register target_reg = X64Register::RAX;
		auto load_opcodes = generateMovFromFrame(target_reg, var_offset);
		textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
		                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);

		// Decrement the value: sub rax, 1
		// REX.W + SUB r/m64, imm8 (opcode 0x83 /5)
		std::array<uint8_t, 4> decInst = { 0x48, 0x83, 0xE8, 0x01 };
		decInst[2] = 0xE8 + static_cast<uint8_t>(target_reg); // ModR/M byte for SUB
		textSectionData.insert(textSectionData.end(), decInst.begin(), decInst.end());

		// Store the decremented value back to the variable
		auto store_opcodes = generateMovToFrame(target_reg, var_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

		// For pre-decrement, the result is the new value (already in RAX)
		// Store result to result_var
		auto result_var = instruction.getOperandAs<TempVar>(0);
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		auto result_store = generateMovToFrame(target_reg, result_offset);
		textSectionData.insert(textSectionData.end(), result_store.op_codes.begin(),
		                       result_store.op_codes.begin() + result_store.size_in_bytes);
	}

	void handlePostDecrement(const IrInstruction& instruction) {
		// Post-decrement: x--
		// Operands: [result_var, type, size, operand]
		assert(instruction.getOperandCount() == 4 && "PostDecrement must have 4 operands");

		// Get operand (variable to decrement)
		auto operand = instruction.getOperandAs<std::string_view>(3);
		const StackVariableScope& current_scope = variable_scopes.back();
		auto it = current_scope.identifier_offset.find(operand);
		if (it == current_scope.identifier_offset.end()) {
			assert(false && "Variable not found in scope");
			return;
		}
		int32_t var_offset = it->second;

		// Load the variable into RAX
		X64Register target_reg = X64Register::RAX;
		auto load_opcodes = generateMovFromFrame(target_reg, var_offset);
		textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
		                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);

		// Save the old value to result_var (post-decrement returns old value)
		auto result_var = instruction.getOperandAs<TempVar>(0);
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		auto result_store = generateMovToFrame(target_reg, result_offset);
		textSectionData.insert(textSectionData.end(), result_store.op_codes.begin(),
		                       result_store.op_codes.begin() + result_store.size_in_bytes);

		// Decrement the value: sub rax, 1
		std::array<uint8_t, 4> decInst = { 0x48, 0x83, 0xE8, 0x01 };
		decInst[2] = 0xE8 + static_cast<uint8_t>(target_reg);
		textSectionData.insert(textSectionData.end(), decInst.begin(), decInst.end());

		// Store the decremented value back to the variable
		auto store_opcodes = generateMovToFrame(target_reg, var_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
	}

	void handleAddressOf(const IrInstruction& instruction) {
		// Address-of: &x
		// Operands: [result_var, type, size, operand]
		assert(instruction.getOperandCount() == 4 && "AddressOf must have 4 operands");

		// Get operand (variable to take address of)
		auto operand = instruction.getOperandAs<std::string_view>(3);
		const StackVariableScope& current_scope = variable_scopes.back();
		auto it = current_scope.identifier_offset.find(operand);
		if (it == current_scope.identifier_offset.end()) {
			assert(false && "Variable not found in scope");
			return;
		}
		int32_t var_offset = it->second;

		// Calculate the address: RBP + offset
		// LEA RAX, [RBP + offset]
		X64Register target_reg = X64Register::RAX;

		// LEA r64, m (opcode 0x8D)
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x8D); // LEA opcode

		if (var_offset >= -128 && var_offset <= 127) {
			// Use 8-bit displacement
			textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
			textSectionData.push_back(static_cast<uint8_t>(var_offset));
		} else {
			// Use 32-bit displacement
			textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
			uint32_t offset_u32 = static_cast<uint32_t>(var_offset);
			textSectionData.push_back(offset_u32 & 0xFF);
			textSectionData.push_back((offset_u32 >> 8) & 0xFF);
			textSectionData.push_back((offset_u32 >> 16) & 0xFF);
			textSectionData.push_back((offset_u32 >> 24) & 0xFF);
		}

		// Store the address to result_var
		auto result_var = instruction.getOperandAs<TempVar>(0);
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		auto result_store = generateMovToFrame(target_reg, result_offset);
		textSectionData.insert(textSectionData.end(), result_store.op_codes.begin(),
		                       result_store.op_codes.begin() + result_store.size_in_bytes);
	}

	void handleDereference(const IrInstruction& instruction) {
		// Dereference: *ptr
		// Operands: [result_var, type, size, operand]
		assert(instruction.getOperandCount() == 4 && "Dereference must have 4 operands");

		// Get the pointer operand
		X64Register ptr_reg = X64Register::RAX;

		if (instruction.isOperandType<std::string_view>(3)) {
			// Load pointer from variable
			auto operand = instruction.getOperandAs<std::string_view>(3);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.identifier_offset.find(operand);
			if (it == current_scope.identifier_offset.end()) {
				assert(false && "Variable not found in scope");
				return;
			}
			int32_t ptr_offset = it->second;

			// Load pointer value into RAX
			auto load_opcodes = generateMovFromFrame(ptr_reg, ptr_offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
		} else if (instruction.isOperandType<TempVar>(3)) {
			// Load pointer from temp var
			auto temp_var = instruction.getOperandAs<TempVar>(3);
			int32_t ptr_offset = getStackOffsetFromTempVar(temp_var);

			// Load pointer value into RAX
			auto load_opcodes = generateMovFromFrame(ptr_reg, ptr_offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
		}

		// Now dereference: load value from [RAX] into RAX
		// MOV RAX, [RAX]
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x8B); // MOV r64, r/m64
		textSectionData.push_back(0x00); // ModR/M: [RAX], RAX

		// Store the dereferenced value to result_var
		auto result_var = instruction.getOperandAs<TempVar>(0);
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		auto result_store = generateMovToFrame(ptr_reg, result_offset);
		textSectionData.insert(textSectionData.end(), result_store.op_codes.begin(),
		                       result_store.op_codes.begin() + result_store.size_in_bytes);
	}

	void handleConditionalBranch(const IrInstruction& instruction) {
		// Conditional branch: test condition, jump if true to then_label, otherwise fall through to else_label
		// Operands: [type, size, condition_value, then_label, else_label]
		assert(instruction.getOperandCount() >= 5 && "ConditionalBranch must have at least 5 operands");

		Type condition_type = instruction.getOperandAs<Type>(0);
		int condition_size = instruction.getOperandAs<int>(1);
		auto condition_value = instruction.getOperand(2);
		auto then_label = instruction.getOperandAs<std::string>(3);
		auto else_label = instruction.getOperandAs<std::string>(4);

		// Flush all dirty registers before branching
		flushAllDirtyRegisters();

		// Load condition value into a register
		X64Register condition_reg = X64Register::RAX;

		if (std::holds_alternative<TempVar>(condition_value)) {
			const TempVar temp_var = std::get<TempVar>(condition_value);
			int condition_offset = getStackOffsetFromTempVar(temp_var);

			// Load condition from stack
			auto load_opcodes = generateMovFromFrame(condition_reg, condition_offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                      load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
		} else if (std::holds_alternative<std::string_view>(condition_value)) {
			std::string_view var_name = std::get<std::string_view>(condition_value);
			int condition_offset = variable_scopes.back().identifier_offset[var_name];

			// Load condition from stack
			auto load_opcodes = generateMovFromFrame(condition_reg, condition_offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                      load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
		} else if (std::holds_alternative<unsigned long long>(condition_value)) {
			// Immediate value
			unsigned long long value = std::get<unsigned long long>(condition_value);

			// MOV RAX, imm64
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0xB8); // MOV RAX, imm64
			for (size_t i = 0; i < 8; ++i) {
				textSectionData.push_back(static_cast<uint8_t>(value & 0xFF));
				value >>= 8;
			}
		}

		// Test if condition is non-zero: TEST RAX, RAX
		std::array<uint8_t, 3> testInst = { 0x48, 0x85, 0xC0 }; // test rax, rax
		textSectionData.insert(textSectionData.end(), testInst.begin(), testInst.end());

		// Jump if not zero (JNZ) to then_label
		textSectionData.push_back(0x0F); // Two-byte opcode prefix
		textSectionData.push_back(0x85); // JNZ rel32

		uint32_t then_patch_position = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		pending_branches_.push_back({then_label, then_patch_position});

		// Fall through or jump to else_label
		// If else_label is different from the next instruction, we need an unconditional jump
		textSectionData.push_back(0xE9); // JMP rel32

		uint32_t else_patch_position = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		pending_branches_.push_back({else_label, else_patch_position});
	}

	void finalizeSections() {
		// Patch all pending branches before finalizing
		patchBranches();

		// Finalize the last function (if any) since there's no subsequent handleFunctionDecl to trigger it
		if (!current_function_name_.empty()) {
			uint32_t function_length = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;

			// Update function length
			writer.update_function_length(current_function_name_, function_length);

			// Set debug range to match reference exactly
			if (function_length > 13) {
				//writer.set_function_debug_range(current_function_name_, 8, 5); // prologue=8, epilogue=3
			}

			// Add exception handling information (required for x64) - once per function
			writer.add_function_exception_info(current_function_name_, current_function_offset_, function_length);

			// Clear the current function state
			current_function_name_.clear();
			current_function_offset_ = 0;
		}

		writer.add_data(textSectionData, SectionType::TEXT);

		// Finalize debug information
		writer.finalize_debug_info();
	}

	void patchBranches() {
		// Patch all pending branch instructions with correct offsets
		for (const auto& branch : pending_branches_) {
			auto label_it = label_positions_.find(branch.target_label);
			if (label_it == label_positions_.end()) {
				std::cerr << "ERROR: Label not found: " << branch.target_label << std::endl;
				continue;
			}

			uint32_t label_offset = label_it->second;
			uint32_t branch_end = branch.patch_position + 4; // Position after the 4-byte offset

			// Calculate relative offset (target - current position)
			int32_t relative_offset = static_cast<int32_t>(label_offset) - static_cast<int32_t>(branch_end);

			// Patch the offset in little-endian format
			textSectionData[branch.patch_position + 0] = static_cast<uint8_t>(relative_offset & 0xFF);
			textSectionData[branch.patch_position + 1] = static_cast<uint8_t>((relative_offset >> 8) & 0xFF);
			textSectionData[branch.patch_position + 2] = static_cast<uint8_t>((relative_offset >> 16) & 0xFF);
			textSectionData[branch.patch_position + 3] = static_cast<uint8_t>((relative_offset >> 24) & 0xFF);
		}
	}

	// Debug information tracking
	void addLineMapping(uint32_t line_number, int32_t manual_offset = 0) {
		if (!current_function_name_.empty()) {
			uint32_t code_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_ + manual_offset;
			writer.add_line_mapping(code_offset, line_number);
		}
	}

	TWriterClass writer;
	std::vector<char> textSectionData;
	std::unordered_map<std::string_view, uint32_t> functionSymbols;
	std::unordered_map<std::string_view, std::vector<IrInstruction>> function_instructions;

	RegisterAllocator regAlloc;

	// Debug information tracking
	std::string current_function_name_;
	uint32_t current_function_offset_ = 0;

	// Pending function info for exception handling
	struct PendingFunctionInfo {
		std::string name;
		uint32_t offset;
		uint32_t length;
	};
	std::vector<PendingFunctionInfo> pending_functions_;
	std::vector<StackVariableScope> variable_scopes;

	// Control flow tracking
	struct PendingBranch {
		std::string target_label;
		uint32_t patch_position; // Position in textSectionData where the offset needs to be written
	};
	std::unordered_map<std::string, uint32_t> label_positions_;
	std::vector<PendingBranch> pending_branches_;

	// Loop context tracking for break/continue
	struct LoopContext {
		std::string loop_end_label;       // Label to jump to for break
		std::string loop_increment_label; // Label to jump to for continue
	};
	std::vector<LoopContext> loop_context_stack_;
};
