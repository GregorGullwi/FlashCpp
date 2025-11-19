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
#include "ProfilingTimer.h"

// Enable detailed profiling by default in debug builds
// Can be overridden by defining ENABLE_DETAILED_PROFILING=0 or =1
#ifndef ENABLE_DETAILED_PROFILING
#define ENABLE_DETAILED_PROFILING 0
#endif

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
static constexpr size_t MAX_MOV_INSTRUCTION_SIZE = 8;

struct OpCodeWithSize {
	std::array<uint8_t, MAX_MOV_INSTRUCTION_SIZE> op_codes;
	size_t size_in_bytes = 0;
};

/**
 * @brief Converts an XMM register enum value to its 0-based encoding for ModR/M bytes.
 * 
 * XMM registers in the X64Register enum start at value 16 (after RAX=0...R15=15),
 * but x86-64 instruction encoding expects XMM registers to be numbered 0-15.
 * This helper ensures consistent conversion to avoid encoding bugs.
 * 
 * @param xmm_reg The XMM register (must be XMM0-XMM15)
 * @return The 0-based register number (0-15) for use in ModR/M/SIB bytes
 */
inline uint8_t xmm_modrm_bits(X64Register xmm_reg) {
	return static_cast<uint8_t>(xmm_reg) - static_cast<uint8_t>(X64Register::XMM0);
}

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
 * @brief Generates x86-64 binary opcodes for 'mov r32, [rbp + offset]'.
 *
 * This function creates the byte sequence for loading a 32-bit value from
 * a frame-relative address (RBP + offset) into a 32-bit register.
 * Note: This zero-extends the value to 64 bits in the destination register.
 *
 * @param destinationRegister The destination register (RAX, RCX, RDX, etc.).
 * @param offset The signed offset from RBP.
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovFromFrame32(X64Register destinationRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// For 32-bit operations, we may need REX prefix only if using R8-R15
	bool needs_rex = static_cast<uint8_t>(destinationRegister) >= static_cast<uint8_t>(X64Register::R8);
	
	if (needs_rex) {
		uint8_t rex_prefix = 0x40; // Base REX prefix (no W bit for 32-bit)
		rex_prefix |= (1 << 2); // Set R bit for R8-R15
		*current_byte_ptr++ = rex_prefix;
		result.size_in_bytes++;
	}

	// Opcode for MOV r32, r/m32
	*current_byte_ptr++ = 0x8B;
	result.size_in_bytes++;

	// ModR/M byte
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(destinationRegister) & 0x07;

	uint8_t mod_field;
	if (offset == 0) {
		mod_field = 0x01; // RBP always needs displacement
	}
	else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01; // 8-bit displacement
	}
	else {
		mod_field = 0x02; // 32-bit displacement
	}

	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05;
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// Displacement
	if (offset == 0 || (offset >= -128 && offset <= 127)) {
		*current_byte_ptr++ = static_cast<uint8_t>(offset);
		result.size_in_bytes++;
	}
	else {
		*current_byte_ptr++ = static_cast<uint8_t>(offset & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 8) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 16) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 24) & 0xFF);
		result.size_in_bytes += 4;
	}

	return result;
}

OpCodeWithSize generateLeaFromFrame(X64Register destinationRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	uint8_t rex_prefix = 0x48; // REX.W
	if (static_cast<uint8_t>(destinationRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= 0x04; // REX.R for registers R8-R15
	}
	*current_byte_ptr++ = rex_prefix;
	result.size_in_bytes++;

	*current_byte_ptr++ = 0x8D; // LEA opcode
	result.size_in_bytes++;

	uint8_t reg_bits = static_cast<uint8_t>(destinationRegister) & 0x07;
	uint8_t mod_field = (offset >= -128 && offset <= 127) ? 0x01 : 0x02;
	uint8_t modrm = static_cast<uint8_t>((mod_field << 6) | (reg_bits << 3) | 0x05); // Base = RBP
	*current_byte_ptr++ = modrm;
	result.size_in_bytes++;

	if (mod_field == 0x01) {
		*current_byte_ptr++ = static_cast<uint8_t>(offset);
		result.size_in_bytes++;
	} else {
		*current_byte_ptr++ = static_cast<uint8_t>(offset & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 8) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 16) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 24) & 0xFF);
		result.size_in_bytes += 4;
	}

	return result;
}

/**
 * @brief Generates x86-64 binary opcodes for 'movzx r32, word ptr [rbp + offset]'.
 *
 * Load a 16-bit value from RBP-relative address and zero-extend to 32/64 bits.
 *
 * @param destinationRegister The destination register.
 * @param offset The signed byte offset from RBP.
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovzxFromFrame16(X64Register destinationRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// REX prefix only if using R8-R15
	if (static_cast<uint8_t>(destinationRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		uint8_t rex_prefix = 0x40 | (1 << 2); // REX + REX.R
		*current_byte_ptr++ = rex_prefix;
		result.size_in_bytes++;
	}

	// Opcode: 0F B7 for MOVZX r32, r/m16
	*current_byte_ptr++ = 0x0F;
	*current_byte_ptr++ = 0xB7;
	result.size_in_bytes += 2;

	// ModR/M and displacement
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(destinationRegister) & 0x07;
	uint8_t mod_field;
	if (offset == 0) {
		mod_field = 0x01; // RBP needs displacement even for 0
	} else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01; // 8-bit displacement
	} else {
		mod_field = 0x02; // 32-bit displacement
	}

	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05;
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// Displacement
	if (offset == 0 || (offset >= -128 && offset <= 127)) {
		*current_byte_ptr++ = static_cast<uint8_t>(offset);
		result.size_in_bytes++;
	} else {
		*current_byte_ptr++ = static_cast<uint8_t>(offset & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 8) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 16) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 24) & 0xFF);
		result.size_in_bytes += 4;
	}

	return result;
}

/**
 * @brief Generates x86-64 binary opcodes for 'movzx r32, byte ptr [rbp + offset]'.
 *
 * Load an 8-bit value from RBP-relative address and zero-extend to 32/64 bits.
 *
 * @param destinationRegister The destination register.
 * @param offset The signed byte offset from RBP.
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovzxFromFrame8(X64Register destinationRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// REX prefix only if using R8-R15
	if (static_cast<uint8_t>(destinationRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		uint8_t rex_prefix = 0x40 | (1 << 2); // REX + REX.R
		*current_byte_ptr++ = rex_prefix;
		result.size_in_bytes++;
	}

	// Opcode: 0F B6 for MOVZX r32, r/m8
	*current_byte_ptr++ = 0x0F;
	*current_byte_ptr++ = 0xB6;
	result.size_in_bytes += 2;

	// ModR/M and displacement
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(destinationRegister) & 0x07;
	uint8_t mod_field;
	if (offset == 0) {
		mod_field = 0x01; // RBP needs displacement even for 0
	} else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01; // 8-bit displacement
	} else {
		mod_field = 0x02; // 32-bit displacement
	}

	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05;
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// Displacement
	if (offset == 0 || (offset >= -128 && offset <= 127)) {
		*current_byte_ptr++ = static_cast<uint8_t>(offset);
		result.size_in_bytes++;
	} else {
		*current_byte_ptr++ = static_cast<uint8_t>(offset & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 8) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 16) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 24) & 0xFF);
		result.size_in_bytes += 4;
	}

	return result;
}

/**
 * @brief Generates x86-64 binary opcodes for 'mov r64, [base_reg + offset]'.
 *
 * Load a 64-bit value from memory via a base register plus offset.
 *
 * @param dest_reg The destination register.
 * @param base_reg The base register for addressing (e.g., RCX for pointer).
 * @param offset The signed offset from the base register.
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovFromMemory(X64Register dest_reg, X64Register base_reg, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// REX prefix for 64-bit operation
	uint8_t rex_prefix = 0x48; // REX.W

	// Set REX.R if dest_reg is R8-R15
	if (static_cast<uint8_t>(dest_reg) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= (1 << 2); // REX.R
	}

	// Set REX.B if base_reg is R8-R15
	if (static_cast<uint8_t>(base_reg) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= (1 << 0); // REX.B
	}

	*current_byte_ptr++ = rex_prefix;
	result.size_in_bytes++;

	// Opcode: MOV r64, r/m64
	*current_byte_ptr++ = 0x8B;
	result.size_in_bytes++;

	// ModR/M byte
	uint8_t dest_bits = static_cast<uint8_t>(dest_reg) & 0x07;
	uint8_t base_bits = static_cast<uint8_t>(base_reg) & 0x07;

	uint8_t mod_field;
	if (offset == 0 && base_bits != 0x05) { // RBP/R13 always need displacement
		mod_field = 0x00; // No displacement
	} else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01; // 8-bit displacement
	} else {
		mod_field = 0x02; // 32-bit displacement
	}

	uint8_t modrm = (mod_field << 6) | (dest_bits << 3) | base_bits;
	*current_byte_ptr++ = modrm;
	result.size_in_bytes++;

	// Add displacement if needed
	if (offset != 0 || base_bits == 0x05) { // RBP/R13 always need displacement
		if (offset >= -128 && offset <= 127) {
			*current_byte_ptr++ = static_cast<uint8_t>(offset);
			result.size_in_bytes++;
		} else {
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
 * @brief Generates x86-64 binary opcodes for 'mov r32, [base_reg + offset]'.
 *
 * Load a 32-bit value from memory via a base register plus offset.
 * Zero-extends to 64 bits in the destination register.
 *
 * @param dest_reg The destination register.
 * @param base_reg The base register for addressing.
 * @param offset The signed offset from the base register.
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovFromMemory32(X64Register dest_reg, X64Register base_reg, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// REX prefix only if using R8-R15
	bool needs_rex = (static_cast<uint8_t>(dest_reg) >= static_cast<uint8_t>(X64Register::R8)) ||
	                 (static_cast<uint8_t>(base_reg) >= static_cast<uint8_t>(X64Register::R8));

	if (needs_rex) {
		uint8_t rex_prefix = 0x40; // Base REX (no W bit for 32-bit)
		if (static_cast<uint8_t>(dest_reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex_prefix |= (1 << 2); // REX.R
		}
		if (static_cast<uint8_t>(base_reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex_prefix |= (1 << 0); // REX.B
		}
		*current_byte_ptr++ = rex_prefix;
		result.size_in_bytes++;
	}

	// Opcode: MOV r32, r/m32
	*current_byte_ptr++ = 0x8B;
	result.size_in_bytes++;

	// ModR/M byte
	uint8_t dest_bits = static_cast<uint8_t>(dest_reg) & 0x07;
	uint8_t base_bits = static_cast<uint8_t>(base_reg) & 0x07;

	uint8_t mod_field;
	if (offset == 0 && base_bits != 0x05) {
		mod_field = 0x00;
	} else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01;
	} else {
		mod_field = 0x02;
	}

	uint8_t modrm = (mod_field << 6) | (dest_bits << 3) | base_bits;
	*current_byte_ptr++ = modrm;
	result.size_in_bytes++;

	// Displacement
	if (offset != 0 || base_bits == 0x05) {
		if (offset >= -128 && offset <= 127) {
			*current_byte_ptr++ = static_cast<uint8_t>(offset);
			result.size_in_bytes++;
		} else {
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
 * @brief Generates x86-64 binary opcodes for 'movzx r32, word ptr [base_reg + offset]'.
 *
 * Load a 16-bit value from memory and zero-extend to 32 bits (then to 64 bits).
 *
 * @param dest_reg The destination register.
 * @param base_reg The base register for addressing.
 * @param offset The signed offset from the base register.
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovFromMemory16(X64Register dest_reg, X64Register base_reg, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// REX prefix only if using R8-R15
	bool needs_rex = (static_cast<uint8_t>(dest_reg) >= static_cast<uint8_t>(X64Register::R8)) ||
	                 (static_cast<uint8_t>(base_reg) >= static_cast<uint8_t>(X64Register::R8));

	if (needs_rex) {
		uint8_t rex_prefix = 0x40; // Base REX
		if (static_cast<uint8_t>(dest_reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex_prefix |= (1 << 2); // REX.R
		}
		if (static_cast<uint8_t>(base_reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex_prefix |= (1 << 0); // REX.B
		}
		*current_byte_ptr++ = rex_prefix;
		result.size_in_bytes++;
	}

	// Opcode: 0F B7 for MOVZX r32, r/m16
	*current_byte_ptr++ = 0x0F;
	*current_byte_ptr++ = 0xB7;
	result.size_in_bytes += 2;

	// ModR/M byte
	uint8_t dest_bits = static_cast<uint8_t>(dest_reg) & 0x07;
	uint8_t base_bits = static_cast<uint8_t>(base_reg) & 0x07;

	uint8_t mod_field;
	if (offset == 0 && base_bits != 0x05) {
		mod_field = 0x00;
	} else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01;
	} else {
		mod_field = 0x02;
	}

	uint8_t modrm = (mod_field << 6) | (dest_bits << 3) | base_bits;
	*current_byte_ptr++ = modrm;
	result.size_in_bytes++;

	// Add displacement if needed
	if (offset != 0 || base_bits == 0x05) {
		if (offset >= -128 && offset <= 127) {
			*current_byte_ptr++ = static_cast<uint8_t>(offset);
			result.size_in_bytes++;
		} else {
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
 * @brief Generates x86-64 binary opcodes for 'movzx r32, byte ptr [base_reg + offset]'.
 *
 * Load an 8-bit value from memory and zero-extend to 32 bits (then to 64 bits).
 *
 * @param dest_reg The destination register.
 * @param base_reg The base register for addressing.
 * @param offset The signed offset from the base register.
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovFromMemory8(X64Register dest_reg, X64Register base_reg, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// REX prefix only if using R8-R15
	bool needs_rex = (static_cast<uint8_t>(dest_reg) >= static_cast<uint8_t>(X64Register::R8)) ||
	                 (static_cast<uint8_t>(base_reg) >= static_cast<uint8_t>(X64Register::R8));

	if (needs_rex) {
		uint8_t rex_prefix = 0x40; // Base REX
		if (static_cast<uint8_t>(dest_reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex_prefix |= (1 << 2); // REX.R
		}
		if (static_cast<uint8_t>(base_reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex_prefix |= (1 << 0); // REX.B
		}
		*current_byte_ptr++ = rex_prefix;
		result.size_in_bytes++;
	}

	// Opcode: 0F B6 for MOVZX r32, r/m8
	*current_byte_ptr++ = 0x0F;
	*current_byte_ptr++ = 0xB6;
	result.size_in_bytes += 2;

	// ModR/M byte
	uint8_t dest_bits = static_cast<uint8_t>(dest_reg) & 0x07;
	uint8_t base_bits = static_cast<uint8_t>(base_reg) & 0x07;

	uint8_t mod_field;
	if (offset == 0 && base_bits != 0x05) {
		mod_field = 0x00;
	} else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01;
	} else {
		mod_field = 0x02;
	}

	uint8_t modrm = (mod_field << 6) | (dest_bits << 3) | base_bits;
	*current_byte_ptr++ = modrm;
	result.size_in_bytes++;

	// Add displacement if needed
	if (offset != 0 || base_bits == 0x05) {
		if (offset >= -128 && offset <= 127) {
			*current_byte_ptr++ = static_cast<uint8_t>(offset);
			result.size_in_bytes++;
		} else {
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
 * @brief Generates x86-64 binary opcodes for 'movss/movsd xmm, [rbp + offset]'.
 *
 * This function creates the byte sequence for loading a float/double value from
 * a frame-relative address (RBP + offset) into an XMM register.
 *
 * @param destinationRegister The destination XMM register (XMM0-XMM3).
 * @param offset The signed byte offset from RBP.
 * @param is_float True for movss (float), false for movsd (double).
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateFloatMovFromFrame(X64Register destinationRegister, int32_t offset, bool is_float) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// Prefix: F3 for movss (float), F2 for movsd (double)
	*current_byte_ptr++ = is_float ? 0xF3 : 0xF2;
	result.size_in_bytes++;

	// Opcode: 0F 10 for movss/movsd xmm, [mem]
	*current_byte_ptr++ = 0x0F;
	*current_byte_ptr++ = 0x10;
	result.size_in_bytes += 2;

	// ModR/M byte
	uint8_t xmm_reg = static_cast<uint8_t>(destinationRegister) - static_cast<uint8_t>(X64Register::XMM0);

	if (offset >= -128 && offset <= 127) {
		// 8-bit displacement
		uint8_t modrm = 0x45 + (xmm_reg << 3); // Mod=01, Reg=XMM, R/M=101 (RBP)
		*current_byte_ptr++ = modrm;
		*current_byte_ptr++ = static_cast<uint8_t>(offset);
		result.size_in_bytes += 2;
	} else {
		// 32-bit displacement
		uint8_t modrm = 0x85 + (xmm_reg << 3); // Mod=10, Reg=XMM, R/M=101 (RBP)
		*current_byte_ptr++ = modrm;
		result.size_in_bytes++;

		*current_byte_ptr++ = static_cast<uint8_t>(offset & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 8) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 16) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 24) & 0xFF);
		result.size_in_bytes += 4;
	}

	return result;
}

/**
 * @brief Generates x86-64 binary opcodes for 'movss/movsd [rbp + offset], xmm'.
 *
 * This function creates the byte sequence for storing a float/double value from
 * an XMM register to a frame-relative address (RBP + offset).
 *
 * @param sourceRegister The source XMM register (XMM0-XMM15).
 * @param offset The signed byte offset from RBP.
 * @param is_float True for movss (float), false for movsd (double).
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateFloatMovToFrame(X64Register sourceRegister, int32_t offset, bool is_float) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// Prefix: F3 for movss (float), F2 for movsd (double)
	*current_byte_ptr++ = is_float ? 0xF3 : 0xF2;
	result.size_in_bytes++;

	// Opcode: 0F 11 for movss/movsd [mem], xmm - store variant
	*current_byte_ptr++ = 0x0F;
	*current_byte_ptr++ = 0x11;
	result.size_in_bytes += 2;

	// ModR/M byte
	uint8_t xmm_reg = static_cast<uint8_t>(sourceRegister) - static_cast<uint8_t>(X64Register::XMM0);

	if (offset >= -128 && offset <= 127) {
		// 8-bit displacement
		uint8_t modrm = 0x45 + (xmm_reg << 3); // Mod=01, Reg=XMM, R/M=101 (RBP)
		*current_byte_ptr++ = modrm;
		*current_byte_ptr++ = static_cast<uint8_t>(offset);
		result.size_in_bytes += 2;
	} else {
		// 32-bit displacement
		uint8_t modrm = 0x85 + (xmm_reg << 3); // Mod=10, Reg=XMM, R/M=101 (RBP)
		*current_byte_ptr++ = modrm;
		result.size_in_bytes++;

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

/**
 * @brief Generates x86-64 binary opcodes for 'mov [rbp + offset], r32'.
 *
 * This function creates the byte sequence for storing a 32-bit value from
 * a 32-bit register to a frame-relative address (RBP + offset).
 * Used for storing 32-bit parameters like int, char, and bool.
 *
 * @param sourceRegister The source register (EAX, ECX, EDX, etc.).
 * @param offset The signed offset from RBP.
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovToFrame32(X64Register sourceRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;

	uint8_t* current_byte_ptr = result.op_codes.data();

	// For 32-bit operations, we may need REX prefix only if using R8-R15
	bool needs_rex = static_cast<uint8_t>(sourceRegister) >= static_cast<uint8_t>(X64Register::R8);
	
	if (needs_rex) {
		uint8_t rex_prefix = 0x40; // Base REX prefix (no W bit for 32-bit)
		rex_prefix |= (1 << 2); // Set R bit for R8-R15
		*current_byte_ptr++ = rex_prefix;
		result.size_in_bytes++;
	}

	// --- Opcode for MOV r/m32, r32 ---
	*current_byte_ptr++ = 0x89;
	result.size_in_bytes++;

	// --- ModR/M byte (Mod | Reg | R/M) ---
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
	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05; // 0x05 for RBP
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
static constexpr std::array<X64Register, 4> INT_PARAM_REGS = {
	X64Register::RCX,  // First integer/pointer argument
	X64Register::RDX,  // Second integer/pointer argument
	X64Register::R8,   // Third integer/pointer argument
	X64Register::R9    // Fourth integer/pointer argument
};

static constexpr std::array<X64Register, 4> FLOAT_PARAM_REGS = {
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

	// Find which register (if any) currently holds a value for the given stack offset
	std::optional<X64Register> findRegisterForStackOffset(int32_t stackOffset) const {
		for (const auto& reg : registers) {
			if (reg.isAllocated && reg.stackVariableOffset == stackOffset) {
				return reg.reg;
			}
		}
		return std::nullopt;
	}

	AllocatedRegister& allocate() {
		for (auto& reg : registers) {
			if (!reg.isAllocated) {
				reg.isAllocated = true;
				return reg;
			}
		}
		// No free registers - need to spill one
		// Return a sentinel value to indicate spilling is needed
		throw std::runtime_error("No registers available");
	}

	// Find a register to spill (prefer non-dirty registers, avoid RSP/RBP)
	std::optional<X64Register> findRegisterToSpill() {
		// Single pass: prefer non-dirty registers, but accept dirty ones if needed
		X64Register best_candidate = X64Register::Count;
		bool found_dirty = false;

		// Iterate only over general-purpose registers (RAX to R15)
		for (size_t i = static_cast<size_t>(X64Register::RAX); i <= static_cast<size_t>(X64Register::R15); ++i) {
			if (registers[i].isAllocated &&
			    registers[i].reg != X64Register::RSP &&
			    registers[i].reg != X64Register::RBP) {

				if (!registers[i].isDirty) {
					// Found a clean register - best case, return immediately
					return registers[i].reg;
				} else if (best_candidate == X64Register::Count) {
					// Found a dirty register - keep it as fallback
					best_candidate = registers[i].reg;
					found_dirty = true;
				}
			}
		}

		// Return the dirty register if we found one, otherwise nullopt
		return (found_dirty) ? std::optional<X64Register>(best_candidate) : std::nullopt;
	}

	// Find an XMM register to spill (prefer non-dirty registers)
	std::optional<X64Register> findXMMRegisterToSpill() {
		// Single pass: prefer non-dirty registers, but accept dirty ones if needed
		X64Register best_candidate = X64Register::Count;
		bool found_dirty = false;

		// Iterate only over XMM registers (XMM0 to XMM15)
		for (size_t i = static_cast<size_t>(X64Register::XMM0); i <= static_cast<size_t>(X64Register::XMM15); ++i) {
			if (registers[i].isAllocated) {
				if (!registers[i].isDirty) {
					// Found a clean register - best case, return immediately
					return registers[i].reg;
				} else if (best_candidate == X64Register::Count) {
					// Found a dirty register - keep it as fallback
					best_candidate = registers[i].reg;
					found_dirty = true;
				}
			}
		}

		// Return the dirty register if we found one, otherwise nullopt
		return (found_dirty) ? std::optional<X64Register>(best_candidate) : std::nullopt;
	}

	// Allocate an XMM register specifically for floating-point operations
	AllocatedRegister& allocateXMM() {
		for (size_t i = static_cast<size_t>(X64Register::XMM0); i <= static_cast<size_t>(X64Register::XMM15); ++i) {
			if (!registers[i].isAllocated) {
				registers[i].isAllocated = true;
				return registers[i];
			}
		}
		throw std::runtime_error("No XMM registers available");
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
			// Skip RSP and RBP - they should never be used as general-purpose registers
			if (reg.reg == X64Register::RSP || reg.reg == X64Register::RBP) {
				continue;
			}
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
			// Build REX prefix: 0100WRXB
			// W=1 for 64-bit, R=1 if src_reg is R8-R15, B=1 if dst_reg is R8-R15
			uint8_t rex = 0x48;  // REX.W = 1
			if (static_cast<uint8_t>(src_reg) >= 8) {
				rex |= 0x04;  // REX.R = 1
			}
			if (static_cast<uint8_t>(dst_reg) >= 8) {
				rex |= 0x01;  // REX.B = 1
			}
			result.op_codes[0] = rex;
			result.op_codes[1] = 0x89;  // MOV r64, r64
			// ModR/M: Mod=11 (register-to-register), Reg=src_reg (lower 3 bits), R/M=dst_reg (lower 3 bits)
			result.op_codes[2] = 0xC0 + ((static_cast<uint8_t>(src_reg) & 0x07) << 3) + (static_cast<uint8_t>(dst_reg) & 0x07);
			result.size_in_bytes = 3;
		}
		// For 32-bit moves, we may need REX prefix for R8-R15
		else if (size_in_bytes == 4) {
			// Check if we need REX prefix for extended registers
			if (static_cast<uint8_t>(src_reg) >= 8 || static_cast<uint8_t>(dst_reg) >= 8) {
				uint8_t rex = 0x40;  // Base REX prefix
				if (static_cast<uint8_t>(src_reg) >= 8) {
					rex |= 0x04;  // REX.R = 1
				}
				if (static_cast<uint8_t>(dst_reg) >= 8) {
					rex |= 0x01;  // REX.B = 1
				}
				result.op_codes[0] = rex;
				result.op_codes[1] = 0x89;  // MOV r32, r32
				result.op_codes[2] = 0xC0 + ((static_cast<uint8_t>(src_reg) & 0x07) << 3) + (static_cast<uint8_t>(dst_reg) & 0x07);
				result.size_in_bytes = 3;
			} else {
				result.op_codes[0] = 0x89;  // MOV r32, r32
				result.op_codes[1] = 0xC0 + (static_cast<uint8_t>(src_reg) << 3) + static_cast<uint8_t>(dst_reg);
				result.size_in_bytes = 2;
			}
		}
		// For 16-bit moves, we need the 66 prefix
		else if (size_in_bytes == 2) {
			result.op_codes[0] = 0x66;
			// Check if we need REX prefix for extended registers
			if (static_cast<uint8_t>(src_reg) >= 8 || static_cast<uint8_t>(dst_reg) >= 8) {
				uint8_t rex = 0x40;  // Base REX prefix
				if (static_cast<uint8_t>(src_reg) >= 8) {
					rex |= 0x04;  // REX.R = 1
				}
				if (static_cast<uint8_t>(dst_reg) >= 8) {
					rex |= 0x01;  // REX.B = 1
				}
				result.op_codes[1] = rex;
				result.op_codes[2] = 0x89;  // MOV r16, r16
				result.op_codes[3] = 0xC0 + ((static_cast<uint8_t>(src_reg) & 0x07) << 3) + (static_cast<uint8_t>(dst_reg) & 0x07);
				result.size_in_bytes = 4;
			} else {
				result.op_codes[1] = 0x89;  // MOV r16, r16
				result.op_codes[2] = 0xC0 + (static_cast<uint8_t>(src_reg) << 3) + static_cast<uint8_t>(dst_reg);
				result.size_in_bytes = 3;
			}
		}
		// For 8-bit moves, we need special handling for high registers
		else if (size_in_bytes == 1) {
			// Check if we need REX prefix for extended registers
			if (static_cast<uint8_t>(src_reg) >= 8 || static_cast<uint8_t>(dst_reg) >= 8) {
				uint8_t rex = 0x40;  // Base REX prefix
				if (static_cast<uint8_t>(src_reg) >= 8) {
					rex |= 0x04;  // REX.R = 1
				}
				if (static_cast<uint8_t>(dst_reg) >= 8) {
					rex |= 0x01;  // REX.B = 1
				}
				result.op_codes[0] = rex;
				result.op_codes[1] = 0x88;  // MOV r8, r8
				result.op_codes[2] = 0xC0 + ((static_cast<uint8_t>(src_reg) & 0x07) << 3) + (static_cast<uint8_t>(dst_reg) & 0x07);
				result.size_in_bytes = 3;
			} else {
				result.op_codes[0] = 0x88;  // MOV r8, r8
				result.op_codes[1] = 0xC0 + (static_cast<uint8_t>(src_reg) << 3) + static_cast<uint8_t>(dst_reg);
				result.size_in_bytes = 2;
			}
		}

		return result;
	}
};

/**
 * @brief Emits x64 opcodes to load a value from [RAX] with size-appropriate instruction.
 *
 * Generates the correct load instruction based on element size:
 * - 1 byte: MOVZX EAX, BYTE PTR [RAX]  (0x0F 0xB6 0x00) - zero-extend byte to 32-bit
 * - 2 bytes: MOVZX EAX, WORD PTR [RAX] (0x0F 0xB7 0x00) - zero-extend word to 32-bit
 * - 4 bytes: MOV EAX, DWORD PTR [RAX]  (0x8B 0x00) - 32-bit load (no REX.W prefix)
 * - 8 bytes: MOV RAX, QWORD PTR [RAX]  (0x48 0x8B 0x00) - 64-bit load (with REX.W prefix)
 *
 * @param textSectionData The vector to append opcodes to
 * @param element_size_bytes The size of the element (1, 2, 4, or 8 bytes)
 */
inline void emitLoadFromAddressInRAX(std::vector<char>& textSectionData, int element_size_bytes) {
	if (element_size_bytes == 1) {
		// MOVZX EAX, BYTE PTR [RAX] - zero-extend byte to 32-bit
		textSectionData.push_back(0x0F); // Two-byte opcode prefix
		textSectionData.push_back(0xB6); // MOVZX r32, r/m8
		textSectionData.push_back(0x00); // ModR/M: [RAX], EAX
	} else if (element_size_bytes == 2) {
		// MOVZX EAX, WORD PTR [RAX] - zero-extend word to 32-bit
		textSectionData.push_back(0x0F); // Two-byte opcode prefix
		textSectionData.push_back(0xB7); // MOVZX r32, r/m16
		textSectionData.push_back(0x00); // ModR/M: [RAX], EAX
	} else if (element_size_bytes == 4) {
		// MOV EAX, DWORD PTR [RAX] - 32-bit load (no REX.W, zero-extends to 64-bit)
		textSectionData.push_back(0x8B); // MOV r32, r/m32
		textSectionData.push_back(0x00); // ModR/M: [RAX], EAX
	} else {
		// MOV RAX, QWORD PTR [RAX] - 64-bit load (8 bytes)
		textSectionData.push_back(0x48); // REX.W prefix for 64-bit operation
		textSectionData.push_back(0x8B); // MOV r64, r/m64
		textSectionData.push_back(0x00); // ModR/M: [RAX], RAX
	}
}

/**
 * @brief Emits x64 opcodes to multiply RCX by element_size_bytes.
 *
 * Optimizes for power-of-2 sizes using SHL (bit shift left):
 * - 1 byte: No operation needed (index already in bytes)
 * - 2 bytes: SHL RCX, 1  (multiply by 2)
 * - 4 bytes: SHL RCX, 2  (multiply by 4)
 * - 8 bytes: SHL RCX, 3  (multiply by 8)
 * - Other: IMUL RCX, RCX, imm32 (general multiplication)
 *
 * @param textSectionData The vector to append opcodes to
 * @param element_size_bytes The size to multiply by (1, 2, 4, 8, or other)
 */
inline void emitMultiplyRCXByElementSize(std::vector<char>& textSectionData, int element_size_bytes) {
	if (element_size_bytes == 1) {
		// No multiplication needed - index is already in bytes
		return;
	} else if (element_size_bytes == 2 || element_size_bytes == 4 || element_size_bytes == 8) {
		// Use bit shift for powers of 2: SHL RCX, shift_amount
		int shift_amount = (element_size_bytes == 2) ? 1 : (element_size_bytes == 4) ? 2 : 3;
		textSectionData.push_back(0x48); // REX.W prefix for 64-bit operation
		textSectionData.push_back(0xC1); // SHL r/m64, imm8
		textSectionData.push_back(0xE1); // ModR/M: RCX (11 100 001)
		textSectionData.push_back(static_cast<uint8_t>(shift_amount)); // Shift amount
	} else {
		// General case: IMUL RCX, RCX, element_size_bytes
		textSectionData.push_back(0x48); // REX.W prefix for 64-bit operation
		textSectionData.push_back(0x69); // IMUL r64, r/m64, imm32
		textSectionData.push_back(0xC9); // ModR/M: RCX, RCX (11 001 001)
		// Little-endian 32-bit immediate
		uint32_t size_u32 = static_cast<uint32_t>(element_size_bytes);
		textSectionData.push_back(size_u32 & 0xFF);
		textSectionData.push_back((size_u32 >> 8) & 0xFF);
		textSectionData.push_back((size_u32 >> 16) & 0xFF);
		textSectionData.push_back((size_u32 >> 24) & 0xFF);
	}
}

/**
 * @brief Emits x64 opcodes for ADD RAX, RCX.
 *
 * Generates: 48 01 C8
 * - 0x48: REX.W prefix (64-bit operation)
 * - 0x01: ADD r/m64, r64
 * - 0xC8: ModR/M byte (11 001 000) = RAX (destination), RCX (source)
 *
 * @param textSectionData The vector to append opcodes to
 */
inline void emitAddRAXRCX(std::vector<char>& textSectionData) {
	textSectionData.push_back(0x48); // REX.W prefix for 64-bit operation
	textSectionData.push_back(0x01); // ADD r/m64, r64
	textSectionData.push_back(0xC8); // ModR/M: RAX (dest), RCX (source)
}

/**
 * @brief Emits x64 opcodes to load a variable from stack into RCX.
 *
 * Generates MOV RCX, [RBP + offset] with optimal displacement encoding:
 * - 8-bit displacement if -128 <= offset <= 127
 * - 32-bit displacement otherwise
 *
 * Opcodes:
 * - 0x48: REX.W prefix (64-bit operation)
 * - 0x8B: MOV r64, r/m64
 * - ModR/M: 0x4D (disp8) or 0x8D (disp32) for [RBP + disp], RCX
 *
 * @param textSectionData The vector to append opcodes to
 * @param offset The stack offset from RBP (negative for locals)
 */
inline void emitLoadIndexIntoRCX(std::vector<char>& textSectionData, int64_t offset) {
	textSectionData.push_back(0x48); // REX.W prefix for 64-bit operation
	textSectionData.push_back(0x8B); // MOV r64, r/m64
	
	if (offset >= -128 && offset <= 127) {
		// 8-bit displacement
		textSectionData.push_back(0x4D); // ModR/M: [RBP + disp8], RCX (01 001 101)
		textSectionData.push_back(static_cast<uint8_t>(offset));
	} else {
		// 32-bit displacement
		textSectionData.push_back(0x8D); // ModR/M: [RBP + disp32], RCX (10 001 101)
		uint32_t offset_u32 = static_cast<uint32_t>(offset);
		textSectionData.push_back(offset_u32 & 0xFF);
		textSectionData.push_back((offset_u32 >> 8) & 0xFF);
		textSectionData.push_back((offset_u32 >> 16) & 0xFF);
		textSectionData.push_back((offset_u32 >> 24) & 0xFF);
	}
}

/**
 * @brief Emits x64 opcodes for LEA RAX, [RBP + offset].
 *
 * Load effective address - computes RBP + offset without dereferencing.
 * Uses optimal displacement encoding (8-bit or 32-bit).
 *
 * Opcodes:
 * - 0x48: REX.W prefix (64-bit operation)
 * - 0x8D: LEA r64, m
 * - ModR/M: 0x45 (disp8) or 0x85 (disp32) for [RBP + disp], RAX
 *
 * @param textSectionData The vector to append opcodes to
 * @param offset The offset from RBP
 */
inline void emitLEAArrayBase(std::vector<char>& textSectionData, int64_t offset) {
	textSectionData.push_back(0x48); // REX.W prefix for 64-bit operation
	textSectionData.push_back(0x8D); // LEA r64, m
	
	if (offset >= -128 && offset <= 127) {
		// 8-bit displacement
		textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX (01 000 101)
		textSectionData.push_back(static_cast<uint8_t>(offset));
	} else {
		// 32-bit displacement
		textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX (10 000 101)
		uint32_t offset_u32 = static_cast<uint32_t>(offset);
		textSectionData.push_back(offset_u32 & 0xFF);
		textSectionData.push_back((offset_u32 >> 8) & 0xFF);
		textSectionData.push_back((offset_u32 >> 16) & 0xFF);
		textSectionData.push_back((offset_u32 >> 24) & 0xFF);
	}
}

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

	void convert(const Ir& ir, const std::string_view filename, const std::string_view source_filename = "", bool show_timing = false) {
		// High-level timing (always enabled when show_timing=true)
		auto convert_start = std::chrono::high_resolution_clock::now();

		// Group instructions by function for stack space calculation
		{
			ProfilingTimer timer("Group instructions by function", show_timing);
			groupInstructionsByFunction(ir);
		}

		// Detailed profiling accumulators (only active when ENABLE_DETAILED_PROFILING is set)
		#if ENABLE_DETAILED_PROFILING
		ProfilingAccumulator funcDecl_accum("FunctionDecl instructions");
		ProfilingAccumulator varDecl_accum("VariableDecl instructions");
		ProfilingAccumulator return_accum("Return instructions");
		ProfilingAccumulator funcCall_accum("FunctionCall instructions");
		ProfilingAccumulator arithmetic_accum("Arithmetic instructions");
		ProfilingAccumulator comparison_accum("Comparison instructions");
		ProfilingAccumulator control_flow_accum("Control flow instructions");
		ProfilingAccumulator memory_accum("Memory access instructions");
		#endif

		auto ir_processing_start = std::chrono::high_resolution_clock::now();
		for (const auto& instruction : ir.getInstructions()) {
			#if ENABLE_DETAILED_PROFILING
			auto instr_start = std::chrono::high_resolution_clock::now();
			#endif
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
			case IrOpcode::ScopeBegin:
				// No code generation needed - just a marker
				break;
			case IrOpcode::ScopeEnd:
				// No code generation needed - destructors are already emitted before this
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
			case IrOpcode::ArrayStore:
				handleArrayStore(instruction);
				break;
			case IrOpcode::StringLiteral:
				handleStringLiteral(instruction);
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
			case IrOpcode::ConstructorCall:
				handleConstructorCall(instruction);
				break;
			case IrOpcode::DestructorCall:
				handleDestructorCall(instruction);
				break;
			case IrOpcode::VirtualCall:
				handleVirtualCall(instruction);
				break;
			case IrOpcode::HeapAlloc:
				handleHeapAlloc(instruction);
				break;
			case IrOpcode::HeapAllocArray:
				handleHeapAllocArray(instruction);
				break;
			case IrOpcode::HeapFree:
				handleHeapFree(instruction);
				break;
			case IrOpcode::HeapFreeArray:
				handleHeapFreeArray(instruction);
				break;
			case IrOpcode::PlacementNew:
				handlePlacementNew(instruction);
				break;
			case IrOpcode::Typeid:
				handleTypeid(instruction);
				break;
			case IrOpcode::DynamicCast:
				handleDynamicCast(instruction);
				break;
			case IrOpcode::GlobalVariableDecl:
				handleGlobalVariableDecl(instruction);
				break;
			case IrOpcode::GlobalLoad:
				handleGlobalLoad(instruction);
				break;
			case IrOpcode::GlobalStore:
				handleGlobalStore(instruction);
				break;
			case IrOpcode::FunctionAddress:
				handleFunctionAddress(instruction);
				break;
			case IrOpcode::IndirectCall:
				handleIndirectCall(instruction);
				break;
			default:
				assert(false && "Not implemented yet");
				break;
			}

			#if ENABLE_DETAILED_PROFILING
			auto instr_end = std::chrono::high_resolution_clock::now();
			auto instr_duration = std::chrono::duration_cast<std::chrono::microseconds>(instr_end - instr_start);

			// Categorize and accumulate timing
			switch (instruction.getOpcode()) {
				case IrOpcode::FunctionDecl:
					funcDecl_accum.add(instr_duration);
					break;
				case IrOpcode::VariableDecl:
				case IrOpcode::StackAlloc:
					varDecl_accum.add(instr_duration);
					break;
				case IrOpcode::Return:
					return_accum.add(instr_duration);
					break;
				case IrOpcode::FunctionCall:
					funcCall_accum.add(instr_duration);
					break;
				case IrOpcode::Add:
				case IrOpcode::Subtract:
				case IrOpcode::Multiply:
				case IrOpcode::Divide:
				case IrOpcode::UnsignedDivide:
				case IrOpcode::Modulo:
				case IrOpcode::FloatAdd:
				case IrOpcode::FloatSubtract:
				case IrOpcode::FloatMultiply:
				case IrOpcode::FloatDivide:
				case IrOpcode::ShiftLeft:
				case IrOpcode::ShiftRight:
				case IrOpcode::UnsignedShiftRight:
				case IrOpcode::BitwiseAnd:
				case IrOpcode::BitwiseOr:
				case IrOpcode::BitwiseXor:
				case IrOpcode::BitwiseNot:
				case IrOpcode::LogicalNot:
				case IrOpcode::Negate:
				case IrOpcode::PreIncrement:
				case IrOpcode::PostIncrement:
				case IrOpcode::PreDecrement:
				case IrOpcode::PostDecrement:
					arithmetic_accum.add(instr_duration);
					break;
				case IrOpcode::Equal:
				case IrOpcode::NotEqual:
				case IrOpcode::LessThan:
				case IrOpcode::LessEqual:
				case IrOpcode::GreaterThan:
				case IrOpcode::GreaterEqual:
				case IrOpcode::UnsignedLessThan:
				case IrOpcode::UnsignedLessEqual:
				case IrOpcode::UnsignedGreaterThan:
				case IrOpcode::UnsignedGreaterEqual:
				case IrOpcode::FloatEqual:
				case IrOpcode::FloatNotEqual:
				case IrOpcode::FloatLessThan:
				case IrOpcode::FloatLessEqual:
				case IrOpcode::FloatGreaterThan:
				case IrOpcode::FloatGreaterEqual:
					comparison_accum.add(instr_duration);
					break;
				case IrOpcode::Label:
				case IrOpcode::Jump:
				case IrOpcode::JumpIfZero:
				case IrOpcode::JumpIfNotZero:
					control_flow_accum.add(instr_duration);
					break;
				case IrOpcode::Store:
				case IrOpcode::AddressOf:
				case IrOpcode::Dereference:
				case IrOpcode::MemberAccess:
				case IrOpcode::MemberStore:
				case IrOpcode::ArrayAccess:
					memory_accum.add(instr_duration);
					break;
				case IrOpcode::ConstructorCall:
				case IrOpcode::DestructorCall:
					funcCall_accum.add(instr_duration);
					break;
				default:
					break;
			}
			#endif // ENABLE_DETAILED_PROFILING
		}

		auto ir_processing_end = std::chrono::high_resolution_clock::now();
	
		// Clean up the last function's variable scope after all instructions are processed
		if (!variable_scopes.empty()) {
			variable_scopes.pop_back();
		}
	
		if (show_timing) {
			auto ir_duration = std::chrono::duration_cast<std::chrono::microseconds>(ir_processing_end - ir_processing_start);
			printf("    IR instruction processing: %8.3f ms\n", ir_duration.count() / 1000.0);
		}

		#if ENABLE_DETAILED_PROFILING
		printf("\n  Detailed instruction timing:\n");
		funcDecl_accum.print();
		varDecl_accum.print();
		return_accum.print();
		funcCall_accum.print();
		arithmetic_accum.print();
		comparison_accum.print();
		control_flow_accum.print();
		memory_accum.print();
		printf("\n");
		#endif

		// Use the provided source filename, or fall back to a default if not provided
		std::string actual_source_file = source_filename.empty() ? "test_debug.cpp" : std::string(source_filename);
		{
			ProfilingTimer timer("Add source file", show_timing);
			writer.add_source_file(actual_source_file);
		}

		{
			ProfilingTimer timer("Finalize sections", show_timing);
			finalizeSections();
		}

		{
			ProfilingTimer timer("Write object file", show_timing);
			writer.write(std::string(filename));
		}

		if (show_timing) {
			auto convert_end = std::chrono::high_resolution_clock::now();
			auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(convert_end - convert_start);
			printf("    Total code generation:     %8.3f ms\n", total_duration.count() / 1000.0);
		}
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
	// Helper function to generate REX prefix and ModR/M byte for register-to-register operations
	// x86-64 opcode extensions for instructions that encode the operation in the reg field of ModR/M
	enum class X64OpcodeExtension : uint8_t {
		ROL = 0,  // Rotate left
		ROR = 1,  // Rotate right
		RCL = 2,  // Rotate through carry left
		RCR = 3,  // Rotate through carry right
		SHL = 4,  // Shift left (same as SAL)
		SHR = 5,  // Shift right logical
		SAL = 6,  // Shift arithmetic left (same as SHL)
		SAR = 7,  // Shift arithmetic right
		
		TEST = 0, // TEST instruction (F6/F7)
		NOT = 2,  // NOT instruction
		NEG = 3,  // NEG instruction
		MUL = 4,  // Unsigned multiply
		IMUL = 5, // Signed multiply
		DIV = 6,  // Unsigned divide
		IDIV = 7  // Signed divide
	};

	// Used by arithmetic, bitwise, and comparison operations with R8-R15 support
	struct RegToRegEncoding {
		uint8_t rex_prefix;
		uint8_t modrm_byte;
	};

	// Enum for unary operations to enable helper function
	// BitwiseNot and Negate use opcode extensions 2 and 3 respectively
	enum class UnaryOperation {
		LogicalNot,
		BitwiseNot = 2,
		Negate = 3
	};
	
	RegToRegEncoding encodeRegToRegInstruction(X64Register reg_field, X64Register rm_field, bool include_rex_w = true) {
		RegToRegEncoding result;
		
		// Start with base REX prefix
		result.rex_prefix = include_rex_w ? 0x48 : 0x40; // REX.W or base REX
		
		// Set REX.R if reg_field (source in Reg field of ModR/M) is R8-R15
		if (static_cast<uint8_t>(reg_field) >= 8) {
			result.rex_prefix |= 0x04; // Set REX.R bit
		}
		
		// Set REX.B if rm_field (destination in R/M field of ModR/M) is R8-R15
		if (static_cast<uint8_t>(rm_field) >= 8) {
			result.rex_prefix |= 0x01; // Set REX.B bit
		}
		
		// Build ModR/M byte: Mod=11 (register-to-register), Reg=reg_field[2:0], R/M=rm_field[2:0]
		result.modrm_byte = 0xC0 + 
			((static_cast<uint8_t>(reg_field) & 0x07) << 3) + 
			(static_cast<uint8_t>(rm_field) & 0x07);
		
		return result;
	}
	
	// Helper for instructions with opcode extension (reg field is a constant, rm is the register)
	// Used by shift instructions and division which encode the operation in the reg field
	RegToRegEncoding encodeOpcodeExtInstruction(X64OpcodeExtension opcode_ext, X64Register rm_field, bool include_rex_w = true) {
		RegToRegEncoding result;
		
		result.rex_prefix = include_rex_w ? 0x48 : 0x40;
		
		// Check if rm_field needs REX.B (registers R8-R15)
		if (static_cast<uint8_t>(rm_field) >= 8) {
			result.rex_prefix |= 0x01; // Set REX.B
		}
		
		// Build ModR/M byte: 11 (register mode) + opcode extension in reg field + rm bits
		uint8_t ext_value = static_cast<uint8_t>(opcode_ext);
		result.modrm_byte = 0xC0 | ((ext_value & 0x07) << 3) | (static_cast<uint8_t>(rm_field) & 0x07);
		
		return result;
	}
	
	// Helper function to emit a comparison instruction (CMP + SETcc + MOVZX)
	void emitComparisonInstruction(const ArithmeticOperationContext& ctx, uint8_t setcc_opcode) {
		// Compare operands: cmp r/m64, r64
		auto cmp_encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg);
		std::array<uint8_t, 3> cmpInst = { cmp_encoding.rex_prefix, 0x39, cmp_encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on condition: setcc r8
		// IMPORTANT: Always use REX prefix (at least 0x40) for byte operations
		// Without REX, registers 4-7 map to AH, CH, DH, BH (high bytes)
		// With REX, registers 4-7 map to SPL, BPL, SIL, DIL (low bytes)
		// For registers 8-15, we need REX.B (0x41)
		uint8_t setcc_rex = (static_cast<uint8_t>(ctx.result_physical_reg) >= 8) ? 0x41 : 0x40;
		textSectionData.push_back(setcc_rex);
		std::array<uint8_t, 3> setccInst = { 0x0F, setcc_opcode, static_cast<uint8_t>(0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) & 0x07)) };
		textSectionData.insert(textSectionData.end(), setccInst.begin(), setccInst.end());

		// Zero-extend the low byte to full register: movzx r64, r8
		auto movzx_encoding = encodeRegToRegInstruction(ctx.result_physical_reg, ctx.result_physical_reg);
		std::array<uint8_t, 4> movzxInst = { movzx_encoding.rex_prefix, 0x0F, 0xB6, movzx_encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), movzxInst.begin(), movzxInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	ArithmeticOperationContext setupAndLoadArithmeticOperation(const IrInstruction& instruction, const char* operation_name) {
		assert(instruction.getOperandCount() == 7 &&
			   (std::string(operation_name) + " instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val").c_str());

		// Get type and size (from LHS)
		ArithmeticOperationContext ctx = {
			.type = instruction.getOperandAs<Type>(1),
			.size_in_bits = instruction.getOperandAs<int>(2),
			.result_operand = instruction.getOperand(0),
			.result_physical_reg = X64Register::Count,
			.rhs_physical_reg = X64Register::RCX
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

					if (is_floating_point_type(ctx.type)) {
						// For float/double, allocate an XMM register
						ctx.result_physical_reg = allocateXMMRegisterWithSpilling();
						bool is_float = (ctx.type == Type::Float);
						auto mov_opcodes = generateFloatMovFromFrame(ctx.result_physical_reg, lhs_var_id->second, is_float);
						textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					} else {
						// For integers, use regular MOV
						ctx.result_physical_reg = allocateRegisterWithSpilling();
						OpCodeWithSize mov_opcodes;
						if (ctx.size_in_bits <= 32) {
							mov_opcodes = generateMovFromFrame32(ctx.result_physical_reg, lhs_var_id->second);
						} else {
							mov_opcodes = generateMovFromFrame(ctx.result_physical_reg, lhs_var_id->second);
						}
						textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
						regAlloc.flushSingleDirtyRegister(ctx.result_physical_reg);
					}
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

				if (is_floating_point_type(ctx.type)) {
					// For float/double, allocate an XMM register
					ctx.result_physical_reg = allocateXMMRegisterWithSpilling();
					bool is_float = (ctx.type == Type::Float);
					auto mov_opcodes = generateFloatMovFromFrame(ctx.result_physical_reg, lhs_stack_var_addr, is_float);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				} else {
					// Check if this is a reference - if so, we need to dereference it
					auto ref_it = reference_stack_info_.find(lhs_stack_var_addr);
					if (ref_it != reference_stack_info_.end()) {
						// This is a reference - load the pointer first, then dereference
						ctx.result_physical_reg = allocateRegisterWithSpilling();
						// Load the pointer into the register
						auto load_ptr = generateMovFromFrame(ctx.result_physical_reg, lhs_stack_var_addr);
						textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(), load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
						// Now dereference: load from [register + 0]
						int value_size_bits = ref_it->second.value_size_bits;
						OpCodeWithSize deref_opcodes;
						if (value_size_bits == 64) {
							deref_opcodes = generateMovFromMemory(ctx.result_physical_reg, ctx.result_physical_reg, 0);
						} else if (value_size_bits == 32) {
							deref_opcodes = generateMovFromMemory32(ctx.result_physical_reg, ctx.result_physical_reg, 0);
						} else if (value_size_bits == 16) {
							deref_opcodes = generateMovFromMemory16(ctx.result_physical_reg, ctx.result_physical_reg, 0);
						} else if (value_size_bits == 8) {
							deref_opcodes = generateMovFromMemory8(ctx.result_physical_reg, ctx.result_physical_reg, 0);
						} else {
							assert(false && "Unsupported reference value size");
						}
						textSectionData.insert(textSectionData.end(), deref_opcodes.op_codes.begin(), deref_opcodes.op_codes.begin() + deref_opcodes.size_in_bytes);
					} else {
						// Not a reference, load normally with correct size
						ctx.result_physical_reg = allocateRegisterWithSpilling();
						OpCodeWithSize mov_opcodes;
						if (ctx.size_in_bits <= 32) {
							mov_opcodes = generateMovFromFrame32(ctx.result_physical_reg, lhs_stack_var_addr);
						} else {
							mov_opcodes = generateMovFromFrame(ctx.result_physical_reg, lhs_stack_var_addr);
						}
						textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					}
					regAlloc.flushSingleDirtyRegister(ctx.result_physical_reg);
				}
			}
		}
		else if (instruction.isOperandType<unsigned long long>(3)) {
			// LHS is a literal value
			auto lhs_value = instruction.getOperandAs<unsigned long long>(3);
			ctx.result_physical_reg = allocateRegisterWithSpilling();

			// Load the literal value into the register
			// mov reg, imm64
			uint8_t rex_prefix = 0x48; // REX.W
			uint8_t reg_num = static_cast<uint8_t>(ctx.result_physical_reg);
			
			// For R8-R15, set REX.B bit
			if (reg_num >= 8) {
				rex_prefix |= 0x01; // Set REX.B
				reg_num &= 0x07; // Use lower 3 bits for opcode
			}
			
			std::array<uint8_t, 10> movInst = { rex_prefix, static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0, 0, 0, 0, 0 };
			std::memcpy(&movInst[2], &lhs_value, sizeof(lhs_value));
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		}
		else if (instruction.isOperandType<double>(3)) {
			// LHS is a floating-point literal value
			auto lhs_value = instruction.getOperandAs<double>(3);
			ctx.result_physical_reg = allocateRegisterWithSpilling();

			// For floating-point, we need to load the value into an XMM register
			// Strategy: Load the bit pattern as integer into a GPR, then move to XMM
			// 1. Load double bits into a GPR using movabs
			// 2. Move from GPR to XMM using movq

			uint64_t bits;
			std::memcpy(&bits, &lhs_value, sizeof(bits));

			// Allocate a temporary GPR for the bit pattern
			X64Register temp_gpr = allocateRegisterWithSpilling();

			// movabs temp_gpr, imm64 (load bit pattern)
			uint8_t rex_prefix = 0x48; // REX.W
			uint8_t reg_num = static_cast<uint8_t>(temp_gpr);
			
			// For R8-R15, set REX.B bit
			if (reg_num >= 8) {
				rex_prefix |= 0x01; // Set REX.B
				reg_num &= 0x07; // Use lower 3 bits for opcode
			}
			
			std::array<uint8_t, 10> movInst = { rex_prefix, static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0, 0, 0, 0, 0 };
			std::memcpy(&movInst[2], &bits, sizeof(bits));
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());

			// movq xmm, r64 (66 REX.W 0F 6E /r) - move from GPR to XMM
			std::array<uint8_t, 5> movqInst = { 0x66, 0x48, 0x0F, 0x6E, 0xC0 };
			movqInst[4] = 0xC0 + (xmm_modrm_bits(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(temp_gpr);
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

					if (is_floating_point_type(ctx.type)) {
						// For float/double, allocate an XMM register
						ctx.rhs_physical_reg = allocateXMMRegisterWithSpilling();
						bool is_float = (ctx.type == Type::Float);
						auto mov_opcodes = generateFloatMovFromFrame(ctx.rhs_physical_reg, rhs_var_id->second, is_float);
						textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					} else {
						// For integers, use regular MOV
						ctx.rhs_physical_reg = allocateRegisterWithSpilling();
						OpCodeWithSize mov_opcodes;
						if (ctx.size_in_bits <= 32) {
							mov_opcodes = generateMovFromFrame32(ctx.rhs_physical_reg, rhs_var_id->second);
						} else {
							mov_opcodes = generateMovFromFrame(ctx.rhs_physical_reg, rhs_var_id->second);
						}
						textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
						regAlloc.flushSingleDirtyRegister(ctx.rhs_physical_reg);
					}
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

				if (is_floating_point_type(ctx.type)) {
					// For float/double, allocate an XMM register
					ctx.rhs_physical_reg = allocateXMMRegisterWithSpilling();
					bool is_float = (ctx.type == Type::Float);
					auto mov_opcodes = generateFloatMovFromFrame(ctx.rhs_physical_reg, rhs_stack_var_addr, is_float);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				} else {
					// Check if this is a reference - if so, we need to dereference it
					auto ref_it = reference_stack_info_.find(rhs_stack_var_addr);
					if (ref_it != reference_stack_info_.end()) {
						// This is a reference - load the pointer first, then dereference
						ctx.rhs_physical_reg = allocateRegisterWithSpilling();
						// Load the pointer into the register
						auto load_ptr = generateMovFromFrame(ctx.rhs_physical_reg, rhs_stack_var_addr);
						textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(), load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
						// Now dereference: load from [register + 0]
						int value_size_bits = ref_it->second.value_size_bits;
						OpCodeWithSize deref_opcodes;
						if (value_size_bits == 64) {
							deref_opcodes = generateMovFromMemory(ctx.rhs_physical_reg, ctx.rhs_physical_reg, 0);
						} else if (value_size_bits == 32) {
							deref_opcodes = generateMovFromMemory32(ctx.rhs_physical_reg, ctx.rhs_physical_reg, 0);
						} else if (value_size_bits == 16) {
							deref_opcodes = generateMovFromMemory16(ctx.rhs_physical_reg, ctx.rhs_physical_reg, 0);
						} else if (value_size_bits == 8) {
							deref_opcodes = generateMovFromMemory8(ctx.rhs_physical_reg, ctx.rhs_physical_reg, 0);
						} else {
							assert(false && "Unsupported reference value size");
						}
						textSectionData.insert(textSectionData.end(), deref_opcodes.op_codes.begin(), deref_opcodes.op_codes.begin() + deref_opcodes.size_in_bytes);
					} else {
						// Not a reference, load normally with correct size
						ctx.rhs_physical_reg = allocateRegisterWithSpilling();
						OpCodeWithSize mov_opcodes;
						if (ctx.size_in_bits <= 32) {
							mov_opcodes = generateMovFromFrame32(ctx.rhs_physical_reg, rhs_stack_var_addr);
						} else {
							mov_opcodes = generateMovFromFrame(ctx.rhs_physical_reg, rhs_stack_var_addr);
						}
						textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					}
					regAlloc.flushSingleDirtyRegister(ctx.rhs_physical_reg);
				}
			}
		}
		else if (instruction.isOperandType<unsigned long long>(6)) {
			// RHS is a literal value
			auto rhs_value = instruction.getOperandAs<unsigned long long>(6);
			ctx.rhs_physical_reg = allocateRegisterWithSpilling();

			// Load the literal value into the register
			// mov reg, imm64
			uint8_t rex_prefix = 0x48; // REX.W
			uint8_t reg_num = static_cast<uint8_t>(ctx.rhs_physical_reg);
			
			// For R8-R15, set REX.B bit
			if (reg_num >= 8) {
				rex_prefix |= 0x01; // Set REX.B
				reg_num &= 0x07; // Use lower 3 bits for opcode
			}
			
			std::array<uint8_t, 10> movInst = { rex_prefix, static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0, 0, 0, 0, 0 };
			std::memcpy(&movInst[2], &rhs_value, sizeof(rhs_value));
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		}
		else if (instruction.isOperandType<double>(6)) {
			// RHS is a floating-point literal value
			auto rhs_value = instruction.getOperandAs<double>(6);
			ctx.rhs_physical_reg = allocateRegisterWithSpilling();

			// For floating-point, we need to load the value into an XMM register
			// Strategy: Load the bit pattern as integer into a GPR, then move to XMM
			// 1. Load double bits into a GPR using movabs
			// 2. Move from GPR to XMM using movq

			uint64_t bits;
			std::memcpy(&bits, &rhs_value, sizeof(bits));

			// Allocate a temporary GPR for the bit pattern
			X64Register temp_gpr = allocateRegisterWithSpilling();

			// movabs temp_gpr, imm64 (load bit pattern)
			uint8_t rex_prefix = 0x48; // REX.W
			uint8_t reg_num = static_cast<uint8_t>(temp_gpr);
			
			// For R8-R15, set REX.B bit
			if (reg_num >= 8) {
				rex_prefix |= 0x01; // Set REX.B
				reg_num &= 0x07; // Use lower 3 bits for opcode
			}
			
			std::array<uint8_t, 10> movInst = { rex_prefix, static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0, 0, 0, 0, 0 };
			std::memcpy(&movInst[2], &bits, sizeof(bits));
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());

			// movq xmm, r64 (66 REX.W 0F 6E /r) - move from GPR to XMM
			std::array<uint8_t, 5> movqInst = { 0x66, 0x48, 0x0F, 0x6E, 0xC0 };
			movqInst[4] = 0xC0 + (xmm_modrm_bits(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(temp_gpr);
			textSectionData.insert(textSectionData.end(), movqInst.begin(), movqInst.end());

			// Release the temporary GPR
			regAlloc.release(temp_gpr);
		}
		
		// If result register hasn't been allocated yet (e.g., LHS is a literal), allocate one now
		if (ctx.result_physical_reg == X64Register::Count) {
			ctx.result_physical_reg = allocateRegisterWithSpilling();
		}

		if (std::holds_alternative<TempVar>(ctx.result_operand)) {
			const TempVar temp_var = std::get<TempVar>(ctx.result_operand);
			const int32_t stack_offset = getStackOffsetFromTempVar(temp_var);
			variable_scopes.back().identifier_offset[temp_var.name()] = stack_offset;
			// Only set stack variable offset for allocated registers (not XMM0/XMM1 used directly)
			if (ctx.result_physical_reg < X64Register::XMM0 || regAlloc.is_allocated(ctx.result_physical_reg)) {
				regAlloc.set_stack_variable_offset(ctx.result_physical_reg, stack_offset);
			}
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
				// Result is already in the correct register, no move needed
			}
			else {
				// Temp variable not currently in a register - keep it in actual_source_reg instead of spilling
				// Tell the register allocator that this register now holds this temp variable
				assert(variable_scopes.back().scope_stack_space <= res_stack_var_addr);
				regAlloc.set_stack_variable_offset(actual_source_reg, res_stack_var_addr);
				// Don't store to memory - keep the value in the register for subsequent operations
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
		std::string current_func_name;

		std::cerr << "DEBUG [groupInstructionsByFunction]: Processing " << ir.getInstructions().size() << " instructions\n";
		int func_decl_count = 0;
		for (const auto& instruction : ir.getInstructions()) {
			if (instruction.getOpcode() == IrOpcode::FunctionDecl) {
				func_decl_count++;
				// Function name can be either std::string or std::string_view (now at index 3)
				if (instruction.isOperandType<std::string>(3)) {
					current_func_name = instruction.getOperandAs<std::string>(3);
				} else if (instruction.isOperandType<std::string_view>(3)) {
					current_func_name = std::string(instruction.getOperandAs<std::string_view>(3));
				}
				std::cerr << "DEBUG [groupInstructionsByFunction]: Found FunctionDecl for '" << current_func_name << "'\n";
				// Only create a new vector if this function name doesn't exist yet
				if (function_instructions.find(current_func_name) == function_instructions.end()) {
					function_instructions[current_func_name] = std::vector<IrInstruction>();
				}
				// If it already exists, we'll just continue appending to it
			} else if (!current_func_name.empty()) {
				function_instructions[current_func_name].push_back(instruction);
			}
		}
		std::cerr << "DEBUG [groupInstructionsByFunction]: Processed " << func_decl_count << " functions\n";
		std::cerr << "DEBUG [groupInstructionsByFunction]: function_instructions map has " << function_instructions.size() << " entries\n";
		for (const auto& [fname, instrs] : function_instructions) {
			std::cerr << "  - '" << fname << "': " << instrs.size() << " instructions\n";
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

	struct ReferenceInfo {
		Type value_type = Type::Invalid;
		int value_size_bits = 0;
		bool is_rvalue_reference = false;
	};
	
	StackSpaceSize calculateFunctionStackSpace(const std::string& func_name, StackVariableScope& var_scope) {
		StackSpaceSize func_stack_space{};

		auto it = function_instructions.find(func_name);
		if (it == function_instructions.end()) {
			return func_stack_space; // No instructions found for this function
		}

		struct VarDecl {
			std::string_view var_name{};
			int size_in_bits{};
			size_t alignment{};  // Custom alignment from alignas(n), 0 = use natural alignment
		};
		std::vector<VarDecl> local_vars;
		
		// Track TempVar sizes from instructions that produce them
		std::unordered_map<std::string_view, int> temp_var_sizes;

		for (const auto& instruction : it->second) {
			// Look for TempVar operands in the instruction
			func_stack_space.shadow_stack_space |= (0x20 * !(instruction.getOpcode() != IrOpcode::FunctionCall));

			if (instruction.getOpcode() == IrOpcode::VariableDecl) {
				auto size_in_bits = instruction.getOperandAs<int>(1);
				// Operand 2 can be string_view or string
				std::string_view var_name;
				if (instruction.isOperandType<std::string_view>(2)) {
					var_name = instruction.getOperandAs<std::string_view>(2);
				} else if (instruction.isOperandType<std::string>(2)) {
					var_name = instruction.getOperandAs<std::string>(2);
				} else {
					assert(false && "VariableDecl operand 2 must be string_view or string");
					continue;
				}
				size_t custom_alignment = instruction.getOperandAs<unsigned long long>(3);

				// Check if this is an array declaration
				// Format: [type, size, name, custom_alignment, is_ref, is_rvalue_ref, is_array, array_size_type?, array_size_bits?, array_size_value?]
				bool is_reference = instruction.getOperandAs<bool>(4);
				bool is_array = instruction.getOperandAs<bool>(6);
				constexpr size_t kArrayInfoStart = 7;
				bool has_array_size = is_array && instruction.getOperandCount() >= kArrayInfoStart + 3 &&
									   instruction.isOperandType<Type>(kArrayInfoStart);
				int total_size_bits = size_in_bits;
				if (is_reference) {
					total_size_bits = 64;
				}
				if (has_array_size && instruction.isOperandType<unsigned long long>(kArrayInfoStart + 2)) {
					uint64_t array_size = instruction.getOperandAs<unsigned long long>(kArrayInfoStart + 2);
					total_size_bits = size_in_bits * static_cast<int>(array_size);
				}
				
				func_stack_space.named_vars_size += (total_size_bits / 8);
				local_vars.push_back(VarDecl{ .var_name = var_name, .size_in_bits = total_size_bits, .alignment = custom_alignment });
			}
			else {
				// Track TempVars and their sizes from result positions
				// Most arithmetic/logic instructions have format: [result_var, type, size, ...]
				// where operand 0 is result, operand 1 is type, operand 2 is size
				if (instruction.getOperandCount() >= 3 && 
				    instruction.isOperandType<TempVar>(0) &&
				    instruction.isOperandType<int>(2)) {
					auto temp_var = instruction.getOperandAs<TempVar>(0);
					int size_in_bits = instruction.getOperandAs<int>(2);
					temp_var_sizes[temp_var.name()] = size_in_bits;
				}
			}
		}

		// Now add all TempVars to local_vars with their actual sizes
		for (const auto& [temp_name, size_bits] : temp_var_sizes) {
			local_vars.push_back(VarDecl{ .var_name = temp_name, .size_in_bits = size_bits, .alignment = static_cast<size_t>((size_bits <= 32) ? 4 : 8) });
		}
		
		int_fast32_t stack_offset = 0;
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

		// Calculate total stack space needed (all variables are now allocated)
		func_stack_space.temp_vars_size = -stack_offset;  // Total space (stack_offset is most negative value)
		func_stack_space.named_vars_size = -stack_offset; // Same as temp_vars_size since they're combined now
		
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

	// Get stack offset for a TempVar by looking it up in the identifier_offset map
	int32_t getStackOffsetFromTempVar(TempVar tempVar) {
		// Look up the TempVar's offset in the identifier_offset map
		if (!variable_scopes.empty()) {
			auto it = variable_scopes.back().identifier_offset.find(tempVar.name());
			if (it != variable_scopes.back().identifier_offset.end()) {
				return it->second;
			}
		}
		// Fallback for old behavior if not found (shouldn't happen in correct code)
		// For RBP-relative addressing, temporary variables use negative offsets
		// TempVar stores 1-based numbers: TempVar(1) is at [rbp-8], TempVar(2) at [rbp-16], etc.
		return static_cast<int32_t>(tempVar.var_number * -8);
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

	// Allocate a register, spilling one to the stack if necessary
	X64Register allocateRegisterWithSpilling() {
		// Try to allocate a free register first
		for (auto& reg : regAlloc.registers) {
			if (!reg.isAllocated && reg.reg < X64Register::XMM0) {
				reg.isAllocated = true;
				return reg.reg;
			}
		}

		// No free registers - need to spill one
		auto reg_to_spill = regAlloc.findRegisterToSpill();
		if (!reg_to_spill.has_value()) {
			throw std::runtime_error("No registers available for spilling");
		}

		X64Register spill_reg = reg_to_spill.value();
		auto& reg_info = regAlloc.registers[static_cast<int>(spill_reg)];

		// If the register is dirty, write it back to the stack
		if (reg_info.isDirty && reg_info.stackVariableOffset != INT_MIN) {
			auto store_opcodes = generateMovToFrame(spill_reg, reg_info.stackVariableOffset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
			                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
		}

		// Release the register and allocate it again
		regAlloc.release(spill_reg);
		reg_info.isAllocated = true;
		return spill_reg;
	}

	// Allocate an XMM register, spilling one to the stack if necessary
	X64Register allocateXMMRegisterWithSpilling() {
		// Try to allocate a free XMM register first
		for (size_t i = static_cast<size_t>(X64Register::XMM0); i <= static_cast<size_t>(X64Register::XMM15); ++i) {
			if (!regAlloc.registers[i].isAllocated) {
				regAlloc.registers[i].isAllocated = true;
				return regAlloc.registers[i].reg;
			}
		}

		// No free XMM registers - need to spill one
		auto reg_to_spill = regAlloc.findXMMRegisterToSpill();
		if (!reg_to_spill.has_value()) {
			throw std::runtime_error("No XMM registers available for spilling");
		}

		X64Register spill_reg = reg_to_spill.value();
		auto& reg_info = regAlloc.registers[static_cast<int>(spill_reg)];

		// If the register is dirty, write it back to the stack
		if (reg_info.isDirty && reg_info.stackVariableOffset != INT_MIN) {
			// For XMM registers, use float mov to frame
			bool is_float = true;  // Assume float for now, could be improved
			auto store_opcodes = generateFloatMovToFrame(spill_reg, reg_info.stackVariableOffset, is_float);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
			                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
		}

		// Release the register and allocate it again
		regAlloc.release(spill_reg);
		reg_info.isAllocated = true;
		return spill_reg;
	}

	void handleFunctionCall(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() >= 2 && "Function call must have at least 2 ope rands (result, function name)");

		flushAllDirtyRegisters();

		struct StackArg {
			Type type;
			int size;
			IrOperand value;
		};
		std::vector<StackArg> stack_args;

		// Get result variable and function name
		auto result_var = instruction.getOperand(0);
		//auto funcName = instruction.getOperandAs<std::string_view>(1);

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
			int argSize = instruction.getOperandAs<int>(argIndex + 1);
			auto argValue = instruction.getOperand(argIndex + 2); // could be immediate or variable

			// Determine if this is a floating-point argument
			bool is_float_arg = is_floating_point_type(argType);

			// Check if this argument goes in a register or on the stack
			// x64 Windows calling convention: first 4 args in registers, rest on stack
			bool use_register = (i < 4);

			if (!use_register) {
				// Collect stack arguments for later processing
				stack_args.push_back({argType, argSize, argValue});
				continue;
			}

			// Determine the target register for the argument (if using registers)
			X64Register target_reg = is_float_arg ? FLOAT_PARAM_REGS[i] : INT_PARAM_REGS[i];

			// Handle floating-point immediate values
			if (is_float_arg && std::holds_alternative<double>(argValue)) {
				// Load floating-point literal into XMM register
				double float_value = std::get<double>(argValue);

				// Convert double to bit pattern
				uint64_t bits;
				std::memcpy(&bits, &float_value, sizeof(bits));

				// Allocate a temporary GPR for the bit pattern
				X64Register temp_gpr = allocateRegisterWithSpilling();

				// movabs temp_gpr, imm64 (load bit pattern)
				std::array<uint8_t, 10> movInst = { 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0 };
				movInst[1] = 0xB8 + static_cast<uint8_t>(temp_gpr);
				std::memcpy(&movInst[2], &bits, sizeof(bits));
				textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());

				// movq xmm, r64 (66 REX.W 0F 6E /r) - move from GPR to XMM
				std::array<uint8_t, 5> movqInst = { 0x66, 0x48, 0x0F, 0x6E, 0xC0 };
				movqInst[4] = 0xC0 + (xmm_modrm_bits(target_reg) << 3) + static_cast<uint8_t>(temp_gpr);
				textSectionData.insert(textSectionData.end(), movqInst.begin(), movqInst.end());				// Release the temporary GPR
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

				// Special case: For Struct type arguments, we need the ADDRESS (this pointer), not the value
				if (argType == Type::Struct) {
					// Load the address of the object using LEA
					// LEA target_reg, [RBP + var_offset]
					textSectionData.push_back(0x48); // REX.W prefix for 64-bit
					if (static_cast<uint8_t>(target_reg) >= static_cast<uint8_t>(X64Register::R8)) {
						textSectionData.back() |= (1 << 2); // Set REX.R bit for R8-R15
					}
					textSectionData.push_back(0x8D); // LEA opcode
					
					// ModR/M byte
					uint8_t reg_bits = static_cast<uint8_t>(target_reg) & 0x07;
					if (var_offset >= -128 && var_offset <= 127) {
						// Use disp8
						textSectionData.push_back(0x45 | (reg_bits << 3)); // Mod=01, Reg=target, R/M=101 (RBP)
						textSectionData.push_back(static_cast<uint8_t>(var_offset));
					} else {
						// Use disp32
						textSectionData.push_back(0x85 | (reg_bits << 3)); // Mod=10, Reg=target, R/M=101 (RBP)
						for (int j = 0; j < 4; ++j) {
							textSectionData.push_back(static_cast<uint8_t>(var_offset & 0xFF));
							var_offset >>= 8;
						}
					}
				} else if (is_float_arg) {
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
					if (auto reg_var = regAlloc.tryGetStackVariableRegister(var_offset); reg_var.has_value() &&
					    reg_var.value() != X64Register::RSP && reg_var.value() != X64Register::RBP) {
						if (reg_var.value() != target_reg) {
							auto movResultToRax = regAlloc.get_reg_reg_move_op_code(target_reg, reg_var.value(), argSize / 8);
							textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
						}
					}
					else {
						// Load from stack using RBP-relative addressing
						// Use size-appropriate load based on argument size
						OpCodeWithSize load_opcodes;
						if (argSize <= 32) {
							load_opcodes = generateMovFromFrame32(target_reg, var_offset);
						} else {
							load_opcodes = generateMovFromFrame(target_reg, var_offset);
						}
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
						if (auto reg_var = regAlloc.tryGetStackVariableRegister(var_offset); reg_var.has_value() &&
						    reg_var.value() != X64Register::RSP && reg_var.value() != X64Register::RBP) {
							if (reg_var.value() != target_reg) {
								auto movResultToRax = regAlloc.get_reg_reg_move_op_code(target_reg, reg_var.value(), 8);	// Use 64-bit move
								textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
							}
						}
						else {
							// Check if this is an 8-bit value that needs MOVZX
							if (argSize == 8) {
								// MOVZX target_reg, byte ptr [rbp+var_offset]
								// Encoding: 0F B6 /r with REX.R if target_reg >= R8
								uint8_t rex_prefix = 0;
								if (static_cast<uint8_t>(target_reg) >= 8) {
									rex_prefix = 0x44; // REX.R for R8-R15 destination
								}
								if (rex_prefix != 0) {
									textSectionData.push_back(rex_prefix);
								}
								textSectionData.push_back(0x0F);
								textSectionData.push_back(0xB6);
								
								// ModR/M byte: Mod=01/10 (disp8/disp32), Reg=target, R/M=101 (RBP)
								uint8_t reg_bits = static_cast<uint8_t>(target_reg) & 0x07;
								if (var_offset >= -128 && var_offset <= 127) {
									uint8_t modrm = 0x45 | (reg_bits << 3); // Mod=01, R/M=101 (RBP)
									textSectionData.push_back(modrm);
									textSectionData.push_back(static_cast<uint8_t>(var_offset));
								} else {
									uint8_t modrm = 0x85 | (reg_bits << 3); // Mod=10, R/M=101 (RBP)
									textSectionData.push_back(modrm);
									for (int j = 0; j < 4; ++j) {
										textSectionData.push_back(static_cast<uint8_t>(var_offset & 0xFF));
										var_offset >>= 8;
									}
								}
							} else {
								// Use the existing generateMovFromFrame function for consistency
								auto mov_opcodes = generateMovFromFrame(target_reg, var_offset);
								textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
							}
							regAlloc.flushSingleDirtyRegister(target_reg);
						}
					}
				} else {
					// Temporary variable not found in stack. This indicates a problem with our
					// TempVar management. Let's allocate a stack slot for it now and assume
					// the value is currently in RAX (from the most recent operation).

					// Allocate stack slot for this TempVar
					int stack_offset = allocateStackSlotForTempVar(src_reg_temp.var_number);

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
						int load_offset = allocateStackSlotForTempVar(src_reg_temp.var_number);
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

		// Push stack arguments in reverse order (right to left)
		for (auto it = stack_args.rbegin(); it != stack_args.rend(); ++it) {
			const auto& arg = *it;
			// Load value into a register
			X64Register temp_reg = allocateRegisterWithSpilling();

			// Handle different value types
			if (std::holds_alternative<unsigned long long>(arg.value)) {
				// Immediate value
				uint8_t rex_prefix = 0x48;
				if (static_cast<uint8_t>(temp_reg) >= static_cast<uint8_t>(X64Register::R8)) {
					rex_prefix |= (1 << 2);
				}
				textSectionData.push_back(rex_prefix);
				if (static_cast<uint8_t>(temp_reg) >= static_cast<uint8_t>(X64Register::R8)) {
					textSectionData.back() |= (1 << 0);
				}
				textSectionData.push_back(0xB8 + (static_cast<uint8_t>(temp_reg) & 0x07));
				unsigned long long value = std::get<unsigned long long>(arg.value);
				for (size_t j = 0; j < 8; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(value & 0xFF));
					value >>= 8;
				}
			} else if (std::holds_alternative<std::string_view>(arg.value)) {
				// Local variable
				std::string_view var_name = std::get<std::string_view>(arg.value);
				int var_offset = variable_scopes.back().identifier_offset[var_name];
				auto load_opcodes = generateMovFromFrame(temp_reg, var_offset);
				textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				regAlloc.flushSingleDirtyRegister(temp_reg);
			} else if (std::holds_alternative<TempVar>(arg.value)) {
				// Temp var
				auto temp_var = std::get<TempVar>(arg.value);
				int var_offset = getStackOffsetFromTempVar(temp_var);
				auto load_opcodes = generateMovFromFrame(temp_reg, var_offset);
				textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				regAlloc.flushSingleDirtyRegister(temp_reg);
			} else if (std::holds_alternative<double>(arg.value)) {
				// Floating-point literal
				double float_value = std::get<double>(arg.value);
				uint64_t bits;
				std::memcpy(&bits, &float_value, sizeof(bits));
				// Load bits into temp_reg
				uint8_t rex_prefix = 0x48;
				if (static_cast<uint8_t>(temp_reg) >= static_cast<uint8_t>(X64Register::R8)) {
					rex_prefix |= (1 << 2);
				}
				textSectionData.push_back(rex_prefix);
				if (static_cast<uint8_t>(temp_reg) >= static_cast<uint8_t>(X64Register::R8)) {
					textSectionData.back() |= (1 << 0);
				}
				textSectionData.push_back(0xB8 + (static_cast<uint8_t>(temp_reg) & 0x07));
				for (size_t j = 0; j < 8; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(bits & 0xFF));
					bits >>= 8;
				}
			}

			// Push the register
			textSectionData.push_back(0x50 + static_cast<uint8_t>(temp_reg)); // PUSH reg
			regAlloc.release(temp_reg);
		}

		// call [function name] instruction is 5 bytes
		// Function name can be either std::string (mangled) or std::string_view (unmangled)
		std::string function_name;
		if (instruction.isOperandType<std::string>(1)) {
			function_name = instruction.getOperandAs<std::string>(1);
		} else if (instruction.isOperandType<std::string_view>(1)) {
			function_name = std::string(instruction.getOperandAs<std::string_view>(1));
		}

		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());

		// Get the mangled name for the function (handles both regular and member functions)
		// If the function name is already mangled (starts with '?'), use it directly
		std::string mangled_name;
		if (!function_name.empty() && function_name[0] == '?') {
			mangled_name = function_name;  // Already mangled
			std::cerr << "DEBUG handleFunctionCall: function_name already mangled: " << mangled_name << "\n";
		} else {
			std::cerr << "DEBUG handleFunctionCall: function_name needs mangling: " << function_name << "\n";
			mangled_name = writer.getMangledName(function_name);  // Need to mangle
			std::cerr << "DEBUG handleFunctionCall: after getMangledName: " << mangled_name << "\n";
		}
		std::cerr << "DEBUG handleFunctionCall: about to add_relocation with: " << mangled_name << "\n";
		writer.add_relocation(textSectionData.size() - 4, mangled_name);

		// REFERENCE COMPATIBILITY: Don't add closing brace line mappings
		// The reference compiler (clang) only maps opening brace and actual statements
		// Closing braces are not mapped in the line information

		// Store the return value from RAX to the result variable's stack location
		// This is critical - without this, the return value in RAX gets overwritten
		// by subsequent operations before it's ever saved
		auto store_opcodes = generateMovToFrame(X64Register::RAX, result_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

		// Clean up stack arguments
		if (!stack_args.empty()) {
			int cleanup_size = static_cast<int>(stack_args.size()) * 8;
			if (cleanup_size <= 127) {
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0x83); // ADD RSP, imm8
				textSectionData.push_back(0xC4);
				textSectionData.push_back(static_cast<uint8_t>(cleanup_size));
			} else {
				textSectionData.push_back(0x48);
				textSectionData.push_back(0x81);
				textSectionData.push_back(0xC4);
				for (int i = 0; i < 4; ++i) {
					textSectionData.push_back(static_cast<uint8_t>(cleanup_size & 0xFF));
					cleanup_size >>= 8;
				}
			}
		}

		// NOTE: We used to call regAlloc.reset() here, but that was causing issues
		// with register allocation across multiple function calls. The register allocator
		// should track register state properly without needing a full reset after each call.
		// Just flush dirty registers before the call (which we already do above) and
		// let the register allocator manage state naturally.
		// regAlloc.reset();  // REMOVED - was causing bugs with multiple function calls
		
		// However, we DO need to invalidate volatile (caller-saved) registers after a call
		// because they may have been clobbered by the callee.
		// x86-64 Windows calling convention: RCX, RDX, R8, R9, R10, R11, XMM0-XMM5 are volatile
		regAlloc.release(X64Register::RCX);
		regAlloc.release(X64Register::RDX);
		regAlloc.release(X64Register::R8);
		regAlloc.release(X64Register::R9);
		regAlloc.release(X64Register::R10);
		regAlloc.release(X64Register::R11);
	}

	void handleConstructorCall(const IrInstruction& instruction) {
		// Constructor call format: [struct_name, object_var, param1_type, param1_size, param1_value, ...]
		assert(instruction.getOperandCount() >= 2 && "ConstructorCall must have at least 2 operands");

		flushAllDirtyRegisters();

		// Get struct name - it could be either std::string or std::string_view
		std::string struct_name;
		if (instruction.isOperandType<std::string>(0)) {
			struct_name = instruction.getOperandAs<std::string>(0);
		} else if (instruction.isOperandType<std::string_view>(0)) {
			struct_name = std::string(instruction.getOperandAs<std::string_view>(0));
		} else {
			throw std::runtime_error("Constructor call: first operand must be struct name (string or string_view)");
		}

		auto object_var = instruction.getOperand(1);

		// Get the object's stack offset
		int object_offset = 0;
		if (std::holds_alternative<TempVar>(object_var)) {
			const TempVar temp_var = std::get<TempVar>(object_var);
			object_offset = getStackOffsetFromTempVar(temp_var);
		} else if (std::holds_alternative<std::string>(object_var)) {
			const std::string& var_name_str = std::get<std::string>(object_var);
			std::string_view var_name(var_name_str);
			auto it = variable_scopes.back().identifier_offset.find(var_name);
			if (it == variable_scopes.back().identifier_offset.end()) {
				throw std::runtime_error("Constructor call: variable not found in identifier_offset map: " + std::string(var_name));
			}
			object_offset = it->second;
		} else {
			std::string_view var_name = std::get<std::string_view>(object_var);
			auto it = variable_scopes.back().identifier_offset.find(var_name);
			if (it == variable_scopes.back().identifier_offset.end()) {
				throw std::runtime_error("Constructor call: variable not found in identifier_offset map: " + std::string(var_name));
			}
			object_offset = it->second;
		}

		// Check if the object is 'this' (a pointer that needs to be reloaded)
		bool object_is_this_pointer = false;
		if (std::holds_alternative<std::string>(object_var)) {
			object_is_this_pointer = (std::get<std::string>(object_var) == "this");
		} else if (std::holds_alternative<std::string_view>(object_var)) {
			object_is_this_pointer = (std::get<std::string_view>(object_var) == "this");
		}

		// Load the address of the object into RCX (first parameter - 'this' pointer)
		if (object_is_this_pointer) {
			// For 'this' pointer: reload the pointer value (not its address)
			// MOV RCX, [RBP + object_offset]
			auto load_opcodes = generateMovFromFrame(X64Register::RCX, object_offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
		} else {
			// For regular objects: get the address
			// LEA RCX, [RBP + object_offset]
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x8D); // LEA opcode
			if (object_offset >= -128 && object_offset <= 127) {
				textSectionData.push_back(0x4D); // ModR/M: [RBP + disp8], RCX
				textSectionData.push_back(static_cast<uint8_t>(object_offset));
			} else {
				textSectionData.push_back(0x8D); // ModR/M: [RBP + disp32], RCX
				for (int j = 0; j < 4; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(object_offset & 0xFF));
					object_offset >>= 8;
				}
			}
		}

		// Process constructor parameters (if any) - similar to function call
		const size_t first_param_index = 2;
		const size_t param_stride = 3; // type, size, value
		const size_t num_params = (instruction.getOperandCount() - first_param_index) / param_stride;

		// Extract parameter types for overload resolution
		std::vector<TypeSpecifierNode> parameter_types;
		for (size_t i = 0; i < num_params; ++i) {
			size_t paramIndex = first_param_index + i * param_stride;
			Type paramType = instruction.getOperandAs<Type>(paramIndex);
			int paramSize = instruction.getOperandAs<int>(paramIndex + 1);
			
			// Build TypeSpecifierNode for this parameter
			TypeSpecifierNode param_type(paramType, TypeQualifier::None, static_cast<unsigned char>(paramSize), Token{});
			
			// For copy/move constructors: if parameter is the same struct type, it should be a const reference
			// Copy constructor: Type(const Type& other) -> paramType == Type::Struct and same as struct_name
			// We detect this by checking if paramType is Struct and num_params == 1
			if (num_params == 1 && paramType == Type::Struct) {
				// This is likely a copy constructor - recreate as const reference
				// Look up struct type index to create proper TypeSpecifierNode
				auto type_it = gTypesByName.find(struct_name);
				if (type_it != gTypesByName.end()) {
					TypeIndex struct_type_index = type_it->second->type_index_;
					param_type = TypeSpecifierNode(paramType, struct_type_index, static_cast<unsigned char>(paramSize), Token{}, CVQualifier::Const);
					param_type.set_reference(false);  // lvalue reference (const Type&)
				}
			}
			
			parameter_types.push_back(param_type);
		}

		// Load parameters into registers
		for (size_t i = 0; i < num_params && i < 3; ++i) { // Max 3 additional params (RCX is 'this')
			size_t paramIndex = first_param_index + i * param_stride;
			Type paramType = instruction.getOperandAs<Type>(paramIndex);
			int paramSize = instruction.getOperandAs<int>(paramIndex + 1);
			auto paramValue = instruction.getOperand(paramIndex + 2);

			X64Register target_reg = INT_PARAM_REGS[i + 1]; // Skip RCX (index 0)

			// Check if this is a reference parameter (copy/move constructor)
			bool is_reference_param = (num_params == 1 && paramType == Type::Struct);

			if (std::holds_alternative<unsigned long long>(paramValue)) {
				// Immediate value
				uint8_t rex_prefix = 0x48;
				if (static_cast<uint8_t>(target_reg) >= static_cast<uint8_t>(X64Register::R8)) {
					rex_prefix |= (1 << 2);
				}
				textSectionData.push_back(rex_prefix);

				if (static_cast<uint8_t>(target_reg) >= static_cast<uint8_t>(X64Register::R8)) {
					textSectionData.back() |= (1 << 0);
				}
				textSectionData.push_back(0xB8 + (static_cast<uint8_t>(target_reg) & 0x07));
				unsigned long long value = std::get<unsigned long long>(paramValue);
				for (size_t j = 0; j < 8; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(value & 0xFF));
					value >>= 8;
				}
			} else if (std::holds_alternative<TempVar>(paramValue)) {
				// Load from temp variable
				const TempVar temp_var = std::get<TempVar>(paramValue);
				int param_offset = getStackOffsetFromTempVar(temp_var);
				if (is_reference_param) {
					// For reference parameters, load address (LEA)
					// LEA target_reg, [RBP + param_offset]
					textSectionData.push_back(0x48); // REX.W prefix
					textSectionData.push_back(0x8D); // LEA opcode
					uint8_t target_reg_bits = static_cast<uint8_t>(target_reg) & 0x07;
					if (param_offset >= -128 && param_offset <= 127) {
						textSectionData.push_back(0x45 + (target_reg_bits << 3)); // ModR/M: [RBP + disp8], target_reg
						textSectionData.push_back(static_cast<uint8_t>(param_offset));
					} else {
						textSectionData.push_back(0x85 + (target_reg_bits << 3)); // ModR/M: [RBP + disp32], target_reg
						for (int j = 0; j < 4; ++j) {
							textSectionData.push_back(static_cast<uint8_t>(param_offset & 0xFF));
							param_offset >>= 8;
						}
					}
				} else {
					// For value parameters, load value (MOV)
					auto load_opcodes = generateMovFromFrame(target_reg, param_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				}
			} else if (std::holds_alternative<std::string_view>(paramValue)) {
				// Load from variable
				std::string_view var_name = std::get<std::string_view>(paramValue);
				auto it = variable_scopes.back().identifier_offset.find(var_name);
				if (it != variable_scopes.back().identifier_offset.end()) {
					int param_offset = it->second;
					if (is_reference_param) {
						// For reference parameters, load address (LEA)
						// LEA target_reg, [RBP + param_offset]
						textSectionData.push_back(0x48); // REX.W prefix
						textSectionData.push_back(0x8D); // LEA opcode
						uint8_t target_reg_bits = static_cast<uint8_t>(target_reg) & 0x07;
						if (param_offset >= -128 && param_offset <= 127) {
							textSectionData.push_back(0x45 + (target_reg_bits << 3)); // ModR/M: [RBP + disp8], target_reg
							textSectionData.push_back(static_cast<uint8_t>(param_offset));
						} else {
							textSectionData.push_back(0x85 + (target_reg_bits << 3)); // ModR/M: [RBP + disp32], target_reg
							for (int j = 0; j < 4; ++j) {
								textSectionData.push_back(static_cast<uint8_t>(param_offset & 0xFF));
								param_offset >>= 8;
							}
						}
					} else {
						// For value parameters, load value (MOV)
						auto load_opcodes = generateMovFromFrame(target_reg, param_offset);
						textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
						                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
					}
				}
			}
		}

		// Generate the call instruction
		std::string function_name = struct_name;  // Constructor name is same as struct name (no :: prefix)
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());

		// Build FunctionSignature for proper overload resolution
		TypeSpecifierNode void_return(Type::Void, TypeQualifier::None, 0, Token{});
		ObjectFileWriter::FunctionSignature sig(void_return, parameter_types);
		sig.class_name = struct_name;

		// Generate the correct mangled name for this specific constructor overload
		std::string mangled_name = writer.generateMangledName(function_name, sig);
		std::cerr << "DEBUG handleConstructorCall: function_name = " << function_name << ", num_params = " << num_params << "\n";
		std::cerr << "DEBUG handleConstructorCall: mangled_name = " << mangled_name << "\n";
		writer.add_relocation(textSectionData.size() - 4, mangled_name);

		regAlloc.reset();
	}

	void handleDestructorCall(const IrInstruction& instruction) {
		// Destructor call format: [struct_name, object_var]
		assert(instruction.getOperandCount() == 2 && "DestructorCall must have exactly 2 operands");

		flushAllDirtyRegisters();

		// Get struct name - it could be either std::string or std::string_view
		std::string struct_name;
		if (instruction.isOperandType<std::string>(0)) {
			struct_name = instruction.getOperandAs<std::string>(0);
		} else if (instruction.isOperandType<std::string_view>(0)) {
			struct_name = std::string(instruction.getOperandAs<std::string_view>(0));
		} else {
			throw std::runtime_error("Destructor call: first operand must be struct name (string or string_view)");
		}

		auto object_var = instruction.getOperand(1);

		// Get the object's stack offset
		int object_offset = 0;
		if (std::holds_alternative<TempVar>(object_var)) {
			const TempVar temp_var = std::get<TempVar>(object_var);
			object_offset = getStackOffsetFromTempVar(temp_var);
		} else if (std::holds_alternative<std::string>(object_var)) {
			const std::string& var_name_str = std::get<std::string>(object_var);
			std::string_view var_name(var_name_str);
			object_offset = variable_scopes.back().identifier_offset[var_name];
		} else {
			std::string_view var_name = std::get<std::string_view>(object_var);
			object_offset = variable_scopes.back().identifier_offset[var_name];
		}

		// Load the address of the object into RCX (first parameter - 'this' pointer)
		// LEA RCX, [RBP + object_offset]
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x8D); // LEA opcode
		if (object_offset >= -128 && object_offset <= 127) {
			textSectionData.push_back(0x4D); // ModR/M: [RBP + disp8], RCX
			textSectionData.push_back(static_cast<uint8_t>(object_offset));
		} else {
			textSectionData.push_back(0x8D); // ModR/M: [RBP + disp32], RCX
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(object_offset & 0xFF));
				object_offset >>= 8;
			}
		}

		// Generate the call instruction
		std::string function_name = std::string(struct_name) + "::~" + std::string(struct_name);
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());

		// Get the mangled name for the destructor (handles name mangling)
		std::string mangled_name = writer.getMangledName(function_name);
		writer.add_relocation(textSectionData.size() - 4, mangled_name);

		regAlloc.reset();
	}

	void handleVirtualCall(const IrInstruction& instruction) {
		// Virtual call format: [result_var, object_type, object_size, object_name, vtable_index, arg1_type, arg1_size, arg1_value, ...]
		assert(instruction.getOperandCount() >= 5 && "VirtualCall must have at least 5 operands");

		flushAllDirtyRegisters();

		// Get operands
		auto result_var = instruction.getOperand(0);
		//auto object_type = instruction.getOperandAs<Type>(1);
		//auto object_size = instruction.getOperandAs<int>(2);
		auto object_name_operand = instruction.getOperand(3);
		int vtable_index = instruction.getOperandAs<int>(4);

		// Get result offset
		int result_offset = 0;
		if (std::holds_alternative<TempVar>(result_var)) {
			const TempVar temp_var = std::get<TempVar>(result_var);
			result_offset = getStackOffsetFromTempVar(temp_var);
			variable_scopes.back().identifier_offset[temp_var.name()] = result_offset;
		} else {
			result_offset = variable_scopes.back().identifier_offset[std::get<std::string_view>(result_var)];
		}

		// Get object offset
		int object_offset = 0;
		if (std::holds_alternative<TempVar>(object_name_operand)) {
			const TempVar temp_var = std::get<TempVar>(object_name_operand);
			object_offset = getStackOffsetFromTempVar(temp_var);
		} else if (std::holds_alternative<std::string>(object_name_operand)) {
			const std::string& var_name_str = std::get<std::string>(object_name_operand);
			std::string_view var_name(var_name_str);
			object_offset = variable_scopes.back().identifier_offset[var_name];
		} else {
			std::string_view var_name = std::get<std::string_view>(object_name_operand);
			object_offset = variable_scopes.back().identifier_offset[var_name];
		}

		// Virtual call sequence:
		// 1. Load vptr from object: vptr = [object + 0]  (vptr is at offset 0)
		// 2. Load function pointer from vtable: func_ptr = [vptr + vtable_index * 8]
		// 3. Call through function pointer: call func_ptr(this, args...)

		// Step 1: Load vptr from object into RAX
		// MOV RAX, [RBP + object_offset]  ; Load vptr (first 8 bytes of object)
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x8B); // MOV r64, r/m64
		if (object_offset >= -128 && object_offset <= 127) {
			textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
			textSectionData.push_back(static_cast<uint8_t>(object_offset));
		} else {
			textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(object_offset & 0xFF));
				object_offset >>= 8;
			}
		}

		// Step 2: Load function pointer from vtable into RAX
		// MOV RAX, [RAX + vtable_index * 8]
		int vtable_offset = vtable_index * 8;
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x8B); // MOV r64, r/m64
		if (vtable_offset >= -128 && vtable_offset <= 127) {
			textSectionData.push_back(0x40); // ModR/M: [RAX + disp8], RAX
			textSectionData.push_back(static_cast<uint8_t>(vtable_offset));
		} else {
			textSectionData.push_back(0x80); // ModR/M: [RAX + disp32], RAX
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(vtable_offset & 0xFF));
				vtable_offset >>= 8;
			}
		}

		// Step 3: Set up 'this' pointer in RCX (first parameter in Windows x64 calling convention)
		// LEA RCX, [RBP + object_offset]
		object_offset = 0; // Reset object_offset (we modified it in step 1)
		if (std::holds_alternative<TempVar>(object_name_operand)) {
			const TempVar temp_var = std::get<TempVar>(object_name_operand);
			object_offset = getStackOffsetFromTempVar(temp_var);
		} else if (std::holds_alternative<std::string>(object_name_operand)) {
			const std::string& var_name_str = std::get<std::string>(object_name_operand);
			std::string_view var_name(var_name_str);
			object_offset = variable_scopes.back().identifier_offset[var_name];
		} else {
			std::string_view var_name = std::get<std::string_view>(object_name_operand);
			object_offset = variable_scopes.back().identifier_offset[var_name];
		}

		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x8D); // LEA opcode
		if (object_offset >= -128 && object_offset <= 127) {
			textSectionData.push_back(0x4D); // ModR/M: [RBP + disp8], RCX
			textSectionData.push_back(static_cast<uint8_t>(object_offset));
		} else {
			textSectionData.push_back(0x8D); // ModR/M: [RBP + disp32], RCX
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(object_offset & 0xFF));
				object_offset >>= 8;
			}
		}

		// TODO: Handle additional function arguments (operands 5+)
		// For now, we only support virtual calls with no additional arguments beyond 'this'

		// Step 4: Call through function pointer in RAX
		// CALL RAX
		textSectionData.push_back(0xFF); // CALL r/m64
		textSectionData.push_back(0xD0); // ModR/M: RAX

		// Step 5: Store return value from RAX to result variable
		auto store_opcodes = generateMovToFrame(X64Register::RAX, result_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

		regAlloc.reset();
	}

	void handleHeapAlloc(const IrInstruction& instruction) {
		// HeapAlloc format: [result_var, type, size_in_bytes, pointer_depth]
		assert(instruction.getOperandCount() == 4 && "HeapAlloc must have exactly 4 operands");

		flushAllDirtyRegisters();

		TempVar result_var = instruction.getOperandAs<TempVar>(0);
		int size_in_bytes = instruction.getOperandAs<int>(2);

		// Call malloc(size)
		// Windows x64 calling convention: RCX = first parameter (size)

		// Move size into RCX (first parameter)
		// MOV RCX, size_in_bytes
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0xC7); // MOV r/m64, imm32
		textSectionData.push_back(0xC1); // ModR/M for RCX
		for (int i = 0; i < 4; ++i) {
			textSectionData.push_back(static_cast<uint8_t>((size_in_bytes >> (i * 8)) & 0xFF));
		}

		// Call malloc
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());
		writer.add_relocation(textSectionData.size() - 4, "malloc");

		// Result is in RAX, store it to the result variable
		int result_offset = getStackOffsetFromTempVar(result_var);

		// MOV [RBP + result_offset], RAX
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x89); // MOV r/m64, r64
		if (result_offset >= -128 && result_offset <= 127) {
			textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
			textSectionData.push_back(static_cast<uint8_t>(result_offset));
		} else {
			textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(result_offset & 0xFF));
				result_offset >>= 8;
			}
		}

		regAlloc.reset();
	}

	void handleHeapAllocArray(const IrInstruction& instruction) {
		// HeapAllocArray format: [result_var, type, size_in_bytes, pointer_depth, count_operand]
		// count_operand can be either TempVar or a constant value (int/unsigned long long)
		assert(instruction.getOperandCount() == 5 && "HeapAllocArray must have exactly 5 operands");

		flushAllDirtyRegisters();

		TempVar result_var = instruction.getOperandAs<TempVar>(0);
		int element_size = instruction.getOperandAs<int>(2);

		// Load count into RAX - handle TempVar, std::string_view (identifier), and constant values
		if (instruction.isOperandType<TempVar>(4)) {
			// Count is a TempVar - load from stack
			TempVar count_var = instruction.getOperandAs<TempVar>(4);
			int count_offset = getStackOffsetFromTempVar(count_var);
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x8B); // MOV r64, r/m64
			if (count_offset >= -128 && count_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: RAX, [RBP + disp8]
				textSectionData.push_back(static_cast<uint8_t>(count_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: RAX, [RBP + disp32]
				for (int j = 0; j < 4; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(count_offset & 0xFF));
					count_offset >>= 8;
				}
			}
		} else if (instruction.isOperandType<std::string_view>(4)) {
			// Count is an identifier (variable name) - load from stack
			std::string_view count_name = instruction.getOperandAs<std::string_view>(4);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.identifier_offset.find(count_name);
			if (it == current_scope.identifier_offset.end()) {
				assert(false && "Array size variable not found in scope");
				return;
			}
			int count_offset = it->second;
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x8B); // MOV r64, r/m64
			if (count_offset >= -128 && count_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: RAX, [RBP + disp8]
				textSectionData.push_back(static_cast<uint8_t>(count_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: RAX, [RBP + disp32]
				for (int j = 0; j < 4; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(count_offset & 0xFF));
					count_offset >>= 8;
				}
			}
		} else {
			// Count is a constant - load immediate value
			uint64_t count_value = 0;
			if (instruction.isOperandType<int>(4)) {
				count_value = static_cast<uint64_t>(instruction.getOperandAs<int>(4));
			} else if (instruction.isOperandType<unsigned long long>(4)) {
				count_value = instruction.getOperandAs<unsigned long long>(4);
			} else {
				assert(false && "Count operand must be TempVar, std::string_view, int, or unsigned long long");
			}

			// MOV RAX, immediate
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0xB8); // MOV RAX, imm64
			for (int i = 0; i < 8; ++i) {
				textSectionData.push_back(static_cast<uint8_t>((count_value >> (i * 8)) & 0xFF));
			}
		}

		// Multiply count by element_size: IMUL RAX, element_size
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x69); // IMUL r64, r/m64, imm32
		textSectionData.push_back(0xC0); // ModR/M: RAX, RAX
		for (int i = 0; i < 4; ++i) {
			textSectionData.push_back(static_cast<uint8_t>((element_size >> (i * 8)) & 0xFF));
		}

		// Move result to RCX (first parameter for malloc)
		// MOV RCX, RAX
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x89); // MOV r/m64, r64
		textSectionData.push_back(0xC1); // ModR/M: RCX, RAX

		// Call malloc
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());
		writer.add_relocation(textSectionData.size() - 4, "malloc");

		// Result is in RAX, store it to the result variable
		int result_offset = getStackOffsetFromTempVar(result_var);

		// MOV [RBP + result_offset], RAX
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x89); // MOV r/m64, r64
		if (result_offset >= -128 && result_offset <= 127) {
			textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
			textSectionData.push_back(static_cast<uint8_t>(result_offset));
		} else {
			textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(result_offset & 0xFF));
				result_offset >>= 8;
			}
		}

		regAlloc.reset();
	}

	void handleHeapFree(const IrInstruction& instruction) {
		// HeapFree format: [ptr_operand] where ptr_operand can be TempVar or std::string_view (identifier)
		assert(instruction.getOperandCount() == 1 && "HeapFree must have exactly 1 operand");

		flushAllDirtyRegisters();

		// Get the pointer offset (from either TempVar or identifier)
		int ptr_offset = 0;
		if (instruction.isOperandType<TempVar>(0)) {
			TempVar ptr_var = instruction.getOperandAs<TempVar>(0);
			ptr_offset = getStackOffsetFromTempVar(ptr_var);
		} else if (instruction.isOperandType<std::string_view>(0)) {
			std::string_view var_name = instruction.getOperandAs<std::string_view>(0);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.identifier_offset.find(var_name);
			if (it == current_scope.identifier_offset.end()) {
				assert(false && "Variable not found in scope");
				return;
			}
			ptr_offset = it->second;
		} else {
			assert(false && "HeapFree operand must be TempVar or std::string_view");
			return;
		}

		// Load pointer from stack into RCX (first parameter for free)
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x8B); // MOV r64, r/m64
		if (ptr_offset >= -128 && ptr_offset <= 127) {
			textSectionData.push_back(0x4D); // ModR/M: RCX, [RBP + disp8]
			textSectionData.push_back(static_cast<uint8_t>(ptr_offset));
		} else {
			textSectionData.push_back(0x8D); // ModR/M: RCX, [RBP + disp32]
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(ptr_offset & 0xFF));
				ptr_offset >>= 8;
			}
		}

		// Call free
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());
		writer.add_relocation(textSectionData.size() - 4, "free");

		regAlloc.reset();
	}

	void handleHeapFreeArray(const IrInstruction& instruction) {
		// HeapFreeArray format: [ptr_var]
		// For C++, delete[] is the same as delete for POD types (just calls free)
		// For types with destructors, we'd need to call destructors for each element first
		// This is a simplified implementation
		handleHeapFree(instruction);
	}

	void handlePlacementNew(const IrInstruction& instruction) {
		// PlacementNew format: [result_var, type, size_in_bytes, pointer_depth, address_operand]
		// address_operand can be TempVar, std::string_view (identifier), or constant
		assert(instruction.getOperandCount() == 5 && "PlacementNew must have exactly 5 operands");

		flushAllDirtyRegisters();

		TempVar result_var = instruction.getOperandAs<TempVar>(0);

		// Load the placement address into RAX
		// The address can be a TempVar, identifier, or constant
		if (instruction.isOperandType<TempVar>(4)) {
			// Address is a TempVar - load from stack
			TempVar address_var = instruction.getOperandAs<TempVar>(4);
			int address_offset = getStackOffsetFromTempVar(address_var);
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x8B); // MOV r64, r/m64
			if (address_offset >= -128 && address_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: RAX, [RBP + disp8]
				textSectionData.push_back(static_cast<uint8_t>(address_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: RAX, [RBP + disp32]
				for (int j = 0; j < 4; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(address_offset & 0xFF));
					address_offset >>= 8;
				}
			}
		} else if (instruction.isOperandType<std::string_view>(4)) {
			// Address is an identifier (variable name) - load from stack
			std::string_view address_name = instruction.getOperandAs<std::string_view>(4);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.identifier_offset.find(address_name);
			if (it == current_scope.identifier_offset.end()) {
				assert(false && "Placement address variable not found in scope");
				return;
			}
			int address_offset = it->second;
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x8B); // MOV r64, r/m64
			if (address_offset >= -128 && address_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: RAX, [RBP + disp8]
				textSectionData.push_back(static_cast<uint8_t>(address_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: RAX, [RBP + disp32]
				for (int j = 0; j < 4; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(address_offset & 0xFF));
					address_offset >>= 8;
				}
			}
		} else if (instruction.isOperandType<unsigned long long>(4)) {
			// Address is a constant - load immediate value
			uint64_t address_value = instruction.getOperandAs<unsigned long long>(4);
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0xB8); // MOV RAX, imm64
			for (int j = 0; j < 8; ++j) {
				textSectionData.push_back(static_cast<uint8_t>((address_value >> (j * 8)) & 0xFF));
			}
		} else if (instruction.isOperandType<int>(4)) {
			// Address is a constant int - load immediate value
			int address_value = instruction.getOperandAs<int>(4);
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0xC7); // MOV r/m64, imm32
			textSectionData.push_back(0xC0); // ModR/M for RAX
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>((address_value >> (j * 8)) & 0xFF));
			}
		} else {
			assert(false && "Placement address must be TempVar, identifier, int, or unsigned long long");
			return;
		}

		// Store the placement address to the result variable
		// No malloc call - we just use the provided address
		int result_offset = getStackOffsetFromTempVar(result_var);

		// MOV [RBP + result_offset], RAX
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x89); // MOV r/m64, r64
		if (result_offset >= -128 && result_offset <= 127) {
			textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
			textSectionData.push_back(static_cast<uint8_t>(result_offset));
		} else {
			textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(result_offset & 0xFF));
				result_offset >>= 8;
			}
		}

		regAlloc.reset();
	}

	void handleTypeid(const IrInstruction& instruction) {
		// Typeid format: [result_var, type_name_or_expr, is_type]
		// For now, we'll implement a simplified version that returns a pointer to static RTTI data
		assert(instruction.getOperandCount() == 3 && "Typeid must have exactly 3 operands");

		flushAllDirtyRegisters();

		TempVar result_var = instruction.getOperandAs<TempVar>(0);
		bool is_type = instruction.getOperandAs<bool>(2);

		if (is_type) {
			// typeid(Type) - compile-time constant
			// For now, return a dummy pointer (in a full implementation, we'd have a .rdata section with type_info)
			const std::string& type_name = instruction.getOperandAs<std::string>(1);

			// Load address of type_info into RAX (using a placeholder address for now)
			// In a real implementation, we'd have a symbol for each type's RTTI data
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0xB8); // MOV RAX, imm64

			// Use a hash of the type name as a placeholder address
			size_t type_hash = std::hash<std::string>{}(type_name);
			for (int j = 0; j < 8; ++j) {
				textSectionData.push_back(static_cast<uint8_t>((type_hash >> (j * 8)) & 0xFF));
			}
		} else {
			// typeid(expr) - may need runtime lookup for polymorphic types
			// For polymorphic types, RTTI pointer is at vtable[-1]
			// For non-polymorphic types, return compile-time constant

			// Load the expression result (should be a pointer to object)
			TempVar expr_var = instruction.getOperandAs<TempVar>(1);
			int expr_offset = getStackOffsetFromTempVar(expr_var);

			// Load object pointer into RAX
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x8B); // MOV r64, r/m64
			if (expr_offset >= -128 && expr_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: RAX, [RBP + disp8]
				textSectionData.push_back(static_cast<uint8_t>(expr_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: RAX, [RBP + disp32]
				for (int j = 0; j < 4; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(expr_offset & 0xFF));
					expr_offset >>= 8;
				}
			}

			// Load vtable pointer from object (first 8 bytes)
			// MOV RAX, [RAX]
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x8B); // MOV r64, r/m64
			textSectionData.push_back(0x00); // ModR/M: RAX, [RAX]

			// Load RTTI pointer from vtable[-1] (8 bytes before vtable)
			// MOV RAX, [RAX - 8]
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x8B); // MOV r64, r/m64
			textSectionData.push_back(0x40); // ModR/M: RAX, [RAX + disp8]
			textSectionData.push_back(static_cast<uint8_t>(-8)); // -8 offset
		}

		// Store result to stack
		int result_offset = getStackOffsetFromTempVar(result_var);
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x89); // MOV r/m64, r64
		if (result_offset >= -128 && result_offset <= 127) {
			textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
			textSectionData.push_back(static_cast<uint8_t>(result_offset));
		} else {
			textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(result_offset & 0xFF));
				result_offset >>= 8;
			}
		}

		regAlloc.reset();
	}

	void handleDynamicCast(const IrInstruction& instruction) {
		// DynamicCast format: [result_var, source_ptr, target_type_name, is_reference]
		// Returns nullptr for failed pointer casts, throws for failed reference casts
		assert(instruction.getOperandCount() == 4 && "DynamicCast must have exactly 4 operands");

		flushAllDirtyRegisters();

		TempVar result_var = instruction.getOperandAs<TempVar>(0);
		TempVar source_var = instruction.getOperandAs<TempVar>(1);
		const std::string& target_type = instruction.getOperandAs<std::string>(2);
		bool is_reference = instruction.getOperandAs<bool>(3);

		// Load source pointer into RAX
		int source_offset = getStackOffsetFromTempVar(source_var);
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x8B); // MOV r64, r/m64
		if (source_offset >= -128 && source_offset <= 127) {
			textSectionData.push_back(0x45); // ModR/M: RAX, [RBP + disp8]
			textSectionData.push_back(static_cast<uint8_t>(source_offset));
		} else {
			textSectionData.push_back(0x85); // ModR/M: RAX, [RBP + disp32]
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(source_offset & 0xFF));
				source_offset >>= 8;
			}
		}

		// For a full implementation, we would:
		// 1. Check if source pointer is null (return null if so)
		// 2. Load vtable pointer from object
		// 3. Load RTTI pointer from vtable[-1]
		// 4. Call isDerivedFrom() to check if cast is valid
		// 5. Return source pointer if valid, nullptr if not

		// Simplified implementation: just return the source pointer
		// (assumes all casts succeed - this is a placeholder)
		// In a real implementation, we'd generate a call to a runtime function

		// For now, just copy source to result (optimistic cast)
		// A proper implementation would check RTTI and potentially return nullptr

		// Store result (RAX already contains source pointer)
		int result_offset = getStackOffsetFromTempVar(result_var);
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x89); // MOV r/m64, r64
		if (result_offset >= -128 && result_offset <= 127) {
			textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
			textSectionData.push_back(static_cast<uint8_t>(result_offset));
		} else {
			textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(result_offset & 0xFF));
				result_offset >>= 8;
			}
		}

		regAlloc.reset();
	}

	void handleGlobalVariableDecl(const IrInstruction& instruction) {
		// Format: [type, size_in_bits, var_name, is_initialized, init_value?]
		assert(instruction.getOperandCount() >= 4 && "GlobalVariableDecl must have at least 4 operands");

		Type var_type = instruction.getOperandAs<Type>(0);
		int size_in_bits = instruction.getOperandAs<int>(1);
		// var_name can be either std::string (for static locals) or std::string_view (for regular globals)
		std::string var_name;
		if (std::holds_alternative<std::string>(instruction.getOperand(2))) {
			var_name = instruction.getOperandAs<std::string>(2);
		} else {
			var_name = std::string(instruction.getOperandAs<std::string_view>(2));
		}
		bool is_initialized = instruction.getOperandAs<bool>(3);

		// Store global variable info for later use
		// We'll need to create .data or .bss sections and add symbols
		// For now, just track the global variable
		GlobalVariableInfo global_info;
		global_info.name = var_name;  // Already a std::string
		global_info.type = var_type;
		global_info.size_in_bytes = size_in_bits / 8;
		global_info.is_initialized = is_initialized;

		if (is_initialized && instruction.getOperandCount() >= 5) {
			// Get the initialization value
			const IrOperand& init_operand = instruction.getOperand(4);
			if (std::holds_alternative<unsigned long long>(init_operand)) {
				global_info.init_value = std::get<unsigned long long>(init_operand);
			} else if (std::holds_alternative<int>(init_operand)) {
				global_info.init_value = static_cast<unsigned long long>(std::get<int>(init_operand));
			}
		}

		global_variables_.push_back(global_info);
	}

	void handleGlobalLoad(const IrInstruction& instruction) {
		// Format: [result_temp, global_name]
		assert(instruction.getOperandCount() == 2 && "GlobalLoad must have exactly 2 operands");

		TempVar result_temp = instruction.getOperandAs<TempVar>(0);
		// Global name can be either std::string (for static locals) or std::string_view (for regular globals)
		std::string global_name;
		if (std::holds_alternative<std::string>(instruction.getOperand(1))) {
			global_name = instruction.getOperandAs<std::string>(1);
		} else {
			global_name = std::string(instruction.getOperandAs<std::string_view>(1));
		}

		// Load global variable using RIP-relative addressing
		// MOV EAX, [RIP + displacement]
		// Opcode: 8B 05 [4-byte displacement]
		// The displacement will be filled in by a relocation

		textSectionData.push_back(0x8B); // MOV r32, r/m32
		textSectionData.push_back(0x05); // ModR/M: EAX, [RIP + disp32]

		// Add a placeholder displacement (will be fixed by relocation)
		uint32_t reloc_offset = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Add a pending relocation for this global variable reference
		// We can't add it now because the symbol doesn't exist yet
		// It will be added in finalizeSections() after globals are emitted
		pending_global_relocations_.push_back({reloc_offset, std::string(global_name), IMAGE_REL_AMD64_REL32});

		// Store the loaded value to the stack
		int result_offset = getStackOffsetFromTempVar(result_temp);
		textSectionData.push_back(0x89); // MOV r/m32, r32
		if (result_offset >= -128 && result_offset <= 127) {
			textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], EAX
			textSectionData.push_back(static_cast<uint8_t>(result_offset));
		} else {
			textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], EAX
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(result_offset & 0xFF));
				result_offset >>= 8;
			}
		}
	}

	void handleGlobalStore(const IrInstruction& instruction) {
		// Format: [global_name, source_temp]
		assert(instruction.getOperandCount() == 2 && "GlobalStore must have exactly 2 operands");

		std::string_view global_name = instruction.getOperandAs<std::string_view>(0);
		TempVar source_temp = instruction.getOperandAs<TempVar>(1);

		// Store to global variable using RIP-relative addressing
		// MOV [RIP + offset_to_global], RAX
		// We'll need to add a relocation for this

		// For now, this is a placeholder
		// TODO: Implement RIP-relative addressing and relocations
	}

	void handleVariableDecl(const IrInstruction& instruction) {
		auto var_type = instruction.getOperandAs<Type>(0);
		// Operand 2 can be string_view or string
		std::string var_name_str;
		if (instruction.isOperandType<std::string_view>(2)) {
			var_name_str = std::string(instruction.getOperandAs<std::string_view>(2));
		} else if (instruction.isOperandType<std::string>(2)) {
			var_name_str = instruction.getOperandAs<std::string>(2);
		} else {
			assert(false && "VariableDecl operand 2 must be string_view or string");
			return;
		}
		// Operand 3 is custom_alignment (added for alignas support)
		StackVariableScope& current_scope = variable_scopes.back();
		auto var_it = current_scope.identifier_offset.find(var_name_str);
		assert(var_it != current_scope.identifier_offset.end());

		bool is_reference = instruction.getOperandAs<bool>(4);
		bool is_rvalue_reference = instruction.getOperandAs<bool>(5);
		bool is_array = instruction.getOperandAs<bool>(6);
		constexpr size_t kArrayInfoStart = 7;
		bool has_array_info = is_array && instruction.getOperandCount() >= kArrayInfoStart + 3 &&
		                     instruction.isOperandType<Type>(kArrayInfoStart);
		size_t initializer_index = has_array_info ? kArrayInfoStart + 3 : kArrayInfoStart;
		bool is_initialized = instruction.getOperandCount() > initializer_index;
		size_t init_value_index = initializer_index + 2;

		if (is_reference) {
			reference_stack_info_[var_it->second] = ReferenceInfo{
				.value_type = var_type,
				.value_size_bits = instruction.getOperandAs<int>(1),
				.is_rvalue_reference = is_rvalue_reference
			};
			int32_t dst_offset = var_it->second;
			X64Register pointer_reg = allocateRegisterWithSpilling();
			bool pointer_initialized = false;
			if (is_initialized) {
				pointer_initialized = loadAddressForOperand(instruction, init_value_index, pointer_reg);
				if (!pointer_initialized) {
					std::cerr << "ERROR: Reference initializer is not an addressable lvalue" << std::endl;
					assert(false && "Reference initializer must be an lvalue");
				}
			} else {
				moveImmediateToRegister(pointer_reg, 0);
			}
			auto store_ptr = generateMovToFrame(pointer_reg, dst_offset);
			textSectionData.insert(textSectionData.end(), store_ptr.op_codes.begin(),
				               store_ptr.op_codes.begin() + store_ptr.size_in_bytes);
			regAlloc.release(pointer_reg);
			return;
		}

		// Format: [type, size, name, custom_alignment, is_ref, is_rvalue_ref, ...] for uninitialized
		//         [type, size, name, custom_alignment, is_ref, is_rvalue_ref, init_type, init_size, init_value] for initialized
		X64Register allocated_reg_val = X64Register::RAX; // Default

		if (is_initialized) {
			auto dst_offset = var_it->second;

			// Check if the initializer is a literal value
			bool is_literal = instruction.isOperandType<unsigned long long>(init_value_index) ||
			                  instruction.isOperandType<int>(init_value_index) ||
			                  instruction.isOperandType<double>(init_value_index);

			if (is_literal) {
				// For literal initialization, allocate a register temporarily
				allocated_reg_val = allocateRegisterWithSpilling();
				// Load immediate value into register
				if (instruction.isOperandType<double>(init_value_index)) {
					// Handle double/float literals
					double value = instruction.getOperandAs<double>(init_value_index);

					// Convert double to bit pattern
					uint64_t bits;
					std::memcpy(&bits, &value, sizeof(bits));

					// For floating-point types, we need to use XMM registers
					// But for now, we'll store the bit pattern in a GPR and let the register allocator handle it
					// This matches the pattern used for function arguments

					// MOV reg, imm64 (load bit pattern)
					std::array<uint8_t, 10> movInst = { 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0 };
					movInst[0] = 0x48 | (static_cast<uint8_t>(allocated_reg_val) >> 3);
					movInst[1] = 0xB8 + (static_cast<uint8_t>(allocated_reg_val) & 0x7);
					std::memcpy(&movInst[2], &bits, sizeof(bits));
					textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
				} else if (instruction.isOperandType<unsigned long long>(init_value_index)) {
					uint64_t value = instruction.getOperandAs<unsigned long long>(init_value_index);

					// MOV reg, imm64
					std::array<uint8_t, 10> movInst = { 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0 };
					movInst[0] = 0x48 | (static_cast<uint8_t>(allocated_reg_val) >> 3);
					movInst[1] = 0xB8 + (static_cast<uint8_t>(allocated_reg_val) & 0x7);
					std::memcpy(&movInst[2], &value, sizeof(value));
					textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
				} else if (instruction.isOperandType<int>(init_value_index)) {
					int value = instruction.getOperandAs<int>(init_value_index);

					// MOV reg, imm32 (sign-extended to 64-bit)
					std::array<uint8_t, 7> movInst = { 0x48, 0xC7, 0xC0, 0, 0, 0, 0 };
					movInst[0] = 0x48 | (static_cast<uint8_t>(allocated_reg_val) >> 3);
					movInst[2] = 0xC0 + (static_cast<uint8_t>(allocated_reg_val) & 0x7);
					std::memcpy(&movInst[3], &value, sizeof(value));
					textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
				}

				// Store the value from register to stack
				// This is necessary so that later accesses to the variable can read from the stack
				auto store_opcodes = generateMovToFrame(allocated_reg_val, dst_offset);
				textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
				                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

				// Release the register since the value is now in the stack
				// This ensures future accesses will load from the stack instead of using a stale register value
				regAlloc.release(allocated_reg_val);
			} else {
				// Load from memory (TempVar or variable)
				// For non-literal initialization, we don't allocate a register
				// We just copy the value from source to destination on the stack
				int src_offset = 0;
				if (instruction.isOperandType<TempVar>(init_value_index)) {
					auto temp_var = instruction.getOperandAs<TempVar>(init_value_index);
					src_offset = getStackOffsetFromTempVar(temp_var);
				} else if (instruction.isOperandType<std::string_view>(init_value_index)) {
					auto rvalue_var_name = instruction.getOperandAs<std::string_view>(init_value_index);
					auto src_it = current_scope.identifier_offset.find(rvalue_var_name);
					if (src_it == current_scope.identifier_offset.end()) {
						std::cerr << "ERROR: Variable '" << rvalue_var_name << "' not found in symbol table\n";
						std::cerr << "Available variables in current scope:\n";
						for (const auto& [name, offset] : current_scope.identifier_offset) {
							std::cerr << "  - " << name << " at offset " << offset << "\n";
						}
					}
					assert(src_it != current_scope.identifier_offset.end());
					src_offset = src_it->second;
				}

				if (auto src_reg = regAlloc.tryGetStackVariableRegister(src_offset); src_reg.has_value()) {
					// Source value is already in a register (e.g., from function return)
					// Store it directly to the destination stack location
					auto store_opcodes = generateMovToFrame(src_reg.value(), dst_offset);
					textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
					                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
				} else {
					// Source is on the stack, load it to a temporary register and store to destination
					allocated_reg_val = allocateRegisterWithSpilling();
					auto load_opcodes = generateMovFromFrame(allocated_reg_val, src_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
					auto store_opcodes = generateMovToFrame(allocated_reg_val, dst_offset);
					textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
					                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
					regAlloc.release(allocated_reg_val);
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
			writer.add_local_variable(var_name_str, type_index, flags, locations);
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
			case X64Register::XMM4: return 158;  // CV_AMD64_XMM4
			case X64Register::XMM5: return 159;  // CV_AMD64_XMM5
			case X64Register::XMM6: return 160;  // CV_AMD64_XMM6
			case X64Register::XMM7: return 161;  // CV_AMD64_XMM7
			case X64Register::XMM8: return 162;  // CV_AMD64_XMM8
			case X64Register::XMM9: return 163;  // CV_AMD64_XMM9
			case X64Register::XMM10: return 164; // CV_AMD64_XMM10
			case X64Register::XMM11: return 165; // CV_AMD64_XMM11
			case X64Register::XMM12: return 166; // CV_AMD64_XMM12
			case X64Register::XMM13: return 167; // CV_AMD64_XMM13
			case X64Register::XMM14: return 168; // CV_AMD64_XMM14
			case X64Register::XMM15: return 169; // CV_AMD64_XMM15
			default: assert(false && "Unsupported X64Register"); return 0;
		}
	}

	void handleFunctionDecl(const IrInstruction& instruction) {
		// Verify we have at least the minimum operands using FunctionDeclLayout constants
		assert(instruction.getOperandCount() >= FunctionDeclLayout::FIRST_PARAM_INDEX &&
		       "FunctionDecl must have at least the fixed operands");

		// Verify the operand count is valid (fixed operands + N complete parameters)
		assert(FunctionDeclLayout::isValidOperandCount(instruction.getOperandCount()) &&
		       "FunctionDecl operand count is invalid - parameter operands don't align!");

		// Extract function name using FunctionDeclLayout constant
		std::string func_name_str;
		if (instruction.isOperandType<std::string>(FunctionDeclLayout::FUNCTION_NAME)) {
			func_name_str = instruction.getOperandAs<std::string>(FunctionDeclLayout::FUNCTION_NAME);
		} else if (instruction.isOperandType<std::string_view>(FunctionDeclLayout::FUNCTION_NAME)) {
			func_name_str = std::string(instruction.getOperandAs<std::string_view>(FunctionDeclLayout::FUNCTION_NAME));
		}
		std::string_view func_name = func_name_str;

		// Extract struct name using FunctionDeclLayout constant
		std::string struct_name_str;
		if (instruction.isOperandType<std::string>(FunctionDeclLayout::STRUCT_NAME)) {
			struct_name_str = instruction.getOperandAs<std::string>(FunctionDeclLayout::STRUCT_NAME);
		} else if (instruction.isOperandType<std::string_view>(FunctionDeclLayout::STRUCT_NAME)) {
			struct_name_str = std::string(instruction.getOperandAs<std::string_view>(FunctionDeclLayout::STRUCT_NAME));
		}
		std::string_view struct_name = struct_name_str;
		std::cerr << "DEBUG [handleFunctionDecl]: func_name='" << func_name << "' struct_name='" << struct_name << "' empty=" << struct_name.empty() << std::endl;

		// Extract return type information using FunctionDeclLayout constants
		auto return_base_type = instruction.getOperandAs<Type>(FunctionDeclLayout::RETURN_TYPE);
		auto return_size = instruction.getOperandAs<int>(FunctionDeclLayout::RETURN_SIZE);
		auto return_pointer_depth = instruction.getOperandAs<int>(FunctionDeclLayout::RETURN_POINTER_DEPTH);

		// Construct return type TypeSpecifierNode
		TypeSpecifierNode return_type(return_base_type, TypeQualifier::None, static_cast<unsigned char>(return_size));
		for (int i = 0; i < return_pointer_depth; ++i) {
			return_type.add_pointer_level();
		}

		std::vector<TypeSpecifierNode> parameter_types;

		// Extract linkage and variadic flag using FunctionDeclLayout constants
		Linkage linkage = static_cast<Linkage>(instruction.getOperandAs<int>(FunctionDeclLayout::LINKAGE));
		bool is_variadic = instruction.getOperandAs<bool>(FunctionDeclLayout::IS_VARIADIC);
		
		// Extract pre-computed mangled name (includes full CV-qualifier info)
		std::string_view mangled_name = instruction.getOperandAs<std::string_view>(FunctionDeclLayout::MANGLED_NAME);

		// Extract parameter types from the instruction using FunctionDeclLayout constants
		size_t paramIndex = FunctionDeclLayout::FIRST_PARAM_INDEX;
		while (paramIndex + FunctionDeclLayout::OPERANDS_PER_PARAM <= instruction.getOperandCount()) {
			auto param_base_type = instruction.getOperandAs<Type>(paramIndex + FunctionDeclLayout::PARAM_TYPE);
			auto param_size = instruction.getOperandAs<int>(paramIndex + FunctionDeclLayout::PARAM_SIZE);
			auto param_pointer_depth = instruction.getOperandAs<int>(paramIndex + FunctionDeclLayout::PARAM_POINTER_DEPTH);
			// paramIndex + PARAM_NAME is the parameter name (not needed for signature)
			bool is_reference = instruction.getOperandAs<bool>(paramIndex + FunctionDeclLayout::PARAM_IS_REFERENCE);
			bool is_rvalue_reference = instruction.getOperandAs<bool>(paramIndex + FunctionDeclLayout::PARAM_IS_RVALUE_REFERENCE);
			CVQualifier cv_qual = static_cast<CVQualifier>(instruction.getOperandAs<int>(paramIndex + FunctionDeclLayout::PARAM_CV_QUALIFIER));

			// Construct parameter TypeSpecifierNode with CV-qualifier
			TypeSpecifierNode param_type(param_base_type, TypeQualifier::None, static_cast<unsigned char>(param_size), Token{}, cv_qual);

			// Set reference information
			if (is_reference) {
				param_type.set_reference(is_rvalue_reference);
				std::cerr << "DEBUG: Parameter " << paramIndex/FunctionDeclLayout::OPERANDS_PER_PARAM 
						  << " is_ref=" << is_reference << " is_rvalue=" << is_rvalue_reference 
						  << " ptr_depth=" << param_pointer_depth << "\n";
			
				// For references, ptr_depth includes +1 for the reference itself (ABI)
				// Subtract 1 to get the actual pointer levels (e.g., int*& has ptr_depth=2, but only 1 PE)
				for (int i = 1; i < param_pointer_depth; ++i) {
					param_type.add_pointer_level();
				}
			} else {
				// Non-reference: add all pointer levels
				for (int i = 0; i < param_pointer_depth; ++i) {
					param_type.add_pointer_level();
				}
			}
		
			parameter_types.push_back(param_type);
			paramIndex += FunctionDeclLayout::OPERANDS_PER_PARAM;
		}

		// Add function signature to the object file writer (still needed for debug info)
		// but use the pre-computed mangled name instead of regenerating it
		if (!struct_name.empty()) {
			// Member function - include struct name
			writer.addFunctionSignature(std::string(func_name), return_type, parameter_types, std::string(struct_name), linkage, is_variadic, mangled_name);
		} else {
			// Regular function
			writer.addFunctionSignature(std::string(func_name), return_type, parameter_types, linkage, is_variadic, mangled_name);
		}
		
		// Finalize previous function before starting new one
		if (!current_function_name_.empty()) {
			uint32_t function_length = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;

			// Update function length (uses unmangled name for debug info)
			writer.update_function_length(current_function_name_, function_length);
			writer.set_function_debug_range(current_function_name_, 0, 0);	// doesn't seem needed

			// Add exception handling information (required for x64) - uses mangled name
			writer.add_function_exception_info(current_function_mangled_name_, current_function_offset_, function_length);
		
			// Clean up the previous function's variable scope
			// This happens when we start a NEW function, ensuring the previous function's scope is removed
			if (!variable_scopes.empty()) {
				variable_scopes.pop_back();
			}
		}

		// align the function to 16 bytes
		static constexpr char nop = static_cast<char>(0x90);
		const uint32_t nop_count = 16 - (textSectionData.size() % 16);
		if (nop_count < 16)
			textSectionData.insert(textSectionData.end(), nop_count, nop);

		// Function debug info is now added in add_function_symbol() with length 0
		StackVariableScope& var_scope = variable_scopes.emplace_back();
		const auto func_stack_space = calculateFunctionStackSpace(func_name_str, var_scope);
		uint32_t total_stack_space = func_stack_space.named_vars_size + func_stack_space.shadow_stack_space + func_stack_space.temp_vars_size;
		
		// Windows x64 calling convention: Functions must provide home space for parameters
		// Even if parameters stay in registers, we need space to spill them if needed
		// Member functions have implicit 'this' pointer as first parameter
		size_t param_count = parameter_types.size();
		if (!struct_name.empty()) {
			param_count++;  // Count 'this' pointer for member functions
		}
		if (param_count > 0 && total_stack_space < param_count * 8) {
			total_stack_space = static_cast<uint32_t>(param_count * 8);
		}
		
		// Ensure stack alignment to 16 bytes
		total_stack_space = (total_stack_space + 15) & -16;
		
		// DEBUG: Check if we're skipping the prologue
		if (total_stack_space == 0 && func_name_str.find("insert") != std::string::npos) {
			std::cerr << "WARNING: Function " << func_name_str << " has total_stack_space=0!\n";
			std::cerr << "  named_vars_size=" << func_stack_space.named_vars_size << "\n";
			std::cerr << "  shadow_stack_space=" << func_stack_space.shadow_stack_space << "\n";
			std::cerr << "  temp_vars_size=" << func_stack_space.temp_vars_size << "\n";
		}

		uint32_t func_offset = static_cast<uint32_t>(textSectionData.size());
		writer.add_function_symbol(mangled_name, func_offset, total_stack_space, linkage);
		functionSymbols[func_name] = func_offset;

		// Track function for debug information
		current_function_name_ = func_name;  // Direct assignment, no temporary
		current_function_mangled_name_ = mangled_name;
		current_function_offset_ = func_offset;

		// Patch pending branches from previous function before clearing
		if (!pending_branches_.empty()) {
			patchBranches();
		}

		// Clear control flow tracking for new function
		label_positions_.clear();
		pending_branches_.clear();

		// Set up debug information for this function
		// For now, use file ID 0 (first source file)
		writer.set_current_function_for_debug(func_name_str, 0);

		// Add line mapping for function declaration (now that current function is set)
		if (instruction.getLineNumber() > 0) {
			// Also add line mapping for function opening brace (next line)
			addLineMapping(instruction.getLineNumber() + 1);
		}

		// Create a new function scope
		regAlloc.reset();

		// MSVC-style prologue: push rbp; mov rbp, rsp; sub rsp, total_stack_space
		// Always generate prologue - even if total_stack_space is 0, we need RBP for parameter access
		textSectionData.push_back(0x55); // push rbp
		textSectionData.push_back(0x48); textSectionData.push_back(0x8B); textSectionData.push_back(0xEC); // mov rbp, rsp

		if (total_stack_space > 0) {
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
		if (variable_scopes.empty()) {
			std::cerr << "FATAL: variable_scopes is EMPTY!\n"; std::cerr.flush();
			std::abort();
		}
		variable_scopes.back().scope_stack_space = -total_stack_space;

		// Handle parameters
		struct ParameterInfo {
			Type param_type;
			int param_size;
			std::string_view param_name;
			int paramNumber;
			int offset;
			X64Register src_reg;
			int pointer_depth;
		};
		std::vector<ParameterInfo> parameters;

		// For member functions, add implicit 'this' pointer as first parameter
		int param_offset_adjustment = 0;
		if (!struct_name.empty()) {
			// 'this' is passed in RCX (first parameter register)
			int this_offset = -8;  // First parameter slot
			variable_scopes.back().identifier_offset["this"] = this_offset;

			// Add 'this' parameter to debug information
			writer.add_function_parameter("this", 0x603, this_offset);  // 0x603 = T_64PVOID (pointer type)

			// Store 'this' parameter info
			parameters.push_back({Type::Struct, 64, "this", 0, this_offset, INT_PARAM_REGS[0]});
			regAlloc.allocateSpecific(INT_PARAM_REGS[0], this_offset);

			param_offset_adjustment = 1;  // Shift other parameters by 1
		}

		// First pass: collect all parameter information using FunctionDeclLayout constants
		paramIndex = FunctionDeclLayout::FIRST_PARAM_INDEX;
		// Clear reference parameter tracking from previous function
		reference_stack_info_.clear();
		while (paramIndex + FunctionDeclLayout::OPERANDS_PER_PARAM <= instruction.getOperandCount()) {
			auto param_type = instruction.getOperandAs<Type>(paramIndex + FunctionDeclLayout::PARAM_TYPE);
			auto param_size = instruction.getOperandAs<int>(paramIndex + FunctionDeclLayout::PARAM_SIZE);
			auto param_pointer_depth = instruction.getOperandAs<int>(paramIndex + FunctionDeclLayout::PARAM_POINTER_DEPTH);

			// Calculate parameter number using FunctionDeclLayout helper
			size_t param_index_in_list = (paramIndex - FunctionDeclLayout::FIRST_PARAM_INDEX) / FunctionDeclLayout::OPERANDS_PER_PARAM;
			int paramNumber = static_cast<int>(param_index_in_list) + param_offset_adjustment;
			int offset = paramNumber < 4 ? (paramNumber + 1) * -8 : 16 + (paramNumber - 4) * 8;

			auto param_name = instruction.getOperandAs<std::string_view>(paramIndex + FunctionDeclLayout::PARAM_NAME);
			variable_scopes.back().identifier_offset[param_name] = offset;

			// Track reference parameters by their stack offset (they need pointer dereferencing like 'this')
			bool is_reference = instruction.getOperandAs<bool>(paramIndex + FunctionDeclLayout::PARAM_IS_REFERENCE);
			if (is_reference) {
				reference_stack_info_[offset] = ReferenceInfo{
					.value_type = param_type,
					.value_size_bits = param_size,
					.is_rvalue_reference = instruction.getOperandAs<bool>(paramIndex + FunctionDeclLayout::PARAM_IS_RVALUE_REFERENCE)
				};
			}

			// Add parameter to debug information
			uint32_t param_type_index = 0x74; // T_INT4 for int parameters
			if (param_pointer_depth > 0) {
				param_type_index = 0x603;  // T_64PVOID for pointer types
			} else {
				switch (param_type) {
					case Type::Int: param_type_index = 0x74; break;  // T_INT4
					case Type::Float: param_type_index = 0x40; break; // T_REAL32
					case Type::Double: param_type_index = 0x41; break; // T_REAL64
					case Type::Char: param_type_index = 0x10; break;  // T_CHAR
					case Type::Bool: param_type_index = 0x30; break;  // T_BOOL08
					case Type::Struct: param_type_index = 0x603; break;  // T_64PVOID for struct pointers
					default: param_type_index = 0x74; break;
				}
			}
			writer.add_function_parameter(std::string(param_name), param_type_index, offset);

			if (paramNumber < static_cast<int>(INT_PARAM_REGS.size())) {
				// Determine if this is a float parameter
				bool is_float_param = (param_type == Type::Float || param_type == Type::Double) && param_pointer_depth == 0;
				X64Register src_reg = is_float_param ? FLOAT_PARAM_REGS[paramNumber] : INT_PARAM_REGS[paramNumber];

				// Don't allocate XMM registers in the general register allocator
				if (!is_float_param) {
					regAlloc.allocateSpecific(src_reg, offset);
				}

				// Store parameter info for later processing
				parameters.push_back({param_type, param_size, param_name, paramNumber, offset, src_reg, param_pointer_depth});
			}

			paramIndex += FunctionDeclLayout::OPERANDS_PER_PARAM;
		}

		// Second pass: generate parameter storage code in the correct order

		// Standard order for other functions
		for (const auto& param : parameters) {
			// MSVC-STYLE: Store parameters using RBP-relative addressing
			bool is_float_param = (param.param_type == Type::Float || param.param_type == Type::Double) && param.pointer_depth == 0;

			if (is_float_param) {
				// For floating-point parameters, use movss/movsd to store from XMM register
				uint8_t prefix = (param.param_type == Type::Float) ? 0xF3 : 0xF2;
				textSectionData.push_back(prefix);
				textSectionData.push_back(0x0F);
				textSectionData.push_back(0x11); // Store variant (movss/movsd [mem], xmm)

				// ModR/M byte for [RBP + offset]
				int32_t stack_offset = param.offset;
				uint8_t xmm_reg = static_cast<uint8_t>(param.src_reg) - static_cast<uint8_t>(X64Register::XMM0);

				if (stack_offset >= -128 && stack_offset <= 127) {
					uint8_t modrm = 0x45 + (xmm_reg << 3); // Mod=01, Reg=XMM, R/M=101 (RBP)
					textSectionData.push_back(modrm);
					textSectionData.push_back(static_cast<uint8_t>(stack_offset));
				} else {
					uint8_t modrm = 0x85 + (xmm_reg << 3); // Mod=10, Reg=XMM, R/M=101 (RBP)
					textSectionData.push_back(modrm);
					for (int j = 0; j < 4; ++j) {
						textSectionData.push_back(static_cast<uint8_t>(stack_offset & 0xFF));
						stack_offset >>= 8;
					}
				}
			} else {
				// For integer parameters, use size-appropriate MOV
				// Use 32-bit store for 32-bit types (int, char, bool), 64-bit for pointers/64-bit types
				bool use_32bit_store = (param.param_size <= 32) && (param.pointer_depth == 0);
				
				if (use_32bit_store) {
					auto mov_opcodes = generateMovToFrame32(param.src_reg, param.offset);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				} else {
					auto mov_opcodes = generateMovToFrame(param.src_reg, param.offset);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				}
				
				// Release the parameter register from the register allocator
				// Parameters are now on the stack, so the register allocator should not
				// think they're still in registers
				regAlloc.release(param.src_reg);
			}
		}
	}

	void handleReturn(const IrInstruction& instruction) {
		if (variable_scopes.empty()) {
			std::cerr << "FATAL [handleReturn]: variable_scopes is EMPTY!\n"; std::cerr.flush();
			std::abort();
		}
		
		// Add line mapping for the return statement itself (only for functions without function calls)
		// For functions with function calls (like main), the closing brace is already mapped in handleFunctionCall
		if (instruction.getLineNumber() > 0 && current_function_name_ != "main") {	// TODO: Is main special case still needed here?
			addLineMapping(instruction.getLineNumber());
		}

		if (instruction.getOperandCount() >= 3) {
			if (instruction.isOperandType<int>(2)) {
				int returnValue = instruction.getOperandAs<int>(2);
				// mov eax, immediate instruction has a fixed size of 5 bytes
				std::array<uint8_t, 5> movEaxImmedInst = { 0xB8, 0, 0, 0, 0 };

				// Fill in the return value
				for (size_t i = 0; i < 4; ++i) {
					movEaxImmedInst[i + 1] = (returnValue >> (8 * i)) & 0xFF;
				}

				textSectionData.insert(textSectionData.end(), movEaxImmedInst.begin(), movEaxImmedInst.end());
			}
			else if (instruction.isOperandType<unsigned long long>(2)) {
				unsigned long long returnValue = instruction.getOperandAs<unsigned long long>(2);
				
				// Check if this is actually a negative number stored as unsigned long long
				// (happens when unary minus is applied to a literal)
				// Values like 0xFFFFFFFFFFFFFFFF (-1) should be treated as signed
				if (returnValue > std::numeric_limits<uint32_t>::max()) {
					// Could be a negative 32-bit value stored as 64-bit unsigned
					// Check if the lower 32 bits represent a valid signed value
					uint32_t lower32 = static_cast<uint32_t>(returnValue);
					// If upper 32 bits are all 1s (sign extension of negative number),
					// treat it as a signed 32-bit value
					if ((returnValue >> 32) == 0xFFFFFFFF) {
						// This is a negative number, use the lower 32 bits
						returnValue = lower32;
					} else {
						throw std::runtime_error("Return value exceeds 32-bit limit");
					}
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
							auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, reg_var.value(), size_it_bits / 8);
							textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
						}
					}
					else {
						// Load from stack using RBP-relative addressing
						// Use 32-bit load for 32-bit types, 64-bit for larger types
						OpCodeWithSize load_opcodes;
						if (size_it_bits <= 32) {
							load_opcodes = generateMovFromFrame32(X64Register::RAX, var_offset);
						} else {
							load_opcodes = generateMovFromFrame(X64Register::RAX, var_offset);
						}
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
							auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, stackRegister.value(), size_in_bits / 8);
							textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
						}
					}
					else {
						assert(variable_scopes.back().scope_stack_space <= var_offset);
						// Load from stack using RBP-relative addressing
						// Use 32-bit load for 32-bit types, 64-bit for larger types
						OpCodeWithSize load_opcodes;
						if (size_in_bits <= 32) {
							load_opcodes = generateMovFromFrame32(X64Register::RAX, var_offset);
						} else {
							load_opcodes = generateMovFromFrame(X64Register::RAX, var_offset);
						}
						textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
						regAlloc.flushSingleDirtyRegister(X64Register::RAX);
					}
				}
			}
		}

		// MSVC-style epilogue
		//int32_t total_stack_space = variable_scopes.back().scope_stack_space;

		// Always generate epilogue since we always generate prologue
		// mov rsp, rbp (restore stack pointer)
		textSectionData.push_back(0x48);
		textSectionData.push_back(0x89);
		textSectionData.push_back(0xEC);

		// pop rbp (restore caller's base pointer)
		textSectionData.push_back(0x5D);

		// ret (return to caller)
		textSectionData.push_back(0xC3);

		// NOTE: We do NOT pop variable_scopes here because there may be multiple
		// return statements in a function (e.g., early returns in if statements).
		// The scope will be popped when we finish processing the entire function.
	}

	void handleStackAlloc(const IrInstruction& instruction) {
		// StackAlloc is not used in the current implementation
		// Variables are allocated in handleVariableDecl instead
		// Just return without doing anything
		return;

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

		// Perform the addition operation: ADD r/m64, r64
		auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg);
		std::array<uint8_t, 3> addInst = { encoding.rex_prefix, 0x01, encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), addInst.begin(), addInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);

		// Release the RHS register (we're done with it)
		regAlloc.release(ctx.rhs_physical_reg);
		// Note: Do NOT release result_physical_reg here - it may be holding a temp variable
		// that will be used by subsequent operations
	}

	void handleSubtract(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "subtraction");

		// Perform the subtraction operation: SUB r/m64, r64
		auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg);
		std::array<uint8_t, 3> subInst = { encoding.rex_prefix, 0x29, encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), subInst.begin(), subInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);

		// Release the RHS register (we're done with it)
		regAlloc.release(ctx.rhs_physical_reg);
		// Note: Do NOT release result_physical_reg here - it may be holding a temp variable
	}

	void handleMultiply(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "multiplication");

		// Perform the multiplication operation: IMUL r64, r/m64
		auto encoding = encodeRegToRegInstruction(ctx.result_physical_reg, ctx.rhs_physical_reg);
		std::array<uint8_t, 4> mulInst = { encoding.rex_prefix, 0x0F, 0xAF, encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), mulInst.begin(), mulInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);

		// Release the RHS register (we're done with it)
		regAlloc.release(ctx.rhs_physical_reg);
		// Note: Do NOT release result_physical_reg here - it may be holding a temp variable
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
		auto encoding = encodeOpcodeExtInstruction(X64OpcodeExtension::SHL, ctx.result_physical_reg);
		std::array<uint8_t, 3> shlInst = { encoding.rex_prefix, 0xD3, encoding.modrm_byte };
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
		auto encoding = encodeOpcodeExtInstruction(X64OpcodeExtension::SAR, ctx.result_physical_reg);
		std::array<uint8_t, 3> sarInst = { encoding.rex_prefix, 0xD3, encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), sarInst.begin(), sarInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleUnsignedDivide(const IrInstruction& instruction) {
		flushAllDirtyRegisters();	// we do this so that RDX is free to use

		regAlloc.release(X64Register::RAX);
		regAlloc.allocateSpecific(X64Register::RAX, INT_MIN);

		regAlloc.release(X64Register::RDX);
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
		auto encoding = encodeOpcodeExtInstruction(X64OpcodeExtension::DIV, ctx.rhs_physical_reg);
		std::array<uint8_t, 3> divInst = { encoding.rex_prefix, 0xF7, encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), divInst.begin(), divInst.end());

		// Store the result from RAX (quotient) to the appropriate destination
		storeArithmeticResult(ctx, X64Register::RAX);

		regAlloc.release(X64Register::RDX);
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
		auto encoding = encodeOpcodeExtInstruction(X64OpcodeExtension::SHR, ctx.result_physical_reg);
		std::array<uint8_t, 3> shrInst = { encoding.rex_prefix, 0xD3, encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), shrInst.begin(), shrInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseAnd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise AND");

		// Perform the bitwise AND operation: AND r/m64, r64
		auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg);
		std::array<uint8_t, 3> andInst = { encoding.rex_prefix, 0x21, encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), andInst.begin(), andInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseOr(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise OR");

		// Perform the bitwise OR operation: OR r/m64, r64
		auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg);
		std::array<uint8_t, 3> orInst = { encoding.rex_prefix, 0x09, encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), orInst.begin(), orInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseXor(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise XOR");

		// Perform the bitwise XOR operation: XOR r/m64, r64
		auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg);
		std::array<uint8_t, 3> xorInst = { encoding.rex_prefix, 0x31, encoding.modrm_byte };
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
		auto encoding = encodeOpcodeExtInstruction(X64OpcodeExtension::IDIV, ctx.rhs_physical_reg);
		std::array<uint8_t, 3> idivInst = { encoding.rex_prefix, 0xF7, encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), idivInst.begin(), idivInst.end());

		// Move remainder from RDX to result register
		if (ctx.result_physical_reg != X64Register::RDX) {
			auto mov_encoding = encodeRegToRegInstruction(X64Register::RDX, ctx.result_physical_reg);
			std::array<uint8_t, 3> movInst = { mov_encoding.rex_prefix, 0x89, mov_encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "equal comparison");
		emitComparisonInstruction(ctx, 0x94); // SETE
	}

	void handleNotEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "not equal comparison");
		emitComparisonInstruction(ctx, 0x95); // SETNE
	}

	void handleLessThan(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "less than comparison");
		emitComparisonInstruction(ctx, 0x9C); // SETL
	}

	void handleLessEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "less than or equal comparison");
		emitComparisonInstruction(ctx, 0x9E); // SETLE
	}

	void handleGreaterThan(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "greater than comparison");
		emitComparisonInstruction(ctx, 0x9F); // SETG
	}

	void handleGreaterEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "greater than or equal comparison");
		emitComparisonInstruction(ctx, 0x9D); // SETGE
	}

	void handleUnsignedLessThan(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned less than comparison");
		emitComparisonInstruction(ctx, 0x92); // SETB
	}

	void handleUnsignedLessEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned less than or equal comparison");
		emitComparisonInstruction(ctx, 0x96); // SETBE
	}

	void handleUnsignedGreaterThan(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned greater than comparison");
		emitComparisonInstruction(ctx, 0x97); // SETA
	}

	void handleUnsignedGreaterEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned greater than or equal comparison");
		emitComparisonInstruction(ctx, 0x93); // SETAE
	}

	void handleLogicalAnd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "logical AND");

		// For logical AND, we need to implement short-circuit evaluation
		// For now, implement as bitwise AND on boolean values
		auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg);
		std::array<uint8_t, 3> andInst = { encoding.rex_prefix, 0x21, encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), andInst.begin(), andInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLogicalOr(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "logical OR");

		// For logical OR, we need to implement short-circuit evaluation
		// For now, implement as bitwise OR on boolean values
		auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg);
		std::array<uint8_t, 3> orInst = { encoding.rex_prefix, 0x09, encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), orInst.begin(), orInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLogicalNot(const IrInstruction& instruction) {
		handleUnaryOperation(instruction, UnaryOperation::LogicalNot);
	}

	void handleBitwiseNot(const IrInstruction& instruction) {
		handleUnaryOperation(instruction, UnaryOperation::BitwiseNot);
	}

	void handleNegate(const IrInstruction& instruction) {
		handleUnaryOperation(instruction, UnaryOperation::Negate);
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
		// XMM registers are encoded as 0-3 in ModR/M byte, but enum values are 16-19
		uint8_t lhs_xmm = static_cast<uint8_t>(ctx.result_physical_reg) - static_cast<uint8_t>(X64Register::XMM0);
		uint8_t rhs_xmm = static_cast<uint8_t>(ctx.rhs_physical_reg) - static_cast<uint8_t>(X64Register::XMM0);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (lhs_xmm << 3) + rhs_xmm;
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (lhs_xmm << 3) + rhs_xmm;
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on zero flag: sete r8 (use AL for RAX)
		std::array<uint8_t, 3> seteInst = { 0x0F, 0x94, 0xC0 };
		seteInst[2] = 0xC0 + static_cast<uint8_t>(bool_reg);
		textSectionData.insert(textSectionData.end(), seteInst.begin(), seteInst.end());

		// Update context for boolean result (1 byte)
		ctx.type = Type::Bool;
		ctx.size_in_bits = 8;

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx, bool_reg);
	}

	void handleFloatNotEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point not equal comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		// XMM registers are encoded as 0-3 in ModR/M byte, but enum values are 16-19
		uint8_t lhs_xmm = static_cast<uint8_t>(ctx.result_physical_reg) - static_cast<uint8_t>(X64Register::XMM0);
		uint8_t rhs_xmm = static_cast<uint8_t>(ctx.rhs_physical_reg) - static_cast<uint8_t>(X64Register::XMM0);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (lhs_xmm << 3) + rhs_xmm;
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (lhs_xmm << 3) + rhs_xmm;
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on zero flag: setne r8
		std::array<uint8_t, 3> setneInst = { 0x0F, 0x95, 0xC0 };
		setneInst[2] = 0xC0 + static_cast<uint8_t>(bool_reg);
		textSectionData.insert(textSectionData.end(), setneInst.begin(), setneInst.end());

		// Update context for boolean result (1 byte)
		ctx.type = Type::Bool;
		ctx.size_in_bits = 8;

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx, bool_reg);
	}

	void handleFloatLessThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point less than comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		// XMM registers are encoded as 0-3 in ModR/M byte, but enum values are 16-19
		uint8_t lhs_xmm = static_cast<uint8_t>(ctx.result_physical_reg) - static_cast<uint8_t>(X64Register::XMM0);
		uint8_t rhs_xmm = static_cast<uint8_t>(ctx.rhs_physical_reg) - static_cast<uint8_t>(X64Register::XMM0);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (lhs_xmm << 3) + rhs_xmm;
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (lhs_xmm << 3) + rhs_xmm;
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on carry flag: setb r8 (below = less than for floating-point)
		std::array<uint8_t, 3> setbInst = { 0x0F, 0x92, 0xC0 };
		setbInst[2] = 0xC0 + static_cast<uint8_t>(bool_reg);
		textSectionData.insert(textSectionData.end(), setbInst.begin(), setbInst.end());

		// Update context for boolean result (1 byte)
		ctx.type = Type::Bool;
		ctx.size_in_bits = 8;

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx, bool_reg);
	}

	void handleFloatLessEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point less than or equal comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		// XMM registers are encoded as 0-3 in ModR/M byte, but enum values are 16-19
		uint8_t lhs_xmm = static_cast<uint8_t>(ctx.result_physical_reg) - static_cast<uint8_t>(X64Register::XMM0);
		uint8_t rhs_xmm = static_cast<uint8_t>(ctx.rhs_physical_reg) - static_cast<uint8_t>(X64Register::XMM0);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (lhs_xmm << 3) + rhs_xmm;
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (lhs_xmm << 3) + rhs_xmm;
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on flags: setbe r8 (below or equal)
		std::array<uint8_t, 3> setbeInst = { 0x0F, 0x96, 0xC0 };
		setbeInst[2] = 0xC0 + static_cast<uint8_t>(bool_reg);
		textSectionData.insert(textSectionData.end(), setbeInst.begin(), setbeInst.end());

		// Update context for boolean result (1 byte)
		ctx.type = Type::Bool;
		ctx.size_in_bits = 8;

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx, bool_reg);
	}

	void handleFloatGreaterThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point greater than comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		// XMM registers are encoded as 0-3 in ModR/M byte, but enum values are 16-19
		uint8_t lhs_xmm = static_cast<uint8_t>(ctx.result_physical_reg) - static_cast<uint8_t>(X64Register::XMM0);
		uint8_t rhs_xmm = static_cast<uint8_t>(ctx.rhs_physical_reg) - static_cast<uint8_t>(X64Register::XMM0);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (lhs_xmm << 3) + rhs_xmm;
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (lhs_xmm << 3) + rhs_xmm;
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on flags: seta r8 (above = greater than for floating-point)
		std::array<uint8_t, 3> setaInst = { 0x0F, 0x97, 0xC0 };
		setaInst[2] = 0xC0 + static_cast<uint8_t>(bool_reg);
		textSectionData.insert(textSectionData.end(), setaInst.begin(), setaInst.end());

		// Update context for boolean result (1 byte)
		ctx.type = Type::Bool;
		ctx.size_in_bits = 8;

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx, bool_reg);
	}

	void handleFloatGreaterEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point greater than or equal comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		// XMM registers are encoded as 0-3 in ModR/M byte, but enum values are 16-19
		uint8_t lhs_xmm = static_cast<uint8_t>(ctx.result_physical_reg) - static_cast<uint8_t>(X64Register::XMM0);
		uint8_t rhs_xmm = static_cast<uint8_t>(ctx.rhs_physical_reg) - static_cast<uint8_t>(X64Register::XMM0);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (lhs_xmm << 3) + rhs_xmm;
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (lhs_xmm << 3) + rhs_xmm;
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on flags: setae r8 (above or equal)
		std::array<uint8_t, 3> setaeInst = { 0x0F, 0x93, 0xC0 };
		setaeInst[2] = 0xC0 + static_cast<uint8_t>(bool_reg);
		textSectionData.insert(textSectionData.end(), setaeInst.begin(), setaeInst.end());

		// Update context for boolean result (1 byte)
		ctx.type = Type::Bool;
		ctx.size_in_bits = 8;

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx, bool_reg);
	}

	// Helper: Load operand value (TempVar or variable name) into a register
	X64Register loadOperandIntoRegister(const IrInstruction& instruction, size_t operand_index) {
		X64Register reg = X64Register::Count;
		
		if (instruction.isOperandType<TempVar>(operand_index)) {
			auto temp = instruction.getOperandAs<TempVar>(operand_index);
			auto stack_addr = getStackOffsetFromTempVar(temp);
			if (auto ref_it = reference_stack_info_.find(stack_addr); ref_it != reference_stack_info_.end()) {
				reg = allocateRegisterWithSpilling();
				loadValueFromReferenceSlot(stack_addr, ref_it->second, reg);
				return reg;
			}
			if (auto reg_opt = regAlloc.tryGetStackVariableRegister(stack_addr); reg_opt.has_value()) {
				reg = reg_opt.value();
			} else {
				reg = allocateRegisterWithSpilling();
				auto mov_opcodes = generateMovFromFrame(reg, stack_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), 
					mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				regAlloc.flushSingleDirtyRegister(reg);
			}
		} else if (instruction.isOperandType<std::string_view>(operand_index)) {
			auto var_name = instruction.getOperandAs<std::string_view>(operand_index);
			auto var_id = variable_scopes.back().identifier_offset.find(var_name);
			if (var_id != variable_scopes.back().identifier_offset.end()) {
				if (auto ref_it = reference_stack_info_.find(var_id->second); ref_it != reference_stack_info_.end()) {
					reg = allocateRegisterWithSpilling();
					loadValueFromReferenceSlot(var_id->second, ref_it->second, reg);
					return reg;
				}
				if (auto reg_opt = regAlloc.tryGetStackVariableRegister(var_id->second); reg_opt.has_value()) {
					reg = reg_opt.value();
				} else {
					reg = allocateRegisterWithSpilling();
					auto mov_opcodes = generateMovFromFrame(reg, var_id->second);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), 
						mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(reg);
				}
			}
		}
		
		return reg;
	}

	std::optional<int32_t> findIdentifierStackOffset(std::string_view name) const {
		for (auto scope_it = variable_scopes.rbegin(); scope_it != variable_scopes.rend(); ++scope_it) {
			const auto& scope = *scope_it;
			auto found = scope.identifier_offset.find(name);
			if (found != scope.identifier_offset.end()) {
				return found->second;
			}
		}
		return std::nullopt;
	}

	enum class IncDecKind { PreIncrement, PostIncrement, PreDecrement, PostDecrement };

	struct UnaryOperandLocation {
		enum class Kind { Stack, Global };
		Kind kind = Kind::Stack;
		int32_t stack_offset = 0;
		std::string global_name;

		static UnaryOperandLocation stack(int32_t offset) {
			UnaryOperandLocation loc;
			loc.kind = Kind::Stack;
			loc.stack_offset = offset;
			return loc;
		}

		static UnaryOperandLocation global(std::string name) {
			UnaryOperandLocation loc;
			loc.kind = Kind::Global;
			loc.global_name = name;
			return loc;
		}
	};

	UnaryOperandLocation resolveUnaryOperandLocation(const IrInstruction& instruction, size_t operand_index) {
		if (instruction.isOperandType<TempVar>(operand_index)) {
			auto temp = instruction.getOperandAs<TempVar>(operand_index);
			return UnaryOperandLocation::stack(getStackOffsetFromTempVar(temp));
		}

		if (instruction.isOperandType<std::string_view>(operand_index)) {
			auto name = instruction.getOperandAs<std::string_view>(operand_index);
			if (auto offset = findIdentifierStackOffset(name); offset.has_value()) {
				return UnaryOperandLocation::stack(offset.value());
			}
			return UnaryOperandLocation::global(std::string(name));
		}

		if (instruction.isOperandType<std::string>(operand_index)) {
			return UnaryOperandLocation::global(instruction.getOperandAs<std::string>(operand_index));
		}

		assert(false && "Unsupported operand type for unary operation");
		return UnaryOperandLocation::stack(0);
	}

	void appendRipRelativePlaceholder(const std::string& global_name) {
		uint32_t reloc_offset = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		pending_global_relocations_.push_back({reloc_offset, global_name, IMAGE_REL_AMD64_REL32});
	}

	void loadValueFromStack(int32_t offset, int size_in_bits, X64Register target_reg) {
		OpCodeWithSize load_opcodes;
		switch (size_in_bits) {
		case 64:
			load_opcodes = generateMovFromFrame(target_reg, offset);
			break;
		case 32:
			load_opcodes = generateMovFromFrame32(target_reg, offset);
			break;
		case 16:
			load_opcodes = generateMovzxFromFrame16(target_reg, offset);
			break;
		case 8:
			load_opcodes = generateMovzxFromFrame8(target_reg, offset);
			break;
		default:
			assert(false && "Unsupported stack load size");
			return;
		}
		textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
		                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
	}

	void emitStoreWordToFrame(X64Register source_reg, int32_t offset) {
		textSectionData.push_back(0x66); // Operand-size override for 16-bit
		bool needs_rex = static_cast<uint8_t>(source_reg) >= static_cast<uint8_t>(X64Register::R8);
		if (needs_rex) {
			uint8_t rex = 0x40 | (1 << 2); // REX.R
			textSectionData.push_back(rex);
		}
		textSectionData.push_back(0x89);
		uint8_t reg_bits = static_cast<uint8_t>(source_reg) & 0x07;
		uint8_t mod_field = (offset >= -128 && offset <= 127) ? 0x01 : 0x02;
		if (offset == 0) {
			mod_field = 0x01;
		}
		uint8_t modrm = (mod_field << 6) | (reg_bits << 3) | 0x05;
		textSectionData.push_back(modrm);
		if (mod_field == 0x01) {
			textSectionData.push_back(static_cast<uint8_t>(offset));
		} else {
			uint32_t offset_u32 = static_cast<uint32_t>(offset);
			textSectionData.push_back(offset_u32 & 0xFF);
			textSectionData.push_back((offset_u32 >> 8) & 0xFF);
			textSectionData.push_back((offset_u32 >> 16) & 0xFF);
			textSectionData.push_back((offset_u32 >> 24) & 0xFF);
		}
	}

	void emitStoreByteToFrame(X64Register source_reg, int32_t offset) {
		bool needs_rex = static_cast<uint8_t>(source_reg) >= static_cast<uint8_t>(X64Register::R8);
		if (needs_rex) {
			uint8_t rex = 0x40 | (1 << 2); // REX.R
			textSectionData.push_back(rex);
		}
		textSectionData.push_back(0x88);
		uint8_t reg_bits = static_cast<uint8_t>(source_reg) & 0x07;
		uint8_t mod_field = (offset >= -128 && offset <= 127) ? 0x01 : 0x02;
		if (offset == 0) {
			mod_field = 0x01;
		}
		uint8_t modrm = (mod_field << 6) | (reg_bits << 3) | 0x05;
		textSectionData.push_back(modrm);
		if (mod_field == 0x01) {
			textSectionData.push_back(static_cast<uint8_t>(offset));
		} else {
			uint32_t offset_u32 = static_cast<uint32_t>(offset);
			textSectionData.push_back(offset_u32 & 0xFF);
			textSectionData.push_back((offset_u32 >> 8) & 0xFF);
			textSectionData.push_back((offset_u32 >> 16) & 0xFF);
			textSectionData.push_back((offset_u32 >> 24) & 0xFF);
		}
	}

	void storeValueToStack(int32_t offset, int size_in_bits, X64Register source_reg) {
		OpCodeWithSize store_opcodes;
		switch (size_in_bits) {
		case 64:
			store_opcodes = generateMovToFrame(source_reg, offset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
			                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
			break;
		case 32:
			store_opcodes = generateMovToFrame32(source_reg, offset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
			                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
			break;
		case 16:
			emitStoreWordToFrame(source_reg, offset);
			break;
		case 8:
			emitStoreByteToFrame(source_reg, offset);
			break;
		default:
			assert(false && "Unsupported stack store size");
		}
	}

	void loadValueFromGlobal(const std::string& global_name, int size_in_bits, X64Register target_reg) {
		uint8_t reg_bits = static_cast<uint8_t>(target_reg) & 0x07;
		bool needs_rex = static_cast<uint8_t>(target_reg) >= static_cast<uint8_t>(X64Register::R8);
		switch (size_in_bits) {
		case 64: {
			uint8_t rex = 0x48;
			if (needs_rex) {
				rex |= (1 << 2); // REX.R
			}
			textSectionData.push_back(rex);
			textSectionData.push_back(0x8B);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		case 32: {
			if (needs_rex) {
				uint8_t rex = 0x40 | (1 << 2); // REX.R
				textSectionData.push_back(rex);
			}
			textSectionData.push_back(0x8B);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		case 16:
		case 8: {
			if (needs_rex) {
				uint8_t rex = 0x40 | (1 << 2); // REX.R
				textSectionData.push_back(rex);
			}
			textSectionData.push_back(0x0F);
			textSectionData.push_back(size_in_bits == 16 ? 0xB7 : 0xB6);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		default:
			assert(false && "Unsupported global load size");
		}
	}

	void moveImmediateToRegister(X64Register reg, uint64_t value) {
		uint8_t rex = 0x48;
		if (static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex |= 0x01;
		}
		textSectionData.push_back(rex);
		textSectionData.push_back(0xB8 + (static_cast<uint8_t>(reg) & 0x07));
		for (int i = 0; i < 8; ++i) {
			textSectionData.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
		}
	}

	void loadValuePointedByRegister(X64Register reg, int value_size_bits) {
		int element_size_bytes = value_size_bits / 8;
		if (value_size_bits <= 8) {
			element_size_bytes = 1;
		}
		if (element_size_bytes != 1 && element_size_bytes != 2 && element_size_bytes != 4 && element_size_bytes != 8) {
			assert(false && "Unsupported reference load size");
			return;
		}

		bool use_temp_reg = reg != X64Register::RAX;
		if (use_temp_reg) {
			auto mov_to_rax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, reg, 8);
			textSectionData.insert(textSectionData.end(), mov_to_rax.op_codes.begin(),
				               mov_to_rax.op_codes.begin() + mov_to_rax.size_in_bytes);
		}

		emitLoadFromAddressInRAX(textSectionData, element_size_bytes);

		if (use_temp_reg) {
			auto mov_back = regAlloc.get_reg_reg_move_op_code(reg, X64Register::RAX, 8);
			textSectionData.insert(textSectionData.end(), mov_back.op_codes.begin(),
				               mov_back.op_codes.begin() + mov_back.size_in_bytes);
		}
	}

	void loadValueFromReferenceSlot(int32_t offset, const ReferenceInfo& ref_info, X64Register target_reg) {
		auto load_ptr = generateMovFromFrame(target_reg, offset);
		textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(),
			               load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
		loadValuePointedByRegister(target_reg, ref_info.value_size_bits);
	}

	bool loadAddressForOperand(const IrInstruction& instruction, size_t operand_index, X64Register target_reg) {
		if (instruction.isOperandType<std::string_view>(operand_index)) {
			auto name = instruction.getOperandAs<std::string_view>(operand_index);
			auto it = variable_scopes.back().identifier_offset.find(name);
			if (it == variable_scopes.back().identifier_offset.end()) {
				return false;
			}
			auto lea = generateLeaFromFrame(target_reg, it->second);
			textSectionData.insert(textSectionData.end(), lea.op_codes.begin(), lea.op_codes.begin() + lea.size_in_bytes);
			return true;
		}
		if (instruction.isOperandType<std::string>(operand_index)) {
			auto name = instruction.getOperandAs<std::string>(operand_index);
			auto it = variable_scopes.back().identifier_offset.find(name);
			if (it == variable_scopes.back().identifier_offset.end()) {
				return false;
			}
			auto lea = generateLeaFromFrame(target_reg, it->second);
			textSectionData.insert(textSectionData.end(), lea.op_codes.begin(), lea.op_codes.begin() + lea.size_in_bytes);
			return true;
		}
		if (instruction.isOperandType<TempVar>(operand_index)) {
			auto temp = instruction.getOperandAs<TempVar>(operand_index);
			int32_t src_offset = getStackOffsetFromTempVar(temp);
			if (auto ref_it = reference_stack_info_.find(src_offset); ref_it != reference_stack_info_.end()) {
				auto load_ptr = generateMovFromFrame(target_reg, src_offset);
				textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(),
				               load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
				return true;
			}
			auto lea = generateLeaFromFrame(target_reg, src_offset);
			textSectionData.insert(textSectionData.end(), lea.op_codes.begin(), lea.op_codes.begin() + lea.size_in_bytes);
			return true;
		}
		return false;
	}

	void storeValueToGlobal(const std::string& global_name, int size_in_bits, X64Register source_reg) {
		uint8_t reg_bits = static_cast<uint8_t>(source_reg) & 0x07;
		bool needs_rex = static_cast<uint8_t>(source_reg) >= static_cast<uint8_t>(X64Register::R8);
		switch (size_in_bits) {
		case 64: {
			uint8_t rex = 0x48;
			if (needs_rex) {
				rex |= (1 << 2);
			}
			textSectionData.push_back(rex);
			textSectionData.push_back(0x89);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		case 32: {
			if (needs_rex) {
				uint8_t rex = 0x40 | (1 << 2);
				textSectionData.push_back(rex);
			}
			textSectionData.push_back(0x89);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		case 16: {
			textSectionData.push_back(0x66);
			if (needs_rex) {
				uint8_t rex = 0x40 | (1 << 2);
				textSectionData.push_back(rex);
			}
			textSectionData.push_back(0x89);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		case 8: {
			if (needs_rex) {
				uint8_t rex = 0x40 | (1 << 2);
				textSectionData.push_back(rex);
			}
			textSectionData.push_back(0x88);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		default:
			assert(false && "Unsupported global store size");
		}
	}

	void loadUnaryOperandValue(const UnaryOperandLocation& location, int size_in_bits, X64Register target_reg) {
		if (location.kind == UnaryOperandLocation::Kind::Stack) {
			loadValueFromStack(location.stack_offset, size_in_bits, target_reg);
		} else {
			loadValueFromGlobal(location.global_name, size_in_bits, target_reg);
		}
	}

	void storeUnaryOperandValue(const UnaryOperandLocation& location, int size_in_bits, X64Register source_reg) {
		if (location.kind == UnaryOperandLocation::Kind::Stack) {
			storeValueToStack(location.stack_offset, size_in_bits, source_reg);
		} else {
			storeValueToGlobal(location.global_name, size_in_bits, source_reg);
		}
	}

	void storeIncDecResultValue(const IrInstruction& instruction, X64Register source_reg, int size_in_bits) {
		const IrOperand& result_operand = instruction.getOperand(0);
		if (std::holds_alternative<TempVar>(result_operand)) {
			auto result_var = std::get<TempVar>(result_operand);
			storeValueToStack(getStackOffsetFromTempVar(result_var), size_in_bits, source_reg);
			return;
		}

		if (std::holds_alternative<std::string_view>(result_operand)) {
			auto result_name = std::get<std::string_view>(result_operand);
			if (auto offset = findIdentifierStackOffset(result_name); offset.has_value()) {
				storeValueToStack(offset.value(), size_in_bits, source_reg);
				return;
			}
			storeValueToGlobal(std::string(result_name), size_in_bits, source_reg);
			return;
		}

		if (std::holds_alternative<std::string>(result_operand)) {
			storeValueToGlobal(std::get<std::string>(result_operand), size_in_bits, source_reg);
			return;
		}

		assert(false && "Unsupported result operand for increment/decrement");
	}

	void emitIncDecInstruction(X64Register target_reg, bool is_increment) {
		uint8_t rex = 0x48;
		if (static_cast<uint8_t>(target_reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex |= 0x01; // Extend r/m field for high registers
		}
		textSectionData.push_back(rex);
		textSectionData.push_back(0x83);
		uint8_t opcode_base = is_increment ? 0xC0 : 0xE8;
		textSectionData.push_back(opcode_base + (static_cast<uint8_t>(target_reg) & 0x07));
		textSectionData.push_back(0x01);
	}

	void handleIncDecCommon(const IrInstruction& instruction, IncDecKind kind) {
		assert(instruction.getOperandCount() == 4 && "Increment/decrement requires 4 operands");
		int size_in_bits = instruction.getOperandAs<int>(2);
		UnaryOperandLocation operand_location = resolveUnaryOperandLocation(instruction, 3);
		X64Register target_reg = X64Register::RAX;
		loadUnaryOperandValue(operand_location, size_in_bits, target_reg);

		bool is_post = (kind == IncDecKind::PostIncrement || kind == IncDecKind::PostDecrement);
		bool is_increment = (kind == IncDecKind::PreIncrement || kind == IncDecKind::PostIncrement);

		if (is_post) {
			storeIncDecResultValue(instruction, target_reg, size_in_bits);
		}

		emitIncDecInstruction(target_reg, is_increment);
		storeUnaryOperandValue(operand_location, size_in_bits, target_reg);

		if (!is_post) {
			storeIncDecResultValue(instruction, target_reg, size_in_bits);
		}
	}

	// Helper: Associate result register with result TempVar's stack offset
	void storeConversionResult(const IrInstruction& instruction, X64Register result_reg) {
		auto result_var = instruction.getOperandAs<TempVar>(0);
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		regAlloc.set_stack_variable_offset(result_reg, result_offset);
		// Don't store to memory yet - keep the value in the register for efficiency
	}

	void handlePreIncrement(const IrInstruction& instruction) {
		handleIncDecCommon(instruction, IncDecKind::PreIncrement);
	}

	void handlePostIncrement(const IrInstruction& instruction) {
		handleIncDecCommon(instruction, IncDecKind::PostIncrement);
	}

	void handlePreDecrement(const IrInstruction& instruction) {
		handleIncDecCommon(instruction, IncDecKind::PreDecrement);
	}

	void handlePostDecrement(const IrInstruction& instruction) {
		handleIncDecCommon(instruction, IncDecKind::PostDecrement);
	}

	void handleUnaryOperation(const IrInstruction& instruction, UnaryOperation op) {
		// All unary operations have 4 operands: result_var, operand_type, operand_size, operand_val
		assert(instruction.getOperandCount() == 4 && "Unary operation must have exactly 4 operands");

		Type type = instruction.getOperandAs<Type>(1);
		int size_in_bits = instruction.getOperandAs<int>(2);

		// Load the operand into a register
		X64Register result_physical_reg = loadOperandIntoRegister(instruction, 3);

		// Perform the specific unary operation
		switch (op) {
			case UnaryOperation::LogicalNot: {
				// Compare with 0: cmp reg, 0
				std::array<uint8_t, 4> cmpInst = { 0x48, 0x83, 0xF8, 0x00 }; // cmp r64, imm8
				cmpInst[2] = 0xF8 + static_cast<uint8_t>(result_physical_reg);
				textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

				// Set result to 1 if zero (sete), 0 otherwise
				std::array<uint8_t, 3> seteInst = { 0x0F, 0x94, 0xC0 }; // sete r8
				seteInst[2] = 0xC0 + static_cast<uint8_t>(result_physical_reg);
				textSectionData.insert(textSectionData.end(), seteInst.begin(), seteInst.end());
				break;
			}
			case UnaryOperation::BitwiseNot:
			case UnaryOperation::Negate: {
				// Unified NOT/NEG instruction: REX.W F7 /opcode_ext r64
				uint8_t opcode_ext = static_cast<uint8_t>(op);
				std::array<uint8_t, 3> unaryInst = { 0x48, 0xF7, 0xC0 };
				unaryInst[2] = 0xC0 + (opcode_ext << 3) + static_cast<uint8_t>(result_physical_reg);
				textSectionData.insert(textSectionData.end(), unaryInst.begin(), unaryInst.end());
				break;
			}
		}

		// Store the result
		storeUnaryResult(instruction.getOperand(0), result_physical_reg, size_in_bits);
	}

	void handleSignExtend(const IrInstruction& instruction) {
		// Sign extension: movsx dest, src
		// Operands: result_var(0), from_type(1), from_size(2), value(3), to_size(4)
		int fromSize = instruction.getOperandAs<int>(2);
		int toSize = instruction.getOperandAs<int>(4);

		// Get source value into a register
		X64Register source_reg = loadOperandIntoRegister(instruction, 3);
		
		// Allocate result register
		X64Register result_reg = allocateRegisterWithSpilling();

		// Generate movsx instruction based on size combination
		if (fromSize == 8 && (toSize == 32 || toSize == 64)) {
			// movsx r32/r64, r8: REX 0F BE /r (sign-extend byte to dword/qword)
			uint8_t rex = (toSize == 64) ? 0x48 : 0x40;
			if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04; // REX.R
			if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01; // REX.B
			
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
			std::array<uint8_t, 4> movsx = { rex, 0x0F, 0xBE, modrm };
			textSectionData.insert(textSectionData.end(), movsx.begin(), movsx.end());
		} else if (fromSize == 16 && (toSize == 32 || toSize == 64)) {
			// movsx r32/r64, r16: REX 0F BF /r (sign-extend word to dword/qword)
			uint8_t rex = (toSize == 64) ? 0x48 : 0x40;
			if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04; // REX.R
			if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01; // REX.B
			
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
			std::array<uint8_t, 4> movsx = { rex, 0x0F, 0xBF, modrm };
			textSectionData.insert(textSectionData.end(), movsx.begin(), movsx.end());
		} else if (fromSize == 32 && toSize == 64) {
			// movsxd r64, r32: REX.W 63 /r (sign-extend dword to qword)
			uint8_t rex = 0x48; // REX.W
			if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04; // REX.R
			if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01; // REX.B
			
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
			std::array<uint8_t, 3> movsx = { rex, 0x63, modrm };
			textSectionData.insert(textSectionData.end(), movsx.begin(), movsx.end());
		} else {
			// Fallback or no extension needed: just copy
			auto encoding = encodeRegToRegInstruction(result_reg, source_reg);
			std::array<uint8_t, 3> mov = { encoding.rex_prefix, 0x89, encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
		}

		// Store result - associate register with result temp variable's stack offset
		storeConversionResult(instruction, result_reg);
	}

	void handleZeroExtend(const IrInstruction& instruction) {
		// Zero extension: movzx dest, src
		// Operands: result_var(0), from_type(1), from_size(2), value(3), to_size(4)
		int fromSize = instruction.getOperandAs<int>(2);
		int toSize = instruction.getOperandAs<int>(4);

		// Get source value into a register
		X64Register source_reg = loadOperandIntoRegister(instruction, 3);
		
		// Allocate result register
		X64Register result_reg = allocateRegisterWithSpilling();

		// Generate movzx instruction
		if (fromSize == 8 && toSize == 32) {
			// movzx r32, r8: 0F B6 /r
			auto encoding = encodeRegToRegInstruction(result_reg, source_reg);
			std::array<uint8_t, 4> movzx = { encoding.rex_prefix, 0x0F, 0xB6, encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), movzx.begin(), movzx.end());
		} else if (fromSize == 16 && toSize == 32) {
			// movzx r32, r16: 0F B7 /r
			auto encoding = encodeRegToRegInstruction(result_reg, source_reg);
			std::array<uint8_t, 4> movzx = { encoding.rex_prefix, 0x0F, 0xB7, encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), movzx.begin(), movzx.end());
		} else if (fromSize == 32 && toSize == 64) {
			// mov r32, r32 (implicitly zero-extends to 64 bits on x86-64)
			std::array<uint8_t, 2> mov = { 0x89, 0xC0 };
			mov[1] = 0xC0 + (static_cast<uint8_t>(source_reg) << 3) + static_cast<uint8_t>(result_reg);
			textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
		} else {
			// Fallback: just copy
			auto encoding = encodeRegToRegInstruction(result_reg, source_reg);
			std::array<uint8_t, 3> mov = { encoding.rex_prefix, 0x89, encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
		}

		// Store result
		auto result_operand = instruction.getOperand(0);
		if (instruction.isOperandType<TempVar>(0)) {
			auto temp = instruction.getOperandAs<TempVar>(0);
			auto stack_addr = getStackOffsetFromTempVar(temp);
			regAlloc.set_stack_variable_offset(result_reg, stack_addr);
		} else if (instruction.isOperandType<std::string_view>(0)) {
			auto var_name = instruction.getOperandAs<std::string_view>(0);
			auto var_id = variable_scopes.back().identifier_offset.find(var_name);
			if (var_id != variable_scopes.back().identifier_offset.end()) {
				regAlloc.set_stack_variable_offset(result_reg, var_id->second);
			}
		}
	}

	void handleTruncate(const IrInstruction& instruction) {
		// Truncation: just use the lower bits by moving to a smaller register
		// Operands: result_var(0), from_type(1), from_size(2), value(3), to_size(4)
		int fromSize = instruction.getOperandAs<int>(2);
		int toSize = instruction.getOperandAs<int>(4);

		// Get source value into a register
		X64Register source_reg = loadOperandIntoRegister(instruction, 3);
		
		// Allocate result register
		X64Register result_reg = allocateRegisterWithSpilling();

		// Generate appropriate MOV instruction based on target size
		// On x86-64, moving to a smaller register automatically truncates
		if (toSize == 8) {
			// mov r8, r8 (byte to byte) - just copy the low byte
			// Use movzx to ensure we only get the low byte
			uint8_t rex = 0x40;
			if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04; // REX.R
			if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01; // REX.B
			
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
			std::array<uint8_t, 4> movzx = { rex, 0x0F, 0xB6, modrm };
			textSectionData.insert(textSectionData.end(), movzx.begin(), movzx.end());
		} else if (toSize == 16) {
			// mov r16, r16 (word to word)
			// Use movzx to ensure we only get the low word
			uint8_t rex = 0x40;
			if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04; // REX.R
			if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01; // REX.B
			
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
			std::array<uint8_t, 4> movzx = { rex, 0x0F, 0xB7, modrm };
			textSectionData.insert(textSectionData.end(), movzx.begin(), movzx.end());
		} else if (toSize == 32) {
			// mov r32, r32 (dword to dword) - implicitly zero-extends on x86-64
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
			
			// Check if we need REX prefix
			if (static_cast<uint8_t>(result_reg) >= 8 || static_cast<uint8_t>(source_reg) >= 8) {
				uint8_t rex = 0x40;
				if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04; // REX.R
				if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01; // REX.B
				std::array<uint8_t, 3> mov = { rex, 0x89, modrm };
				textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
			} else {
				std::array<uint8_t, 2> mov = { 0x89, modrm };
				textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
			}
		} else {
			// 64-bit or fallback: just copy the whole register
			auto encoding = encodeRegToRegInstruction(result_reg, source_reg);
			std::array<uint8_t, 3> mov = { encoding.rex_prefix, 0x89, encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
		}

		// Store result - associate register with result temp variable's stack offset
		storeConversionResult(instruction, result_reg);
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
		//int lhs_size_bits = instruction.getOperandAs<int>(2);

		// Special handling for function pointer assignment
		if (lhs_type == Type::FunctionPointer) {
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
				assert(false && "LHS variable not found in function pointer assignment");
				return;
			}

			// Get RHS source (function address)
			const IrOperand& rhs_operand = instruction.getOperand(6);
			X64Register source_reg = X64Register::RAX;

			if (std::holds_alternative<TempVar>(rhs_operand)) {
				TempVar rhs_var = std::get<TempVar>(rhs_operand);
				int32_t rhs_offset = getStackOffsetFromTempVar(rhs_var);

				// Load function address from RHS stack location into RAX
				auto load_opcodes = generateMovFromFrame(source_reg, rhs_offset);
				textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
				                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
			}

			// Store RAX to LHS stack location (8 bytes for function pointer)
			auto store_opcodes = generateMovToFrame(source_reg, lhs_offset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
			                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

			return;
		}

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

		// For non-struct types, we need to copy the value from RHS to LHS
		// Get LHS destination (operand 3)
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
			assert(false && "LHS variable not found in assignment");
			return;
		}

		// Get RHS source (operand 6)
		const IrOperand& rhs_operand = instruction.getOperand(6);
		Type rhs_type = instruction.getOperandAs<Type>(4);
		X64Register source_reg = X64Register::RAX;

		// Load RHS value into a register
		if (std::holds_alternative<std::string_view>(rhs_operand)) {
			std::string_view rhs_var_name = std::get<std::string_view>(rhs_operand);
			auto it = variable_scopes.back().identifier_offset.find(rhs_var_name);
			if (it != variable_scopes.back().identifier_offset.end()) {
				int32_t rhs_offset = it->second;
				
				if (is_floating_point_type(rhs_type)) {
					source_reg = allocateXMMRegisterWithSpilling();
					bool is_float = (rhs_type == Type::Float);
					auto load_opcodes = generateFloatMovFromFrame(source_reg, rhs_offset, is_float);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				} else {
					// Load from RHS stack location into RAX
					auto load_opcodes = generateMovFromFrame(source_reg, rhs_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				}
			}
		} else if (std::holds_alternative<TempVar>(rhs_operand)) {
			TempVar rhs_var = std::get<TempVar>(rhs_operand);
			int32_t rhs_offset = getStackOffsetFromTempVar(rhs_var);

			// Check if the value is already in a register
			if (auto rhs_reg = regAlloc.tryGetStackVariableRegister(rhs_offset); rhs_reg.has_value()) {
				source_reg = rhs_reg.value();
			} else {
				if (is_floating_point_type(rhs_type)) {
					source_reg = allocateXMMRegisterWithSpilling();
					bool is_float = (rhs_type == Type::Float);
					auto load_opcodes = generateFloatMovFromFrame(source_reg, rhs_offset, is_float);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				} else {
					// Load from RHS stack location into RAX
					auto load_opcodes = generateMovFromFrame(source_reg, rhs_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				}
			}
		} else if (std::holds_alternative<unsigned long long>(rhs_operand)) {
			// RHS is an immediate value
			unsigned long long rhs_value = std::get<unsigned long long>(rhs_operand);
			// MOV RAX, imm64
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0xB8); // MOV RAX, imm64
			for (size_t i = 0; i < 8; ++i) {
				textSectionData.push_back(static_cast<uint8_t>(rhs_value & 0xFF));
				rhs_value >>= 8;
			}
		}

		// Store source register to LHS stack location
		if (is_floating_point_type(rhs_type)) {
			bool is_float = (rhs_type == Type::Float);
			auto store_opcodes = generateFloatMovToFrame(source_reg, lhs_offset, is_float);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
			                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
		} else {
			auto store_opcodes = generateMovToFrame(source_reg, lhs_offset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
			                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
		}
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
		
		// Release all register allocations at merge points (labels)
		// Different execution paths may have left different values in registers,
		// so we can't trust that a register still holds a particular variable
		regAlloc.reset();
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
		//auto array_type = instruction.getOperandAs<Type>(1);
		auto element_size_bits = instruction.getOperandAs<int>(2);
		//auto index_type = instruction.getOperandAs<Type>(4);
		//auto index_size_bits = instruction.getOperandAs<int>(5);

		// Get the array base address (from stack or register)
		int64_t array_base_offset = 0;
		bool is_array_pointer = false;  // true if array is a pointer/temp var
		TempVar array_temp_var;

		if (instruction.isOperandType<std::string_view>(3)) {
			// Simple case: array is a variable name
			std::string_view array_name = instruction.getOperandAs<std::string_view>(3);
			array_base_offset = variable_scopes.back().identifier_offset[array_name];
		} else if (instruction.isOperandType<std::string>(3)) {
			// Simple case: array is a variable name (std::string)
			const std::string& array_name = instruction.getOperandAs<std::string>(3);
			array_base_offset = variable_scopes.back().identifier_offset[array_name];
		} else if (instruction.isOperandType<TempVar>(3)) {
			// Complex case: array is the result of a previous expression (pointer)
			array_temp_var = instruction.getOperandAs<TempVar>(3);
			array_base_offset = getStackOffsetFromTempVar(array_temp_var);
			is_array_pointer = true;
		} else {
			// Debug: print what type we actually got
			std::cerr << "ERROR in handleArrayAccess: operand 3 is neither string_view nor std::string nor TempVar\n";
			std::cerr << "Operand count: " << instruction.getOperandCount() << "\n";
			assert(false && "Array must be an identifier, qualified name, or temp var");
		}

		// Get the index value (could be a constant or a temp var)
		int64_t index_offset = getStackOffsetFromTempVar(result_var);

		// Calculate element size in bytes
		int element_size_bytes = element_size_bits / 8;

		// Check if this is a member array access (object.member format)
		// Only applies to string-based arrays, not temp vars
		bool is_member_array = false;
		std::string_view array_name_view;
		std::string_view object_name;
		std::string_view member_name;
		int64_t member_offset = 0;

		if (!is_array_pointer) {
			// Only check for member arrays if we have a string-based array
			if (instruction.isOperandType<std::string_view>(3)) {
				array_name_view = instruction.getOperandAs<std::string_view>(3);
			} else if (instruction.isOperandType<std::string>(3)) {
				array_name_view = instruction.getOperandAs<std::string>(3);
			}
			
			is_member_array = array_name_view.find('.') != std::string::npos;
			if (is_member_array) {
				// Parse object.member
				size_t dot_pos = array_name_view.find('.');
				object_name = array_name_view.substr(0, dot_pos);
				member_name = array_name_view.substr(dot_pos + 1);
				// member_offset will be 0 for now - needs proper struct lookup
			}
		}

		std::string_view lookup_name = is_member_array ? object_name : array_name_view;

		// Load index into a register (RAX)
		if (instruction.isOperandType<unsigned long long>(6)) {
			// Constant index
			uint64_t index_value = instruction.getOperandAs<unsigned long long>(6);
			std::cerr << "DEBUG: Constant index=" << index_value << "\n";

			if (is_array_pointer) {
				// Array is a pointer/temp var - load pointer and compute address
				// Load array pointer into RAX: MOV RAX, [RBP + array_base_offset]
				auto load_ptr_opcodes = generateMovFromFrame(X64Register::RAX, array_base_offset);
				textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
				                       load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);

				// Add offset to pointer: ADD RAX, (index * element_size)
				int64_t offset_bytes = index_value * element_size_bytes;
				if (offset_bytes != 0) {
					// ADD RAX, imm32
					textSectionData.push_back(0x48); // REX.W
					textSectionData.push_back(0x05); // ADD RAX, imm32
					uint32_t offset_u32 = static_cast<uint32_t>(offset_bytes);
					textSectionData.push_back(offset_u32 & 0xFF);
					textSectionData.push_back((offset_u32 >> 8) & 0xFF);
				textSectionData.push_back((offset_u32 >> 16) & 0xFF);
				textSectionData.push_back((offset_u32 >> 24) & 0xFF);
			}

			// Load value from [RAX] with size-appropriate instruction
			emitLoadFromAddressInRAX(textSectionData, element_size_bytes);
		} else {
			// Array is a regular variable - use direct stack offset
			int64_t element_offset = array_base_offset + member_offset + (index_value * element_size_bytes);				// Load from [RBP + offset] into RAX
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
			}
		} else if (instruction.isOperandType<TempVar>(6)) {
			// Variable index - need to compute address at runtime
			TempVar index_var = instruction.getOperandAs<TempVar>(6);
			int64_t index_var_offset = getStackOffsetFromTempVar(index_var);
			std::cerr << "DEBUG: Variable index=" << index_var.name() << " offset=" << index_var_offset << " array_base=" << array_base_offset << "\n";

			if (is_array_pointer) {
				// Array is a pointer/temp var
				// Load array pointer into RAX: MOV RAX, [RBP + array_base_offset]
				auto load_ptr_opcodes = generateMovFromFrame(X64Register::RAX, array_base_offset);
				textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
				                       load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);

				// Load index variable from stack into RCX
				emitLoadIndexIntoRCX(textSectionData, index_var_offset);

				// Multiply index by element size (optimized for power-of-2 sizes)
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);

				// Add scaled index to array pointer: ADD RAX, RCX
				emitAddRAXRCX(textSectionData);

				// Load value from [RAX] with size-appropriate instruction
				emitLoadFromAddressInRAX(textSectionData, element_size_bytes);
			} else {
				// Array is a regular variable - use direct stack offset calculation
				// Load index variable from stack into RCX
				emitLoadIndexIntoRCX(textSectionData, index_var_offset);

				// Multiply index by element size (optimized for power-of-2 sizes)
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);

				// Load array base address: LEA RAX, [RBP + combined_offset]
				int64_t combined_offset = array_base_offset + member_offset;
				emitLEAArrayBase(textSectionData, combined_offset);

				// Add scaled index to base address: ADD RAX, RCX
				emitAddRAXRCX(textSectionData);

				// Load value from [RAX] with size-appropriate instruction
				emitLoadFromAddressInRAX(textSectionData, element_size_bytes);
			}
		} else if (instruction.isOperandType<std::string_view>(6) || instruction.isOperandType<std::string>(6)) {
			// Variable index stored as identifier name - need to compute address at runtime
			std::string index_var_name_str;
			if (instruction.isOperandType<std::string_view>(6)) {
				index_var_name_str = std::string(instruction.getOperandAs<std::string_view>(6));
			} else {
				index_var_name_str = instruction.getOperandAs<std::string>(6);
			}
			
			auto index_it = variable_scopes.back().identifier_offset.find(index_var_name_str);
			assert(index_it != variable_scopes.back().identifier_offset.end() && "Index variable not found");
			int64_t index_var_offset = index_it->second;
			std::cerr << "DEBUG: Variable index (identifier)=" << index_var_name_str << " offset=" << index_var_offset << " array_base=" << array_base_offset << "\n";

			if (is_array_pointer) {
				// Array is a pointer/temp var
				// Load array pointer into RAX: MOV RAX, [RBP + array_base_offset]
				auto load_ptr_opcodes = generateMovFromFrame(X64Register::RAX, array_base_offset);
				textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
				                       load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);

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

				// Add index offset to array pointer: ADD RAX, RCX
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0x01); // ADD r/m64, r64
				textSectionData.push_back(0xC8); // ModR/M: RAX, RCX

				// Load value from computed address based on element size
				if (element_size_bytes == 1) {
					// MOVZX EAX, BYTE PTR [RAX]
					textSectionData.push_back(0x0F); // Two-byte opcode prefix
					textSectionData.push_back(0xB6); // MOVZX r32, r/m8
					textSectionData.push_back(0x00); // ModR/M: [RAX], EAX
				} else if (element_size_bytes == 2) {
					// MOVZX EAX, WORD PTR [RAX]
					textSectionData.push_back(0x0F); // Two-byte opcode prefix
					textSectionData.push_back(0xB7); // MOVZX r32, r/m16
					textSectionData.push_back(0x00); // ModR/M: [RAX], EAX
				} else if (element_size_bytes == 4) {
					// MOV EAX, DWORD PTR [RAX]
					textSectionData.push_back(0x8B); // MOV r32, r/m32
					textSectionData.push_back(0x00); // ModR/M: [RAX], EAX
				} else {
					// MOV RAX, QWORD PTR [RAX] (8 bytes)
					textSectionData.push_back(0x48); // REX.W
					textSectionData.push_back(0x8B); // MOV r64, r/m64
					textSectionData.push_back(0x00); // ModR/M: [RAX], RAX
				}
			} else {
				// Array is a regular variable - use direct stack offset calculation
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

				// Load array base address into RAX: LEA RAX, [RBP + array_base_offset + member_offset]
				int64_t combined_offset = array_base_offset + member_offset;
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0x8D); // LEA r64, m
				if (combined_offset >= -128 && combined_offset <= 127) {
					textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
					textSectionData.push_back(static_cast<uint8_t>(combined_offset));
				} else {
					textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
					uint32_t offset_u32 = static_cast<uint32_t>(combined_offset);
					textSectionData.push_back(offset_u32 & 0xFF);
					textSectionData.push_back((offset_u32 >> 8) & 0xFF);
					textSectionData.push_back((offset_u32 >> 16) & 0xFF);
					textSectionData.push_back((offset_u32 >> 24) & 0xFF);
				}

				// Add index offset to base: ADD RAX, RCX
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0x01); // ADD r/m64, r64
				textSectionData.push_back(0xC8); // ModR/M: RAX, RCX

				// Load value from computed address based on element size
				if (element_size_bytes == 1) {
					// MOVZX EAX, BYTE PTR [RAX]
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0xB6);
					textSectionData.push_back(0x00);
				} else if (element_size_bytes == 2) {
					// MOVZX EAX, WORD PTR [RAX]
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0xB7);
					textSectionData.push_back(0x00);
				} else if (element_size_bytes == 4) {
					// MOV EAX, DWORD PTR [RAX]
					textSectionData.push_back(0x8B);
					textSectionData.push_back(0x00);
				} else {
					// MOV RAX, QWORD PTR [RAX]
					textSectionData.push_back(0x48);
					textSectionData.push_back(0x8B);
					textSectionData.push_back(0x00);
				}
			}
		} else {
			std::cerr << "ERROR: Index operand (6) is not unsigned long long or TempVar!\n";
			std::cerr << "  Index is string_view: " << instruction.isOperandType<std::string_view>(6) << "\n";
			std::cerr << "  Index is string: " << instruction.isOperandType<std::string>(6) << "\n";
			std::cerr << "  Index is int: " << instruction.isOperandType<int>(6) << "\n";
			assert(false && "Index must be constant or temp var");
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

	void handleArrayStore(const IrInstruction& instruction) {
		// ArrayStore: array_store [ElementType][ElementSize] %array, [IndexType][IndexSize] %index, %value
		// Format: [element_type, element_size, array_name, index_type, index_size, index_value, value]
		assert(instruction.getOperandCount() == 7 && "ArrayStore must have 7 operands");

		//auto element_type = instruction.getOperandAs<Type>(0);
		auto element_size_bits = instruction.getOperandAs<int>(1);
		//auto index_type = instruction.getOperandAs<Type>(3);
		//auto index_size_bits = instruction.getOperandAs<int>(4);

		// Get the array base address (from stack)
		std::string array_name;
		if (instruction.isOperandType<std::string_view>(2)) {
			array_name = std::string(instruction.getOperandAs<std::string_view>(2));
		} else if (instruction.isOperandType<std::string>(2)) {
			array_name = instruction.getOperandAs<std::string>(2);
		} else {
			assert(false && "Array must be an identifier for now");
		}

		// Calculate element size in bytes
		int element_size_bytes = element_size_bits / 8;

		// Check if this is a member array access (object.member format)
		bool is_member_array = array_name.find('.') != std::string::npos;
		std::string_view array_name_view(array_name);
		std::string_view object_name;
		std::string_view member_name;
		int64_t member_offset = 0;

		if (is_member_array) {
			// Parse object.member
			size_t dot_pos = array_name_view.find('.');
			object_name = array_name_view.substr(0, dot_pos);
			member_name = array_name_view.substr(dot_pos + 1);

			// Look up the member offset
			// Get object's type info to find member offset
			auto it = variable_scopes.back().identifier_offset.find(object_name);
			if (it != variable_scopes.back().identifier_offset.end()) {
				// Find member offset in struct layout
				// For now, we'll need to get this from the struct type info
				// This is a simplified approach - we'll add proper lookup later
				member_offset = 0; // Will be computed below when we have struct info access
			}
		}

		// Get the value to store
		X64Register value_reg = X64Register::RAX;
		if (instruction.isOperandType<unsigned long long>(6)) {
			// Constant value
			uint64_t value = instruction.getOperandAs<unsigned long long>(6);

			// Load immediate value into RAX
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0xB8); // MOV RAX, imm64
			for (size_t i = 0; i < 8; ++i) {
				textSectionData.push_back(static_cast<uint8_t>(value & 0xFF));
				value >>= 8;
			}
		} else if (instruction.isOperandType<TempVar>(6)) {
			// Value from temp var
			TempVar value_var = instruction.getOperandAs<TempVar>(6);
			int64_t value_offset = getStackOffsetFromTempVar(value_var);

			// Load value from stack into RAX
			auto load_opcodes = generateMovFromFrame(value_reg, value_offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
		}

		// Calculate offset directly: base_offset + member_offset + (index * element_size)
		// Arrays grow upward in memory (toward higher addresses), so we add the index
		std::string_view lookup_name = is_member_array ? object_name : array_name_view;
		int64_t array_base_offset = variable_scopes.back().identifier_offset[lookup_name];

		// Now compute the target address and store
		if (instruction.isOperandType<unsigned long long>(5)) {
			// Constant index
			uint64_t index_value = instruction.getOperandAs<unsigned long long>(5);
			int64_t element_offset = array_base_offset + member_offset + (index_value * element_size_bytes);

			// Store RAX to [RBP + offset]
			if (element_size_bytes == 1) {
				// MOV [RBP + offset], AL
				textSectionData.push_back(0x88); // MOV r/m8, r8
			} else if (element_size_bytes == 2) {
				// MOV [RBP + offset], AX
				textSectionData.push_back(0x66); // Operand size override
				textSectionData.push_back(0x89); // MOV r/m16, r16
			} else if (element_size_bytes == 4) {
				// MOV [RBP + offset], EAX
				textSectionData.push_back(0x89); // MOV r/m32, r32
			} else {
				// MOV [RBP + offset], RAX
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0x89); // MOV r/m64, r64
			}

			if (element_offset >= -128 && element_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], AL/AX/EAX/RAX
				textSectionData.push_back(static_cast<uint8_t>(element_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], AL/AX/EAX/RAX
				uint32_t offset_u32 = static_cast<uint32_t>(element_offset);
				textSectionData.push_back(offset_u32 & 0xFF);
				textSectionData.push_back((offset_u32 >> 8) & 0xFF);
				textSectionData.push_back((offset_u32 >> 16) & 0xFF);
				textSectionData.push_back((offset_u32 >> 24) & 0xFF);
			}
		} else if (instruction.isOperandType<TempVar>(5)) {
			// Variable index - need to compute address at runtime
			TempVar index_var = instruction.getOperandAs<TempVar>(5);
			int64_t index_var_offset = getStackOffsetFromTempVar(index_var);

			// Load index into RCX
			emitLoadIndexIntoRCX(textSectionData, index_var_offset);

			// Multiply index by element size
			emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);

			// Load array base address into RDX and add index offset
			int64_t combined_offset = array_base_offset + member_offset;
			// Note: Using RDX instead of RAX for array store operations
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x8D); // LEA r64, m
			if (combined_offset >= -128 && combined_offset <= 127) {
				textSectionData.push_back(0x55); // ModR/M: [RBP + disp8], RDX
				textSectionData.push_back(static_cast<uint8_t>(combined_offset));
			} else {
				textSectionData.push_back(0x95); // ModR/M: [RBP + disp32], RDX
				uint32_t offset_u32 = static_cast<uint32_t>(combined_offset);
				textSectionData.push_back(offset_u32 & 0xFF);
				textSectionData.push_back((offset_u32 >> 8) & 0xFF);
				textSectionData.push_back((offset_u32 >> 16) & 0xFF);
				textSectionData.push_back((offset_u32 >> 24) & 0xFF);
			}
			// ADD RDX, RCX (arrays grow upward in memory)
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x01); // ADD r/m64, r64
			textSectionData.push_back(0xCA); // ModR/M: RDX, RCX

			// Store value to computed address: MOV [RDX], AL/AX/EAX/RAX
			if (element_size_bytes == 1) {
				textSectionData.push_back(0x88); // MOV r/m8, r8
				textSectionData.push_back(0x02); // ModR/M: [RDX], AL
			} else if (element_size_bytes == 2) {
				textSectionData.push_back(0x66); // Operand size override
				textSectionData.push_back(0x89); // MOV r/m16, r16
				textSectionData.push_back(0x02); // ModR/M: [RDX], AX
			} else if (element_size_bytes == 4) {
				textSectionData.push_back(0x89); // MOV r/m32, r32
				textSectionData.push_back(0x02); // ModR/M: [RDX], EAX
			} else {
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0x89); // MOV r/m64, r64
				textSectionData.push_back(0x02); // ModR/M: [RDX], RAX
			}
		}
	}

	void handleStringLiteral(const IrInstruction& instruction) {
		// StringLiteral: %result = string_literal "content"
		// Format: [result_var, string_content]
		assert(instruction.getOperandCount() == 2 && "StringLiteral must have 2 operands");

		auto result_var = instruction.getOperandAs<TempVar>(0);
		auto string_content = instruction.getOperandAs<std::string_view>(1);

		// Add the string literal to the .rdata section and get its symbol name
		std::string symbol_name = writer.add_string_literal(std::string(string_content));

		// Calculate stack offset for the result variable (pointer to string)
		int64_t stack_offset = getStackOffsetFromTempVar(result_var);
		variable_scopes.back().identifier_offset[std::string(result_var.name())] = stack_offset;

		// LEA RAX, [RIP + symbol]
		// This requires a relocation entry
		textSectionData.push_back(0x48); // REX.W
		textSectionData.push_back(0x8D); // LEA
		textSectionData.push_back(0x05); // ModR/M: [RIP + disp32], RAX

		// Add placeholder for the displacement (will be filled by relocation)
		uint32_t relocation_offset = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Add a relocation for the string literal symbol
		writer.add_relocation(relocation_offset, symbol_name);

		// Store the address to the stack
		// MOV [RBP + offset], RAX
		if (stack_offset >= -128 && stack_offset <= 127) {
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x89); // MOV r/m64, r64
			textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
			textSectionData.push_back(static_cast<uint8_t>(stack_offset));
		} else {
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x89); // MOV r/m64, r64
			textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
			uint32_t offset_u32 = static_cast<uint32_t>(stack_offset);
			textSectionData.push_back(offset_u32 & 0xFF);
			textSectionData.push_back((offset_u32 >> 8) & 0xFF);
			textSectionData.push_back((offset_u32 >> 16) & 0xFF);
			textSectionData.push_back((offset_u32 >> 24) & 0xFF);
		}
	}

	void handleMemberAccess(const IrInstruction& instruction) {
		// MemberAccess: %result = member_access [MemberType][MemberSize] %object, member_name, offset
		// Format (new): [result_var, member_type, member_size, object_name, member_name, offset, is_ref, is_rvalue_ref, referenced_bits]
		assert(instruction.getOperandCount() >= 6 && "MemberAccess must have at least 6 operands");

		auto result_var = instruction.getOperandAs<TempVar>(0);
		auto member_type = instruction.getOperandAs<Type>(1);
		auto member_size_bits = instruction.getOperandAs<int>(2);
		bool has_reference_metadata = instruction.getOperandCount() >= 9;
		bool is_reference_member = has_reference_metadata ? instruction.getOperandAs<bool>(6) : false;
		bool is_rvalue_reference_member = has_reference_metadata ? instruction.getOperandAs<bool>(7) : false;
		int referenced_size_bits = has_reference_metadata ? instruction.getOperandAs<int>(8) : member_size_bits;
		// Operand 3 can be either a string_view (variable name) or TempVar (nested access result)
		//auto member_name = instruction.getOperandAs<std::string_view>(4);
		auto member_offset = instruction.getOperandAs<int>(5);

		// Get the object's base stack offset or pointer
		int32_t object_base_offset = 0;
		bool is_pointer_access = false;  // true if object is 'this' or a reference parameter (both are pointers)
		const StackVariableScope& current_scope = variable_scopes.back();

		// Check if operand 3 is a string_view (variable name) or TempVar (nested access)
		std::string_view object_name;
		if (instruction.isOperandType<std::string_view>(3)) {
			// Simple case: object is a variable name
			object_name = instruction.getOperandAs<std::string_view>(3);
			auto it = current_scope.identifier_offset.find(object_name);
			if (it == current_scope.identifier_offset.end()) {
				assert(false && "Struct object not found in scope");
				return;
			}
			object_base_offset = it->second;

			// Check if this is the 'this' pointer or a reference parameter (both need dereferencing)
			if (object_name == "this" || reference_stack_info_.count(object_base_offset) > 0) {
				is_pointer_access = true;
			}
		} else if (instruction.isOperandType<TempVar>(3)) {
			// Nested case: object is the result of a previous member access
			auto object_temp = instruction.getOperandAs<TempVar>(3);
			object_base_offset = getStackOffsetFromTempVar(object_temp);
		} else {
			assert(false && "MemberAccess operand 3 must be string_view or TempVar");
			return;
		}

		// Calculate the member's actual stack offset
		int32_t member_stack_offset;
		if (is_pointer_access) {
			// For 'this' pointer: we need to load the pointer and use indirect addressing
			// The pointer is at [RBP + object_base_offset]
			// We'll handle this differently below
			member_stack_offset = 0;  // Not used for pointer access
		} else {
			// For a struct at [RBP - 8] with member at offset 4:
			//   member is at [RBP - 8 + 4] = [RBP - 4]
			member_stack_offset = object_base_offset + member_offset;
		}

		// Get the result variable's stack offset from the identifier_offset map
		int32_t result_offset;
		auto it = current_scope.identifier_offset.find(result_var.name());
		if (it != current_scope.identifier_offset.end()) {
			result_offset = it->second;
		} else {
			// Fallback (shouldn't happen if temp var was properly registered)
			result_offset = getStackOffsetFromTempVar(result_var);
		}

		// Calculate member size in bytes
		int member_size_bytes = member_size_bits / 8;

		// Flush all dirty registers to ensure values are saved before allocating
		// This fixes the codegen bug where intermediate arithmetic results get overwritten
		flushAllDirtyRegisters();

		// Allocate a register for loading the member value
		X64Register temp_reg = allocateRegisterWithSpilling();

		if (is_pointer_access) {
			// For 'this' pointer: load pointer into RCX, then load from [RCX + member_offset]
			// Load 'this' pointer from stack into RCX
			// MOV RCX, [RBP + object_base_offset]
			auto load_ptr_opcodes = generateMovFromFrame(X64Register::RCX, object_base_offset);
			textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
			                       load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);

			// Now load from [RCX + member_offset] into temp_reg using size-appropriate function
			OpCodeWithSize load_opcodes;
			if (member_size_bytes == 8) {
				load_opcodes = generateMovFromMemory(temp_reg, X64Register::RCX, member_offset);
			} else if (member_size_bytes == 4) {
				load_opcodes = generateMovFromMemory32(temp_reg, X64Register::RCX, member_offset);
			} else if (member_size_bytes == 2) {
				load_opcodes = generateMovFromMemory16(temp_reg, X64Register::RCX, member_offset);
			} else if (member_size_bytes == 1) {
				load_opcodes = generateMovFromMemory8(temp_reg, X64Register::RCX, member_offset);
			} else {
				assert(false && "Unsupported member size");
				return;
			}
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
		} else {
			// For regular struct variables on the stack, use size-appropriate load
			OpCodeWithSize load_opcodes;
			if (member_size_bytes == 8) {
				load_opcodes = generateMovFromFrame(temp_reg, member_stack_offset);
			} else if (member_size_bytes == 4) {
				load_opcodes = generateMovFromFrame32(temp_reg, member_stack_offset);
			} else if (member_size_bytes == 2) {
				load_opcodes = generateMovzxFromFrame16(temp_reg, member_stack_offset);
			} else if (member_size_bytes == 1) {
				load_opcodes = generateMovzxFromFrame8(temp_reg, member_stack_offset);
			} else {
				assert(false && "Unsupported member size");
				return;
			}
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
		}

		if (is_reference_member) {
			auto store_ptr = generateMovToFrame(temp_reg, result_offset);
			textSectionData.insert(textSectionData.end(), store_ptr.op_codes.begin(),
				               store_ptr.op_codes.begin() + store_ptr.size_in_bytes);
			regAlloc.release(temp_reg);
			reference_stack_info_[result_offset] = ReferenceInfo{
				.value_type = member_type,
				.value_size_bits = referenced_size_bits,
				.is_rvalue_reference = is_rvalue_reference_member
			};
			return;
		}

		// Store the result - but keep it in the register for subsequent operations
		regAlloc.set_stack_variable_offset(temp_reg, result_offset);
	}

	void handleMemberStore(const IrInstruction& instruction) {
		// MemberStore: member_store [MemberType][MemberSize] %object, member_name, offset, %value
		// Format (new): [member_type, member_size, object_name, member_name, offset, is_ref, is_rvalue_ref, referenced_bits, value]
		assert(instruction.getOperandCount() >= 6 && "MemberStore must have at least 6 operands");

		//auto member_type = instruction.getOperandAs<Type>(0);
		auto member_size_bits = instruction.getOperandAs<int>(1);
		// Operand 2 can be either a string_view (variable name) or TempVar (nested access result)
		//auto member_name = instruction.getOperandAs<std::string_view>(3);
		auto member_offset = instruction.getOperandAs<int>(4);

		bool has_reference_metadata = instruction.getOperandCount() >= 9;
		bool is_reference_member = has_reference_metadata ? instruction.getOperandAs<bool>(5) : false;
		bool is_rvalue_reference_member = has_reference_metadata ? instruction.getOperandAs<bool>(6) : false;
		int referenced_size_bits = has_reference_metadata ? instruction.getOperandAs<int>(7) : member_size_bits;
		size_t value_operand_index = has_reference_metadata ? 8 : 5;

		// Get the value - it could be a TempVar, a literal (int, unsigned long long, bool, double), or a string_view (variable name)
		TempVar value_var;
		bool is_literal = false;
		int64_t literal_value = 0;
		double literal_double_value = 0.0;
		bool is_double_literal = false;
		bool is_variable = false;
		std::string variable_name;

		if (instruction.isOperandType<TempVar>(value_operand_index)) {
			value_var = instruction.getOperandAs<TempVar>(value_operand_index);
		} else if (instruction.isOperandType<int>(value_operand_index)) {
			is_literal = true;
			literal_value = instruction.getOperandAs<int>(value_operand_index);
		} else if (instruction.isOperandType<unsigned long long>(value_operand_index)) {
			is_literal = true;
			literal_value = static_cast<int64_t>(instruction.getOperandAs<unsigned long long>(value_operand_index));
		} else if (instruction.isOperandType<bool>(value_operand_index)) {
			is_literal = true;
			literal_value = instruction.getOperandAs<bool>(value_operand_index) ? 1 : 0;
		} else if (instruction.isOperandType<char>(value_operand_index)) {
			is_literal = true;
			literal_value = static_cast<int64_t>(instruction.getOperandAs<char>(value_operand_index));
		} else if (instruction.isOperandType<double>(value_operand_index)) {
			is_literal = true;
			is_double_literal = true;
			literal_double_value = instruction.getOperandAs<double>(value_operand_index);
		} else if (instruction.isOperandType<std::string_view>(value_operand_index)) {
			is_variable = true;
			variable_name = std::string(instruction.getOperandAs<std::string_view>(value_operand_index));
		} else if (instruction.isOperandType<std::string>(value_operand_index)) {
			is_variable = true;
			variable_name = instruction.getOperandAs<std::string>(value_operand_index);
		} else {
			assert(false && "Value must be TempVar, int/unsigned long long/bool/double literal, or string_view");
			return;
		}

		// Get the object's base stack offset or pointer
		int32_t object_base_offset = 0;
		bool is_pointer_access = false;  // true if object is 'this' (a pointer)
		const StackVariableScope& current_scope = variable_scopes.back();

		// Check if operand 2 is a string_view/string (variable name) or TempVar (nested access)
		std::string object_name_str;
		if (instruction.isOperandType<std::string_view>(2)) {
			// Simple case: object is a variable name (string_view)
			object_name_str = std::string(instruction.getOperandAs<std::string_view>(2));
			auto it = current_scope.identifier_offset.find(object_name_str);
			if (it == current_scope.identifier_offset.end()) {
				assert(false && "Struct object not found in scope");
				return;
			}
			object_base_offset = it->second;

			// Check if this is the 'this' pointer
			if (object_name_str == "this") {
				is_pointer_access = true;
			}
		} else if (instruction.isOperandType<std::string>(2)) {
			// Simple case: object is a variable name (std::string)
			object_name_str = instruction.getOperandAs<std::string>(2);
			auto it = current_scope.identifier_offset.find(object_name_str);
			if (it == current_scope.identifier_offset.end()) {
				assert(false && "Struct object not found in scope");
				return;
			}
			object_base_offset = it->second;

			// Check if this is the 'this' pointer or a reference parameter
			if (object_name_str == "this" || reference_stack_info_.count(object_base_offset) > 0) {
				is_pointer_access = true;
			}
		} else if (instruction.isOperandType<TempVar>(2)) {
			// Nested case: object is the result of a previous member access
			auto object_temp = instruction.getOperandAs<TempVar>(2);
			object_base_offset = getStackOffsetFromTempVar(object_temp);
		} else {
			assert(false && "MemberStore operand 2 must be string_view or TempVar");
			return;
		}

		// Calculate the member's actual stack offset
		int32_t member_stack_offset;
		if (is_pointer_access) {
			// For 'this' pointer: we need to load the pointer and use indirect addressing
			// The pointer is at [RBP + object_base_offset]
			// We'll handle this differently below
			member_stack_offset = 0;  // Not used for pointer access
		} else {
			// For a struct at [RBP - 8] with member at offset 4:
			//   member is at [RBP - 8 + 4] = [RBP - 4]
			member_stack_offset = object_base_offset + member_offset;
		}

		// Calculate member size in bytes
		int member_size_bytes = member_size_bits / 8;

		// Load the value into a register
		X64Register value_reg = X64Register::RAX;

		if (is_reference_member) {
			value_reg = allocateRegisterWithSpilling();
			bool pointer_loaded = false;
			if (!is_literal) {
				pointer_loaded = loadAddressForOperand(instruction, value_operand_index, value_reg);
			}
			if (!pointer_loaded) {
				if (is_literal && literal_value == 0) {
					moveImmediateToRegister(value_reg, 0);
					pointer_loaded = true;
				}
			}
			if (!pointer_loaded) {
				std::cerr << "ERROR: Reference member initializer must be an lvalue" << std::endl;
				assert(false && "Reference member initializer must be an lvalue");
			}
		} else if (is_literal) {
			if (is_double_literal) {
				uint64_t bits;
				std::memcpy(&bits, &literal_double_value, sizeof(bits));
				textSectionData.push_back(0x48);
				textSectionData.push_back(0xB8 + static_cast<uint8_t>(value_reg));
				for (int i = 0; i < 8; i++) {
					textSectionData.push_back((bits >> (i * 8)) & 0xFF);
				}
			} else {
				textSectionData.push_back(0x48);
				textSectionData.push_back(0xB8 + static_cast<uint8_t>(value_reg));
				uint64_t imm64 = static_cast<uint64_t>(literal_value);
				for (int i = 0; i < 8; i++) {
					textSectionData.push_back((imm64 >> (i * 8)) & 0xFF);
				}
			}
		} else if (is_variable) {
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
			int32_t value_offset = getStackOffsetFromTempVar(value_var);
			auto existing_reg = regAlloc.findRegisterForStackOffset(value_offset);
			if (existing_reg.has_value()) {
				value_reg = existing_reg.value();
			} else {
				auto load_opcodes = generateMovFromFrame(value_reg, value_offset);
				textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
				               load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
			}
		}

		// Store the value to the member's location
		if (is_pointer_access) {
			// For 'this' pointer: load pointer into RCX, then store to [RCX + member_offset]
			// Load 'this' pointer from stack into RCX
			// MOV RCX, [RBP + object_base_offset]
			auto load_ptr_opcodes = generateMovFromFrame(X64Register::RCX, object_base_offset);
			textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
			                       load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);

			// Now store value_reg to [RCX + member_offset]
			// Get the register encoding (lower 3 bits for ModR/M, and check if R8-R15 for REX)
			uint8_t value_reg_bits = static_cast<uint8_t>(value_reg) & 0x07;
			bool value_needs_rex_r = static_cast<uint8_t>(value_reg) >= static_cast<uint8_t>(X64Register::R8);

			if (member_size_bytes == 8) {
				// 64-bit member: MOV [RCX + member_offset], value_reg
				uint8_t rex = 0x48; // REX.W
				if (value_needs_rex_r) rex |= 0x04; // REX.R for R8-R15
				textSectionData.push_back(rex);
				textSectionData.push_back(0x89); // MOV r/m64, r64
				if (member_offset == 0) {
					textSectionData.push_back(0x01 + (value_reg_bits << 3)); // ModR/M: [RCX], value_reg
				} else if (member_offset >= -128 && member_offset <= 127) {
					textSectionData.push_back(0x41 + (value_reg_bits << 3)); // ModR/M: [RCX + disp8], value_reg
					textSectionData.push_back(static_cast<uint8_t>(member_offset));
				} else {
					textSectionData.push_back(0x81 + (value_reg_bits << 3)); // ModR/M: [RCX + disp32], value_reg
					uint32_t offset_u32 = static_cast<uint32_t>(member_offset);
					textSectionData.push_back(offset_u32 & 0xFF);
					textSectionData.push_back((offset_u32 >> 8) & 0xFF);
					textSectionData.push_back((offset_u32 >> 16) & 0xFF);
					textSectionData.push_back((offset_u32 >> 24) & 0xFF);
				}
			} else if (member_size_bytes == 4) {
				// 32-bit member: MOV [RCX + member_offset], value_reg (32-bit)
				if (value_needs_rex_r) {
					textSectionData.push_back(0x44); // REX.R for R8-R15
				}
				textSectionData.push_back(0x89); // MOV r/m32, r32
				if (member_offset == 0) {
					textSectionData.push_back(0x01 + (value_reg_bits << 3)); // ModR/M: [RCX], value_reg
				} else if (member_offset >= -128 && member_offset <= 127) {
					textSectionData.push_back(0x41 + (value_reg_bits << 3)); // ModR/M: [RCX + disp8], value_reg
					textSectionData.push_back(static_cast<uint8_t>(member_offset));
				} else {
					textSectionData.push_back(0x81 + (value_reg_bits << 3)); // ModR/M: [RCX + disp32], value_reg
					uint32_t offset_u32 = static_cast<uint32_t>(member_offset);
					textSectionData.push_back(offset_u32 & 0xFF);
					textSectionData.push_back((offset_u32 >> 8) & 0xFF);
					textSectionData.push_back((offset_u32 >> 16) & 0xFF);
					textSectionData.push_back((offset_u32 >> 24) & 0xFF);
				}
			} else if (member_size_bytes == 2) {
				// 16-bit member: MOV [RCX + member_offset], value_reg (16-bit)
				textSectionData.push_back(0x66); // Operand-size override prefix
				if (value_needs_rex_r) {
					textSectionData.push_back(0x44); // REX.R for R8-R15
				}
				textSectionData.push_back(0x89); // MOV r/m16, r16
				if (member_offset == 0) {
					textSectionData.push_back(0x01 + (value_reg_bits << 3)); // ModR/M: [RCX], value_reg
				} else if (member_offset >= -128 && member_offset <= 127) {
					textSectionData.push_back(0x41 + (value_reg_bits << 3)); // ModR/M: [RCX + disp8], value_reg
					textSectionData.push_back(static_cast<uint8_t>(member_offset));
				} else {
					textSectionData.push_back(0x81 + (value_reg_bits << 3)); // ModR/M: [RCX + disp32], value_reg
					uint32_t offset_u32 = static_cast<uint32_t>(member_offset);
					textSectionData.push_back(offset_u32 & 0xFF);
					textSectionData.push_back((offset_u32 >> 8) & 0xFF);
					textSectionData.push_back((offset_u32 >> 16) & 0xFF);
					textSectionData.push_back((offset_u32 >> 24) & 0xFF);
				}
			} else if (member_size_bytes == 1) {
				// 8-bit member: MOV [RCX + member_offset], value_reg (8-bit)
				if (value_needs_rex_r) {
					textSectionData.push_back(0x44); // REX.R for R8-R15
				}
				textSectionData.push_back(0x88); // MOV r/m8, r8
				if (member_offset == 0) {
					textSectionData.push_back(0x01 + (value_reg_bits << 3)); // ModR/M: [RCX], value_reg
				} else if (member_offset >= -128 && member_offset <= 127) {
					textSectionData.push_back(0x41 + (value_reg_bits << 3)); // ModR/M: [RCX + disp8], value_reg
					textSectionData.push_back(static_cast<uint8_t>(member_offset));
				} else {
					textSectionData.push_back(0x81 + (value_reg_bits << 3)); // ModR/M: [RCX + disp32], value_reg
					uint32_t offset_u32 = static_cast<uint32_t>(member_offset);
					textSectionData.push_back(offset_u32 & 0xFF);
					textSectionData.push_back((offset_u32 >> 8) & 0xFF);
					textSectionData.push_back((offset_u32 >> 16) & 0xFF);
					textSectionData.push_back((offset_u32 >> 24) & 0xFF);
				}
			} else {
				assert(false && "Unsupported member size");
				return;
			}
		} else {
			// For regular struct variables on the stack
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
				// Array members should use ArrayStore, not MemberStore
				// If this fires, CodeGen.h is generating incorrect IR - fix it there
				std::cerr << "ERROR: MemberStore with size " << member_size_bytes << " bytes (array member?)\n";
				assert(false && "Array elements should use ArrayStore, not MemberStore");
				return;
			}
		}
	}

	void handleAddressOf(const IrInstruction& instruction) {
		// Address-of: &x
		// Operands: [result_var, type, size, operand]
		assert(instruction.getOperandCount() == 4 && "AddressOf must have 4 operands");

		int32_t var_offset;
		
		// Get operand (variable to take address of) - can be string_view, string, or TempVar
		if (instruction.isOperandType<TempVar>(3)) {
			// Taking address of a temporary variable (e.g., for rvalue references)
			TempVar temp = instruction.getOperandAs<TempVar>(3);
			var_offset = getStackOffsetFromTempVar(temp);
		} else {
			// Taking address of a named variable
			std::string operand_str;
			if (instruction.isOperandType<std::string_view>(3)) {
				operand_str = std::string(instruction.getOperandAs<std::string_view>(3));
			} else if (instruction.isOperandType<std::string>(3)) {
				operand_str = instruction.getOperandAs<std::string>(3);
			} else {
				assert(false && "AddressOf operand must be string_view, string, or TempVar");
				return;
			}

			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.identifier_offset.find(operand_str);
			if (it == current_scope.identifier_offset.end()) {
				assert(false && "Variable not found in scope");
				return;
			}
			var_offset = it->second;
		}

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

		Type value_type = instruction.getOperandAs<Type>(1);
		int value_size = instruction.getOperandAs<int>(2);

		// Load the pointer operand into a register
		X64Register ptr_reg = loadOperandIntoRegister(instruction, 3);

		// Track which register holds the dereferenced value (may differ from ptr_reg for MOVZX)
		X64Register value_reg = ptr_reg;

		// Determine the correct MOV instruction based on size and build REX prefix
		uint8_t rex_prefix = 0x40; // Base REX prefix
		uint8_t opcode = 0x8B;     // MOV r64, r/m64
		
		// Check if we need REX.W (64-bit operand size)
		if (value_size == 64) {
			rex_prefix |= 0x08; // Set W bit for 64-bit operand
		} else if (value_size == 32) {
			// 32-bit uses base REX (no W bit)
		}
		
		// Check if destination register (same as source) is R8-R15
		if (static_cast<uint8_t>(ptr_reg) >= 8) {
			rex_prefix |= 0x05; // Set both R and B bits (destination and base)
		}
		
		// For 8-bit, we'll use MOVZX which has different encoding
		bool use_movzx = (value_size == 8);
		
		// Handle special case: RSP/R12 requires SIB byte
		uint8_t ptr_encoding = static_cast<uint8_t>(ptr_reg) & 0x07;
		bool needs_sib = (ptr_encoding == 0x04);

		if (use_movzx) {
			// MOVZX EAX, byte ptr [ptr_reg] - always loads into RAX
			value_reg = X64Register::RAX;
			
			// ModR/M byte: mod=00 (indirect), reg=RAX (0), r/m=ptr_reg
			uint8_t modrm_byte = (0x00 << 6) | (0x00 << 3) | ptr_encoding;
			
			if (static_cast<uint8_t>(ptr_reg) >= 8) {
				textSectionData.push_back(0x41); // REX with B bit for extended base register
			}
			textSectionData.push_back(0x0F);
			textSectionData.push_back(0xB6);
			textSectionData.push_back(modrm_byte);
			if (needs_sib) {
				textSectionData.push_back(0x24); // SIB: no scale, no index, base=RSP/R12
			}
		} else {
			// Regular MOV ptr_reg, [ptr_reg]
			// ModR/M byte: mod=00 (indirect, no disp), reg=ptr_reg, r/m=ptr_reg
			uint8_t modrm_byte = (0x00 << 6) | (ptr_encoding << 3) | ptr_encoding;
			
			if (rex_prefix != 0x40 || static_cast<uint8_t>(ptr_reg) >= 8) {
				textSectionData.push_back(rex_prefix);
			}
			textSectionData.push_back(opcode);
			textSectionData.push_back(modrm_byte);
			if (needs_sib) {
				textSectionData.push_back(0x24); // SIB: no scale, no index, base=RSP/R12
			}
		}

		// Store the dereferenced value to result_var
		auto result_var = instruction.getOperandAs<TempVar>(0);
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		auto result_store = generateMovToFrame(value_reg, result_offset);
		textSectionData.insert(textSectionData.end(), result_store.op_codes.begin(),
		                       result_store.op_codes.begin() + result_store.size_in_bytes);
	}

	void handleConditionalBranch(const IrInstruction& instruction) {
		// Conditional branch: test condition, jump if false to else_label, fall through to then_label
		// Operands: [type, size, condition_value, then_label, else_label]
		assert(instruction.getOperandCount() >= 5 && "ConditionalBranch must have at least 5 operands");

		//Type condition_type = instruction.getOperandAs<Type>(0);
		//int condition_size = instruction.getOperandAs<int>(1);
		auto condition_value = instruction.getOperand(2);
		auto then_label = instruction.getOperandAs<std::string>(3);
		auto else_label = instruction.getOperandAs<std::string>(4);

		// Flush all dirty registers before branching
		flushAllDirtyRegisters();

		// Load condition value into a register
		X64Register condition_reg = X64Register::RAX;

		if (std::holds_alternative<TempVar>(condition_value) ||
		    std::holds_alternative<std::string_view>(condition_value)) {
			// Load condition from TempVar or variable using helper
			condition_reg = loadOperandIntoRegister(instruction, 2);
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
			condition_reg = X64Register::RAX;
		}

		// Test if condition is non-zero: TEST reg, reg
		// Use the actual register that holds the condition value
		uint8_t reg_code = static_cast<uint8_t>(condition_reg);
		uint8_t modrm = 0xC0 | ((reg_code & 0x7) << 3) | (reg_code & 0x7);  // TEST reg, reg
		std::array<uint8_t, 3> testInst = { 0x48, 0x85, modrm }; // test condition_reg, condition_reg
		textSectionData.insert(textSectionData.end(), testInst.begin(), testInst.end());

		// Jump if ZERO (JZ/JE) to else_label - inverted logic for better code layout
		textSectionData.push_back(0x0F); // Two-byte opcode prefix
		textSectionData.push_back(0x84); // JZ/JE rel32

		uint32_t else_patch_position = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		pending_branches_.push_back({else_label, else_patch_position});

		// Fall through to then block - then_label will be placed right after this
	}

	void handleFunctionAddress(const IrInstruction& instruction) {
		// FunctionAddress: Load address of a function into a register
		// Format: [result_temp, function_name]
		assert(instruction.getOperandCount() == 2 && "FunctionAddress must have exactly 2 operands");

		flushAllDirtyRegisters();

		auto result_var = instruction.getOperandAs<TempVar>(0);
		std::string_view func_name = instruction.getOperandAs<std::string_view>(1);

		// Get result offset
		int result_offset = getStackOffsetFromTempVar(result_var);

		// Load the address of the function into RAX using RIP-relative addressing
		// LEA RAX, [RIP + function_name]  (position-independent code, uses REL32 relocation)
		textSectionData.push_back(0x48); // REX.W
		textSectionData.push_back(0x8D); // LEA
		textSectionData.push_back(0x05); // ModR/M: RAX, [RIP + disp32]

		// Add placeholder for the displacement (will be filled by relocation)
		uint32_t reloc_position = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Add REL32 relocation for the function address (RIP-relative)
		std::string mangled_name = writer.getMangledName(std::string(func_name));
		writer.add_relocation(reloc_position, mangled_name, IMAGE_REL_AMD64_REL32);

		// Store RAX to result variable
		auto store_opcodes = generateMovToFrame(X64Register::RAX, result_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

		regAlloc.reset();
	}

	void handleIndirectCall(const IrInstruction& instruction) {
		// IndirectCall: Call through function pointer
		// Format: [result_temp, func_ptr_var, arg1_type, arg1_size, arg1_value, ...]
		assert(instruction.getOperandCount() >= 2 && "IndirectCall must have at least 2 operands");

		flushAllDirtyRegisters();

		auto result_var = instruction.getOperand(0);
		auto func_ptr_operand = instruction.getOperand(1);

		// Get result offset
		int result_offset = 0;
		if (std::holds_alternative<TempVar>(result_var)) {
			const TempVar temp_var = std::get<TempVar>(result_var);
			result_offset = getStackOffsetFromTempVar(temp_var);
			variable_scopes.back().identifier_offset[temp_var.name()] = result_offset;
		} else {
			result_offset = variable_scopes.back().identifier_offset[std::get<std::string_view>(result_var)];
		}

		// Load function pointer into RAX using helper
		X64Register func_ptr_reg = loadOperandIntoRegister(instruction, 1);
		if (func_ptr_reg != X64Register::RAX) {
			// Move to RAX if not already there
			auto mov_encoding = encodeRegToRegInstruction(X64Register::RAX, func_ptr_reg);
			std::array<uint8_t, 3> movInst = { mov_encoding.rex_prefix, 0x89, mov_encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
			func_ptr_reg = X64Register::RAX;
		}

		// Process arguments (if any)
		const size_t first_arg_index = 2; // Start after result and function pointer
		const size_t arg_stride = 3; // type, size, value
		const size_t num_args = (instruction.getOperandCount() - first_arg_index) / arg_stride;
		for (size_t i = 0; i < num_args && i < 4; ++i) {
			size_t argIndex = first_arg_index + i * arg_stride;
			Type argType = instruction.getOperandAs<Type>(argIndex);
			// int argSize = instruction.getOperandAs<int>(argIndex + 1); // unused for now
			auto argValue = instruction.getOperand(argIndex + 2);

			// Determine if this is a floating-point argument
			bool is_float_arg = is_floating_point_type(argType);

			// Determine the target register for the argument
			X64Register target_reg = is_float_arg ? FLOAT_PARAM_REGS[i] : INT_PARAM_REGS[i];

			// Load argument into target register
			if (std::holds_alternative<TempVar>(argValue)) {
				const TempVar temp_var = std::get<TempVar>(argValue);
				int arg_offset = getStackOffsetFromTempVar(temp_var);
				if (is_float_arg) {
					bool is_float = (argType == Type::Float);
					auto load_opcodes = generateFloatMovFromFrame(target_reg, arg_offset, is_float);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				} else {
					auto load_opcodes = generateMovFromFrame(target_reg, arg_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				}
			} else if (std::holds_alternative<std::string_view>(argValue)) {
				std::string_view var_name = std::get<std::string_view>(argValue);
				int arg_offset = variable_scopes.back().identifier_offset[var_name];
				if (is_float_arg) {
					bool is_float = (argType == Type::Float);
					auto load_opcodes = generateFloatMovFromFrame(target_reg, arg_offset, is_float);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				} else {
					auto load_opcodes = generateMovFromFrame(target_reg, arg_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				}
			} else if (std::holds_alternative<unsigned long long>(argValue)) {
				// Immediate value
				unsigned long long value = std::get<unsigned long long>(argValue);
				// MOV target_reg, imm64
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0xB8 + static_cast<uint8_t>(target_reg)); // MOV reg, imm64
				for (size_t j = 0; j < 8; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(value & 0xFF));
					value >>= 8;
				}
			}
		}

		// Call through function pointer in RAX
		// CALL RAX
		textSectionData.push_back(0xFF); // CALL r/m64
		textSectionData.push_back(0xD0); // ModR/M: RAX

		// Store return value from RAX to result variable
		auto store_opcodes = generateMovToFrame(X64Register::RAX, result_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

		regAlloc.reset();
	}

	void finalizeSections() {
		// Emit global variables to .data or .bss sections FIRST
		// This creates the symbols that relocations will reference
		for (const auto& global : global_variables_) {
			writer.add_global_variable(global.name, global.size_in_bytes, global.is_initialized, global.init_value);
		}

		// Now add pending global variable relocations (after symbols are created)
		for (const auto& reloc : pending_global_relocations_) {
			writer.add_text_relocation(reloc.offset, reloc.symbol_name, reloc.type);
		}

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
			writer.add_function_exception_info(current_function_mangled_name_, current_function_offset_, function_length);

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
	std::unordered_map<std::string, std::vector<IrInstruction>> function_instructions;

	RegisterAllocator regAlloc;

	// Debug information tracking
	std::string current_function_name_;
	std::string_view current_function_mangled_name_;
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

	// Global variable tracking
	struct GlobalVariableInfo {
		std::string name;
		Type type;
		size_t size_in_bytes;
		bool is_initialized;
		unsigned long long init_value = 0;
	};
	std::vector<GlobalVariableInfo> global_variables_;

	// Pending global variable relocations (added after symbols are created)
	struct PendingGlobalRelocation {
		uint64_t offset;
		std::string symbol_name;
		uint32_t type;
	};
	std::vector<PendingGlobalRelocation> pending_global_relocations_;

	// Track which stack offsets hold references (parameters or locals)
	std::unordered_map<int32_t, ReferenceInfo> reference_stack_info_;
};




