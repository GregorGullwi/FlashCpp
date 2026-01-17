#pragma once

#include <string>
#include <numeric>
#include <vector>
#include <array>
#include <variant>
#include <string_view>
#include <span>
#include <assert.h>
#include <unordered_map>
#include <type_traits>

#include "IRTypes.h"
#include "ObjFileWriter.h"
#include "ElfFileWriter.h"
#include "ProfilingTimer.h"
#include "ChunkedString.h"

// Global exception handling control (defined in main.cpp)
extern bool g_enable_exceptions;

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

// Maximum possible size for mov instructions:
// - Regular integer mov: REX (1) + Opcode (1) + ModR/M (1) + SIB (1) + Disp32 (4) = 8 bytes
// - SSE float mov: Prefix (1) + REX (1) + Opcode (2) + ModR/M (1) + Disp32 (4) = 9 bytes
static constexpr size_t MAX_MOV_INSTRUCTION_SIZE = 9;

struct OpCodeWithSize {
	std::array<uint8_t, MAX_MOV_INSTRUCTION_SIZE> op_codes{};  // Zero-initialize
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
 * @brief Checks if an XMM register requires a REX prefix (XMM8-XMM15).
 * 
 * @param xmm_reg The XMM register to check
 * @return true if the register is XMM8-XMM15, false otherwise
 */
inline bool xmm_needs_rex(X64Register xmm_reg) {
	uint8_t xmm_index = xmm_modrm_bits(xmm_reg);
	return xmm_index >= 8;
}

/**
 * @brief Generates a properly encoded SSE instruction for XMM register operations.
 * 
 * This function handles the REX prefix for XMM8-XMM15 registers. For SSE instructions,
 * the REX prefix format is: 0100WRXB where:
 * - W is 0 for most SSE ops (legacy SSE, not 64-bit extension)
 * - R extends the ModR/M reg field (for xmm_dst >= XMM8)
 * - X extends the SIB index field (not used for reg-reg ops)
 * - B extends the ModR/M r/m field (for xmm_src >= XMM8)
 * 
 * @param prefix1 First prefix byte (e.g., 0xF3 for single-precision, 0xF2 for double-precision)
 * @param opcode1 First opcode byte (usually 0x0F for two-byte opcodes)
 * @param opcode2 Second opcode byte (e.g., 0x58 for addss/addsd)
 * @param xmm_dst Destination XMM register
 * @param xmm_src Source XMM register
 * @return An OpCodeWithSize struct containing the instruction bytes
 */
inline OpCodeWithSize generateSSEInstruction(uint8_t prefix1, uint8_t opcode1, uint8_t opcode2, 
                                              X64Register xmm_dst, X64Register xmm_src) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* ptr = result.op_codes.data();
	
	uint8_t dst_index = xmm_modrm_bits(xmm_dst);
	uint8_t src_index = xmm_modrm_bits(xmm_src);
	
	bool needs_rex = (dst_index >= 8) || (src_index >= 8);
	
	// Always emit the mandatory prefix (F3 or F2)
	*ptr++ = prefix1;
	result.size_in_bytes++;
	
	// REX prefix must come after F3/F2 but before 0F for SSE instructions
	if (needs_rex) {
		uint8_t rex = 0x40;  // Base REX prefix
		if (dst_index >= 8) {
			rex |= 0x04;  // REX.R - extends ModR/M reg field
		}
		if (src_index >= 8) {
			rex |= 0x01;  // REX.B - extends ModR/M r/m field
		}
		*ptr++ = rex;
		result.size_in_bytes++;
	}
	
	// Emit opcode bytes
	*ptr++ = opcode1;
	result.size_in_bytes++;
	*ptr++ = opcode2;
	result.size_in_bytes++;
	
	// ModR/M byte: 11 reg r/m (register-to-register mode)
	uint8_t modrm = 0xC0 + ((dst_index & 0x07) << 3) + (src_index & 0x07);
	*ptr++ = modrm;
	result.size_in_bytes++;
	
	return result;
}

/**
 * @brief Generates a properly encoded SSE instruction without mandatory prefix (like comiss).
 * 
 * This function handles the REX prefix for XMM8-XMM15 registers. For SSE instructions
 * without mandatory prefix (F3/F2), the format is: [REX] 0F opcode ModR/M
 * 
 * @param opcode1 First opcode byte (usually 0x0F for two-byte opcodes)
 * @param opcode2 Second opcode byte (e.g., 0x2F for comiss)
 * @param xmm_dst Destination XMM register
 * @param xmm_src Source XMM register
 * @return An OpCodeWithSize struct containing the instruction bytes
 */
inline OpCodeWithSize generateSSEInstructionNoPrefix(uint8_t opcode1, uint8_t opcode2, 
                                                      X64Register xmm_dst, X64Register xmm_src) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* ptr = result.op_codes.data();
	
	uint8_t dst_index = xmm_modrm_bits(xmm_dst);
	uint8_t src_index = xmm_modrm_bits(xmm_src);
	
	bool needs_rex = (dst_index >= 8) || (src_index >= 8);
	
	// REX prefix must come before opcode for SSE instructions without mandatory prefix
	if (needs_rex) {
		uint8_t rex = 0x40;  // Base REX prefix
		if (dst_index >= 8) {
			rex |= 0x04;  // REX.R - extends ModR/M reg field
		}
		if (src_index >= 8) {
			rex |= 0x01;  // REX.B - extends ModR/M r/m field
		}
		*ptr++ = rex;
		result.size_in_bytes++;
	}
	
	// Emit opcode bytes
	*ptr++ = opcode1;
	result.size_in_bytes++;
	*ptr++ = opcode2;
	result.size_in_bytes++;
	
	// ModR/M byte: 11 reg r/m (register-to-register mode)
	uint8_t modrm = 0xC0 + ((dst_index & 0x07) << 3) + (src_index & 0x07);
	*ptr++ = modrm;
	result.size_in_bytes++;
	
	return result;
}

/**
 * @brief Generates a properly encoded double-precision SSE instruction with 0x66 prefix.
 * 
 * This function handles double-precision scalar instructions like comisd, ucomisd that use
 * the 0x66 operand-size override prefix. The format is: 66 [REX] 0F opcode ModR/M
 * 
 * @param opcode1 First opcode byte (usually 0x0F for two-byte opcodes)
 * @param opcode2 Second opcode byte (e.g., 0x2F for comisd)
 * @param xmm_dst Destination XMM register
 * @param xmm_src Source XMM register
 * @return An OpCodeWithSize struct containing the instruction bytes
 */
inline OpCodeWithSize generateSSEInstructionDouble(uint8_t opcode1, uint8_t opcode2, 
                                                    X64Register xmm_dst, X64Register xmm_src) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* ptr = result.op_codes.data();
	
	uint8_t dst_index = xmm_modrm_bits(xmm_dst);
	uint8_t src_index = xmm_modrm_bits(xmm_src);
	
	bool needs_rex = (dst_index >= 8) || (src_index >= 8);
	
	// 0x66 operand-size override prefix comes FIRST
	*ptr++ = 0x66;
	result.size_in_bytes++;
	
	// REX prefix comes AFTER 0x66 but BEFORE opcode
	if (needs_rex) {
		uint8_t rex = 0x40;  // Base REX prefix
		if (dst_index >= 8) {
			rex |= 0x04;  // REX.R - extends ModR/M reg field
		}
		if (src_index >= 8) {
			rex |= 0x01;  // REX.B - extends ModR/M r/m field
		}
		*ptr++ = rex;
		result.size_in_bytes++;
	}
	
	// Emit opcode bytes
	*ptr++ = opcode1;
	result.size_in_bytes++;
	*ptr++ = opcode2;
	result.size_in_bytes++;
	
	// ModR/M byte: 11 reg r/m (register-to-register mode)
	uint8_t modrm = 0xC0 + ((dst_index & 0x07) << 3) + (src_index & 0x07);
	*ptr++ = modrm;
	result.size_in_bytes++;
	
	return result;
}

/**
 * @brief Generates x86-64 binary opcodes for 'mov destination_register, [rbp + offset]'.
 *
 * This function creates the byte sequence for moving a 64-bit pointer value from a
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
OpCodeWithSize generatePtrMovFromFrame(X64Register destinationRegister, int32_t offset) {
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
 * @brief Generates MOVSX r64, byte ptr [rbp + offset].
 *
 * Loads an 8-bit value from the stack (source_size=8) and sign-extends to 64-bit register.
 * Internal function - prefer using emitMovFromFrameSized() for clarity.
 *
 * @param destinationRegister The destination 64-bit register.
 * @param offset The signed byte offset from RBP (stack slot location).
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovsxFromFrame_8to64(X64Register destinationRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// REX.W prefix for 64-bit sign extension
	uint8_t rex_prefix = 0x48; // REX.W
	if (static_cast<uint8_t>(destinationRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= 0x04; // REX.R for R8-R15
	}
	*current_byte_ptr++ = rex_prefix;
	result.size_in_bytes++;

	// Opcode: 0F BE for MOVSX r64, r/m8
	*current_byte_ptr++ = 0x0F;
	*current_byte_ptr++ = 0xBE;
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
 * @brief Generates MOVSX r64, word ptr [rbp + offset].
 *
 * Loads a 16-bit value from the stack (source_size=16) and sign-extends to 64-bit register.
 * Internal function - prefer using emitMovFromFrameSized() for clarity.
 *
 * @param destinationRegister The destination 64-bit register.
 * @param offset The signed byte offset from RBP (stack slot location).
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovsxFromFrame_16to64(X64Register destinationRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// REX.W prefix for 64-bit sign extension
	uint8_t rex_prefix = 0x48; // REX.W
	if (static_cast<uint8_t>(destinationRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= 0x04; // REX.R for R8-R15
	}
	*current_byte_ptr++ = rex_prefix;
	result.size_in_bytes++;

	// Opcode: 0F BF for MOVSX r64, r/m16
	*current_byte_ptr++ = 0x0F;
	*current_byte_ptr++ = 0xBF;
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
 * @brief Generates MOVSXD r64, dword ptr [rbp + offset].
 *
 * Loads a 32-bit value from the stack (source_size=32) and sign-extends to 64-bit register.
 * Internal function - prefer using emitMovFromFrameSized() for clarity.
 *
 * @param destinationRegister The destination 64-bit register.
 * @param offset The signed byte offset from RBP (stack slot location).
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovsxdFromFrame_32to64(X64Register destinationRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// REX.W prefix for 64-bit sign extension
	uint8_t rex_prefix = 0x48; // REX.W
	if (static_cast<uint8_t>(destinationRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= 0x04; // REX.R for R8-R15
	}
	*current_byte_ptr++ = rex_prefix;
	result.size_in_bytes++;

	// Opcode: 63 for MOVSXD r64, r/m32
	*current_byte_ptr++ = 0x63;
	result.size_in_bytes++;

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
 * @brief Helper function to generate MOV from frame based on operand size.
 *
 * Selects between 8, 16, 32-bit and 64-bit MOV based on size_in_bits parameter.
 *
 * @param destinationRegister The destination register (RAX, RCX, RDX, etc.).
 * @param offset The signed offset from RBP.
 * @param size_in_bits The size of the value in bits.
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
 OpCodeWithSize generateMovFromFrameBySize(X64Register destinationRegister, int32_t offset, int size_in_bits) {
	if (size_in_bits == 8) {
		return generateMovzxFromFrame8(destinationRegister, offset);
	} else if (size_in_bits == 16) {
		return generateMovzxFromFrame16(destinationRegister, offset);
	} else if (size_in_bits == 32) {
		return generateMovFromFrame32(destinationRegister, offset);
	} else {
		return generatePtrMovFromFrame(destinationRegister, offset);
	}
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
 * @param destinationRegister The destination XMM register (XMM0-XMM15).
 * @param offset The signed byte offset from RBP.
 * @param is_float True for movss (float), false for movsd (double).
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
/**
 * @brief Generates x86-64 binary opcodes for 'movss/movsd xmm, [base_reg + offset]'.
 *
 * This function creates the byte sequence for loading a float/double value from
 * a memory address ([base_reg + offset]) into an XMM register.
 *
 * @param destinationRegister The destination XMM register (XMM0-XMM15).
 * @param base_reg The base address register (any GP register, typically a pointer).
 * @param offset The signed byte offset from base_reg.
 * @param is_float True for movss (float), false for movsd (double).
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateFloatMovFromMemory(X64Register destinationRegister, X64Register base_reg, int32_t offset, bool is_float) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// Prefix: F3 for movss (float), F2 for movsd (double)
	*current_byte_ptr++ = is_float ? 0xF3 : 0xF2;
	result.size_in_bytes++;

	// Check if we need REX prefix
	uint8_t xmm_reg = xmm_modrm_bits(destinationRegister);
	uint8_t base_bits = static_cast<uint8_t>(base_reg) & 0x07;
	bool need_rex = (xmm_reg >= 8) || (static_cast<uint8_t>(base_reg) >= 8);
	
	if (need_rex) {
		uint8_t rex = 0x40;
		if (xmm_reg >= 8) rex |= 0x04;  // REX.R for XMM8-15
		if (static_cast<uint8_t>(base_reg) >= 8) rex |= 0x01;  // REX.B for R8-R15
		*current_byte_ptr++ = rex;
		result.size_in_bytes++;
	}

	// Opcode: 0F 10 for movss/movsd xmm, [mem]
	*current_byte_ptr++ = 0x0F;
	*current_byte_ptr++ = 0x10;
	result.size_in_bytes += 2;

	// ModR/M byte - encode [base_reg + offset]
	if (offset == 0 && base_reg != X64Register::RBP && base_reg != X64Register::R13) {
		// Mod=00, no displacement (except for RBP/R13 which need at least disp8)
		uint8_t modrm = 0x00 | ((xmm_reg & 0x07) << 3) | base_bits;
		*current_byte_ptr++ = modrm;
		result.size_in_bytes++;
	} else if (offset >= -128 && offset <= 127) {
		// 8-bit displacement
		uint8_t modrm = 0x40 | ((xmm_reg & 0x07) << 3) | base_bits;  // Mod=01
		*current_byte_ptr++ = modrm;
		*current_byte_ptr++ = static_cast<uint8_t>(offset);
		result.size_in_bytes += 2;
	} else {
		// 32-bit displacement
		uint8_t modrm = 0x80 | ((xmm_reg & 0x07) << 3) | base_bits;  // Mod=10
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

OpCodeWithSize generateFloatMovFromFrame(X64Register destinationRegister, int32_t offset, bool is_float) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// Prefix: F3 for movss (float), F2 for movsd (double)
	*current_byte_ptr++ = is_float ? 0xF3 : 0xF2;
	result.size_in_bytes++;

	// Check if we need REX prefix for XMM8-XMM15
	uint8_t xmm_reg = xmm_modrm_bits(destinationRegister);
	if (xmm_reg >= 8) {
		// REX.R - extends ModR/M reg field for XMM8-XMM15
		*current_byte_ptr++ = 0x44;  // REX.R
		result.size_in_bytes++;
	}

	// Opcode: 0F 10 for movss/movsd xmm, [mem]
	*current_byte_ptr++ = 0x0F;
	*current_byte_ptr++ = 0x10;
	result.size_in_bytes += 2;

	// ModR/M byte - use only low 3 bits of xmm register
	if (offset >= -128 && offset <= 127) {
		// 8-bit displacement
		uint8_t modrm = 0x45 | ((xmm_reg & 0x07) << 3); // Mod=01, Reg=XMM, R/M=101 (RBP)
		*current_byte_ptr++ = modrm;
		*current_byte_ptr++ = static_cast<uint8_t>(offset);
		result.size_in_bytes += 2;
	} else {
		// 32-bit displacement
		uint8_t modrm = 0x85 | ((xmm_reg & 0x07) << 3); // Mod=10, Reg=XMM, R/M=101 (RBP)
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

	// Check if we need REX prefix for XMM8-XMM15
	uint8_t xmm_reg = xmm_modrm_bits(sourceRegister);
	if (xmm_reg >= 8) {
		// REX.R - extends ModR/M reg field for XMM8-XMM15
		*current_byte_ptr++ = 0x44;  // REX.R
		result.size_in_bytes++;
	}

	// Opcode: 0F 11 for movss/movsd [mem], xmm - store variant
	*current_byte_ptr++ = 0x0F;
	*current_byte_ptr++ = 0x11;
	result.size_in_bytes += 2;

	// ModR/M byte - use only low 3 bits of xmm register
	if (offset >= -128 && offset <= 127) {
		// 8-bit displacement
		uint8_t modrm = 0x45 | ((xmm_reg & 0x07) << 3); // Mod=01, Reg=XMM, R/M=101 (RBP)
		*current_byte_ptr++ = modrm;
		*current_byte_ptr++ = static_cast<uint8_t>(offset);
		result.size_in_bytes += 2;
	} else {
		// 32-bit displacement
		uint8_t modrm = 0x85 | ((xmm_reg & 0x07) << 3); // Mod=10, Reg=XMM, R/M=101 (RBP)
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
 * @brief Generates x86-64 binary opcodes for 'movss/movsd [ptr_reg], xmm'.
 *
 * This function creates the byte sequence for storing a float/double value from
 * an XMM register to memory pointed to by a general-purpose register (indirect addressing).
 *
 * @param sourceRegister The source XMM register (XMM0-XMM15).
 * @param ptr_reg The pointer register containing the memory address.
 * @param is_float True for movss (float), false for movsd (double).
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateFloatMovToMemory(X64Register sourceRegister, X64Register ptr_reg, bool is_float) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// Prefix: F3 for movss (float), F2 for movsd (double)
	*current_byte_ptr++ = is_float ? 0xF3 : 0xF2;
	result.size_in_bytes++;

	// Check if we need REX prefix
	uint8_t xmm_reg = xmm_modrm_bits(sourceRegister);
	uint8_t ptr_reg_bits = static_cast<uint8_t>(ptr_reg) & 0x07;
	bool need_rex = (xmm_reg >= 8) || (static_cast<uint8_t>(ptr_reg) >= 8);
	
	if (need_rex) {
		uint8_t rex = 0x40;
		if (xmm_reg >= 8) rex |= 0x04;  // REX.R for XMM8-15
		if (static_cast<uint8_t>(ptr_reg) >= 8) rex |= 0x01;  // REX.B for R8-R15
		*current_byte_ptr++ = rex;
		result.size_in_bytes++;
	}

	// Opcode: 0F 11 for movss/movsd [mem], xmm
	*current_byte_ptr++ = 0x0F;
	*current_byte_ptr++ = 0x11;
	result.size_in_bytes += 2;

	// ModR/M byte: 00 xmm ptr_reg (indirect addressing, no displacement)
	uint8_t modrm = 0x00 | ((xmm_reg & 0x07) << 3) | ptr_reg_bits;
	*current_byte_ptr++ = modrm;
	result.size_in_bytes++;

	return result;
}

/**
 * @brief Generates x86-64 binary opcodes for 'mov [rbp + offset], source_register'.
 *
 * This function creates the byte sequence for moving a 64-bit pointer value from a
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
OpCodeWithSize generatePtrMovToFrame(X64Register sourceRegister, int32_t offset) {
	// Assert that this is a general-purpose register, not an XMM register
	assert(static_cast<uint8_t>(sourceRegister) < 16 && 
	       "generatePtrMovToFrame called with XMM register - use generateFloatMovToFrame instead");
	
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
	// Assert that this is a general-purpose register, not an XMM register
	assert(static_cast<uint8_t>(sourceRegister) < 16 && 
	       "generateMovToFrame32 called with XMM register - use generateFloatMovToFrame instead");
	
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

/**
 * @brief Generates x86-64 binary opcodes for 'mov byte ptr [rbp + offset], r8'.
 *
 * Store an 8-bit value to RBP-relative address.
 *
 * @param sourceRegister The source register (uses lower 8 bits).
 * @param offset The signed offset from RBP.
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovToFrame8(X64Register sourceRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// REX prefix needed for R8-R15, or to access low byte (instead of high byte) of RSP, RBP, RSI, RDI
	bool needs_rex = static_cast<uint8_t>(sourceRegister) >= static_cast<uint8_t>(X64Register::R8) ||
	                 sourceRegister == X64Register::RSP || sourceRegister == X64Register::RBP ||
	                 sourceRegister == X64Register::RSI || sourceRegister == X64Register::RDI;
	
	if (needs_rex) {
		uint8_t rex_prefix = 0x40; // Base REX
		if (static_cast<uint8_t>(sourceRegister) >= static_cast<uint8_t>(X64Register::R8)) {
			rex_prefix |= (1 << 2); // Set R bit for R8-R15
		}
		*current_byte_ptr++ = rex_prefix;
		result.size_in_bytes++;
	}

	// Opcode: MOV r/m8, r8
	*current_byte_ptr++ = 0x88;
	result.size_in_bytes++;

	// ModR/M byte
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(sourceRegister) & 0x07;
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
 * @brief Generates x86-64 binary opcodes for 'mov word ptr [rbp + offset], r16'.
 *
 * Store a 16-bit value to RBP-relative address.
 *
 * @param sourceRegister The source register (uses lower 16 bits).
 * @param offset The signed offset from RBP.
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovToFrame16(X64Register sourceRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* current_byte_ptr = result.op_codes.data();

	// 16-bit operand size prefix
	*current_byte_ptr++ = 0x66;
	result.size_in_bytes++;

	// REX prefix may be needed for R8-R15
	bool needs_rex = static_cast<uint8_t>(sourceRegister) >= static_cast<uint8_t>(X64Register::R8);
	
	if (needs_rex) {
		uint8_t rex_prefix = 0x40; // Base REX
		rex_prefix |= (1 << 2); // Set R bit for R8-R15
		*current_byte_ptr++ = rex_prefix;
		result.size_in_bytes++;
	}

	// Opcode: MOV r/m16, r16
	*current_byte_ptr++ = 0x89;
	result.size_in_bytes++;

	// ModR/M byte
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(sourceRegister) & 0x07;
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
 * @brief Helper function to generate MOV to frame based on operand size.
 *
 * Selects between 32-bit and 64-bit MOV based on size_in_bits parameter.
 * Uses generateMovToFrame32 for sizes <= 32 bits, otherwise generatePtrMovToFrame.
 *
 * @param sourceRegister The source register (RAX, RCX, RDX, etc.).
 * @param offset The signed offset from RBP.
 * @param size_in_bits The size of the value in bits.
 * @return OpCodeWithSize containing the generated opcodes and their size.
 */
OpCodeWithSize generateMovToFrameBySize(X64Register sourceRegister, int32_t offset, int size_in_bits) {
	if (size_in_bits == 8) {
		return generateMovToFrame8(sourceRegister, offset);
	} else if (size_in_bits == 16) {
		return generateMovToFrame16(sourceRegister, offset);
	} else if (size_in_bits == 32) {
		return generateMovToFrame32(sourceRegister, offset);
	} else {
		return generatePtrMovToFrame(sourceRegister, offset);
	}
}

/**
 * @brief Emits ADD reg, imm32 instruction (64-bit register with 32-bit immediate) directly to textSectionData.
 *
 * Emits: REX.W + 81 /0 id (ADD r64, imm32)
 *
 * @param textSectionData The output buffer for opcodes.
 * @param reg The register to add to.
 * @param immediate The 32-bit signed immediate value.
 */
inline void emitAddRegImm32(std::vector<char>& textSectionData, X64Register reg, int32_t immediate) {
	// REX.W prefix, with REX.B if register is R8-R15
	uint8_t rex = 0x48; // REX.W
	if (static_cast<uint8_t>(reg) >= 8) {
		rex |= 0x01; // REX.B
	}
	textSectionData.push_back(rex);
	
	// Opcode: 81 /0 (ADD r/m64, imm32)
	textSectionData.push_back(0x81);
	
	// ModR/M: 11 (mod=direct register) | 000 (opcode extension /0) | reg (r/m)
	uint8_t modrm = 0xC0 | (static_cast<uint8_t>(reg) & 0x7);
	textSectionData.push_back(modrm);
	
	// 32-bit immediate (little-endian)
	textSectionData.push_back(static_cast<uint8_t>(immediate & 0xFF));
	textSectionData.push_back(static_cast<uint8_t>((immediate >> 8) & 0xFF));
	textSectionData.push_back(static_cast<uint8_t>((immediate >> 16) & 0xFF));
	textSectionData.push_back(static_cast<uint8_t>((immediate >> 24) & 0xFF));
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

// ============================================================================
// Platform-Specific Calling Conventions
// ============================================================================
// Windows x64 (Win64 ABI): Uses Microsoft x64 calling convention
//   - Integer/pointer args: RCX, RDX, R8, R9 (4 registers)
//   - Float args: XMM0-XMM3 (4 registers)
//   - Shadow space: 32 bytes (4 * 8) for spilling register parameters
//   - Stack alignment: 16 bytes at call instruction
//
// Linux x86-64 (System V AMD64 ABI): Uses System V calling convention
//   - Integer/pointer args: RDI, RSI, RDX, RCX, R8, R9 (6 registers)
//   - Float args: XMM0-XMM7 (8 registers)
//   - No shadow space required
//   - Stack alignment: 16 bytes at call instruction
//   - Red zone: 128 bytes below RSP that won't be clobbered by signals/interrupts
// ============================================================================

// Win64 calling convention register mapping (Windows)
static constexpr std::array<X64Register, 4> WIN64_INT_PARAM_REGS = {
	X64Register::RCX,  // First integer/pointer argument
	X64Register::RDX,  // Second integer/pointer argument
	X64Register::R8,   // Third integer/pointer argument
	X64Register::R9    // Fourth integer/pointer argument
};

static constexpr std::array<X64Register, 4> WIN64_FLOAT_PARAM_REGS = {
	X64Register::XMM0, // First floating point argument
	X64Register::XMM1, // Second floating point argument
	X64Register::XMM2, // Third floating point argument
	X64Register::XMM3  // Fourth floating point argument
};

// System V AMD64 calling convention register mapping (Linux/Unix)
static constexpr std::array<X64Register, 6> SYSV_INT_PARAM_REGS = {
	X64Register::RDI,  // First integer/pointer argument
	X64Register::RSI,  // Second integer/pointer argument
	X64Register::RDX,  // Third integer/pointer argument
	X64Register::RCX,  // Fourth integer/pointer argument
	X64Register::R8,   // Fifth integer/pointer argument
	X64Register::R9    // Sixth integer/pointer argument
};

static constexpr std::array<X64Register, 8> SYSV_FLOAT_PARAM_REGS = {
	X64Register::XMM0, // First floating point argument
	X64Register::XMM1, // Second floating point argument
	X64Register::XMM2, // Third floating point argument
	X64Register::XMM3, // Fourth floating point argument
	X64Register::XMM4, // Fifth floating point argument
	X64Register::XMM5, // Sixth floating point argument
	X64Register::XMM6, // Seventh floating point argument
	X64Register::XMM7  // Eighth floating point argument
};

// ============================================================================
// Platform-Specific ABI Helper Functions
// ============================================================================
// These template functions select the correct parameter registers based on 
// whether we're targeting Windows (COFF/PE) or Linux (ELF).
// ============================================================================

// Get the integer parameter register for the given index based on platform
template<typename TWriterClass>
constexpr X64Register getIntParamReg(size_t index) {
	if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
		// Linux: System V AMD64 ABI (6 integer parameter registers)
		return (index < SYSV_INT_PARAM_REGS.size()) ? SYSV_INT_PARAM_REGS[index] : X64Register::Count;
	} else {
		// Windows: Win64 ABI (4 integer parameter registers)
		return (index < WIN64_INT_PARAM_REGS.size()) ? WIN64_INT_PARAM_REGS[index] : X64Register::Count;
	}
}

// Get the float parameter register for the given index based on platform
template<typename TWriterClass>
constexpr X64Register getFloatParamReg(size_t index) {
	if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
		// Linux: System V AMD64 ABI (8 float parameter registers)
		return (index < SYSV_FLOAT_PARAM_REGS.size()) ? SYSV_FLOAT_PARAM_REGS[index] : X64Register::Count;
	} else {
		// Windows: Win64 ABI (4 float parameter registers)
		return (index < WIN64_FLOAT_PARAM_REGS.size()) ? WIN64_FLOAT_PARAM_REGS[index] : X64Register::Count;
	}
}

// Get the maximum number of integer parameter registers based on platform
template<typename TWriterClass>
constexpr size_t getMaxIntParamRegs() {
	if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
		return SYSV_INT_PARAM_REGS.size();  // 6 for Linux
	} else {
		return WIN64_INT_PARAM_REGS.size(); // 4 for Windows
	}
}

// Get the maximum number of float parameter registers based on platform
template<typename TWriterClass>
constexpr size_t getMaxFloatParamRegs() {
	if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
		return SYSV_FLOAT_PARAM_REGS.size();  // 8 for Linux
	} else {
		return WIN64_FLOAT_PARAM_REGS.size(); // 4 for Windows
	}
}

// Get the shadow space size based on platform
template<typename TWriterClass>
constexpr size_t getShadowSpaceSize() {
	if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
		return 0;   // Linux: No shadow space
	} else {
		return 32;  // Windows: 32 bytes (4 * 8) for spilling register parameters
	}
}

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
	// TempVar with var_number N is at offset -(N * 8)
	// For example: var_number=1 → offset=-8, var_number=2 → offset=-16, var_number=3 → offset=-24, etc.
	if (stackVariableOffset < 0 && (stackVariableOffset % 8) == 0) {
		size_t var_number = static_cast<size_t>(-stackVariableOffset / 8);
		return TempVar(var_number);
	}

	return std::nullopt;
}


struct RegisterAllocator
{
	static constexpr uint8_t REGISTER_COUNT = static_cast<uint8_t>(X64Register::Count);
	struct AllocatedRegister {
		X64Register reg = X64Register::Count;
		bool isAllocated = false;
		bool isDirty = false;	// Does the stack variable need to be updated on a flush
		int32_t stackVariableOffset = INT_MIN;
		int size_in_bits = 0;	// Size of the value stored in this register (for proper spilling)
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
				func(reg.reg, reg.stackVariableOffset, reg.size_in_bits);
				reg.isDirty = false;
				// Clear the stack variable mapping after flushing to prevent stale register lookups.
				// This ensures that subsequent code will reload from memory rather than using
				// a potentially stale register value. INT_MIN is the sentinel value (see AllocatedRegister init).
				reg.stackVariableOffset = INT_MIN;
			}
		}
	}

	void flushSingleDirtyRegister(X64Register reg) {
		assert(reg != X64Register::Count);
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
		return findRegisterToSpill(X64Register::Count);
	}

	// Find a register to spill, excluding a specific register
	std::optional<X64Register> findRegisterToSpill(X64Register exclude) {
		// Single pass: prefer non-dirty registers, but accept dirty ones if needed
		X64Register best_candidate = X64Register::Count;
		bool found_dirty = false;

		// Iterate only over general-purpose registers (RAX to R15)
		for (size_t i = static_cast<size_t>(X64Register::RAX); i <= static_cast<size_t>(X64Register::R15); ++i) {
			if (registers[i].isAllocated &&
			    registers[i].reg != X64Register::RSP &&
			    registers[i].reg != X64Register::RBP &&
			    registers[i].reg != exclude) {

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
		assert(reg != X64Register::Count);
		assert(!registers[static_cast<int>(reg)].isAllocated);
		registers[static_cast<int>(reg)].isAllocated = true;
		registers[static_cast<int>(reg)].stackVariableOffset = stackVariableOffset;
	}

	void release(X64Register reg) {
		assert(reg != X64Register::Count);
		registers[static_cast<int>(reg)] = AllocatedRegister{ .reg = reg };
	}

	bool is_allocated(X64Register reg) const {
		return registers[static_cast<size_t>(reg)].isAllocated;
	}

	void mark_reg_dirty(X64Register reg) {
		assert(reg != X64Register::Count);
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

	void set_stack_variable_offset(X64Register reg, int32_t stackVariableOffset, int size_in_bits = 64) {
		assert(reg != X64Register::Count);
		assert(registers[static_cast<int>(reg)].isAllocated);
		// Clear any other registers that think they hold this stack variable
		for (auto& r : registers) {
			if (r.stackVariableOffset == stackVariableOffset && r.reg != reg) {
				r.stackVariableOffset = INT_MIN; // Clear the mapping
				r.isDirty = false;
			}
		}
		registers[static_cast<int>(reg)].stackVariableOffset = stackVariableOffset;
		registers[static_cast<int>(reg)].size_in_bits = size_in_bits;
		registers[static_cast<int>(reg)].isDirty = true;
	}

	// Clear all register associations for a specific stack offset
	// Use this when storing to a stack slot to invalidate any cached register values
	void clearStackVariableAssociations(int32_t stackVariableOffset) {
		for (auto& r : registers) {
			if (r.stackVariableOffset == stackVariableOffset) {
				r.stackVariableOffset = INT_MIN;
				r.isDirty = false;
			}
		}
	}

	OpCodeWithSize get_reg_reg_move_op_code(X64Register dst_reg, X64Register src_reg, size_t size_in_bytes) {
		OpCodeWithSize result;
		/*if (dst_reg == src_reg) {	// removed for now, since this is an optimization
			return result;
		}*/

		// Handle invalid size or cross-type register moves (e.g., XMM to GP with size 0)
		if (size_in_bytes < 1 || size_in_bytes > 8) {
			// Return empty opcode for invalid moves
			return result;
		}

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
			// Check if we need REX prefix:
			// 1. Extended registers (R8-R15)
			// 2. To access SIL, DIL, BPL, SPL instead of AH, CH, DH, BH (RSI=6, RDI=7, RBP=5, RSP=4)
			bool needs_rex = (static_cast<uint8_t>(src_reg) >= 4 && static_cast<uint8_t>(src_reg) <= 7) ||
			                 (static_cast<uint8_t>(dst_reg) >= 4 && static_cast<uint8_t>(dst_reg) <= 7) ||
			                 static_cast<uint8_t>(src_reg) >= 8 || 
			                 static_cast<uint8_t>(dst_reg) >= 8;
			
			if (needs_rex) {
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

	// Invalidate all caller-saved registers after a function call
	// According to x64 calling convention, RAX, RCX, RDX, R8, R9, R10, R11 and XMM0-XMM15 are volatile
	void invalidateCallerSavedRegisters() {
		// Clear general-purpose caller-saved registers
		const X64Register caller_saved_gpr[] = {
			X64Register::RAX, X64Register::RCX, X64Register::RDX,
			X64Register::R8, X64Register::R9, X64Register::R10, X64Register::R11
		};
		for (auto reg : caller_saved_gpr) {
			int idx = static_cast<int>(reg);
			// Don't release if not allocated, but clear the stack variable mapping
			if (registers[idx].isAllocated) {
				registers[idx].stackVariableOffset = INT_MIN;
				registers[idx].isDirty = false;
			}
		}
		
		// Clear all XMM registers (all are caller-saved)
		for (size_t i = static_cast<size_t>(X64Register::XMM0); i <= static_cast<size_t>(X64Register::XMM15); ++i) {
			if (registers[i].isAllocated) {
				registers[i].stackVariableOffset = INT_MIN;
				registers[i].isDirty = false;
			}
		}
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
 * @brief Emits load from address in a register with size-appropriate instruction.
 * Loads from [addr_reg] into dest_reg with proper zero/sign extension.
 *
 * @param textSectionData The vector to append opcodes to
 * @param dest_reg The destination register for the loaded value
 * @param addr_reg The register containing the memory address to load from
 * @param element_size_bytes The size of the element to load (1, 2, 4, or 8)
 */
inline void emitLoadFromAddressInReg(std::vector<char>& textSectionData, X64Register dest_reg, X64Register addr_reg, int element_size_bytes) {
	uint8_t dest_bits = static_cast<uint8_t>(dest_reg) & 0x07;
	uint8_t addr_bits = static_cast<uint8_t>(addr_reg) & 0x07;
	bool dest_extended = static_cast<uint8_t>(dest_reg) >= static_cast<uint8_t>(X64Register::R8);
	bool addr_extended = static_cast<uint8_t>(addr_reg) >= static_cast<uint8_t>(X64Register::R8);
	
	// x86-64 ModR/M encoding quirks (Intel SDM Vol 2A, Table 2-2):
	// - r/m=100 (RSP/R12) in mod=00 requires SIB byte to specify base register
	// - r/m=101 (RBP/R13) in mod=00 means disp32[RIP], not [RBP/R13]
	// To encode [RBP/R13], use mod=01 with disp8=0
	bool needs_sib = (addr_bits == 4);   // RSP or R12
	bool needs_disp0 = (addr_bits == 5); // RBP or R13
	
	// ModR/M byte
	// For normal indirect: mod=00, reg=dest_bits, r/m=addr_bits
	// For RBP/R13 with disp8=0: mod=01, reg=dest_bits, r/m=addr_bits
	uint8_t mod = needs_disp0 ? 0x40 : 0x00;  // 0x40 = mod=01
	uint8_t modrm = mod | (dest_bits << 3) | addr_bits;
	// SIB byte for RSP/R12: scale=00, index=100 (none), base=100 (RSP or R12)
	uint8_t sib = 0x24;  // scale=00, index=100, base=100
	
	if (element_size_bytes == 1) {
		// MOVZX dest32, BYTE PTR [addr_reg]
		uint8_t rex = 0x40;
		if (dest_extended) rex |= 0x04; // R bit
		if (addr_extended) rex |= 0x01; // B bit
		if (rex != 0x40) textSectionData.push_back(rex);
		textSectionData.push_back(0x0F);
		textSectionData.push_back(0xB6); // MOVZX r32, r/m8
		textSectionData.push_back(modrm);
		if (needs_sib) textSectionData.push_back(sib);
		if (needs_disp0) textSectionData.push_back(0x00);  // disp8 = 0
	} else if (element_size_bytes == 2) {
		// MOVZX dest32, WORD PTR [addr_reg]
		uint8_t rex = 0x40;
		if (dest_extended) rex |= 0x04;
		if (addr_extended) rex |= 0x01;
		if (rex != 0x40) textSectionData.push_back(rex);
		textSectionData.push_back(0x0F);
		textSectionData.push_back(0xB7); // MOVZX r32, r/m16
		textSectionData.push_back(modrm);
		if (needs_sib) textSectionData.push_back(sib);
		if (needs_disp0) textSectionData.push_back(0x00);  // disp8 = 0
	} else if (element_size_bytes == 4) {
		// MOV dest32, DWORD PTR [addr_reg]
		uint8_t rex = 0x40;
		if (dest_extended) rex |= 0x04;
		if (addr_extended) rex |= 0x01;
		if (rex != 0x40) textSectionData.push_back(rex);
		textSectionData.push_back(0x8B); // MOV r32, r/m32
		textSectionData.push_back(modrm);
		if (needs_sib) textSectionData.push_back(sib);
		if (needs_disp0) textSectionData.push_back(0x00);  // disp8 = 0
	} else {
		// MOV dest64, QWORD PTR [addr_reg]
		uint8_t rex = 0x48; // REX.W
		if (dest_extended) rex |= 0x04;
		if (addr_extended) rex |= 0x01;
		textSectionData.push_back(rex);
		textSectionData.push_back(0x8B); // MOV r64, r/m64
		textSectionData.push_back(modrm);
		if (needs_sib) textSectionData.push_back(sib);
		if (needs_disp0) textSectionData.push_back(0x00);  // disp8 = 0
	}
}

/**
 * @brief Emits floating-point load from address in a register.
 * Loads from [addr_reg] into xmm_dest using MOVSD (double) or MOVSS (float).
 *
 * @param textSectionData The vector to append opcodes to
 * @param xmm_dest The destination XMM register for the loaded value
 * @param addr_reg The general-purpose register containing the memory address to load from
 * @param is_float True for float (MOVSS), false for double (MOVSD)
 */
inline void emitFloatLoadFromAddressInReg(std::vector<char>& textSectionData, X64Register xmm_dest, X64Register addr_reg, bool is_float) {
	uint8_t xmm_bits = static_cast<uint8_t>(xmm_dest) & 0x07;
	uint8_t addr_bits = static_cast<uint8_t>(addr_reg) & 0x07;
	bool addr_extended = static_cast<uint8_t>(addr_reg) >= static_cast<uint8_t>(X64Register::R8);
	
	// MOVSD xmm, [addr_reg]: F2 0F 10 modrm
	// MOVSS xmm, [addr_reg]: F3 0F 10 modrm
	textSectionData.push_back(is_float ? 0xF3 : 0xF2);
	if (addr_extended) {
		textSectionData.push_back(0x41); // REX.B for extended addr_reg
	}
	textSectionData.push_back(0x0F);
	textSectionData.push_back(0x10); // MOVSD/MOVSS xmm, m (load variant)
	
	// ModR/M: mod=00 (indirect), reg=xmm_bits, r/m=addr_bits
	uint8_t modrm = (xmm_bits << 3) | addr_bits;
	textSectionData.push_back(modrm);
}

/**
 * @brief Emits floating-point load from address in register with displacement.
 * Loads from [addr_reg + offset] into xmm_dest using MOVSD (double) or MOVSS (float).
 *
 * @param textSectionData The vector to append opcodes to
 * @param xmm_dest The destination XMM register for the loaded value
 * @param addr_reg The general-purpose register containing the base memory address
 * @param offset The displacement to add to the base address
 * @param is_float True for float (MOVSS), false for double (MOVSD)
 */
inline void emitFloatLoadFromAddressWithOffset(std::vector<char>& textSectionData, X64Register xmm_dest, X64Register addr_reg, int32_t offset, bool is_float) {
	uint8_t xmm_bits = static_cast<uint8_t>(xmm_dest) & 0x07;
	uint8_t addr_bits = static_cast<uint8_t>(addr_reg) & 0x07;
	bool addr_extended = static_cast<uint8_t>(addr_reg) >= static_cast<uint8_t>(X64Register::R8);
	
	// MOVSD xmm, [addr_reg + disp]: F2 0F 10 modrm [disp]
	// MOVSS xmm, [addr_reg + disp]: F3 0F 10 modrm [disp]
	textSectionData.push_back(is_float ? 0xF3 : 0xF2);
	if (addr_extended) {
		textSectionData.push_back(0x41); // REX.B for extended addr_reg
	}
	textSectionData.push_back(0x0F);
	textSectionData.push_back(0x10); // MOVSD/MOVSS xmm, m (load variant)
	
	// ModR/M and displacement
	if (offset >= -128 && offset <= 127) {
		// Use disp8: mod=01
		uint8_t modrm = 0x40 | (xmm_bits << 3) | addr_bits;
		textSectionData.push_back(modrm);
		textSectionData.push_back(static_cast<uint8_t>(offset));
	} else {
		// Use disp32: mod=10
		uint8_t modrm = 0x80 | (xmm_bits << 3) | addr_bits;
		textSectionData.push_back(modrm);
		textSectionData.push_back((offset >> 0) & 0xFF);
		textSectionData.push_back((offset >> 8) & 0xFF);
		textSectionData.push_back((offset >> 16) & 0xFF);
		textSectionData.push_back((offset >> 24) & 0xFF);
	}
}

/**
 * @brief Emits floating-point store to address in register with displacement.
 * Stores from xmm_src to [addr_reg + offset] using MOVSD (double) or MOVSS (float).
 *
 * @param textSectionData The vector to append opcodes to
 * @param xmm_src The source XMM register containing the value to store
 * @param addr_reg The general-purpose register containing the base memory address
 * @param offset The displacement to add to the base address
 * @param is_float True for float (MOVSS), false for double (MOVSD)
 */
inline void emitFloatStoreToAddressWithOffset(std::vector<char>& textSectionData, X64Register xmm_src, X64Register addr_reg, int32_t offset, bool is_float) {
	uint8_t xmm_bits = static_cast<uint8_t>(xmm_src) & 0x07;
	uint8_t addr_bits = static_cast<uint8_t>(addr_reg) & 0x07;
	bool addr_extended = static_cast<uint8_t>(addr_reg) >= static_cast<uint8_t>(X64Register::R8);
	
	// MOVSD [addr_reg + disp], xmm: F2 0F 11 modrm [disp]
	// MOVSS [addr_reg + disp], xmm: F3 0F 11 modrm [disp]
	textSectionData.push_back(is_float ? 0xF3 : 0xF2);
	if (addr_extended) {
		textSectionData.push_back(0x41); // REX.B for extended addr_reg
	}
	textSectionData.push_back(0x0F);
	textSectionData.push_back(0x11); // MOVSD/MOVSS m, xmm (store variant)
	
	// ModR/M and displacement
	if (offset >= -128 && offset <= 127) {
		// Use disp8: mod=01
		uint8_t modrm = 0x40 | (xmm_bits << 3) | addr_bits;
		textSectionData.push_back(modrm);
		textSectionData.push_back(static_cast<uint8_t>(offset));
	} else {
		// Use disp32: mod=10
		uint8_t modrm = 0x80 | (xmm_bits << 3) | addr_bits;
		textSectionData.push_back(modrm);
		textSectionData.push_back((offset >> 0) & 0xFF);
		textSectionData.push_back((offset >> 8) & 0xFF);
		textSectionData.push_back((offset >> 16) & 0xFF);
		textSectionData.push_back((offset >> 24) & 0xFF);
	}
}

/**
 * @brief Emits MOVQ to transfer data from XMM register to general-purpose register.
 * MOVQ gpr, xmm: 66 48 0F 7E /r
 *
 * @param textSectionData The vector to append opcodes to
 * @param xmm_src The source XMM register
 * @param gpr_dest The destination general-purpose register
 */
inline void emitMovqXmmToGpr(std::vector<char>& textSectionData, X64Register xmm_src, X64Register gpr_dest) {
	textSectionData.push_back(0x66);
	textSectionData.push_back(0x48);
	textSectionData.push_back(0x0F);
	textSectionData.push_back(0x7E);
	uint8_t xmm_bits = static_cast<uint8_t>(xmm_src) & 0x07;
	uint8_t gpr_bits = static_cast<uint8_t>(gpr_dest) & 0x07;
	textSectionData.push_back(0xC0 | (xmm_bits << 3) | gpr_bits);
}

/**
 * @brief Emits MOVQ to transfer data from general-purpose register to XMM register.
 * MOVQ xmm, gpr: 66 48 0F 6E /r
 *
 * @param textSectionData The vector to append opcodes to
 * @param gpr_src The source general-purpose register
 * @param xmm_dest The destination XMM register
 */
inline void emitMovqGprToXmm(std::vector<char>& textSectionData, X64Register gpr_src, X64Register xmm_dest) {
	textSectionData.push_back(0x66);
	textSectionData.push_back(0x48);
	textSectionData.push_back(0x0F);
	textSectionData.push_back(0x6E);
	uint8_t xmm_bits = static_cast<uint8_t>(xmm_dest) & 0x07;
	uint8_t gpr_bits = static_cast<uint8_t>(gpr_src) & 0x07;
	textSectionData.push_back(0xC0 | (xmm_bits << 3) | gpr_bits);
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
 * @brief Emits x64 opcodes to multiply a register by element_size_bytes.
 *
 * Optimizes for power-of-2 sizes using SHL (bit shift left):
 * - 1 byte: No operation needed (index already in bytes)
 * - 2 bytes: SHL reg, 1
 * - 4 bytes: SHL reg, 2
 * - 8 bytes: SHL reg, 3
 * - Other: IMUL reg, reg, imm32 (general multiplication)
 *
 * @param textSectionData The vector to append opcodes to
 * @param reg The register to multiply
 * @param element_size_bytes The size to multiply by (1, 2, 4, 8, or other)
 */
inline void emitMultiplyRegByElementSize(std::vector<char>& textSectionData, X64Register reg, int element_size_bytes) {
	if (element_size_bytes == 1) {
		return; // No multiplication needed
	}
	
	uint8_t reg_bits = static_cast<uint8_t>(reg) & 0x07;
	bool reg_extended = static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8);
	
	if (element_size_bytes == 2 || element_size_bytes == 4 || element_size_bytes == 8) {
		int shift_amount = (element_size_bytes == 2) ? 1 : (element_size_bytes == 4) ? 2 : 3;
		uint8_t rex = 0x48; // REX.W
		if (reg_extended) rex |= 0x01; // B bit
		textSectionData.push_back(rex);
		textSectionData.push_back(0xC1); // SHL r/m64, imm8
		textSectionData.push_back(0xE0 | reg_bits); // ModR/M: mod=11, reg=100 (SHL), r/m=reg
		textSectionData.push_back(static_cast<uint8_t>(shift_amount));
	} else {
		// IMUL reg, reg, imm32
		uint8_t rex = 0x48;
		if (reg_extended) rex |= 0x05; // R and B bits (same register for src and dest)
		textSectionData.push_back(rex);
		textSectionData.push_back(0x69); // IMUL r64, r/m64, imm32
		textSectionData.push_back(0xC0 | (reg_bits << 3) | reg_bits); // ModR/M: mod=11, reg, r/m
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
 * @brief Emits PUSH reg instruction (branchless).
 * @param textSectionData The vector to append opcodes to
 * @param reg The register to push
 */
inline void emitPush(std::vector<char>& textSectionData, X64Register reg) {
	uint8_t reg_bits = static_cast<uint8_t>(reg) & 0x07;
	int reg_extended = static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8);
	// Branchless: emit [REX.B, PUSH+reg] and skip REX.B if not extended
	char opcodes[2] = { 0x41, static_cast<char>(0x50 + reg_bits) };
	textSectionData.insert(textSectionData.end(), opcodes + !reg_extended, opcodes + 2);
}

/**
 * @brief Emits POP reg instruction (branchless).
 * @param textSectionData The vector to append opcodes to
 * @param reg The register to pop into
 */
inline void emitPop(std::vector<char>& textSectionData, X64Register reg) {
	uint8_t reg_bits = static_cast<uint8_t>(reg) & 0x07;
	int reg_extended = static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8);
	// Branchless: emit [REX.B, POP+reg] and skip REX.B if not extended
	char opcodes[2] = { 0x41, static_cast<char>(0x58 + reg_bits) };
	textSectionData.insert(textSectionData.end(), opcodes + !reg_extended, opcodes + 2);
}

/**
 * @brief Emits CALL r64 instruction (indirect call through register).
 * @param textSectionData The vector to append opcodes to
 * @param reg The register containing the function address
 * 
 * Encoding: FF /2 where /2 means reg field = 2 (call r/m64)
 * For RAX: FF D0 (ModR/M = 11 010 000)
 * For R8+: 41 FF D0+ (REX.B prefix needed)
 */
inline void emitCallReg(std::vector<char>& textSectionData, X64Register reg) {
	uint8_t reg_bits = static_cast<uint8_t>(reg) & 0x07;
	bool reg_extended = static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8);
	
	if (reg_extended) {
		textSectionData.push_back(0x41);  // REX.B prefix
	}
	textSectionData.push_back(static_cast<char>(0xFF));  // Opcode for CALL r/m64
	// ModR/M: mod=11 (register), reg=010 (/2), r/m=reg_bits
	textSectionData.push_back(static_cast<char>(0xD0 + reg_bits));
}

/**
 * @brief Emits x64 opcodes for ADD dest_reg, src_reg.
 *
 * @param textSectionData The vector to append opcodes to
 * @param dest_reg The destination register
 * @param src_reg The source register to add
 */
inline void emitAddRegs(std::vector<char>& textSectionData, X64Register dest_reg, X64Register src_reg) {
	uint8_t dest_bits = static_cast<uint8_t>(dest_reg) & 0x07;
	uint8_t src_bits = static_cast<uint8_t>(src_reg) & 0x07;
	bool dest_extended = static_cast<uint8_t>(dest_reg) >= static_cast<uint8_t>(X64Register::R8);
	bool src_extended = static_cast<uint8_t>(src_reg) >= static_cast<uint8_t>(X64Register::R8);
	
	// REX.W with branchless R and B bits
	uint8_t rex = 0x48 | (static_cast<uint8_t>(src_extended) << 2) | static_cast<uint8_t>(dest_extended);
	textSectionData.push_back(rex);
	textSectionData.push_back(0x01); // ADD r/m64, r64
	textSectionData.push_back(0xC0 | (src_bits << 3) | dest_bits); // ModR/M: mod=11, reg=src, r/m=dest
}

/**
 * @brief Emits x64 opcodes for ADD reg, imm32.
 *
 * @param textSectionData The vector to append opcodes to
 * @param reg The destination register
 * @param imm The immediate value to add (32-bit)
 */
inline void emitAddImmToReg(std::vector<char>& textSectionData, X64Register reg, int64_t imm) {
	if (imm == 0) return; // No-op
	
	uint8_t reg_bits = static_cast<uint8_t>(reg) & 0x07;
	uint8_t reg_extended = static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8);
	
	// REX.W with branchless B bit
	uint8_t rex = 0x48 | reg_extended;
	textSectionData.push_back(rex);
	
	// Branchless opcode selection using array
	// RAX: just 0x05 (1 byte), others: 0x81, 0xC0|reg_bits (2 bytes)
	bool is_rax = (reg == X64Register::RAX);
	// For RAX: opcodes = {0x05}, size=1
	// For others: opcodes = {0x81, 0xC0|reg_bits}, size=2
	char opcodes[2] = { static_cast<char>(0x81), static_cast<char>(0xC0 | reg_bits) };
	opcodes[0] = is_rax ? 0x05 : static_cast<char>(0x81);
	int opcode_size = 2 - is_rax; // 1 for RAX, 2 for others
	textSectionData.insert(textSectionData.end(), opcodes, opcodes + opcode_size);
	
	uint32_t imm_u32 = static_cast<uint32_t>(imm);
	textSectionData.push_back(imm_u32 & 0xFF);
	textSectionData.push_back((imm_u32 >> 8) & 0xFF);
	textSectionData.push_back((imm_u32 >> 16) & 0xFF);
	textSectionData.push_back((imm_u32 >> 24) & 0xFF);
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
inline void emitLoadIndexIntoRCX(std::vector<char>& textSectionData, int64_t offset, int size_in_bits) {
	// For 32-bit values, use MOV r32, r/m32 which zero-extends to 64 bits
	// For 64-bit values, use REX.W + MOV r64, r/m64
	if (size_in_bits == 32) {
		// 32-bit MOV (no REX.W needed, zero-extends automatically)
		textSectionData.push_back(0x8B); // MOV r32, r/m32
	} else {
		// 64-bit MOV (needs REX.W)
		textSectionData.push_back(0x48); // REX.W prefix for 64-bit operation
		textSectionData.push_back(0x8B); // MOV r64, r/m64
	}
	
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
 * @brief Emits x64 opcodes to load a value from stack into any register.
 *
 * Generates MOV reg, [RBP + offset] with optimal displacement encoding.
 * Supports 1, 2, 4, and 8 byte loads with appropriate zero/sign extension.
 * Uses branchless opcode emission for better pipeline performance.
 *
 * @param textSectionData The vector to append opcodes to
 * @param reg The destination register
 * @param offset The stack offset from RBP (negative for locals)
 * @param size_bytes The size of the value to load (1, 2, 4, or 8)
 */
inline void emitLoadFromFrame(std::vector<char>& textSectionData, X64Register reg, int64_t offset, int size_bytes = 8) {
	uint8_t reg_bits = static_cast<uint8_t>(reg) & 0x07;
	int reg_extended = static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8);
	
	// Branchless ModR/M + displacement emission
	auto emitModRM = [&]() {
		uint8_t modrm_base = (offset >= -128 && offset <= 127) ? 0x40 : 0x80;
		textSectionData.push_back(modrm_base | (reg_bits << 3) | 0x05);
		
		if (offset >= -128 && offset <= 127) {
			textSectionData.push_back(static_cast<uint8_t>(offset));
		} else {
			uint32_t offset_u32 = static_cast<uint32_t>(offset);
			textSectionData.push_back(offset_u32 & 0xFF);
			textSectionData.push_back((offset_u32 >> 8) & 0xFF);
			textSectionData.push_back((offset_u32 >> 16) & 0xFF);
			textSectionData.push_back((offset_u32 >> 24) & 0xFF);
		}
	};
	
	// Opcode tables for branchless emission (REX prefix, then opcode bytes)
	// Index 0 = with REX, Index 1 = without REX (we skip first byte)
	if (size_bytes == 8) {
		// MOV r64, [RBP + offset] - always needs REX.W, optionally REX.R
		uint8_t rex = 0x48 | (reg_extended << 2); // REX.W, optionally REX.R
		textSectionData.push_back(rex);
		textSectionData.push_back(static_cast<char>(0x8B));
		emitModRM();
	} else if (size_bytes == 4) {
		// MOV r32, [RBP + offset] - branchless REX emission
		static constexpr char mov32_ops[2] = { 0x44, static_cast<char>(0x8B) }; // REX.R, MOV r32
		textSectionData.insert(textSectionData.end(), mov32_ops + !reg_extended, mov32_ops + 2);
		emitModRM();
	} else if (size_bytes == 2) {
		// MOVZX r32, WORD PTR [RBP + offset] - branchless REX emission
		static constexpr char movzx16_ops[3] = { 0x44, 0x0F, static_cast<char>(0xB7) }; // REX.R, 0F, MOVZX r32/r/m16
		textSectionData.insert(textSectionData.end(), movzx16_ops + !reg_extended, movzx16_ops + 3);
		emitModRM();
	} else if (size_bytes == 1) {
		// MOVZX r32, BYTE PTR [RBP + offset] - branchless REX emission
		static constexpr char movzx8_ops[3] = { 0x44, 0x0F, static_cast<char>(0xB6) }; // REX.R, 0F, MOVZX r32/r/m8
		textSectionData.insert(textSectionData.end(), movzx8_ops + !reg_extended, movzx8_ops + 3);
		emitModRM();
	}
}

/**
 * @brief Emits x64 opcodes to store a value from register to stack frame.
 *
 * Generates MOV [RBP + offset], reg with size-specific encoding.
 * Handles 1, 2, 4, and 8 byte stores with optimal displacement encoding.
 *
 * @param textSectionData The vector to append opcodes to
 * @param reg The source register containing the value
 * @param offset The stack offset from RBP (negative for locals)
 * @param size_bytes The size of the value to store (1, 2, 4, or 8)
 */
inline void emitStoreToFrame(std::vector<char>& textSectionData, X64Register reg, int64_t offset, int size_bytes) {
	uint8_t reg_bits = static_cast<uint8_t>(reg) & 0x07;
	bool reg_extended = static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8);
	
	// Helper to emit ModR/M + displacement
	auto emitModRM = [&]() {
		if (offset >= -128 && offset <= 127) {
			// [RBP + disp8]: Mod=01, Reg=reg_bits, R/M=101 (RBP)
			textSectionData.push_back(static_cast<char>(0x40 | (reg_bits << 3) | 0x05));
			textSectionData.push_back(static_cast<char>(offset));
		} else {
			// [RBP + disp32]: Mod=10, Reg=reg_bits, R/M=101 (RBP)
			textSectionData.push_back(static_cast<char>(0x80 | (reg_bits << 3) | 0x05));
			uint32_t offset_u32 = static_cast<uint32_t>(offset);
			textSectionData.push_back(offset_u32 & 0xFF);
			textSectionData.push_back((offset_u32 >> 8) & 0xFF);
			textSectionData.push_back((offset_u32 >> 16) & 0xFF);
			textSectionData.push_back((offset_u32 >> 24) & 0xFF);
		}
	};
	
	// Branchless opcode emission using static arrays
	if (size_bytes == 8) {
		// MOV QWORD PTR [RBP + offset], reg - 64-bit store
		// REX.W (0x48) | REX.R (0x04) if extended register
		textSectionData.push_back(static_cast<char>(0x48 | (reg_extended << 2)));
		textSectionData.push_back(static_cast<char>(0x89)); // MOV r/m64, r64
		emitModRM();
	} else if (size_bytes == 4) {
		// MOV DWORD PTR [RBP + offset], reg - 32-bit store (branchless REX emission)
		static constexpr char mov32_ops[2] = { 0x44, static_cast<char>(0x89) }; // REX.R, MOV r/m32
		textSectionData.insert(textSectionData.end(), mov32_ops + !reg_extended, mov32_ops + 2);
		emitModRM();
	} else if (size_bytes == 2) {
		// MOV WORD PTR [RBP + offset], reg - 16-bit store (branchless REX emission)
		static constexpr char mov16_ops[3] = { 0x66, 0x44, static_cast<char>(0x89) }; // 66, REX.R, MOV r/m16
		// Always emit 0x66, then optionally REX.R, then MOV
		textSectionData.push_back(0x66);
		textSectionData.insert(textSectionData.end(), mov16_ops + 1 + !reg_extended, mov16_ops + 3);
		emitModRM();
	} else if (size_bytes == 1) {
		// MOV BYTE PTR [RBP + offset], reg - 8-bit store
		// Need REX prefix for registers 4-7 to access SPL, BPL, SIL, DIL instead of AH, CH, DH, BH
		int needs_rex = reg_extended | (static_cast<uint8_t>(reg) >= 4);
		// REX = 0x40 | (0x04 if extended) = branchless
		char rex8_ops[2] = { static_cast<char>(0x40 | (reg_extended << 2)), static_cast<char>(0x88) };
		textSectionData.insert(textSectionData.end(), rex8_ops + !needs_rex, rex8_ops + 2);
		emitModRM();
	}
}

/**
 * @brief Emits x64 opcodes to store a value from register to memory via pointer.
 *
 * Generates MOV [base_reg + offset], value_reg with size-specific encoding.
 * Handles 1, 2, 4, and 8 byte stores with optimal displacement encoding.
 * For sizes > 8, generates multiple 8-byte stores.
 *
 * @param textSectionData The vector to append opcodes to
 * @param value_reg The register containing the value to store
 * @param base_reg The base pointer register (e.g., RCX for 'this' pointer)
 * @param offset The offset from the base register
 * @param size_bytes The size in bytes (1, 2, 4, 8, or larger multiples)
 */
inline void emitStoreToMemory(std::vector<char>& textSectionData, X64Register value_reg, X64Register base_reg, int32_t offset, int size_bytes) {
	// For sizes > 8, we can't store in a single instruction
	// This shouldn't be called for large struct members - they should use memcpy or be initialized differently
	if (size_bytes > 8) {
		// For now, just skip large stores - they should be handled at a higher level
		// (e.g., compiler-generated constructors shouldn't try to initialize large members this way)
		return;
	}

	uint8_t value_reg_bits = static_cast<uint8_t>(value_reg) & 0x07;
	uint8_t base_reg_bits = static_cast<uint8_t>(base_reg) & 0x07;
	bool value_needs_rex_r = static_cast<uint8_t>(value_reg) >= static_cast<uint8_t>(X64Register::R8);
	bool base_needs_rex_b = static_cast<uint8_t>(base_reg) >= static_cast<uint8_t>(X64Register::R8);

	// Emit REX prefix and opcode based on size
	if (size_bytes == 8) {
		uint8_t rex = 0x48; // REX.W for 64-bit
		if (value_needs_rex_r) rex |= 0x04; // REX.R
		if (base_needs_rex_b) rex |= 0x01;  // REX.B
		textSectionData.push_back(rex);
		textSectionData.push_back(0x89); // MOV r/m64, r64
	} else if (size_bytes == 4) {
		if (value_needs_rex_r || base_needs_rex_b) {
			uint8_t rex = 0x40;
			if (value_needs_rex_r) rex |= 0x04;
			if (base_needs_rex_b) rex |= 0x01;
			textSectionData.push_back(rex);
		}
		textSectionData.push_back(0x89); // MOV r/m32, r32
	} else if (size_bytes == 2) {
		textSectionData.push_back(0x66); // Operand-size override prefix
		if (value_needs_rex_r || base_needs_rex_b) {
			uint8_t rex = 0x40;
			if (value_needs_rex_r) rex |= 0x04;
			if (base_needs_rex_b) rex |= 0x01;
			textSectionData.push_back(rex);
		}
		textSectionData.push_back(0x89); // MOV r/m16, r16
	} else if (size_bytes == 1) {
		// IMPORTANT: For 8-bit operations, always emit REX prefix to avoid high-byte registers (AH/BH/CH/DH)
		// Without REX, registers 4-7 map to AH, CH, DH, BH. With REX, they map to SPL, BPL, SIL, DIL.
		bool value_needs_rex_for_byte = (static_cast<uint8_t>(value_reg) >= 4);
		bool base_needs_rex_for_byte = (static_cast<uint8_t>(base_reg) >= 4);
		if (value_needs_rex_for_byte || base_needs_rex_for_byte) {
			uint8_t rex = 0x40;
			if (value_needs_rex_r) rex |= 0x04;
			if (base_needs_rex_b) rex |= 0x01;
			textSectionData.push_back(rex);
		}
		textSectionData.push_back(0x88); // MOV r/m8, r8
	} else {
		assert(false && "Unsupported store size");
		return;
	}

	// Emit ModR/M byte and displacement
	if (offset == 0) {
		// [base_reg] with no displacement
		textSectionData.push_back(0x00 | (value_reg_bits << 3) | base_reg_bits);
	} else if (offset >= -128 && offset <= 127) {
		// [base_reg + disp8]
		uint8_t modrm = 0x40 | (value_reg_bits << 3) | base_reg_bits;
		uint8_t disp = static_cast<uint8_t>(offset);
		textSectionData.push_back(modrm);
		textSectionData.push_back(disp);
	} else {
		// [base_reg + disp32]
		textSectionData.push_back(0x80 | (value_reg_bits << 3) | base_reg_bits);
		uint32_t offset_u32 = static_cast<uint32_t>(offset);
		textSectionData.push_back(offset_u32 & 0xFF);
		textSectionData.push_back((offset_u32 >> 8) & 0xFF);
		textSectionData.push_back((offset_u32 >> 16) & 0xFF);
		textSectionData.push_back((offset_u32 >> 24) & 0xFF);
	}
}

/**
 * @brief Emits MOV [RSP + offset], reg for storing a value to RSP-relative stack slot.
 *
 * RSP addressing requires a SIB byte. This is used for placing function call arguments
 * beyond the first 4 on the stack per Windows x64 calling convention.
 *
 * @param textSectionData The vector to append opcodes to
 * @param value_reg The source register containing the value to store
 * @param offset The offset from RSP (e.g., 32 for 5th arg, 40 for 6th arg)
 */
inline void emitStoreToRSP(std::vector<char>& textSectionData, X64Register value_reg, int32_t offset) {
	uint8_t reg_bits = static_cast<uint8_t>(value_reg) & 0x07;
	bool reg_extended = static_cast<uint8_t>(value_reg) >= static_cast<uint8_t>(X64Register::R8);
	
	// REX.W prefix for 64-bit, REX.R if source register is R8-R15 (branchless)
	uint8_t rex = 0x48 | (reg_extended << 2);
	textSectionData.push_back(rex);
	
	textSectionData.push_back(0x89); // MOV r/m64, r64
	
	// Use branchless encoding with static arrays
	bool use_disp8 = (offset >= -128 && offset <= 127);
	
	// ModR/M byte: 0x44 for disp8, 0x84 for disp32, both with r/m=100 (SIB follows)
	uint8_t modrm = (0x44 + (!use_disp8 * 0x40)) | (reg_bits << 3);
	textSectionData.push_back(modrm);
	textSectionData.push_back(0x24); // SIB: scale=00, index=100 (none), base=100 (RSP)
	
	// Emit displacement bytes - always emit at least 1, emit 4 for disp32
	uint32_t offset_u32 = static_cast<uint32_t>(offset);
	static constexpr int disp_sizes[] = { 4, 1 }; // disp32=4 bytes, disp8=1 byte
	int num_bytes = disp_sizes[use_disp8];
	for (int i = 0; i < num_bytes; ++i) {
		textSectionData.push_back((offset_u32 >> (i * 8)) & 0xFF);
	}
}

/**
 * @brief Emits SSE store instruction to [RSP + offset] for XMM registers.
 * 
 * Generates MOVSD [RSP + offset], xmm or MOVSS [RSP + offset], xmm
 * Uses SIB byte encoding for RSP-relative addressing.
 *
 * @param textSectionData The vector to append opcodes to
 * @param xmm_reg The source XMM register
 * @param offset The offset from RSP (signed 32-bit)
 * @param is_float true for float (MOVSS), false for double (MOVSD)
 */
inline void emitFloatStoreToRSP(std::vector<char>& textSectionData, X64Register xmm_reg, int32_t offset, bool is_float) {
	uint8_t xmm_bits = xmm_modrm_bits(xmm_reg);
	
	// Prefix: F3 for MOVSS (float), F2 for MOVSD (double)
	textSectionData.push_back(is_float ? 0xF3 : 0xF2);
	
	// REX prefix if XMM8-XMM15
	if (xmm_bits >= 8) {
		textSectionData.push_back(0x44); // REX.R for XMM8-XMM15
	}
	
	// Opcode: 0F 11 for MOVSS/MOVSD [mem], xmm (store variant)
	textSectionData.push_back(0x0F);
	textSectionData.push_back(0x11);
	
	// ModR/M and SIB for [RSP + disp]
	// RSP requires SIB byte
	bool use_disp8 = (offset >= -128 && offset <= 127);
	
	// ModR/M byte: mod=01/10 for disp8/disp32, reg=xmm_bits, r/m=100 (SIB follows)
	uint8_t modrm = (use_disp8 ? 0x44 : 0x84) | ((xmm_bits & 0x07) << 3);
	textSectionData.push_back(modrm);
	
	// SIB byte: scale=00, index=100 (none), base=100 (RSP)
	textSectionData.push_back(0x24);
	
	// Displacement
	if (use_disp8) {
		textSectionData.push_back(static_cast<uint8_t>(offset));
	} else {
		textSectionData.push_back((offset >> 0) & 0xFF);
		textSectionData.push_back((offset >> 8) & 0xFF);
		textSectionData.push_back((offset >> 16) & 0xFF);
		textSectionData.push_back((offset >> 24) & 0xFF);
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

/**
 * @brief Emits LEA reg, [RBP + offset] for any register
 * @param textSectionData The vector to append opcodes to
 * @param reg The destination register
 * @param offset The offset from RBP
 */
inline void emitLEAFromFrame(std::vector<char>& textSectionData, X64Register reg, int64_t offset) {
	uint8_t reg_bits = static_cast<uint8_t>(reg) & 0x07;
	bool needs_rex_r = static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8);
	
	// REX.W prefix for 64-bit operation
	uint8_t rex = 0x48;
	if (needs_rex_r) rex |= 0x04; // Set R bit for extended registers
	textSectionData.push_back(rex);
	
	textSectionData.push_back(0x8D); // LEA r64, m
	
	if (offset >= -128 && offset <= 127) {
		// 8-bit displacement: Mod=01, Reg=reg_bits, R/M=101 (RBP)
		textSectionData.push_back(0x40 | (reg_bits << 3) | 0x05);
		textSectionData.push_back(static_cast<uint8_t>(offset));
	} else {
		// 32-bit displacement: Mod=10, Reg=reg_bits, R/M=101 (RBP)
		textSectionData.push_back(0x80 | (reg_bits << 3) | 0x05);
		uint32_t offset_u32 = static_cast<uint32_t>(offset);
		textSectionData.push_back(offset_u32 & 0xFF);
		textSectionData.push_back((offset_u32 >> 8) & 0xFF);
		textSectionData.push_back((offset_u32 >> 16) & 0xFF);
		textSectionData.push_back((offset_u32 >> 24) & 0xFF);
	}
}

// Emit MOV reg, [RBP + offset] instruction
inline void emitMOVFromFrame(std::vector<uint8_t>& textSectionData, X64Register reg, int32_t offset) {
	// REX.W prefix for 64-bit operation
	if (reg >= X64Register::R8) {
		textSectionData.push_back(0x4C); // REX.WR
	} else {
		textSectionData.push_back(0x48); // REX.W
	}

	textSectionData.push_back(0x8B); // MOV r64, r/m64

	// Determine ModR/M byte
	uint8_t reg_field = static_cast<uint8_t>(reg) & 0x07; // Lower 3 bits
	uint8_t modrm;

	if (offset >= -128 && offset <= 127) {
		// [RBP + disp8]
		modrm = 0x45 | (reg_field << 3);
		textSectionData.push_back(modrm);
		textSectionData.push_back(static_cast<uint8_t>(offset));
	} else {
		// [RBP + disp32]
		modrm = 0x85 | (reg_field << 3);
		textSectionData.push_back(modrm);
		uint32_t offset_u32 = static_cast<uint32_t>(offset);
		textSectionData.push_back(offset_u32 & 0xFF);
		textSectionData.push_back((offset_u32 >> 8) & 0xFF);
		textSectionData.push_back((offset_u32 >> 16) & 0xFF);
		textSectionData.push_back((offset_u32 >> 24) & 0xFF);
	}
}

// Emit MOV [RBP + offset], reg instruction
inline void emitMOVToFrame(std::vector<uint8_t>& textSectionData, X64Register reg, int32_t offset) {
	// REX.W prefix for 64-bit operation
	if (reg >= X64Register::R8) {
		textSectionData.push_back(0x4C); // REX.WR
	} else {
		textSectionData.push_back(0x48); // REX.W
	}

	textSectionData.push_back(0x89); // MOV r/m64, r64

	// Determine ModR/M byte
	uint8_t reg_field = static_cast<uint8_t>(reg) & 0x07; // Lower 3 bits
	uint8_t modrm;

	if (offset >= -128 && offset <= 127) {
		// [RBP + disp8]
		modrm = 0x45 | (reg_field << 3);
		textSectionData.push_back(modrm);
		textSectionData.push_back(static_cast<uint8_t>(offset));
	} else {
		// [RBP + disp32]
		modrm = 0x85 | (reg_field << 3);
		textSectionData.push_back(modrm);
		uint32_t offset_u32 = static_cast<uint32_t>(offset);
		textSectionData.push_back(offset_u32 & 0xFF);
		textSectionData.push_back((offset_u32 >> 8) & 0xFF);
		textSectionData.push_back((offset_u32 >> 16) & 0xFF);
		textSectionData.push_back((offset_u32 >> 24) & 0xFF);
	}
}

// Phase 5: SafeStringKey removed - replaced with StringHandle throughout backend
// StringHandle provides superior performance:
// - No string allocations (32 bytes → 4 bytes)
// - No runtime hashing (pre-computed O(1) hash retrieval)
// - Integer-based comparisons instead of string comparisons

template<class TWriterClass = ObjectFileWriter>
class IrToObjConverter {
public:
	IrToObjConverter() = default;
	
	~IrToObjConverter() = default;

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
				FLASH_LOG(Codegen, Debug, "Processing IrOpcode::FunctionDecl");
				handleFunctionDecl(instruction);
				break;
			case IrOpcode::VariableDecl:
				FLASH_LOG(Codegen, Debug, "Processing IrOpcode::VariableDecl");
				handleVariableDecl(instruction);
				break;
			case IrOpcode::Return:
				FLASH_LOG(Codegen, Debug, "Processing IrOpcode::Return");
				handleReturn(instruction);
				break;
			case IrOpcode::FunctionCall:
				FLASH_LOG(Codegen, Debug, "Processing IrOpcode::FunctionCall");
				handleFunctionCall(instruction);
				break;
			case IrOpcode::StackAlloc:
				handleStackAlloc(instruction);
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
			case IrOpcode::FloatToInt:
				handleFloatToInt(instruction);
				break;
			case IrOpcode::IntToFloat:
				handleIntToFloat(instruction);
				break;
			case IrOpcode::FloatToFloat:
				handleFloatToFloat(instruction);
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
				FLASH_LOG(Codegen, Debug, "Processing IrOpcode::ShrAssign");
				handleShrAssign(instruction);
				break;
			case IrOpcode::Assignment:
				FLASH_LOG(Codegen, Debug, "Processing IrOpcode::Assignment");
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
			case IrOpcode::ArrayElementAddress:
				handleArrayElementAddress(instruction);
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
			case IrOpcode::AddressOfMember:
				handleAddressOfMember(instruction);
				break;
			case IrOpcode::ComputeAddress:
				handleComputeAddress(instruction);
				break;
			case IrOpcode::Dereference:
				handleDereference(instruction);
				break;
			case IrOpcode::DereferenceStore:
				handleDereferenceStore(instruction);
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
			case IrOpcode::TryBegin:
				handleTryBegin(instruction);
				break;
			case IrOpcode::TryEnd:
				handleTryEnd(instruction);
				break;
			case IrOpcode::CatchBegin:
				handleCatchBegin(instruction);
				break;
			case IrOpcode::CatchEnd:
				handleCatchEnd(instruction);
				break;
			case IrOpcode::Throw:
				handleThrow(instruction);
				break;
			case IrOpcode::Rethrow:
				handleRethrow(instruction);
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

		// Emit dynamic_cast runtime helpers if needed
		if (needs_dynamic_cast_runtime_) {
			ProfilingTimer timer("Emit dynamic_cast runtime helpers", show_timing);
			emit_dynamic_cast_runtime_helpers();
		}

		{
			ProfilingTimer timer("Finalize sections", show_timing);
			finalizeSections();
		}

		// Clean up the last function's variable scope AFTER finalizeSections has used it
		// for stack size patching
		if (!variable_scopes.empty()) {
			variable_scopes.pop_back();
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
	// Helper to convert internal try blocks to ObjectFileWriter format
	// Used during function finalization to prepare exception handling information
	std::pair<std::vector<ObjectFileWriter::TryBlockInfo>, std::vector<ObjectFileWriter::UnwindMapEntryInfo>>
	convertExceptionInfoToWriterFormat() {
		std::vector<ObjectFileWriter::TryBlockInfo> try_blocks;
		for (const auto& try_block : current_function_try_blocks_) {
			ObjectFileWriter::TryBlockInfo block_info;
			block_info.try_start_offset = try_block.try_start_offset;
			block_info.try_end_offset = try_block.try_end_offset;
			for (const auto& handler : try_block.catch_handlers) {
				ObjectFileWriter::CatchHandlerInfo handler_info;
				handler_info.type_index = static_cast<uint32_t>(handler.type_index);
				handler_info.handler_offset = handler.handler_offset;
				handler_info.is_catch_all = handler.is_catch_all;
				handler_info.is_const = handler.is_const;
				handler_info.is_reference = handler.is_reference;
				handler_info.is_rvalue_reference = handler.is_rvalue_reference;
				
				// Use pre-computed frame offset for caught exception object
				handler_info.catch_obj_offset = handler.catch_obj_stack_offset;
				
				// Get type name for type descriptor generation
				if (!handler.is_catch_all) {
					// For built-in types, use the Type enum; for user-defined types, use gTypeInfo
					if (handler.exception_type != Type::Void && handler.exception_type != Type::UserDefined && handler.exception_type != Type::Struct) {
						// Built-in type - get name from Type enum
						handler_info.type_name = getTypeName(handler.exception_type);
					} else if (handler.type_index < gTypeInfo.size()) {
						// User-defined type - get name from gTypeInfo
						handler_info.type_name = StringTable::getStringView(gTypeInfo[handler.type_index].name());
					}
				}
				
				block_info.catch_handlers.push_back(handler_info);
			}
			try_blocks.push_back(block_info);
		}
		
		std::vector<ObjectFileWriter::UnwindMapEntryInfo> unwind_map;
		for (const auto& unwind_entry : current_function_unwind_map_) {
			ObjectFileWriter::UnwindMapEntryInfo entry_info;
			entry_info.to_state = unwind_entry.to_state;
			entry_info.action = unwind_entry.action.isValid() ? std::string(StringTable::getStringView(unwind_entry.action)) : std::string();
			unwind_map.push_back(entry_info);
		}
		
		return {std::move(try_blocks), std::move(unwind_map)};
	}

	// Shared arithmetic operation context
	struct ArithmeticOperationContext {
		TypedValue result_value;
		X64Register result_physical_reg;
		X64Register rhs_physical_reg;
		Type operand_type;  // Type of the operands (for comparisons, different from result_value.type)
		int operand_size_in_bits;  // Size of the operands (for comparisons, different from result_value.size_in_bits)
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
		
		// Determine if we need REX prefix
		bool needs_rex = include_rex_w; // Always need REX for 64-bit (REX.W)
		
		// Start with appropriate REX prefix
		result.rex_prefix = include_rex_w ? 0x48 : 0x40; // REX.W for 64-bit, base REX for 32-bit
		
		// Set REX.R if reg_field (source in Reg field of ModR/M) is R8-R15
		if (static_cast<uint8_t>(reg_field) >= 8) {
			result.rex_prefix |= 0x04; // Set REX.R bit
			needs_rex = true;
		}
		
		// Set REX.B if rm_field (destination in R/M field of ModR/M) is R8-R15
		if (static_cast<uint8_t>(rm_field) >= 8) {
			result.rex_prefix |= 0x01; // Set REX.B bit
			needs_rex = true;
		}
		
		// If we don't need REX prefix (32-bit op with registers < 8), set to 0
		// The caller should check if rex_prefix is 0 and skip emitting it
		if (!needs_rex) {
			result.rex_prefix = 0;
		}
		
		// Build ModR/M byte: Mod=11 (register-to-register), Reg=reg_field[2:0], R/M=rm_field[2:0]
		result.modrm_byte = 0xC0 + 
			((static_cast<uint8_t>(reg_field) & 0x07) << 3) + 
			(static_cast<uint8_t>(rm_field) & 0x07);
		
		return result;
	}
	
	// Helper for instructions with opcode extension (reg field is a constant, rm is the register)
	// Used by shift instructions and division which encode the operation in the reg field
	void emitOpcodeExtInstruction(uint8_t opcode, X64OpcodeExtension opcode_ext, X64Register rm_field, int size_in_bits) {
		// Determine if we need REX.W based on operand size
		uint8_t rex_prefix = (size_in_bits == 64) ? 0x48 : 0x40;
		
		// Check if rm_field needs REX.B (registers R8-R15)
		if (static_cast<uint8_t>(rm_field) >= 8) {
			rex_prefix |= 0x01; // Set REX.B
		}
		
		// Build ModR/M byte: 11 (register mode) + opcode extension in reg field + rm bits
		uint8_t ext_value = static_cast<uint8_t>(opcode_ext);
		uint8_t modrm_byte = 0xC0 | ((ext_value & 0x07) << 3) | (static_cast<uint8_t>(rm_field) & 0x07);
		
		// Emit the instruction
		textSectionData.push_back(rex_prefix);
		textSectionData.push_back(opcode);
		textSectionData.push_back(modrm_byte);
	}
	
	// Helper function to emit a binary operation instruction (reg-to-reg)
	void emitBinaryOpInstruction(uint8_t opcode, X64Register src_reg, X64Register dst_reg, int size_in_bits) {
		// Determine if we need a REX prefix
		bool needs_rex = (size_in_bits == 64);  // Always need REX for 64-bit (REX.W)
		uint8_t rex_prefix = (size_in_bits == 64) ? 0x48 : 0x40;  // REX.W for 64-bit, base REX for 32-bit
		
		// Check if registers need REX extensions
		if (static_cast<uint8_t>(src_reg) >= 8) {
			rex_prefix |= 0x04; // Set REX.R for source (reg field)
			needs_rex = true;
		}
		if (static_cast<uint8_t>(dst_reg) >= 8) {
			rex_prefix |= 0x01; // Set REX.B for destination (rm field)
			needs_rex = true;
		}
		
		// Build ModR/M byte: 11 (register mode) + src in reg field + dst in rm field
		uint8_t modrm_byte = 0xC0 | ((static_cast<uint8_t>(src_reg) & 0x07) << 3) | (static_cast<uint8_t>(dst_reg) & 0x07);
		
		// Emit the instruction
		if (needs_rex) {
			textSectionData.push_back(rex_prefix);
		}
		textSectionData.push_back(opcode);
		textSectionData.push_back(modrm_byte);
	}
	
	// Helper function to emit MOV reg, reg instruction with size awareness
	void emitMovRegToReg(X64Register src_reg, X64Register dst_reg, int src_size_in_bits) {
		emitBinaryOpInstruction(0x89, src_reg, dst_reg, src_size_in_bits);
	}
	
	// Helper function to emit a comparison instruction (CMP + SETcc + MOVZX)
	void emitComparisonInstruction(const ArithmeticOperationContext& ctx, uint8_t setcc_opcode) {
		// Compare operands: CMP dst, src (opcode 0x39)
		// Use the operand size to determine whether to use 32-bit or 64-bit operation
		emitBinaryOpInstruction(0x39, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.operand_size_in_bits);

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
		const BinaryOp& bin_op = *getTypedPayload<BinaryOp>(instruction);
		
		// Determine result type based on operation
		// For comparisons, result is bool (8 bits for code generation)
		// For arithmetic operations, result type matches operand type
		Type result_type = bin_op.lhs.type;
		int result_size = bin_op.lhs.size_in_bits;
		
		auto opcode = instruction.getOpcode();
		bool is_comparison = (opcode == IrOpcode::Equal || opcode == IrOpcode::NotEqual ||
		                      opcode == IrOpcode::LessThan || opcode == IrOpcode::LessEqual ||
		                      opcode == IrOpcode::GreaterThan || opcode == IrOpcode::GreaterEqual ||
		                      opcode == IrOpcode::UnsignedLessThan || opcode == IrOpcode::UnsignedLessEqual ||
		                      opcode == IrOpcode::UnsignedGreaterThan || opcode == IrOpcode::UnsignedGreaterEqual ||
		                      opcode == IrOpcode::FloatEqual || opcode == IrOpcode::FloatNotEqual ||
		                      opcode == IrOpcode::FloatLessThan || opcode == IrOpcode::FloatLessEqual ||
		                      opcode == IrOpcode::FloatGreaterThan || opcode == IrOpcode::FloatGreaterEqual);
		
		// Store the operand type and size for register allocation and loading decisions
		Type operand_type = bin_op.lhs.type;
		int operand_size = bin_op.lhs.size_in_bits;
		
		if (is_comparison) {
			result_type = Type::Bool;
			result_size = 8;  // We store bool as 8 bits for register operations
		}
		
		// Create context with correct result type
		ArithmeticOperationContext ctx = {
			.result_value = TypedValue{result_type, result_size, bin_op.result},
			.result_physical_reg = X64Register::Count,
			.rhs_physical_reg = X64Register::RCX,
			.operand_type = operand_type,
			.operand_size_in_bits = operand_size
		};

		// Support integer, boolean, and floating-point operations
		if (!is_integer_type(ctx.result_value.type) && !is_bool_type(ctx.result_value.type) && !is_floating_point_type(ctx.result_value.type)) {
			assert(false && (std::string("Only integer/boolean/floating-point ") + operation_name + " is supported").c_str());
		}

		ctx.result_physical_reg = X64Register::Count;
		if (std::holds_alternative<StringHandle>(bin_op.lhs.value)) {
			StringHandle lhs_var_op = std::get<StringHandle>(bin_op.lhs.value);
			auto lhs_var_id = variable_scopes.back().variables.find(lhs_var_op);
			if (lhs_var_id != variable_scopes.back().variables.end()) {
				if (auto var_reg = regAlloc.tryGetStackVariableRegister(lhs_var_id->second.offset); var_reg.has_value()) {
					ctx.result_physical_reg = var_reg.value();	// value is already in a register, we can use it without a move!
				}
				else {
					assert(variable_scopes.back().scope_stack_space <= lhs_var_id->second.offset);

					if (is_floating_point_type(operand_type)) {
						// For float/double, allocate an XMM register
						ctx.result_physical_reg = allocateXMMRegisterWithSpilling();
						bool is_float = (operand_type == Type::Float);
						auto mov_opcodes = generateFloatMovFromFrame(ctx.result_physical_reg, lhs_var_id->second.offset, is_float);
						textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					} else {
						// Check if this is a reference - if so, we need to dereference it
						auto ref_it = reference_stack_info_.find(lhs_var_id->second.offset);
						if (ref_it != reference_stack_info_.end()) {
							// This is a reference - load the pointer first, then dereference
							ctx.result_physical_reg = allocateRegisterWithSpilling();
							// Load the pointer into the register
							emitMovFromFrame(ctx.result_physical_reg, lhs_var_id->second.offset);
							// Now dereference: load from [register + 0]
							int value_size_bytes = ref_it->second.value_size_bits / 8;
							emitMovFromMemory(ctx.result_physical_reg, ctx.result_physical_reg, 0, value_size_bytes);
						} else if (lhs_var_id->second.is_array) {
							// Source is an array - use LEA to get its address (array-to-pointer decay)
							ctx.result_physical_reg = allocateRegisterWithSpilling();
							emitLeaFromFrame(ctx.result_physical_reg, lhs_var_id->second.offset);
						} else {
							// Not a reference, load normally
							// For integers, use regular MOV
							ctx.result_physical_reg = allocateRegisterWithSpilling();
							emitMovFromFrameBySize(ctx.result_physical_reg, lhs_var_id->second.offset, ctx.operand_size_in_bits);
						}
						regAlloc.flushSingleDirtyRegister(ctx.result_physical_reg);
					}
				}
			}
			else {
				assert(false && "Missing variable name"); // TODO: Error handling
			}
		}
		else if (std::holds_alternative<TempVar>(bin_op.lhs.value)) {
			auto lhs_var_op = std::get<TempVar>(bin_op.lhs.value);
			auto lhs_stack_var_addr = getStackOffsetFromTempVar(lhs_var_op, bin_op.lhs.size_in_bits);
			if (auto lhs_reg = regAlloc.tryGetStackVariableRegister(lhs_stack_var_addr); lhs_reg.has_value()) {
				ctx.result_physical_reg = lhs_reg.value();
			}
			else {
				assert(variable_scopes.back().scope_stack_space <= lhs_stack_var_addr);

				if (is_floating_point_type(operand_type)) {
					// For float/double, allocate an XMM register
					ctx.result_physical_reg = allocateXMMRegisterWithSpilling();
					bool is_float = (operand_type == Type::Float);
					auto mov_opcodes = generateFloatMovFromFrame(ctx.result_physical_reg, lhs_stack_var_addr, is_float);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				} else {
					// Check if this is a reference - if so, we need to dereference it
					auto ref_it = reference_stack_info_.find(lhs_stack_var_addr);
					
					// If not found with TempVar offset, try looking up by name
					if (ref_it == reference_stack_info_.end()) {
						std::string_view var_name = lhs_var_op.name();
						// Remove the '%' prefix if present
						if (!var_name.empty() && var_name[0] == '%') {
							var_name = var_name.substr(1);
						}
						auto named_var_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(var_name));
						if (named_var_it != variable_scopes.back().variables.end()) {
							int32_t named_offset = named_var_it->second.offset;
							ref_it = reference_stack_info_.find(named_offset);
							if (ref_it != reference_stack_info_.end()) {
								// Found it! Update lhs_stack_var_addr to use the named variable offset
								lhs_stack_var_addr = named_offset;
							}
						}
					}
					
					if (ref_it != reference_stack_info_.end() && !ref_it->second.holds_address_only) {
						// This is a reference - load the pointer first, then dereference
						ctx.result_physical_reg = allocateRegisterWithSpilling();
						// Load the pointer into the register
						auto load_ptr = generatePtrMovFromFrame(ctx.result_physical_reg, lhs_stack_var_addr);
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
					} else if (ref_it != reference_stack_info_.end() && ref_it->second.holds_address_only) {
						// This holds an address value directly (from addressof) - load without dereferencing
						ctx.result_physical_reg = allocateRegisterWithSpilling();
						auto load_ptr = generatePtrMovFromFrame(ctx.result_physical_reg, lhs_stack_var_addr);
						textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(), load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
					} else {
						// Not a reference, load normally with correct size
						ctx.result_physical_reg = allocateRegisterWithSpilling();
						emitMovFromFrameBySize(ctx.result_physical_reg, lhs_stack_var_addr, ctx.operand_size_in_bits);
					}
					regAlloc.flushSingleDirtyRegister(ctx.result_physical_reg);
				}
			}
		}
		else if (std::holds_alternative<unsigned long long>(bin_op.lhs.value)) {
			// LHS is a literal value
			auto lhs_value = std::get<unsigned long long>(bin_op.lhs.value);
			ctx.result_physical_reg = allocateRegisterWithSpilling();

			// Load the literal value into the register
			// Use the correct operand size for the move instruction
			uint8_t reg_num = static_cast<uint8_t>(ctx.result_physical_reg);
			
			if (ctx.operand_size_in_bits == 64) {
				// 64-bit: mov reg, imm64 with REX.W
				uint8_t rex_prefix = 0x48; // REX.W
				
				// For R8-R15, set REX.B bit
				if (reg_num >= 8) {
					rex_prefix |= 0x01; // Set REX.B
					reg_num &= 0x07; // Use lower 3 bits for opcode
				}
				
				std::array<uint8_t, 10> movInst = { rex_prefix, static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0, 0, 0, 0, 0 };
				std::memcpy(&movInst[2], &lhs_value, sizeof(lhs_value));
				textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
			} else {
				// 32-bit (or smaller): mov r32, imm32
				// Only use REX if we need extended registers (R8-R15)
				bool needs_rex = (reg_num >= 8);
				
				if (needs_rex) {
					uint8_t rex_prefix = 0x40; // Base REX (no REX.W for 32-bit)
					rex_prefix |= 0x01; // Set REX.B
					textSectionData.push_back(rex_prefix);
					reg_num &= 0x07; // Use lower 3 bits for opcode
				}
				
				// mov r32, imm32: opcode B8+r, imm32
				textSectionData.push_back(static_cast<uint8_t>(0xB8 + reg_num));
				uint32_t imm32 = static_cast<uint32_t>(lhs_value);
				textSectionData.push_back(imm32 & 0xFF);
				textSectionData.push_back((imm32 >> 8) & 0xFF);
				textSectionData.push_back((imm32 >> 16) & 0xFF);
				textSectionData.push_back((imm32 >> 24) & 0xFF);
			}
		}
		else if (instruction.isOperandType<double>(3)) {
			// LHS is a floating-point literal value
			auto lhs_value = instruction.getOperandAs<double>(3);
			ctx.result_physical_reg = allocateXMMRegisterWithSpilling();

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
		if (std::holds_alternative<StringHandle>(bin_op.rhs.value)) {
			StringHandle rhs_var_op = std::get<StringHandle>(bin_op.rhs.value);
			auto rhs_var_id = variable_scopes.back().variables.find(rhs_var_op);
			if (rhs_var_id != variable_scopes.back().variables.end()) {
				if (auto var_reg = regAlloc.tryGetStackVariableRegister(rhs_var_id->second.offset); var_reg.has_value()) {
					ctx.rhs_physical_reg = var_reg.value();	// value is already in a register, we can use it without a move!
				}
				else {
					assert(variable_scopes.back().scope_stack_space <= rhs_var_id->second.offset);

					if (is_floating_point_type(operand_type)) {
						// For float/double, allocate an XMM register
						ctx.rhs_physical_reg = allocateXMMRegisterWithSpilling();
						bool is_float = (operand_type == Type::Float);
						auto mov_opcodes = generateFloatMovFromFrame(ctx.rhs_physical_reg, rhs_var_id->second.offset, is_float);
						textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					} else {
						// Check if this is a reference - if so, we need to dereference it
						auto ref_it = reference_stack_info_.find(rhs_var_id->second.offset);
						if (ref_it != reference_stack_info_.end()) {
							// This is a reference - load the pointer first, then dereference
							ctx.rhs_physical_reg = allocateRegisterWithSpilling();
							
							// If RHS register conflicts with result register, we need to handle it
							// Strategy: Keep LHS in its register, allocate a fresh register for RHS
							if (ctx.rhs_physical_reg == ctx.result_physical_reg) {
								// Allocate a NEW register for RHS, excluding the LHS register
								ctx.rhs_physical_reg = allocateRegisterWithSpilling(ctx.result_physical_reg);
							}
							
							// Load the pointer into the register
							emitMovFromFrame(ctx.rhs_physical_reg, rhs_var_id->second.offset);
							// Now dereference: load from [register + 0]
							int value_size_bytes = ref_it->second.value_size_bits / 8;
							emitMovFromMemory(ctx.rhs_physical_reg, ctx.rhs_physical_reg, 0, value_size_bytes);
						} else {
							// Not a reference, load normally
							// For integers, use regular MOV
							ctx.rhs_physical_reg = allocateRegisterWithSpilling();
							
							// If RHS register conflicts with result register, we need to handle it
							// Strategy: Keep LHS in its register, allocate a fresh register for RHS
							if (ctx.rhs_physical_reg == ctx.result_physical_reg) {
								// Allocate a NEW register for RHS, excluding the LHS register
								ctx.rhs_physical_reg = allocateRegisterWithSpilling(ctx.result_physical_reg);
							}
							
							// Use the RHS's actual size for loading, not the LHS/operand size
							// This is important when types are mixed (e.g., int + long)
							emitMovFromFrameBySize(ctx.rhs_physical_reg, rhs_var_id->second.offset, bin_op.rhs.size_in_bits);
						}
						regAlloc.flushSingleDirtyRegister(ctx.rhs_physical_reg);
					}
				}
			}
			else {
				assert(false && "Missing variable name"); // TODO: Error handling
			}
		}
		else if (std::holds_alternative<TempVar>(bin_op.rhs.value)) {
			auto rhs_var_op = std::get<TempVar>(bin_op.rhs.value);
			auto rhs_stack_var_addr = getStackOffsetFromTempVar(rhs_var_op, bin_op.rhs.size_in_bits);
			if (auto rhs_reg = regAlloc.tryGetStackVariableRegister(rhs_stack_var_addr); rhs_reg.has_value()) {
				ctx.rhs_physical_reg = rhs_reg.value();
			}
			else {
				assert(variable_scopes.back().scope_stack_space <= rhs_stack_var_addr);

				if (is_floating_point_type(operand_type)) {
					// For float/double, allocate an XMM register
					ctx.rhs_physical_reg = allocateXMMRegisterWithSpilling();
					bool is_float = (operand_type == Type::Float);
					auto mov_opcodes = generateFloatMovFromFrame(ctx.rhs_physical_reg, rhs_stack_var_addr, is_float);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				} else {
					// Check if this is a reference - if so, we need to dereference it
					auto ref_it = reference_stack_info_.find(rhs_stack_var_addr);
					
					// If not found with TempVar offset, try looking up by name
					if (ref_it == reference_stack_info_.end()) {
						std::string_view var_name = rhs_var_op.name();
						// Remove the '%' prefix if present
						if (!var_name.empty() && var_name[0] == '%') {
							var_name = var_name.substr(1);
						}
						auto named_var_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(var_name));
						if (named_var_it != variable_scopes.back().variables.end()) {
							int32_t named_offset = named_var_it->second.offset;
							ref_it = reference_stack_info_.find(named_offset);
							if (ref_it != reference_stack_info_.end()) {
								// Found it! Update rhs_stack_var_addr to use the named variable offset
								rhs_stack_var_addr = named_offset;
							}
						}
					}
					
					if (ref_it != reference_stack_info_.end()) {
						// This is a reference - load the pointer first, then dereference
						ctx.rhs_physical_reg = allocateRegisterWithSpilling();
						
						// If RHS register conflicts with result register, we need to handle it
						// Strategy: Keep LHS in its register, allocate a fresh register for RHS
						if (ctx.rhs_physical_reg == ctx.result_physical_reg) {
							// Allocate a NEW register for RHS, excluding the LHS register
							ctx.rhs_physical_reg = allocateRegisterWithSpilling(ctx.result_physical_reg);
						}
						
						// Load the pointer into the register
						emitMovFromFrame(ctx.rhs_physical_reg, rhs_stack_var_addr);
						// Now dereference: load from [register + 0]
						int value_size_bytes = ref_it->second.value_size_bits / 8;
						emitMovFromMemory(ctx.rhs_physical_reg, ctx.rhs_physical_reg, 0, value_size_bytes);
					} else {
						// Not a reference, load normally with correct size
						ctx.rhs_physical_reg = allocateRegisterWithSpilling();
						
						// If RHS register conflicts with result register, we need to handle it
						// Strategy: Keep LHS in its register, allocate a fresh register for RHS
						if (ctx.rhs_physical_reg == ctx.result_physical_reg) {
							// Allocate a NEW register for RHS, excluding the LHS register
							ctx.rhs_physical_reg = allocateRegisterWithSpilling(ctx.result_physical_reg);
						}
						
						// Use the RHS's actual size for loading, not the LHS/operand size
						// This is important when types are mixed (e.g., int + long)
						emitMovFromFrameBySize(ctx.rhs_physical_reg, rhs_stack_var_addr, bin_op.rhs.size_in_bits);
					}
					regAlloc.flushSingleDirtyRegister(ctx.rhs_physical_reg);
				}
			}
		}
		else if (std::holds_alternative<unsigned long long>(bin_op.rhs.value)) {
			// RHS is a literal value
			auto rhs_value = std::get<unsigned long long>(bin_op.rhs.value);
			ctx.rhs_physical_reg = allocateRegisterWithSpilling();

			// If RHS register conflicts with result register, we need to handle it
			// Strategy: Keep LHS in its register, allocate a fresh register for RHS
			if (ctx.rhs_physical_reg == ctx.result_physical_reg) {
				// Allocate a NEW register for RHS, excluding the LHS register
				ctx.rhs_physical_reg = allocateRegisterWithSpilling(ctx.result_physical_reg);
			}

			// Load the literal value into the register
			// Use the correct operand size for the move instruction
			uint8_t reg_num = static_cast<uint8_t>(ctx.rhs_physical_reg);
			
			if (ctx.operand_size_in_bits == 64) {
				// 64-bit: mov reg, imm64 with REX.W
				uint8_t rex_prefix = 0x48; // REX.W
				
				// For R8-R15, set REX.B bit
				if (reg_num >= 8) {
					rex_prefix |= 0x01; // Set REX.B
					reg_num &= 0x07; // Use lower 3 bits for opcode
				}
				
				std::array<uint8_t, 10> movInst = { rex_prefix, static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0, 0, 0, 0, 0 };
				std::memcpy(&movInst[2], &rhs_value, sizeof(rhs_value));
				textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
			} else {
				// 32-bit (or smaller): mov r32, imm32
				// Only use REX if we need extended registers (R8-R15)
				bool needs_rex = (reg_num >= 8);
				
				if (needs_rex) {
					uint8_t rex_prefix = 0x40; // Base REX (no REX.W for 32-bit)
					rex_prefix |= 0x01; // Set REX.B
					textSectionData.push_back(rex_prefix);
					reg_num &= 0x07; // Use lower 3 bits for opcode
				}
				
				// mov r32, imm32: opcode B8+r, imm32
				textSectionData.push_back(static_cast<uint8_t>(0xB8 + reg_num));
				uint32_t imm32 = static_cast<uint32_t>(rhs_value);
				textSectionData.push_back(imm32 & 0xFF);
				textSectionData.push_back((imm32 >> 8) & 0xFF);
				textSectionData.push_back((imm32 >> 16) & 0xFF);
				textSectionData.push_back((imm32 >> 24) & 0xFF);
			}
		}
		else if (std::holds_alternative<double>(bin_op.rhs.value)) {
			// RHS is a floating-point literal value
			auto rhs_value = std::get<double>(bin_op.rhs.value);
			ctx.rhs_physical_reg = allocateXMMRegisterWithSpilling();

			// For floating-point, we need to load the value into an XMM register
			// Strategy: Load the bit pattern as integer into a GPR, then move to XMM
			// 1. Load bits into a GPR using movabs
			// 2. Move from GPR to XMM using movq (for double) or movd (for float)

			// Allocate a temporary GPR for the bit pattern
			X64Register temp_gpr = allocateRegisterWithSpilling();

			if (operand_type == Type::Float) {
				// For float (single precision), convert double to float and get 32-bit representation
				float float_value = static_cast<float>(rhs_value);
				uint32_t bits;
				std::memcpy(&bits, &float_value, sizeof(bits));

				// mov temp_gpr_32, imm32 (load 32-bit bit pattern)
				uint8_t reg_num = static_cast<uint8_t>(temp_gpr);
				
				// For R8-R15, we need a REX prefix with REX.B set
				if (reg_num >= 8) {
					textSectionData.push_back(0x41); // REX.B
					reg_num &= 0x07; // Use lower 3 bits for opcode
				}
				
				std::array<uint8_t, 5> movInst = { static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0 };
				std::memcpy(&movInst[1], &bits, sizeof(bits));
				textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());

				// movd xmm, r32 (66 0F 6E /r) - move 32-bit from GPR to XMM
				std::array<uint8_t, 4> movdInst = { 0x66, 0x0F, 0x6E, 0xC0 };
				// Add REX prefix if either XMM or GPR is extended
				uint8_t xmm_num = xmm_modrm_bits(ctx.rhs_physical_reg);
				uint8_t gpr_num = static_cast<uint8_t>(temp_gpr);
				if (xmm_num >= 8 || gpr_num >= 8) {
					uint8_t rex = 0x40;
					if (xmm_num >= 8) rex |= 0x04; // REX.R
					if (gpr_num >= 8) rex |= 0x01; // REX.B
					textSectionData.push_back(rex);
				}
				movdInst[3] = 0xC0 + ((xmm_num & 0x07) << 3) + (gpr_num & 0x07);
				textSectionData.insert(textSectionData.end(), movdInst.begin(), movdInst.end());
			} else {
				// For double, load 64-bit representation
				uint64_t bits;
				std::memcpy(&bits, &rhs_value, sizeof(bits));

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
			}

			// Release the temporary GPR
			regAlloc.release(temp_gpr);
		}
		
		// If result register hasn't been allocated yet (e.g., LHS is a literal), allocate one now
		if (ctx.result_physical_reg == X64Register::Count) {
			if (is_floating_point_type(ctx.result_value.type)) {
				ctx.result_physical_reg = allocateXMMRegisterWithSpilling();
			} else {
				ctx.result_physical_reg = allocateRegisterWithSpilling();
			}
		}

		if (std::holds_alternative<TempVar>(ctx.result_value.value)) {
			const TempVar temp_var = std::get<TempVar>(ctx.result_value.value);
			const int32_t stack_offset = getStackOffsetFromTempVar(temp_var);
			StringHandle reassign_handle = StringTable::getOrInternStringHandle(temp_var.name());
			variable_scopes.back().variables[reassign_handle].offset = stack_offset;
			// Only set stack variable offset for allocated registers (not XMM0/XMM1 used directly)
			if (ctx.result_physical_reg < X64Register::XMM0 || regAlloc.is_allocated(ctx.result_physical_reg)) {
				// IMPORTANT: Before reassigning this register to the result TempVar's offset,
				// we must flush its current value to the OLD offset if it was dirty.
				// This happens when the LHS operand was in a register that we're reusing for the result.
				// Without flushing, the LHS value would be lost (crucial for post-increment).
				auto& reg_info = regAlloc.registers[static_cast<int>(ctx.result_physical_reg)];
				if (reg_info.isDirty && reg_info.stackVariableOffset != INT_MIN && 
				    reg_info.stackVariableOffset != stack_offset) {
					FLASH_LOG_FORMAT(Codegen, Debug, "FLUSHING dirty reg {} from old offset {} to new offset {}, size={}", 
						static_cast<int>(ctx.result_physical_reg), reg_info.stackVariableOffset, stack_offset, reg_info.size_in_bits);
					// Use the actual register size from reg_info, not hardcoded 64 bits
					emitMovToFrameSized(
						SizedRegister{ctx.result_physical_reg, static_cast<uint8_t>(reg_info.size_in_bits), false},
						SizedStackSlot{reg_info.stackVariableOffset, reg_info.size_in_bits, false}
					);
				}
				regAlloc.set_stack_variable_offset(ctx.result_physical_reg, stack_offset, ctx.result_value.size_in_bits);
			}
		}

		// Final safety check: if LHS and RHS ended up in the same register, we need to fix it
		// This can happen when all registers are in use and spilling picks the same register twice
		if (ctx.result_physical_reg == ctx.rhs_physical_reg && !is_floating_point_type(ctx.result_value.type)) {
			// Get the LHS variable's stack location and reload it into a different register
			auto& reg_info = regAlloc.registers[static_cast<int>(ctx.result_physical_reg)];
			if (reg_info.stackVariableOffset != INT_MIN) {
				// Allocate a fresh register for LHS and reload it from the stack
				X64Register new_lhs_reg = allocateRegisterWithSpilling();
				emitMovFromFrameBySize(new_lhs_reg, reg_info.stackVariableOffset, reg_info.size_in_bits);
				
				// Update tracking: the new register now holds the LHS variable
				regAlloc.set_stack_variable_offset(new_lhs_reg, reg_info.stackVariableOffset, reg_info.size_in_bits);
				regAlloc.registers[static_cast<int>(new_lhs_reg)].isDirty = reg_info.isDirty;
				
				// Clear the old register's tracking (it now only holds RHS)
				regAlloc.registers[static_cast<int>(ctx.result_physical_reg)].stackVariableOffset = INT_MIN;
				regAlloc.registers[static_cast<int>(ctx.result_physical_reg)].isDirty = false;
				
				ctx.result_physical_reg = new_lhs_reg;
			}
		}

		return ctx;
	}

	// Store the result of arithmetic operations to the appropriate destination
	void storeArithmeticResult(const ArithmeticOperationContext& ctx, X64Register source_reg = X64Register::Count) {
		// Use the result register by default, or the specified source register (e.g., RAX for division)
		X64Register actual_source_reg = (source_reg == X64Register::Count) ? ctx.result_physical_reg : source_reg;

		// Check if we're dealing with floating-point types
		bool is_float_type = (ctx.result_value.type == Type::Float || ctx.result_value.type == Type::Double);

		// Track whether we should release the source register after storing
		bool should_release_source = false;

		// Determine the final destination of the result (register or memory)
		if (std::holds_alternative<StringHandle>(ctx.result_value.value)) {
			// If the result is a named variable, find its stack offset - Phase 5: Convert to StringHandle
			int final_result_offset = variable_scopes.back().variables[std::get<StringHandle>(ctx.result_value.value)].offset;

			// Check if this is a reference - if so, we need to store through the pointer
			auto ref_it = reference_stack_info_.find(final_result_offset);
			if (ref_it != reference_stack_info_.end()) {
				// This is a reference - load the pointer, then store the value through it
				X64Register ptr_reg = allocateRegisterWithSpilling();
				// Load the pointer into the register
				auto load_ptr = generatePtrMovFromFrame(ptr_reg, final_result_offset);
				textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(), load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
				// Now store the value through the pointer: [ptr_reg + 0] = actual_source_reg
				int value_size_bits = ref_it->second.value_size_bits;
				int value_size_bytes = value_size_bits / 8;
				emitStoreToMemory(textSectionData, actual_source_reg, ptr_reg, 0, value_size_bytes);
				regAlloc.release(ptr_reg);
			} else {
				// Not a reference, store normally
				// Store the computed result from actual_source_reg to memory
				if (is_float_type) {
					// Use SSE movss/movsd for float/double
					bool is_single_precision = (ctx.result_value.type == Type::Float);
					auto store_opcodes = generateFloatMovToFrame(actual_source_reg, final_result_offset, is_single_precision);
					textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
					                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
				} else {
					emitMovToFrameSized(
						SizedRegister{actual_source_reg, 64, false},  // source: 64-bit register
						SizedStackSlot{final_result_offset, ctx.result_value.size_in_bits, isSignedType(ctx.result_value.type)}  // dest
					);
				}
			}
			// For named variables, we can release the source register since the value is now in memory
			should_release_source = true;
		}
		else if (std::holds_alternative<TempVar>(ctx.result_value.value)) {
			auto res_var_op = std::get<TempVar>(ctx.result_value.value);
			auto res_stack_var_addr = getStackOffsetFromTempVar(res_var_op, ctx.result_value.size_in_bits);
			
			// Check if this is a reference - if so, we need to store through the pointer
			auto ref_it = reference_stack_info_.find(res_stack_var_addr);
			if (ref_it != reference_stack_info_.end()) {
				// This is a reference - load the pointer, then store the value through it
				X64Register ptr_reg = allocateRegisterWithSpilling();
				// Load the pointer into the register
				emitMovFromFrame(ptr_reg, res_stack_var_addr);
				// Now store the value through the pointer: [ptr_reg + 0] = actual_source_reg
				int value_size_bits = ref_it->second.value_size_bits;
				int value_size_bytes = value_size_bits / 8;
				emitStoreToMemory(textSectionData, actual_source_reg, ptr_reg, 0, value_size_bytes);
				regAlloc.release(ptr_reg);
				should_release_source = true;
			} else {
				// Not a reference, handle as before
				// IMPORTANT: Clear any stale register mappings for this stack variable BEFORE checking
				// This prevents using an old register value that was from a previous unrelated operation
				for (size_t i = 0; i < regAlloc.registers.size(); ++i) {
					auto& r = regAlloc.registers[i];
					if (r.stackVariableOffset == res_stack_var_addr && r.reg != actual_source_reg) {
						r.stackVariableOffset = INT_MIN; // Clear the mapping
						r.isDirty = false;
					}
				}
				
				if (auto res_reg = regAlloc.tryGetStackVariableRegister(res_stack_var_addr); res_reg.has_value()) {
					if (res_reg != actual_source_reg) {
						if (is_float_type) {
							// For float types, use SSE mov instructions for register-to-register moves
							// TODO: Implement SSE register-to-register moves if needed
							// For now, assert false since we shouldn't hit this path with current code
							assert(false && "Float register-to-register move not yet implemented");
						} else {
							auto moveFromRax = regAlloc.get_reg_reg_move_op_code(res_reg.value(), actual_source_reg, ctx.result_value.size_in_bits / 8);
							textSectionData.insert(textSectionData.end(), moveFromRax.op_codes.begin(), moveFromRax.op_codes.begin() + moveFromRax.size_in_bytes);
						}
					}
					// Result is already in the correct register, no move needed
					// For floating-point types, we MUST also write to memory even when register is correct
					// because the return handling will load from memory (XMM registers aren't fully tracked)
					if (is_float_type) {
						bool is_single_precision = (ctx.result_value.type == Type::Float);
						emitFloatMovToFrame(actual_source_reg, res_stack_var_addr, is_single_precision);
					} else {
						emitMovToFrameSized(
							SizedRegister{actual_source_reg, 64, false},
							SizedStackSlot{res_stack_var_addr, ctx.result_value.size_in_bits, isSignedType(ctx.result_value.type)}
						);
					}
					// Can release source register since result is now tracked in the destination register
					should_release_source = true;
				}
				else {
					// Temp variable not currently in a register - keep it in actual_source_reg instead of spilling
					// NOTE: The flushing of old register values is now handled in setupAndLoadArithmeticOperation
					// before the register is reassigned to the result TempVar's offset.
					
					// Tell the register allocator that this register now holds this temp variable
					assert(variable_scopes.back().scope_stack_space <= res_stack_var_addr);
					regAlloc.set_stack_variable_offset(actual_source_reg, res_stack_var_addr, ctx.result_value.size_in_bits);
					
					// For floating-point types, we MUST write to memory immediately because the register
					// allocator doesn't properly track XMM registers across all operations.
					// Without this, subsequent loads from the stack location will read garbage.
					if (is_float_type) {
						bool is_single_precision = (ctx.result_value.type == Type::Float);
						emitFloatMovToFrame(actual_source_reg, res_stack_var_addr, is_single_precision);
					} else {
						emitMovToFrameSized(
							SizedRegister{actual_source_reg, 64, false},
							SizedStackSlot{res_stack_var_addr, ctx.result_value.size_in_bits, isSignedType(ctx.result_value.type)}
						);
					}
					// Keep the value in the register for subsequent operations
					// DON'T release the source register for integer temps - keeping value in register for optimization
					should_release_source = false;
				}
			}

		}
		else {
			assert(false && "Unhandled destination type");
		}

		if (source_reg != X64Register::Count && should_release_source) {
			regAlloc.release(source_reg);
		}
	}

	// Group IR instructions by function for analysis
	void groupInstructionsByFunction(const Ir& ir) {
		function_spans.clear();
		std::string_view current_func_name;
		size_t current_func_start = 0;

		const auto& instructions = ir.getInstructions();

		for (size_t i = 0; i < instructions.size(); ++i) {
			const auto& instruction = instructions[i];
			if (instruction.getOpcode() == IrOpcode::FunctionDecl) {
				// Save previous function's span
				if (!current_func_name.empty()) {
					function_spans[std::string(current_func_name)] = std::span<const IrInstruction>(
						&instructions[current_func_start], i - current_func_start
					);
				}

				// Extract function name from typed payload
				const auto& func_decl = instruction.getTypedPayload<FunctionDeclOp>();
				// Use mangled name if available (for member functions like lambda operator()),
				// otherwise use function_name (Phase 4: Use helpers)
				StringHandle mangled_handle = func_decl.getMangledName();
				StringHandle func_name_handle = func_decl.getFunctionName();
				current_func_name = mangled_handle.handle != 0 ? StringTable::getStringView(mangled_handle) : StringTable::getStringView(func_name_handle);
				current_func_start = i + 1; // Instructions start after FunctionDecl
			}
		}

		// Save the last function's span
		if (!current_func_name.empty()) {
			function_spans[std::string(current_func_name)] = std::span<const IrInstruction>(
				&instructions[current_func_start], instructions.size() - current_func_start
			);
		}
	}

	// Calculate the total stack space needed for a function by analyzing its IR instructions
	struct StackSpaceSize {
		uint16_t temp_vars_size = 0;
		uint16_t named_vars_size = 0;
		uint16_t shadow_stack_space = 0;
		uint16_t outgoing_args_space = 0;  // Space for largest outgoing function call
	};
	struct VariableInfo {
		int offset = INT_MIN;  // Stack offset from RBP (INT_MIN = unallocated)
		int size_in_bits = 0;  // Size in bits
		bool is_array = false; // True if this is an array declaration (enables array-to-pointer decay in expressions and assignments)
	};

	struct StackVariableScope
	{
		int scope_stack_space = 0;
		std::unordered_map<StringHandle, VariableInfo> variables;  // Phase 5: StringHandle for integer-based lookups
	};

	struct ReferenceInfo {
		Type value_type = Type::Invalid;
		int value_size_bits = 0;
		bool is_rvalue_reference = false;
		// When true (e.g., AddressOf results), this TempVar holds a raw address/pointer value,
		// not a reference that should be implicitly dereferenced.
		bool holds_address_only = false;
	};
	
	// Helper function to set reference information in both storage systems
	// This ensures metadata stays synchronized between stack offset tracking and TempVar metadata
	void setReferenceInfo(int32_t stack_offset, Type value_type, int value_size_bits, bool is_rvalue_ref, TempVar temp_var = TempVar()) {
		// Always update the stack offset map (for named variables and legacy lookups)
		reference_stack_info_[stack_offset] = ReferenceInfo{
			.value_type = value_type,
			.value_size_bits = value_size_bits,
			.is_rvalue_reference = is_rvalue_ref,
			.holds_address_only = false
		};
		
		// If we have a valid TempVar, also update its metadata
		if (temp_var.var_number != 0) {
			setTempVarMetadata(temp_var, TempVarMetadata::makeReference(value_type, value_size_bits, is_rvalue_ref));
		}
	}
	
	// Helper function to check if a TempVar or stack offset is a reference
	// Checks TempVar metadata first (preferred), then falls back to stack offset lookup
	bool isReference(TempVar temp_var, int32_t stack_offset) const {
		// Check TempVar metadata first (more reliable, travels with the value)
		if (temp_var.var_number != 0 && isTempVarReference(temp_var)) {
			return true;
		}
		
		// Fall back to stack offset lookup (for named variables or legacy code)
		return reference_stack_info_.find(stack_offset) != reference_stack_info_.end();
	}
	
	// Helper function to get reference info for a TempVar or stack offset
	// Returns the reference info from TempVar metadata if available, otherwise from stack offset map
	std::optional<ReferenceInfo> getReferenceInfo(TempVar temp_var, int32_t stack_offset) const {
		// Check TempVar metadata first
		if (temp_var.var_number != 0 && isTempVarReference(temp_var)) {
			return ReferenceInfo{
				.value_type = getTempVarValueType(temp_var),
				.value_size_bits = getTempVarValueSizeBits(temp_var),
				.is_rvalue_reference = isTempVarRValueReference(temp_var),
				.holds_address_only = false
			};
		}
		
		// Fall back to stack offset lookup
		auto it = reference_stack_info_.find(stack_offset);
		if (it != reference_stack_info_.end()) {
			return it->second;
		}
		
		return std::nullopt;
	}
	
	StackSpaceSize calculateFunctionStackSpace(std::string_view func_name, StackVariableScope& var_scope, size_t param_count) {
		StackSpaceSize func_stack_space{};

		auto it = function_spans.find(std::string(func_name));
		if (it == function_spans.end()) {
			return func_stack_space; // No instructions found for this function
		}

		struct VarDecl {
			StringHandle var_name{};  // Phase 5: StringHandle for efficient storage
			int size_in_bits{};
			size_t alignment{};  // Custom alignment from alignas(n), 0 = use natural alignment
			bool is_array{};     // True if this variable is an array (for array-to-pointer decay)
		};
		std::vector<VarDecl> local_vars;

		// Clear temp_var_sizes for this function
		temp_var_sizes_.clear();
		
		// Track maximum outgoing call argument space needed
		size_t max_outgoing_arg_bytes = 0;

		for (const auto& instruction : it->second) {
			// Look for TempVar operands in the instruction
			func_stack_space.shadow_stack_space |= (0x20 * !(instruction.getOpcode() != IrOpcode::FunctionCall));
			
			// Track outgoing call argument space
			if (instruction.getOpcode() == IrOpcode::FunctionCall && instruction.hasTypedPayload()) {
				if (const CallOp* call_op = std::any_cast<CallOp>(&instruction.getTypedPayload())) {
					// For Windows variadic calls: ALL args on stack starting at RSP+0
					// For Windows normal calls: Args beyond 4 on stack starting at RSP+32 (shadow space)
					// For Linux: Args beyond 6 on stack starting at RSP+0
					constexpr bool is_coff_format = !std::is_same_v<TWriterClass, ElfFileWriter>;
					size_t arg_count = call_op->args.size();
					size_t outgoing_bytes = 0;
					
					if (is_coff_format) {
						if (call_op->is_variadic) {
							// Windows variadic: ALL args on stack, starting at RSP+0
							outgoing_bytes = arg_count * 8;
						} else {
							// Windows normal: First 4 in registers, rest on stack starting at RSP+32
							if (arg_count > 4) {
								outgoing_bytes = 32 + (arg_count - 4) * 8;
							} else {
								outgoing_bytes = 32;  // Shadow space even if all args in registers
							}
						}
					} else {
						// Linux: First 6 in registers, rest on stack starting at RSP+0
						if (arg_count > 6) {
							outgoing_bytes = (arg_count - 6) * 8;
						}
						// No shadow space on Linux
					}
					
					if (outgoing_bytes > max_outgoing_arg_bytes) {
						max_outgoing_arg_bytes = outgoing_bytes;
					}
				}
			}

			if (instruction.getOpcode() == IrOpcode::VariableDecl) {
				const VariableDeclOp& op = std::any_cast<const VariableDeclOp&>(instruction.getTypedPayload());
				auto size_in_bits = op.size_in_bits;
				// Get variable name (Phase 4: Use helper)
				std::string_view var_name = op.getVarName();
				size_t custom_alignment = op.custom_alignment;

				bool is_reference = op.is_reference;
				bool is_array = op.is_array;
				int total_size_bits = size_in_bits;
				if (is_reference) {
					total_size_bits = 64;
				}
				if (is_array && op.array_count.has_value()) {
					uint64_t array_size = op.array_count.value();
					total_size_bits = size_in_bits * static_cast<int>(array_size);
				}
				
				func_stack_space.named_vars_size += (total_size_bits / 8);
				// Phase 5: Store StringHandle directly for efficient variable tracking
				local_vars.push_back(VarDecl{ .var_name = StringTable::getOrInternStringHandle(var_name), .size_in_bits = total_size_bits, .alignment = custom_alignment, .is_array = is_array });
			}
			else {
				// Track TempVars and their sizes from typed payloads or legacy operand format
				bool handled_by_typed_payload = false;
				
				// For typed payload instructions, try common payload types
				if (instruction.hasTypedPayload()) {
					try {
						// Try BinaryOp (arithmetic, comparisons, logic)
						if (const BinaryOp* bin_op = std::any_cast<BinaryOp>(&instruction.getTypedPayload())) {
							if (std::holds_alternative<TempVar>(bin_op->result)) {
								auto temp_var = std::get<TempVar>(bin_op->result);
								// Phase 5: Convert temp var name to StringHandle
								// For comparison operations, result is always bool (8 bits)
								// For arithmetic/logical operations, result size matches operand size
								auto opcode = instruction.getOpcode();
								bool is_comparison = (opcode == IrOpcode::Equal || opcode == IrOpcode::NotEqual ||
								                      opcode == IrOpcode::LessThan || opcode == IrOpcode::LessEqual ||
								                      opcode == IrOpcode::GreaterThan || opcode == IrOpcode::GreaterEqual ||
								                      opcode == IrOpcode::UnsignedLessThan || opcode == IrOpcode::UnsignedLessEqual ||
								                      opcode == IrOpcode::UnsignedGreaterThan || opcode == IrOpcode::UnsignedGreaterEqual ||
								                      opcode == IrOpcode::FloatEqual || opcode == IrOpcode::FloatNotEqual ||
								                      opcode == IrOpcode::FloatLessThan || opcode == IrOpcode::FloatLessEqual ||
								                      opcode == IrOpcode::FloatGreaterThan || opcode == IrOpcode::FloatGreaterEqual);
								int result_size = is_comparison ? 8 : bin_op->lhs.size_in_bits;
								temp_var_sizes_[StringTable::getOrInternStringHandle(temp_var.name())] = result_size;
								handled_by_typed_payload = true;
							}
						}
						// Try UnaryOp (logical not, bitwise not, negate)
						else if (const UnaryOp* unary_op = std::any_cast<UnaryOp>(&instruction.getTypedPayload())) {
							// Phase 5: Convert temp var name to StringHandle
							// For logical not, result is always bool (8 bits)
							// For bitwise not and negate, result size matches operand size
							temp_var_sizes_[StringTable::getOrInternStringHandle(unary_op->result.name())] = unary_op->value.size_in_bits;
							handled_by_typed_payload = true;
						}
						// Try CallOp (function calls)
						else if (const CallOp* call_op = std::any_cast<CallOp>(&instruction.getTypedPayload())) {
							// Phase 5: Convert temp var name to StringHandle
							temp_var_sizes_[StringTable::getOrInternStringHandle(call_op->result.name())] = call_op->return_size_in_bits;
							handled_by_typed_payload = true;
						}
						// Try ArrayAccessOp (array element load)
						else if (const ArrayAccessOp* array_op = std::any_cast<ArrayAccessOp>(&instruction.getTypedPayload())) {
							// Phase 5: Convert temp var name to StringHandle
							temp_var_sizes_[StringTable::getOrInternStringHandle(array_op->result.name())] = array_op->element_size_in_bits;
							handled_by_typed_payload = true;
						}
						// Try ArrayElementAddressOp (get address of array element)
						else if (const ArrayElementAddressOp* addr_op = std::any_cast<ArrayElementAddressOp>(&instruction.getTypedPayload())) {
							// Phase 5: Convert temp var name to StringHandle
							temp_var_sizes_[StringTable::getOrInternStringHandle(addr_op->result.name())] = 64; // Pointer is always 64-bit
							handled_by_typed_payload = true;
						}
						// Try DereferenceOp (for dereferencing pointers/references)
						else if (const DereferenceOp* deref_op = std::any_cast<DereferenceOp>(&instruction.getTypedPayload())) {
							// Phase 5: Convert temp var name to StringHandle
							// Determine size based on pointer depth: if depth > 1, result is a pointer (64 bits)
							int result_size = (deref_op->pointer.pointer_depth > 1) ? 64 : deref_op->pointer.size_in_bits;
							temp_var_sizes_[StringTable::getOrInternStringHandle(deref_op->result.name())] = result_size;
							handled_by_typed_payload = true;
						}
						// Try AssignmentOp (for materializing literals to temporaries)
						else if (const AssignmentOp* assign_op = std::any_cast<AssignmentOp>(&instruction.getTypedPayload())) {
							// Track the LHS TempVar if it's a TempVar
							if (std::holds_alternative<TempVar>(assign_op->lhs.value)) {
								auto temp_var = std::get<TempVar>(assign_op->lhs.value);
								// Phase 5: Convert temp var name to StringHandle
								temp_var_sizes_[StringTable::getOrInternStringHandle(temp_var.name())] = assign_op->lhs.size_in_bits;
								handled_by_typed_payload = true;
							}
						}
						// Try AddressOfOp (for taking address of temporaries)
						else if (const AddressOfOp* addr_of_op = std::any_cast<AddressOfOp>(&instruction.getTypedPayload())) {
							// Phase 5: Convert temp var name to StringHandle
							temp_var_sizes_[StringTable::getOrInternStringHandle(addr_of_op->result.name())] = 64; // Pointer is always 64-bit
							handled_by_typed_payload = true;
						}
						// Try GlobalLoadOp (for loading global variables)
						else if (const GlobalLoadOp* global_load_op = std::any_cast<GlobalLoadOp>(&instruction.getTypedPayload())) {
							if (std::holds_alternative<TempVar>(global_load_op->result.value)) {
								auto temp_var = std::get<TempVar>(global_load_op->result.value);
								temp_var_sizes_[StringTable::getOrInternStringHandle(temp_var.name())] = global_load_op->result.size_in_bits;
								handled_by_typed_payload = true;
							}
						}
						// Add more payload types here as they produce TempVars
					} catch (const std::exception& e) {
						FLASH_LOG(Codegen, Warning, "[calculateFunctionStackSpace]: Exception while processing typed payload for opcode ", 
						          static_cast<int>(instruction.getOpcode()), ": ", e.what());
					} catch (...) {
						FLASH_LOG(Codegen, Warning, "[calculateFunctionStackSpace]: Unknown exception while processing typed payload for opcode ", 
						          static_cast<int>(instruction.getOpcode()));
					}
				}
				
				// Fallback: Track TempVars from legacy operand format
				// Most arithmetic/logic instructions have format: [result_var, type, size, ...]
				// where operand 0 is result, operand 1 is type, operand 2 is size
				if (!handled_by_typed_payload && 
				    instruction.getOperandCount() >= 3 && 
				    instruction.isOperandType<TempVar>(0) &&
				    instruction.isOperandType<int>(2)) {
					auto temp_var = instruction.getOperandAs<TempVar>(0);
					int size_in_bits = instruction.getOperandAs<int>(2);
					temp_var_sizes_[StringTable::getOrInternStringHandle(temp_var.name())] = size_in_bits;
				}
			}
		}

		// TempVars are now allocated dynamically via formula, not pre-allocated
		
		// Start stack allocation AFTER parameter home space
		// Windows x64 ABI: first 4 parameters get home space at [rbp-8], [rbp-16], [rbp-24], [rbp-32]
		// Additional parameters are passed on the stack at positive RBP offsets
		// Local variables start AFTER the parameter home space
		int param_home_space = std::max(static_cast<int>(param_count), 4) * 8;  // At least 32 bytes for register parameters
		int_fast32_t stack_offset = -param_home_space;
		
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

			// Store both offset and size in unified structure, including is_array flag
			var_scope.variables.insert_or_assign(local_var.var_name, VariableInfo{static_cast<int>(stack_offset), local_var.size_in_bits, local_var.is_array});
		}

		// Calculate space needed for TempVars
		// Each TempVar uses 8 bytes (64-bit alignment)
		// Calculate space for temp vars using actual sizes, not just count * 8
		int temp_var_space = 0;
		for (const auto& [temp_var_name, size_bits] : temp_var_sizes_) {
			int size_in_bytes = (size_bits + 7) / 8;
			size_in_bytes = (size_in_bytes + 7) & ~7;  // 8-byte alignment
			temp_var_space += size_in_bytes;
		}

		// Don't subtract from stack_offset - TempVars are allocated separately via getStackOffsetFromTempVar

		// Store TempVar sizes for later use during code generation
		// TempVars will have their offsets set when actually allocated via getStackOffsetFromTempVar
		// Use INT_MIN as a sentinel value to indicate "not yet allocated"
		for (const auto& [temp_var_name, size_bits] : temp_var_sizes_) {
			// Initialize with sentinel offset (INT_MIN), actual offset set later
			var_scope.variables.insert_or_assign(temp_var_name, VariableInfo{INT_MIN, size_bits});
		}

		// Calculate total stack space needed
		func_stack_space.temp_vars_size = temp_var_space;  // TempVar space (added to total separately)
		func_stack_space.named_vars_size = -stack_offset;  // Just named variables space
		func_stack_space.outgoing_args_space = static_cast<uint16_t>(max_outgoing_arg_bytes);  // Outgoing call argument space
		
		// if we are a leaf function (don't call other functions), we can get by with just register if we don't have more than 8 * 64 bytes of values to store
		//if (shadow_stack_space == 0 && max_temp_var_index <= 8) {
			//return 0;
		//}

		return func_stack_space;
	}

	// Helper function to get or reserve a stack slot for a temporary variable.
	// This is now just a thin wrapper around getStackOffsetFromTempVar which 
	// handles stack space tracking and offset registration.
	int allocateStackSlotForTempVar(int32_t index, int size_in_bits = 64) {
		TempVar tempVar(index);
		return getStackOffsetFromTempVar(tempVar, size_in_bits);
	}

	// Get stack offset for a TempVar using formula-based allocation.
	// TempVars are allocated within the pre-allocated temp_vars space.
	// The space starts after named_vars + shadow_space.
	// 
	// This function also:
	// - Extends scope_stack_space if the offset exceeds current tracked allocation
	// - Registers the TempVar in variables for consistent subsequent lookups
	int32_t getStackOffsetFromTempVar(TempVar tempVar, int size_in_bits = 64) {
		// Check if this TempVar was pre-allocated (named variables or previously computed TempVars)
		if (!variable_scopes.empty()) {
			StringHandle lookup_handle = StringTable::getOrInternStringHandle(tempVar.name());
			auto& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(lookup_handle);
			if (it != current_scope.variables.end() && it->second.offset != INT_MIN) {
				int existing_offset = it->second.offset;
				
				// Check if we need to extend the allocation for a larger size
				// This can happen when a TempVar is first allocated with default size,
				// then later used for a large struct (e.g., constructor call result)
				int size_in_bytes = (size_in_bits + 7) / 8;
				size_in_bytes = (size_in_bytes + 7) & ~7;  // 8-byte alignment
				
				int32_t end_offset = existing_offset - size_in_bytes;
				if (end_offset < variable_scopes.back().scope_stack_space) {
					FLASH_LOG_FORMAT(Codegen, Debug,
						"Extending scope_stack_space from {} to {} for pre-allocated {} (offset={}, size={})",
						variable_scopes.back().scope_stack_space, end_offset, tempVar.name(), existing_offset, size_in_bytes);
					variable_scopes.back().scope_stack_space = end_offset;
				}
				
				FLASH_LOG_FORMAT(Codegen, Debug,
					"TempVar {} already allocated at offset {}, size={} bytes", 
					tempVar.name(), existing_offset, size_in_bytes);
				return existing_offset;  // Use pre-allocated offset (if it's been properly set)
			}
			
			// CRITICAL FIX: If TempVar entry has INT_MIN, check if it corresponds to the most recently
			// allocated named variable (tracked in handleVariableDecl)
			// This handles the duplicate entry problem where named variables get both a name entry
			// and a TempVar entry
			if (it != variable_scopes.back().variables.end() && it->second.offset == INT_MIN) {
				if (last_allocated_variable_name_.isValid() && last_allocated_variable_offset_ != 0) {
					// Use the last allocated variable's offset for this TempVar
					// Update the TempVar entry so future lookups are O(1)
					it->second.offset = last_allocated_variable_offset_;
					return last_allocated_variable_offset_;
				}
			}
		}
		// Allocate TempVars sequentially after named_vars + shadow space
		// Use next_temp_var_offset_ to track the next available slot
		// Each TempVar gets size_in_bits bytes (rounded up to 8-byte alignment)
		// Check temp_var_sizes_ for pre-calculated size (from calculateFunctionStackSpace)
		// This ensures large struct returns are allocated with correct size from the start
		StringHandle temp_var_handle = StringTable::getOrInternStringHandle(tempVar.name());
		auto size_it = temp_var_sizes_.find(temp_var_handle);
		int actual_size_in_bits = size_in_bits;
		if (size_it != temp_var_sizes_.end() && size_it->second > size_in_bits) {
			actual_size_in_bits = size_it->second;  // Use pre-calculated size if larger
		}
		
		int size_in_bytes = (actual_size_in_bits + 7) / 8;  // Round up to nearest byte
		size_in_bytes = (size_in_bytes + 7) & ~7;    // Round up to 8-byte alignment
		
		// Advance next_temp_var_offset_ FIRST to reserve space for this allocation
		// This ensures large structs don't overlap with previously allocated variables
		// The offset points to the BASE of the struct (lowest address), and the struct
		// extends UPWARD in memory by size_in_bytes
		next_temp_var_offset_ += size_in_bytes;
		int32_t offset = -(static_cast<int32_t>(current_function_named_vars_size_) + next_temp_var_offset_);
		
		// Track the maximum TempVar index for stack size calculation
		if (tempVar.var_number > max_temp_var_index_) {
			max_temp_var_index_ = tempVar.var_number;
		}

		// Extend scope_stack_space if the computed offset exceeds current allocation
		// This ensures assertions checking scope_stack_space <= offset remain valid
		// NOTE: offset is the START of the allocation (highest address), but large structs extend downward
		// A struct at offset -40 with size 120 bytes occupies addresses from -40 down to -160
		int32_t end_offset = offset - size_in_bytes;  // Struct extends from offset down by size_in_bytes
		if (end_offset < variable_scopes.back().scope_stack_space) {
			FLASH_LOG_FORMAT(Codegen, Debug,
				"Extending scope_stack_space from {} to {} for {} (offset={}, size={})",
				variable_scopes.back().scope_stack_space, end_offset, tempVar.name(), offset, size_in_bytes);
			variable_scopes.back().scope_stack_space = end_offset;
		}
		
		// Register the TempVar's offset in variables map so subsequent lookups
		// return the same offset even if scope_stack_space changes
		// Note: temp_var_handle was already created above for the size lookup
		variable_scopes.back().variables[temp_var_handle].offset = offset;
		
		return offset;
	}

	void flushAllDirtyRegisters()
	{
		regAlloc.flushAllDirtyRegisters([this](X64Register reg, int32_t stackVariableOffset, int size_in_bits)
			{
				// Always flush dirty registers to stack, regardless of offset alignment.
				// This fixes the register flush bug where non-8-byte-aligned offsets
				// (from structured bindings) would cause getTempVarFromOffset to return
				// nullopt, preventing the register from being flushed.
				
				// Note: stackVariableOffset should be within allocated space (scope_stack_space <= stackVariableOffset <= 0)
				// However, during code generation, constructors may create additional TempVars beyond pre-calculated space.
				// Extend scope_stack_space dynamically if needed.
				if (stackVariableOffset < variable_scopes.back().scope_stack_space) {
					variable_scopes.back().scope_stack_space = stackVariableOffset;
				}
				assert(variable_scopes.back().scope_stack_space <= stackVariableOffset && stackVariableOffset <= 0);

				// Store the computed result from register to stack using size-appropriate MOV
				emitMovToFrameSized(
					SizedRegister{reg, 64, false},  // source: 64-bit register
					SizedStackSlot{stackVariableOffset, size_in_bits, false}  // dest: sized stack slot
				);
			});
	}

	// Helper to generate and emit size-appropriate MOV to frame (legacy - prefer emitMovToFrameSized)
	void emitMovToFrameBySize(X64Register sourceRegister, int32_t offset, int size_in_bits) {
		auto opcodes = generateMovToFrameBySize(sourceRegister, offset, size_in_bits);
		textSectionData.insert(textSectionData.end(), opcodes.op_codes.begin(), opcodes.op_codes.begin() + opcodes.size_in_bytes);
	}

	// Helper to generate and emit size-aware MOV to frame
	// Takes SizedRegister for source (register + size) and SizedStackSlot for destination (offset + size)
	// This makes both source and destination sizes explicit for clarity
	void emitMovToFrameSized(SizedRegister source, SizedStackSlot dest) {
		OpCodeWithSize opcodes;
		
		// Check if source is an XMM register (enum values >= 16)
		bool is_xmm_source = static_cast<uint8_t>(source.reg) >= 16;
		
		// Use the destination size to determine the store instruction
		if (dest.size_in_bits == 64) {
			if (is_xmm_source) {
				// For XMM registers, use MOVSD (double) instruction
				opcodes = generateFloatMovToFrame(source.reg, dest.offset, false);
			} else {
				opcodes = generatePtrMovToFrame(source.reg, dest.offset);
			}
		} else if (dest.size_in_bits == 32) {
			if (is_xmm_source) {
				// For XMM registers, use MOVSS (float) instruction
				opcodes = generateFloatMovToFrame(source.reg, dest.offset, true);
			} else {
				opcodes = generateMovToFrame32(source.reg, dest.offset);
			}
		} else if (dest.size_in_bits == 16) {
			opcodes = generateMovToFrame16(source.reg, dest.offset);
		} else { // 8-bit
			opcodes = generateMovToFrame8(source.reg, dest.offset);
		}
		
		// Insert opcodes into text section
		if (opcodes.size_in_bytes > 0 && opcodes.size_in_bytes <= MAX_MOV_INSTRUCTION_SIZE) {
			textSectionData.insert(textSectionData.end(), opcodes.op_codes.begin(), opcodes.op_codes.begin() + opcodes.size_in_bytes);
		}
	}

	// Helper to generate and emit size-appropriate MOV from frame
	void emitMovFromFrameBySize(X64Register destinationRegister, int32_t offset, int size_in_bits) {
		auto opcodes = generateMovFromFrameBySize(destinationRegister, offset, size_in_bits);
		textSectionData.insert(textSectionData.end(), opcodes.op_codes.begin(), opcodes.op_codes.begin() + opcodes.size_in_bytes);
	}

	// Helper to generate and emit 64-bit MOV from frame (for pointers/references)
	void emitMovFromFrame(X64Register destinationRegister, int32_t offset) {
		auto opcodes = generateMovFromFrameBySize(destinationRegister, offset, 64);
		textSectionData.insert(textSectionData.end(), opcodes.op_codes.begin(), opcodes.op_codes.begin() + opcodes.size_in_bytes);
	}

	// Helper to generate and emit pointer MOV from frame
	void emitPtrMovFromFrame(X64Register destinationRegister, int32_t offset) {
		auto opcodes = generatePtrMovFromFrame(destinationRegister, offset);
		textSectionData.insert(textSectionData.end(), opcodes.op_codes.begin(), opcodes.op_codes.begin() + opcodes.size_in_bytes);
	}

	// Helper to generate and emit size-aware MOV from frame
	// Takes SizedRegister for destination (register + size) and SizedStackSlot for source (offset + size)
	// This makes both source and destination sizes explicit for clarity
	void emitMovFromFrameSized(SizedRegister dest, SizedStackSlot source) {
		OpCodeWithSize opcodes;
		
		// Currently, x64 registers always load to 64-bit (using sign/zero extension)
		// The dest.size_in_bits indicates what portion of the register is meaningful
		// but the actual load always goes to the full 64-bit register
		
		if (source.size_in_bits == 64) {
			opcodes = generatePtrMovFromFrame(dest.reg, source.offset);
		} else if (source.size_in_bits == 32) {
			if (source.is_signed) {
				opcodes = generateMovsxdFromFrame_32to64(dest.reg, source.offset);
			} else {
				opcodes = generateMovFromFrame32(dest.reg, source.offset);
			}
		} else if (source.size_in_bits == 16) {
			if (source.is_signed) {
				opcodes = generateMovsxFromFrame_16to64(dest.reg, source.offset);
			} else {
				opcodes = generateMovzxFromFrame16(dest.reg, source.offset);
			}
		} else { // 8-bit
			if (source.is_signed) {
				opcodes = generateMovsxFromFrame_8to64(dest.reg, source.offset);
			} else {
				opcodes = generateMovzxFromFrame8(dest.reg, source.offset);
			}
		}
		
		textSectionData.insert(textSectionData.end(), opcodes.op_codes.begin(), opcodes.op_codes.begin() + opcodes.size_in_bytes);
	}

	// Helper to generate and emit LEA from frame
	void emitLeaFromFrame(X64Register destinationRegister, int32_t offset) {
		auto opcodes = generateLeaFromFrame(destinationRegister, offset);
		textSectionData.insert(textSectionData.end(), opcodes.op_codes.begin(), opcodes.op_codes.begin() + opcodes.size_in_bytes);
	}

	// Helper to emit RIP-relative LEA for loading symbol addresses
	// Returns the offset where the relocation displacement should be added
	uint32_t emitLeaRipRelative(X64Register destinationRegister) {
		// LEA reg, [RIP + disp32]
		textSectionData.push_back(0x48); // REX.W for 64-bit
		textSectionData.push_back(0x8D); // LEA opcode
		
		// ModR/M byte: mod=00 (indirect), reg=destination, r/m=101 ([RIP+disp32])
		uint8_t dest_bits = static_cast<uint8_t>(destinationRegister) & 0x07;
		textSectionData.push_back(0x05 | (dest_bits << 3)); // ModR/M: [RIP + disp32]
		
		// Add placeholder for the displacement (will be filled by relocation)
		uint32_t relocation_offset = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		
		return relocation_offset;
	}

	// Helper to generate and emit MOV to frame (64-bit pointers/references)
	void emitMovToFrame(X64Register sourceRegister, int32_t offset) {
		auto opcodes = generateMovToFrameBySize(sourceRegister, offset, 64);
		std::string bytes_str;
		for (size_t i = 0; i < static_cast<size_t>(opcodes.size_in_bytes); i++) {
			bytes_str += std::format("{:02x} ", static_cast<uint8_t>(opcodes.op_codes[i]));
		}
		FLASH_LOG_FORMAT(Codegen, Debug, "emitMovToFrame: reg={} offset={} bytes={}", static_cast<int>(sourceRegister), offset, bytes_str);
		textSectionData.insert(textSectionData.end(), opcodes.op_codes.begin(), opcodes.op_codes.begin() + opcodes.size_in_bytes);
	}

	// Helper to emit MOVQ from XMM to GPR (for varargs: float args must be in both XMM and INT regs)
	// movq r64, xmm: 66 REX.W 0F 7E /r
	void emitMovqXmmToGpr(X64Register xmm_src, X64Register gpr_dest) {
		uint8_t xmm_val = static_cast<uint8_t>(xmm_src);
		uint8_t gpr_val = static_cast<uint8_t>(gpr_dest);
		// XMM registers are encoded as 16+ in our enum, so subtract 16 to get 0-15 range
		uint8_t xmm_idx = (xmm_val >= 16) ? (xmm_val - 16) : xmm_val;
		// Branchless REX: REX.W=1, REX.R from XMM high bit, REX.B from GPR high bit
		uint8_t rex = 0x48 | ((xmm_idx >> 3) << 2) | (gpr_val >> 3);
		uint8_t xmm_bits = xmm_idx & 0x07;
		uint8_t gpr_bits = gpr_val & 0x07;
		textSectionData.push_back(0x66);
		textSectionData.push_back(rex);
		textSectionData.push_back(0x0F);
		textSectionData.push_back(0x7E);
		// ModR/M: mod=11 (register), reg=xmm, r/m=gpr
		textSectionData.push_back(0xC0 | (xmm_bits << 3) | gpr_bits);
	}

	// Helper to emit MOVQ from GPR to XMM (for moving float bits to XMM register)
	// movq xmm, r64: 66 REX.W 0F 6E /r
	void emitMovqGprToXmm(X64Register gpr_src, X64Register xmm_dest) {
		uint8_t gpr_val = static_cast<uint8_t>(gpr_src);
		uint8_t xmm_val = static_cast<uint8_t>(xmm_dest);
		// XMM registers are encoded as 16+ in our enum, so subtract 16 to get 0-15 range
		uint8_t xmm_idx = (xmm_val >= 16) ? (xmm_val - 16) : xmm_val;
		// Branchless REX: REX.W=1, REX.R from XMM high bit, REX.B from GPR high bit
		uint8_t rex = 0x48 | ((xmm_idx >> 3) << 2) | (gpr_val >> 3);
		uint8_t xmm_bits = xmm_idx & 0x07;
		uint8_t gpr_bits = gpr_val & 0x07;
		textSectionData.push_back(0x66);
		textSectionData.push_back(rex);
		textSectionData.push_back(0x0F);
		textSectionData.push_back(0x6E);
		// ModR/M: mod=11 (register), reg=xmm, r/m=gpr
		textSectionData.push_back(0xC0 | (xmm_bits << 3) | gpr_bits);
	}

	// Helper to emit CVTSS2SD (convert float to double in XMM register)
	// For varargs: floats are promoted to double before passing
	// cvtss2sd xmm, xmm: F3 0F 5A /r
	void emitCvtss2sd(X64Register xmm_dest, X64Register xmm_src) {
		textSectionData.push_back(0xF3);
		textSectionData.push_back(0x0F);
		textSectionData.push_back(0x5A);
		uint8_t dest_bits = static_cast<uint8_t>(xmm_dest) & 0x07;
		uint8_t src_bits = static_cast<uint8_t>(xmm_src) & 0x07;
		uint8_t modrm = 0xC0 | (dest_bits << 3) | src_bits;
		textSectionData.push_back(modrm);
	}

	// Helper to generate and emit float MOV from frame (movss/movsd)
	void emitFloatMovFromFrame(X64Register destinationRegister, int32_t offset, bool is_float) {
		auto opcodes = generateFloatMovFromFrame(destinationRegister, offset, is_float);
		textSectionData.insert(textSectionData.end(), opcodes.op_codes.begin(), opcodes.op_codes.begin() + opcodes.size_in_bytes);
	}

	void emitFloatMovToFrame(X64Register sourceRegister, int32_t offset, bool is_float) {
		auto opcodes = generateFloatMovToFrame(sourceRegister, offset, is_float);
		textSectionData.insert(textSectionData.end(), opcodes.op_codes.begin(), opcodes.op_codes.begin() + opcodes.size_in_bytes);
	}

	// Helper to emit MOVSS/MOVSD from memory [reg + offset] into XMM register
	void emitFloatMovFromMemory(X64Register xmm_dest, X64Register base_reg, int32_t offset, bool is_float) {
		// Assert that xmm_dest is an XMM register
		assert(static_cast<uint8_t>(xmm_dest) >= 16 && static_cast<uint8_t>(xmm_dest) < 32 && 
		       "emitFloatMovFromMemory requires XMM destination register (XMM0-XMM15)");
		// Assert that base_reg is NOT an XMM register
		assert(static_cast<uint8_t>(base_reg) < 16 && 
		       "emitFloatMovFromMemory requires non-XMM base register");
		
		// Generate the float mov instruction from memory
		auto opcodes = generateFloatMovFromMemory(xmm_dest, base_reg, offset, is_float);
		textSectionData.insert(textSectionData.end(), opcodes.op_codes.begin(), opcodes.op_codes.begin() + opcodes.size_in_bytes);
	}

	// Helper to emit MOVDQU (unaligned 128-bit move) from XMM register to frame
	// Used for saving full XMM registers in variadic function register save areas
	void emitMovdquToFrame(X64Register xmm_src, int32_t offset) {
		// Assert that xmm_src is actually an XMM register (16-31 for XMM0-XMM15)
		assert(static_cast<uint8_t>(xmm_src) >= 16 && static_cast<uint8_t>(xmm_src) < 32 && 
		       "emitMovdquToFrame requires XMM register (XMM0-XMM15)");
		uint8_t xmm_idx = xmm_modrm_bits(xmm_src);
		
		// MOVDQU [RBP + offset], xmm: F3 0F 7F /r
		textSectionData.push_back(0xF3);  // movdqu prefix
		if (xmm_idx >= 8) {
			textSectionData.push_back(0x44);  // REX.R for XMM8-15
		}
		textSectionData.push_back(0x0F);
		textSectionData.push_back(0x7F);  // movdqu [mem], xmm
		
		// Encode [RBP + offset]
		if (offset >= -128 && offset <= 127) {
			uint8_t modrm = 0x45 | ((xmm_idx & 0x07) << 3);  // Mod=01, Reg=XMM, R/M=101 (RBP)
			textSectionData.push_back(modrm);
			textSectionData.push_back(static_cast<uint8_t>(offset));
		} else {
			uint8_t modrm = 0x85 | ((xmm_idx & 0x07) << 3);  // Mod=10, Reg=XMM, R/M=101 (RBP)
			textSectionData.push_back(modrm);
			textSectionData.push_back((offset >> 0) & 0xFF);
			textSectionData.push_back((offset >> 8) & 0xFF);
			textSectionData.push_back((offset >> 16) & 0xFF);
			textSectionData.push_back((offset >> 24) & 0xFF);
		}
	}

	// Helper to emit MOV DWORD PTR [reg + offset], imm32
	void emitMovDwordPtrImmToRegOffset(X64Register base_reg, int32_t offset, uint32_t imm32) {
		// Assert that base_reg is NOT an XMM register
		assert(static_cast<uint8_t>(base_reg) < 16 && 
		       "emitMovDwordPtrImmToRegOffset requires non-XMM base register");
		// MOV r/m32, imm32: C7 /0
		textSectionData.push_back(0xC7);
		
		// ModR/M and offset encoding
		uint8_t base_bits = static_cast<uint8_t>(base_reg) & 0x07;
		if (offset == 0 && base_reg != X64Register::RBP && base_reg != X64Register::R13) {
			// Mod=00, no displacement
			textSectionData.push_back(0x00 | base_bits);
		} else if (offset >= -128 && offset <= 127) {
			// Mod=01, 8-bit displacement
			textSectionData.push_back(0x40 | base_bits);
			textSectionData.push_back(static_cast<uint8_t>(offset));
		} else {
			// Mod=10, 32-bit displacement
			textSectionData.push_back(0x80 | base_bits);
			textSectionData.push_back((offset >> 0) & 0xFF);
			textSectionData.push_back((offset >> 8) & 0xFF);
			textSectionData.push_back((offset >> 16) & 0xFF);
			textSectionData.push_back((offset >> 24) & 0xFF);
		}
		
		// Immediate value (32-bit little-endian)
		textSectionData.push_back((imm32 >> 0) & 0xFF);
		textSectionData.push_back((imm32 >> 8) & 0xFF);
		textSectionData.push_back((imm32 >> 16) & 0xFF);
		textSectionData.push_back((imm32 >> 24) & 0xFF);
	}

	// Helper to emit MOV QWORD PTR [reg + offset], imm32 (sign-extended to 64-bit)
	void emitMovQwordPtrImmToRegOffset(X64Register base_reg, int32_t offset, uint32_t imm32) {
		// Assert that base_reg is NOT an XMM register
		assert(static_cast<uint8_t>(base_reg) < 16 && 
		       "emitMovQwordPtrImmToRegOffset requires non-XMM base register");
		// REX.W prefix for 64-bit operation
		textSectionData.push_back(0x48);
		
		// MOV r/m64, imm32: C7 /0 (imm32 is sign-extended to 64-bit)
		textSectionData.push_back(0xC7);
		
		// ModR/M and offset encoding
		uint8_t base_bits = static_cast<uint8_t>(base_reg) & 0x07;
		if (offset == 0 && base_reg != X64Register::RBP && base_reg != X64Register::R13) {
			// Mod=00, no displacement
			textSectionData.push_back(0x00 | base_bits);
		} else if (offset >= -128 && offset <= 127) {
			// Mod=01, 8-bit displacement
			textSectionData.push_back(0x40 | base_bits);
			textSectionData.push_back(static_cast<uint8_t>(offset));
		} else {
			// Mod=10, 32-bit displacement
			textSectionData.push_back(0x80 | base_bits);
			textSectionData.push_back((offset >> 0) & 0xFF);
			textSectionData.push_back((offset >> 8) & 0xFF);
			textSectionData.push_back((offset >> 16) & 0xFF);
			textSectionData.push_back((offset >> 24) & 0xFF);
		}
		
		// Immediate value (32-bit little-endian, will be sign-extended)
		textSectionData.push_back((imm32 >> 0) & 0xFF);
		textSectionData.push_back((imm32 >> 8) & 0xFF);
		textSectionData.push_back((imm32 >> 16) & 0xFF);
		textSectionData.push_back((imm32 >> 24) & 0xFF);
	}

	// Helper to emit MOV QWORD PTR [reg + offset], src_reg
	void emitMovQwordPtrRegToRegOffset(X64Register base_reg, int32_t offset, X64Register src_reg) {
		// Assert that base_reg is NOT an XMM register
		assert(static_cast<uint8_t>(base_reg) < 16 && 
		       "emitMovQwordPtrRegToRegOffset requires non-XMM base register");
		// Assert that src_reg is NOT an XMM register
		assert(static_cast<uint8_t>(src_reg) < 16 && 
		       "emitMovQwordPtrRegToRegOffset requires non-XMM source register");
		// REX.W prefix for 64-bit operation
		uint8_t rex = 0x48;
		if (static_cast<uint8_t>(src_reg) >= 8) rex |= 0x04;  // REX.R if src is R8-R15
		if (static_cast<uint8_t>(base_reg) >= 8) rex |= 0x01; // REX.B if base is R8-R15
		textSectionData.push_back(rex);
		
		// MOV r/m64, r64: 89 /r
		textSectionData.push_back(0x89);
		
		// ModR/M encoding
		uint8_t src_bits = (static_cast<uint8_t>(src_reg) & 0x07) << 3;
		uint8_t base_bits = static_cast<uint8_t>(base_reg) & 0x07;
		
		if (offset == 0 && base_reg != X64Register::RBP && base_reg != X64Register::R13) {
			// Mod=00, no displacement
			textSectionData.push_back(0x00 | src_bits | base_bits);
		} else if (offset >= -128 && offset <= 127) {
			// Mod=01, 8-bit displacement
			textSectionData.push_back(0x40 | src_bits | base_bits);
			textSectionData.push_back(static_cast<uint8_t>(offset));
		} else {
			// Mod=10, 32-bit displacement
			textSectionData.push_back(0x80 | src_bits | base_bits);
			textSectionData.push_back((offset >> 0) & 0xFF);
			textSectionData.push_back((offset >> 8) & 0xFF);
			textSectionData.push_back((offset >> 16) & 0xFF);
			textSectionData.push_back((offset >> 24) & 0xFF);
		}
	}

	// Helper to generate and emit MOV reg, imm64
	void emitMovImm32(X64Register destinationRegister, uint32_t immediate_value) {
		// MOV r32, imm32 (zero-extends to 64-bit in x64 mode)
		// REX.B prefix needed if destination is R8-R15 (for lower 32-bit access)
		uint8_t reg_encoding = static_cast<uint8_t>(destinationRegister);
		if (reg_encoding >= 8) {
			textSectionData.push_back(0x41); // REX.B for R8-R15
		}
		// MOV r32, imm32 opcode (0xB8 + lower 3 bits of register encoding)
		textSectionData.push_back(0xB8 + (reg_encoding & 0x07));
		// Encode the 32-bit immediate value (little-endian)
		for (int j = 0; j < 4; ++j) {
			textSectionData.push_back(static_cast<uint8_t>((immediate_value >> (j * 8)) & 0xFF));
		}
	}

	void emitMovImm64(X64Register destinationRegister, uint64_t immediate_value) {
		// REX prefix: REX.W for 64-bit operation, REX.B if destination is R8-R15
		uint8_t rex_prefix = 0x48; // REX.W
		uint8_t reg_encoding = static_cast<uint8_t>(destinationRegister);
		if (reg_encoding >= 8) {
			rex_prefix |= 0x01; // REX.B for R8-R15
		}
		textSectionData.push_back(rex_prefix);
		// MOV r64, imm64 opcode (0xB8 + lower 3 bits of register encoding)
		textSectionData.push_back(0xB8 + (reg_encoding & 0x07));
		// Encode the 64-bit immediate value (little-endian)
		for (int j = 0; j < 8; ++j) {
			textSectionData.push_back(static_cast<uint8_t>((immediate_value >> (j * 8)) & 0xFF));
		}
	}

	// Helper to emit SUB RSP, imm8 for stack allocation
	void emitSubRSP(uint8_t amount) {
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x83); // SUB r/m64, imm8
		textSectionData.push_back(0xEC); // ModR/M: RSP
		textSectionData.push_back(amount);
	}

	// Helper to emit ADD RSP, imm8 for stack deallocation
	void emitAddRSP(uint8_t amount) {
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x83); // ADD r/m64, imm8
		textSectionData.push_back(0xC4); // ModR/M: RSP
		textSectionData.push_back(amount);
	}

	// Helper to emit CALL instruction with relocation
	void emitCall(const std::string& symbol_name) {
		textSectionData.push_back(0xE8); // CALL rel32
		size_t relocation_offset = textSectionData.size();
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		writer.add_relocation(relocation_offset, symbol_name);
	}

	// Helper to emit MOV reg, reg
	void emitMovRegReg(X64Register dest, X64Register src) {
		// MOV r/m64, r64 (opcode 0x89)
		// ModR/M: reg = source, r/m = destination
		// Determine REX prefix based on registers
		uint8_t rex = 0x48; // REX.W for 64-bit
		if (static_cast<uint8_t>(src) >= 8) rex |= 0x04;  // REX.R extends reg field (source)
		if (static_cast<uint8_t>(dest) >= 8) rex |= 0x01; // REX.B extends r/m field (dest)
		
		textSectionData.push_back(rex);
		textSectionData.push_back(0x89); // MOV r/m64, r64
		
		// ModR/M byte: mod=11 (register), reg=src, r/m=dest
		uint8_t modrm = 0xC0;
		modrm |= ((static_cast<uint8_t>(src) & 0x07) << 3);
		modrm |= (static_cast<uint8_t>(dest) & 0x07);
		textSectionData.push_back(modrm);
	}
	
	// Helper to emit MOV dest, [base + offset] with size
	void emitMovFromMemory(X64Register dest, X64Register base, int32_t offset, size_t size_bytes) {
		OpCodeWithSize opcode_result;
		if (size_bytes == 8) {
			opcode_result = generateMovFromMemory(dest, base, offset);
		} else if (size_bytes == 4) {
			opcode_result = generateMovFromMemory32(dest, base, offset);
		} else if (size_bytes == 2) {
			opcode_result = generateMovFromMemory16(dest, base, offset);
		} else if (size_bytes == 1) {
			opcode_result = generateMovFromMemory8(dest, base, offset);
		} else {
			// Default to 8 bytes
			opcode_result = generateMovFromMemory(dest, base, offset);
		}
		
		// Emit the opcodes
		for (size_t i = 0; i < opcode_result.size_in_bytes; ++i) {
			textSectionData.push_back(opcode_result.op_codes[i]);
		}
	}

	// Helper to emit MOV reg, [reg]
	void emitMovRegFromMemReg(X64Register dest, X64Register src_addr) {
		uint8_t rex = 0x48; // REX.W
		if (static_cast<uint8_t>(dest) >= 8) rex |= 0x04;
		if (static_cast<uint8_t>(src_addr) >= 8) rex |= 0x01;
		
		textSectionData.push_back(rex);
		textSectionData.push_back(0x8B); // MOV r64, r/m64
		
		// ModR/M: mod=00 (indirect), reg=dest, r/m=src_addr
		uint8_t modrm = 0x00;
		modrm |= ((static_cast<uint8_t>(dest) & 0x07) << 3);
		modrm |= (static_cast<uint8_t>(src_addr) & 0x07);
		textSectionData.push_back(modrm);
	}

	// Helper to emit MOV reg, [reg + disp8]
	void emitMovRegFromMemRegDisp8(X64Register dest, X64Register src_addr, int8_t disp) {
		uint8_t rex = 0x48; // REX.W
		if (static_cast<uint8_t>(dest) >= 8) rex |= 0x04;
		if (static_cast<uint8_t>(src_addr) >= 8) rex |= 0x01;
		
		textSectionData.push_back(rex);
		textSectionData.push_back(0x8B); // MOV r64, r/m64
		
		// ModR/M: mod=01 (indirect + disp8), reg=dest, r/m=src_addr
		uint8_t modrm = 0x40;
		modrm |= ((static_cast<uint8_t>(dest) & 0x07) << 3);
		modrm |= (static_cast<uint8_t>(src_addr) & 0x07);
		textSectionData.push_back(modrm);
		textSectionData.push_back(static_cast<uint8_t>(disp));
	}

	// Helper to emit TEST reg, reg
	void emitTestRegReg(X64Register reg) {
		textSectionData.push_back(0x48); // REX.W
		textSectionData.push_back(0x85); // TEST r64, r64
		
		// ModR/M: mod=11, reg=reg, r/m=reg
		uint8_t modrm = 0xC0;
		uint8_t reg_val = static_cast<uint8_t>(reg) & 0x07;
		modrm |= (reg_val << 3) | reg_val;
		textSectionData.push_back(modrm);
	}

	// Helper to emit TEST AL, AL
	void emitTestAL() {
		textSectionData.push_back(0x84); // TEST r/m8, r8
		textSectionData.push_back(0xC0); // ModR/M: AL, AL
	}

	// Helper to emit LEA reg, [RIP + disp32] with relocation
	void emitLeaRipRelativeWithRelocation(X64Register dest, std::string_view symbol_name) {
		uint8_t rex = 0x48; // REX.W
		if (static_cast<uint8_t>(dest) >= 8) rex |= 0x04;

		textSectionData.push_back(rex);
		textSectionData.push_back(0x8D); // LEA r64, m

		// ModR/M: mod=00, reg=dest, r/m=101 (RIP-relative)
		uint8_t modrm = 0x05;
		modrm |= ((static_cast<uint8_t>(dest) & 0x07) << 3);
		textSectionData.push_back(modrm);

		// LEA uses RIP-relative addressing for data symbols
		// Use R_X86_64_PC32 (not PLT32) for data references like typeinfo
		size_t relocation_offset = textSectionData.size();
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// For ELF: Use R_X86_64_PC32 for data symbols (typeinfo, vtables, etc.)
			// PLT32 is only for function calls
			writer.add_relocation(relocation_offset, std::string(symbol_name), 2 /* R_X86_64_PC32 */);
		} else {
			// For COFF: Use default relocation type
			writer.add_relocation(relocation_offset, std::string(symbol_name));
		}
	}

	// Helper to emit MOV reg, [RIP + disp32] for integer loads
	// Returns the offset where the displacement placeholder starts (for relocation)
	uint32_t emitMovRipRelative(X64Register dest, int size_in_bits) {
		// For 64-bit: MOV RAX, [RIP + disp32] - 48 8B 05 [disp32]
		// For 32-bit: MOV EAX, [RIP + disp32] - 8B 05 [disp32]
		// For 16-bit: MOVZX EAX, word [RIP + disp32] - 0F B7 05 [disp32]
		// For 8-bit:  MOVZX EAX, byte [RIP + disp32] - 0F B6 05 [disp32]
		uint8_t dest_val = static_cast<uint8_t>(dest);
		uint8_t dest_bits = dest_val & 0x07;
		
		// Handle small sizes with MOVZX to zero-extend
		if (size_in_bits <= 8) {
			// MOVZX EAX, byte ptr [RIP + disp32]: 0F B6 05 [disp32]
			size_t base = textSectionData.size();
			textSectionData.resize(base + 7);
			char* p = textSectionData.data() + base;
			p[0] = 0x0F;
			p[1] = static_cast<char>(0xB6); // MOVZX r32, r/m8
			p[2] = 0x05 | (dest_bits << 3); // ModR/M: reg, [RIP + disp32]
			p[3] = 0x00; // disp32 placeholder
			p[4] = 0x00;
			p[5] = 0x00;
			p[6] = 0x00;
			return static_cast<uint32_t>(base + 3);
		} else if (size_in_bits == 16) {
			// MOVZX EAX, word ptr [RIP + disp32]: 0F B7 05 [disp32]
			size_t base = textSectionData.size();
			textSectionData.resize(base + 7);
			char* p = textSectionData.data() + base;
			p[0] = 0x0F;
			p[1] = static_cast<char>(0xB7); // MOVZX r32, r/m16
			p[2] = 0x05 | (dest_bits << 3); // ModR/M: reg, [RIP + disp32]
			p[3] = 0x00; // disp32 placeholder
			p[4] = 0x00;
			p[5] = 0x00;
			p[6] = 0x00;
			return static_cast<uint32_t>(base + 3);
		}
		
		// For 32-bit and 64-bit, use regular MOV
		// Branchless: compute REX prefix (0x48 for 64-bit, 0x40 for 32-bit with high reg, 0 otherwise)
		// REX.W bit is 0x08, so 0x48 = 0x40 | 0x08
		uint8_t needs_rex_w = (size_in_bits == 64) ? 0x08 : 0x00;
		uint8_t needs_rex_b = (dest_val >> 3) & 0x01;
		uint8_t rex = 0x40 | needs_rex_w | needs_rex_b;
		// Only emit REX if any bits are set beyond base 0x40
		uint8_t emit_rex = (needs_rex_w | needs_rex_b) != 0;
		// Use reserve + direct writes for branchless emission
		size_t base = textSectionData.size();
		textSectionData.resize(base + 6 + emit_rex);
		char* p = textSectionData.data() + base;
		// Branchless write: if emit_rex, write rex and shift; else start at opcode
		p[0] = emit_rex ? rex : 0x8B;
		p[emit_rex] = 0x8B; // MOV r32/r64, r/m32/r/m64
		p[1 + emit_rex] = 0x05 | (dest_bits << 3); // ModR/M: reg, [RIP + disp32]
		// disp32 placeholder (4 bytes of zeros)
		p[2 + emit_rex] = 0x00;
		p[3 + emit_rex] = 0x00;
		p[4 + emit_rex] = 0x00;
		p[5 + emit_rex] = 0x00;
		return static_cast<uint32_t>(base + 2 + emit_rex);
	}

	// Helper to emit MOVSD/MOVSS XMM, [RIP + disp32] for floating-point loads
	// Returns the offset where the displacement placeholder starts (for relocation)
	uint32_t emitFloatMovRipRelative(X64Register xmm_dest, bool is_float) {
		// MOVSD XMM0, [RIP + disp32]: F2 0F 10 05 [disp32]
		// MOVSS XMM0, [RIP + disp32]: F3 0F 10 05 [disp32]
		textSectionData.push_back(is_float ? 0xF3 : 0xF2);
		textSectionData.push_back(0x0F);
		textSectionData.push_back(0x10); // MOVSD/MOVSS xmm, m (load variant)
		uint8_t xmm_bits = static_cast<uint8_t>(xmm_dest) & 0x07;
		textSectionData.push_back(0x05 | (xmm_bits << 3)); // ModR/M: XMMn, [RIP + disp32]
		
		uint32_t reloc_offset = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		return reloc_offset;
	}

	// Helper to emit MOV [RIP + disp32], reg for integer stores
	// Returns the offset where the displacement placeholder starts (for relocation)
	uint32_t emitMovRipRelativeStore(X64Register src, int size_in_bits) {
		// For 64-bit: MOV [RIP + disp32], RAX - 48 89 05 [disp32]
		// For 32-bit: MOV [RIP + disp32], EAX - 89 05 [disp32]
		uint8_t src_val = static_cast<uint8_t>(src);
		uint8_t src_bits = src_val & 0x07;
		// Branchless: compute REX prefix (0x48 for 64-bit, 0x40 for 32-bit with high reg, 0 otherwise)
		// REX.W bit is 0x08, so 0x48 = 0x40 | 0x08
		uint8_t needs_rex_w = (size_in_bits == 64) ? 0x08 : 0x00;
		uint8_t needs_rex_b = (src_val >> 3) & 0x01;
		uint8_t rex = 0x40 | needs_rex_w | needs_rex_b;
		// Only emit REX if any bits are set beyond base 0x40
		uint8_t emit_rex = (needs_rex_w | needs_rex_b) != 0;
		// Use reserve + direct writes for branchless emission
		size_t base = textSectionData.size();
		textSectionData.resize(base + 6 + emit_rex);
		char* p = textSectionData.data() + base;
		// Branchless write: if emit_rex, write rex and shift; else start at opcode
		p[0] = emit_rex ? rex : 0x89;
		p[emit_rex] = 0x89; // MOV r/m32/r/m64, r32/r64 (store variant)
		p[1 + emit_rex] = 0x05 | (src_bits << 3); // ModR/M: reg, [RIP + disp32]
		// disp32 placeholder (4 bytes of zeros)
		p[2 + emit_rex] = 0x00;
		p[3 + emit_rex] = 0x00;
		p[4 + emit_rex] = 0x00;
		p[5 + emit_rex] = 0x00;
		return static_cast<uint32_t>(base + 2 + emit_rex);
	}

	// Helper to emit MOVSD/MOVSS [RIP + disp32], XMM for floating-point stores
	// Returns the offset where the displacement placeholder starts (for relocation)
	uint32_t emitFloatMovRipRelativeStore(X64Register xmm_src, bool is_float) {
		// MOVSD [RIP + disp32], XMM0: F2 0F 11 05 [disp32]
		// MOVSS [RIP + disp32], XMM0: F3 0F 11 05 [disp32]
		textSectionData.push_back(is_float ? 0xF3 : 0xF2);
		textSectionData.push_back(0x0F);
		textSectionData.push_back(0x11); // MOVSD/MOVSS m, xmm (store variant)
		uint8_t xmm_bits = static_cast<uint8_t>(xmm_src) & 0x07;
		textSectionData.push_back(0x05 | (xmm_bits << 3)); // ModR/M: XMMn, [RIP + disp32]
		
		uint32_t reloc_offset = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		return reloc_offset;
	}

	// Additional emit helpers for dynamic_cast runtime generation
	
	void emitCmpRegReg(X64Register r1, X64Register r2) {
		uint8_t rex = 0x48; // REX.W for 64-bit
		if (static_cast<uint8_t>(r1) >= 8) rex |= 0x01; // REX.B
		if (static_cast<uint8_t>(r2) >= 8) rex |= 0x04; // REX.R
		
		textSectionData.push_back(rex);
		textSectionData.push_back(0x39); // CMP r/m64, r64
		
		// ModR/M: mod=11 (register), reg=r2, r/m=r1
		uint8_t modrm = 0xC0;
		modrm |= ((static_cast<uint8_t>(r2) & 0x07) << 3);
		modrm |= (static_cast<uint8_t>(r1) & 0x07);
		textSectionData.push_back(modrm);
	}

	void emitCmpRegWithMem(X64Register reg, X64Register mem_base) {
		// CMP reg, [mem_base]
		uint8_t rex = 0x48; // REX.W
		if (static_cast<uint8_t>(reg) >= 8) rex |= 0x04; // REX.R
		if (static_cast<uint8_t>(mem_base) >= 8) rex |= 0x01; // REX.B
		
		textSectionData.push_back(rex);
		textSectionData.push_back(0x3B); // CMP r64, r/m64
		
		// ModR/M: mod=00 (no disp), reg=reg, r/m=mem_base
		uint8_t modrm = 0x00;
		modrm |= ((static_cast<uint8_t>(reg) & 0x07) << 3);
		modrm |= (static_cast<uint8_t>(mem_base) & 0x07);
		textSectionData.push_back(modrm);
	}

	void emitJumpIfZero(int8_t offset) {
		textSectionData.push_back(0x74); // JZ rel8
		textSectionData.push_back(static_cast<uint8_t>(offset));
	}

	void emitJumpIfEqual(int8_t offset) {
		textSectionData.push_back(0x74); // JE rel8 (same as JZ)
		textSectionData.push_back(static_cast<uint8_t>(offset));
	}

	void emitJumpIfNotZero(int8_t offset) {
		textSectionData.push_back(0x75); // JNZ rel8
		textSectionData.push_back(static_cast<uint8_t>(offset));
	}

	void emitJumpUnconditional(int8_t offset) {
		textSectionData.push_back(0xEB); // JMP rel8
		textSectionData.push_back(static_cast<uint8_t>(offset));
	}

	void emitXorRegReg(X64Register reg) {
		// XOR reg, reg (zero register)
		uint8_t rex = 0x48; // REX.W
		if (static_cast<uint8_t>(reg) >= 8) rex |= 0x05; // REX.R | REX.B
		
		textSectionData.push_back(rex);
		textSectionData.push_back(0x31); // XOR r/m64, r64
		
		// ModR/M: mod=11, reg=reg, r/m=reg
		uint8_t modrm = 0xC0;
		modrm |= ((static_cast<uint8_t>(reg) & 0x07) << 3);
		modrm |= (static_cast<uint8_t>(reg) & 0x07);
		textSectionData.push_back(modrm);
	}

	// Helper to emit REP MOVSB for memory copying
	// Copies RCX bytes from [RSI] to [RDI]
	void emitRepMovsb() {
		textSectionData.push_back(0xF3); // REP prefix
		textSectionData.push_back(0xA4); // MOVSB
	}

	// Helper to emit MOV [RSP+disp8], reg
	void emitMovToRSPDisp8(X64Register sourceRegister, int8_t displacement) {
		// MOV [RSP+disp8], reg
		uint8_t rex = 0x48; // REX.W for 64-bit
		if (static_cast<uint8_t>(sourceRegister) >= 8) {
			rex |= 0x04; // REX.R for extended register
		}
		textSectionData.push_back(rex);
		textSectionData.push_back(0x89); // MOV r/m64, r64
		// ModR/M: mod=01 (disp8), reg=sourceRegister, r/m=100 (SIB follows)
		uint8_t modrm = 0x44 | ((static_cast<uint8_t>(sourceRegister) & 0x07) << 3);
		textSectionData.push_back(modrm);
		textSectionData.push_back(0x24); // SIB: scale=0, index=RSP(4), base=RSP(4)
		textSectionData.push_back(static_cast<uint8_t>(displacement));
	}

	// Helper to emit LEA reg, [RSP+disp8]
	void emitLeaFromRSPDisp8(X64Register destinationRegister, int8_t displacement) {
		// LEA reg, [RSP+disp8]
		uint8_t rex = 0x48; // REX.W for 64-bit
		if (static_cast<uint8_t>(destinationRegister) >= 8) {
			rex |= 0x04; // REX.R for extended register
		}
		textSectionData.push_back(rex);
		textSectionData.push_back(0x8D); // LEA
		// ModR/M: mod=01 (disp8), reg=destinationRegister, r/m=100 (SIB follows)
		uint8_t modrm = 0x44 | ((static_cast<uint8_t>(destinationRegister) & 0x07) << 3);
		textSectionData.push_back(modrm);
		textSectionData.push_back(0x24); // SIB: scale=0, index=RSP(4), base=RSP(4)
		textSectionData.push_back(static_cast<uint8_t>(displacement));
	}

	void emitRet() {
		textSectionData.push_back(0xC3); // RET
	}

	void emitMovRegImm8(X64Register reg, uint8_t imm) {
		// MOV reg, imm8 (using AL/BL/CL etc. encoding)
		if (reg == X64Register::RAX) {
			textSectionData.push_back(0xB0); // MOV AL, imm8
			textSectionData.push_back(imm);
		} else {
			// For other registers, use MOV r8, imm8
			uint8_t rex = 0x40; // REX prefix (needed for SPL, BPL, SIL, DIL)
			if (static_cast<uint8_t>(reg) >= 8) rex |= 0x01; // REX.B
			textSectionData.push_back(rex);
			
			uint8_t opcode = 0xB0 + (static_cast<uint8_t>(reg) & 0x07);
			textSectionData.push_back(opcode);
			textSectionData.push_back(imm);
		}
	}

	void emitPushReg(X64Register reg) {
		uint8_t opcode = 0x50 + (static_cast<uint8_t>(reg) & 0x07);
		if (static_cast<uint8_t>(reg) >= 8) {
			textSectionData.push_back(0x41); // REX.B
		}
		textSectionData.push_back(opcode);
	}

	void emitPopReg(X64Register reg) {
		uint8_t opcode = 0x58 + (static_cast<uint8_t>(reg) & 0x07);
		if (static_cast<uint8_t>(reg) >= 8) {
			textSectionData.push_back(0x41); // REX.B
		}
		textSectionData.push_back(opcode);
	}

	void emitIncReg(X64Register reg) {
		uint8_t rex = 0x48; // REX.W
		if (static_cast<uint8_t>(reg) >= 8) rex |= 0x01; // REX.B
		
		textSectionData.push_back(rex);
		textSectionData.push_back(0xFF); // INC/DEC r/m64
		
		// ModR/M: mod=11, reg=0 (INC), r/m=reg
		uint8_t modrm = 0xC0;
		modrm |= (static_cast<uint8_t>(reg) & 0x07);
		textSectionData.push_back(modrm);
	}

	void emitCmpRegImm32(X64Register reg, uint32_t imm) {
		uint8_t rex = 0x48; // REX.W
		if (static_cast<uint8_t>(reg) >= 8) rex |= 0x01; // REX.B
		
		textSectionData.push_back(rex);
		
		if (reg == X64Register::RAX) {
			textSectionData.push_back(0x3D); // CMP EAX, imm32 (shorter encoding)
		} else {
			textSectionData.push_back(0x81); // CMP r/m64, imm32
			// ModR/M: mod=11, reg=7 (CMP), r/m=reg
			uint8_t modrm = 0xF8 | (static_cast<uint8_t>(reg) & 0x07);
			textSectionData.push_back(modrm);
		}
		
		// Emit imm32 (little-endian)
		textSectionData.push_back(static_cast<uint8_t>(imm));
		textSectionData.push_back(static_cast<uint8_t>(imm >> 8));
		textSectionData.push_back(static_cast<uint8_t>(imm >> 16));
		textSectionData.push_back(static_cast<uint8_t>(imm >> 24));
	}

	void emitJumpIfAbove(int8_t offset) {
		textSectionData.push_back(0x77); // JA rel8 (unsigned >)
		textSectionData.push_back(static_cast<uint8_t>(offset));
	}

	void emitJumpIfBelow(int8_t offset) {
		textSectionData.push_back(0x72); // JB rel8 (unsigned <)
		textSectionData.push_back(static_cast<uint8_t>(offset));
	}

	void emitLeaRegScaledIndex(X64Register dest, X64Register base, X64Register index, uint8_t scale, int8_t disp) {
		// LEA dest, [base + index*scale + disp]
		uint8_t rex = 0x48; // REX.W
		if (static_cast<uint8_t>(dest) >= 8) rex |= 0x04; // REX.R
		if (static_cast<uint8_t>(index) >= 8) rex |= 0x02; // REX.X
		if (static_cast<uint8_t>(base) >= 8) rex |= 0x01; // REX.B
		
		textSectionData.push_back(rex);
		textSectionData.push_back(0x8D); // LEA r64, m
		
		// ModR/M: mod=01 (disp8), reg=dest, r/m=100 (SIB follows)
		uint8_t modrm = 0x44;
		modrm |= ((static_cast<uint8_t>(dest) & 0x07) << 3);
		textSectionData.push_back(modrm);
		
		// SIB: scale, index, base
		uint8_t scale_bits = 0;
		if (scale == 2) scale_bits = 1;
		else if (scale == 4) scale_bits = 2;
		else if (scale == 8) scale_bits = 3;
		
		uint8_t sib = (scale_bits << 6) | ((static_cast<uint8_t>(index) & 0x07) << 3) | (static_cast<uint8_t>(base) & 0x07);
		textSectionData.push_back(sib);
		
		// disp8
		textSectionData.push_back(static_cast<uint8_t>(disp));
	}

	// Allocate a register, spilling one to the stack if necessary
	X64Register allocateRegisterWithSpilling() {
		return allocateRegisterWithSpilling(X64Register::Count);
	}

	// Allocate a register, spilling one to the stack if necessary, excluding a specific register
	X64Register allocateRegisterWithSpilling(X64Register exclude) {
		// Try to allocate a free register first (excluding the specified one)
		for (auto& reg : regAlloc.registers) {
			if (!reg.isAllocated && reg.reg < X64Register::XMM0 && reg.reg != exclude) {
				reg.isAllocated = true;
				return reg.reg;
			}
		}

		// No free registers - need to spill one (excluding the specified one)
		auto reg_to_spill = regAlloc.findRegisterToSpill(exclude);
		if (!reg_to_spill.has_value()) {
			throw std::runtime_error("No registers available for spilling");
		}

		X64Register spill_reg = reg_to_spill.value();
		auto& reg_info = regAlloc.registers[static_cast<int>(spill_reg)];

		// If the register is dirty, write it back to the stack using size-appropriate MOV
		if (reg_info.isDirty && reg_info.stackVariableOffset != INT_MIN) {
			emitMovToFrameSized(
				SizedRegister{spill_reg, 64, false},  // source: 64-bit register
				SizedStackSlot{reg_info.stackVariableOffset, reg_info.size_in_bits, false}  // dest: sized stack slot
			);
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
		// Use typed payload
		if (instruction.hasTypedPayload()) {
			const auto& call_op = instruction.getTypedPayload<CallOp>();
			
			flushAllDirtyRegisters();
			
			// Determine effective return size; fall back to type size if not provided
			int return_size_bits = call_op.return_size_in_bits;
			if (return_size_bits == 0) {
				int computed_size = get_type_size_bits(call_op.return_type);
				if (computed_size > 0) {
					return_size_bits = computed_size;
				} else {
					// Default to pointer size to ensure unique stack slot
					return_size_bits = static_cast<int>(sizeof(void*) * 8);
				}
			}
			
			// Get result offset - use actual return size for proper stack allocation
			FLASH_LOG_FORMAT(Codegen, Debug,
				"handleFunctionCall: allocating result {} (var_number={}) with return_size_in_bits={}",
				call_op.result.name(), call_op.result.var_number, return_size_bits);
			int result_offset = allocateStackSlotForTempVar(call_op.result.var_number, return_size_bits);
			FLASH_LOG_FORMAT(Codegen, Debug,
				"handleFunctionCall: result_offset={} for {} (var_number={})",
				result_offset, call_op.result.name(), call_op.result.var_number);
			variable_scopes.back().variables[StringTable::getOrInternStringHandle(call_op.result.name())].offset = result_offset;
			
			// For functions returning struct by value, prepare hidden return parameter
			// The return slot address will be passed as the first argument
			int param_shift = 0;  // Tracks how many parameters to shift (for hidden return param)
			if (call_op.uses_return_slot) {
				param_shift = 1;  // Regular parameters shift by 1 to make room for hidden return param
				
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Function call uses return slot - will pass address of temp_{} in first parameter register",
					call_op.result.var_number);
			}
			
			// IMPORTANT: Process stack arguments (beyond register count) FIRST, before loading register arguments.
			// To prevent loadTypedValueIntoRegister from clobbering parameter registers,
			// we reserve all parameter registers before processing stack arguments.
			// Platform-specific: Windows has 4 int regs, Linux has 6 int regs
			size_t max_int_regs = getMaxIntParamRegs<TWriterClass>();
			size_t max_float_regs = getMaxFloatParamRegs<TWriterClass>();
			size_t shadow_space = getShadowSpaceSize<TWriterClass>();
			
			// Reserve parameter registers to prevent them from being allocated as temporaries
			// Only reserve registers that aren't already allocated
			std::vector<X64Register> reserved_regs;
			for (size_t i = 0; i < max_int_regs; ++i) {
				X64Register reg = getIntParamReg<TWriterClass>(i);
				if (!regAlloc.is_allocated(reg)) {
					regAlloc.allocateSpecific(reg, -1);  // Reserve with dummy offset
					reserved_regs.push_back(reg);
				}
			}
			
			// Enhanced stack overflow logic: Track both int and float register usage independently
			// to correctly identify which arguments overflow to stack
			// For Windows variadic functions: ALL arguments must go on stack (in addition to registers)
			constexpr bool is_coff_format = !std::is_same_v<TWriterClass, ElfFileWriter>;
			bool variadic_needs_stack_args = call_op.is_variadic && is_coff_format;
			
			size_t temp_int_idx = 0;
			size_t temp_float_idx = 0;
			size_t stack_arg_count = 0;
			
			for (size_t i = 0; i < call_op.args.size(); ++i) {
				const auto& arg = call_op.args[i];
				// Reference arguments (including rvalue references) are passed as pointers,
				// so they should use integer registers, not floating-point registers
				bool is_float_arg = is_floating_point_type(arg.type) && !arg.is_reference;
				
				// Check if this is a two-register struct (System V AMD64 ABI: 9-16 byte structs)
				bool is_two_reg_struct = false;
				if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
					if (arg.type == Type::Struct && arg.size_in_bits > 64 && arg.size_in_bits <= 128 && !arg.is_reference) {
						is_two_reg_struct = true;
					}
				}
				
				// Determine if this argument goes on stack
				bool goes_on_stack = variadic_needs_stack_args; // For Windows variadic: ALL args go on stack
				if (!goes_on_stack) {
					if (is_float_arg) {
						if (temp_float_idx >= max_float_regs) {
							goes_on_stack = true;
						}
						temp_float_idx++;
					} else {
						// For two-register structs, need two consecutive int registers
						size_t regs_needed = is_two_reg_struct ? 2 : 1;
						if (temp_int_idx + regs_needed > max_int_regs) {
							goes_on_stack = true;
						}
						temp_int_idx += regs_needed;
					}
				} else {
					// Still need to increment counters for variadic functions
					if (is_float_arg) {
						temp_float_idx++;
					} else {
						size_t regs_needed = is_two_reg_struct ? 2 : 1;
						temp_int_idx += regs_needed;
					}
				}
				
				if (goes_on_stack) {
					// Stack args placement:
					// Windows variadic: RSP+0 + arg_index*8 (args start at RSP+0, no shadow space offset)
					// Windows normal: RSP+32 (shadow space) + stack_arg_count*8
					// Linux: RSP+0 (no shadow space) + stack_arg_count*8
					int stack_offset;
					if (variadic_needs_stack_args) {
						// For variadic on Windows: args at RSP+0, RSP+8, RSP+16, ...
						stack_offset = static_cast<int>(i * 8);
					} else {
						// For normal functions: skip shadow space first
						stack_offset = static_cast<int>(shadow_space + stack_arg_count * 8);
					}
					
					if (is_float_arg) {
						// For floating-point arguments, load into XMM register and store with float instruction
						X64Register temp_xmm = allocateXMMRegisterWithSpilling();
						
						// Load the float value into XMM register
						if (std::holds_alternative<double>(arg.value)) {
							// Handle floating-point literal
							double float_value = std::get<double>(arg.value);
							uint64_t bits;
							if (arg.type == Type::Float) {
								float float_val = static_cast<float>(float_value);
								uint32_t float_bits;
								std::memcpy(&float_bits, &float_val, sizeof(float_bits));
								bits = float_bits;
							} else {
								std::memcpy(&bits, &float_value, sizeof(bits));
							}
							
							// Load bit pattern into temp GPR first
							X64Register temp_gpr = allocateRegisterWithSpilling();
							emitMovImm64(temp_gpr, bits);
							
							// Move from GPR to XMM register
							emitMovqGprToXmm(temp_gpr, temp_xmm);
							
							regAlloc.release(temp_gpr);
						} else if (std::holds_alternative<TempVar>(arg.value)) {
							const auto& temp_var = std::get<TempVar>(arg.value);
							int var_offset = getStackOffsetFromTempVar(temp_var);
							bool is_float = (arg.type == Type::Float);
							emitFloatMovFromFrame(temp_xmm, var_offset, is_float);
						} else if (std::holds_alternative<StringHandle>(arg.value)) {
							StringHandle var_name_handle = std::get<StringHandle>(arg.value);
							int var_offset = variable_scopes.back().variables[var_name_handle].offset;
							bool is_float = (arg.type == Type::Float);
							emitFloatMovFromFrame(temp_xmm, var_offset, is_float);
						}
						
						// Store XMM register to stack using float store instruction
						bool is_float = (arg.type == Type::Float);
						emitFloatStoreToRSP(textSectionData, temp_xmm, stack_offset, is_float);
						
						regAlloc.release(temp_xmm);
					} else {
						// For integer arguments, use the existing code path
						X64Register temp_reg = loadTypedValueIntoRegister(arg);
						emitStoreToRSP(textSectionData, temp_reg, stack_offset);
						regAlloc.release(temp_reg);
					}
					stack_arg_count++;
				}
			}
			
			// Release reserved parameter registers now that stack arguments are processed
			for (X64Register reg : reserved_regs) {
				regAlloc.release(reg);
			}
			
			// Now process register arguments (platform-specific: 4 on Windows, 6 on Linux for integers)
			// Note: max_int_regs and max_float_regs already declared above for stack arg processing
			// Use separate counters for integer and float registers (System V AMD64 ABI requirement)
			// If function uses return slot, start at index param_shift to leave room for hidden parameter
			size_t int_reg_index = param_shift;  // Start at param_shift if hidden return param present
			size_t float_reg_index = 0;
			
			for (size_t i = 0; i < call_op.args.size(); ++i) {
				const auto& arg = call_op.args[i];
				
				// Determine if this is a floating-point argument
				// Reference arguments (including rvalue references) are passed as pointers (addresses),
				// so they should use integer registers regardless of the underlying type
				bool is_float_arg = is_floating_point_type(arg.type) && !arg.is_reference;
				
				// Check if this is a two-register struct (System V AMD64 ABI: 9-16 byte structs)
				bool is_potential_two_reg_struct = false;
				if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
					if (arg.type == Type::Struct && arg.size_in_bits > 64 && arg.size_in_bits <= 128 && !arg.is_reference) {
						is_potential_two_reg_struct = true;
					}
				}
				
				// Check if this argument fits in a register (accounting for param_shift)
				bool use_register = false;
				if (is_float_arg) {
					if (float_reg_index < max_float_regs) {
						use_register = true;
					}
				} else {
					// For two-register structs, need two consecutive int registers
					size_t regs_needed = is_potential_two_reg_struct ? 2 : 1;
					if (int_reg_index + regs_needed <= max_int_regs) {
						use_register = true;
					}
				}
				
				// Skip arguments that go on stack (already handled)
				if (!use_register) {
					continue;
				}
				
				// Get the platform-specific calling convention register
				// Use separate indices for int and float registers
				X64Register target_reg = is_float_arg 
					? getFloatParamReg<TWriterClass>(float_reg_index++)
					: getIntParamReg<TWriterClass>(int_reg_index++);

				
				// Special handling for passing addresses (this pointer or large struct references)
				// For member functions: first arg is always "this" pointer (pass address)
				// System V AMD64 ABI (Linux): 
				//   - Structs ≤8 bytes: pass by value in one register
				//   - Structs 9-16 bytes: pass by value in TWO consecutive registers
				//   - Structs >16 bytes: pass by pointer
				// x64 Windows ABI:
				//   - Structs of 1, 2, 4, or 8 bytes: pass by value in one register
				//   - All other structs: pass by pointer
				bool should_pass_address = false;
				bool is_two_register_struct = false;  // For System V AMD64 ABI 9-16 byte structs
				if (call_op.is_member_function && i == 0) {
					// First argument of member function is always "this" pointer
					should_pass_address = true;
				} else if (arg.is_reference) {
					// Parameter is explicitly a reference - always pass by address
					should_pass_address = true;
				} else if (arg.type == Type::Struct && std::holds_alternative<StringHandle>(arg.value)) {
					// Check struct size for ABI-specific handling
					if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
						// System V AMD64 ABI (Linux)
						if (arg.size_in_bits > 128) {
							// Struct > 16 bytes - pass by reference (address)
							should_pass_address = true;
						} else if (arg.size_in_bits > 64) {
							// Struct 9-16 bytes - pass by value in TWO consecutive registers
							is_two_register_struct = true;
						}
						// Else: Struct ≤8 bytes - pass by value in one register (default path)
					} else {
						// Windows x64 ABI
						if (arg.size_in_bits > 64) {
							// Large struct - pass by reference (address)
							should_pass_address = true;
						}
					}
				} else if (arg.type == Type::Struct && std::holds_alternative<TempVar>(arg.value)) {
					// Handle TempVar struct arguments for ABI-specific handling
					if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
						// System V AMD64 ABI (Linux)
						if (arg.size_in_bits > 128) {
							// Struct > 16 bytes - pass by reference (address)
							should_pass_address = true;
						} else if (arg.size_in_bits > 64) {
							// Struct 9-16 bytes - pass by value in TWO consecutive registers
							is_two_register_struct = true;
						}
						// Else: Struct ≤8 bytes - pass by value in one register (default path)
					} else {
						// Windows x64 ABI
						if (arg.size_in_bits > 64) {
							// Large struct - pass by reference (address)
							should_pass_address = true;
						}
					}
				}
				
				if (should_pass_address && std::holds_alternative<StringHandle>(arg.value)) {
					// Load ADDRESS of object using LEA or MOV depending on whether it's a reference
					StringHandle object_name_handle = std::get<StringHandle>(arg.value);
					int object_offset = variable_scopes.back().variables[object_name_handle].offset;
					
					// Check if this variable is itself a reference (e.g., rvalue reference variable)
					// If so, it already holds a pointer, so load it with MOV instead of LEA
					auto ref_it = reference_stack_info_.find(object_offset);
					if (ref_it != reference_stack_info_.end()) {
						// Variable is a reference - it already holds a pointer, load it
						emitMovFromFrame(target_reg, object_offset);
					} else {
						// Variable is not a reference - take its address with LEA
						emitLEAFromFrame(textSectionData, target_reg, object_offset);
					}
					continue;
				}
				
				// Handle System V AMD64 ABI: Structs 9-16 bytes passed in TWO consecutive registers
				if (is_two_register_struct && std::holds_alternative<StringHandle>(arg.value)) {
					StringHandle object_name_handle = std::get<StringHandle>(arg.value);
					int object_offset = variable_scopes.back().variables[object_name_handle].offset;
					
					// Load first 8 bytes into target_reg (already allocated)
					emitMovFromFrame(target_reg, object_offset);
					
					// Check if we have a second register available
					if (int_reg_index < max_int_regs) {
						// Load second 8 bytes into next integer register
						X64Register second_reg = getIntParamReg<TWriterClass>(int_reg_index++);
						emitMovFromFrame(second_reg, object_offset + 8);
					} else {
						// No second register available - need to spill to stack
						// This case should be rare in practice
						FLASH_LOG(Codegen, Warning, "Two-register struct has no second register available");
					}
					continue;
				}
				
				// Handle TempVar arguments that should pass an address (e.g., constructor calls passed to rvalue reference params)
				if (should_pass_address && std::holds_alternative<TempVar>(arg.value)) {
					// When should_pass_address is true, the TempVar can be either:
					// 1. An object value that needs its address taken (like Widget(42)) - use LEA
					// 2. A pointer value from AddressOf or cast (like result of (Widget&&)w1) - use MOV
					//
					// To distinguish:
					// - Case 2: The TempVar was written by AddressOf/cast and holds a pointer
					// - Case 1: The TempVar holds the actual object
					//
					// Since we can't easily tell from the IR alone, use a simple heuristic:
					// Check reference_stack_info_ to see if this variable is marked as holding a reference/pointer
					const auto& temp_var = std::get<TempVar>(arg.value);
					int var_offset = getStackOffsetFromTempVar(temp_var);
					
					auto ref_it = reference_stack_info_.find(var_offset);
					if (ref_it != reference_stack_info_.end()) {
						// Variable is marked as holding a pointer/reference - load it with MOV
						emitMovFromFrame(target_reg, var_offset);
					} else {
						// Variable holds an object value - take its address with LEA
						emitLeaFromFrame(target_reg, var_offset);
					}
					continue;
				}
				
				// Handle System V AMD64 ABI: TempVar structs 9-16 bytes passed in TWO consecutive registers
				if (is_two_register_struct && std::holds_alternative<TempVar>(arg.value)) {
					const auto& temp_var = std::get<TempVar>(arg.value);
					int var_offset = getStackOffsetFromTempVar(temp_var);
					
					// Load first 8 bytes into target_reg (already allocated)
					emitMovFromFrame(target_reg, var_offset);
					
					// Check if we have a second register available
					if (int_reg_index < max_int_regs) {
						// Load second 8 bytes into next integer register
						X64Register second_reg = getIntParamReg<TWriterClass>(int_reg_index++);
						emitMovFromFrame(second_reg, var_offset + 8);
					} else {
						FLASH_LOG(Codegen, Warning, "Two-register TempVar struct has no second register available");
					}
					continue;
				}
				
				// Handle floating-point immediate values (double literals)
				if (is_float_arg && std::holds_alternative<double>(arg.value)) {
					// Load floating-point literal into XMM register
					double float_value = std::get<double>(arg.value);
					
					// For float (32-bit), we need to convert the double to float first
					uint64_t bits;
					if (arg.type == Type::Float) {
						float float_val = static_cast<float>(float_value);
						uint32_t float_bits;
						std::memcpy(&float_bits, &float_val, sizeof(float_bits));
						bits = float_bits; // Zero-extend to 64-bit
					} else {
						std::memcpy(&bits, &float_value, sizeof(bits));
					}
					
					// Allocate a temporary GPR for the bit pattern
					X64Register temp_gpr = allocateRegisterWithSpilling();
					
					// Load bit pattern into temp GPR
					emitMovImm64(temp_gpr, bits);
					
					// movq xmm, r64 (66 REX.W 0F 6E /r) - move from GPR to XMM
					textSectionData.push_back(0x66);
					uint8_t xmm_idx = xmm_modrm_bits(target_reg);
					uint8_t rex_movq = 0x48;  // REX.W
					if (xmm_idx >= 8) {
						rex_movq |= 0x04; // REX.R for XMM8-XMM15 destination
					}
					if (static_cast<uint8_t>(temp_gpr) >= static_cast<uint8_t>(X64Register::R8)) {
						rex_movq |= 0x01; // REX.B for source GPR
					}
					textSectionData.push_back(rex_movq);
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0x6E);
					uint8_t modrm_movq = 0xC0 + ((xmm_idx & 0x07) << 3) + (static_cast<uint8_t>(temp_gpr) & 0x07);
					textSectionData.push_back(modrm_movq);
					
					// For varargs functions, System V AMD64 ABI requires copying XMM value to GPR
					// System V AMD64 ABI does NOT require this - floats stay in XMM registers only
					// Only copy if the corresponding integer register exists (i < max_int_regs)
					if (call_op.is_variadic && i < max_int_regs && is_coff_format) {
						emitMovqXmmToGpr(target_reg, getIntParamReg<TWriterClass>(i));
					}
					
					// Release the temporary GPR
					regAlloc.release(temp_gpr);
					continue;
				}
				
				// Load value into target register
				if (std::holds_alternative<unsigned long long>(arg.value)) {
					// Load immediate directly into target register
					unsigned long long value = std::get<unsigned long long>(arg.value);
					// Use 32-bit mov for 32-bit arguments (automatically zero-extends to 64-bit)
					// This ensures proper handling of signed 32-bit values like -1
					if (arg.size_in_bits == 32) {
						// Cast to uint32_t truncates to lower 32 bits (intended behavior)
						// For signed values like -1 (0xFFFFFFFFFFFFFFFF), this gives 0xFFFFFFFF
						emitMovImm32(target_reg, static_cast<uint32_t>(value));
					} else {
						emitMovImm64(target_reg, value);
					}
				} else if (std::holds_alternative<TempVar>(arg.value)) {
					// Load from stack
					const auto& temp_var = std::get<TempVar>(arg.value);
					int var_offset = getStackOffsetFromTempVar(temp_var);
					if (is_float_arg) {
						// For floating-point, use movsd/movss into XMM register
						bool is_float = (arg.type == Type::Float);
						emitFloatMovFromFrame(target_reg, var_offset, is_float);
						
						// For varargs: floats must be promoted to double (C standard)
						if (call_op.is_variadic && is_float) {
							emitCvtss2sd(target_reg, target_reg);
						}
						
						// For varargs: also copy to corresponding INT register
						// System V AMD64 ABI does NOT require this
						if (call_op.is_variadic && i < max_int_regs && is_coff_format) {
							emitMovqXmmToGpr(target_reg, getIntParamReg<TWriterClass>(i));
						}
					} else {
						// Use size-aware load: source (stack slot) -> destination (register)
						// Both sizes are explicit for clarity
						emitMovFromFrameSized(
							SizedRegister{target_reg, 64, false},  // dest: always load into 64-bit register
							SizedStackSlot{var_offset, arg.size_in_bits, isSignedType(arg.type)}  // source: sized stack slot
						);
						regAlloc.flushSingleDirtyRegister(target_reg);
					}
				} else if (std::holds_alternative<StringHandle>(arg.value)) {
					// Load variable
					StringHandle var_name_handle = std::get<StringHandle>(arg.value);
			[[maybe_unused]] std::string_view var_name = StringTable::getStringView(var_name_handle);
					int var_offset = variable_scopes.back().variables[var_name_handle].offset;
					if (is_float_arg) {
						// For floating-point, use movsd/movss into XMM register
						bool is_float = (arg.type == Type::Float);
						emitFloatMovFromFrame(target_reg, var_offset, is_float);
						
						// For varargs: floats must be promoted to double (C standard)
						if (call_op.is_variadic && is_float) {
							emitCvtss2sd(target_reg, target_reg);
						}
						
						// For varargs: also copy to corresponding INT register
						// System V AMD64 ABI does NOT require this
						if (call_op.is_variadic && i < max_int_regs && is_coff_format) {
							emitMovqXmmToGpr(target_reg, getIntParamReg<TWriterClass>(i));
						}
					} else {
						// Use size-aware load: source (stack slot) -> destination (register)
						emitMovFromFrameSized(
							SizedRegister{target_reg, 64, false},  // dest: always load into 64-bit register
							SizedStackSlot{var_offset, arg.size_in_bits, isSignedType(arg.type)}  // source: sized stack slot
						);
						regAlloc.flushSingleDirtyRegister(target_reg);
					}
				}
			}
			
			// For varargs functions on System V AMD64, set AL to number of XMM registers actually used
			if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
				if (call_op.is_variadic) {
					// Count XMM registers actually allocated (need to track float_reg_index)
					size_t xmm_count = 0;
					size_t va_temp_float_idx = 0;
					for (size_t i = 0; i < call_op.args.size(); ++i) {
						const auto& arg = call_op.args[i];
						if (is_floating_point_type(arg.type)) {
							if (va_temp_float_idx < max_float_regs) {
								xmm_count++;
								va_temp_float_idx++;
							}
						}
					}
					// Set AL (lower 8 bits of RAX) to the count
					// MOV AL, imm8: B0 + imm8
					textSectionData.push_back(0xB0);
					textSectionData.push_back(static_cast<uint8_t>(xmm_count));
				}
			}
			
			// If function uses return slot, pass the address of the result location as hidden first parameter
			if (call_op.uses_return_slot) {
				// Load address of return slot (result_offset) into first integer parameter register
				X64Register return_slot_reg = getIntParamReg<TWriterClass>(0);
				
				// LEA return_slot_reg, [RBP + result_offset]
				emitLeaFromFrame(return_slot_reg, result_offset);
				
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Passing return slot address (offset {}) in register {} for struct return",
					result_offset, static_cast<int>(return_slot_reg));
			}
			
			// Generate call instruction
			if (call_op.is_indirect_call) {
				// Indirect call: the function_name is actually the variable name holding the function pointer
				// Allocate a register using the register allocator, load the function pointer, then call through it
				StringHandle func_ptr_name = call_op.getFunctionName();
				int func_ptr_offset = variable_scopes.back().variables[func_ptr_name].offset;
				
				// Note: Both function pointers and function references are handled the same way here.
				// The reference variable holds the function address directly (function references
				// decay to function pointers, so we just load the 64-bit function address from the
				// stack location and call through it).
				
				// Allocate a scratch register for the indirect call
				X64Register call_reg = allocateRegisterWithSpilling();
				
				// Load the function pointer/reference value
				emitMovFromFrame(call_reg, func_ptr_offset);
				
				// Emit indirect call through the allocated register
				emitCallReg(textSectionData, call_reg);
				
				// Release the register after the call
				regAlloc.release(call_reg);
				
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Generated indirect call through {} at offset {}",
					static_cast<int>(call_reg), func_ptr_offset);
			} else {
				// Direct call: E8 + 32-bit relative offset
				std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
				textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());
				
				// Add relocation for function name (Phase 4: Use helper)
				StringHandle func_name_handle = call_op.getFunctionName();
				std::string mangled_name(StringTable::getStringView(func_name_handle));
				writer.add_relocation(textSectionData.size() - 4, mangled_name);
			}
			
			// Invalidate caller-saved registers (function calls clobber them)
			regAlloc.invalidateCallerSavedRegisters();
			
			// Phase 5: Copy elision opportunity detection
			// Check if this is a prvalue return being used to initialize a variable
			bool is_prvalue_return = isTempVarPRValue(call_op.result);
			
			FLASH_LOG_FORMAT(Codegen, Debug,
				"FunctionCall result: {} is_prvalue={}", 
				call_op.result.name(), is_prvalue_return);
			
			// Store return value - RAX for integers, XMM0 for floats
			// For struct returns using return slot, the struct is already in place - no copy needed
			if (call_op.return_type != Type::Void && !call_op.uses_return_slot) {
				if (is_floating_point_type(call_op.return_type)) {
					// Float return value is in XMM0
					bool is_float = (call_op.return_type == Type::Float);
					emitFloatMovToFrame(X64Register::XMM0, result_offset, is_float);
				} else {
					emitMovToFrameSized(
						SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
						SizedStackSlot{result_offset, return_size_bits, isSignedType(call_op.return_type)}  // dest
					);
				}
			} else if (call_op.uses_return_slot) {
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Struct return using return slot - struct already constructed at offset {}",
					result_offset);
			}
			
			// No stack cleanup needed after call:
			// - Windows x64 ABI: Uses pre-allocated shadow space, not PUSH
			// - Linux System V AMD64: Arguments in registers or pushed before call, stack pointer already adjusted
			
			return;
		}
		
		// All function calls should use typed payload (CallOp)
		// Legacy operand-based path has been removed for better maintainability
		assert(false && "Function call without typed payload - should not happen");
	}

	void handleConstructorCall(const IrInstruction& instruction) {
		// Constructor call format: ConstructorCallOp {struct_name, object, arguments}
		const ConstructorCallOp& ctor_op = instruction.getTypedPayload<ConstructorCallOp>();

		flushAllDirtyRegisters();

		std::string_view struct_name = StringTable::getStringView(ctor_op.struct_name);

		// Get the object's stack offset
		int object_offset = 0;
		bool object_is_pointer = false;  // Declare early so RVO branch can set it
		
		// If using return slot (RVO), get offset from return_slot_offset or look up __return_slot
		if (ctor_op.use_return_slot) {
			if (ctor_op.return_slot_offset.has_value()) {
				object_offset = ctor_op.return_slot_offset.value();
			} else {
				// Look up __return_slot in the variables map
				auto return_slot_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle("__return_slot"));
				if (return_slot_it != variable_scopes.back().variables.end()) {
					// __return_slot holds the address where we should construct
					// Load this address into RDI for the constructor call
					int return_slot_param_offset = return_slot_it->second.offset;
					X64Register dest_reg = X64Register::RDI;
					emitMovFromFrame(dest_reg, return_slot_param_offset);
					
					// Store the address in a temp location so we can use it as object_offset
					// Actually, we'll pass it differently - set object_is_pointer flag
					object_offset = return_slot_param_offset;
					object_is_pointer = true;  // The offset holds a pointer to where object should be
					
					FLASH_LOG_FORMAT(Codegen, Debug,
						"Constructor using RVO: loading return slot address from __return_slot at offset {}",
						return_slot_param_offset);
				} else {
					FLASH_LOG(Codegen, Error,
						"Constructor marked for RVO but __return_slot not found in variables");
					// Fall through to regular handling
				}
			}
			
			FLASH_LOG_FORMAT(Codegen, Debug,
				"Constructor using return slot (RVO) at offset {}",
				object_offset);
		} else if (std::holds_alternative<TempVar>(ctor_op.object)) {
			const TempVar temp_var = std::get<TempVar>(ctor_op.object);
			
			// Get struct size for proper stack allocation
			int struct_size_bits = 64;  // Default to 8 bytes
			auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
			if (struct_type_it != gTypesByName.end()) {
				const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
				if (struct_info) {
					struct_size_bits = static_cast<int>(struct_info->total_size * 8);  // Convert bytes to bits
					FLASH_LOG_FORMAT(Codegen, Debug,
						"Constructor for {} found struct_info with size {} bits",
						struct_name, struct_size_bits);
				} else {
					FLASH_LOG_FORMAT(Codegen, Debug,
						"Constructor for {} found in gTypesByName but no struct_info",
						struct_name);
				}
			} else {
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Constructor for {} NOT found in gTypesByName",
					struct_name);
			}
			
			// TempVars can be either stack-allocated or heap-allocated
			// Use is_heap_allocated flag to distinguish:
			// - Heap-allocated (from new): TempVar holds a pointer, use MOV to load it
			// - Stack-allocated (RVO/NRVO): TempVar is the object location, use LEA to get address
			object_offset = getStackOffsetFromTempVar(temp_var, struct_size_bits);
			object_is_pointer = ctor_op.is_heap_allocated;
		} else {
			StringHandle var_name_handle = std::get<StringHandle>(ctor_op.object);
			auto it = variable_scopes.back().variables.find(var_name_handle);
			if (it == variable_scopes.back().variables.end()) {
				throw std::runtime_error("Constructor call: variable not found in variables map: " + std::string(StringTable::getStringView(var_name_handle)));
			}
			object_offset = it->second.offset;
			object_is_pointer = (StringTable::getStringView(var_name_handle) == "this");
			
			// If this is an array element constructor call, adjust offset for the specific element
			if (ctor_op.array_index.has_value()) {
				// Look up struct size to calculate element offset
				auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
				if (struct_type_it != gTypesByName.end()) {
					const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
					if (struct_info) {
						size_t element_size = struct_info->total_size;
						size_t index = ctor_op.array_index.value();
						// Adjust offset: base_offset + (index * element_size)
						object_offset += static_cast<int>(index * element_size);
					}
				}
			}
		}

		// Load the address of the object into the first parameter register ('this' pointer)
		// Use platform-specific register: RDI on Linux, RCX on Windows
		X64Register this_reg = getIntParamReg<TWriterClass>(0);
		
		FLASH_LOG_FORMAT(Codegen, Debug,
			"Constructor call for {}: object_is_pointer={}, object_offset={}, base_class_offset={}",
			struct_name, object_is_pointer, object_offset, ctor_op.base_class_offset);
		
		if (object_is_pointer) {
			// For pointers (this, heap-allocated): reload the pointer value (not its address)
			// MOV this_reg, [RBP + object_offset]
			emitMovFromFrame(this_reg, object_offset);
			// Add base_class_offset for multiple inheritance (adjust pointer to base subobject)
			if (ctor_op.base_class_offset != 0) {
				emitAddRegImm32(textSectionData, this_reg, ctor_op.base_class_offset);
			}
		} else {
			// For regular stack objects: get the address
			// LEA this_reg, [RBP + object_offset + base_class_offset]
			// The base_class_offset adjusts for multiple inheritance
			auto lea_inst = generateLeaFromFrame(this_reg, object_offset + ctor_op.base_class_offset);
			textSectionData.insert(textSectionData.end(), lea_inst.op_codes.begin(), lea_inst.op_codes.begin() + lea_inst.size_in_bytes);
		}

		// Process constructor parameters (if any) - similar to function call
		const size_t num_params = ctor_op.arguments.size();

		// Look up the struct type once for use in both loops
		auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));

		// Find the actual constructor to get the correct parameter types
		// This is more reliable than trying to infer from argument types
		const ConstructorDeclarationNode* actual_ctor = nullptr;
		if (struct_type_it != gTypesByName.end()) {
			const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
			if (struct_info) {
				// Look for a constructor with matching number of parameters
				for (const auto& func : struct_info->member_functions) {
					if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
						const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
						const auto& params = ctor_node.parameter_nodes();
						if (params.size() == num_params) {
							actual_ctor = &ctor_node;
							break;
						}
					}
				}
			}
		}

		// Extract parameter types for overload resolution
		std::vector<TypeSpecifierNode> parameter_types;
		
		// If we found the actual constructor, use its parameter types directly
		if (actual_ctor) {
			const auto& ctor_params = actual_ctor->parameter_nodes();
			for (size_t i = 0; i < num_params && i < ctor_params.size(); ++i) {
				if (ctor_params[i].is<DeclarationNode>()) {
					const auto& param_decl = ctor_params[i].as<DeclarationNode>();
					if (param_decl.type_node().is<TypeSpecifierNode>()) {
						const auto& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();
						parameter_types.push_back(param_type_spec);
						continue;
					}
				}
				// Fallback: if we can't get the param type, create a default one
				const TypedValue& arg = ctor_op.arguments[i];
				parameter_types.push_back(TypeSpecifierNode(arg.type, TypeQualifier::None, static_cast<unsigned char>(arg.size_in_bits), Token{}));
			}
		} else {
			// Fallback to old logic: infer from argument types
			for (size_t i = 0; i < num_params; ++i) {
				const TypedValue& arg = ctor_op.arguments[i];
				Type paramType = arg.type;
				int paramSize = arg.size_in_bits;
				TypeIndex arg_type_index = arg.type_index;
				bool arg_is_reference = arg.is_reference;  // Check if marked as reference
				int arg_pointer_depth = arg.pointer_depth;
				CVQualifier arg_cv_qualifier = arg.cv_qualifier;
				
				// Build TypeSpecifierNode for this parameter
				// For pointers, use the base type size, not the pointer size (64 bits)
				int actual_size = paramSize;
				if (arg_pointer_depth > 0) {
					// This is a pointer - set size to pointee type size
					// For basic types, use get_type_size_bits
					int basic_size = get_type_size_bits(paramType);
					if (basic_size > 0) {
						actual_size = basic_size;
					}
					// For struct types, keep the size as-is (basic_size will be 0)
				}
				
				TypeSpecifierNode param_type(paramType, TypeQualifier::None, static_cast<unsigned char>(actual_size), Token{}, arg_cv_qualifier);
				
				// Add pointer levels
				for (int p = 0; p < arg_pointer_depth; ++p) {
					param_type.add_pointer_level(CVQualifier::None);
				}
				
				// If the argument is marked as a reference, set it as such
				if (arg_is_reference) {
					param_type.set_reference(false);  // set_reference(false) creates an lvalue reference
				}
				
				// For copy/move constructors: if parameter is the same struct type, it should be a reference
				// Copy constructor: Type(Type& other) or Type(const Type& other) -> paramType == Type::Struct and same as struct_name
				// We detect this by checking if paramType is Struct and num_params == 1 AND the type_index matches
				bool is_same_struct_type = false;
				if (struct_type_it != gTypesByName.end() && arg_type_index != 0) {
					is_same_struct_type = (arg_type_index == struct_type_it->second->type_index_);
				}
				
				if (num_params == 1 && paramType == Type::Struct && is_same_struct_type && !arg_is_reference) {
					// This is likely a copy constructor, but arg_is_reference wasn't set
					// Determine the actual CV qualifier from the constructor signature
					auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
					if (type_it != gTypesByName.end()) {
						TypeIndex struct_type_index = type_it->second->type_index_;
						const StructTypeInfo* struct_info = type_it->second->getStructInfo();
						
						// Default to const reference (standard implicit copy constructor)
						CVQualifier copy_ctor_cv = CVQualifier::Const;
						
						// Check if there's an explicit copy constructor with a different signature
						if (struct_info) {
							const StructMemberFunction* copy_ctor = struct_info->findCopyConstructor();
							if (copy_ctor && copy_ctor->function_decl.is<ConstructorDeclarationNode>()) {
								const auto& ctor_node = copy_ctor->function_decl.as<ConstructorDeclarationNode>();
								const auto& params = ctor_node.parameter_nodes();
								if (params.size() == 1 && params[0].is<DeclarationNode>()) {
									const auto& param_decl = params[0].as<DeclarationNode>();
									if (param_decl.type_node().is<TypeSpecifierNode>()) {
										const auto& ctor_param_type = param_decl.type_node().as<TypeSpecifierNode>();
										copy_ctor_cv = ctor_param_type.cv_qualifier();
									}
								}
							}
						}
						
						param_type = TypeSpecifierNode(paramType, struct_type_index, static_cast<unsigned char>(actual_size), Token{}, copy_ctor_cv);
						param_type.set_reference(false);  // set_reference(false) creates an lvalue reference (not rvalue)
					}
				} else if (paramType == Type::Struct && arg_type_index != 0) {
					// Not a copy constructor, but still a struct parameter - set the type_index
					param_type = TypeSpecifierNode(paramType, arg_type_index, static_cast<unsigned char>(actual_size), Token{}, arg_cv_qualifier);
					// Add pointer levels (rebuild after creating with type_index)
					for (int p = 0; p < arg_pointer_depth; ++p) {
						param_type.add_pointer_level(CVQualifier::None);
					}
					// Also preserve the reference flag if it was set
					if (arg_is_reference) {
						param_type.set_reference(false);  // set_reference(false) creates an lvalue reference
					}
				}
				
				parameter_types.push_back(param_type);
			}  // End of fallback for loop
		}  // End of if (actual_ctor) else block

		// Load parameters into registers
		// Max additional params: 5 on Linux (RDI is 'this', RSI-R9 for params), 3 on Windows (RCX is 'this', RDX-R9 for params)
		const size_t max_register_params = getMaxIntParamRegs<TWriterClass>() - 1;  // Subtract 1 for 'this' pointer
		for (size_t i = 0; i < num_params && i < max_register_params; ++i) {
			const TypedValue& arg = ctor_op.arguments[i];
			Type paramType = arg.type;
			int paramSize = arg.size_in_bits;
			TypeIndex arg_type_index = arg.type_index;
			const IrValue& paramValue = arg.value;
			bool arg_is_reference = arg.is_reference;  // Check if marked as reference

			// Skip first register (this pointer): RCX on Windows, RDI on Linux
			X64Register target_reg = getIntParamReg<TWriterClass>(i + 1);

			// Check if this is a reference parameter (copy/move constructor - same struct type, OR marked as reference)
			bool is_same_struct_type = false;
			if (struct_type_it != gTypesByName.end() && arg_type_index != 0) {
				is_same_struct_type = (arg_type_index == struct_type_it->second->type_index_);
			}
			bool is_reference_param = arg_is_reference || (num_params == 1 && paramType == Type::Struct && is_same_struct_type);

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
					// For reference parameters, check if the temp var already holds a pointer
					// (e.g., from addressof operation). If so, load the pointer value (MOV),
					// otherwise take the address of the variable (LEA).
					auto ref_it = reference_stack_info_.find(param_offset);
					if (ref_it != reference_stack_info_.end()) {
						// Temp var holds a pointer - load it
						emitMovFromFrame(target_reg, param_offset);
					} else {
						// Temp var holds a value - take its address
						emitLeaFromFrame(target_reg, param_offset);
					}
				} else {
					// For value parameters: source (sized stack slot) -> dest (64-bit register)
					emitMovFromFrameSized(
						SizedRegister{target_reg, 64, false},  // dest: 64-bit register
						SizedStackSlot{param_offset, paramSize, isSignedType(paramType)}  // source: sized stack slot
					);
				}
			} else if (std::holds_alternative<StringHandle>(paramValue)) {
				// Load from variable
				StringHandle var_name_handle = std::get<StringHandle>(paramValue);
				auto it = variable_scopes.back().variables.find(var_name_handle);
				if (it != variable_scopes.back().variables.end()) {
					int param_offset = it->second.offset;
					// For large struct parameters (> 64 bits), pass by pointer according to System V AMD64 ABI
					// This includes std::initializer_list which is 128 bits (16 bytes)
					bool pass_by_pointer = is_reference_param || (paramType == Type::Struct && paramSize > 64);
					if (pass_by_pointer) {
						// For reference parameters or large structs, load address (LEA)
						// LEA target_reg, [RBP + param_offset]
						emitLeaFromFrame(target_reg, param_offset);
					} else {
						// For value parameters: source (sized stack slot) -> dest (64-bit register)
						emitMovFromFrameSized(
							SizedRegister{target_reg, 64, false},  // dest: 64-bit register
							SizedStackSlot{param_offset, paramSize, isSignedType(paramType)}  // source: sized stack slot
						);
					}
				}
			}
		}

		// Generate the call instruction
		// For constructors, the function name is the last component of the class name
		// For nested classes like "Outer::Inner", function_name="Inner" and class_name="Outer::Inner"
		std::string function_name;
		std::string class_name;
		size_t last_colon_pos = struct_name.rfind("::");
		if (last_colon_pos != std::string::npos) {
			// Nested class: "Outer::Inner" -> function="Inner", class="Outer::Inner" (full name)
			function_name = struct_name.substr(last_colon_pos + 2);
			class_name = struct_name;  // Keep full name for proper constructor detection
		} else {
			// Regular class: function_name = class_name = struct_name
			function_name = struct_name;
			class_name = struct_name;
		}
		
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());

		// Build FunctionSignature for proper overload resolution
		TypeSpecifierNode void_return(Type::Void, TypeQualifier::None, 0, Token{});
		ObjectFileWriter::FunctionSignature sig(void_return, parameter_types);
		sig.class_name = class_name;

		// Generate the correct mangled name for this specific constructor overload
		auto mangled_name = writer.generateMangledName(function_name, sig);
		writer.add_relocation(textSectionData.size() - 4, mangled_name);
		
		// Invalidate caller-saved registers (function calls clobber them)
		regAlloc.invalidateCallerSavedRegisters();

		regAlloc.reset();
	}

	void handleDestructorCall(const IrInstruction& instruction) {
		// Destructor call format: DestructorCallOp {struct_name, object}
		const DestructorCallOp& dtor_op = instruction.getTypedPayload<DestructorCallOp>();

		flushAllDirtyRegisters();

		std::string_view struct_name = StringTable::getStringView(dtor_op.struct_name);

		// Get the object's stack offset
		int object_offset = 0;
		if (std::holds_alternative<TempVar>(dtor_op.object)) {
			const TempVar temp_var = std::get<TempVar>(dtor_op.object);
			object_offset = getStackOffsetFromTempVar(temp_var);
		} else {
			StringHandle var_name_handle = std::get<StringHandle>(dtor_op.object);
			auto it = variable_scopes.back().variables.find(var_name_handle);
			if (it == variable_scopes.back().variables.end()) {
				throw std::runtime_error("Destructor call: variable not found in variables map: " + std::string(StringTable::getStringView(var_name_handle)));
			}
			object_offset = it->second.offset;
		}

		// Check if the object is a pointer (needs to be loaded, not addressed)
		// This includes 'this' pointer and TempVars from heap_alloc
		bool object_is_pointer = false;
		if (std::holds_alternative<TempVar>(dtor_op.object)) {
			// TempVars are always pointers in destructor calls (from heap_free)
			object_is_pointer = true;
		} else if (std::holds_alternative<StringHandle>(dtor_op.object)) {
			StringHandle obj_handle = std::get<StringHandle>(dtor_op.object);
			object_is_pointer = (StringTable::getStringView(obj_handle) == "this");
		}

		// Load the address of the object into the first parameter register ('this' pointer)
		// Use platform-specific register: RDI on Linux, RCX on Windows
		X64Register this_reg = getIntParamReg<TWriterClass>(0);
		if (object_is_pointer) {
			// For pointers (this, heap-allocated): reload the pointer value (not its address)
			// MOV this_reg, [RBP + object_offset]
			emitMovFromFrame(this_reg, object_offset);
		} else {
			// For regular stack objects: get the address
			// LEA this_reg, [RBP + object_offset]
			emitLeaFromFrame(this_reg, object_offset);
		}

		// Generate the call instruction
		// For nested classes, split "Outer::Inner" into class="Outer" and function="~Inner"
		std::string function_name;
		std::string class_name;
		size_t last_colon_pos = struct_name.rfind("::");
		if (last_colon_pos != std::string::npos) {
			// Nested class: "Outer::Inner" -> class="Outer", function="~Inner"
			class_name = std::string(struct_name.substr(0, last_colon_pos));
			function_name = std::string("~") + std::string(struct_name.substr(last_colon_pos + 2));
		} else {
			// Regular class: function_name = "~ClassName", class_name = struct_name
			function_name = std::string("~") + std::string(struct_name);
			class_name = std::string(struct_name);
		}
		
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());

		// Build FunctionSignature for destructor (destructors take no parameters and return void)
		std::vector<TypeSpecifierNode> empty_params;  // Destructors have no parameters
		TypeSpecifierNode void_return(Type::Void, TypeQualifier::None, 0, Token{});
		ObjectFileWriter::FunctionSignature sig(void_return, empty_params);
		sig.class_name = class_name;

		// Generate the correct mangled name for the destructor
		auto mangled_name = writer.generateMangledName(function_name, sig);
		writer.add_relocation(textSectionData.size() - 4, mangled_name);
		
		// Invalidate caller-saved registers (function calls clobber them)
		regAlloc.invalidateCallerSavedRegisters();

		regAlloc.reset();
	}

	void handleVirtualCall(const IrInstruction& instruction) {
		// Extract VirtualCallOp typed payload
		const VirtualCallOp& op = instruction.getTypedPayload<VirtualCallOp>();

		flushAllDirtyRegisters();

		// Get result offset
		assert(std::holds_alternative<TempVar>(op.result.value) && "VirtualCallOp result must be a TempVar");
		const TempVar& result_var = std::get<TempVar>(op.result.value);
		int result_offset = getStackOffsetFromTempVar(result_var);
		variable_scopes.back().variables[StringTable::getOrInternStringHandle(result_var.name())].offset = result_offset;

		// Get object offset
		int object_offset = 0;
		if (std::holds_alternative<TempVar>(op.object)) {
			const TempVar& temp_var = std::get<TempVar>(op.object);
			object_offset = getStackOffsetFromTempVar(temp_var);
		} else {
			StringHandle var_name_handle = std::get<StringHandle>(op.object);
			[[maybe_unused]] std::string_view var_name = StringTable::getStringView(var_name_handle);
			object_offset = variable_scopes.back().variables[var_name_handle].offset;
		}

		// Virtual call sequence varies based on whether object is a pointer or direct:
		// For pointers (is_pointer_access == true, e.g., ptr->method()):
		//   1. Load object pointer value → 2. Load vptr from [pointer] → 3. Load func from [vptr + index*8] → 4. Call
		// For direct objects (is_pointer_access == false, e.g., obj.method()):
		//   1. Get object address → 2. Load vptr from [address] → 3. Load func from [vptr + index*8] → 4. Call

		const X64Register this_reg = getIntParamReg<TWriterClass>(0); // First parameter register

		// Use is_pointer_access flag to determine if object is a pointer or direct object
		// Previously we used (op.object_size == 64) but that's wrong for small structs (like those with only vptr)
		bool is_pointer_object = op.is_pointer_access;

		if (is_pointer_object) {
			// Step 1a: Load pointer value from stack into this_reg
			// MOV this_reg, [RBP + object_offset]
			emitMovFromFrame(this_reg, object_offset);

			// Step 2a: Load vptr from object (dereference the pointer)
			// MOV RAX, [this_reg + 0]
			emitMovRegFromMemReg(X64Register::RAX, this_reg);
		} else {
			// Step 1b: Load object address into this_reg
			// LEA this_reg, [RBP + object_offset]
			emitLeaFromFrame(this_reg, object_offset);

			// Step 2b: Load vptr from object (object address is in this_reg)
			// MOV RAX, [this_reg + 0]
			emitMovRegFromMemReg(X64Register::RAX, this_reg);
		}

		// Step 3: Load function pointer from vtable into RAX
		// MOV RAX, [RAX + vtable_index * 8]
		int vtable_offset = op.vtable_index * 8;
		if (vtable_offset == 0) {
			// No offset, use simple dereference
			emitMovRegFromMemReg(X64Register::RAX, X64Register::RAX);
		} else if (vtable_offset >= -128 && vtable_offset <= 127) {
			// Use 8-bit displacement
			emitMovRegFromMemRegDisp8(X64Register::RAX, X64Register::RAX, static_cast<int8_t>(vtable_offset));
		} else {
			// Use 32-bit displacement with emitMovFromMemory
			emitMovFromMemory(X64Register::RAX, X64Register::RAX, vtable_offset, 8);
		}

		// Step 4: 'this' pointer is already in the correct register from Step 1
		// No need to recalculate or reload - it's preserved throughout

		// Step 5: Handle additional function arguments (beyond 'this')
		// Virtual member functions have 'this' as first parameter (already in this_reg)
		// Additional arguments start at parameter index 1
		if (!op.arguments.empty()) {
			// Get platform-specific parameter counts
			size_t max_int_regs = getMaxIntParamRegs<TWriterClass>();
			size_t max_float_regs = getMaxFloatParamRegs<TWriterClass>();
			size_t shadow_space = getShadowSpaceSize<TWriterClass>();
			
			// Start at index 1 because 'this' is already in parameter register 0
			size_t int_reg_index = 1;
			size_t float_reg_index = 0;
			size_t stack_arg_count = 0;
			
			// First pass: handle stack arguments
			for (size_t i = 0; i < op.arguments.size(); ++i) {
				const auto& arg = op.arguments[i];
				bool is_float_arg = is_floating_point_type(arg.type);
				
				bool use_register = false;
				if (is_float_arg) {
					use_register = (float_reg_index < max_float_regs);
					float_reg_index++;
				} else {
					use_register = (int_reg_index < max_int_regs);
					int_reg_index++;
				}
				
				if (!use_register) {
					// Argument goes on stack
					int stack_offset = static_cast<int>(shadow_space + stack_arg_count * 8);
					X64Register temp_reg = loadTypedValueIntoRegister(arg);
					emitStoreToRSP(textSectionData, temp_reg, stack_offset);
					regAlloc.release(temp_reg);
					stack_arg_count++;
				}
			}
			
			// Second pass: handle register arguments
			int_reg_index = 1;  // Reset, 'this' is in register 0
			float_reg_index = 0;
			
			for (size_t i = 0; i < op.arguments.size(); ++i) {
				const auto& arg = op.arguments[i];
				bool is_float_arg = is_floating_point_type(arg.type);
				
				bool use_register = false;
				X64Register target_reg;
				if (is_float_arg) {
					if (float_reg_index < max_float_regs) {
						use_register = true;
						target_reg = getFloatParamReg<TWriterClass>(float_reg_index);
					}
					float_reg_index++;
				} else {
					if (int_reg_index < max_int_regs) {
						use_register = true;
						target_reg = getIntParamReg<TWriterClass>(int_reg_index);
					}
					int_reg_index++;
				}
				
				if (use_register) {
					// Load argument into parameter register
					if (is_float_arg) {
						// Handle float arguments
						if (std::holds_alternative<double>(arg.value)) {
							double float_value = std::get<double>(arg.value);
							uint64_t bits;
							if (arg.type == Type::Float) {
								float float_val = static_cast<float>(float_value);
								uint32_t float_bits;
								std::memcpy(&float_bits, &float_val, sizeof(float_bits));
								bits = float_bits;
							} else {
								std::memcpy(&bits, &float_value, sizeof(bits));
							}
							X64Register temp_gpr = allocateRegisterWithSpilling();
							emitMovImm64(temp_gpr, bits);
							emitMovqGprToXmm(temp_gpr, target_reg);
							regAlloc.release(temp_gpr);
						} else if (std::holds_alternative<TempVar>(arg.value)) {
							const auto& temp_var = std::get<TempVar>(arg.value);
							int var_offset = getStackOffsetFromTempVar(temp_var);
							bool is_float = (arg.type == Type::Float);
							emitFloatMovFromFrame(target_reg, var_offset, is_float);
						} else if (std::holds_alternative<StringHandle>(arg.value)) {
							StringHandle var_name_handle = std::get<StringHandle>(arg.value);
							int var_offset = variable_scopes.back().variables[var_name_handle].offset;
							bool is_float = (arg.type == Type::Float);
							emitFloatMovFromFrame(target_reg, var_offset, is_float);
						}
					} else {
						// Handle integer/pointer arguments
						if (std::holds_alternative<unsigned long long>(arg.value)) {
							uint64_t imm_value = std::get<unsigned long long>(arg.value);
							emitMovImm64(target_reg, imm_value);
						} else if (std::holds_alternative<TempVar>(arg.value)) {
							const auto& temp_var = std::get<TempVar>(arg.value);
							int var_offset = getStackOffsetFromTempVar(temp_var);
							emitMovFromFrame(target_reg, var_offset);
						} else if (std::holds_alternative<StringHandle>(arg.value)) {
							StringHandle var_name_handle = std::get<StringHandle>(arg.value);
							int var_offset = variable_scopes.back().variables[var_name_handle].offset;
							emitMovFromFrame(target_reg, var_offset);
						}
					}
				}
			}
		}

		// Step 6: Call through function pointer in RAX
		// CALL RAX
		textSectionData.push_back(0xFF); // CALL r/m64
		textSectionData.push_back(0xD0); // ModR/M: RAX

		// Step 7: Store return value from RAX to result variable using the correct size
		if (op.result.type != Type::Void) {
			emitMovToFrameSized(
				SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
				SizedStackSlot{result_offset, op.result.size_in_bits, isSignedType(op.result.type)}  // dest
			);
		}

		regAlloc.reset();
	}

	void handleHeapAlloc(const IrInstruction& instruction) {
		const HeapAllocOp& op = instruction.getTypedPayload<HeapAllocOp>();

		flushAllDirtyRegisters();

		// Call malloc(size)
		// Windows x64 calling convention: RCX = first parameter (size)

		// Move size into RCX (first parameter)
		// MOV RCX, size_in_bytes
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0xC7); // MOV r/m64, imm32
		textSectionData.push_back(0xC1); // ModR/M for RCX
		for (int i = 0; i < 4; ++i) {
			textSectionData.push_back(static_cast<uint8_t>((op.size_in_bytes >> (i * 8)) & 0xFF));
		}

		// Call malloc
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());
		writer.add_relocation(textSectionData.size() - 4, "malloc");
		
		// Invalidate caller-saved registers (function calls clobber them)
		regAlloc.invalidateCallerSavedRegisters();

		// Result is in RAX, store it to the result variable (pointer is always 64-bit)
		int result_offset = getStackOffsetFromTempVar(op.result);

		emitMovToFrameSized(
			SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
		);

		regAlloc.reset();
	}

	void handleHeapAllocArray(const IrInstruction& instruction) {
		const HeapAllocArrayOp& op = instruction.getTypedPayload<HeapAllocArrayOp>();

		flushAllDirtyRegisters();

		// Load count into RAX - handle TempVar, std::string_view (identifier), and constant values
		// Array counts are typically size_t (unsigned 64-bit on x64)
		if (std::holds_alternative<TempVar>(op.count)) {
			// Count is a TempVar - load from stack (assume 64-bit for size_t)
			TempVar count_var = std::get<TempVar>(op.count);
			int count_offset = getStackOffsetFromTempVar(count_var);
			emitMovFromFrameSized(
				SizedRegister{X64Register::RAX, 64, false},
				SizedStackSlot{count_offset, 64, false}  // size_t is 64-bit unsigned
			);
		} else if (std::holds_alternative<StringHandle>(op.count)) {
			// Count is an identifier (variable name) - load from stack
			StringHandle count_name_handle = std::get<StringHandle>(op.count);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(count_name_handle);
			if (it == current_scope.variables.end()) {
				assert(false && "Array size variable not found in scope");
				return;
			}
			int count_offset = it->second.offset;
			emitMovFromFrameSized(
				SizedRegister{X64Register::RAX, 64, false},
				SizedStackSlot{count_offset, 64, false}  // size_t is 64-bit unsigned
			);
		} else if (std::holds_alternative<unsigned long long>(op.count)) {
			// Count is a constant - load immediate value
			uint64_t count_value = std::get<unsigned long long>(op.count);

			// MOV RAX, immediate
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0xB8); // MOV RAX, imm64
			for (int i = 0; i < 8; ++i) {
				textSectionData.push_back(static_cast<uint8_t>((count_value >> (i * 8)) & 0xFF));
			}
		} else {
			assert(false && "Count must be TempVar, std::string_view, or unsigned long long");
		}

		// Multiply count by element_size: IMUL RAX, element_size
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x69); // IMUL r64, r/m64, imm32
		textSectionData.push_back(0xC0); // ModR/M: RAX, RAX
		for (int i = 0; i < 4; ++i) {
			textSectionData.push_back(static_cast<uint8_t>((op.size_in_bytes >> (i * 8)) & 0xFF));
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
		
		// Invalidate caller-saved registers (function calls clobber them)
		regAlloc.invalidateCallerSavedRegisters();

		// Result is in RAX, store it to the result variable (pointer is always 64-bit)
		int result_offset = getStackOffsetFromTempVar(op.result);

		emitMovToFrameSized(
			SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
		);

		regAlloc.reset();
	}

	void handleHeapFree(const IrInstruction& instruction) {
		const HeapFreeOp& op = instruction.getTypedPayload<HeapFreeOp>();

		flushAllDirtyRegisters();

		// Get the pointer offset (from either TempVar or identifier)
		int ptr_offset = 0;
		if (std::holds_alternative<TempVar>(op.pointer)) {
			TempVar ptr_var = std::get<TempVar>(op.pointer);
			ptr_offset = getStackOffsetFromTempVar(ptr_var);
		} else if (std::holds_alternative<StringHandle>(op.pointer)) {
			StringHandle var_name_handle = std::get<StringHandle>(op.pointer);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(var_name_handle);
			if (it == current_scope.variables.end()) {
				assert(false && "Variable not found in scope");
				return;
			}
			ptr_offset = it->second.offset;
		} else {
			assert(false && "HeapFree pointer must be TempVar or std::string_view");
			return;
		}

		// Load pointer from stack into RCX (first parameter for free)
		emitMovFromFrame(X64Register::RCX, ptr_offset);

		// Call free
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());
		writer.add_relocation(textSectionData.size() - 4, "free");
		
		// Invalidate caller-saved registers (function calls clobber them)
		regAlloc.invalidateCallerSavedRegisters();

		regAlloc.reset();
	}

	void handleHeapFreeArray(const IrInstruction& instruction) {
		// HeapFreeArray format: same as HeapFree (just has a pointer)
		// For C++, delete[] is the same as delete for POD types (just calls free)
		// For types with destructors, we'd need to call destructors for each element first
		// This is a simplified implementation
		const HeapFreeArrayOp& op = instruction.getTypedPayload<HeapFreeArrayOp>();

		flushAllDirtyRegisters();

		// Get the pointer offset (from either TempVar or identifier)
		int ptr_offset = 0;
		if (std::holds_alternative<TempVar>(op.pointer)) {
			TempVar ptr_var = std::get<TempVar>(op.pointer);
			ptr_offset = getStackOffsetFromTempVar(ptr_var);
		} else if (std::holds_alternative<StringHandle>(op.pointer)) {
			StringHandle var_name_handle = std::get<StringHandle>(op.pointer);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(var_name_handle);
			if (it == current_scope.variables.end()) {
				assert(false && "Variable not found in scope");
				return;
			}
			ptr_offset = it->second.offset;
		} else {
			assert(false && "HeapFreeArray pointer must be TempVar or std::string_view");
			return;
		}

		// Load pointer from stack into RCX (first parameter for free)
		emitMovFromFrame(X64Register::RCX, ptr_offset);

		// Call free
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());
		writer.add_relocation(textSectionData.size() - 4, "free");
		
		// Invalidate caller-saved registers (function calls clobber them)
		regAlloc.invalidateCallerSavedRegisters();

		regAlloc.reset();
	}

	void handlePlacementNew(const IrInstruction& instruction) {
		const PlacementNewOp& op = instruction.getTypedPayload<PlacementNewOp>();

		flushAllDirtyRegisters();

		// Load the placement address into RAX
		// The address can be a TempVar, identifier, or constant
		if (std::holds_alternative<TempVar>(op.address)) {
			// Address is a TempVar - load from stack
			TempVar address_var = std::get<TempVar>(op.address);
			int address_offset = getStackOffsetFromTempVar(address_var);
			emitMovFromFrame(X64Register::RAX, address_offset);
		} else if (std::holds_alternative<StringHandle>(op.address)) {
			// Address is an identifier (variable name) - load from stack
			StringHandle address_name_handle = std::get<StringHandle>(op.address);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(address_name_handle);
			if (it == current_scope.variables.end()) {
				assert(false && "Placement address variable not found in scope");
				return;
			}
			int address_offset = it->second.offset;
			emitMovFromFrame(X64Register::RAX, address_offset);
		} else if (std::holds_alternative<unsigned long long>(op.address)) {
			// Address is a constant - load immediate value
			uint64_t address_value = std::get<unsigned long long>(op.address);
			emitMovImm64(X64Register::RAX, address_value);
		} else {
			assert(false && "Placement address must be TempVar, identifier, or unsigned long long");
			return;
		}

		// Store the placement address to the result variable (pointer is always 64-bit)
		// No malloc call - we just use the provided address
		int result_offset = getStackOffsetFromTempVar(op.result);
		emitMovToFrameSized(
			SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
		);

		regAlloc.reset();
	}

	void handleTypeid(const IrInstruction& instruction) {
		// Typeid: returns pointer to type_info
		auto& op = instruction.getTypedPayload<TypeidOp>();

		flushAllDirtyRegisters();

		if (op.is_type) {
			// typeid(Type) - compile-time constant
			// For now, return a dummy pointer (in a full implementation, we'd have a .rdata section with type_info)
			StringHandle type_name_handle = std::get<StringHandle>(op.operand);
			std::string_view type_name = StringTable::getStringView(type_name_handle);

			// Load address of type_info into RAX (using a placeholder address for now)
			// In a real implementation, we'd have a symbol for each type's RTTI data
			// Use a hash of the type name as a placeholder address
			size_t type_hash = std::hash<std::string_view>{}(type_name);
			emitMovImm64(X64Register::RAX, type_hash);
		} else {
			// typeid(expr) - may need runtime lookup for polymorphic types
			// For polymorphic types, RTTI pointer is at vtable[-1]
			// For non-polymorphic types, return compile-time constant

			// Load the expression result (should be a pointer to object)
			TempVar expr_var = std::get<TempVar>(op.operand);
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
		int result_offset = getStackOffsetFromTempVar(op.result);
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
		// DynamicCast: Returns nullptr for failed pointer casts, throws for failed reference casts
		auto& op = instruction.getTypedPayload<DynamicCastOp>();

		flushAllDirtyRegisters();

		// Mark that we need the dynamic_cast runtime helpers
		needs_dynamic_cast_runtime_ = true;

		// Implementation using auto-generated runtime helper __dynamic_cast_check
		// (Generated at end of compilation - see emit_dynamic_cast_check_function)
		// 
		// C++ equivalent logic:
		//   bool __dynamic_cast_check(RTTIInfo* source, RTTIInfo* target) {
		//     if (!source || !target) return false;
		//     if (source == target) return true;
		//     if (source->class_hash == target->class_hash) return true;
		//     // Check each base class recursively
		//     for (size_t i = 0; i < source->num_bases && i < 64; i++) {
		//       if (__dynamic_cast_check(source->base_ptrs[i], target)) return true;
		//     }
		//     return false;
		//   }
		//
		// Calling convention: Windows x64 (first 4 args in RCX, RDX, R8, R9)
		// Arguments:
		//   RCX = source RTTI pointer (loaded from vtable[-1])
		//   RDX = target RTTI pointer
		// Returns: RAX = 1 if cast is valid, 0 otherwise
		
		// Step 1: Load source pointer from stack
		int source_offset = getStackOffsetFromTempVar(op.source);
		emitMovFromFrame(X64Register::RAX, source_offset);

		// Step 2: Save source pointer to R8 (we'll need it later if cast succeeds)
		emitMovRegReg(X64Register::R8, X64Register::RAX);

		// Step 3: Check if source pointer is null
		emitTestRegReg(X64Register::RAX);

		// JZ to null_result (if source is null, return null)
		textSectionData.push_back(0x0F); // Two-byte opcode prefix
		textSectionData.push_back(0x84); // JZ rel32
		size_t null_check_offset = textSectionData.size();
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Step 4: Load vtable pointer from object (first 8 bytes)
		emitMovRegFromMemReg(X64Register::RAX, X64Register::RAX);

		// Step 5: Load source RTTI pointer from vtable[-1] into first parameter register
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// Linux: First parameter in RDI
			emitMovRegFromMemRegDisp8(X64Register::RDI, X64Register::RAX, -8);
		} else {
			// Windows: First parameter in RCX
			emitMovRegFromMemRegDisp8(X64Register::RCX, X64Register::RAX, -8);
		}

		// Step 6: Load target RTTI pointer into second parameter register
		// Generate platform-specific RTTI symbol
		StringBuilder sb;
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// Linux/ELF: Use Itanium C++ ABI typeinfo symbol: _ZTI<length><classname>
			// Example: class "Derived" -> "_ZTI7Derived"
			sb.append("_ZTI");
			sb.append(op.target_type_name.length());
			sb.append(op.target_type_name);
		} else {
			// Windows/COFF: Use MSVC Complete Object Locator symbol: ??_R4.?AV<classname>@@6B@
			sb.append("??_R4.?AV");
			sb.append(op.target_type_name);
			sb.append("@@6B@");
		}
		std::string_view target_rtti_symbol = sb.commit();
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// Linux: Second parameter in RSI
			emitLeaRipRelativeWithRelocation(X64Register::RSI, target_rtti_symbol);
		} else {
			// Windows: Second parameter in RDX
			emitLeaRipRelativeWithRelocation(X64Register::RDX, target_rtti_symbol);
		}

		// Step 7: Call __dynamic_cast_check(source_rtti, target_rtti)
		if constexpr (!std::is_same_v<TWriterClass, ElfFileWriter>) {
			emitSubRSP(32);  // Shadow space for Windows x64 calling convention
		}
		emitCall("__dynamic_cast_check");
		if constexpr (!std::is_same_v<TWriterClass, ElfFileWriter>) {
			emitAddRSP(32);  // Restore stack
		}

		// Step 8: Check return value (RAX contains 0 or 1)
		emitTestAL();

		// JZ to null_result (if check failed, return null)
		textSectionData.push_back(0x0F); // Two-byte opcode prefix
		textSectionData.push_back(0x84); // JZ rel32
		size_t check_failed_offset = textSectionData.size();
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Step 9: Cast succeeded - return source pointer (which we saved in R8)
		emitMovRegReg(X64Register::RAX, X64Register::R8);

		// JMP to end
		textSectionData.push_back(0xEB); // JMP rel8
		size_t success_jmp_offset = textSectionData.size();
		textSectionData.push_back(0x00); // Placeholder

		// null_result label:
		size_t null_result_offset = textSectionData.size();
		
		// Check if this is a reference cast (needs to throw exception on failure)
		if (op.is_reference) {
			// For reference casts, throw std::bad_cast instead of returning nullptr
			// Call __dynamic_cast_throw_bad_cast (no arguments, never returns)
			if constexpr (!std::is_same_v<TWriterClass, ElfFileWriter>) {
				emitSubRSP(32);  // Shadow space for Windows x64 calling convention
			}
			emitCall("__dynamic_cast_throw_bad_cast");
			// Note: We don't restore RSP or add code after this because __dynamic_cast_throw_bad_cast never returns
		} else {
			// For pointer casts, return nullptr
			// XOR RAX, RAX  ; set result to nullptr
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x31); // XOR r64, r64
			textSectionData.push_back(0xC0); // ModR/M: RAX, RAX
		}

		// end label:
		size_t end_offset = textSectionData.size();

		// Patch jump offsets
		int32_t null_check_delta = static_cast<int32_t>(null_result_offset - null_check_offset - 4);
		textSectionData[null_check_offset + 0] = static_cast<uint8_t>(null_check_delta & 0xFF);
		textSectionData[null_check_offset + 1] = static_cast<uint8_t>((null_check_delta >> 8) & 0xFF);
		textSectionData[null_check_offset + 2] = static_cast<uint8_t>((null_check_delta >> 16) & 0xFF);
		textSectionData[null_check_offset + 3] = static_cast<uint8_t>((null_check_delta >> 24) & 0xFF);

		int32_t check_failed_delta = static_cast<int32_t>(null_result_offset - check_failed_offset - 4);
		textSectionData[check_failed_offset + 0] = static_cast<uint8_t>(check_failed_delta & 0xFF);
		textSectionData[check_failed_offset + 1] = static_cast<uint8_t>((check_failed_delta >> 8) & 0xFF);
		textSectionData[check_failed_offset + 2] = static_cast<uint8_t>((check_failed_delta >> 16) & 0xFF);
		textSectionData[check_failed_offset + 3] = static_cast<uint8_t>((check_failed_delta >> 24) & 0xFF);

		int8_t success_jmp_delta = static_cast<int8_t>(end_offset - success_jmp_offset - 1);
		textSectionData[success_jmp_offset] = static_cast<uint8_t>(success_jmp_delta);

		// Step 10: Store result to stack (pointer is always 64-bit)
		int result_offset = getStackOffsetFromTempVar(op.result);
		emitMovToFrameSized(
			SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
		);

		regAlloc.reset();
	}

	void handleGlobalVariableDecl(const IrInstruction& instruction) {
		// Extract typed payload
		const GlobalVariableDeclOp& op = std::any_cast<const GlobalVariableDeclOp&>(instruction.getTypedPayload());

		// Store global variable info for later use
		GlobalVariableInfo global_info;
		global_info.name = op.var_name;
		global_info.type = op.type;
		global_info.is_initialized = op.is_initialized;
		global_info.size_in_bytes = (op.size_in_bits / 8) * op.element_count;
		
		// Copy raw init data if present
		if (op.is_initialized) {
			global_info.init_data = op.init_data;
		}

		global_variables_.push_back(global_info);
	}

	void handleGlobalLoad(const IrInstruction& instruction) {
		// Extract typed payload - all GlobalLoad instructions use typed payloads
		const GlobalLoadOp& op = std::any_cast<const GlobalLoadOp&>(instruction.getTypedPayload());
		
		TempVar result_temp = std::get<TempVar>(op.result.value);
		StringHandle global_name_handle = op.getGlobalName();
		int size_in_bits = op.result.size_in_bits;
		Type result_type = op.result.type;
		bool is_floating_point = (result_type == Type::Float || result_type == Type::Double);
		bool is_float = (result_type == Type::Float);

		// BUGFIX: Before using RAX or XMM0, flush them if they hold dirty data
		// This prevents overwriting intermediate results in chained operations
		X64Register target_reg = is_floating_point ? X64Register::XMM0 : X64Register::RAX;
		auto& reg_info = regAlloc.registers[static_cast<size_t>(target_reg)];
		if (reg_info.isDirty && reg_info.stackVariableOffset != INT_MIN) {
			// Flush the register to memory before overwriting it
			auto tempVarIndex = getTempVarFromOffset(reg_info.stackVariableOffset);
			if (tempVarIndex.has_value()) {
				int32_t stackVariableOffset = reg_info.stackVariableOffset;
				int flush_size_in_bits = reg_info.size_in_bits;
				
				// Extend scope_stack_space if needed
				if (stackVariableOffset < variable_scopes.back().scope_stack_space) {
					variable_scopes.back().scope_stack_space = stackVariableOffset;
				}
				
				// Store the register value to stack
				emitMovToFrameSized(
					SizedRegister{target_reg, 64, false},
					SizedStackSlot{stackVariableOffset, flush_size_in_bits, false}
				);
			}
			reg_info.isDirty = false;
			// Clear the register allocation so it won't be reused without reloading
			reg_info.stackVariableOffset = INT_MIN;
		}

		// Load the global value/address using RIP-relative addressing
		uint32_t reloc_offset;
		if (op.is_array) {
			// For arrays: use LEA to get the address of the global
			reloc_offset = emitLeaRipRelative(X64Register::RAX);
		} else if (is_floating_point) {
			// For floating-point scalars: use MOVSD/MOVSS to load into XMM0
			reloc_offset = emitFloatMovRipRelative(X64Register::XMM0, is_float);
		} else {
			// For integer scalars: load the value using MOV
			reloc_offset = emitMovRipRelative(X64Register::RAX, size_in_bits);
		}

		// Add a pending relocation for this global variable reference
		pending_global_relocations_.push_back({reloc_offset, global_name_handle, IMAGE_REL_AMD64_REL32});

		// Store the loaded value/address to the stack
		int result_offset = allocateStackSlotForTempVar(result_temp.var_number);
		
		if (is_floating_point && !op.is_array) {
			// For floating-point: use emitFloatMovToFrame
			emitFloatMovToFrame(X64Register::XMM0, result_offset, is_float);
		} else {
			// For integers/pointers: use emitMovToFrameBySize
			int store_size = op.is_array ? 64 : size_in_bits;
			emitMovToFrameBySize(X64Register::RAX, result_offset, store_size);
		}
	}

	void handleGlobalStore(const IrInstruction& instruction) {
		// Format: [global_name, source_temp]
		assert(instruction.getOperandCount() == 2 && "GlobalStore must have exactly 2 operands");

		StringHandle global_name = instruction.getOperandAs<StringHandle>(0);
		TempVar source_temp = instruction.getOperandAs<TempVar>(1);

		// Determine the size and type of the global variable
		// We need to look it up in the global variables vector
		const GlobalVariableInfo* global_info = nullptr;
		for (const auto& global : global_variables_) {
			if (global.name == global_name) {
				global_info = &global;
				break;
			}
		}
		
		if (!global_info) {
			FLASH_LOG(Codegen, Error, "Global variable not found: ", global_name);
			assert(false && "Global variable not found during GlobalStore");
			return;
		}

		int size_in_bits = static_cast<int>(global_info->size_in_bytes * 8);
		Type var_type = global_info->type;
		bool is_floating_point = (var_type == Type::Float || var_type == Type::Double);
		bool is_float = (var_type == Type::Float);

		// Load the source value from stack into a register
		int source_offset = getStackOffsetFromTempVar(source_temp);
		
		if (is_floating_point) {
			// Load floating-point value into XMM0
			emitFloatMovFromFrame(X64Register::XMM0, source_offset, is_float);
			// Store to global using RIP-relative addressing
			uint32_t reloc_offset = emitFloatMovRipRelativeStore(X64Register::XMM0, is_float);
			pending_global_relocations_.push_back({reloc_offset, global_name, IMAGE_REL_AMD64_REL32});
		} else {
			// Load integer value into RAX
			emitMovFromFrameBySize(X64Register::RAX, source_offset, size_in_bits);
			// Store to global using RIP-relative addressing
			uint32_t reloc_offset = emitMovRipRelativeStore(X64Register::RAX, size_in_bits);
			pending_global_relocations_.push_back({reloc_offset, global_name, IMAGE_REL_AMD64_REL32});
		}
	}

	void handleVariableDecl(const IrInstruction& instruction) {
		// Extract typed payload
		const VariableDeclOp& op = std::any_cast<const VariableDeclOp&>(instruction.getTypedPayload());

		// Get variable name as StringHandle
		StringHandle var_name_handle = op.var_name;
		std::string var_name_str = std::string(StringTable::getStringView(var_name_handle));

		Type var_type = op.type;
		StackVariableScope& current_scope = variable_scopes.back();
		auto var_it = current_scope.variables.find(var_name_handle);
		assert(var_it != current_scope.variables.end());

		bool is_reference = op.is_reference;
		bool is_rvalue_reference = op.is_rvalue_reference;
		[[maybe_unused]] bool is_array = op.is_array;
		bool is_initialized = op.initializer.has_value();

		FLASH_LOG(Codegen, Debug, "handleVariableDecl: var='", var_name_str, "', is_reference=", is_reference, ", offset=", var_it->second.offset, ", is_initialized=", is_initialized, ", type=", static_cast<int>(var_type));

		// Store mapping from variable name to offset for reference lookups
		variable_name_to_offset_[var_name_str] = var_it->second.offset;

		// REMOVED: Flawed TempVar linking heuristic
		// Track the most recently allocated named variable for TempVar linking
		//last_allocated_variable_name_ = var_name_str;
		//last_allocated_variable_offset_ = var_it->second.offset;

		if (is_reference) {
			// For references, we need to determine the size of the VALUE being referenced,
			// not the size of the reference itself (which is always 64 bits for a pointer)
			int value_size_bits = op.size_in_bits;
			
			// If size_in_bits is 64 and the type is not a 64-bit type, we need to calculate the actual size
			// This happens for structured bindings where size_in_bits is set to 64 (pointer size)
			if (op.size_in_bits == 64) {
				// Try to get the actual size from the type
				int calculated_size = get_type_size_bits(var_type);
				if (calculated_size > 0 && calculated_size != 64) {
					value_size_bits = calculated_size;
					FLASH_LOG(Codegen, Debug, "Reference variable: Calculated value_size_bits=", value_size_bits, " from type=", static_cast<int>(var_type));
				}
			}
			
			setReferenceInfo(var_it->second.offset, var_type, value_size_bits, is_rvalue_reference);
			int32_t dst_offset = var_it->second.offset;
			X64Register pointer_reg = allocateRegisterWithSpilling();
			bool pointer_initialized = false;
			if (is_initialized) {
				// For reference initialization from typed payload
				// We need to handle TempVar or string_view in the initializer value
				const TypedValue& init = op.initializer.value();
				if (std::holds_alternative<TempVar>(init.value)) {
					auto temp_var = std::get<TempVar>(init.value);
					int src_offset = getStackOffsetFromTempVar(temp_var);
					FLASH_LOG(Codegen, Debug, "Reference init from TempVar: src_offset=", src_offset, 
					          " init.type=", static_cast<int>(init.type), 
					          " init.size_in_bits=", init.size_in_bits);
					// Check if source is itself a pointer/reference - if so, load the value
					// Otherwise, take the address
					auto src_ref_it = reference_stack_info_.find(src_offset);
					if (src_ref_it != reference_stack_info_.end()) {
						// Source is a reference - copy the pointer value
						FLASH_LOG(Codegen, Debug, "Source is in reference_stack_info, using MOV");
						emitMovFromFrame(pointer_reg, src_offset);
					} else {
						// Check if it's a 64-bit value (likely a pointer)
						// For __range_begin_ and similar, which are int64 pointers
						// Also check for struct types that returned as pointers (reference returns)
						// And function pointers which are always 64-bit addresses
						bool is_likely_pointer = (init.size_in_bits == 64 && 
						                          (init.type == Type::Long || init.type == Type::Int || 
						                           init.type == Type::UnsignedLong || init.type == Type::LongLong ||
						                           init.type == Type::Struct || init.type == Type::FunctionPointer));  // Struct references and function references return 64-bit pointers
						FLASH_LOG(Codegen, Debug, "is_likely_pointer=", is_likely_pointer);
						if (is_likely_pointer) {
							// Load the pointer value
							emitMovFromFrame(pointer_reg, src_offset);
						} else {
							// Load address of the source variable
							emitLeaFromFrame(pointer_reg, src_offset);
						}
					}
					pointer_initialized = true;
				} else if (std::holds_alternative<StringHandle>(init.value)) {
					StringHandle rvalue_var_name_handle = std::get<StringHandle>(init.value);
					
					auto src_it = current_scope.variables.find(rvalue_var_name_handle);
					if (src_it != current_scope.variables.end()) {
						FLASH_LOG(Codegen, Debug, "Initializing reference from: '", StringTable::getStringView(rvalue_var_name_handle), "', type=", static_cast<int>(init.type), ", size=", init.size_in_bits);
						// Check if source is a reference
						auto src_ref_it = reference_stack_info_.find(src_it->second.offset);
						if (src_ref_it != reference_stack_info_.end()) {
							// Source is a reference - copy the pointer value
							FLASH_LOG(Codegen, Debug, "Using MOV (source is reference)");
							emitMovFromFrame(pointer_reg, src_it->second.offset);
						} else if (init.size_in_bits == 64 && 
						           (init.type == Type::Long || init.type == Type::Int || 
						            init.type == Type::UnsignedLong || init.type == Type::LongLong)) {
							// 64-bit value, likely a pointer
							FLASH_LOG(Codegen, Debug, "Using MOV (64-bit type)");
							emitMovFromFrame(pointer_reg, src_it->second.offset);
						} else {
							// Regular value - take its address
							FLASH_LOG(Codegen, Debug, "Using LEA (regular value)");
							emitLeaFromFrame(pointer_reg, src_it->second.offset);
						}
						pointer_initialized = true;
					}
				}
				if (!pointer_initialized) {
					FLASH_LOG(Codegen, Error, "Reference initializer is not an addressable lvalue");
					throw std::runtime_error("Reference initializer must be an lvalue");
				}
			} else {
				moveImmediateToRegister(pointer_reg, 0);
			}
			auto store_ptr = generatePtrMovToFrame(pointer_reg, dst_offset);
			textSectionData.insert(textSectionData.end(), store_ptr.op_codes.begin(),
				               store_ptr.op_codes.begin() + store_ptr.size_in_bytes);
			regAlloc.release(pointer_reg);
			return;
		}

		X64Register allocated_reg_val = X64Register::RAX; // Default

		if (is_initialized) {
			auto dst_offset = var_it->second.offset;
			const TypedValue& init = op.initializer.value();

			// Check if the initializer is a literal value
			bool is_literal = std::holds_alternative<unsigned long long>(init.value) ||
			                  std::holds_alternative<double>(init.value);

			if (is_literal) {
				if (std::holds_alternative<double>(init.value)) {
					// Handle double/float literals
					double value = std::get<double>(init.value);

					FLASH_LOG(Codegen, Debug, "Initializing ", (var_type == Type::Float ? "float" : "double"), " literal: ", value);

					// Convert double to bit pattern
					uint64_t bits;
					
					// If the variable type is Float (32-bit), convert the double to float first
					if (var_type == Type::Float) {
						float float_value = static_cast<float>(value);
						uint32_t float_bits;
						std::memcpy(&float_bits, &float_value, sizeof(float_bits));
						
						FLASH_LOG(Codegen, Debug, "Storing float immediate to [RBP+", dst_offset, "], bits=0x", std::hex, float_bits, std::dec);
						
						// For 32-bit floats, store immediate directly to memory
						// This is more efficient and avoids register allocation
						emitMovDwordPtrImmToRegOffset(X64Register::RBP, dst_offset, float_bits);
					} else {
						// For 64-bit doubles, load into GPR then store to memory
						std::memcpy(&bits, &value, sizeof(bits));
						
						FLASH_LOG(Codegen, Debug, "Storing double via GPR to [RBP+", dst_offset, "], bits=0x", std::hex, bits, std::dec);
						
						// Allocate a GPR temporarily
						allocated_reg_val = allocateRegisterWithSpilling();
						
						// MOV reg, imm64 (load bit pattern) - use emit function
						emitMovImm64(allocated_reg_val, bits);
						
						// Store the 64-bit value to stack
						emitMovToFrameSized(
							SizedRegister{allocated_reg_val, 64, false},
							SizedStackSlot{dst_offset, 64, false}
						);
						
						// Release the register
						regAlloc.release(allocated_reg_val);
					}
				} else if (std::holds_alternative<unsigned long long>(init.value)) {
					uint64_t value = std::get<unsigned long long>(init.value);

					// For integer literals, allocate a register temporarily
					allocated_reg_val = allocateRegisterWithSpilling();
					
					// MOV reg, imm64 - use emit function
					emitMovImm64(allocated_reg_val, value);

					// Store the value from register to stack (size-aware)
					emitMovToFrameSized(
						SizedRegister{allocated_reg_val, 64, false},  // source: 64-bit register
						SizedStackSlot{dst_offset, op.size_in_bits, isSignedType(op.type)}  // dest
					);

					// Release the register since the value is now in the stack
					regAlloc.release(allocated_reg_val);
				}
			} else {
				// Load from memory (TempVar or variable)
				// For non-literal initialization, we don't allocate a register
				// We just copy the value from source to destination on the stack
				int src_offset = 0;
				bool src_is_pointer = false;  // Track if source is a pointer to the actual data
				if (std::holds_alternative<TempVar>(init.value)) {
					auto temp_var = std::get<TempVar>(init.value);
					src_offset = getStackOffsetFromTempVar(temp_var);
					// Check if this temp_var is a reference/pointer to the actual struct
					// For RVO struct returns, temp_var holds the address of the constructed struct
					auto ref_it = reference_stack_info_.find(src_offset);
					if (ref_it != reference_stack_info_.end()) {
						// This is a reference - need to dereference it
						src_is_pointer = true;
					}
				} else if (std::holds_alternative<StringHandle>(init.value)) {
					StringHandle rvalue_var_name_handle = std::get<StringHandle>(init.value);
					auto src_it = current_scope.variables.find(rvalue_var_name_handle);
					if (src_it == current_scope.variables.end()) {
						FLASH_LOG(Codegen, Error, "Variable '", StringTable::getStringView(rvalue_var_name_handle), "' not found in symbol table");
						FLASH_LOG(Codegen, Error, "Available variables in current scope:");
						for (const auto& [name, var_info] : current_scope.variables) {
							// Phase 5: Convert StringHandle to string_view for logging
							FLASH_LOG(Codegen, Error, "  - ", StringTable::getStringView(name), " at var_info.offset ");
						}
					}
					assert(src_it != current_scope.variables.end());
					src_offset = src_it->second.offset;
					
					// Check if source is an array - for array-to-pointer decay, we need LEA
					if (src_it->second.is_array) {
						// Source is an array being assigned to a pointer - use LEA to get address
						X64Register addr_reg = allocateRegisterWithSpilling();
						emitLeaFromFrame(addr_reg, src_offset);
						emitMovToFrameSized(
							SizedRegister{addr_reg, 64, false},
							SizedStackSlot{dst_offset, 64, false}
						);
						regAlloc.release(addr_reg);
						return;  // Early return - we've handled this case
					}
				}

				if (auto src_reg = regAlloc.tryGetStackVariableRegister(src_offset); src_reg.has_value()) {
					// Source value is already in a register (e.g., from function return or arithmetic)
					// Store it directly to the destination stack location
					if (is_floating_point_type(var_type)) {
						// For floating-point types, the value is in an XMM register
						// Use float mov instructions instead of integer mov
						bool is_float = (var_type == Type::Float);
						emitFloatMovToFrame(src_reg.value(), dst_offset, is_float);
					} else {
						// For integer types, use regular mov
						// Use the actual size from the variable type, not hardcoded 64 bits
						emitMovToFrameSized(
							SizedRegister{src_reg.value(), static_cast<uint8_t>(op.size_in_bits), false},
							SizedStackSlot{dst_offset, op.size_in_bits, isSignedType(op.type)}
						);
					}
				} else {
					// Source is on the stack, load it to a temporary register and store to destination
					if (var_type == Type::Struct) {
						// For struct types, copy entire struct using 8-byte chunks
						int struct_size_bytes = (op.size_in_bits + 7) / 8;
						
						FLASH_LOG(Codegen, Info, "==================== STRUCT COPY IN HANDLEVARIABLE ====================");
						FLASH_LOG(Codegen, Info, "size_bytes=", struct_size_bytes, ", src_offset=", src_offset, ", dst_offset=", dst_offset, ", src_is_pointer=", src_is_pointer);
						
						// Determine actual source address
						int32_t actual_src_offset;
						if (src_is_pointer) {
							// Source is a pointer to the struct - dereference it
							// Load the pointer value into a register
							X64Register ptr_reg = allocateRegisterWithSpilling();
							emitMovFromFrame(ptr_reg, src_offset);
							FLASH_LOG(Codegen, Debug, "Struct copy (via pointer): size_in_bits=", op.size_in_bits, ", size_bytes=", struct_size_bytes, ", ptr_at_offset=", src_offset, ", dst_offset=", dst_offset);
							
							// Now copy from the address in ptr_reg to dst_offset
							// We need to use memory-to-memory copy via a temporary register
							for (int offset = 0; offset < struct_size_bytes; ) {
								if (offset + 8 <= struct_size_bytes) {
									// Copy 8 bytes: load from [ptr_reg + offset], store to [rbp + dst_offset + offset]
									X64Register temp_reg = allocateRegisterWithSpilling();
									// MOV temp_reg, [ptr_reg + offset]
									emitMovFromMemory(temp_reg, ptr_reg, offset, 8);
									// MOV [rbp + dst_offset + offset], temp_reg
									emitMovToFrameSized(
										SizedRegister{temp_reg, 64, false},
										SizedStackSlot{dst_offset + offset, 64, false}
									);
									regAlloc.release(temp_reg);
									offset += 8;
								} else if (offset + 4 <= struct_size_bytes) {
									X64Register temp_reg = allocateRegisterWithSpilling();
									emitMovFromMemory(temp_reg, ptr_reg, offset, 4);
									emitMovToFrameSized(
										SizedRegister{temp_reg, 64, false},
										SizedStackSlot{dst_offset + offset, 32, false}
									);
									regAlloc.release(temp_reg);
									offset += 4;
								} else if (offset + 2 <= struct_size_bytes) {
									X64Register temp_reg = allocateRegisterWithSpilling();
									emitMovFromMemory(temp_reg, ptr_reg, offset, 2);
									emitMovToFrameSized(
										SizedRegister{temp_reg, 64, false},
										SizedStackSlot{dst_offset + offset, 16, false}
									);
									regAlloc.release(temp_reg);
									offset += 2;
								} else if (offset + 1 <= struct_size_bytes) {
									X64Register temp_reg = allocateRegisterWithSpilling();
									emitMovFromMemory(temp_reg, ptr_reg, offset, 1);
									emitMovToFrameSized(
										SizedRegister{temp_reg, 64, false},
										SizedStackSlot{dst_offset + offset, 8, false}
									);
									regAlloc.release(temp_reg);
									offset += 1;
								}
							}
							regAlloc.release(ptr_reg);
						} else {
							// Source is the struct itself on the stack
							actual_src_offset = src_offset;
							FLASH_LOG(Codegen, Debug, "Struct copy (direct): size_in_bits=", op.size_in_bits, ", size_bytes=", struct_size_bytes, ", src_offset=", src_offset, ", dst_offset=", dst_offset);
							
							// Copy struct using 8-byte chunks, then handle remaining bytes
							// Use an allocated register to avoid clobbering dirty registers
							X64Register copy_reg = allocateRegisterWithSpilling();
							int offset = 0;
							while (offset + 8 <= struct_size_bytes) {
								// Load 8 bytes from source
								emitMovFromFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{actual_src_offset + offset, 64, false}
								);
								// Store 8 bytes to destination
								emitMovToFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{dst_offset + offset, 64, false}
								);
								offset += 8;
							}
							
							// Handle remaining bytes (4, 2, 1)
							if (offset + 4 <= struct_size_bytes) {
								emitMovFromFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{actual_src_offset + offset, 32, false}
								);
								emitMovToFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{dst_offset + offset, 32, false}
								);
								offset += 4;
							}
							if (offset + 2 <= struct_size_bytes) {
								emitMovFromFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{actual_src_offset + offset, 16, false}
								);
								emitMovToFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{dst_offset + offset, 16, false}
								);
								offset += 2;
							}
							if (offset + 1 <= struct_size_bytes) {
								emitMovFromFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{actual_src_offset + offset, 8, false}
								);
								emitMovToFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{dst_offset + offset, 8, false}
								);
							}
							regAlloc.release(copy_reg);
						}
					} else if (is_floating_point_type(var_type)) {
						// For floating-point types, use XMM register and float moves
						allocated_reg_val = allocateXMMRegisterWithSpilling();
						bool is_float = (var_type == Type::Float);
						emitFloatMovFromFrame(allocated_reg_val, src_offset, is_float);
						emitFloatMovToFrame(allocated_reg_val, dst_offset, is_float);
						regAlloc.release(allocated_reg_val);
					} else {
						// For integer types, use GPR and integer moves
						allocated_reg_val = allocateRegisterWithSpilling();
						emitMovFromFrameBySize(allocated_reg_val, src_offset, op.size_in_bits);
						emitMovToFrameSized(
							SizedRegister{allocated_reg_val, 64, false},
							SizedStackSlot{dst_offset, op.size_in_bits, isSignedType(op.type)}
						);
						regAlloc.release(allocated_reg_val);
					}
				}
			} // end else (not literal)
		} // end if (is_initialized)
		
		// Add debug information for the local variable
		if (current_function_name_.isValid()) {
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
				loc.offset = var_it->second.offset;
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
		assert(instruction.hasTypedPayload() && "FunctionDecl instruction must use typed payload");
		
		// Reset register allocator state for the new function
		// This ensures registers from previous functions don't interfere
		regAlloc.reset();
		
		// Use typed payload path
		const auto& func_decl = instruction.getTypedPayload<FunctionDeclOp>();
		
		// Use mangled name if available (for member functions like lambda operator()),
		// otherwise use function_name. This is important for nested lambdas where multiple
		// operator() functions would otherwise have the same name.
		// Phase 4: Use helpers
		StringHandle mangled_handle = func_decl.getMangledName();
		StringHandle func_name_handle = func_decl.getFunctionName();
		StringHandle struct_name_handle = func_decl.getStructName();
		std::string_view mangled = StringTable::getStringView(mangled_handle);
		std::string_view func_name = mangled_handle.handle != 0 ? mangled : StringTable::getStringView(func_name_handle);
		std::string_view struct_name = StringTable::getStringView(struct_name_handle);
		
		// Construct return type
		TypeSpecifierNode return_type(func_decl.return_type, TypeQualifier::None, static_cast<unsigned char>(func_decl.return_size_in_bits));
		for (int i = 0; i < func_decl.return_pointer_depth; ++i) {
			return_type.add_pointer_level();
		}
		
		// Extract parameters
		std::vector<TypeSpecifierNode> parameter_types;
		for (const auto& param : func_decl.parameters) {
			TypeSpecifierNode param_type(param.type, TypeQualifier::None, static_cast<unsigned char>(param.size_in_bits));
			for (int i = 0; i < param.pointer_depth; ++i) {
				param_type.add_pointer_level();
			}
			parameter_types.push_back(param_type);
		}
		
		Linkage linkage = func_decl.linkage;
		bool is_variadic = func_decl.is_variadic;
		std::string_view mangled_name = StringTable::getStringView(func_decl.getMangledName());  // Phase 4: Use helper

		// Add function signature to the object file writer (still needed for debug info)
		// but use the pre-computed mangled name instead of regenerating it
		bool is_inline = func_decl.is_inline;
		if (!struct_name.empty()) {
			// Member function - include struct name
			writer.addFunctionSignature(func_name, return_type, parameter_types, struct_name, linkage, is_variadic, mangled_name, is_inline);
		} else {
			// Regular function
			writer.addFunctionSignature(func_name, return_type, parameter_types, linkage, is_variadic, mangled_name, is_inline);
		}
		
		// Finalize previous function before starting new one
		if (current_function_name_.isValid()) {
			// Calculate actual stack space needed from scope_stack_space (which includes varargs area if present)
			// scope_stack_space is negative (offset from RBP), so negate to get positive size
			size_t total_stack = static_cast<size_t>(-variable_scopes.back().scope_stack_space);
			
			// Align stack so that after `push rbp; sub rsp, total_stack` the stack is 16-byte aligned
			// System V AMD64 / MS x64: after `push rbp`, RSP is misaligned by 8 bytes.
			// Subtracting a 16-byte-aligned stack size keeps RSP % 16 == 8 at call sites,
			// so align total_stack up to the next 16-byte boundary.
			if (total_stack % 16 != 0) {
				total_stack = (total_stack + 15) & ~static_cast<size_t>(15);
			}
			
			// Patch the SUB RSP immediate at prologue offset + 3 (skip REX.W, opcode, ModR/M)
			if (current_function_prologue_offset_ > 0) {
				uint32_t patch_offset = current_function_prologue_offset_ + 3;
				const auto bytes = std::bit_cast<std::array<char, 4>>(static_cast<uint32_t>(total_stack));
				for (int i = 0; i < 4; i++) {
					textSectionData[patch_offset + i] = bytes[i];
				}
			}
			
			uint32_t function_length = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;

			// Update function length
			writer.update_function_length(mangled, function_length);
			writer.set_function_debug_range(mangled, 0, 0);	// doesn't seem needed

			// Add exception handling information (required for x64) - uses mangled name
			auto [try_blocks, unwind_map] = convertExceptionInfoToWriterFormat();
			if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
				writer.add_function_exception_info(StringTable::getStringView(current_function_mangled_name_), current_function_offset_, function_length, try_blocks, unwind_map, current_function_cfi_);
			} else {
				writer.add_function_exception_info(StringTable::getStringView(current_function_mangled_name_), current_function_offset_, function_length, try_blocks, unwind_map);
			}
		
			// Clean up the previous function's variable scope
			// This happens when we start a NEW function, ensuring the previous function's scope is removed
			if (!variable_scopes.empty()) {
				variable_scopes.pop_back();
			}
			
			// Reset for new function
			max_temp_var_index_ = 0;
			next_temp_var_offset_ = 8;  // Reset TempVar allocation offset
			current_function_try_blocks_.clear();  // Clear exception tracking for next function
			current_try_block_ = nullptr;
			current_function_local_objects_.clear();  // Clear local object tracking
			current_function_unwind_map_.clear();  // Clear unwind map
			current_exception_state_ = -1;  // Reset state counter
			if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
				current_function_cfi_.clear();  // Clear CFI tracking for next function
			}
		}

		// align the function to 16 bytes
		static constexpr char nop = static_cast<char>(0x90);
		const uint32_t nop_count = 16 - (textSectionData.size() % 16);
		if (nop_count < 16)
			textSectionData.insert(textSectionData.end(), nop_count, nop);

		// Windows x64 calling convention: Functions must provide home space for parameters
		// Calculate param_count BEFORE calling calculateFunctionStackSpace so it can allocate
		// local variables/temp vars AFTER the parameter home space
		size_t param_count = parameter_types.size();
		if (!struct_name.empty()) {
			param_count++;  // Count 'this' pointer for member functions
		}

		// Function debug info is now added in add_function_symbol() with length 0
		// Create std::string where needed for function calls that require it
		std::string func_name_str(func_name);
		
		// Pop the previous function's scope before creating the new one
		// The finalization code above has already used the previous scope, so it's safe to pop now
		if (!variable_scopes.empty()) {
			variable_scopes.pop_back();
		}
		
		StackVariableScope& var_scope = variable_scopes.emplace_back();
		const auto func_stack_space = calculateFunctionStackSpace(func_name_str, var_scope, param_count);
		
		// TempVars are now pre-counted in calculateFunctionStackSpace, include them in total
		// Also include outgoing_args_space for function calls made from this function
		// Note: named_vars_size already includes parameter home space, so don't add shadow_stack_space
		uint32_t total_stack_space = func_stack_space.named_vars_size + func_stack_space.temp_vars_size + func_stack_space.outgoing_args_space;

		
		// Even if parameters stay in registers, we need space to spill them if needed
		// Member functions have implicit 'this' pointer as first parameter
		if (param_count > 0 && total_stack_space < param_count * 8) {
			total_stack_space = static_cast<uint32_t>(param_count * 8);
		}
		
		// Ensure stack alignment to 16 bytes
		// System V AMD64 (Linux): After push rbp, RSP is at 16n. We need RSP at 16m+8 before calls.
		// So total_stack_space should be 16k+8 (rounds up to next 16k+8)
		// Windows x64: Different alignment rules, keep existing 16-byte alignment
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// Round up to 16k + 8 form for System V AMD64
			total_stack_space = ((total_stack_space + 7) & -16) + 8;
		} else {
			// Round up to 16k form for Windows x64
			total_stack_space = (total_stack_space + 15) & -16;
		}
		
		// Save function prologue information before setup
		current_function_offset_ = static_cast<uint32_t>(textSectionData.size());
		current_function_name_ = func_name_handle;
		current_function_prologue_offset_ = 0;
		
		uint32_t func_offset = static_cast<uint32_t>(textSectionData.size());
		writer.add_function_symbol(mangled_name, func_offset, total_stack_space, linkage);
		functionSymbols[std::string(func_name)] = func_offset;

		// Track function for debug information
		current_function_name_ = func_name_handle;
		current_function_mangled_name_ = mangled_handle;
		current_function_offset_ = func_offset;
		current_function_is_variadic_ = is_variadic;
		current_function_has_hidden_return_param_ = func_decl.has_hidden_return_param;  // Track for return statement handling
		current_function_returns_reference_ = func_decl.returns_reference;  // Track if function returns a reference

		// Patch pending branches from previous function before clearing
		if (!pending_branches_.empty()) {
			patchBranches();
		}

		// Clear control flow tracking for new function
		label_positions_.clear();
		pending_branches_.clear();

		// Set up debug information for this function
		// For now, use file ID 0 (first source file)
		writer.set_current_function_for_debug(std::string(func_name), 0);

		// If this is a member function, check if we need to register vtable for this class
		if (!struct_name.empty()) {
			// Look up the struct type info
			auto struct_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
			if (struct_it != gTypesByName.end()) {
				const TypeInfo* type_info = struct_it->second;
				const StructTypeInfo* struct_info = type_info->getStructInfo();
				
				if (struct_info && struct_info->has_vtable) {
					// Use the pre-generated vtable symbol from struct_info
					std::string_view vtable_symbol = struct_info->vtable_symbol;
					
					// Check if we've already registered this vtable
					bool vtable_exists = false;
					for (const auto& vt : vtables_) {
						if (vt.vtable_symbol == StringTable::getOrInternStringHandle(vtable_symbol)) {
							vtable_exists = true;
							break;
						}
					}
					
					if (!vtable_exists) {
						// Register this vtable - we'll populate function symbols as we encounter them
						VTableInfo vtable_info;
						vtable_info.vtable_symbol = StringTable::getOrInternStringHandle(vtable_symbol);
						vtable_info.class_name = StringTable::getOrInternStringHandle(struct_name);
						
						// Reserve space for vtable entries
						vtable_info.function_symbols.resize(struct_info->vtable.size());
						
						// Initialize vtable entries with appropriate function symbols:
						// - Pure virtual functions: __cxa_pure_virtual / _purecall
						// - Inherited functions (from base classes): base class's mangled function name
						// - Overridden functions: will be updated when we process the derived class's function definition
						const std::string_view pure_virtual_symbol = []()
						{
							if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
								return "__cxa_pure_virtual"sv;
							}
							return "_purecall"sv;
						}();
						for (size_t i = 0; const auto* vfunc : struct_info->vtable) {
							if (vfunc) {
								if (vfunc->is_pure_virtual) {
									vtable_info.function_symbols[i] = pure_virtual_symbol;
								} else {
									// Generate mangled name for this function
									// The function_decl contains information about which class owns this function
									std::string_view owning_struct_name;
									[[maybe_unused]] std::string_view vtable_func_name;
									
									if (vfunc->is_destructor) {
										// Destructor - get struct name from DestructorDeclarationNode
										const auto& dtor_node = vfunc->function_decl.as<DestructorDeclarationNode>();
										owning_struct_name = StringTable::getStringView(dtor_node.struct_name());
										
										// Generate mangled destructor name
										auto dtor_mangled = NameMangling::generateMangledNameFromNode(dtor_node);
										vtable_info.function_symbols[i] = dtor_mangled.view();
									} else if (!vfunc->is_constructor) {
										// Regular virtual function - get struct name from FunctionDeclarationNode
										const auto& func_node = vfunc->function_decl.as<FunctionDeclarationNode>();
										owning_struct_name = func_node.parent_struct_name();
										vtable_func_name = StringTable::getStringView(vfunc->getName());
										
										// Generate mangled function name using the function's owning struct
										const auto& vfunc_return_type = func_node.decl_node().type_node().as<TypeSpecifierNode>();
										const auto& vfunc_params = func_node.parameter_nodes();
										std::vector<std::string_view> empty_ns_path;
										auto vfunc_mangled = NameMangling::generateMangledName(
											vtable_func_name, vfunc_return_type, vfunc_params, false, 
											owning_struct_name, empty_ns_path, Linkage::CPlusPlus
										);
										vtable_info.function_symbols[i] = vfunc_mangled.view();
									}
								}
							}
							++i;
						}
						
						// Populate base class names for RTTI
						for (const auto& base : struct_info->base_classes) {
							if (base.type_index < gTypeInfo.size()) {
								const TypeInfo& base_type = gTypeInfo[base.type_index];
								if (base_type.isStruct()) {
									const StructTypeInfo* base_struct = base_type.getStructInfo();
									if (base_struct) {
										vtable_info.base_class_names.push_back(std::string(StringTable::getStringView(base_struct->getName())));
										
										// Add detailed base class info
										ObjectFileWriter::BaseClassDescriptorInfo bci;
										bci.name = std::string(StringTable::getStringView(base_struct->getName()));
										bci.num_contained_bases = static_cast<uint32_t>(base_struct->base_classes.size());
										bci.offset = static_cast<uint32_t>(base.offset);
										bci.is_virtual = base.is_virtual;
										vtable_info.base_class_info.push_back(bci);
									}
								}
							}
						}
						
						// Add RTTI information for this class
						vtable_info.rtti_info = struct_info->rtti_info;
						
						vtables_.push_back(std::move(vtable_info));
					}
					
					// Check if this function is virtual and add it to the vtable
					const StructMemberFunction* member_func = nullptr;
					// Use the unmangled function name for lookup (member_functions store unmangled names)
					// Phase 4: Use helper
					StringHandle unmangled_func_name_handle = func_decl.getFunctionName();
					for (const auto& func : struct_info->member_functions) {
						if (func.getName() == unmangled_func_name_handle) {
							member_func = &func;
							break;
						}
					}
					
					if (member_func && member_func->vtable_index >= 0) {
						// Find the vtable entry and update it with the mangled name
						for (auto& vt : vtables_) {
							if (vt.vtable_symbol == vtable_symbol) {
								if (member_func->vtable_index < static_cast<int>(vt.function_symbols.size())) {
									vt.function_symbols[member_func->vtable_index] = mangled_name;
									FLASH_LOG(Codegen, Debug, "  Added virtual function ", func_name, 
									          " at vtable index ", member_func->vtable_index);
								}
								break;
							}
						}
					}
				}
			}
		}

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
		
		// Track CFI: After push rbp, CFA = RSP+16, RBP at CFA-16
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			current_function_cfi_.push_back({
				ElfFileWriter::CFIInstruction::PUSH_RBP,
				static_cast<uint32_t>(textSectionData.size() - current_function_offset_),
				0
			});
		}
		
		textSectionData.push_back(0x48); textSectionData.push_back(0x8B); textSectionData.push_back(0xEC); // mov rbp, rsp
		
		// Track CFI: After mov rbp, rsp, CFA = RBP+16
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			current_function_cfi_.push_back({
				ElfFileWriter::CFIInstruction::MOV_RSP_RBP,
				static_cast<uint32_t>(textSectionData.size() - current_function_offset_),
				0
			});
		}

		// Always emit SUB RSP with 32-bit immediate (7 bytes total) for patching flexibility
		// We'll patch the actual value at function end after we know max_temp_var_index
		current_function_prologue_offset_ = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x48); // REX.W
		textSectionData.push_back(0x81); // SUB with 32-bit immediate
		textSectionData.push_back(0xEC); // RSP
		// Placeholder - will be patched with actual stack size
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// For RBP-relative addressing, we start with negative offset after total allocated space
		if (variable_scopes.empty()) {
			FLASH_LOG(Codegen, Error, "FATAL: variable_scopes is EMPTY!");
			std::abort();
		}
		// Set scope_stack_space to include ALL pre-allocated space (named + shadow + temp_vars)
		// TempVars are allocated within this space, not extending beyond it
		variable_scopes.back().scope_stack_space = -total_stack_space;
		
		// Store named_vars + outgoing_args for patching (temp_vars are pre-calculated, not dynamic)
		// Note: named_vars_size already includes parameter home space
		current_function_named_vars_size_ = func_stack_space.named_vars_size + func_stack_space.outgoing_args_space;

		// Handle parameters
		struct ParameterInfo {
			Type param_type;
			int param_size;
			std::string_view param_name;
			int paramNumber;
			int offset;
			X64Register src_reg;
			int pointer_depth;
			bool is_reference;
		};
		std::vector<ParameterInfo> parameters;

		// For member functions, add implicit 'this' pointer as first parameter
		int param_offset_adjustment = 0;
		
		// For functions returning struct by value, add hidden return parameter FIRST
		// This comes BEFORE all other parameters (including 'this' for member functions)
		// System V AMD64: hidden param in RDI (first register)
		// Windows x64: hidden param in RCX (first register)
		if (func_decl.has_hidden_return_param) {
			int return_slot_offset = -8;  // Hidden return parameter is always first, so offset -8
			variable_scopes.back().variables[StringTable::getOrInternStringHandle("__return_slot")].offset = return_slot_offset;
			
			X64Register return_slot_reg = getIntParamReg<TWriterClass>(0);  // Always first register
			parameters.push_back({Type::Struct, 64, "__return_slot", 0, return_slot_offset, return_slot_reg, 1, false});
			regAlloc.allocateSpecific(return_slot_reg, return_slot_offset);
			
			param_offset_adjustment = 1;  // Shift other parameters (including 'this') by 1
			
			FLASH_LOG_FORMAT(Codegen, Debug,
				"Function {} has hidden return parameter at offset {} in register {}",
				func_name, return_slot_offset, static_cast<int>(return_slot_reg));
		}
		
		// For member functions, add 'this' pointer parameter
		// This comes after hidden return parameter (if present)
		int this_offset_saved = 0;  // Will be set if this is a member function
		if (!struct_name.empty()) {
			// 'this' offset depends on whether there's a hidden return parameter
			int this_offset = (param_offset_adjustment + 1) * -8;
			this_offset_saved = this_offset;  // Save for later reference_stack_info_ registration
			variable_scopes.back().variables[StringTable::getOrInternStringHandle("this")].offset = this_offset;

			// Add 'this' parameter to debug information
			writer.add_function_parameter("this", 0x603, this_offset);  // 0x603 = T_64PVOID (pointer type)

			// Store 'this' parameter info (register depends on param_offset_adjustment)
			X64Register this_reg = getIntParamReg<TWriterClass>(param_offset_adjustment);
			parameters.push_back({Type::Struct, 64, "this", param_offset_adjustment, this_offset, this_reg, 1, false});
			regAlloc.allocateSpecific(this_reg, this_offset);

			param_offset_adjustment++;  // Shift regular parameters by 1 more
		}

		// Use separate counters for integer and float parameter registers (System V AMD64 ABI)
		// For member functions, 'this' was already added above and consumed index 0,
		// so we start counting from param_offset_adjustment (which is 1 for member functions)
		// These counters are used to compute gp_offset/fp_offset for variadic functions
		size_t int_param_reg_index = param_offset_adjustment;
		size_t float_param_reg_index = 0;
		
		// First pass: collect all parameter information
		if (!instruction.hasTypedPayload()) {
			// Operand-based path: extract parameters from operands
			size_t paramIndex = FunctionDeclLayout::FIRST_PARAM_INDEX;
			// Clear reference parameter tracking from previous function
			reference_stack_info_.clear();
			
			// Register 'this' as a pointer in reference_stack_info_ (AFTER the clear)
			// This is critical for member function calls that pass 'this' as an argument
			// Without this, the codegen would use LEA (address-of) instead of MOV (load)
			// Set holds_address_only = true because 'this' is a pointer, not a reference -
			// when we return 'this', we should return the pointer value itself, not dereference it
			if (!struct_name.empty()) {
				setReferenceInfo(this_offset_saved, Type::Struct, 64, false);
				reference_stack_info_[this_offset_saved].holds_address_only = true;
			}
			
			while (paramIndex + FunctionDeclLayout::OPERANDS_PER_PARAM <= instruction.getOperandCount()) {
				auto param_type = instruction.getOperandAs<Type>(paramIndex + FunctionDeclLayout::PARAM_TYPE);
				auto param_size = instruction.getOperandAs<int>(paramIndex + FunctionDeclLayout::PARAM_SIZE);
				auto param_pointer_depth = instruction.getOperandAs<int>(paramIndex + FunctionDeclLayout::PARAM_POINTER_DEPTH);
				// Fetch is_reference early since it affects register selection (references use integer regs, not float regs)
				bool is_reference = instruction.getOperandAs<bool>(paramIndex + FunctionDeclLayout::PARAM_IS_REFERENCE);

				// Calculate parameter number using FunctionDeclLayout helper
				size_t param_index_in_list = (paramIndex - FunctionDeclLayout::FIRST_PARAM_INDEX) / FunctionDeclLayout::OPERANDS_PER_PARAM;
				int paramNumber = static_cast<int>(param_index_in_list) + param_offset_adjustment;
			
				// Platform-specific and type-aware offset calculation
				size_t max_int_regs = getMaxIntParamRegs<TWriterClass>();
				size_t max_float_regs = getMaxFloatParamRegs<TWriterClass>();
				// Reference parameters (including rvalue references) are passed as pointers,
				// so they should use integer registers regardless of the underlying type
				bool is_float_param = (param_type == Type::Float || param_type == Type::Double) && param_pointer_depth == 0 && !is_reference;
			
				// Determine the register count threshold for this parameter type
				size_t reg_threshold = is_float_param ? max_float_regs : max_int_regs;
				size_t type_specific_index = is_float_param ? float_param_reg_index : int_param_reg_index;
			
				// Calculate offset based on whether this parameter comes from a register or stack
				// For Windows variadic functions: ALL parameters are on caller's stack starting at [RBP+16]
				constexpr bool is_coff_format = !std::is_same_v<TWriterClass, ElfFileWriter>;
				int offset;
				if (is_variadic && is_coff_format) {
					// Windows x64 variadic: ALL params at positive offsets from RBP
					// paramNumber is 0-based, so first param is at +16, second at +24, etc.
					offset = 16 + (paramNumber - param_offset_adjustment) * 8;
				} else if (type_specific_index < reg_threshold) {
					// Parameter comes from register - allocate home/shadow space
					// Use paramNumber for sequential stack allocation (not type_specific_index)
					// This ensures int and float parameters don't overlap on the stack
					offset = (paramNumber + 1) * -8;
				} else {
					// Parameter comes from stack - calculate positive offset
					// Stack parameters start at [rbp+16] (after return address at [rbp+8] and saved rbp at [rbp+0])
					offset = 16 + static_cast<int>(type_specific_index - reg_threshold) * 8;
				}

				StringHandle param_name_handle = instruction.getOperandAs<StringHandle>(paramIndex + FunctionDeclLayout::PARAM_NAME);
				std::string_view param_name = StringTable::getStringView(param_name_handle);
				variable_scopes.back().variables[param_name_handle].offset = offset;

				// Track reference parameters by their stack offset (they need pointer dereferencing like 'this')
				// Also track pointer parameters (T*) since they also contain addresses that need dereferencing
				// Also track large struct parameters (> 64 bits) which are passed by pointer
				bool is_passed_by_pointer = is_reference || param_pointer_depth > 0 ||
				                            (param_type == Type::Struct && param_size > 64);
				if (is_passed_by_pointer) {
					setReferenceInfo(offset, param_type, param_size, 
						instruction.getOperandAs<bool>(paramIndex + FunctionDeclLayout::PARAM_IS_RVALUE_REFERENCE));
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

				// Check if parameter fits in a register using separate int/float counters
				bool use_register = false;
				X64Register src_reg = X64Register::Count;
				if (is_float_param) {
					if (float_param_reg_index < max_float_regs) {
						src_reg = getFloatParamReg<TWriterClass>(float_param_reg_index++);
						use_register = true;
					} else {
						float_param_reg_index++;  // Still increment counter for stack params
					}
				} else {
					if (int_param_reg_index < max_int_regs) {
						src_reg = getIntParamReg<TWriterClass>(int_param_reg_index++);
						use_register = true;
					} else {
						int_param_reg_index++;  // Still increment counter for stack params
					}
				}
			
				if (use_register) {

					// Don't allocate XMM registers in the general register allocator
					if (!is_float_param) {
						regAlloc.allocateSpecific(src_reg, offset);
					}

					// Store parameter info for later processing
					parameters.push_back({param_type, param_size, param_name, paramNumber, offset, src_reg, param_pointer_depth, is_reference});
				}

				paramIndex += FunctionDeclLayout::OPERANDS_PER_PARAM;
			}
		} else {
			// Typed payload path: build ParameterInfo from already-extracted parameter_types
			[[maybe_unused]] const auto& typed_func_decl = instruction.getTypedPayload<FunctionDeclOp>();
			reference_stack_info_.clear();
			
			// Register 'this' as a pointer in reference_stack_info_ (AFTER the clear)
			// This is critical for member function calls that pass 'this' as an argument
			// Without this, the codegen would use LEA (address-of) instead of MOV (load)
			// Set holds_address_only = true because 'this' is a pointer, not a reference -
			// when we return 'this', we should return the pointer value itself, not dereference it
			if (!struct_name.empty()) {
				setReferenceInfo(this_offset_saved, Type::Struct, 64, false);
				reference_stack_info_[this_offset_saved].holds_address_only = true;
			}
		
			// Reset counters for this code path (they start at param_offset_adjustment for int, 0 for float)
			// The counters were already declared before the if/else block
			int_param_reg_index = param_offset_adjustment;
			float_param_reg_index = 0;
		
			for (size_t i = 0; i < func_decl.parameters.size(); ++i) {
				const auto& param = func_decl.parameters[i];
				int paramNumber = static_cast<int>(i) + param_offset_adjustment;
			
				// Platform-specific and type-aware offset calculation
				size_t max_int_regs = getMaxIntParamRegs<TWriterClass>();
				size_t max_float_regs = getMaxFloatParamRegs<TWriterClass>();
				// Reference parameters (including rvalue references) are passed as pointers,
				// so they should use integer registers regardless of the underlying type
				bool is_float_param = (param.type == Type::Float || param.type == Type::Double) && param.pointer_depth == 0 && !param.is_reference;
			
				// Determine the register count threshold for this parameter type
				size_t reg_threshold = is_float_param ? max_float_regs : max_int_regs;
				size_t type_specific_index = is_float_param ? float_param_reg_index : int_param_reg_index;
			
				// Calculate offset based on whether this parameter comes from a register or stack
				// For Windows variadic functions: ALL parameters are on caller's stack starting at [RBP+16]
				constexpr bool is_coff_format = !std::is_same_v<TWriterClass, ElfFileWriter>;
				int offset;
				if (is_variadic && is_coff_format) {
					// Windows x64 variadic: ALL params at positive offsets from RBP
					// paramNumber is 0-based, so first param is at +16, second at +24, etc.
					offset = 16 + (paramNumber - param_offset_adjustment) * 8;
				} else if (type_specific_index < reg_threshold) {
					// Parameter comes from register - allocate home/shadow space
					// Use paramNumber for sequential stack allocation (not type_specific_index)
					// This ensures int and float parameters don't overlap on the stack
					offset = (paramNumber + 1) * -8;
				} else {
					// Parameter comes from stack - calculate positive offset
					// Stack parameters start at [rbp+16] (after return address at [rbp+8] and saved rbp at [rbp+0])
					offset = 16 + static_cast<int>(type_specific_index - reg_threshold) * 8;
				}

				// Phase 4: Use helper to get param name for map key
				variable_scopes.back().variables[param.getName()].offset = offset;

				// Track reference parameters and pointer parameters
				// Both need pointer dereferencing when accessing their members
				// Also track large struct parameters (> 64 bits) which are passed by pointer
				bool is_passed_by_pointer = param.is_reference || param.pointer_depth > 0 ||
				                            (param.type == Type::Struct && param.size_in_bits > 64);
				if (is_passed_by_pointer) {
					setReferenceInfo(offset, param.type, param.size_in_bits, param.is_rvalue_reference);
				}

				// Add parameter to debug information
				uint32_t param_type_index = 0x74; // T_INT4 for int parameters
				if (param.pointer_depth > 0) {
					param_type_index = 0x603;  // T_64PVOID for pointer types
				} else {
					switch (param.type) {
						case Type::Int: param_type_index = 0x74; break;  // T_INT4
						case Type::Float: param_type_index = 0x40; break; // T_REAL32
						case Type::Double: param_type_index = 0x41; break; // T_REAL64
						case Type::Char: param_type_index = 0x10; break;  // T_CHAR
						case Type::Bool: param_type_index = 0x30; break;  // T_BOOL08
						case Type::Struct: param_type_index = 0x603; break;  // T_64PVOID for struct pointers
						default: param_type_index = 0x74; break;
					}
				}
				// Phase 4: Use helper to get param name
				std::string param_name_str(StringTable::getStringView(param.getName()));
				writer.add_function_parameter(param_name_str, param_type_index, offset);

				// Check if parameter fits in a register using separate int/float counters
				bool use_register = false;
				X64Register src_reg = X64Register::Count;
				if (is_float_param) {
					if (float_param_reg_index < max_float_regs) {
						src_reg = getFloatParamReg<TWriterClass>(float_param_reg_index++);
						use_register = true;
					} else {
						float_param_reg_index++;  // Still increment counter for stack params
					}
				} else {
					if (int_param_reg_index < max_int_regs) {
						src_reg = getIntParamReg<TWriterClass>(int_param_reg_index++);
						use_register = true;
					} else {
						int_param_reg_index++;  // Still increment counter for stack params
					}
				}
			
				if (use_register) {

					if (!is_float_param && !regAlloc.is_allocated(src_reg)) {
						regAlloc.allocateSpecific(src_reg, offset);
					}

					parameters.push_back({param.type, param.size_in_bits, StringTable::getStringView(param.getName()), paramNumber, offset, src_reg, param.pointer_depth, param.is_reference});
				}
			}
		}

		// Second pass: generate parameter storage code in the correct order

		// For Windows x64 variadic functions, parameters are already at positive offsets
		// from RBP (caller's stack), so we DON'T spill them from registers
		// For Linux (ELF), register save area is more complex - not handled here
		constexpr bool is_coff_format = !std::is_same_v<TWriterClass, ElfFileWriter>;
		bool skip_param_spilling = is_variadic && is_coff_format;

		// Standard order for other functions
		if (!skip_param_spilling) {
			for (const auto& param : parameters) {
				// MSVC-STYLE: Store parameters using RBP-relative addressing
				bool is_float_param = (param.param_type == Type::Float || param.param_type == Type::Double) && param.pointer_depth == 0;

				if (is_float_param) {
					// For floating-point parameters, use movss/movsd to store from XMM register
					bool is_float = (param.param_type == Type::Float);
					emitFloatMovToFrame(param.src_reg, param.offset, is_float);
				} else {
					// For integer parameters, use size-appropriate MOV
					// References are always passed as 64-bit pointers regardless of the type they refer to
					// Large struct parameters (> 64 bits) are passed by pointer according to System V AMD64 ABI
					bool is_passed_by_pointer = param.is_reference || param.pointer_depth > 0 ||
					                            (param.param_type == Type::Struct && param.param_size > 64);
					int store_size = is_passed_by_pointer ? 64 : param.param_size;
					emitMovToFrameSized(
						SizedRegister{param.src_reg, 64, false},  // source: 64-bit register
						SizedStackSlot{param.offset, store_size, isSignedType(param.param_type)}  // dest
					);
				
					// Release the parameter register from the register allocator
					// Parameters are now on the stack, so the register allocator should not
					// think they're still in registers
					regAlloc.release(param.src_reg);
				}
			}
		}
		
		// For Linux (System V AMD64) variadic functions: Create register save area and va_list structure
		// On System V AMD64, variadic arguments are passed in registers, so we need to
		// save all potential variadic argument registers to a register save area and
		// create a va_list structure to track offsets
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			if (is_variadic) {
				// System V AMD64 ABI register save area layout:
				// Integer registers: RDI, RSI, RDX, RCX, R8, R9  (6 registers * 8 bytes = 48 bytes)
				// Float registers: XMM0-XMM7  (8 registers * 16 bytes = 128 bytes, need full 16 for alignment)
				// Total register save area: 176 bytes
				//
				// Additionally, we need a va_list structure (compatible with System V AMD64):
				// struct __va_list_tag {
				//     unsigned int gp_offset;       // 4 bytes - offset into integer registers (0-48)
				//     unsigned int fp_offset;       // 4 bytes - offset into float registers (48-176)  
				//     void *overflow_arg_area;      // 8 bytes - stack overflow area
				//     void *reg_save_area;          // 8 bytes - pointer to register save area
				// };  // Total: 24 bytes
				
				// Calculate layout offsets
				constexpr int INT_REG_AREA_SIZE = 6 * 8;      // 48 bytes for integer registers
				constexpr int FLOAT_REG_AREA_SIZE = 8 * 16;   // 128 bytes for XMM registers  
				constexpr int REG_SAVE_AREA_SIZE = INT_REG_AREA_SIZE + FLOAT_REG_AREA_SIZE;  // 176 bytes
				constexpr int VA_LIST_STRUCT_SIZE = 24;        // Size of va_list structure
				
				// Allocate space: register save area first, then va_list structure
				int32_t reg_save_area_base = variable_scopes.back().scope_stack_space - REG_SAVE_AREA_SIZE;
				int32_t va_list_struct_base = reg_save_area_base - VA_LIST_STRUCT_SIZE;
				current_function_varargs_reg_save_offset_ = reg_save_area_base;
				
				// Update the scope stack space to include both areas
				variable_scopes.back().scope_stack_space = va_list_struct_base;
				
				// Save all integer registers: RDI, RSI, RDX, RCX, R8, R9 at offsets 0-47
				// (RDI is the first fixed param but we save it for completeness)
				constexpr X64Register int_regs[] = {
					X64Register::RDI,  // Offset 0
					X64Register::RSI,  // Offset 8
					X64Register::RDX,  // Offset 16
					X64Register::RCX,  // Offset 24
					X64Register::R8,   // Offset 32
					X64Register::R9    // Offset 40
				};
				constexpr size_t INT_REG_COUNT = sizeof(int_regs) / sizeof(int_regs[0]);
				static_assert(INT_REG_COUNT == 6, "System V AMD64 ABI has exactly 6 integer argument registers");
				
				// Number of XMM registers saved in register save area (System V AMD64 ABI)
				constexpr size_t FLOAT_REG_COUNT = 8;
				
				for (size_t i = 0; i < INT_REG_COUNT; ++i) {
					int32_t offset = reg_save_area_base + static_cast<int32_t>(i * 8);
					emitMovToFrameSized(
						SizedRegister{int_regs[i], 64, false},  // source: 64-bit register
						SizedStackSlot{offset, 64, false}  // dest: 64-bit stack slot
					);
				}
				
				// Save all float registers: XMM0-XMM7 at offsets 48-175
				// Use full 16 bytes per register for proper alignment
				for (size_t i = 0; i < FLOAT_REG_COUNT; ++i) {
					X64Register xmm_reg = static_cast<X64Register>(static_cast<int>(X64Register::XMM0) + i);
					int32_t offset = reg_save_area_base + INT_REG_AREA_SIZE + static_cast<int32_t>(i * 16);
					emitMovdquToFrame(xmm_reg, offset);
				}
				
				// Register special variables for va_list structure and register save area
				variable_scopes.back().variables[StringTable::getOrInternStringHandle("__varargs_va_list_struct__")].offset = va_list_struct_base;
				variable_scopes.back().variables[StringTable::getOrInternStringHandle("__varargs_reg_save_area__")].offset = reg_save_area_base;
				
				// Initialize the va_list structure fields directly in the function prologue
				// This avoids IR complexity with pointer arithmetic and dereferencing
				// Structure layout (24 bytes total):
				//   unsigned int gp_offset;       // offset 0 (4 bytes): Skip fixed integer parameters in registers
				//   unsigned int fp_offset;       // offset 4 (4 bytes): Skip fixed float parameters in registers  
				//   void *overflow_arg_area;      // offset 8 (8 bytes): NULL for now (not used for register args)
				//   void *reg_save_area;          // offset 16 (8 bytes): Pointer to register save area base
				
				// Calculate gp_offset: skip registers used by fixed integer parameters
				// Each integer register slot is 8 bytes, capped at 6 (INT_REG_COUNT)
				size_t fixed_int_params = std::min(int_param_reg_index, static_cast<size_t>(INT_REG_COUNT));
				int initial_gp_offset = static_cast<int>(fixed_int_params * 8);
				
				// Calculate fp_offset: skip registers used by fixed float parameters
				// Float registers start at offset 48 (after integer registers), each is 16 bytes
				size_t fixed_float_params = std::min(float_param_reg_index, FLOAT_REG_COUNT);
				int initial_fp_offset = INT_REG_AREA_SIZE + static_cast<int>(fixed_float_params * 16);
				
				// Load va_list structure base address into RAX
				emitLeaFromFrame(X64Register::RAX, va_list_struct_base);
				
				// Store gp_offset at [RAX + 0]
				emitMovDwordPtrImmToRegOffset(X64Register::RAX, 0, initial_gp_offset);
				
				// Store fp_offset at [RAX + 4]
				emitMovDwordPtrImmToRegOffset(X64Register::RAX, 4, initial_fp_offset);
				
				// Store overflow_arg_area at [RAX + 8]
				// For System V AMD64 ABI, overflow arguments are passed on the stack
				// by the caller. They start at [RBP+16] (after saved RBP and return address).
				// LEA RCX, [RBP + 16] then store to [RAX + 8]
				emitLeaFromFrame(X64Register::RCX, 16);  // overflow args are at RBP+16
				emitMovQwordPtrRegToRegOffset(X64Register::RAX, 8, X64Register::RCX);
				
				// Store reg_save_area pointer at [RAX + 16]
				// Load register save area address into RCX
				emitLeaFromFrame(X64Register::RCX, reg_save_area_base);
				emitMovQwordPtrRegToRegOffset(X64Register::RAX, 16, X64Register::RCX);
			}
		}
	}

	// Helper function to get the actual size of a variable for proper zero/sign-extension
	int getActualVariableSize(StringHandle var_name, int default_size) const {
		if (variable_scopes.empty()) {
			return default_size;
		}
		
		const auto& current_scope = variable_scopes.back();
		auto var_it = current_scope.variables.find(var_name);
		if (var_it != current_scope.variables.end()) {
			// Return the stored size if it's non-zero, otherwise use default
			if (var_it->second.size_in_bits > 0) {
				return var_it->second.size_in_bits;
			}
		}
		
		return default_size;
	}

	void handleReturn(const IrInstruction& instruction) {
		FLASH_LOG(Codegen, Debug, "handleReturn called");
		
		if (variable_scopes.empty()) {
			FLASH_LOG(Codegen, Error, "FATAL [handleReturn]: variable_scopes is EMPTY!");
			std::abort();
		}
		
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			if (inside_catch_handler_ && g_enable_exceptions) {
				emitCall("__cxa_end_catch");
				inside_catch_handler_ = false;
			}
		}

		// Add line mapping for the return statement itself (only for functions without function calls)
		// For functions with function calls (like main), the closing brace is already mapped in handleFunctionCall
		if (instruction.getLineNumber() > 0 && current_function_name_ != StringTable::getOrInternStringHandle("main")) {	// TODO: Is main special case still needed here?
			addLineMapping(instruction.getLineNumber());
		}

		// Check for typed payload first
		if (instruction.hasTypedPayload()) {
			const auto& ret_op = instruction.getTypedPayload<ReturnOp>();
			
			// Void return - no value to return
			if (!ret_op.return_value.has_value()) {
				// Fall through to epilogue generation below
			}
			else {
				// Return with value
				const auto& ret_val = ret_op.return_value.value();
				
				// Debug: log what type of return value we have
				if (std::holds_alternative<unsigned long long>(ret_val)) {
					FLASH_LOG(Codegen, Debug, "Return value type: unsigned long long");
				} else if (std::holds_alternative<TempVar>(ret_val)) {
					FLASH_LOG(Codegen, Debug, "Return value type: TempVar");
				} else if (std::holds_alternative<StringHandle>(ret_val)) {
					FLASH_LOG(Codegen, Debug, "Return value type: StringHandle");
				} else if (std::holds_alternative<double>(ret_val)) {
					FLASH_LOG(Codegen, Debug, "Return value type: double");
				} else {
					FLASH_LOG(Codegen, Debug, "Return value type: UNKNOWN");
				}
				
				if (std::holds_alternative<unsigned long long>(ret_val)) {
					unsigned long long returnValue = std::get<unsigned long long>(ret_val);
					
					// Check if this is actually a negative number stored as unsigned long long
					if (returnValue > std::numeric_limits<uint32_t>::max()) {
						uint32_t lower32 = static_cast<uint32_t>(returnValue);
						if ((returnValue >> 32) == 0xFFFFFFFF) {
							returnValue = lower32;
						} else {
							throw std::runtime_error("Return value exceeds 32-bit limit");
						}
					}

					// mov eax, immediate instruction has a fixed size of 5 bytes
					std::array<uint8_t, 5> movEaxImmedInst = { 0xB8, 0, 0, 0, 0 };
					for (size_t i = 0; i < 4; ++i) {
						movEaxImmedInst[i + 1] = (returnValue >> (8 * i)) & 0xFF;
					}
					textSectionData.insert(textSectionData.end(), movEaxImmedInst.begin(), movEaxImmedInst.end());
				}
				else if (std::holds_alternative<TempVar>(ret_val)) {
					// Handle temporary variable (stored on stack)
					auto return_var = std::get<TempVar>(ret_val);
					auto temp_var_name = StringTable::getOrInternStringHandle(return_var.name());
					const StackVariableScope& current_scope = variable_scopes.back();
					auto it = current_scope.variables.find(temp_var_name);
					
					FLASH_LOG_FORMAT(Codegen, Debug,
						"handleReturn TempVar path: return_var={}, found_in_scope={}",
						return_var.name(), (it != current_scope.variables.end()));
					
					// Check if return type is float/double
					bool is_float_return = ret_op.return_type.has_value() && 
					                        is_floating_point_type(ret_op.return_type.value());

					bool handled_reference_return = false;
					{
						auto lv_info_opt = getTempVarLValueInfo(return_var);
						auto return_meta = getTempVarMetadata(return_var);
						FLASH_LOG(Codegen, Debug, "handleReturn: lvalue metadata present=", lv_info_opt.has_value(), ", returns_reference=", current_function_returns_reference_, ", is_address=", return_meta.is_address);
						if (lv_info_opt.has_value() && (current_function_returns_reference_ || return_meta.is_address)) {
							const LValueInfo& lv_info = lv_info_opt.value();
							auto loadBaseAddress = [&](const std::variant<StringHandle, TempVar>& base, bool base_is_pointer) -> bool {
								int base_offset = 0;
								if (std::holds_alternative<StringHandle>(base)) {
									auto base_name = std::get<StringHandle>(base);
									const StackVariableScope* scope_with_var = nullptr;
									for (auto scope_it = variable_scopes.rbegin(); scope_it != variable_scopes.rend(); ++scope_it) {
										auto var_it = scope_it->variables.find(base_name);
										if (var_it != scope_it->variables.end()) {
											scope_with_var = &(*scope_it);
											base_offset = var_it->second.offset;
											break;
										}
									}
									if (scope_with_var == nullptr) {
										return false;
									}
								} else {
									base_offset = getStackOffsetFromTempVar(std::get<TempVar>(base));
								}

								if (base_is_pointer) {
									emitMovFromFrame(X64Register::RAX, base_offset);
								} else {
									emitLeaFromFrame(X64Register::RAX, base_offset);
								}
								return true;
							};

							switch (lv_info.kind) {
								case LValueInfo::Kind::Indirect: {
									if (loadBaseAddress(lv_info.base, true)) {
										if (lv_info.offset != 0) {
											emitAddImmToReg(textSectionData, X64Register::RAX, lv_info.offset);
										}
										handled_reference_return = true;
									}
									break;
								}
								case LValueInfo::Kind::Direct: {
									if (loadBaseAddress(lv_info.base, false)) {
										if (lv_info.offset != 0) {
											emitAddImmToReg(textSectionData, X64Register::RAX, lv_info.offset);
										}
										handled_reference_return = true;
									}
									break;
								}
								case LValueInfo::Kind::Member: {
									bool base_is_pointer = lv_info.is_pointer_to_member;
									if (loadBaseAddress(lv_info.base, base_is_pointer)) {
										if (!base_is_pointer) {
											emitAddImmToReg(textSectionData, X64Register::RAX, lv_info.offset);
										} else if (lv_info.offset != 0) {
											emitAddImmToReg(textSectionData, X64Register::RAX, lv_info.offset);
										}
										handled_reference_return = true;
									}
									break;
								}
								default:
									break;
							}

							if (handled_reference_return) {
								regAlloc.flushSingleDirtyRegister(X64Register::RAX);
							}
						}
					}
					
					if (handled_reference_return) {
						// Address already loaded into RAX for reference return
					}
					else if (it != current_scope.variables.end()) {
						int var_offset = it->second.offset;
						
						// Ensure stack space is allocated for large structs being returned
						// The TempVar might have been pre-allocated with default size, so re-check with actual size
						if (ret_op.return_size > 64) {
							// Call getStackOffsetFromTempVar with the correct size to extend scope if needed
							getStackOffsetFromTempVar(return_var, ret_op.return_size);
						}
						
						// Check if this is a reference variable - if so, dereference it
						// EXCEPT when the function itself returns a reference - in that case, return the address as-is
						auto ref_it = reference_stack_info_.find(var_offset);
						if (ref_it != reference_stack_info_.end() && !ref_it->second.holds_address_only && !current_function_returns_reference_) {
							// This is a reference and function returns by value - load pointer and dereference
							FLASH_LOG(Codegen, Debug, "handleReturn: Dereferencing reference at offset ", var_offset);
							X64Register ptr_reg = X64Register::RAX;
							emitMovFromFrame(ptr_reg, var_offset);  // Load the pointer
							// Dereference to get the value
							int value_size_bytes = ref_it->second.value_size_bits / 8;
							emitMovFromMemory(ptr_reg, ptr_reg, 0, value_size_bytes);
							// Value is now in RAX, ready to return
						} else if (ref_it != reference_stack_info_.end() && current_function_returns_reference_) {
							// This is a reference and function returns a reference - return the address itself
							FLASH_LOG(Codegen, Debug, "handleReturn: Returning reference address from offset ", var_offset);
							X64Register ptr_reg = X64Register::RAX;
							emitMovFromFrame(ptr_reg, var_offset);  // Load the pointer (address)
							// Address is now in RAX, ready to return
						} else {
							// Not a reference - normal variable return
							// Get the actual size of the variable being returned
							int var_size = getActualVariableSize(temp_var_name, ret_op.return_size);
							
							// Check if function uses hidden return parameter (RVO/NRVO)
							// Only skip copy if this specific return value is RVO-eligible (was constructed via RVO)
							bool is_rvo_eligible = isTempVarRVOEligible(return_var);
							FLASH_LOG_FORMAT(Codegen, Debug,
								"Return statement check: hidden_param={}, rvo_eligible={}, return_var={}",
								current_function_has_hidden_return_param_, is_rvo_eligible, return_var.name());
							
							if (current_function_has_hidden_return_param_ && is_rvo_eligible) {
								FLASH_LOG_FORMAT(Codegen, Debug,
									"Return statement in function with hidden return parameter - RVO-eligible struct already in return slot at offset {}",
									var_offset);
								// Struct already constructed in return slot via RVO
								// For System V ABI: must return the hidden parameter (return slot address) in RAX
								auto return_slot_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle("__return_slot"));
								if (return_slot_it != variable_scopes.back().variables.end()) {
									int return_slot_param_offset = return_slot_it->second.offset;
									emitMovFromFrame(X64Register::RAX, return_slot_param_offset);
								}
							} else if (current_function_has_hidden_return_param_) {
								// Function uses hidden return param but this value is NOT RVO-eligible
								// Need to copy the struct to the return slot
								FLASH_LOG_FORMAT(Codegen, Debug,
									"Return statement: copying non-RVO struct from offset {} to return slot (var_size={} bits)",
									var_offset, var_size);
								
								// Load return slot address from __return_slot parameter
								auto return_slot_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle("__return_slot"));
								if (return_slot_it != variable_scopes.back().variables.end()) {
									int return_slot_param_offset = return_slot_it->second.offset;
									// Load the address from __return_slot into a register
									X64Register dest_reg = X64Register::RDI;
									emitMovFromFrame(dest_reg, return_slot_param_offset);
									
									FLASH_LOG_FORMAT(Codegen, Debug,
										"Copying struct: size={} bytes, from offset {}, return_slot_param at offset {}",
										var_size / 8, var_offset, return_slot_param_offset);
									
									// Copy struct from var_offset to address in dest_reg
									// Copy in 8-byte chunks, then handle remaining bytes (4, 2, 1)
									int struct_size_bytes = var_size / 8;
									int bytes_copied = 0;
									
									// Copy 8-byte chunks
									while (bytes_copied + 8 <= struct_size_bytes) {
										emitMovFromFrame(X64Register::RAX, var_offset + bytes_copied);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 8);
										bytes_copied += 8;
									}
									
									// Handle remaining bytes (4, 2, 1)
									if (bytes_copied + 4 <= struct_size_bytes) {
										emitMovFromFrameBySize(X64Register::RAX, var_offset + bytes_copied, 32);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 4);
										bytes_copied += 4;
									}
									if (bytes_copied + 2 <= struct_size_bytes) {
										emitMovFromFrameBySize(X64Register::RAX, var_offset + bytes_copied, 16);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 2);
										bytes_copied += 2;
									}
									if (bytes_copied + 1 <= struct_size_bytes) {
										emitMovFromFrameBySize(X64Register::RAX, var_offset + bytes_copied, 8);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 1);
										bytes_copied += 1;
									}
									
									FLASH_LOG_FORMAT(Codegen, Debug,
										"Struct copy complete: copied {} bytes", bytes_copied);
								}
							} else if (is_float_return) {
								// Load floating-point value into XMM0
								bool is_float = (ret_op.return_size == 32);
								emitFloatMovFromFrame(X64Register::XMM0, var_offset, is_float);
							} else {
								// Integer/pointer return
								if (auto reg_var = regAlloc.tryGetStackVariableRegister(var_offset); reg_var.has_value()) {
									if (reg_var.value() != X64Register::RAX) {
										auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, reg_var.value(), ret_op.return_size / 8);
										for (size_t i = 0; i < movResultToRax.size_in_bytes; ++i) {
										}
										logAsmEmit("handleReturn mov to RAX", movResultToRax.op_codes.data(), movResultToRax.size_in_bytes);
										textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
									} else {
									}
								}
								else {
									// Load from stack using RBP-relative addressing
									// Use actual variable size for proper zero/sign extension
									emitMovFromFrameBySize(X64Register::RAX, var_offset, var_size);
									regAlloc.flushSingleDirtyRegister(X64Register::RAX);
								}
							}
						}
					} else {
						// Value not in variables - use fallback offset calculation
						int var_offset = getStackOffsetFromTempVar(return_var);
						
						// Get the actual size of the variable being returned
						int var_size = getActualVariableSize(temp_var_name, ret_op.return_size);
						
						// Check if function uses hidden return parameter (RVO/NRVO)
						// For System V ABI: must return the hidden parameter (return slot address) in RAX
						if (current_function_has_hidden_return_param_) {
							FLASH_LOG(Codegen, Debug,
								"Return statement (fallback): function has hidden return parameter, loading return slot address into RAX");
							auto return_slot_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle("__return_slot"));
							if (return_slot_it != variable_scopes.back().variables.end()) {
								int return_slot_param_offset = return_slot_it->second.offset;
								emitMovFromFrame(X64Register::RAX, return_slot_param_offset);
							}
						} else if (is_float_return) {
							// Load floating-point value into XMM0
							bool is_float = (ret_op.return_size == 32);
							emitFloatMovFromFrame(X64Register::XMM0, var_offset, is_float);
						} else {
							// Load integer/pointer value into RAX
							// Use actual variable size for proper zero/sign extension
							emitMovFromFrameBySize(X64Register::RAX, var_offset, var_size);
							regAlloc.flushSingleDirtyRegister(X64Register::RAX);
						}
					}
				}
				else if (std::holds_alternative<StringHandle>(ret_val)) {
					// Handle named variable
					StringHandle var_name_handle = std::get<StringHandle>(ret_val);
					const StackVariableScope& current_scope = variable_scopes.back();
					auto it = current_scope.variables.find(var_name_handle);
					if (it != current_scope.variables.end()) {
						int var_offset = it->second.offset;
						
						// Check if this is a reference variable - if so, dereference it
						// EXCEPT when the function itself returns a reference - in that case, return the address as-is
						// ALSO skip dereferencing if this is 'this' or holds_address_only is set (pointer, not reference)
						auto ref_it = reference_stack_info_.find(var_offset);
						if (ref_it != reference_stack_info_.end() && !ref_it->second.holds_address_only && !current_function_returns_reference_) {
							// This is a reference and function does not return a reference - load pointer and dereference to get value
							FLASH_LOG(Codegen, Debug, "handleReturn: Dereferencing named reference '", StringTable::getStringView(var_name_handle), "' at offset ", var_offset);
							X64Register ptr_reg = X64Register::RAX;
							emitMovFromFrame(ptr_reg, var_offset);  // Load the pointer
							// Dereference to get the value
							int value_size_bytes = ref_it->second.value_size_bits / 8;
							emitMovFromMemory(ptr_reg, ptr_reg, 0, value_size_bytes);
							// Value is now in RAX, ready to return
						} else if (ref_it != reference_stack_info_.end() && !ref_it->second.holds_address_only && current_function_returns_reference_) {
							// This is a reference and function returns a reference - return the address itself
							FLASH_LOG(Codegen, Debug, "handleReturn: Returning named reference address '", StringTable::getStringView(var_name_handle), "' at offset ", var_offset);
							X64Register ptr_reg = X64Register::RAX;
							emitMovFromFrame(ptr_reg, var_offset);  // Load the pointer (address)
							// Address is now in RAX, ready to return
						} else {
							// Not a reference - normal variable return
							// Get the actual size of the variable being returned
							int var_size = getActualVariableSize(var_name_handle, ret_op.return_size);
							
							// Check if return type is float/double
							bool is_float_return = ret_op.return_type.has_value() && 
							                        is_floating_point_type(ret_op.return_type.value());
							
							// Check if function uses hidden return parameter (for struct returns)
							if (current_function_has_hidden_return_param_) {
								// Function uses hidden return param - need to copy struct to return slot
								FLASH_LOG_FORMAT(Codegen, Debug,
									"Return statement (StringHandle): copying struct '{}' from offset {} to return slot (size={} bits)",
									StringTable::getStringView(var_name_handle), var_offset, var_size);
								
								// Load return slot address from __return_slot parameter
								auto return_slot_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle("__return_slot"));
								if (return_slot_it != variable_scopes.back().variables.end()) {
									int return_slot_param_offset = return_slot_it->second.offset;
									// Load the address from __return_slot into a register
									X64Register dest_reg = X64Register::RDI;
									emitMovFromFrame(dest_reg, return_slot_param_offset);
									
									FLASH_LOG_FORMAT(Codegen, Debug,
										"Copying struct: size={} bytes, from offset {}, return_slot_param at offset {}",
										var_size / 8, var_offset, return_slot_param_offset);
									
									// Copy struct from var_offset to address in dest_reg
									// Copy in 8-byte chunks, then handle remaining bytes (4, 2, 1)
									int struct_size_bytes = var_size / 8;
									int bytes_copied = 0;
									
									// Copy 8-byte chunks
									while (bytes_copied + 8 <= struct_size_bytes) {
										emitMovFromFrame(X64Register::RAX, var_offset + bytes_copied);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 8);
										bytes_copied += 8;
									}
									
									// Handle remaining bytes (4, 2, 1)
									if (bytes_copied + 4 <= struct_size_bytes) {
										emitMovFromFrameBySize(X64Register::RAX, var_offset + bytes_copied, 32);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 4);
										bytes_copied += 4;
									}
									if (bytes_copied + 2 <= struct_size_bytes) {
										emitMovFromFrameBySize(X64Register::RAX, var_offset + bytes_copied, 16);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 2);
										bytes_copied += 2;
									}
									if (bytes_copied + 1 <= struct_size_bytes) {
										emitMovFromFrameBySize(X64Register::RAX, var_offset + bytes_copied, 8);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 1);
										bytes_copied += 1;
									}
									
									FLASH_LOG_FORMAT(Codegen, Debug,
										"Struct copy complete: copied {} bytes", bytes_copied);
								}
							} else if (is_float_return) {
								// Load floating-point value into XMM0
								bool is_float = (ret_op.return_size == 32);
								emitFloatMovFromFrame(X64Register::XMM0, var_offset, is_float);
							} else {
								// Load integer/pointer value into RAX
								// Use actual variable size for proper zero/sign extension
								emitMovFromFrameBySize(X64Register::RAX, var_offset, var_size);
								regAlloc.flushSingleDirtyRegister(X64Register::RAX);
							}
						}
					}
				}
				else if (std::holds_alternative<double>(ret_val)) {
					// Floating point return in XMM0
					double returnValue = std::get<double>(ret_val);
					
					// Determine if this is float or double based on return_size
					bool is_float = (ret_op.return_size == 32);
					
					// We need a temporary location on the stack to load from
					// Use the shadow space / spill area at the end of the frame
					// This is safe because we're about to return
					int literal_offset = -8; // Use first slot in shadow space
					
					// Store the literal bits to the stack via RAX
					uint64_t bits;
					if (is_float) {
						float float_val = static_cast<float>(returnValue);
						std::memcpy(&bits, &float_val, sizeof(float));
					} else {
						std::memcpy(&bits, &returnValue, sizeof(double));
					}
					
					// mov rax, immediate64
					textSectionData.push_back(0x48);
					textSectionData.push_back(0xB8);
					for (int i = 0; i < 8; ++i) {
						textSectionData.push_back(static_cast<uint8_t>(bits & 0xFF));
						bits >>= 8;
					}
					
					// mov [rbp + offset], rax (store to stack - 64-bit)
					emitMovToFrameSized(
						SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
						SizedStackSlot{literal_offset, 64, false}  // dest: 64-bit for float bits
					);
					
					// Load from stack to XMM0
					// movss/movsd xmm0, [rbp + offset]
					emitFloatMovFromFrame(X64Register::XMM0, literal_offset, is_float);
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

		// Track CFI: After pop rbp, CFA = RSP+8 (back to call site state)
		// This CFI instruction describes the state AFTER pop rbp executes
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			current_function_cfi_.push_back({
				ElfFileWriter::CFIInstruction::POP_RBP,
				static_cast<uint32_t>(textSectionData.size() - current_function_offset_),
				0
			});
		}

		// ret (return to caller)
		textSectionData.push_back(0xC3);

		// NOTE: We do NOT pop variable_scopes here because there may be multiple
		// return statements in a function (e.g., early returns in if statements).
		// The scope will be popped when we finish processing the entire function.
	}

	void handleStackAlloc([[maybe_unused]] const IrInstruction& instruction) {
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
		current_scope.variables[instruction.getOperandAs<std::string_view>(2)].offset = stack_offset;*/
	}

	void handleAdd(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "addition");

		// Perform the addition operation: ADD dst, src (opcode 0x01)
		// Use the operand size to determine whether to use 32-bit or 64-bit operation
		emitBinaryOpInstruction(0x01, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.operand_size_in_bits);

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

		// Perform the subtraction operation: SUB dst, src (opcode 0x29)
		// Use the operand size to determine whether to use 32-bit or 64-bit operation
		emitBinaryOpInstruction(0x29, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.operand_size_in_bits);

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);

		// Release the RHS register (we're done with it)
		regAlloc.release(ctx.rhs_physical_reg);
		// Note: Do NOT release result_physical_reg here - it may be holding a temp variable
	}

	void handleMultiply(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "multiplication");

		// Perform the multiplication operation: IMUL dst, src (opcode 0x0F 0xAF)
		// Determine if we need a REX prefix
		bool needs_rex = (ctx.operand_size_in_bits == 64);
		uint8_t rex_prefix = (ctx.operand_size_in_bits == 64) ? 0x48 : 0x40;
		
		// Check if registers need REX extensions
		if (static_cast<uint8_t>(ctx.result_physical_reg) >= 8) {
			rex_prefix |= 0x04; // Set REX.R for result_physical_reg (reg field)
			needs_rex = true;
		}
		if (static_cast<uint8_t>(ctx.rhs_physical_reg) >= 8) {
			rex_prefix |= 0x01; // Set REX.B for rhs_physical_reg (rm field)
			needs_rex = true;
		}
		
		// Build ModR/M byte
		uint8_t modrm_byte = 0xC0 | ((static_cast<uint8_t>(ctx.result_physical_reg) & 0x07) << 3) | (static_cast<uint8_t>(ctx.rhs_physical_reg) & 0x07);
		
		// Emit the instruction (IMUL is a two-byte opcode: 0x0F 0xAF)
		if (needs_rex) {
			textSectionData.push_back(rex_prefix);
		}
		textSectionData.push_back(0x0F);
		textSectionData.push_back(0xAF);
		textSectionData.push_back(modrm_byte);

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
		auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, ctx.result_physical_reg, ctx.result_value.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);

		// Sign extend RAX into RDX:RAX (CQO for 64-bit)
		if (ctx.result_value.size_in_bits == 64) {
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
		if (ctx.result_value.size_in_bits == 64) {
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
		auto movRhsToCx = regAlloc.get_reg_reg_move_op_code(X64Register::RCX, ctx.rhs_physical_reg, ctx.result_value.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movRhsToCx.op_codes.begin(), movRhsToCx.op_codes.begin() + movRhsToCx.size_in_bytes);

		// Perform the shift left operation: shl r/m, cl
		emitOpcodeExtInstruction(0xD3, X64OpcodeExtension::SHL, ctx.result_physical_reg, ctx.result_value.size_in_bits);

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleShiftRight(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift right");

		// Shift operations require the shift count to be in CL (lower 8 bits of RCX)
		// Move rhs_physical_reg to RCX
		auto movRhsToCx = regAlloc.get_reg_reg_move_op_code(X64Register::RCX, ctx.rhs_physical_reg, ctx.result_value.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movRhsToCx.op_codes.begin(), movRhsToCx.op_codes.begin() + movRhsToCx.size_in_bytes);

		// Perform the shift right operation: sar r/m, cl (arithmetic right shift)
		// Note: Using SAR (arithmetic) instead of SHR (logical) to preserve sign for signed integers
		emitOpcodeExtInstruction(0xD3, X64OpcodeExtension::SAR, ctx.result_physical_reg, ctx.result_value.size_in_bits);

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
		auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, ctx.result_physical_reg, ctx.result_value.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);

		// xor edx, edx - clear upper 32 bits of dividend for unsigned division
		std::array<uint8_t, 2> xorEdxInst = { 0x31, 0xD2 };
		textSectionData.insert(textSectionData.end(), xorEdxInst.begin(), xorEdxInst.end());

		// div rhs_physical_reg (unsigned division)
		emitOpcodeExtInstruction(0xF7, X64OpcodeExtension::DIV, ctx.rhs_physical_reg, ctx.result_value.size_in_bits);

		// Store the result from RAX (quotient) to the appropriate destination
		storeArithmeticResult(ctx, X64Register::RAX);

		regAlloc.release(X64Register::RDX);
	}

	void handleUnsignedShiftRight(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned shift right");

		// Shift operations require the shift count to be in CL (lower 8 bits of RCX)
		// Move rhs_physical_reg to RCX
		auto movRhsToCx = regAlloc.get_reg_reg_move_op_code(X64Register::RCX, ctx.rhs_physical_reg, ctx.result_value.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movRhsToCx.op_codes.begin(), movRhsToCx.op_codes.begin() + movRhsToCx.size_in_bytes);

		// Perform the unsigned shift right operation: shr r/m, cl (logical right shift)
		// Note: Using SHR (logical) instead of SAR (arithmetic) for unsigned integers
		emitOpcodeExtInstruction(0xD3, X64OpcodeExtension::SHR, ctx.result_physical_reg, ctx.result_value.size_in_bits);

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseAnd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise AND");

		// Perform the bitwise AND operation: AND dst, src (opcode 0x21)
		// Use the operand size to determine whether to use 32-bit or 64-bit operation
		emitBinaryOpInstruction(0x21, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.operand_size_in_bits);

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseOr(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise OR");

		// Perform the bitwise OR operation: OR dst, src (opcode 0x09)
		// Use the operand size to determine whether to use 32-bit or 64-bit operation
		emitBinaryOpInstruction(0x09, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.operand_size_in_bits);

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseXor(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise XOR");

		// Perform the bitwise XOR operation: XOR dst, src (opcode 0x31)
		// Use the operand size to determine whether to use 32-bit or 64-bit operation
		emitBinaryOpInstruction(0x31, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.operand_size_in_bits);

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleModulo(const IrInstruction& instruction) {
		flushAllDirtyRegisters();	// we do this so that RDX is free to use

		regAlloc.release(X64Register::RAX);
		regAlloc.allocateSpecific(X64Register::RAX, INT_MIN);

		regAlloc.release(X64Register::RDX);
		regAlloc.allocateSpecific(X64Register::RDX, INT_MIN);

		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "modulo");

		// For x86-64, modulo is implemented using division
		// idiv instruction computes both quotient (RAX) and remainder (RDX)
		// We need the remainder in RDX

		// Move dividend to RAX (dividend must be in RAX for idiv)
		auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, ctx.result_physical_reg, ctx.result_value.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);

		// Release the original result register since we moved its value to RAX
		regAlloc.release(ctx.result_physical_reg);

		// Sign extend RAX into RDX:RAX
		if (ctx.result_value.size_in_bits == 64) {
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
		if (ctx.result_value.size_in_bits == 64) {
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

		// Manually store remainder from RDX to the result variable's stack location
		// Don't use storeArithmeticResult because it tries to be too clever with register tracking
		if (std::holds_alternative<StringHandle>(ctx.result_value.value)) {
			int final_result_offset = variable_scopes.back().variables[std::get<StringHandle>(ctx.result_value.value)].offset;
			emitMovToFrameSized(
				SizedRegister{X64Register::RDX, 64, false},  // source: RDX register
				SizedStackSlot{final_result_offset, ctx.result_value.size_in_bits, isSignedType(ctx.result_value.type)}  // dest
			);
		} else if (std::holds_alternative<TempVar>(ctx.result_value.value)) {
			auto res_var_op = std::get<TempVar>(ctx.result_value.value);
			auto res_stack_var_addr = getStackOffsetFromTempVar(res_var_op, ctx.result_value.size_in_bits);
			emitMovToFrameSized(
				SizedRegister{X64Register::RDX, 64, false},  // source: RDX register
				SizedStackSlot{res_stack_var_addr, ctx.result_value.size_in_bits, isSignedType(ctx.result_value.type)}  // dest
			);
		}

		regAlloc.release(X64Register::RDX);
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
		logAsmEmit("handleLogicalAnd AND", andInst.data(), andInst.size());
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
				auto mov_opcodes = generatePtrMovToFrame(result_physical_reg, result_stack_var_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
			}
		} else if (std::holds_alternative<StringHandle>(result_operand)) {
			StringHandle result_var_name = std::get<StringHandle>(result_operand);
			auto var_id = variable_scopes.back().variables.find(result_var_name);
			if (var_id != variable_scopes.back().variables.end()) {
				auto store_opcodes = generatePtrMovToFrame(result_physical_reg, var_id->second.offset);
				textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(), store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
			}
		}
	}

	void handleFloatAdd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point addition");

		// Use SSE addss (scalar single-precision) or addsd (scalar double-precision)
		// Now properly handles XMM8-XMM15 registers with REX prefix
		if (ctx.result_value.type == Type::Float) {
			// addss xmm_dst, xmm_src (F3 [REX] 0F 58 /r)
			auto inst = generateSSEInstruction(0xF3, 0x0F, 0x58, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.result_value.type == Type::Double) {
			// addsd xmm_dst, xmm_src (F2 [REX] 0F 58 /r)
			auto inst = generateSSEInstruction(0xF2, 0x0F, 0x58, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatSubtract(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point subtraction");

		// Use SSE subss (scalar single-precision) or subsd (scalar double-precision)
		// Now properly handles XMM8-XMM15 registers with REX prefix
		if (ctx.result_value.type == Type::Float) {
			// subss xmm_dst, xmm_src (F3 [REX] 0F 5C /r)
			auto inst = generateSSEInstruction(0xF3, 0x0F, 0x5C, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.result_value.type == Type::Double) {
			// subsd xmm_dst, xmm_src (F2 [REX] 0F 5C /r)
			auto inst = generateSSEInstruction(0xF2, 0x0F, 0x5C, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatMultiply(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point multiplication");

		// Use SSE mulss (scalar single-precision) or mulsd (scalar double-precision)
		// Now properly handles XMM8-XMM15 registers with REX prefix
		if (ctx.result_value.type == Type::Float) {
			// mulss xmm_dst, xmm_src (F3 [REX] 0F 59 /r)
			auto inst = generateSSEInstruction(0xF3, 0x0F, 0x59, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.result_value.type == Type::Double) {
			// mulsd xmm_dst, xmm_src (F2 [REX] 0F 59 /r)
			auto inst = generateSSEInstruction(0xF2, 0x0F, 0x59, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatDivide(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point division");

		// Use SSE divss (scalar single-precision) or divsd (scalar double-precision)
		// Now properly handles XMM8-XMM15 registers with REX prefix
		if (ctx.result_value.type == Type::Float) {
			// divss xmm_dst, xmm_src (F3 [REX] 0F 5E /r)
			auto inst = generateSSEInstruction(0xF3, 0x0F, 0x5E, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.result_value.type == Type::Double) {
			// divsd xmm_dst, xmm_src (F2 [REX] 0F 5E /r)
			auto inst = generateSSEInstruction(0xF2, 0x0F, 0x5E, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point equal comparison");

		// Use SSE comiss/comisd for comparison
		// Now properly handles XMM8-XMM15 registers with REX prefix
		if (ctx.operand_type == Type::Float) {
			// comiss xmm1, xmm2 ([REX] 0F 2F /r)
			auto inst = generateSSEInstructionNoPrefix(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.operand_type == Type::Double) {
			// comisd xmm1, xmm2 (66 [REX] 0F 2F /r)
			auto inst = generateSSEInstructionDouble(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on zero flag: sete r8
		// IMPORTANT: Always use REX prefix for byte operations to avoid high-byte registers
		uint8_t sete_rex = (static_cast<uint8_t>(bool_reg) >= 8) ? 0x41 : 0x40;
		textSectionData.push_back(sete_rex);
		std::array<uint8_t, 3> seteInst = { 0x0F, 0x94, static_cast<uint8_t>(0xC0 | (static_cast<uint8_t>(bool_reg) & 0x07)) };
		textSectionData.insert(textSectionData.end(), seteInst.begin(), seteInst.end());

		// Update context for boolean result (1 byte)
		ctx.result_value.type = Type::Bool;
		ctx.result_value.size_in_bits = 8;
		ctx.result_physical_reg = bool_reg;

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatNotEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point not equal comparison");

		// Use SSE comiss/comisd for comparison
		// Now properly handles XMM8-XMM15 registers with REX prefix
		Type type = ctx.operand_type; // Use operand type instead of result type
		if (type == Type::Float) {
			// comiss xmm1, xmm2 ([REX] 0F 2F /r)
			auto inst = generateSSEInstructionNoPrefix(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.operand_type == Type::Double) {
			// comisd xmm1, xmm2 (66 [REX] 0F 2F /r)
			auto inst = generateSSEInstructionDouble(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on zero flag: setne r8
		// IMPORTANT: Always use REX prefix for byte operations to avoid high-byte registers
		uint8_t setne_rex = (static_cast<uint8_t>(bool_reg) >= 8) ? 0x41 : 0x40;
		textSectionData.push_back(setne_rex);
		std::array<uint8_t, 3> setneInst = { 0x0F, 0x95, static_cast<uint8_t>(0xC0 | (static_cast<uint8_t>(bool_reg) & 0x07)) };
		textSectionData.insert(textSectionData.end(), setneInst.begin(), setneInst.end());

		// Update context for boolean result (1 byte)
		ctx.result_value.type = Type::Bool;
		ctx.result_value.size_in_bits = 8;
		ctx.result_physical_reg = bool_reg;

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatLessThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point less than comparison");

		// Use SSE comiss/comisd for comparison
		// Now properly handles XMM8-XMM15 registers with REX prefix
		Type type = ctx.operand_type; // Use operand type instead of result type
		if (type == Type::Float) {
			// comiss xmm1, xmm2 ([REX] 0F 2F /r)
			auto inst = generateSSEInstructionNoPrefix(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.operand_type == Type::Double) {
			// comisd xmm1, xmm2 (66 [REX] 0F 2F /r)
			auto inst = generateSSEInstructionDouble(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on carry flag: setb r8 (below = less than for floating-point)
		// IMPORTANT: Always use REX prefix for byte operations to avoid high-byte registers
		uint8_t setb_rex = (static_cast<uint8_t>(bool_reg) >= 8) ? 0x41 : 0x40;
		textSectionData.push_back(setb_rex);
		std::array<uint8_t, 3> setbInst = { 0x0F, 0x92, static_cast<uint8_t>(0xC0 | (static_cast<uint8_t>(bool_reg) & 0x07)) };
		textSectionData.insert(textSectionData.end(), setbInst.begin(), setbInst.end());

		// Update context for boolean result (1 byte)
		ctx.result_value.type = Type::Bool;
		ctx.result_value.size_in_bits = 8;
		ctx.result_physical_reg = bool_reg;  // Update to the boolean result register

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatLessEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point less than or equal comparison");

		// Use SSE comiss/comisd for comparison
		// Now properly handles XMM8-XMM15 registers with REX prefix
		Type type = ctx.operand_type; // Use operand type instead of result type
		if (type == Type::Float) {
			// comiss xmm1, xmm2 ([REX] 0F 2F /r)
			auto inst = generateSSEInstructionNoPrefix(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.operand_type == Type::Double) {
			// comisd xmm1, xmm2 (66 [REX] 0F 2F /r)
			auto inst = generateSSEInstructionDouble(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on flags: setbe r8 (below or equal)
		// IMPORTANT: Always use REX prefix for byte operations to avoid high-byte registers
		uint8_t setbe_rex = (static_cast<uint8_t>(bool_reg) >= 8) ? 0x41 : 0x40;
		textSectionData.push_back(setbe_rex);
		std::array<uint8_t, 3> setbeInst = { 0x0F, 0x96, static_cast<uint8_t>(0xC0 | (static_cast<uint8_t>(bool_reg) & 0x07)) };
		textSectionData.insert(textSectionData.end(), setbeInst.begin(), setbeInst.end());

		// Update context for boolean result (1 byte)
		ctx.result_value.type = Type::Bool;
		ctx.result_value.size_in_bits = 8;
		ctx.result_physical_reg = bool_reg;

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatGreaterThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point greater than comparison");

		// Use SSE comiss/comisd for comparison
		// Now properly handles XMM8-XMM15 registers with REX prefix
		Type type = ctx.operand_type; // Use operand type instead of result type
		if (type == Type::Float) {
			// comiss xmm1, xmm2 ([REX] 0F 2F /r)
			auto inst = generateSSEInstructionNoPrefix(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.operand_type == Type::Double) {
			// comisd xmm1, xmm2 (66 [REX] 0F 2F /r)
			auto inst = generateSSEInstructionDouble(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on flags: seta r8 (above = greater than for floating-point)
		// IMPORTANT: Always use REX prefix for byte operations to avoid high-byte registers
		uint8_t seta_rex = (static_cast<uint8_t>(bool_reg) >= 8) ? 0x41 : 0x40;
		textSectionData.push_back(seta_rex);
		std::array<uint8_t, 3> setaInst = { 0x0F, 0x97, static_cast<uint8_t>(0xC0 | (static_cast<uint8_t>(bool_reg) & 0x07)) };
		textSectionData.insert(textSectionData.end(), setaInst.begin(), setaInst.end());

		// Update context for boolean result (1 byte)
		ctx.result_value.type = Type::Bool;
		ctx.result_value.size_in_bits = 8;
		ctx.result_physical_reg = bool_reg;  // Update to the boolean result register

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatGreaterEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point greater than or equal comparison");

		// Use SSE comiss/comisd for comparison
		// Now properly handles XMM8-XMM15 registers with REX prefix
		Type type = ctx.operand_type; // Use operand type instead of result type
		if (type == Type::Float) {
			// comiss xmm1, xmm2 ([REX] 0F 2F /r)
			auto inst = generateSSEInstructionNoPrefix(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.operand_type == Type::Double) {
			// comisd xmm1, xmm2 (66 [REX] 0F 2F /r)
			auto inst = generateSSEInstructionDouble(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on flags: setae r8 (above or equal)
		// IMPORTANT: Always use REX prefix for byte operations to avoid high-byte registers
		uint8_t setae_rex = (static_cast<uint8_t>(bool_reg) >= 8) ? 0x41 : 0x40;
		textSectionData.push_back(setae_rex);
		std::array<uint8_t, 3> setaeInst = { 0x0F, 0x93, static_cast<uint8_t>(0xC0 | (static_cast<uint8_t>(bool_reg) & 0x07)) };
		textSectionData.insert(textSectionData.end(), setaeInst.begin(), setaeInst.end());

		// Update context for boolean result (1 byte)
		ctx.result_value.type = Type::Bool;
		ctx.result_value.size_in_bits = 8;
		ctx.result_physical_reg = bool_reg;  // Update to the boolean result register

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
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
				auto mov_opcodes = generatePtrMovFromFrame(reg, stack_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), 
					mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				regAlloc.flushSingleDirtyRegister(reg);
			}
		} else if (instruction.isOperandType<StringHandle>(operand_index)) {
			auto var_name = instruction.getOperandAs<StringHandle>(operand_index);
			auto var_id = variable_scopes.back().variables.find(var_name);
			if (var_id != variable_scopes.back().variables.end()) {
				if (auto ref_it = reference_stack_info_.find(var_id->second.offset); ref_it != reference_stack_info_.end()) {
					reg = allocateRegisterWithSpilling();
					loadValueFromReferenceSlot(var_id->second.offset, ref_it->second, reg);
					return reg;
				}
				if (auto reg_opt = regAlloc.tryGetStackVariableRegister(var_id->second.offset); reg_opt.has_value()) {
					reg = reg_opt.value();
				} else {
					reg = allocateRegisterWithSpilling();
					auto mov_opcodes = generatePtrMovFromFrame(reg, var_id->second.offset);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), 
						mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(reg);
				}
			}
		}
		
		return reg;
	}

	X64Register loadTypedValueIntoRegister(const TypedValue& typed_value) {
		X64Register reg = X64Register::Count;
		bool is_signed = isSignedType(typed_value.type);
		
		if (std::holds_alternative<TempVar>(typed_value.value)) {
			auto temp = std::get<TempVar>(typed_value.value);
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
				// Use size-aware loading: source (stack slot) -> destination (64-bit register)
				emitMovFromFrameSized(
					SizedRegister{reg, 64, false},  // dest: 64-bit register
					SizedStackSlot{stack_addr, typed_value.size_in_bits, is_signed}  // source: sized stack slot
				);
				regAlloc.flushSingleDirtyRegister(reg);
			}
		} else if (std::holds_alternative<StringHandle>(typed_value.value)) {
			StringHandle var_name = std::get<StringHandle>(typed_value.value);
			auto var_id = variable_scopes.back().variables.find(var_name);
			if (var_id != variable_scopes.back().variables.end()) {
				if (auto ref_it = reference_stack_info_.find(var_id->second.offset); ref_it != reference_stack_info_.end()) {
					reg = allocateRegisterWithSpilling();
					loadValueFromReferenceSlot(var_id->second.offset, ref_it->second, reg);
					return reg;
				}
				if (auto reg_opt = regAlloc.tryGetStackVariableRegister(var_id->second.offset); reg_opt.has_value()) {
					reg = reg_opt.value();
				} else {
					reg = allocateRegisterWithSpilling();
					// Use size-aware loading: source (stack slot) -> destination (64-bit register)
					emitMovFromFrameSized(
						SizedRegister{reg, 64, false},  // dest: 64-bit register
						SizedStackSlot{var_id->second.offset, typed_value.size_in_bits, is_signed}  // source: sized stack slot
					);
					regAlloc.flushSingleDirtyRegister(reg);
				}
			}
		} else if (std::holds_alternative<unsigned long long>(typed_value.value)) {
			// Load immediate value
			reg = allocateRegisterWithSpilling();
			unsigned long long imm_value = std::get<unsigned long long>(typed_value.value);
			// MOV reg, immediate (64-bit)
			uint8_t rex = 0x48; // REX.W
			if (static_cast<uint8_t>(reg) >= 8) rex |= 0x01; // REX.B
			textSectionData.push_back(rex);
			textSectionData.push_back(0xB8 + (static_cast<uint8_t>(reg) & 0x07)); // MOV reg, imm64
			for (int i = 0; i < 8; ++i) {
				textSectionData.push_back(static_cast<uint8_t>((imm_value >> (i * 8)) & 0xFF));
			}
		}
		
		return reg;
	}

	std::optional<int32_t> findIdentifierStackOffset(StringHandle name) const {
		for (auto scope_it = variable_scopes.rbegin(); scope_it != variable_scopes.rend(); ++scope_it) {
			const auto& scope = *scope_it;
			auto found = scope.variables.find(name);
			if (found != scope.variables.end()) {
				return found->second.offset;
			}
		}
		return std::nullopt;
	}

	enum class IncDecKind { PreIncrement, PostIncrement, PreDecrement, PostDecrement };

	struct UnaryOperandLocation {
		enum class Kind { Stack, Global };
		Kind kind = Kind::Stack;
		int32_t stack_offset = 0;
		StringHandle global_name;

		static UnaryOperandLocation stack(int32_t offset) {
			UnaryOperandLocation loc;
			loc.kind = Kind::Stack;
			loc.stack_offset = offset;
			return loc;
		}

		static UnaryOperandLocation global(StringHandle name) {
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
			return UnaryOperandLocation::global(StringTable::getOrInternStringHandle(name));
		}

		if (instruction.isOperandType<std::string>(operand_index)) {
			return UnaryOperandLocation::global(StringTable::getOrInternStringHandle(instruction.getOperandAs<std::string>(operand_index)));
		}

		assert(false && "Unsupported operand type for unary operation");
		return UnaryOperandLocation::stack(0);
	}

	void appendRipRelativePlaceholder(StringHandle global_name) {
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
		case 32:
			emitMovFromFrameBySize(target_reg, offset, size_in_bits);
			break;
		case 16:
			load_opcodes = generateMovzxFromFrame16(target_reg, offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
			break;
		case 8:
			load_opcodes = generateMovzxFromFrame8(target_reg, offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
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
		switch (size_in_bits) {
		case 64:
		case 32:
			emitMovToFrameSized(
				SizedRegister{source_reg, 64, false},  // source: 64-bit register
				SizedStackSlot{offset, size_in_bits, false}  // dest: sized stack slot
			);
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

	void loadValueFromGlobal(StringHandle global_name, int size_in_bits, X64Register target_reg) {
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
		auto load_ptr = generatePtrMovFromFrame(target_reg, offset);
		textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(),
			               load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
		loadValuePointedByRegister(target_reg, ref_info.value_size_bits);
	}

	bool loadAddressForOperand(const IrInstruction& instruction, size_t operand_index, X64Register target_reg) {
		if (instruction.isOperandType<std::string_view>(operand_index)) {
			auto name = instruction.getOperandAs<std::string_view>(operand_index);
			auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(name));
			if (it == variable_scopes.back().variables.end()) {
				return false;
			}
			auto lea = generateLeaFromFrame(target_reg, it->second.offset);
			textSectionData.insert(textSectionData.end(), lea.op_codes.begin(), lea.op_codes.begin() + lea.size_in_bytes);
			return true;
		}
		if (instruction.isOperandType<std::string>(operand_index)) {
			auto name = instruction.getOperandAs<std::string>(operand_index);
			auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(name));
			if (it == variable_scopes.back().variables.end()) {
				return false;
			}
			auto lea = generateLeaFromFrame(target_reg, it->second.offset);
			textSectionData.insert(textSectionData.end(), lea.op_codes.begin(), lea.op_codes.begin() + lea.size_in_bytes);
			return true;
		}
		if (instruction.isOperandType<TempVar>(operand_index)) {
			auto temp = instruction.getOperandAs<TempVar>(operand_index);
			int32_t src_offset = getStackOffsetFromTempVar(temp);
			if (auto ref_it = reference_stack_info_.find(src_offset); ref_it != reference_stack_info_.end()) {
				auto load_ptr = generatePtrMovFromFrame(target_reg, src_offset);
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

	void storeValueToGlobal(StringHandle global_name, int size_in_bits, X64Register source_reg) {
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
		switch (location.kind) {
			case UnaryOperandLocation::Kind::Stack:
				loadValueFromStack(location.stack_offset, size_in_bits, target_reg);
				break;
			case UnaryOperandLocation::Kind::Global:
				loadValueFromGlobal(location.global_name, size_in_bits, target_reg);
				break;
			default:
				assert(false && "Unhandled UnaryOperandLocation kind in loadUnaryOperandValue");
				break;
		}
	}

	void storeUnaryOperandValue(const UnaryOperandLocation& location, int size_in_bits, X64Register source_reg) {
		switch (location.kind) {
			case UnaryOperandLocation::Kind::Stack:
				storeValueToStack(location.stack_offset, size_in_bits, source_reg);
				break;
			case UnaryOperandLocation::Kind::Global:
				storeValueToGlobal(location.global_name, size_in_bits, source_reg);
				break;
			default:
				assert(false && "Unhandled UnaryOperandLocation kind in storeUnaryOperandValue");
				break;
		}
	}

	void storeIncDecResultValue(TempVar result_var, X64Register source_reg, int size_in_bits) {
		// getStackOffsetFromTempVar automatically allocates stack space if needed
		int32_t offset = getStackOffsetFromTempVar(result_var);
		storeValueToStack(offset, size_in_bits, source_reg);
	}

	UnaryOperandLocation resolveTypedValueLocation(const TypedValue& typed_value) {
		if (std::holds_alternative<TempVar>(typed_value.value)) {
			auto temp = std::get<TempVar>(typed_value.value);
			return UnaryOperandLocation::stack(getStackOffsetFromTempVar(temp));
		}

		if (std::holds_alternative<StringHandle>(typed_value.value)) {
			StringHandle name = std::get<StringHandle>(typed_value.value);
			if (auto offset = findIdentifierStackOffset(name); offset.has_value()) {
				return UnaryOperandLocation::stack(offset.value());
			}
			return UnaryOperandLocation::global(name);
		}

		// IrValue can also contain immediate values (unsigned long long, double)
		// For inc/dec operations, these should not occur
		assert(false && "Unsupported typed value for unary operand location (immediate values not allowed)");
		return UnaryOperandLocation::stack(0);
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
		// Extract UnaryOp from typed payload
		const UnaryOp& unary_op = instruction.getTypedPayload<UnaryOp>();

		int size_in_bits = unary_op.value.size_in_bits;
		UnaryOperandLocation operand_location = resolveTypedValueLocation(unary_op.value);
		X64Register target_reg = X64Register::RAX;
		loadUnaryOperandValue(operand_location, size_in_bits, target_reg);

		bool is_post = (kind == IncDecKind::PostIncrement || kind == IncDecKind::PostDecrement);
		bool is_increment = (kind == IncDecKind::PreIncrement || kind == IncDecKind::PostIncrement);

		if (is_post) {
			storeIncDecResultValue(unary_op.result, target_reg, size_in_bits);
		}

		emitIncDecInstruction(target_reg, is_increment);
		storeUnaryOperandValue(operand_location, size_in_bits, target_reg);

		if (!is_post) {
			storeIncDecResultValue(unary_op.result, target_reg, size_in_bits);
		}
	}

	// Helper: Associate result register with result TempVar's stack offset
	void storeConversionResult(const IrInstruction& instruction, X64Register result_reg, int size_in_bits) {
		TempVar result_var;
		// Try to get result from typed payload first
		if (instruction.hasTypedPayload()) {
			const auto& op = instruction.getTypedPayload<TypeConversionOp>();
			result_var = op.result;
		} else {
			result_var = instruction.getOperandAs<TempVar>(0);
		}
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		regAlloc.set_stack_variable_offset(result_reg, result_offset, size_in_bits);
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
		// Extract UnaryOp from typed payload
		const UnaryOp& unary_op = instruction.getTypedPayload<UnaryOp>();

		[[maybe_unused]] Type type = unary_op.value.type;
		int size_in_bits = unary_op.value.size_in_bits;

		// Load the operand into a register
		X64Register result_physical_reg;
		if (std::holds_alternative<TempVar>(unary_op.value.value)) {
			auto temp_var = std::get<TempVar>(unary_op.value.value);
			auto stack_offset = getStackOffsetFromTempVar(temp_var);
			if (auto reg_opt = regAlloc.tryGetStackVariableRegister(stack_offset); reg_opt.has_value()) {
				result_physical_reg = reg_opt.value();
			} else {
				result_physical_reg = allocateRegisterWithSpilling();
				emitMovFromFrameBySize(result_physical_reg, stack_offset, size_in_bits);
				regAlloc.flushSingleDirtyRegister(result_physical_reg);
			}
		} else if (std::holds_alternative<unsigned long long>(unary_op.value.value)) {
			// Load immediate value
			result_physical_reg = allocateRegisterWithSpilling();
			auto imm_value = std::get<unsigned long long>(unary_op.value.value);
			uint8_t rex_prefix = 0x48;
			uint8_t reg_num = static_cast<uint8_t>(result_physical_reg);
			if (reg_num >= 8) {
				rex_prefix |= 0x01;
				reg_num &= 0x07;
			}
			std::array<uint8_t, 10> movInst = { rex_prefix, static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0, 0, 0, 0, 0 };
			std::memcpy(&movInst[2], &imm_value, sizeof(imm_value));
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		} else if (std::holds_alternative<StringHandle>(unary_op.value.value)) {
			// Load from variable (could be local or global)
			result_physical_reg = allocateRegisterWithSpilling();
			StringHandle var_name = std::get<StringHandle>(unary_op.value.value);
		
			// Check if it's a local variable first
			auto var_id = variable_scopes.back().variables.find(var_name);
			if (var_id != variable_scopes.back().variables.end()) {
				// It's a local variable on the stack - use the correct size
				auto stack_offset = var_id->second.offset;
				emitMovFromFrameBySize(result_physical_reg, stack_offset, size_in_bits);
			} else {
				// It's a global variable - this shouldn't happen for unary ops on locals
				// but we need to handle it for completeness
				assert(false && "Global variables not yet supported in unary operations");
			}
			regAlloc.flushSingleDirtyRegister(result_physical_reg);
		} else {
			assert(false && "Unsupported operand type for unary operation");
			result_physical_reg = X64Register::RAX;
		}
		
		// Perform the specific unary operation
		switch (op) {
			case UnaryOperation::LogicalNot: {
				// Compare with 0: cmp reg, 0 (using full instruction encoding with REX support)
				uint8_t reg_num = static_cast<uint8_t>(result_physical_reg);
				uint8_t rex_prefix = 0x48; // REX.W for 64-bit operation
				if (reg_num >= 8) {
					rex_prefix |= 0x01; // Set REX.B for R8-R15
					reg_num &= 0x07;
				}
				uint8_t modrm = 0xF8 | reg_num; // mod=11, opcode_ext=111 (CMP), r/m=reg
				std::array<uint8_t, 4> cmpInst = { rex_prefix, 0x83, modrm, 0x00 };
				textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

				// Set result to 1 if zero (sete), 0 otherwise
				uint8_t sete_rex = 0x00;
				uint8_t sete_reg = static_cast<uint8_t>(result_physical_reg);
				if (sete_reg >= 8) {
					sete_rex = 0x41; // REX with B bit for R8-R15
					sete_reg &= 0x07;
				} else if (sete_reg >= 4) {
					// RSP, RBP, RSI, RDI need REX to access low byte
					sete_rex = 0x40;
				}
				if (sete_rex != 0) {
					textSectionData.push_back(sete_rex);
				}
				std::array<uint8_t, 3> seteInst = { 0x0F, 0x94, static_cast<uint8_t>(0xC0 | sete_reg) };
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

		// Store the result - associate register with result temp variable's stack offset
		int32_t result_offset = getStackOffsetFromTempVar(unary_op.result);
		regAlloc.set_stack_variable_offset(result_physical_reg, result_offset, size_in_bits);
	}

	void handleSignExtend(const IrInstruction& instruction) {
		// Sign extension: movsx dest, src
		const ConversionOp& conv_op = instruction.getTypedPayload<ConversionOp>();
		int fromSize = conv_op.from.size_in_bits;
		int toSize = conv_op.to_size;

		// Get source value into a register
		X64Register source_reg = loadTypedValueIntoRegister(conv_op.from);
		
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
		int32_t result_offset = getStackOffsetFromTempVar(conv_op.result);
		regAlloc.set_stack_variable_offset(result_reg, result_offset, toSize);
	}

	void handleZeroExtend(const IrInstruction& instruction) {
		// Zero extension: movzx dest, src
		const ConversionOp& conv_op = instruction.getTypedPayload<ConversionOp>();
		int fromSize = conv_op.from.size_in_bits;
		int toSize = conv_op.to_size;

		// If source size is 0 (unknown/auto type) or equal to target size, this is a no-op.
		// The value is already in the correct format, just ensure register tracking.
		if (fromSize == 0 || fromSize == toSize) {
			// Get source value's register (or load it if needed)
			X64Register source_reg = loadTypedValueIntoRegister(conv_op.from);
			// Associate it with the result TempVar - no code generation needed
			int32_t result_offset = getStackOffsetFromTempVar(conv_op.result);
			regAlloc.set_stack_variable_offset(source_reg, result_offset, toSize);
			return;
		}

		// Get source value into a register
		X64Register source_reg = loadTypedValueIntoRegister(conv_op.from);
		
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

		// Store result - associate register with result temp variable's stack offset
		int32_t result_offset = getStackOffsetFromTempVar(conv_op.result);
		regAlloc.set_stack_variable_offset(result_reg, result_offset, toSize);
	}

	void handleTruncate(const IrInstruction& instruction) {
		// Truncation: just use the lower bits by moving to a smaller register
		const ConversionOp& conv_op = instruction.getTypedPayload<ConversionOp>();
		int toSize = conv_op.to_size;

		// Get source value into a register
		X64Register source_reg = loadTypedValueIntoRegister(conv_op.from);
		
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
			logAsmEmit("handleTruncate 8-bit MOVZX", movzx.data(), movzx.size());
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
			// For MOV r/m32, r32 (opcode 89): reg field is SOURCE, r/m field is DEST
			// So we put source_reg in reg field and result_reg in r/m field
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(source_reg) & 0x07) << 3) | (static_cast<uint8_t>(result_reg) & 0x07);
			
			// Check if we need REX prefix
			if (static_cast<uint8_t>(result_reg) >= 8 || static_cast<uint8_t>(source_reg) >= 8) {
				uint8_t rex = 0x40;
				if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x04; // REX.R for source in reg field
				if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x01; // REX.B for dest in r/m field
				std::array<uint8_t, 3> mov = { rex, 0x89, modrm };
				textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
			} else {
				std::array<uint8_t, 2> mov = { 0x89, modrm };
				textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
			}
		} else {
			// 64-bit or fallback: just copy the whole register
			// For MOV r/m64, r64 (opcode 89): reg field is SOURCE, r/m field is DEST
			auto encoding = encodeRegToRegInstruction(source_reg, result_reg);
			std::array<uint8_t, 3> mov = { encoding.rex_prefix, 0x89, encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
		}

		// Store result - associate register with result temp variable's stack offset
		int32_t result_offset = getStackOffsetFromTempVar(conv_op.result);
		regAlloc.set_stack_variable_offset(result_reg, result_offset, toSize);
	}

	void handleFloatToInt(const IrInstruction& instruction) {
		// FloatToInt: convert float/double to integer
		auto& op = instruction.getTypedPayload<TypeConversionOp>();
		
		// Load source value into XMM register
		X64Register source_xmm = X64Register::Count;
		if (std::holds_alternative<TempVar>(op.from.value)) {
			auto temp_var = std::get<TempVar>(op.from.value);
			auto stack_offset = getStackOffsetFromTempVar(temp_var);
			// Check if the value is already in an XMM register
			if (auto existing_reg = regAlloc.tryGetStackVariableRegister(stack_offset); existing_reg.has_value()) {
				source_xmm = existing_reg.value();
			} else {
				source_xmm = allocateXMMRegisterWithSpilling();
				bool is_float = (op.from.type == Type::Float);
				emitFloatMovFromFrame(source_xmm, stack_offset, is_float);
			}
		} else {
			assert(std::holds_alternative<StringHandle>(op.from.value) && "Expected StringHandle or TempVar type");
			StringHandle var_name = std::get<StringHandle>(op.from.value);
			auto var_it = variable_scopes.back().variables.find(var_name);
			assert(var_it != variable_scopes.back().variables.end() && "Variable not found in variables");
			// Check if the value is already in an XMM register
			if (auto existing_reg = regAlloc.tryGetStackVariableRegister(var_it->second.offset); existing_reg.has_value()) {
				source_xmm = existing_reg.value();
			} else {
				source_xmm = allocateXMMRegisterWithSpilling();
				bool is_float = (op.from.type == Type::Float);
				emitFloatMovFromFrame(source_xmm, var_it->second.offset, is_float);
			}
		}

		// Allocate result GPR
		X64Register result_reg = allocateRegisterWithSpilling();

		// cvttss2si (float to int) or cvttsd2si (double to int)
		// For 32-bit: F3 0F 2C /r (cvttss2si r32, xmm) or F2 0F 2C /r (cvttsd2si r32, xmm)
		// For 64-bit: F3 REX.W 0F 2C /r (cvttss2si r64, xmm) or F2 REX.W 0F 2C /r (cvttsd2si r64, xmm)
		bool is_float = (op.from.type == Type::Float);
		uint8_t prefix = is_float ? 0xF3 : 0xF2;
		
		// Only use REX.W for 64-bit result
		bool need_rex_w = (op.to_size_in_bits == 64);
		uint8_t rex = need_rex_w ? 0x48 : 0x40;
		
		// Add REX.R if result register >= 8
		if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04;
		
		// Add REX.B if XMM register >= 8
		uint8_t xmm_bits = static_cast<uint8_t>(source_xmm) - static_cast<uint8_t>(X64Register::XMM0);
		if (xmm_bits >= 8) rex |= 0x01;

		uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (xmm_bits & 0x07);
		
		// Only emit REX prefix if needed (64-bit or extended registers)
		if (rex != 0x40) {
			std::array<uint8_t, 5> cvtt = { prefix, rex, 0x0F, 0x2C, modrm };
			textSectionData.insert(textSectionData.end(), cvtt.begin(), cvtt.end());
		} else {
			std::array<uint8_t, 4> cvtt = { prefix, 0x0F, 0x2C, modrm };
			textSectionData.insert(textSectionData.end(), cvtt.begin(), cvtt.end());
		}

		// Release XMM register
		regAlloc.release(source_xmm);

		// Store result
		storeConversionResult(instruction, result_reg, op.to_size_in_bits);
	}

	void handleIntToFloat(const IrInstruction& instruction) {
		// IntToFloat: convert integer to float/double
		auto& op = instruction.getTypedPayload<TypeConversionOp>();

		// Load source value into GPR
		X64Register source_reg = loadTypedValueIntoRegister(op.from);

		// Allocate result XMM register
		X64Register result_xmm = allocateXMMRegisterWithSpilling();

		// cvtsi2ss (int to float) or cvtsi2sd (int to double)
		// Opcode: F3 REX.W 0F 2A /r (cvtsi2ss xmm, r64) for float
		// Opcode: F2 REX.W 0F 2A /r (cvtsi2sd xmm, r64) for double
		bool is_float = (op.to_type == Type::Float);
		uint8_t prefix = is_float ? 0xF3 : 0xF2;
		
		uint8_t rex = 0x48;  // REX.W for 64-bit source
		uint8_t xmm_bits = static_cast<uint8_t>(result_xmm) - static_cast<uint8_t>(X64Register::XMM0);
		if (xmm_bits >= 8) rex |= 0x04;  // REX.R
		if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01;  // REX.B

		uint8_t modrm = 0xC0 | ((xmm_bits & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
		std::array<uint8_t, 5> cvt = { prefix, rex, 0x0F, 0x2A, modrm };
		textSectionData.insert(textSectionData.end(), cvt.begin(), cvt.end());

		// Release source GPR
		regAlloc.release(source_reg);

		// Store result XMM to stack - keep it in register for now
		auto result_offset = getStackOffsetFromTempVar(op.result);
		regAlloc.set_stack_variable_offset(result_xmm, result_offset, op.to_size_in_bits);
	}

	void handleFloatToFloat(const IrInstruction& instruction) {
		// FloatToFloat: convert float <-> double
		auto& op = instruction.getTypedPayload<TypeConversionOp>();

		// Load source value into XMM register
		X64Register source_xmm = X64Register::Count;
		if (std::holds_alternative<TempVar>(op.from.value)) {
			auto temp_var = std::get<TempVar>(op.from.value);
			auto stack_offset = getStackOffsetFromTempVar(temp_var);
			source_xmm = allocateXMMRegisterWithSpilling();
			bool is_float = (op.from.type == Type::Float);
			emitFloatMovFromFrame(source_xmm, stack_offset, is_float);
		} else if (std::holds_alternative<StringHandle>(op.from.value)) {
			StringHandle var_name = std::get<StringHandle>(op.from.value);
			auto var_it = variable_scopes.back().variables.find(var_name);
			assert(var_it != variable_scopes.back().variables.end());
			source_xmm = allocateXMMRegisterWithSpilling();
			bool is_float = (op.from.type == Type::Float);
			emitFloatMovFromFrame(source_xmm, var_it->second.offset, is_float);
		}

		// Allocate result XMM register
		X64Register result_xmm = allocateXMMRegisterWithSpilling();

		// cvtss2sd (float to double) or cvtsd2ss (double to float)
		// Now properly handles XMM8-XMM15 registers with REX prefix
		if (op.from.type == Type::Float && op.to_type == Type::Double) {
			// cvtss2sd xmm, xmm (F3 [REX] 0F 5A /r)
			auto inst = generateSSEInstruction(0xF3, 0x0F, 0x5A, result_xmm, source_xmm);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else {
			// cvtsd2ss xmm, xmm (F2 [REX] 0F 5A /r)
			auto inst = generateSSEInstruction(0xF2, 0x0F, 0x5A, result_xmm, source_xmm);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Release source XMM
		regAlloc.release(source_xmm);

		// Store result XMM to stack - keep it in register for now
		auto result_offset = getStackOffsetFromTempVar(op.result);
		regAlloc.set_stack_variable_offset(result_xmm, result_offset, op.to_size_in_bits);
	}

	void handleAddAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "add assignment");
		
		// Check if this is floating-point addition
		if (ctx.result_value.type == Type::Float || ctx.result_value.type == Type::Double) {
			// Use SSE addss (scalar single-precision) or addsd (scalar double-precision)
			if (ctx.result_value.type == Type::Float) {
				// addss xmm_dst, xmm_src (F3 [REX] 0F 58 /r)
				auto inst = generateSSEInstruction(0xF3, 0x0F, 0x58, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			} else {
				// addsd xmm_dst, xmm_src (F2 [REX] 0F 58 /r)
				auto inst = generateSSEInstruction(0xF2, 0x0F, 0x58, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			}
		} else {
			// Integer addition: Use correct register size based on operand size
			// Pass include_rex_w=false for 32-bit operations
			bool include_rex_w = (ctx.operand_size_in_bits == 64);
			auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg, include_rex_w);
			
			// Only emit REX prefix if needed (will be 0 for 32-bit with regs < 8)
			if (encoding.rex_prefix != 0) {
				textSectionData.push_back(encoding.rex_prefix);
			}
			textSectionData.push_back(0x01); // ADD opcode
			textSectionData.push_back(encoding.modrm_byte);
		}
		storeArithmeticResult(ctx);
	}

	void handleSubAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "subtract assignment");
		
		// Check if this is floating-point subtraction
		if (ctx.result_value.type == Type::Float || ctx.result_value.type == Type::Double) {
			// Use SSE subss (scalar single-precision) or subsd (scalar double-precision)
			if (ctx.result_value.type == Type::Float) {
				// subss xmm_dst, xmm_src (F3 [REX] 0F 5C /r)
				auto inst = generateSSEInstruction(0xF3, 0x0F, 0x5C, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			} else {
				// subsd xmm_dst, xmm_src (F2 [REX] 0F 5C /r)
				auto inst = generateSSEInstruction(0xF2, 0x0F, 0x5C, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			}
		} else {
			// Integer subtraction: Use correct register size based on operand size
			// Pass include_rex_w=false for 32-bit operations
			bool include_rex_w = (ctx.operand_size_in_bits == 64);
			auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg, include_rex_w);
			
			// Only emit REX prefix if needed (will be 0 for 32-bit with regs < 8)
			if (encoding.rex_prefix != 0) {
				textSectionData.push_back(encoding.rex_prefix);
			}
			textSectionData.push_back(0x29); // SUB opcode
			textSectionData.push_back(encoding.modrm_byte);
		}
		storeArithmeticResult(ctx);
	}

	void handleMulAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "multiply assignment");
		
		// Check if this is floating-point multiplication
		if (ctx.result_value.type == Type::Float || ctx.result_value.type == Type::Double) {
			// Use SSE mulss (scalar single-precision) or mulsd (scalar double-precision)
			if (ctx.result_value.type == Type::Float) {
				// mulss xmm_dst, xmm_src (F3 [REX] 0F 59 /r)
				auto inst = generateSSEInstruction(0xF3, 0x0F, 0x59, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			} else {
				// mulsd xmm_dst, xmm_src (F2 [REX] 0F 59 /r)
				auto inst = generateSSEInstruction(0xF2, 0x0F, 0x59, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			}
		} else {
			// Integer multiplication: IMUL r64, r/m64
			// Use correct register size based on operand size
			// Note: For IMUL, the reg field is the destination (result) and rm is the source (rhs)
			bool include_rex_w = (ctx.operand_size_in_bits == 64);
			auto encoding = encodeRegToRegInstruction(ctx.result_physical_reg, ctx.rhs_physical_reg, include_rex_w);
			
			// Only emit REX prefix if needed
			if (encoding.rex_prefix != 0) {
				textSectionData.push_back(encoding.rex_prefix);
			}
			textSectionData.push_back(0x0F);
			textSectionData.push_back(0xAF);
			textSectionData.push_back(encoding.modrm_byte);
		}
		storeArithmeticResult(ctx);
	}

	void handleDivAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "divide assignment");
		
		// Check if this is floating-point division
		if (ctx.result_value.type == Type::Float || ctx.result_value.type == Type::Double) {
			// Use SSE divss (scalar single-precision) or divsd (scalar double-precision)
			if (ctx.result_value.type == Type::Float) {
				// divss xmm_dst, xmm_src (F3 [REX] 0F 5E /r)
				auto inst = generateSSEInstruction(0xF3, 0x0F, 0x5E, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			} else {
				// divsd xmm_dst, xmm_src (F2 [REX] 0F 5E /r)
				auto inst = generateSSEInstruction(0xF2, 0x0F, 0x5E, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			}
		} else {
			// Integer division
			// Use correct register size based on operand size
			bool include_rex_w = (ctx.operand_size_in_bits == 64);
			
			// mov rax, result_reg (move dividend to RAX)
			auto mov_to_rax = encodeRegToRegInstruction(ctx.result_physical_reg, X64Register::RAX, include_rex_w);
			if (mov_to_rax.rex_prefix != 0) {
				textSectionData.push_back(mov_to_rax.rex_prefix);
			}
			textSectionData.push_back(0x89);
			textSectionData.push_back(mov_to_rax.modrm_byte);
			
			// Sign extend based on operand size
			if (ctx.operand_size_in_bits == 64) {
				// cqo (sign extend RAX to RDX:RAX)
				std::array<uint8_t, 2> cqoInst = { 0x48, 0x99 };
				textSectionData.insert(textSectionData.end(), cqoInst.begin(), cqoInst.end());
			} else {
				// cdq (sign extend EAX to EDX:EAX) - 32-bit
				textSectionData.push_back(0x99);
			}
			
			// idiv rhs_reg (divide RDX:RAX by rhs_reg, quotient in RAX)
			emitOpcodeExtInstruction(0xF7, X64OpcodeExtension::IDIV, ctx.rhs_physical_reg, ctx.operand_size_in_bits);
			
			// mov result_reg, rax (move quotient to result)
			auto mov_from_rax = encodeRegToRegInstruction(X64Register::RAX, ctx.result_physical_reg, include_rex_w);
			if (mov_from_rax.rex_prefix != 0) {
				textSectionData.push_back(mov_from_rax.rex_prefix);
			}
			textSectionData.push_back(0x89);
			textSectionData.push_back(mov_from_rax.modrm_byte);
		}
		
		storeArithmeticResult(ctx);
	}

	void handleModAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "modulo assignment");
		
		// Use correct register size based on operand size
		bool include_rex_w = (ctx.operand_size_in_bits == 64);
		
		// mov rax, result_reg (move dividend to RAX)
		auto mov_to_rax = encodeRegToRegInstruction(ctx.result_physical_reg, X64Register::RAX, include_rex_w);
		if (mov_to_rax.rex_prefix != 0) {
			textSectionData.push_back(mov_to_rax.rex_prefix);
		}
		textSectionData.push_back(0x89);
		textSectionData.push_back(mov_to_rax.modrm_byte);
		
		// Sign extend based on operand size
		if (ctx.operand_size_in_bits == 64) {
			// cqo (sign extend RAX to RDX:RAX)
			std::array<uint8_t, 2> cqoInst = { 0x48, 0x99 };
			textSectionData.insert(textSectionData.end(), cqoInst.begin(), cqoInst.end());
		} else {
			// cdq (sign extend EAX to EDX:EAX) - 32-bit
			textSectionData.push_back(0x99);
		}
		
		// idiv rhs_reg (divide RDX:RAX by rhs_reg, remainder in RDX)
		emitOpcodeExtInstruction(0xF7, X64OpcodeExtension::IDIV, ctx.rhs_physical_reg, ctx.operand_size_in_bits);
		
		// mov result_reg, rdx (move remainder to result)
		auto mov_from_rdx = encodeRegToRegInstruction(X64Register::RDX, ctx.result_physical_reg, include_rex_w);
		if (mov_from_rdx.rex_prefix != 0) {
			textSectionData.push_back(mov_from_rdx.rex_prefix);
		}
		textSectionData.push_back(0x89);
		textSectionData.push_back(mov_from_rdx.modrm_byte);
		
		storeArithmeticResult(ctx);
	}

	void handleAndAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise and assignment");
		emitBinaryOpInstruction(0x21, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.result_value.size_in_bits);
		storeArithmeticResult(ctx);
	}

	void handleOrAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise or assignment");
		emitBinaryOpInstruction(0x09, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.result_value.size_in_bits);
		storeArithmeticResult(ctx);
	}

	void handleXorAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise xor assignment");
		emitBinaryOpInstruction(0x31, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.result_value.size_in_bits);
		storeArithmeticResult(ctx);
	}

	void handleShlAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift left assignment");
		auto bin_op = *getTypedPayload<BinaryOp>(instruction);
		
		// Move RHS to CL register (using RHS size for the move)
		emitMovRegToReg(ctx.rhs_physical_reg, X64Register::RCX, bin_op.rhs.size_in_bits);
		
		// Emit SHL instruction with correct size
		emitOpcodeExtInstruction(0xD3, X64OpcodeExtension::SHL, ctx.result_physical_reg, ctx.result_value.size_in_bits);
		
		storeArithmeticResult(ctx);
	}

	void handleShrAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift right assignment");
		auto bin_op = *getTypedPayload<BinaryOp>(instruction);
		
		// Move RHS to CL register (using RHS size for the move)
		emitMovRegToReg(ctx.rhs_physical_reg, X64Register::RCX, bin_op.rhs.size_in_bits);
		
		// Emit SAR instruction with correct size
		emitOpcodeExtInstruction(0xD3, X64OpcodeExtension::SAR, ctx.result_physical_reg, ctx.result_value.size_in_bits);
		
		storeArithmeticResult(ctx);
	}

	void handleAssignment(const IrInstruction& instruction) {
		// Use typed payload format
		const AssignmentOp& op = instruction.getTypedPayload<AssignmentOp>();
		FLASH_LOG(Codegen, Debug, "handleAssignment called");
		Type lhs_type = op.lhs.type;
		//int lhs_size_bits = instruction.getOperandAs<int>(2);

		// Special handling for pointer store (assignment through pointer)
		if (op.is_pointer_store) {
			// LHS is a pointer (TempVar), RHS is the value to store
			// Load the pointer into a register
			X64Register ptr_reg = allocateRegisterWithSpilling();
			if (std::holds_alternative<TempVar>(op.lhs.value)) {
				TempVar ptr_var = std::get<TempVar>(op.lhs.value);
				int32_t ptr_offset = getStackOffsetFromTempVar(ptr_var);
				emitMovFromFrame(ptr_reg, ptr_offset);
			} else {
				assert(false && "Pointer store LHS must be a TempVar");
				return;
			}
			
			// Get the value to store
			X64Register value_reg = allocateRegisterWithSpilling();
			int value_size_bytes = op.rhs.size_in_bits / 8;
			
			if (std::holds_alternative<unsigned long long>(op.rhs.value)) {
				// Immediate integer value
				unsigned long long imm_value = std::get<unsigned long long>(op.rhs.value);
				if (value_size_bytes == 8) {
					emitMovImm64(value_reg, imm_value);
				} else {
					moveImmediateToRegister(value_reg, static_cast<int32_t>(imm_value));
				}
			} else if (std::holds_alternative<double>(op.rhs.value)) {
				// Immediate double value
				double double_value = std::get<double>(op.rhs.value);
				uint64_t bits;
				std::memcpy(&bits, &double_value, sizeof(bits));
				emitMovImm64(value_reg, bits);
			} else if (std::holds_alternative<TempVar>(op.rhs.value)) {
				// Load from temp var
				TempVar rhs_var = std::get<TempVar>(op.rhs.value);
				int32_t rhs_offset = getStackOffsetFromTempVar(rhs_var);
				emitMovFromFrameBySize(value_reg, rhs_offset, op.rhs.size_in_bits);
			} else {
				assert(false && "Unsupported RHS type for pointer store");
				return;
			}
			
			// Store through the pointer: [ptr_reg] = value_reg
			emitStoreToMemory(textSectionData, value_reg, ptr_reg, 0, value_size_bytes);
			
			regAlloc.release(ptr_reg);
			regAlloc.release(value_reg);
			return;
		}

		// Special handling for function pointer assignment
		if (lhs_type == Type::FunctionPointer) {
			// Get LHS destination
			int32_t lhs_offset = -1;

			if (std::holds_alternative<StringHandle>(op.lhs.value)) {
				StringHandle lhs_var_name_handle = std::get<StringHandle>(op.lhs.value);
			std::string_view lhs_var_name = StringTable::getStringView(lhs_var_name_handle);
				auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(lhs_var_name));
				if (it != variable_scopes.back().variables.end()) {
					lhs_offset = it->second.offset;
				}
			} else if (std::holds_alternative<TempVar>(op.lhs.value)) {
				TempVar lhs_var = std::get<TempVar>(op.lhs.value);
				lhs_offset = getStackOffsetFromTempVar(lhs_var);
			}

			if (lhs_offset == -1) {
				assert(false && "LHS variable not found in function pointer assignment");
				return;
			}

			// Get RHS source (function address or nullptr)
			X64Register source_reg = X64Register::RAX;

			if (std::holds_alternative<TempVar>(op.rhs.value)) {
				TempVar rhs_var = std::get<TempVar>(op.rhs.value);
				int32_t rhs_offset = getStackOffsetFromTempVar(rhs_var);

				// Load function address from RHS stack location into RAX
				emitMovFromFrame(source_reg, rhs_offset);
			} else if (std::holds_alternative<unsigned long long>(op.rhs.value)) {
				// RHS is an immediate value (e.g., nullptr = 0)
				unsigned long long rhs_value = std::get<unsigned long long>(op.rhs.value);
				emitMovImm64(source_reg, rhs_value);
			}
			
			// Store RAX to LHS stack location (8 bytes for function pointer - always 64-bit)
			emitMovToFrameSized(
				SizedRegister{source_reg, 64, false},  // source: 64-bit register
				SizedStackSlot{lhs_offset, 64, false}  // dest: 64-bit for function pointer
			);

			// Clear any stale register associations for this stack offset
			// This ensures subsequent loads will actually load from memory instead of using stale cached values
			regAlloc.clearStackVariableAssociations(lhs_offset);

			return;
		}
	
		// Special handling for struct assignment
		if (lhs_type == Type::Struct) {
			// For struct assignment, we need to copy the entire struct value
			// LHS is the destination (should be a variable name or TempVar)
			// RHS is the source (should be a TempVar from function return, or another variable)

			// Get LHS destination
			int32_t lhs_offset = -1;

			if (std::holds_alternative<StringHandle>(op.lhs.value)) {
				StringHandle lhs_var_name_handle = std::get<StringHandle>(op.lhs.value);
			std::string_view lhs_var_name = StringTable::getStringView(lhs_var_name_handle);
				auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(lhs_var_name));
				if (it != variable_scopes.back().variables.end()) {
					lhs_offset = it->second.offset;
				}
			} else if (std::holds_alternative<TempVar>(op.lhs.value)) {
				TempVar lhs_var = std::get<TempVar>(op.lhs.value);
				lhs_offset = getStackOffsetFromTempVar(lhs_var);
			}

			if (lhs_offset == -1) {
				assert(false && "LHS variable not found in struct assignment");
				return;
			}

			// Get RHS source offset
			int32_t rhs_offset = -1;
			if (std::holds_alternative<StringHandle>(op.rhs.value)) {
				StringHandle rhs_var_name_handle = std::get<StringHandle>(op.rhs.value);
			std::string_view rhs_var_name = StringTable::getStringView(rhs_var_name_handle);
				auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(rhs_var_name));
				if (it != variable_scopes.back().variables.end()) {
					rhs_offset = it->second.offset;
				}
			} else if (std::holds_alternative<TempVar>(op.rhs.value)) {
				TempVar rhs_var = std::get<TempVar>(op.rhs.value);
				rhs_offset = getStackOffsetFromTempVar(rhs_var);
			}

			if (rhs_offset == -1) {
				assert(false && "RHS variable not found in struct assignment");
				return;
			}

			// Get struct size in bytes from TypedValue (round up to handle partial bytes)
			int struct_size_bytes = (op.lhs.size_in_bits + 7) / 8;
			
			// Copy struct using 8-byte chunks, then handle remaining bytes
			int offset = 0;
			while (offset + 8 <= struct_size_bytes) {
				// Load 8 bytes from RHS: MOV RAX, [RBP + rhs_offset + offset]
				emitMovFromFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{rhs_offset + offset, 64, false}
				);
				// Store 8 bytes to LHS: MOV [RBP + lhs_offset + offset], RAX
				emitMovToFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{lhs_offset + offset, 64, false}
				);
				offset += 8;
			}
			
			// Handle remaining bytes (4, 2, 1)
			if (offset + 4 <= struct_size_bytes) {
				emitMovFromFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{rhs_offset + offset, 32, false}
				);
				emitMovToFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{lhs_offset + offset, 32, false}
				);
				offset += 4;
			}
			if (offset + 2 <= struct_size_bytes) {
				emitMovFromFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{rhs_offset + offset, 16, false}
				);
				emitMovToFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{lhs_offset + offset, 16, false}
				);
				offset += 2;
			}
			if (offset + 1 <= struct_size_bytes) {
				emitMovFromFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{rhs_offset + offset, 8, false}
				);
				emitMovToFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{lhs_offset + offset, 8, false}
				);
			}
			return;
		}

		// For non-struct types, we need to copy the value from RHS to LHS
		// Get LHS destination
		int32_t lhs_offset = -1;

		if (std::holds_alternative<StringHandle>(op.lhs.value)) {
			StringHandle lhs_var_name_handle = std::get<StringHandle>(op.lhs.value);
			std::string_view lhs_var_name = StringTable::getStringView(lhs_var_name_handle);
			auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(lhs_var_name));
			if (it != variable_scopes.back().variables.end()) {
				lhs_offset = it->second.offset;
			} else {
				FLASH_LOG(Codegen, Error, "String LHS variable '", lhs_var_name, "' not found in variables map");
			}
		} else if (std::holds_alternative<TempVar>(op.lhs.value)) {
			TempVar lhs_var = std::get<TempVar>(op.lhs.value);
			// TempVar(0) is a sentinel value indicating an invalid/uninitialized temp variable
			// This can happen with template functions that have reference parameters
			// In this case, the assignment should not have been generated - report error and skip
			if (lhs_var.var_number == 0) {
				FLASH_LOG(Codegen, Error, "Invalid assignment to sentinel TempVar(0) - likely a code generation bug with template reference parameters");
				return;  // Skip this invalid assignment
			}
			lhs_offset = getStackOffsetFromTempVar(lhs_var);
			if (lhs_offset == -1) {
				FLASH_LOG(Codegen, Error, "TempVar LHS with var_number=", lhs_var.var_number, " (name='", lhs_var.name(), "') not found");
			}
		} else if (std::holds_alternative<unsigned long long>(op.lhs.value)) {
			unsigned long long lhs_value = std::get<unsigned long long>(op.lhs.value);
			std::ostringstream rhs_str;
			printTypedValue(rhs_str, op.rhs);
			FLASH_LOG(Codegen, Error, "[Line ", instruction.getLineNumber(), "] LHS is an immediate value (", lhs_value, ") - invalid for assignment. RHS: ", rhs_str.str());
			return;
		} else if (std::holds_alternative<double>(op.lhs.value)) {
			double lhs_value = std::get<double>(op.lhs.value);
			std::ostringstream rhs_str;
			printTypedValue(rhs_str, op.rhs);
			FLASH_LOG(Codegen, Error, "[Line ", instruction.getLineNumber(), "] LHS is an immediate value (", lhs_value, ") - invalid for assignment. RHS: ", rhs_str.str());
			return;
		} else {
			FLASH_LOG(Codegen, Error, "LHS value has completely unexpected type in variant");
			return;
		}

		if (lhs_offset == -1) {
			FLASH_LOG(Codegen, Error, "LHS variable not found in assignment - skipping");
			return;
		}

		// Check if LHS is a reference - if so, we're initializing a reference binding
		auto lhs_ref_it = reference_stack_info_.find(lhs_offset);
		
		// Debug: check what type LHS is
		if (std::holds_alternative<StringHandle>(op.lhs.value)) {
			FLASH_LOG(Codegen, Debug, "LHS is string_view: '", std::get<StringHandle>(op.lhs.value), "'");
		} else if (std::holds_alternative<TempVar>(op.lhs.value)) {
			FLASH_LOG(Codegen, Debug, "LHS is TempVar: '", std::get<TempVar>(op.lhs.value).name(), "'");
		} else {
			FLASH_LOG(Codegen, Debug, "LHS is other type");
		}
		
		// If not found with TempVar offset and LHS is a TempVar, try looking up by name
		if (lhs_ref_it == reference_stack_info_.end() && std::holds_alternative<TempVar>(op.lhs.value)) {
			TempVar lhs_var = std::get<TempVar>(op.lhs.value);
			std::string_view var_name = lhs_var.name();
			FLASH_LOG(Codegen, Debug, "LHS is TempVar with name: '", var_name, "'");
			// Remove the '%' prefix if present
			if (!var_name.empty() && var_name[0] == '%') {
				var_name = var_name.substr(1);
				FLASH_LOG(Codegen, Debug, "After removing %, name: '", var_name, "'");
			}
			auto named_var_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(var_name));
			if (named_var_it != variable_scopes.back().variables.end()) {
				int32_t named_offset = named_var_it->second.offset;
				FLASH_LOG(Codegen, Debug, "Found in named vars at offset: ", named_offset);
				lhs_ref_it = reference_stack_info_.find(named_offset);
				if (lhs_ref_it != reference_stack_info_.end()) {
					// Found it! Update lhs_offset to use the named variable offset
					lhs_offset = named_offset;
					FLASH_LOG(Codegen, Debug, "Found reference info at named offset!");
				}
			} else {
				FLASH_LOG(Codegen, Debug, "Not found in named vars");
			}
		}
		
		FLASH_LOG(Codegen, Debug, "Assignment: lhs_offset=", lhs_offset, ", is_reference=", (lhs_ref_it != reference_stack_info_.end()), ", lhs.is_reference=", op.lhs.is_reference);
		
		// Check if LHS is a reference - either from reference_stack_info_ or from the TypedValue metadata
		bool lhs_is_reference = (lhs_ref_it != reference_stack_info_.end()) || op.lhs.is_reference;
		
		if (lhs_is_reference) {
			// LHS is a reference variable
			// In C++, references cannot be rebound after initialization
			// Any assignment to a reference should modify the object it refers to (dereference semantics)
			// Example: int x = 10; int& ref = x; ref = 20; // This modifies x, not ref
			
			// Step 1: Load the address stored in the reference variable (LHS)
			X64Register ref_addr_reg = allocateRegisterWithSpilling();
			emitMovFromFrame(ref_addr_reg, lhs_offset);
			FLASH_LOG(Codegen, Debug, "Reference assignment: Loaded address from reference variable at offset ", lhs_offset);
			
			// Step 2: Load or compute the value to store (RHS)
			X64Register value_reg = allocateRegisterWithSpilling();
			
			// Get reference value type and size
			Type value_type;
			int value_size_bits;
			if (lhs_ref_it != reference_stack_info_.end()) {
				value_type = lhs_ref_it->second.value_type;
				value_size_bits = lhs_ref_it->second.value_size_bits;
			} else {
				// Use TypedValue metadata
				value_type = op.lhs.type;
				value_size_bits = op.lhs.size_in_bits;
			}
			int value_size_bytes = value_size_bits / 8;
			
			if (std::holds_alternative<unsigned long long>(op.rhs.value)) {
				// RHS is an immediate value
				uint64_t imm_value = std::get<unsigned long long>(op.rhs.value);
				FLASH_LOG(Codegen, Debug, "Reference assignment: RHS is immediate value: ", imm_value);
				moveImmediateToRegister(value_reg, imm_value);
			} else if (std::holds_alternative<StringHandle>(op.rhs.value)) {
				// RHS is a variable name
				StringHandle rhs_var_name_handle = std::get<StringHandle>(op.rhs.value);
				std::string_view rhs_var_name = StringTable::getStringView(rhs_var_name_handle);
				FLASH_LOG(Codegen, Debug, "Reference assignment: RHS is variable: '", rhs_var_name, "'");
				auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(rhs_var_name));
				if (it != variable_scopes.back().variables.end()) {
					int32_t rhs_offset = it->second.offset;
					// Check if RHS is also a reference
					auto rhs_ref_it = reference_stack_info_.find(rhs_offset);
					if (rhs_ref_it != reference_stack_info_.end()) {
						// RHS is a reference - dereference it to get the value
						X64Register rhs_addr_reg = allocateRegisterWithSpilling();
						emitMovFromFrame(rhs_addr_reg, rhs_offset);  // Load pointer from reference
						emitMovFromMemory(value_reg, rhs_addr_reg, 0, value_size_bytes);  // Dereference
						regAlloc.release(rhs_addr_reg);
					} else {
						// RHS is a regular variable - load its value
						emitMovFromFrameSized(
							SizedRegister{value_reg, value_size_bits, isSignedType(value_type)},
							SizedStackSlot{rhs_offset, value_size_bits, isSignedType(value_type)}
						);
					}
				} else {
					FLASH_LOG(Codegen, Error, "RHS variable '", rhs_var_name, "' not found for reference assignment");
					regAlloc.release(ref_addr_reg);
					regAlloc.release(value_reg);
					return;
				}
			} else if (std::holds_alternative<TempVar>(op.rhs.value)) {
				// RHS is a TempVar
				TempVar rhs_var = std::get<TempVar>(op.rhs.value);
				FLASH_LOG(Codegen, Debug, "Reference assignment: RHS is TempVar: '", rhs_var.name(), "'");
				int32_t rhs_offset = getStackOffsetFromTempVar(rhs_var);
				// Check if RHS is a reference
				auto rhs_ref_it = reference_stack_info_.find(rhs_offset);
				if (rhs_ref_it != reference_stack_info_.end()) {
					// RHS is a reference - dereference it
					X64Register rhs_addr_reg = allocateRegisterWithSpilling();
					emitMovFromFrame(rhs_addr_reg, rhs_offset);
					emitMovFromMemory(value_reg, rhs_addr_reg, 0, value_size_bytes);
					regAlloc.release(rhs_addr_reg);
				} else {
					// Load value from TempVar
					emitMovFromFrameSized(
						SizedRegister{value_reg, value_size_bits, isSignedType(value_type)},
						SizedStackSlot{rhs_offset, value_size_bits, isSignedType(value_type)}
					);
				}
			} else {
				FLASH_LOG(Codegen, Error, "Unsupported RHS type for reference assignment");
				regAlloc.release(ref_addr_reg);
				regAlloc.release(value_reg);
				return;
			}
			
			// Step 3: Store the value to the address pointed to by the reference (dereference and store)
			emitStoreToMemory(textSectionData, value_reg, ref_addr_reg, 0, value_size_bytes);
			FLASH_LOG(Codegen, Debug, "Reference assignment: Stored value to dereferenced address");
			
			regAlloc.release(ref_addr_reg);
			regAlloc.release(value_reg);
			
			return;  // Done with reference assignment
		}

		// For non-reference LHS, proceed with normal assignment
		// Get RHS source
		Type rhs_type = op.rhs.type;
		X64Register source_reg = X64Register::RAX;

		// Load RHS value into a register
		if (std::holds_alternative<StringHandle>(op.rhs.value)) {
			StringHandle rhs_var_name_handle = std::get<StringHandle>(op.rhs.value);
			std::string_view rhs_var_name = StringTable::getStringView(rhs_var_name_handle);
			auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(rhs_var_name));
			if (it != variable_scopes.back().variables.end()) {
				int32_t rhs_offset = it->second.offset;
				
				// Check if RHS is a reference - if so, dereference it (unless explicitly disabled)
				// Skip dereferencing if holds_address_only is true (AddressOf results)
				auto rhs_ref_it = reference_stack_info_.find(rhs_offset);
				if (rhs_ref_it != reference_stack_info_.end() && op.dereference_rhs_references && !rhs_ref_it->second.holds_address_only) {
					// RHS is a reference - load pointer and dereference
					X64Register ptr_reg = allocateRegisterWithSpilling();
					emitMovFromFrame(ptr_reg, rhs_offset);  // Load the pointer
					// Dereference to get the value
					int value_size_bytes = rhs_ref_it->second.value_size_bits / 8;
					emitMovFromMemory(ptr_reg, ptr_reg, 0, value_size_bytes);
					source_reg = ptr_reg;
				} else if (is_floating_point_type(rhs_type)) {
					source_reg = allocateXMMRegisterWithSpilling();
					bool is_float = (rhs_type == Type::Float);
					emitFloatMovFromFrame(source_reg, rhs_offset, is_float);
				} else {
					// Load from RHS stack location: source (sized stack slot) -> dest (64-bit register)
					emitMovFromFrameSized(
						SizedRegister{source_reg, 64, false},  // dest: 64-bit register
						SizedStackSlot{rhs_offset, op.rhs.size_in_bits, isSignedType(rhs_type)}  // source: sized stack slot
					);
				}
			}
		} else if (std::holds_alternative<TempVar>(op.rhs.value)) {
			TempVar rhs_var = std::get<TempVar>(op.rhs.value);
			int32_t rhs_offset = getStackOffsetFromTempVar(rhs_var);

			// Check if RHS is a reference - if so, dereference it
			auto rhs_ref_it = reference_stack_info_.find(rhs_offset);
			
			// If not found with TempVar offset, try looking up by name
			// This handles the case where TempVar offset differs from named variable offset
			if (rhs_ref_it == reference_stack_info_.end()) {
				std::string_view var_name = rhs_var.name();
				// Remove the '%' prefix if present
				if (!var_name.empty() && var_name[0] == '%') {
					var_name = var_name.substr(1);
				}
				// Only try to match if this looks like it could be a named variable
				// (not a pure temporary like "temp_10")
				if (!var_name.empty() && var_name.find("temp_") != 0) {
					auto named_var_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(var_name));
					if (named_var_it != variable_scopes.back().variables.end()) {
						int32_t named_offset = named_var_it->second.offset;
						rhs_ref_it = reference_stack_info_.find(named_offset);
						if (rhs_ref_it != reference_stack_info_.end()) {
							// Found it! Update rhs_offset to use the named variable offset
							rhs_offset = named_offset;
						}
					}
				}
			}
			
			if (rhs_ref_it != reference_stack_info_.end() && op.dereference_rhs_references && !rhs_ref_it->second.holds_address_only) {
				// RHS is a reference - load pointer and dereference
				X64Register ptr_reg = allocateRegisterWithSpilling();
				emitMovFromFrame(ptr_reg, rhs_offset);  // Load the pointer
				// Dereference to get the value
				int value_size_bytes = rhs_ref_it->second.value_size_bits / 8;
				emitMovFromMemory(ptr_reg, ptr_reg, 0, value_size_bytes);
				source_reg = ptr_reg;
			} else if (auto rhs_reg = regAlloc.tryGetStackVariableRegister(rhs_offset); rhs_reg.has_value()) {
				// Check if the value is already in a register
				source_reg = rhs_reg.value();
			} else {
				if (is_floating_point_type(rhs_type)) {
					source_reg = allocateXMMRegisterWithSpilling();
					bool is_float = (rhs_type == Type::Float);
					emitFloatMovFromFrame(source_reg, rhs_offset, is_float);
				} else {
					// Load from RHS stack location: source (sized stack slot) -> dest (64-bit register)
					emitMovFromFrameSized(
						SizedRegister{source_reg, 64, false},  // dest: 64-bit register
						SizedStackSlot{rhs_offset, op.rhs.size_in_bits, isSignedType(rhs_type)}  // source: sized stack slot
					);
				}
			}
		} else if (std::holds_alternative<unsigned long long>(op.rhs.value)) {
			// RHS is an immediate value
			unsigned long long rhs_value = std::get<unsigned long long>(op.rhs.value);
			// MOV RAX, imm64
			emitMovImm64(X64Register::RAX, rhs_value);
		} else if (std::holds_alternative<double>(op.rhs.value)) {
			// RHS is a floating-point immediate value
			double double_value = std::get<double>(op.rhs.value);
			// Allocate an XMM register and load the double into it
			source_reg = allocateXMMRegisterWithSpilling();
			// Convert double to uint64_t bit representation
			uint64_t bits;
			std::memcpy(&bits, &double_value, sizeof(bits));
			// Load bits into a general-purpose register first
			emitMovImm64(X64Register::RAX, bits);
			// Move from RAX to XMM register using movq instruction
			emitMovqGprToXmm(X64Register::RAX, source_reg);
		}
		
		// Store source register to LHS stack location
		// Check if LHS is a reference parameter that needs dereferencing
		auto ref_it = reference_stack_info_.find(lhs_offset);
		if (ref_it != reference_stack_info_.end()) {
			// LHS is a reference - need to dereference it before storing
			// First, load the pointer (reference address) into a temporary register
			X64Register ptr_reg = allocateRegisterWithSpilling();
			auto load_ptr = generatePtrMovFromFrame(ptr_reg, lhs_offset);
			textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(), load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
			
			// Now store the value to the address pointed to by ptr_reg
			int value_size_bits = ref_it->second.value_size_bits;
			int size_bytes = value_size_bits / 8;
			
			if (is_floating_point_type(rhs_type)) {
				// For floating-point, use SSE store instruction helper
				bool is_float = (rhs_type == Type::Float);
				auto store_inst = generateFloatMovToMemory(source_reg, ptr_reg, is_float);
				textSectionData.insert(textSectionData.end(), store_inst.op_codes.begin(), 
				                       store_inst.op_codes.begin() + store_inst.size_in_bytes);
			} else {
				// For integer types, use the existing emitStoreToMemory helper
				emitStoreToMemory(textSectionData, source_reg, ptr_reg, 0, size_bytes);
			}
			
			// Release the pointer register
			regAlloc.release(ptr_reg);
		} else {
			// Normal (non-reference) assignment - store directly to stack location
			if (is_floating_point_type(rhs_type)) {
				bool is_float = (rhs_type == Type::Float);
				emitFloatMovToFrame(source_reg, lhs_offset, is_float);
			} else {
				emitMovToFrameSized(
					SizedRegister{source_reg, 64, false},  // source: 64-bit register
					SizedStackSlot{lhs_offset, op.lhs.size_in_bits, isSignedType(lhs_type)}  // dest: sized stack slot
				);
				// Clear any stale register associations for this stack offset
				regAlloc.clearStackVariableAssociations(lhs_offset);
			}
		}
	}

	void handleLabel(const IrInstruction& instruction) {
		// Label instruction: mark a position in code for jumps
			assert(instruction.hasTypedPayload() && "Label instruction must use typed payload");
		const auto& label_op = instruction.getTypedPayload<LabelOp>();
		std::string_view label_name = StringTable::getStringView(label_op.getLabelName());  // Phase 4: Use helper

		// Store the current code offset for this label
		uint32_t label_offset = static_cast<uint32_t>(textSectionData.size());

		// Track label positions for later resolution
		std::string label_name_str(label_name);
		if (label_positions_.find(StringTable::getOrInternStringHandle(label_name_str)) == label_positions_.end()) {
			label_positions_[StringTable::getOrInternStringHandle(label_name_str)] = label_offset;
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
	assert(instruction.hasTypedPayload() && "Branch instruction must use typed payload");
	const auto& branch_op = instruction.getTypedPayload<BranchOp>();
	std::string_view target_label = StringTable::getStringView(branch_op.getTargetLabel());  // Phase 4: Use helper
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
		pending_branches_.push_back({StringTable::getOrInternStringHandle(target_label), patch_position});
	}

	void handleLoopBegin(const IrInstruction& instruction) {
		// LoopBegin marks the start of a loop and provides labels for break/continue
		assert(instruction.hasTypedPayload() && "LoopBegin must use typed payload");
		const auto& op = instruction.getTypedPayload<LoopBeginOp>();
		//StringHandle loop_start_label = op.loop_start_label;
		StringHandle loop_end_label = op.loop_end_label;
		StringHandle loop_increment_label = op.loop_increment_label;
		
		// Push loop context onto stack for break/continue handling
		loop_context_stack_.push_back({loop_end_label, 
		                                loop_increment_label});

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
		assert(instruction.hasTypedPayload() && "ArrayAccess without typed payload - should not happen");
		
		// Flush all registers to memory before array access
		// This ensures any previously computed values in registers are saved
		flushAllDirtyRegisters();
		
		const ArrayAccessOp& op = std::any_cast<const ArrayAccessOp&>(instruction.getTypedPayload());
			
		TempVar result_var = op.result;
		int element_size_bits = op.element_size_in_bits;
		int element_size_bytes = element_size_bits / 8;
		Type element_type = op.element_type;
		bool is_floating_point = (element_type == Type::Float || element_type == Type::Double);
		bool is_float = (element_type == Type::Float);
		bool is_struct = is_struct_type(element_type);
		
		// Phase 5 Optimization: Use value category metadata for LEA vs MOV decision
		// For struct types, always use LEA (original behavior)
		// For primitive lvalues, we could use LEA but need to handle dereferencing correctly
		// For now, only optimize struct types to avoid breaking existing code
		bool result_is_lvalue = isTempVarLValue(result_var);
		bool optimize_lea = is_struct;  // Conservative: only struct types for now
		
		FLASH_LOG_FORMAT(Codegen, Debug, 
			"ArrayAccess: is_struct={} is_lvalue={} optimize_lea={}",
			is_struct, result_is_lvalue, optimize_lea);
		
		// For floating-point, we'll use XMM0 for the loaded value
		// For integers and struct addresses, we allocate a general-purpose register
		X64Register base_reg = allocateRegisterWithSpilling();
		
		// Get the array base address (from stack or register)
		int64_t array_base_offset = 0;
		bool is_array_pointer = op.is_pointer_to_array;  // Use flag from codegen
		StringHandle array_name_handle;
		std::string_view array_name_view;
			
		if (std::holds_alternative<StringHandle>(op.array)) {
			array_name_handle = std::get<StringHandle>(op.array);
			array_name_view = StringTable::getStringView(array_name_handle);
		} else if (std::holds_alternative<TempVar>(op.array)) {
			TempVar array_temp_var = std::get<TempVar>(op.array);
			array_base_offset = getStackOffsetFromTempVar(array_temp_var);
			is_array_pointer = true;  // TempVar always means pointer
		}
			
		// Check if this is a member array access (object.member format)
		bool is_member_array = false;
		std::string_view object_name;
		std::string_view member_name;
		int64_t member_offset = op.member_offset;  // Get from payload
			
		// Check if the object (not the array) is a pointer (like 'this' or a reference)
		bool is_object_pointer = false;
		
		if (!array_name_view.empty()) {
			is_member_array = array_name_view.find('.') != std::string::npos;
			if (is_member_array) {
				// Parse object.member
				size_t dot_pos = array_name_view.find('.');
				object_name = array_name_view.substr(0, dot_pos);
				member_name = array_name_view.substr(dot_pos + 1);
				// Update array_base_offset to point to the object
				StringHandle object_name_handle = StringTable::getOrInternStringHandle(object_name);
				array_base_offset = variable_scopes.back().variables[object_name_handle].offset;
				
				// Check if object is 'this' or a reference parameter (both need pointer dereferencing)
				if (object_name == "this"sv || reference_stack_info_.count(array_base_offset) > 0) {
					is_object_pointer = true;
				}
			} else {
				// Regular array/pointer - get offset directly
				array_base_offset = variable_scopes.back().variables[array_name_handle].offset;
			}
		}
			
		// Get the result storage location
		int64_t result_offset = getStackOffsetFromTempVar(result_var);
			
		// Handle index value from TypedValue
		if (std::holds_alternative<unsigned long long>(op.index.value)) {
			// Constant index
			uint64_t index_value = std::get<unsigned long long>(op.index.value);

			if (is_array_pointer || is_object_pointer) {
				// Array is a pointer/temp var, or member array of a pointer object (like this.values[i])
				// Load pointer and compute address
				auto load_ptr_opcodes = generatePtrMovFromFrame(base_reg, array_base_offset);
				textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
					                    load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);

				// Add member offset + index offset to pointer
				// For is_object_pointer: total offset = member_offset + (index * element_size)
				// For is_array_pointer: total offset = index * element_size (member_offset is 0)
				int64_t offset_bytes = member_offset + (index_value * element_size_bytes);
				if (offset_bytes != 0) {
					emitAddImmToReg(textSectionData, base_reg, offset_bytes);
				}

				// Phase 5: Use optimize_lea for LEA vs MOV decision
				// For struct types or lvalues, keep the address in base_reg
				// For primitive prvalues, load the value
				if (!optimize_lea) {
					// Load value from [base_reg] with appropriate instruction
					if (is_floating_point) {
						emitFloatLoadFromAddressInReg(textSectionData, X64Register::XMM0, base_reg, is_float);
					} else {
						emitLoadFromAddressInReg(textSectionData, base_reg, base_reg, element_size_bytes);
					}
				}
			} else {
				// Array is a regular variable - use direct stack offset
				int64_t element_offset = array_base_offset + member_offset + (index_value * element_size_bytes);
					
				// Phase 5: Use optimize_lea for LEA vs MOV decision
				if (optimize_lea) {
					// For struct types or lvalues, compute the address using LEA
					emitLEAFromFrame(textSectionData, base_reg, element_offset);
				} else {
					// Load from [RBP + offset] with appropriate instruction
					if (is_floating_point) {
						emitFloatMovFromFrame(X64Register::XMM0, static_cast<int32_t>(element_offset), is_float);
					} else {
						emitMovFromFrameSized(
							SizedRegister{base_reg, 64, false},
							SizedStackSlot{static_cast<int32_t>(element_offset), element_size_bits, isSignedType(op.element_type)}
						);
					}
				}
			}
		} else if (std::holds_alternative<TempVar>(op.index.value)) {
			// Variable index - need to compute address at runtime
			TempVar index_var = std::get<TempVar>(op.index.value);
			int64_t index_var_offset = getStackOffsetFromTempVar(index_var);
			
			// Allocate a second register for the index, excluding base_reg to avoid conflicts
			X64Register index_reg = allocateRegisterWithSpilling(base_reg);
			FLASH_LOG_FORMAT(Codegen, Debug, "ArrayAccess TempVar: base_reg={}, index_reg={}, array_base_offset={}, index_var_offset={}",
				static_cast<int>(base_reg), static_cast<int>(index_reg), array_base_offset, index_var_offset);

			if (is_array_pointer || is_object_pointer) {
				// Array is a pointer/temp var, or member array of a pointer object (like this.values[i])
				auto load_ptr_opcodes = generatePtrMovFromFrame(base_reg, array_base_offset);
				textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
					                    load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);

				// Add member offset for pointer objects (e.g., this->member)
				if (is_object_pointer && member_offset != 0) {
					emitAddImmToReg(textSectionData, base_reg, member_offset);
				}

				// Load index with proper sign extension based on index type
				bool is_signed = isSignedType(op.index.type);
				emitMovFromFrameSized(
					SizedRegister{index_reg, 64, false},
					SizedStackSlot{static_cast<int32_t>(index_var_offset), op.index.size_in_bits, is_signed}
				);
				emitMultiplyRegByElementSize(textSectionData, index_reg, element_size_bytes);
				emitAddRegs(textSectionData, base_reg, index_reg);
				
				// Phase 5: Use optimize_lea for LEA vs MOV decision
				// For struct types or lvalues, keep the address in base_reg
				// For primitive prvalues, load the value
				if (!optimize_lea) {
					if (is_floating_point) {
						emitFloatLoadFromAddressInReg(textSectionData, X64Register::XMM0, base_reg, is_float);
					} else {
						emitLoadFromAddressInReg(textSectionData, base_reg, base_reg, element_size_bytes);
					}
				}
			} else {
				// Array is a regular variable
				// Load index with proper sign extension based on index type
				bool is_signed = isSignedType(op.index.type);
				emitMovFromFrameSized(
					SizedRegister{index_reg, 64, false},
					SizedStackSlot{static_cast<int32_t>(index_var_offset), op.index.size_in_bits, is_signed}
				);
				emitMultiplyRegByElementSize(textSectionData, index_reg, element_size_bytes);
					
				int64_t combined_offset = array_base_offset + member_offset;
				emitLEAFromFrame(textSectionData, base_reg, combined_offset);
				emitAddRegs(textSectionData, base_reg, index_reg);
				
				// Phase 5: Use optimize_lea for LEA vs MOV decision
				// For struct types or lvalues, keep the address in base_reg
				// For primitive prvalues, load the value
				if (!optimize_lea) {
					if (is_floating_point) {
						emitFloatLoadFromAddressInReg(textSectionData, X64Register::XMM0, base_reg, is_float);
					} else {
						emitLoadFromAddressInReg(textSectionData, base_reg, base_reg, element_size_bytes);
					}
				}
			}
			
			// Release the index register
			regAlloc.release(index_reg);
		} else if (std::holds_alternative<StringHandle>(op.index.value)) {
			// Variable index stored as identifier name
			StringHandle index_var_name_handle = std::get<StringHandle>(op.index.value);
			auto index_it = variable_scopes.back().variables.find(index_var_name_handle);
			assert(index_it != variable_scopes.back().variables.end() && "Index variable not found");
			int64_t index_var_offset = index_it->second.offset;
			
			// Allocate a second register for the index
			X64Register index_reg = allocateRegisterWithSpilling();

			if (is_array_pointer || is_object_pointer) {
				// Array is a pointer/temp var, or member array of a pointer object
				auto load_ptr_opcodes = generatePtrMovFromFrame(base_reg, array_base_offset);
				textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
					                    load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);
				
				// Add member offset for pointer objects (e.g., this->member)
				if (is_object_pointer && member_offset != 0) {
					emitAddImmToReg(textSectionData, base_reg, member_offset);
				}
			} else {
				int64_t combined_offset = array_base_offset + member_offset;
				emitLEAFromFrame(textSectionData, base_reg, combined_offset);
			}
			
			// Load index into index_reg with proper sign extension based on index type
			bool is_signed = isSignedType(op.index.type);
			emitMovFromFrameSized(
				SizedRegister{index_reg, 64, false},
				SizedStackSlot{static_cast<int32_t>(index_var_offset), op.index.size_in_bits, is_signed}
			);
			
			emitMultiplyRegByElementSize(textSectionData, index_reg, element_size_bytes);
			emitAddRegs(textSectionData, base_reg, index_reg);
			
			// Phase 5: Use optimize_lea for LEA vs MOV decision
			// For struct types or lvalues, keep the address in base_reg
			// For primitive prvalues, load the value
			if (!optimize_lea) {
				if (is_floating_point) {
					emitFloatLoadFromAddressInReg(textSectionData, X64Register::XMM0, base_reg, is_float);
				} else {
					emitLoadFromAddressInReg(textSectionData, base_reg, base_reg, element_size_bytes);
				}
			}
			
			// Release the index register
			regAlloc.release(index_reg);
		}
			
		// Store result in temp variable's stack location
		if (is_floating_point) {
			emitFloatMovToFrame(X64Register::XMM0, static_cast<int32_t>(result_offset), is_float);
		} else {
			emitMovToFrameSized(
				SizedRegister{base_reg, 64, false},  // source: 64-bit register
				SizedStackSlot{static_cast<int32_t>(result_offset), 64, false}  // dest: 64-bit
			);
		}
		
		// Phase 5: Mark the result temp var as holding a pointer/reference when using LEA
		// This allows subsequent operations to properly handle the address
		if (optimize_lea) {
			setReferenceInfo(result_offset, element_type, element_size_bits, false, result_var);
		}
		
		// Release the base register
		regAlloc.release(base_reg);
	}


	void handleArrayElementAddress(const IrInstruction& instruction) {
		// Flush dirty registers to ensure index values are in memory
		flushAllDirtyRegisters();
		
		// Try typed payload first
		if (instruction.hasTypedPayload()) {
			const ArrayElementAddressOp& op = std::any_cast<const ArrayElementAddressOp&>(instruction.getTypedPayload());
			
			TempVar result_var = op.result;
			int element_size_bits = op.element_size_in_bits;
			int element_size_bytes = element_size_bits / 8;
			
			// Get the array base address
			int64_t array_base_offset = 0;
			if (std::holds_alternative<StringHandle>(op.array)) {
				StringHandle array_name_handle = std::get<StringHandle>(op.array);
			[[maybe_unused]] std::string_view array_name = StringTable::getStringView(array_name_handle);
				array_base_offset = variable_scopes.back().variables[array_name_handle].offset;
			} else if (std::holds_alternative<TempVar>(op.array)) {
				TempVar array_temp = std::get<TempVar>(op.array);
				array_base_offset = getStackOffsetFromTempVar(array_temp);
			}
			
			// Get result storage location
			int64_t result_offset = getStackOffsetFromTempVar(result_var);
			
			// Handle constant or variable index
			if (std::holds_alternative<unsigned long long>(op.index.value)) {
				uint64_t index_value = std::get<unsigned long long>(op.index.value);
				int64_t element_offset = array_base_offset + (index_value * element_size_bytes);
				
				// LEA RAX, [RBP + element_offset]
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0x8D); // LEA r64, m
				
				if (element_offset >= -128 && element_offset <= 127) {
					textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
					textSectionData.push_back(static_cast<uint8_t>(element_offset));
				} else {
					textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
					uint32_t offset_u32 = static_cast<uint32_t>(static_cast<int32_t>(element_offset));
					textSectionData.push_back(offset_u32 & 0xFF);
					textSectionData.push_back((offset_u32 >> 8) & 0xFF);
					textSectionData.push_back((offset_u32 >> 16) & 0xFF);
					textSectionData.push_back((offset_u32 >> 24) & 0xFF);
				}
			} else if (std::holds_alternative<TempVar>(op.index.value)) {
				TempVar index_var = std::get<TempVar>(op.index.value);
				int64_t index_offset = getStackOffsetFromTempVar(index_var);
				
				// Load index: source (sized stack slot) -> dest (64-bit RCX)
				emitMovFromFrameSized(
					SizedRegister{X64Register::RCX, 64, false},  // dest: 64-bit register
					SizedStackSlot{static_cast<int32_t>(index_offset), op.index.size_in_bits, isSignedType(op.index.type)}  // source: index from stack
				);
				
				// Multiply index by element size
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);
				
				// Load address of array base into RAX
				emitLeaFromFrame(X64Register::RAX, array_base_offset);
				
				// Add offset to get final address
				emitAddRAXRCX(textSectionData);
			} else if (std::holds_alternative<StringHandle>(op.index.value)) {
				// Handle variable name (StringHandle) as index
				StringHandle index_var_name = std::get<StringHandle>(op.index.value);
				auto it = variable_scopes.back().variables.find(index_var_name);
				if (it == variable_scopes.back().variables.end()) {
					assert(false && "Index variable not found in scope");
					return;
				}
				int64_t index_offset = it->second.offset;
				
				// Load index: source (sized stack slot) -> dest (64-bit RCX)
				emitMovFromFrameSized(
					SizedRegister{X64Register::RCX, 64, false},  // dest: 64-bit register
					SizedStackSlot{static_cast<int32_t>(index_offset), op.index.size_in_bits, isSignedType(op.index.type)}  // source: index from stack
				);
				
				// Multiply index by element size
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);
				
				// Load address of array base into RAX
				emitLeaFromFrame(X64Register::RAX, array_base_offset);
				
				// Add offset to get final address
				emitAddRAXRCX(textSectionData);
			}
			
			// Store the computed address to result_var
			auto store_opcodes = generatePtrMovToFrame(X64Register::RAX, result_offset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
			                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
			return;
		}
		
		// Legacy operand-based format
		// All array element address now uses typed payload - no legacy code path
		assert(false && "ArrayElementAddress without typed payload - should not happen");
	}


	void handleArrayStore(const IrInstruction& instruction) {
		// Ensure all computed values (especially indices from expressions) are spilled to stack
		// before we load them. This is necessary because variable indices (TempVars) may still
		// be in registers and not yet written to their stack locations.
		flushAllDirtyRegisters();
		
		// Try typed payload first
		if (instruction.hasTypedPayload()) {
			const ArrayStoreOp& op = std::any_cast<const ArrayStoreOp&>(instruction.getTypedPayload());
			
			int element_size_bits = op.element_size_in_bits;
			int element_size_bytes = element_size_bits / 8;
			bool is_pointer_to_array = op.is_pointer_to_array;
			
			// Get the array base address
			StringHandle array_name_handle;
			std::string_view array_name_view;
			int64_t array_base_offset = 0;
			bool array_is_tempvar = false;
			
			if (std::holds_alternative<StringHandle>(op.array)) {
				array_name_handle = std::get<StringHandle>(op.array);
				array_name_view = StringTable::getStringView(array_name_handle);
			} else if (std::holds_alternative<TempVar>(op.array)) {
				// Array is a TempVar (e.g., from member_access for struct.array_member)
				// The TempVar holds a pointer to the array base
				TempVar array_temp = std::get<TempVar>(op.array);
				array_base_offset = getStackOffsetFromTempVar(array_temp);
				array_is_tempvar = true;
			}
			
			// Check if this is a member array access (object.member format)
			bool is_member_array = array_name_view.find('.') != std::string::npos;
			std::string_view object_name;
			std::string_view member_name;
			int64_t member_offset = op.member_offset;  // Get from payload
			
			if (is_member_array) {
				// Parse object.member
				size_t dot_pos = array_name_view.find('.');
				object_name = array_name_view.substr(0, dot_pos);
				member_name = array_name_view.substr(dot_pos + 1);
			}
			
			// Get the value to store into RDX or XMM0 (we use RCX for index, RAX for address)
			bool is_float_store = is_floating_point_type(op.element_type);
			
			if (std::holds_alternative<unsigned long long>(op.value.value)) {
				// Constant value
				uint64_t value = std::get<unsigned long long>(op.value.value);
				if (is_float_store) {
					// For float constants, we need to load into XMM0
					// First load the bit pattern into RDX, then move to XMM0
					emitMovImm64(X64Register::RDX, value);
					// MOVD XMM0, RDX (0x66 0x48 0x0F 0x6E 0xC2)
					textSectionData.push_back(0x66);
					textSectionData.push_back(0x48);
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0x6E);
					textSectionData.push_back(0xC2);
				} else {
					emitMovImm64(X64Register::RDX, value);
				}
			} else if (std::holds_alternative<TempVar>(op.value.value)) {
				// Value from temp var: check if already in register, otherwise load from stack
				TempVar value_var = std::get<TempVar>(op.value.value);
				int64_t value_offset = getStackOffsetFromTempVar(value_var, op.value.size_in_bits);
				
				if (is_float_store) {
					// For floats, check if already in XMM register, otherwise load from stack
					if (auto value_reg = regAlloc.tryGetStackVariableRegister(value_offset); value_reg.has_value()) {
						// Value is already in a register
						// If it's an XMM register and not XMM0, move it
						if (value_reg.value() != X64Register::XMM0) {
							// MOVSS XMM0, source_xmm
							bool is_double = (op.value.size_in_bits == 64);
							if (is_double) {
								textSectionData.push_back(0xF2);  // MOVSD prefix
							} else {
								textSectionData.push_back(0xF3);  // MOVSS prefix
							}
							textSectionData.push_back(0x0F);
							textSectionData.push_back(0x10);
							// ModR/M for XMM0 <- source_xmm
							uint8_t src_xmm_num = static_cast<uint8_t>(value_reg.value()) - static_cast<uint8_t>(X64Register::XMM0);
							textSectionData.push_back(0xC0 | src_xmm_num);
						}
					} else {
						// Load float from stack into XMM0
						bool is_double = (op.value.size_in_bits == 64);
						emitFloatMovFromFrame(X64Register::XMM0, value_offset, !is_double);
					}
				} else {
					// Integer/pointer value
					// Check if value is already in a register
					if (auto value_reg = regAlloc.tryGetStackVariableRegister(value_offset); value_reg.has_value()) {
						// Value is already in a register - move it to RDX if not already there
						if (value_reg.value() != X64Register::RDX) {
							auto move_op = regAlloc.get_reg_reg_move_op_code(X64Register::RDX, value_reg.value(), op.value.size_in_bits / 8);
							textSectionData.insert(textSectionData.end(), move_op.op_codes.begin(), move_op.op_codes.begin() + move_op.size_in_bytes);
						}
						// If already in RDX, no move needed
					} else {
						// Not in register - load from stack
						emitMovFromFrameSized(
							SizedRegister{X64Register::RDX, 64, false},  // dest: 64-bit register
							SizedStackSlot{static_cast<int32_t>(value_offset), op.value.size_in_bits, isSignedType(op.value.type)}  // source: value from stack
						);
					}
				}
			}
			
			// Get array base offset (only needed if array is StringHandle, not TempVar)
			if (!array_is_tempvar) {
				StringHandle lookup_name_handle = is_member_array ? StringTable::getOrInternStringHandle(object_name) : array_name_handle;
				array_base_offset = variable_scopes.back().variables[lookup_name_handle].offset;
				// Fallback: if not found (offset == INT_MIN), try matching by string to tolerate handle mismatches
				if (array_base_offset == INT_MIN) {
					for (const auto& [handle, info] : variable_scopes.back().variables) {
						if (StringTable::getStringView(handle) == (is_member_array ? object_name : array_name_view)) {
							array_base_offset = info.offset;
							break;
						}
					}
				}
			}
			
			// Check if the object (not the array) is a pointer (like 'this' or a reference)
			bool is_object_pointer = false;
			if (is_member_array) {
				// Check if object is 'this' or a reference parameter
				if (object_name == "this"sv || reference_stack_info_.count(array_base_offset) > 0) {
					is_object_pointer = true;
				}
			}
			
			// When array is from a TempVar (member_access result), it holds a pointer to the array
			// We need to treat it like is_pointer_to_array case
			if (array_is_tempvar) {
				is_pointer_to_array = true;
			}
			
			FLASH_LOG_FORMAT(Codegen, Debug,
				"ArrayStore: is_member_array={}, object_name='{}', is_object_pointer={}, is_pointer_to_array={}, array_is_tempvar={}, array_base_offset={}, member_offset={}",
				is_member_array, (is_member_array ? object_name : "N/A"), is_object_pointer, is_pointer_to_array, array_is_tempvar, array_base_offset, member_offset);
			
			// Handle constant vs variable index
			if (std::holds_alternative<unsigned long long>(op.index.value)) {
				// Constant index
				uint64_t index_value = std::get<unsigned long long>(op.index.value);
				
				if (is_pointer_to_array) {
					// Load the pointer value first
					emitPtrMovFromFrame(X64Register::RAX, array_base_offset);
					
					// Add offset to pointer: ADD RAX, (index * element_size)
					int64_t offset_bytes = index_value * element_size_bytes;
					emitAddImmToReg(textSectionData, X64Register::RAX, offset_bytes);
					
					// Store to [RAX] with appropriate size
					if (is_float_store) {
						// MOVSS/MOVSD [RAX], XMM0
						bool is_double = (element_size_bits == 64);
						if (is_double) {
							textSectionData.push_back(0xF2);  // MOVSD prefix
						} else {
							textSectionData.push_back(0xF3);  // MOVSS prefix
						}
						textSectionData.push_back(0x0F);
						textSectionData.push_back(0x11);  // Store opcode
						textSectionData.push_back(0x00);  // ModR/M: [RAX]
					} else {
						emitStoreToMemory(textSectionData, X64Register::RDX, X64Register::RAX, 0, element_size_bytes);
					}
				} else if (is_object_pointer) {
					// Member array of a pointer object (like this.values[i])
					// Load the object pointer first
					emitPtrMovFromFrame(X64Register::RAX, array_base_offset);
					
					// Add member offset + index offset: ADD RAX, (member_offset + index * element_size)
					int64_t total_offset = member_offset + (index_value * element_size_bytes);
					
					FLASH_LOG_FORMAT(Codegen, Debug,
						"ArrayStore (const index): object_pointer path, base_offset={}, member_offset={}, index={}, elem_size={}, total_offset={}",
						array_base_offset, member_offset, index_value, element_size_bytes, total_offset);
					
					emitAddImmToReg(textSectionData, X64Register::RAX, total_offset);
					
					// Store to [RAX] with appropriate size
					if (is_float_store) {
						// MOVSS/MOVSD [RAX], XMM0
						bool is_double = (element_size_bits == 64);
						if (is_double) {
							textSectionData.push_back(0xF2);  // MOVSD prefix
						} else {
							textSectionData.push_back(0xF3);  // MOVSS prefix
						}
						textSectionData.push_back(0x0F);
						textSectionData.push_back(0x11);  // Store opcode
						textSectionData.push_back(0x00);  // ModR/M: [RAX]
					} else {
						emitStoreToMemory(textSectionData, X64Register::RDX, X64Register::RAX, 0, element_size_bytes);
					}
				} else {
					// Regular array - direct stack access
					int64_t element_offset = array_base_offset + member_offset + (index_value * element_size_bytes);
					
					// Store RDX to [RBP + offset] with appropriate size
					emitStoreToFrame(textSectionData, X64Register::RDX, element_offset, element_size_bytes);
				}
			} else if (std::holds_alternative<TempVar>(op.index.value)) {
				// Variable index - compute address at runtime
				TempVar index_var = std::get<TempVar>(op.index.value);
				int64_t index_var_offset = getStackOffsetFromTempVar(index_var, op.index.size_in_bits);
				
				// Load index into RCX (value is already in RDX)
				emitLoadIndexIntoRCX(textSectionData, index_var_offset, op.index.size_in_bits);
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);
				
				if (is_pointer_to_array) {
					// Load pointer into RAX
					emitPtrMovFromFrame(X64Register::RAX, array_base_offset);
					// RAX += RCX (add index offset to pointer)
					emitAddRAXRCX(textSectionData);
				} else if (is_object_pointer) {
					// Member array of a pointer object (like this.values[i])
					// Load the object pointer first
					emitPtrMovFromFrame(X64Register::RAX, array_base_offset);
					// Add member offset: ADD RAX, member_offset
					if (member_offset != 0) {
						FLASH_LOG_FORMAT(Codegen, Debug,
							"ArrayStore (var index): object_pointer path, base_offset={}, member_offset={}, elem_size={}",
							array_base_offset, member_offset, element_size_bytes);
						emitAddImmToReg(textSectionData, X64Register::RAX, member_offset);
					}
					// RAX += RCX (add index offset)
					emitAddRAXRCX(textSectionData);
				} else {
					// LEA RAX, [RBP + array_base_offset]
					int64_t combined_offset = array_base_offset + member_offset;
					emitLEAFromFrame(textSectionData, X64Register::RAX, combined_offset);
					// RAX += RCX
					emitAddRAXRCX(textSectionData);
				}
				
				// Store to [RAX]
				if (is_float_store) {
					// MOVSS/MOVSD [RAX], XMM0
					bool is_double = (element_size_bits == 64);
					if (is_double) {
						textSectionData.push_back(0xF2);  // MOVSD prefix
					} else {
						textSectionData.push_back(0xF3);  // MOVSS prefix
					}
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0x11);  // Store opcode
					textSectionData.push_back(0x00);  // ModR/M: [RAX]
				} else {
					emitStoreToMemory(textSectionData, X64Register::RDX, X64Register::RAX, 0, element_size_bytes);
				}
			} else if (std::holds_alternative<StringHandle>(op.index.value)) {
				// Index is a named variable - get its stack offset
				StringHandle index_handle = std::get<StringHandle>(op.index.value);
				auto it = variable_scopes.back().variables.find(index_handle);
				if (it == variable_scopes.back().variables.end()) {
					assert(false && "Index variable not found in scope");
					return;
				}
				int64_t index_var_offset = it->second.offset;
				int index_size_in_bits = it->second.size_in_bits;
				
				// Load index into RCX (value is already in RDX)
				emitLoadIndexIntoRCX(textSectionData, index_var_offset, index_size_in_bits);
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);
				
				if (is_pointer_to_array) {
					// Load pointer into RAX
					emitPtrMovFromFrame(X64Register::RAX, array_base_offset);
					// RAX += RCX (add index offset to pointer)
					emitAddRAXRCX(textSectionData);
				} else if (is_object_pointer) {
					// Member array of a pointer object (like this.values[i])
					// Load the object pointer first
					emitPtrMovFromFrame(X64Register::RAX, array_base_offset);
					// Add member offset: ADD RAX, member_offset
					if (member_offset != 0) {
						emitAddImmToReg(textSectionData, X64Register::RAX, member_offset);
					}
					// RAX += RCX (add index offset)
					emitAddRAXRCX(textSectionData);
				} else {
					// LEA RAX, [RBP + array_base_offset]
					int64_t combined_offset = array_base_offset + member_offset;
					emitLEAFromFrame(textSectionData, X64Register::RAX, combined_offset);
					// RAX += RCX
					emitAddRAXRCX(textSectionData);
				}
				
				// Store to [RAX]
				if (is_float_store) {
					// MOVSS/MOVSD [RAX], XMM0
					bool is_double = (element_size_bits == 64);
					if (is_double) {
						textSectionData.push_back(0xF2);  // MOVSD prefix
					} else {
						textSectionData.push_back(0xF3);  // MOVSS prefix
					}
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0x11);  // Store opcode
					textSectionData.push_back(0x00);  // ModR/M: [RAX]
				} else {
					emitStoreToMemory(textSectionData, X64Register::RDX, X64Register::RAX, 0, element_size_bytes);
				}
			} else {
				assert(false && "ArrayStore index must be constant, TempVar, or StringHandle");
			}
			return;
		}
		
		// All array store now uses typed payload - no legacy code path
		assert(false && "ArrayStore without typed payload - should not happen");
	}


	void handleStringLiteral(const IrInstruction& instruction) {
		const StringLiteralOp& op = instruction.getTypedPayload<StringLiteralOp>();
		TempVar result_var = std::get<TempVar>(op.result);

		// Add string literal to .rdata and get symbol
		std::string_view symbol_name = writer.add_string_literal(op.content);
		int64_t stack_offset = getStackOffsetFromTempVar(result_var);
		variable_scopes.back().variables[StringTable::getOrInternStringHandle(result_var.name())].offset = stack_offset;

		// LEA RAX, [RIP + symbol] with relocation
		uint32_t reloc_offset = emitLeaRipRelative(X64Register::RAX);
		writer.add_relocation(reloc_offset, symbol_name);

		// Store address to stack
		emitMovToFrame(X64Register::RAX, stack_offset);
	}

	void handleMemberAccess(const IrInstruction& instruction) {
		// MemberAccess: %result = member_access [MemberType][MemberSize] %object, member_name, offset
		
		// Extract typed payload - all MemberAccess instructions use typed payloads
		const MemberLoadOp& op = std::any_cast<const MemberLoadOp&>(instruction.getTypedPayload());

		// Get the object's base stack offset or pointer
		int32_t object_base_offset = 0;
		bool is_pointer_access = false;  // true if object is 'this' or a reference parameter (both are pointers)
		bool is_global_access = false;   // true if object is a global variable
		StringHandle global_object_name;  // name for global variable access
		const StackVariableScope& current_scope = variable_scopes.back();

		// Get object base offset
		if (std::holds_alternative<StringHandle>(op.object)) {
			StringHandle object_name_handle = std::get<StringHandle>(op.object);
			auto it = current_scope.variables.find(object_name_handle);
			if (it == current_scope.variables.end()) {
				// Not found in local scope - check if it's a global variable
				bool found_global = false;
				for (const auto& global : global_variables_) {
					if (global.name == object_name_handle) {
						found_global = true;
						is_global_access = true;
						global_object_name = global.name;
						break;
					}
				}
				if (!found_global) {
					FLASH_LOG(Codegen, Error, "MemberAccess missing object: ", StringTable::getStringView(object_name_handle), "\n");
					assert(false && "Struct object not found in scope or globals");
					return;
				}
			} else {
				object_base_offset = it->second.offset;

				// Check if this is the 'this' pointer or a reference parameter (both need dereferencing)
				bool is_this = (StringTable::getStringView(object_name_handle) == "this"sv);
				bool in_ref_stack_info = (reference_stack_info_.count(object_base_offset) > 0);
				FLASH_LOG(Codegen, Debug, "MemberAccess check: object='", StringTable::getStringView(object_name_handle),
				          "' offset=", object_base_offset,
				          " is_this=", is_this,
				          " in_ref_stack_info=", in_ref_stack_info,
				          " is_pointer_to_member=", op.is_pointer_to_member);
				if (is_this || in_ref_stack_info || op.is_pointer_to_member) {
					is_pointer_access = true;
				}
			}
		} else {
			// Nested case: object is the result of a previous member access
			auto object_temp = std::get<TempVar>(op.object);
			object_base_offset = getStackOffsetFromTempVar(object_temp);
			
			// Check if this temp var holds a pointer/address (from large member access) or is pointer-to-member
			if (reference_stack_info_.count(object_base_offset) > 0 || op.is_pointer_to_member) {
				is_pointer_access = true;
			}
		}

		// Calculate the member's actual stack offset
		int32_t member_stack_offset;
		if (is_pointer_access) {
			member_stack_offset = 0;  // Not used for pointer access
		} else {
			// For a struct at [RBP - 8] with member at offset 4: member is at [RBP - 8 + 4] = [RBP - 4]
			member_stack_offset = object_base_offset + op.offset;
		}

		// Calculate member size in bytes
		int member_size_bytes = op.result.size_in_bits / 8;

		// Flush all dirty registers to ensure values are saved before allocating
		flushAllDirtyRegisters();

		// Get the result variable's stack offset (needed for both paths)
		auto result_var = std::get<TempVar>(op.result.value);
		int32_t result_offset;
		StringHandle result_var_handle = StringTable::getOrInternStringHandle(result_var.name());
		auto it = current_scope.variables.find(result_var_handle);
		if (it != current_scope.variables.end() && it->second.offset != INT_MIN) {
			result_offset = it->second.offset;
		} else {
			// Allocate stack space for the result TempVar (or if offset is sentinel INT_MIN)
			result_offset = allocateStackSlotForTempVar(result_var.var_number);
			// Note: allocateStackSlotForTempVar already updates the variables map
		}

		// For large members (> 8 bytes), we can't load the value into a register
		// Instead, we compute and store the ADDRESS for later nested member access
		if (member_size_bytes > 8) {
			// Allocate a register to compute the address
			X64Register addr_reg = allocateRegisterWithSpilling();
			
			if (is_global_access) {
				// LEA addr_reg, [RIP + global_name]
				// REX.W + 8D /r for LEA r64, m
				uint8_t rex = 0x48; // REX.W
				if (static_cast<uint8_t>(addr_reg) >= 8) {
					rex |= 0x04; // REX.R
				}
				textSectionData.push_back(rex);
				textSectionData.push_back(0x8D);  // LEA opcode
				
				// ModR/M: mod=00 (RIP-relative), reg=addr_reg, r/m=101 (RIP+disp32)
				uint8_t modrm = 0x05 | ((static_cast<uint8_t>(addr_reg) & 0x7) << 3);
				textSectionData.push_back(modrm);
				
				// Placeholder for relocation (disp32)
				uint32_t reloc_offset = static_cast<uint32_t>(textSectionData.size());
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				pending_global_relocations_.push_back({reloc_offset, global_object_name, IMAGE_REL_AMD64_REL32});
				
				// If offset != 0, add it to addr_reg
				if (op.offset != 0) {
					emitAddRegImm32(textSectionData, addr_reg, op.offset);
				}
			} else if (is_pointer_access) {
				// Load pointer into addr_reg, then add offset if needed
				auto load_ptr = generatePtrMovFromFrame(addr_reg, object_base_offset);
				textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(),
				                       load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
				if (op.offset != 0) {
					emitAddRegImm32(textSectionData, addr_reg, op.offset);
				}
			} else {
				// LEA addr_reg, [RBP + member_stack_offset]
				int32_t effective_offset = object_base_offset + op.offset;
				auto lea_opcodes = generateLeaFromFrame(addr_reg, effective_offset);
				textSectionData.insert(textSectionData.end(), lea_opcodes.op_codes.begin(),
				                       lea_opcodes.op_codes.begin() + lea_opcodes.size_in_bytes);
			}
			
			// Store the address to result_offset
			auto store_addr = generatePtrMovToFrame(addr_reg, result_offset);
			textSectionData.insert(textSectionData.end(), store_addr.op_codes.begin(),
			                       store_addr.op_codes.begin() + store_addr.size_in_bytes);
			regAlloc.release(addr_reg);
			
			// Mark this temp var as containing a pointer/address
			setReferenceInfo(result_offset, op.result.type, op.result.size_in_bits, false, result_var);
			return;
		}

		// Allocate a register for loading the member value
		X64Register temp_reg = allocateRegisterWithSpilling();

		if (is_global_access) {
			// LEA temp_reg, [RIP + global] with relocation
			uint32_t reloc_offset = emitLeaRipRelative(temp_reg);
			pending_global_relocations_.push_back({reloc_offset, global_object_name, IMAGE_REL_AMD64_REL32});
			
			// Load member from [temp_reg + offset]
			bool is_float_type = (op.result.type == Type::Float || op.result.type == Type::Double);
			
			if (is_float_type) {
				// For floating-point: load into XMM and store to stack
				X64Register xmm_reg = X64Register::XMM0;
				bool is_float = (op.result.type == Type::Float);
				emitFloatLoadFromAddressWithOffset(textSectionData, xmm_reg, temp_reg, op.offset, is_float);
				
				int32_t float_result_offset = allocateStackSlotForTempVar(result_var.var_number);
				auto store_opcodes = generateFloatMovToFrame(xmm_reg, float_result_offset, is_float);
				textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
				                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
				regAlloc.release(temp_reg);
				variable_scopes.back().variables[StringTable::getOrInternStringHandle(result_var.name())].offset = float_result_offset;
				return;
			} else {
				// For integers: use standard integer load
				OpCodeWithSize load_opcodes;
				if (member_size_bytes == 8) {
					load_opcodes = generateMovFromMemory(temp_reg, temp_reg, op.offset);
				} else if (member_size_bytes == 4) {
					load_opcodes = generateMovFromMemory32(temp_reg, temp_reg, op.offset);
				} else if (member_size_bytes == 2) {
					load_opcodes = generateMovFromMemory16(temp_reg, temp_reg, op.offset);
				} else if (member_size_bytes == 1) {
					load_opcodes = generateMovFromMemory8(temp_reg, temp_reg, op.offset);
				} else {
					assert(false && "Unsupported member size");
					return;
				}
				textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
				                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				
				// Store loaded value to result_offset for later use (e.g., indirect_call)
				emitMovToFrame(temp_reg, result_offset);
				regAlloc.release(temp_reg);
				variable_scopes.back().variables[result_var_handle].offset = result_offset;
				return;
			}
		} else if (is_pointer_access) {
			// Load pointer into an allocated register, then load from [ptr_reg + offset]
			FLASH_LOG_FORMAT(Codegen, Debug,
				"MemberAccess pointer path: object_base_offset={}, op.offset={}, member_size_bytes={}",
				object_base_offset, op.offset, member_size_bytes);
			X64Register ptr_reg = allocateRegisterWithSpilling();
			emitMovFromFrame(ptr_reg, object_base_offset);
			
			// Load from [ptr_reg + offset] into temp_reg
			OpCodeWithSize load_opcodes;
			if (member_size_bytes == 8) {
				load_opcodes = generateMovFromMemory(temp_reg, ptr_reg, op.offset);
			} else if (member_size_bytes == 4) {
				load_opcodes = generateMovFromMemory32(temp_reg, ptr_reg, op.offset);
			} else if (member_size_bytes == 2) {
				load_opcodes = generateMovFromMemory16(temp_reg, ptr_reg, op.offset);
			} else if (member_size_bytes == 1) {
				load_opcodes = generateMovFromMemory8(temp_reg, ptr_reg, op.offset);
			} else {
				assert(false && "Unsupported member size");
				regAlloc.release(ptr_reg);
				return;
			}
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
			
			// Release pointer register - no longer needed
			regAlloc.release(ptr_reg);
			
			// Store loaded value to result_offset for later use (e.g., indirect_call)
			emitMovToFrame(temp_reg, result_offset);
			regAlloc.release(temp_reg);
			variable_scopes.back().variables[result_var_handle].offset = result_offset;
			return;
		} else {
			// For regular struct variables on the stack, load from computed offset
			emitLoadFromFrame(textSectionData, temp_reg, member_stack_offset, member_size_bytes);
		}

		if (op.is_reference) {
			emitMovToFrame(temp_reg, result_offset);
			regAlloc.release(temp_reg);
			setReferenceInfo(result_offset, op.result.type, op.result.size_in_bits, op.is_rvalue_reference, result_var);
			return;
		}

		// Store the loaded value into the temp slot so subsequent uses read the value,
		// avoiding aliasing the TempVar to the struct member location.
		emitMovToFrame(temp_reg, result_offset);
		regAlloc.release(temp_reg);
		variable_scopes.back().variables[result_var_handle].offset = result_offset;
		return;
	}

	void handleMemberStore(const IrInstruction& instruction) {
		// MemberStore: member_store [MemberType][MemberSize] %object, member_name, offset, %value
		
		// Extract typed payload - all MemberStore instructions use typed payloads
		const MemberStoreOp& op = std::any_cast<const MemberStoreOp&>(instruction.getTypedPayload());

		// Check if this is a vtable pointer initialization (vptr)
		if (op.vtable_symbol.isValid()) {
			// This is a vptr initialization - load vtable address and store to offset 0
			// Get the object's base stack offset
			int32_t object_base_offset = 0;
			const StackVariableScope& current_scope = variable_scopes.back();
			
			if (std::holds_alternative<StringHandle>(op.object)) {
				StringHandle object_name_handle = std::get<StringHandle>(op.object);
				auto it = current_scope.variables.find(object_name_handle);
				if (it == current_scope.variables.end()) {
					assert(false && "Struct object not found in scope");
					return;
				}
				object_base_offset = it->second.offset;
			}
			
			// Load vtable address using LEA with relocation
			// The vtable symbol (_ZTV...) already points to the function pointer array
			// (the ElfFileWriter's add_vtable creates the symbol at offset +16 past the RTTI header)
			// So we just need a standard PC-relative relocation with the default addend
			uint32_t relocation_offset = emitLeaRipRelative(X64Register::RAX);
			
			// Add a relocation for the vtable symbol
			writer.add_relocation(relocation_offset, StringTable::getStringView(op.vtable_symbol));
			
			// Store vtable pointer to [RCX + 0] (this pointer is in RCX, vptr is at offset 0)
			// First load 'this' pointer into RCX
			emitMovFromFrame(X64Register::RCX, object_base_offset);
			
			// Store RAX (vtable address) to [RCX + 0]
			emitStoreToMemory(textSectionData, X64Register::RAX, X64Register::RCX, 0, 8);
			
			return;  // Done with vptr initialization
		}

		// Now process the MemberStoreOp
		// Get the value - it could be a TempVar, a literal (unsigned long long, double), or a string_view (variable name)
		bool is_literal = false;
		int64_t literal_value = 0;
		double literal_double_value = 0.0;
		bool is_double_literal = false;
		bool is_variable = false;
		StringHandle variable_name;

		if (std::holds_alternative<TempVar>(op.value.value)) {
			// TempVar - handled below
		} else if (std::holds_alternative<unsigned long long>(op.value.value)) {
			is_literal = true;
			literal_value = static_cast<int64_t>(std::get<unsigned long long>(op.value.value));
		} else if (std::holds_alternative<double>(op.value.value)) {
			is_literal = true;
			is_double_literal = true;
			literal_double_value = std::get<double>(op.value.value);
		} else if (std::holds_alternative<StringHandle>(op.value.value)) {
			is_variable = true;
			variable_name = std::get<StringHandle>(op.value.value);
		} else {
			assert(false && "Value must be TempVar, unsigned long long, double, or StringHandle");
			return;
		}

		// Get the object's base stack offset or pointer
		int32_t object_base_offset = 0;
		bool is_pointer_access = false;  // true if object is 'this' (a pointer)
		const StackVariableScope& current_scope = variable_scopes.back();

		if (std::holds_alternative<StringHandle>(op.object)) {
			StringHandle object_name_handle = std::get<StringHandle>(op.object);
			
			// First check if this is a global variable
			bool is_global_variable = false;
			for (const auto& global : global_variables_) {
				if (global.name == object_name_handle) {
					is_global_variable = true;
					break;
				}
			}
			
			if (is_global_variable) {
				// Handle global struct member assignment using RIP-relative addressing
				// Load the value into a register first
				X64Register value_reg = allocateRegisterWithSpilling();
				
				if (is_literal) {
					if (is_double_literal) {
						uint64_t bits;
						std::memcpy(&bits, &literal_double_value, sizeof(bits));
						emitMovImm64(value_reg, bits);
					} else {
						uint64_t imm64 = static_cast<uint64_t>(literal_value);
						emitMovImm64(value_reg, imm64);
					}
				} else if (is_variable) {
					auto it = current_scope.variables.find(variable_name);
					if (it == current_scope.variables.end()) {
						assert(false && "Variable not found in scope");
						return;
					}
					int32_t value_offset = it->second.offset;
					emitMovFromFrameBySize(value_reg, value_offset, op.value.size_in_bits);
				} else {
					auto value_var = std::get<TempVar>(op.value.value);
					int32_t value_offset = getStackOffsetFromTempVar(value_var);
					emitMovFromFrameBySize(value_reg, value_offset, op.value.size_in_bits);
				}
				
				// Now store to the global struct member using RIP-relative addressing with offset
				// For doubles: MOVSD [RIP + disp32 + offset], XMM
				// For integers: MOV [RIP + disp32 + offset], reg
				bool is_floating_point = (op.value.type == Type::Float || op.value.type == Type::Double);
				bool is_float = (op.value.type == Type::Float);
				
				if (is_floating_point) {
					// Move to XMM register for floating-point stores
					X64Register xmm_reg = X64Register::XMM0;
					// MOVQ XMM0, value_reg (reinterpret bits)
					emitMovqGprToXmm(value_reg, xmm_reg);
					
					// MOVSD/MOVSS [RIP + disp32], XMM0
					textSectionData.push_back(is_float ? 0xF3 : 0xF2);
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0x11);
					uint8_t xmm_bits = static_cast<uint8_t>(xmm_reg) & 0x07;
					textSectionData.push_back(0x05 | (xmm_bits << 3));
					
					// Placeholder for displacement - will be patched by relocation
					uint32_t reloc_offset = static_cast<uint32_t>(textSectionData.size());
					// The actual displacement should account for the member offset
					int32_t disp_with_offset = op.offset;
					textSectionData.push_back((disp_with_offset >> 0) & 0xFF);
					textSectionData.push_back((disp_with_offset >> 8) & 0xFF);
					textSectionData.push_back((disp_with_offset >> 16) & 0xFF);
					textSectionData.push_back((disp_with_offset >> 24) & 0xFF);
					
					// Add relocation for the global variable (with offset already included in displacement)
					pending_global_relocations_.push_back({reloc_offset, object_name_handle, IMAGE_REL_AMD64_REL32, op.offset - 4});
				} else {
					// Integer store: MOV [RIP + disp32], reg
					int size_in_bits = op.value.size_in_bits;
					uint8_t src_val = static_cast<uint8_t>(value_reg);
					uint8_t src_bits = src_val & 0x07;
					uint8_t needs_rex_w = (size_in_bits == 64) ? 0x08 : 0x00;
					uint8_t needs_rex_b = (src_val >> 3) & 0x01;
					uint8_t rex = 0x40 | needs_rex_w | needs_rex_b;
					uint8_t emit_rex = (needs_rex_w | needs_rex_b) != 0;
					
					if (emit_rex) {
						textSectionData.push_back(rex);
					}
					
					if (size_in_bits == 8) {
						textSectionData.push_back(0x88); // MOV r/m8, r8
					} else {
						textSectionData.push_back(0x89); // MOV r/m32/r/m64, r32/r64
					}
					
					textSectionData.push_back(0x05 | (src_bits << 3));
					
					// Placeholder for displacement with member offset
					uint32_t reloc_offset = static_cast<uint32_t>(textSectionData.size());
					int32_t disp_with_offset = op.offset;
					textSectionData.push_back((disp_with_offset >> 0) & 0xFF);
					textSectionData.push_back((disp_with_offset >> 8) & 0xFF);
					textSectionData.push_back((disp_with_offset >> 16) & 0xFF);
					textSectionData.push_back((disp_with_offset >> 24) & 0xFF);
					
					// Add relocation
					pending_global_relocations_.push_back({reloc_offset, object_name_handle, IMAGE_REL_AMD64_REL32, op.offset - 4});
				}
				
				regAlloc.release(value_reg);
				return;  // Done with global member store
			}
			
			// Not a global - look in local scope
			auto it = current_scope.variables.find(object_name_handle);
			if (it == current_scope.variables.end()) {
				assert(false && "Struct object not found in scope");
				return;
			}
			object_base_offset = it->second.offset;

			// Check if this is the 'this' pointer or a reference parameter or pointer-to-member access
			if (StringTable::getStringView(object_name_handle) == "this"sv || 
			    reference_stack_info_.count(object_base_offset) > 0 ||
			    op.is_pointer_to_member) {
				is_pointer_access = true;
			}
		} else {
			// Nested case: object is the result of a previous member access
			auto object_temp = std::get<TempVar>(op.object);
			object_base_offset = getStackOffsetFromTempVar(object_temp);
			
			// Check if this temp var holds a pointer/address (from large member access) or is pointer-to-member
			if (reference_stack_info_.count(object_base_offset) > 0 || op.is_pointer_to_member) {
				is_pointer_access = true;
			}
		}

		// Calculate the member's actual stack offset
		int32_t member_stack_offset;
		if (is_pointer_access) {
			member_stack_offset = 0;  // Not used for pointer access
		} else {
			member_stack_offset = object_base_offset + op.offset;
		}

		// Calculate member size in bytes
		int member_size_bytes = op.value.size_in_bits / 8;

		// Load the value into a register - allocate through register allocator to avoid conflicts
		X64Register value_reg = allocateRegisterWithSpilling();

		if (op.is_reference) {
			// value_reg already allocated above
			bool pointer_loaded = false;
			if (is_variable) {
				// Check if this variable is itself a reference (e.g., reference parameter)
				auto it = current_scope.variables.find(variable_name);
				if (it != current_scope.variables.end()) {
					int32_t var_offset = it->second.offset;
					// Check if this stack variable is a reference
					auto ref_it = reference_stack_info_.find(var_offset);
					if (ref_it != reference_stack_info_.end()) {
						// This variable is a reference - it already holds a pointer
						// MOV the pointer value, don't take its address
						emitMovFromFrame(value_reg, var_offset);
					} else {
						// This variable is not a reference - take its address
						emitLeaFromFrame(value_reg, var_offset);
					}
					pointer_loaded = true;
				}
			} else if (!is_literal) {
				// TempVar - load its value (which is already a pointer from addressof)
				auto value_var = std::get<TempVar>(op.value.value);
				int32_t value_offset = getStackOffsetFromTempVar(value_var);
				// The TempVar contains an address, so we MOV (load value) not LEA (load address of)
				emitMovFromFrame(value_reg, value_offset);
				pointer_loaded = true;
			}
			if (!pointer_loaded) {
				if (is_literal && literal_value == 0) {
					moveImmediateToRegister(value_reg, 0);
					pointer_loaded = true;
				}
			}
			if (!pointer_loaded) {
				FLASH_LOG(Codegen, Error, "Reference member initializer must be an lvalue");
				throw std::runtime_error("Reference member initializer must be an lvalue");
			}
		} else if (is_literal) {
			if (is_double_literal) {
				uint64_t bits;
				std::memcpy(&bits, &literal_double_value, sizeof(bits));
				emitMovImm64(value_reg, bits);
			} else {
				uint64_t imm64 = static_cast<uint64_t>(literal_value);
				emitMovImm64(value_reg, imm64);
			}
		} else if (is_variable) {
			// Check if this is a vtable symbol (check vtable_symbol field in MemberStoreOp)
			// This will be handled separately below
			auto it = current_scope.variables.find(variable_name);
			if (it == current_scope.variables.end()) {
				assert(false && "Variable not found in scope");
				return;
			}
			int32_t value_offset = it->second.offset;
			// If pointer_depth > 0, we need to store the address of the variable (LEA)
			// not the value at that address (MOV). This is used for initializer_list
			// backing arrays where we need to store &array[0], not array[0].
			if (op.value.pointer_depth > 0) {
				emitLeaFromFrame(value_reg, value_offset);
			} else {
				emitMovFromFrameBySize(value_reg, value_offset, op.value.size_in_bits);
			}
		} else {
			auto value_var = std::get<TempVar>(op.value.value);
			int32_t value_offset = getStackOffsetFromTempVar(value_var);
			auto existing_reg = regAlloc.findRegisterForStackOffset(value_offset);
			if (existing_reg.has_value()) {
				value_reg = existing_reg.value();
			} else {
				emitMovFromFrameBySize(value_reg, value_offset, op.value.size_in_bits);
			}
		}

		// Store the value to the member's location
		if (is_pointer_access) {
			// For 'this' pointer or reference: load pointer into base_reg, then store to [base_reg + offset]
			// IMPORTANT: Allocate a register for the base pointer to avoid clobbering value_reg
			X64Register base_reg = allocateRegisterWithSpilling();
			auto load_ptr_opcodes = generatePtrMovFromFrame(base_reg, object_base_offset);
			textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
			                       load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);
			
			// Store value_reg to [base_reg + op.offset] using helper function
			emitStoreToMemory(textSectionData, value_reg, base_reg, op.offset, member_size_bytes);
			
			// Release the base register
			regAlloc.release(base_reg);
		} else {
			// For regular struct variables on the stack: store to [RBP + member_stack_offset]
			emitStoreToMemory(textSectionData, value_reg, X64Register::RBP, member_stack_offset, member_size_bytes);
		}

		// Release value_reg - we allocated it above
		regAlloc.release(value_reg);
	}

	void handleAddressOf(const IrInstruction& instruction) {
		// Address-of: &x
		// Check for typed payload
		if (instruction.hasTypedPayload()) {
			const auto& op = instruction.getTypedPayload<AddressOfOp>();
			
			int32_t var_offset;
			const StackVariableScope& current_scope = variable_scopes.back();
			// Use register allocator instead of directly using RAX to avoid clobbering dirty registers
			X64Register target_reg = allocateRegisterWithSpilling();
			bool is_global = false;
			StringHandle global_name_handle;
			
			// Get operand (variable to take address of) from TypedValue
			if (std::holds_alternative<TempVar>(op.operand.value)) {
				// Taking address of a temporary variable (e.g., for rvalue references)
				TempVar temp = std::get<TempVar>(op.operand.value);
				var_offset = getStackOffsetFromTempVar(temp);
			} else {
				// Taking address of a named variable
				std::string_view operand_str;
				if (std::holds_alternative<StringHandle>(op.operand.value)) {
					operand_str = StringTable::getStringView(std::get<StringHandle>(op.operand.value));
					global_name_handle = std::get<StringHandle>(op.operand.value);
				} else {
					assert(std::holds_alternative<TempVar>(op.operand.value) && "AddressOf operand must be StringHandle or TempVar");
					return;
				}

				// First, check if this is a global/static local variable
				is_global = isGlobalVariable(global_name_handle);
				
				if (!is_global) {
					auto it = current_scope.variables.find(StringTable::getOrInternStringHandle(operand_str));
					if (it == current_scope.variables.end()) {
						// Special case: This might be taking address of a class member (e.g., &Point::x)
						// which is only valid for pointer-to-member types
						// For now, stub this out - full implementation would need to generate
						// a pointer-to-member constant value
						FLASH_LOG(Codegen, Debug, "AddressOf operand '", operand_str, "' not found in scope - might be pointer-to-member, stubbing with zero");
						// Store zero as a placeholder for pointer-to-member
						emitMovImm64(target_reg, 0);
						return;
					}
					var_offset = it->second.offset;
				}
			}

			// Calculate the address
			if (is_global) {
				// Global/static local variable - use LEA with RIP-relative addressing
				uint32_t reloc_offset = emitLeaRipRelative(target_reg);
				pending_global_relocations_.push_back({reloc_offset, global_name_handle, IMAGE_REL_AMD64_REL32});
			} else {
				// If the variable is a reference, it already holds an address - use MOV to load it
				// Otherwise, use LEA to compute the address of the variable
				auto ref_it = reference_stack_info_.find(var_offset);
				if (ref_it != reference_stack_info_.end()) {
					// Variable is a reference - load the address it contains
					emitMovFromFrame(target_reg, var_offset);
				} else {
					// Regular variable - compute its address
					emitLeaFromFrame(target_reg, var_offset);
				}
			}

			// Store the address to result_var (pointer is always 64-bit)
			int32_t result_offset = getStackOffsetFromTempVar(op.result);
			emitMovToFrameSized(
				SizedRegister{target_reg, 64, false},  // source: 64-bit register
				SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
			);
			
			// NOTE: The result of addressof is a POINTER value, not a reference.
			// However, we mark it in reference_stack_info_ so that subsequent operations
			// know this TempVar holds a pointer and should be loaded with MOV, not LEA.
			// This is needed for proper handling when passing AddressOf results to functions.
			reference_stack_info_[result_offset] = ReferenceInfo{
				.value_type = op.operand.type,
				.value_size_bits = op.operand.size_in_bits,
				.is_rvalue_reference = false,  // AddressOf result is a pointer, not a reference
				.holds_address_only = true
			};
			
			// Release the register since the address has been stored to memory
			regAlloc.release(target_reg);
			
			return;
		}
		
		// Legacy format: Operands: [result_var, type, size, operand]
		assert(instruction.getOperandCount() == 4 && "AddressOf must have 4 operands");

		int32_t var_offset = 0;
		// Use register allocator instead of directly using RAX to avoid clobbering dirty registers
		X64Register target_reg = allocateRegisterWithSpilling();
		bool is_global = false;
		StringHandle global_name_handle;
		
		// Get operand (variable to take address of) - can be string_view, string, or TempVar
		if (instruction.isOperandType<TempVar>(3)) {
			// Taking address of a temporary variable (e.g., for rvalue references)
			TempVar temp = instruction.getOperandAs<TempVar>(3);
			var_offset = getStackOffsetFromTempVar(temp);
		} else {
			// Taking address of a named variable
			assert(instruction.isOperandType<StringHandle>(3) && "AddressOf operand must be string_view, string, or TempVar");
			global_name_handle = instruction.getOperandAs<StringHandle>(3);
			
			// First, check if this is a global/static local variable
			is_global = isGlobalVariable(global_name_handle);
			
			if (!is_global) {
				const StackVariableScope& current_scope = variable_scopes.back();
				auto it = current_scope.variables.find(global_name_handle);
				if (it == current_scope.variables.end()) {
					assert(false && "Variable not found in scope");
					return;
				}
				var_offset = it->second.offset;
			}
		}

		// Calculate the address
		if (is_global) {
			// Global/static local variable - use LEA with RIP-relative addressing
			uint32_t reloc_offset = emitLeaRipRelative(target_reg);
			pending_global_relocations_.push_back({reloc_offset, global_name_handle, IMAGE_REL_AMD64_REL32});
		} else {
			// Regular local variable - LEA RAX, [RBP + offset]
			emitLeaFromFrame(target_reg, var_offset);
		}
		
		// Store the address to result_var (pointer is always 64-bit)
		auto result_var = instruction.getOperandAs<TempVar>(0);
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		emitMovToFrameSized(
			SizedRegister{target_reg, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
		);
		
		// Release the register since the address has been stored to memory
		regAlloc.release(target_reg);
	}
	
	void handleAddressOfMember(const IrInstruction& instruction) {
		// AddressOfMember: &obj.member
		// Calculate address of struct member directly: LEA result, [RBP + obj_offset + member_offset]
		
		const AddressOfMemberOp& op = std::any_cast<const AddressOfMemberOp&>(instruction.getTypedPayload());
		
		// Look up the base object's stack offset
		const StackVariableScope& current_scope = variable_scopes.back();
		auto it = current_scope.variables.find(op.base_object);
		if (it == current_scope.variables.end()) {
			assert(false && "Base object not found in scope for AddressOfMember");
			return;
		}
		
		int32_t obj_offset = it->second.offset;
		int32_t combined_offset = obj_offset + op.member_offset;
		
		// Calculate the address: LEA target_reg, [RBP + combined_offset]
		// Use register allocator to avoid clobbering dirty registers
		X64Register target_reg = allocateRegisterWithSpilling();
		emitLeaFromFrame(target_reg, combined_offset);
		
		// Store the address to result_var (pointer is always 64-bit)
		int32_t result_offset = getStackOffsetFromTempVar(op.result);
		emitMovToFrameSized(
			SizedRegister{target_reg, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
		);
		
		// Release the register since the address has been stored to memory
		regAlloc.release(target_reg);
		
		// DO NOT mark as reference - this is a plain pointer value for use in arithmetic
	}

	void handleComputeAddress(const IrInstruction& instruction) {
		// ComputeAddress: One-pass address calculation for complex expressions
		// Handles: &arr[i].member1.member2, &arr[i][j], &arr[i].inner_arr[j].member
		// Algorithm: address = base + (index1 * elem_size1) + (index2 * elem_size2) + ... + member_offset
		
		const ComputeAddressOp& op = std::any_cast<const ComputeAddressOp&>(instruction.getTypedPayload());
		
		// Step 1: Load base address into RAX
		int64_t base_offset = 0;
		bool base_is_reference = false;
		bool base_is_pointer = false;  // For 'this' and other pointers
		if (std::holds_alternative<StringHandle>(op.base)) {
			// Variable name - look up its stack offset
			StringHandle base_name = std::get<StringHandle>(op.base);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(base_name);
			if (it == current_scope.variables.end()) {
				assert(false && "Base variable not found in scope for ComputeAddress");
				return;
			}
			base_offset = it->second.offset;
			
			// Check if base is 'this' - it's a pointer, so we need to load its value
			// instead of computing the address of the 'this' variable
			std::string_view base_name_str = StringTable::getStringView(base_name);
			if (base_name_str == "this") {
				base_is_pointer = true;
			}
			
			// Check if base is a reference - if so, we need to load the address it contains
			// instead of computing the address of the variable itself
			auto ref_it = reference_stack_info_.find(static_cast<int32_t>(base_offset));
			base_is_reference = (ref_it != reference_stack_info_.end());
		} else {
			// TempVar - get its stack offset
			TempVar base_temp = std::get<TempVar>(op.base);
			base_offset = getStackOffsetFromTempVar(base_temp);
			
			// Check if TempVar is a reference
			auto ref_it = reference_stack_info_.find(static_cast<int32_t>(base_offset));
			base_is_reference = (ref_it != reference_stack_info_.end());
		}
		
		if (base_is_reference || base_is_pointer) {
			// Base is a reference or pointer - load the address it contains (MOV, not LEA)
			emitMovFromFrame(X64Register::RAX, base_offset);
		} else {
			// Base is a regular variable - compute its address (LEA)
			emitLeaFromFrame(X64Register::RAX, base_offset);
		}
		
		// Step 2: Process each array index
		for (const auto& arr_idx : op.array_indices) {
			int element_size_bytes = arr_idx.element_size_bits / 8;
			
			// Load index into RCX
			if (std::holds_alternative<unsigned long long>(arr_idx.index)) {
				// Constant index
				uint64_t index_value = std::get<unsigned long long>(arr_idx.index);
				int64_t offset = index_value * element_size_bytes;
				
				// Add constant offset to RAX
				if (offset != 0) {
					emitAddImmToReg(textSectionData, X64Register::RAX, offset);
				}
			} else if (std::holds_alternative<TempVar>(arr_idx.index)) {
				// Variable index from temp
				TempVar index_var = std::get<TempVar>(arr_idx.index);
				int64_t index_offset = getStackOffsetFromTempVar(index_var);
				
				// Load index into RCX with proper size and sign extension
				bool is_signed = isSignedType(arr_idx.index_type);
				emitMovFromFrameSized(
					SizedRegister{X64Register::RCX, 64, false},
					SizedStackSlot{static_cast<int32_t>(index_offset), arr_idx.index_size_bits, is_signed}
				);
				
				// Multiply RCX by element size
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);
				
				// Add RCX to RAX
				emitAddRAXRCX(textSectionData);
			} else if (std::holds_alternative<StringHandle>(arr_idx.index)) {
				// Variable index from variable name
				StringHandle index_var_name = std::get<StringHandle>(arr_idx.index);
				const StackVariableScope& current_scope = variable_scopes.back();
				auto it = current_scope.variables.find(index_var_name);
				if (it == current_scope.variables.end()) {
					assert(false && "Index variable not found in scope");
					return;
				}
				int64_t index_offset = it->second.offset;
				
				// Load index into RCX with proper size and sign extension
				bool is_signed = isSignedType(arr_idx.index_type);
				emitMovFromFrameSized(
					SizedRegister{X64Register::RCX, 64, false},
					SizedStackSlot{static_cast<int32_t>(index_offset), arr_idx.index_size_bits, is_signed}
				);
				
				// Multiply RCX by element size
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);
				
				// Add RCX to RAX
				emitAddRAXRCX(textSectionData);
			}
		}
		
		// Step 3: Add accumulated member offset (if any)
		if (op.total_member_offset > 0) {
			emitAddImmToReg(textSectionData, X64Register::RAX, op.total_member_offset);
		}
		
		// Step 4: Store result
		int32_t result_offset = getStackOffsetFromTempVar(op.result);
		emitMovToFrameSized(
			SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}     // dest: 64-bit for pointer
		);
	}

	void handleDereference(const IrInstruction& instruction) {
		// Dereference: *ptr
		// Check for typed payload
		if (instruction.hasTypedPayload()) {
			const auto& op = instruction.getTypedPayload<DereferenceOp>();
			
			// THE FIX: Use pointer_depth to determine the correct dereference size
			// If pointer_depth > 1, we're dereferencing a multi-level pointer (e.g., int*** -> int**)
			// and the result is still a pointer (64 bits).
			// If pointer_depth == 1, we're dereferencing to the final value (use pointer.size_in_bits).
			int value_size;
			if (op.pointer.pointer_depth > 1) {
				value_size = 64;  // Result is still a pointer
			} else {
				// Final dereference - use the pointee size (stored in size_in_bits of the pointer's type)
				value_size = op.pointer.size_in_bits;
			}
			
			// Load the pointer into a register
			X64Register ptr_reg;
			const StackVariableScope& current_scope = variable_scopes.back();
			
			if (std::holds_alternative<TempVar>(op.pointer.value)) {
				TempVar temp = std::get<TempVar>(op.pointer.value);
				int32_t temp_offset = getStackOffsetFromTempVar(temp);
				
				// Check if the TempVar is already in a register (e.g., from a previous operation)
				if (auto reg_opt = regAlloc.tryGetStackVariableRegister(temp_offset); reg_opt.has_value()) {
					ptr_reg = reg_opt.value();
				} else {
					// Not in a register, load from stack
					ptr_reg = allocateRegisterWithSpilling();
					emitMovFromFrame(ptr_reg, temp_offset);
				}
			} else {
				StringHandle var_name_handle = std::get<StringHandle>(op.pointer.value);
				auto it = current_scope.variables.find(var_name_handle);
				if (it == current_scope.variables.end()) {
					assert(false && "Pointer variable not found");
					return;
				}
				
				// Check if the variable is already in a register
				if (auto reg_opt = regAlloc.tryGetStackVariableRegister(it->second.offset); reg_opt.has_value()) {
					ptr_reg = reg_opt.value();
				} else {
					ptr_reg = allocateRegisterWithSpilling();
					emitMovFromFrame(ptr_reg, it->second.offset);
				}
			}

			// Check if we're dereferencing a float/double type - use XMM register and MOVSD/MOVSS
			bool is_float_type = (op.pointer.type == Type::Float || op.pointer.type == Type::Double);
			
			if (is_float_type && op.pointer.pointer_depth <= 1) {
				// Only use float instructions for final dereference
				// Use XMM0 as the destination register for float loads
				X64Register xmm_reg = X64Register::XMM0;
				bool is_float = (op.pointer.type == Type::Float);
				
				// Load float/double from memory into XMM register
				emitFloatMovFromMemory(xmm_reg, ptr_reg, 0, is_float);
				
				// Store the XMM value to the result location
				int32_t result_offset = getStackOffsetFromTempVar(op.result);
				emitFloatMovToFrame(xmm_reg, result_offset, is_float);
				return;
			}

			// Handle struct types (values > 64 bits) by doing a multi-step memory copy
			if (value_size > 64 && op.pointer.pointer_depth <= 1) {
				int32_t result_offset = getStackOffsetFromTempVar(op.result);
				int struct_size_bytes = (value_size + 7) / 8;
				
				// Copy from [ptr_reg] to [rbp + result_offset] in 8-byte chunks
				for (int offset = 0; offset < struct_size_bytes; ) {
					if (offset + 8 <= struct_size_bytes) {
						// Copy 8 bytes
						X64Register temp_reg = allocateRegisterWithSpilling();
						emitMovFromMemory(temp_reg, ptr_reg, offset, 8);
						emitMovToFrameSized(
							SizedRegister{temp_reg, 64, false},
							SizedStackSlot{result_offset + offset, 64, false}
						);
						regAlloc.release(temp_reg);
						offset += 8;
					} else if (offset + 4 <= struct_size_bytes) {
						// Copy 4 bytes
						X64Register temp_reg = allocateRegisterWithSpilling();
						emitMovFromMemory(temp_reg, ptr_reg, offset, 4);
						emitMovToFrameSized(
							SizedRegister{temp_reg, 32, false},
							SizedStackSlot{result_offset + offset, 32, false}
						);
						regAlloc.release(temp_reg);
						offset += 4;
					} else if (offset + 2 <= struct_size_bytes) {
						// Copy 2 bytes
						X64Register temp_reg = allocateRegisterWithSpilling();
						emitMovFromMemory(temp_reg, ptr_reg, offset, 2);
						emitMovToFrameSized(
							SizedRegister{temp_reg, 16, false},
							SizedStackSlot{result_offset + offset, 16, false}
						);
						regAlloc.release(temp_reg);
						offset += 2;
					} else {
						// Copy 1 byte
						X64Register temp_reg = allocateRegisterWithSpilling();
						emitMovFromMemory(temp_reg, ptr_reg, offset, 1);
						emitMovToFrameSized(
							SizedRegister{temp_reg, 8, false},
							SizedStackSlot{result_offset + offset, 8, false}
						);
						regAlloc.release(temp_reg);
						offset += 1;
					}
				}
				return;
			}

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
			int32_t result_offset = getStackOffsetFromTempVar(op.result);
			auto result_store = generateMovToFrameBySize(value_reg, result_offset, value_size);
			textSectionData.insert(textSectionData.end(), result_store.op_codes.begin(),
			                       result_store.op_codes.begin() + result_store.size_in_bytes);
			
			// CRITICAL FIX: Clear the register association with the old pointer offset
			// After dereferencing, value_reg no longer holds the pointer - it holds the dereferenced value
			// However, we need to be careful: if value_reg was reused from ptr_reg and ptr_reg had
			// an old association with a different offset, we must clear that to prevent corruption.
			// But we don't want to release the register completely, as it might be needed.
			// So we only clear the stack variable offset if it doesn't match the result offset.
			for (auto& reg_info : regAlloc.registers) {
				if (reg_info.reg == value_reg && reg_info.stackVariableOffset != result_offset) {
					reg_info.stackVariableOffset = INT_MIN;
					reg_info.isDirty = false;
				}
			}
			return;
		}
		
		// Legacy format: Operands: [result_var, type, size, operand]
		assert(instruction.getOperandCount() == 4 && "Dereference must have 4 operands");

		[[maybe_unused]] Type value_type = instruction.getOperandAs<Type>(1);
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
		auto result_store = generateMovToFrameBySize(value_reg, result_offset, value_size);
		textSectionData.insert(textSectionData.end(), result_store.op_codes.begin(),
		                       result_store.op_codes.begin() + result_store.size_in_bytes);
		
		// CRITICAL FIX: Clear the register association with the old pointer offset
		// After dereferencing, value_reg no longer holds the pointer - it holds the dereferenced value
		// However, we need to be careful: if value_reg was reused from ptr_reg and ptr_reg had
		// an old association with a different offset, we must clear that to prevent corruption.
		// But we don't want to release the register completely, as it might be needed.
		// So we only clear the stack variable offset if it doesn't match the result offset.
		for (auto& reg_info : regAlloc.registers) {
			if (reg_info.reg == value_reg && reg_info.stackVariableOffset != result_offset) {
				reg_info.stackVariableOffset = INT_MIN;
				reg_info.isDirty = false;
			}
		}
	}

	void handleDereferenceStore(const IrInstruction& instruction) {
		// DereferenceStore: *ptr = value
		// Store a value through a pointer
		assert(instruction.hasTypedPayload() && "DereferenceStore instruction must use typed payload");
		const auto& op = instruction.getTypedPayload<DereferenceStoreOp>();
		
		// Flush all dirty registers before loading values from stack
		// This ensures that any values computed in previous instructions (like ADD) 
		// are written to their stack locations before we try to load them
		flushAllDirtyRegisters();
		
		int value_size = op.value.size_in_bits;
		int value_size_bytes = value_size / 8;
		
		// Allocate registers through the register allocator to avoid conflicts
		X64Register ptr_reg = allocateRegisterWithSpilling();
		const StackVariableScope& current_scope = variable_scopes.back();
		
		if (std::holds_alternative<TempVar>(op.pointer.value)) {
			TempVar temp = std::get<TempVar>(op.pointer.value);
			int32_t temp_offset = getStackOffsetFromTempVar(temp);
			emitMovFromFrame(ptr_reg, temp_offset);
		} else {
			StringHandle var_name_handle = std::get<StringHandle>(op.pointer.value);
			auto it = current_scope.variables.find(var_name_handle);
			if (it == current_scope.variables.end()) {
				assert(false && "Pointer variable not found in DereferenceStore");
				return;
			}
			emitMovFromFrame(ptr_reg, it->second.offset);
		}
		
		// Allocate a second register for the value - must be different from ptr_reg
		X64Register value_reg = allocateRegisterWithSpilling();
		
		if (std::holds_alternative<unsigned long long>(op.value.value)) {
			uint64_t imm_value = std::get<unsigned long long>(op.value.value);
			emitMovImm64(value_reg, imm_value);
		} else if (std::holds_alternative<TempVar>(op.value.value)) {
			TempVar value_temp = std::get<TempVar>(op.value.value);
			int32_t value_offset = getStackOffsetFromTempVar(value_temp);
			emitMovFromFrameSized(
				SizedRegister{value_reg, static_cast<uint8_t>(value_size), isSignedType(op.value.type)},
				SizedStackSlot{value_offset, value_size, isSignedType(op.value.type)}
			);
		} else if (std::holds_alternative<StringHandle>(op.value.value)) {
			StringHandle var_name_handle = std::get<StringHandle>(op.value.value);
			auto it = current_scope.variables.find(var_name_handle);
			if (it != current_scope.variables.end()) {
				emitMovFromFrameSized(
					SizedRegister{value_reg, static_cast<uint8_t>(value_size), isSignedType(op.value.type)},
					SizedStackSlot{static_cast<int32_t>(it->second.offset), value_size, isSignedType(op.value.type)}
				);
			}
		}
		
		// Store value_reg to [ptr_reg] with appropriate size
		emitStoreToMemory(textSectionData, value_reg, ptr_reg, 0, value_size_bytes);
	}

	void handleConditionalBranch(const IrInstruction& instruction) {
		// Conditional branch: test condition, jump if false to else_label, fall through to then_label
		assert(instruction.hasTypedPayload() && "ConditionalBranch instruction must use typed payload");
		const auto& cond_branch_op = instruction.getTypedPayload<CondBranchOp>();
		IrValue condition_value = cond_branch_op.condition.value;
		auto then_label = cond_branch_op.getLabelTrue();
		auto else_label = cond_branch_op.getLabelFalse();
		// Flush all dirty registers before branching
		flushAllDirtyRegisters();

		// Load condition value into a register
		X64Register condition_reg = X64Register::RAX;

		if (std::holds_alternative<TempVar>(condition_value)) {
			auto temp_var = std::get<TempVar>(condition_value);
			int var_offset = getStackOffsetFromTempVar(temp_var);
			
			// Look up the actual size of this temp var (default to 32 if not found)
			int load_size = 32;
			auto size_it = temp_var_sizes_.find(StringTable::getOrInternStringHandle(temp_var.name()));
			if (size_it != temp_var_sizes_.end()) {
				load_size = size_it->second;
			}
			
			// Check if temp var is already in a register
			if (auto reg = regAlloc.tryGetStackVariableRegister(var_offset); reg.has_value()) {
				condition_reg = reg.value();
			} else {
				// Load from memory with correct size
				emitMovFromFrameBySize(X64Register::RAX, var_offset, load_size);
				condition_reg = X64Register::RAX;
			}
		} else if (std::holds_alternative<StringHandle>(condition_value)) {
			StringHandle var_name = std::get<StringHandle>(condition_value);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(var_name);
			if (it != current_scope.variables.end()) {
				// Use the size stored in the variable info, default to 32 if 0 (shouldn't happen)
				int load_size = it->second.size_in_bits > 0 ? it->second.size_in_bits : 32;
				
				// Check if variable is already in a register
				if (auto reg = regAlloc.tryGetStackVariableRegister(it->second.offset); reg.has_value()) {
					condition_reg = reg.value();
				} else {
					emitMovFromFrameBySize(X64Register::RAX, it->second.offset, load_size);
					condition_reg = X64Register::RAX;
				}
			}
		} else if (std::holds_alternative<unsigned long long>(condition_value)) {
			// Immediate value
			unsigned long long value = std::get<unsigned long long>(condition_value);

			// MOV RAX, imm64
			emitMovImm64(X64Register::RAX, value);
			condition_reg = X64Register::RAX;
		}

		// Test if condition is non-zero: TEST reg, reg
		// Use the actual register that holds the condition value
		uint8_t reg_code = static_cast<uint8_t>(condition_reg);
		uint8_t modrm = 0xC0 | ((reg_code & 0x7) << 3) | (reg_code & 0x7);  // TEST reg, reg
		std::array<uint8_t, 3> testInst = { 0x48, 0x85, modrm }; // test condition_reg, condition_reg
		textSectionData.insert(textSectionData.end(), testInst.begin(), testInst.end());

		// Check if then_label is a backward reference (already defined)
		// This happens in do-while loops where we jump back to the start when true
		bool then_is_backward = label_positions_.find(then_label) != label_positions_.end();
		
		if (then_is_backward) {
			// For do-while: then_label is backward (jump to loop start), else_label is forward (fall through to end)
			// Use JNZ (jump if not zero) to then_label, fall through to else_label
			textSectionData.push_back(0x0F); // Two-byte opcode prefix
			textSectionData.push_back(0x85); // JNZ/JNE rel32

			uint32_t then_patch_position = static_cast<uint32_t>(textSectionData.size());
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);

			pending_branches_.push_back({then_label, then_patch_position});
			// Fall through to else block (loop end)
		} else {
			// For while/if: then_label is forward (fall through to body), else_label is forward (jump to end)
			// Use JZ (jump if zero) to else_label, fall through to then_label
			textSectionData.push_back(0x0F); // Two-byte opcode prefix
			textSectionData.push_back(0x84); // JZ/JE rel32

			uint32_t else_patch_position = static_cast<uint32_t>(textSectionData.size());
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);

			pending_branches_.push_back({else_label, else_patch_position});
			// Fall through to then block
		}
	}
	
	void handleFunctionAddress(const IrInstruction& instruction) {
		// Extract typed payload - all FunctionAddress instructions use typed payloads
		const FunctionAddressOp& op = std::any_cast<const FunctionAddressOp&>(instruction.getTypedPayload());

		flushAllDirtyRegisters();

		auto result_var = std::get<TempVar>(op.result.value);

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
		// All FunctionAddress instructions should now have the mangled name pre-computed
		// Phase 4: Use helper
		std::string_view mangled = StringTable::getStringView(op.getMangledName());
		assert(!mangled.empty() && "FunctionAddress instruction missing mangled_name");
		writer.add_relocation(reloc_position, mangled, IMAGE_REL_AMD64_REL32);

		// Store RAX to result variable
		auto store_opcodes = generatePtrMovToFrame(X64Register::RAX, result_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

		regAlloc.reset();
	}

	void handleIndirectCall(const IrInstruction& instruction) {
		// IndirectCall: Call through function pointer
		auto& op = instruction.getTypedPayload<IndirectCallOp>();

		flushAllDirtyRegisters();

		// Get result offset
		int result_offset = getStackOffsetFromTempVar(op.result);
		variable_scopes.back().variables[StringTable::getOrInternStringHandle(op.result.name())].offset = result_offset;

		// Load function pointer into RAX
		X64Register func_ptr_reg;
		if (std::holds_alternative<TempVar>(op.function_pointer)) {
			TempVar func_ptr_temp = std::get<TempVar>(op.function_pointer);
			int func_ptr_offset = getStackOffsetFromTempVar(func_ptr_temp);
			func_ptr_reg = X64Register::RAX;
			emitMovFromFrame(func_ptr_reg, func_ptr_offset);
		} else {
			// Function pointer is a variable name
			StringHandle var_name_handle = std::get<StringHandle>(op.function_pointer);
			int func_ptr_offset = variable_scopes.back().variables[var_name_handle].offset;
			func_ptr_reg = X64Register::RAX;
			emitMovFromFrame(func_ptr_reg, func_ptr_offset);
		}
		// Process arguments (if any)
		for (size_t i = 0; i < op.arguments.size() && i < 4; ++i) {
			const auto& arg = op.arguments[i];
			Type argType = arg.type;

			// Determine if this is a floating-point argument
			bool is_float_arg = is_floating_point_type(argType);

			// Determine the target register for the argument
			X64Register target_reg = is_float_arg ? getFloatParamReg<TWriterClass>(i) : getIntParamReg<TWriterClass>(i);

			// Load argument into target register
			if (std::holds_alternative<TempVar>(arg.value)) {
				const TempVar temp_var = std::get<TempVar>(arg.value);
				int arg_offset = getStackOffsetFromTempVar(temp_var);
				if (is_float_arg) {
					bool is_float = (argType == Type::Float);
					emitFloatMovFromFrame(target_reg, arg_offset, is_float);
				} else {
					// Use size-aware load: source (sized stack slot) -> dest (64-bit register)
					emitMovFromFrameSized(
						SizedRegister{target_reg, 64, false},  // dest: 64-bit register
						SizedStackSlot{arg_offset, arg.size_in_bits, isSignedType(argType)}  // source: sized stack slot
					);
				}
			} else if (std::holds_alternative<StringHandle>(arg.value)) {
				StringHandle arg_var_name_handle = std::get<StringHandle>(arg.value);
				int arg_offset = variable_scopes.back().variables[arg_var_name_handle].offset;
				if (is_float_arg) {
					bool is_float = (argType == Type::Float);
					auto load_opcodes = generateFloatMovFromFrame(target_reg, arg_offset, is_float);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				} else {
					// Use size-aware load: source (sized stack slot) -> dest (64-bit register)
					emitMovFromFrameSized(
						SizedRegister{target_reg, 64, false},  // dest: 64-bit register
						SizedStackSlot{arg_offset, arg.size_in_bits, isSignedType(argType)}  // source: sized stack slot
					);
				}
			} else if (std::holds_alternative<unsigned long long>(arg.value)) {
				// Immediate value
				unsigned long long value = std::get<unsigned long long>(arg.value);
				emitMovImm64(target_reg, value);
			}
		}

		// Call through function pointer in RAX
		// CALL RAX
		textSectionData.push_back(0xFF); // CALL r/m64
		textSectionData.push_back(0xD0); // ModR/M: RAX

		// Store return value from RAX to result variable
		auto store_opcodes = generatePtrMovToFrame(X64Register::RAX, result_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

		regAlloc.reset();
	}

	// ============================================================================
	// Exception Handling Implementation
	// ============================================================================
	// Implementation status:
	// [X] Exceptions are thrown via _CxxThrowException (proper MSVC C++ runtime call)
	// [X] SEH frames exist via PDATA/XDATA sections with __CxxFrameHandler3 reference
	// [X] Stack unwinding works via unwind codes in XDATA
	// [X] FuncInfo structures generated with try-block maps and catch handlers
	// [X] Catch blocks execute for thrown exceptions
	// [X] Type-specific exception matching with type descriptors
	//
	// What works:
	// - throw statement properly calls _CxxThrowException with exception object
	// - throw; (rethrow) properly calls _CxxThrowException with NULL
	// - Stack unwinding occurs correctly during exception propagation
	// - Programs terminate properly for uncaught exceptions
	// - Try/catch blocks with catch handlers execute when exceptions are thrown
	// - catch(...) catches all exception types
	// - Type descriptors (??_R0) generated for caught exception types
	// - Type-specific catch blocks match based on exception type
	// - catch by const (catch(const int&)) supported via adjectives field
	// - catch by lvalue reference (catch(int&)) supported
	// - catch by rvalue reference (catch(int&&)) supported
	// - Destructor unwinding infrastructure: UnwindMap entries can track local objects with destructors
	//
	// Current implementation:
	// - Type descriptors created in .rdata for each unique exception type
	// - HandlerType pType field points to appropriate type descriptor
	// - MSVC name mangling used for built-in types (int, char, double, etc.)
	// - Simple mangling for class/struct types (V<name>@@)
	// - Adjectives field set for const/reference catch clauses
	//   - 0x01 = const
	//   - 0x08 = lvalue reference (&)
	//   - 0x10 = rvalue reference (&&)
	// - State-based exception handling through tryLow/tryHigh/catchHigh state numbers
	//   - __CxxFrameHandler3 uses states to determine active try blocks
	// - UnwindMap data structure generation in XDATA
	//   - Infrastructure in place for tracking local objects with destructors
	//   - UnwindMapEntry: toState (next state) + action (destructor RVA)
	//
	// Limitations:
	// - Automatic destructor calls not yet connected (need parser/codegen to track object lifetimes)
	// - Template type mangling is simplified (not full MSVC encoding)
	//
	// For full C++ exception semantics, the following enhancements could be added:
	// - Automatic tracking of object construction/destruction in parser/codegen
	// - Connection of destructor calls to unwind map entries
	// - Full MSVC template type mangling with argument encoding
	// ============================================================================
	
	void handleTryBegin([[maybe_unused]] const IrInstruction& instruction) {
		// Skip exception handling if disabled
		if (!g_enable_exceptions) {
			return;
		}
		
		// TryBegin marks the start of a try block
		// Record the current code offset as the start of the try block
		
		TryBlock try_block;
		try_block.try_start_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
		try_block.try_end_offset = 0;  // Will be set in handleTryEnd
		
		current_function_try_blocks_.push_back(try_block);
		current_try_block_ = &current_function_try_blocks_.back();
		
		// Note: The instruction has a BranchOp typed payload with the handler label,
		// but we don't need it for code generation - we track offsets directly.
	}

	void handleTryEnd([[maybe_unused]] const IrInstruction& instruction) {
		// Skip exception handling if disabled
		if (!g_enable_exceptions) {
			return;
		}
		
		// TryEnd marks the end of a try block
		// Record the current code offset as the end of the try block
		
		if (current_try_block_) {
			current_try_block_->try_end_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
			current_try_block_ = nullptr;
		}
	}

	void handleCatchBegin(const IrInstruction& instruction) {
		// Skip exception handling if disabled
		if (!g_enable_exceptions) {
			return;
		}
		
		// CatchBegin marks the start of a catch handler
		// Record this catch handler in the most recent try block
		
		if (!current_function_try_blocks_.empty()) {
			// Get the last try block (the one we just finished with TryEnd)
			TryBlock& try_block = current_function_try_blocks_.back();
			
			CatchHandler handler;
			handler.handler_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
			
			// Extract data from typed payload
			const auto& catch_op = instruction.getTypedPayload<CatchBeginOp>();
			handler.type_index = catch_op.type_index;
			handler.exception_type = catch_op.exception_type;  // Copy the Type enum
			handler.is_const = catch_op.is_const;
			handler.is_reference = catch_op.is_reference;
			handler.is_rvalue_reference = catch_op.is_rvalue_reference;
			handler.is_catch_all = catch_op.is_catch_all;  // Use the flag from IR, not derive from type_index
			
			// Pre-compute stack offset for exception object during IR processing.
			// This is necessary because variable_scopes may be cleared by the time
			// we call convertExceptionInfoToWriterFormat() during finalization.
			// var_number == 0 indicates catch(...) which has no exception variable,
			// or an unnamed catch parameter like catch(int) without a variable name.
			if (!handler.is_catch_all && catch_op.exception_temp.var_number != 0) {
				handler.catch_obj_stack_offset = getStackOffsetFromTempVar(catch_op.exception_temp);
			} else {
				handler.catch_obj_stack_offset = 0;
			}
			
			try_block.catch_handlers.push_back(handler);
		}
		
		// Platform-specific landing pad code generation
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			inside_catch_handler_ = true;
			// ========== Linux/ELF (Itanium C++ ABI) ==========
			// Landing pad: call __cxa_begin_catch to get the exception object
			//
			// The exception object pointer is in RAX (passed by personality routine)
			// We need to:
			// 1. Call __cxa_begin_catch(void* exceptionObject) -> returns adjusted pointer
			// 2. Extract/cast the exception value
			// 3. Store it in the catch variable
			
			const auto& catch_op = instruction.getTypedPayload<CatchBeginOp>();
			
			// Call __cxa_begin_catch with exception pointer in RDI
			// The exception pointer is in RAX from personality routine
			emitMovRegReg(X64Register::RDI, X64Register::RAX);
			emitCall("__cxa_begin_catch");
			
			// Result in RAX is pointer to the actual exception object
			// Store it to the catch variable's stack location
			if (catch_op.exception_temp.var_number != 0) {
				int32_t stack_offset = getStackOffsetFromTempVar(catch_op.exception_temp);
				
				if (g_enable_debug_output) {
					std::cerr << "[DEBUG][Codegen] CatchBegin: is_ref=" << catch_op.is_reference
					          << " is_rvalue_ref=" << catch_op.is_rvalue_reference
					          << " type_index=" << catch_op.type_index
					          << " stack_offset=" << stack_offset << std::endl;
				}
				
				// For POD types, dereference and copy the value
				// For references, store the pointer itself
				if (catch_op.is_reference || catch_op.is_rvalue_reference) {
					// Store the pointer (RAX) directly
					if (g_enable_debug_output) {
						std::cerr << "[DEBUG][Codegen] CatchBegin: storing pointer (reference type)" << std::endl;
					}
					emitMovToFrame(X64Register::RAX, stack_offset);
				} else {
					// Get type size for dereferencing
					// For built-in types, use get_type_size_bits directly
					// For user-defined types, look up in gTypeInfo
					int type_size_bits = 0;
					
					// Check if this is a built-in type that get_type_size_bits can handle
					// These are types where the Type enum directly maps to a size
					bool is_builtin = false;
					switch (catch_op.exception_type) {
						case Type::Bool:
						case Type::Char:
						case Type::UnsignedChar:
						case Type::Short:
						case Type::UnsignedShort:
						case Type::Int:
						case Type::UnsignedInt:
						case Type::Long:
						case Type::UnsignedLong:
						case Type::LongLong:
						case Type::UnsignedLongLong:
						case Type::Float:
						case Type::Double:
						case Type::LongDouble:
						case Type::FunctionPointer:
						case Type::MemberFunctionPointer:
						case Type::MemberObjectPointer:
						case Type::Nullptr:
							is_builtin = true;
							break;
						default:
							is_builtin = false;
							break;
					}
					
					if (is_builtin) {
						// Built-in type - use direct lookup
						type_size_bits = get_type_size_bits(catch_op.exception_type);
					} else if (catch_op.type_index != 0 && catch_op.type_index < gTypeInfo.size()) {
						// User-defined type - look up in gTypeInfo
						const TypeInfo& type_info = gTypeInfo[catch_op.type_index];
						type_size_bits = type_info.type_size_;
					}
					size_t type_size = type_size_bits / 8;  // Convert bits to bytes
					
					if (g_enable_debug_output) {
						std::cerr << "[DEBUG][Codegen] CatchBegin: exception_type=" << static_cast<int>(catch_op.exception_type)
						          << " type_size_bits=" << type_size_bits
						          << " type_size=" << type_size << std::endl;
					}
					
					// Load value from exception object and store to catch variable
					if (type_size <= 8 && type_size > 0) {
						// Small POD: load from [RAX] and store to frame
						// Move value from [RAX] to RCX
						if (g_enable_debug_output) {
							std::cerr << "[DEBUG][Codegen] CatchBegin: dereferencing exception value" << std::endl;
						}
						emitMovFromMemory(X64Register::RCX, X64Register::RAX, 0, type_size);
						emitMovToFrameBySize(X64Register::RCX, stack_offset, type_size_bits);
					} else {
						// Large type or unknown size: just store pointer
						if (g_enable_debug_output) {
							std::cerr << "[DEBUG][Codegen] CatchBegin: storing pointer (large or unknown type)" << std::endl;
						}
						emitMovToFrame(X64Register::RAX, stack_offset);
					}
				}
			}
			
		} else {
			// ========== Windows/COFF (MSVC ABI) ==========
			// Windows uses SEH which is already handled by existing code
		}
	}

	void handleCatchEnd([[maybe_unused]] const IrInstruction& instruction) {
		// Skip exception handling if disabled
		if (!g_enable_exceptions) {
			return;
		}
		
		// CatchEnd marks the end of a catch handler
		
		// Platform-specific cleanup
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// ========== Linux/ELF (Itanium C++ ABI) ==========
			// Call __cxa_end_catch to complete exception handling
			// This cleans up the exception object if needed
			
			emitCall("__cxa_end_catch");
			inside_catch_handler_ = false;
			
		} else {
			// ========== Windows/COFF (MSVC ABI) ==========
			// Windows SEH cleanup already handled
		}
	}

	void handleThrow(const IrInstruction& instruction) {
		// Skip exception handling if disabled - generate abort() instead
		if (!g_enable_exceptions) {
			// Call abort() to terminate when exceptions are disabled
			emitCall("abort");
			return;
		}
		
		// Extract data from typed payload
		const auto& throw_op = instruction.getTypedPayload<ThrowOp>();
		
		size_t exception_size = throw_op.size_in_bytes;
		if (exception_size == 0) exception_size = 8; // Minimum size
		
		// Round exception size up to 8-byte alignment
		size_t aligned_exception_size = (exception_size + 7) & ~7;
		
		// Platform-specific exception handling
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// ========== Linux/ELF (Itanium C++ ABI) ==========
			// Two-step process:
			// 1. Call __cxa_allocate_exception(size_t thrown_size) -> returns void*
			// 2. Copy exception object to allocated memory
			// 3. Call __cxa_throw(void* thrown_object, type_info* tinfo, void (*dest)(void*))
			//
			// System V AMD64 calling convention: RDI, RSI, RDX, RCX, R8, R9
			
			// Step 1: Call __cxa_allocate_exception(exception_size)
			// Argument: RDI = exception_size
			emitMovImm64(X64Register::RDI, aligned_exception_size);
			emitSubRSP(8); // Align stack to 16 bytes before call
			emitCall("__cxa_allocate_exception");
			emitAddRSP(8); // Clean up alignment
			
			// Result is in RAX (pointer to allocated exception object)
			// Save it to R15 (callee-saved register)
			emitMovRegReg(X64Register::R15, X64Register::RAX);
			
			// Step 2: Copy exception object to allocated memory
			if (exception_size <= 8) {
				// Small object: load value into RCX and store to [R15]
				// Use IrValue variant to handle TempVar, integer literal, or float literal
				if (std::holds_alternative<double>(throw_op.exception_value)) {
					// Float immediate - convert to bit representation
					double float_val = std::get<double>(throw_op.exception_value);
					uint64_t bits;
					if (exception_size == 4) {
						float f = static_cast<float>(float_val);
						std::memcpy(&bits, &f, sizeof(float));
						bits &= 0xFFFFFFFF; // Clear upper bits
					} else {
						std::memcpy(&bits, &float_val, sizeof(double));
					}
					emitMovImm64(X64Register::RCX, bits);
				} else if (std::holds_alternative<unsigned long long>(throw_op.exception_value)) {
					// Integer immediate - load directly
					emitMovImm64(X64Register::RCX, std::get<unsigned long long>(throw_op.exception_value));
				} else if (std::holds_alternative<TempVar>(throw_op.exception_value)) {
					// TempVar - load from stack
					TempVar temp = std::get<TempVar>(throw_op.exception_value);
					if (temp.var_number != 0) {
						int32_t stack_offset = getStackOffsetFromTempVar(temp);
						emitMovFromFrameBySize(X64Register::RCX, stack_offset, static_cast<int>(exception_size * 8));
					} else {
						emitMovImm64(X64Register::RCX, 0);
					}
				} else {
					// StringHandle is not a valid exception value type - IrValue includes it for
					// other contexts (like variable names), but throw expressions only use TempVar
					// or immediate values. Default to zero as a safety fallback.
					emitMovImm64(X64Register::RCX, 0);
				}
				// Store exception value to allocated memory [R15 + 0]
				emitStoreToMemory(textSectionData, X64Register::RCX, X64Register::R15, 0, static_cast<int>(exception_size));
			} else {
				// Large object: memory-to-memory copy (must be TempVar)
				if (std::holds_alternative<TempVar>(throw_op.exception_value)) {
					TempVar temp = std::get<TempVar>(throw_op.exception_value);
					if (temp.var_number != 0) {
						int32_t stack_offset = getStackOffsetFromTempVar(temp);
						emitLeaFromFrame(X64Register::RSI, stack_offset);
					} else {
						emitXorRegReg(X64Register::RSI);
					}
				} else {
					// Large objects can only be TempVars - immediates and StringHandles are not valid here
					emitXorRegReg(X64Register::RSI);
				}
				// RDI = destination (allocated memory in R15)
				emitMovRegReg(X64Register::RDI, X64Register::R15);
				// RCX = count
				emitMovImm64(X64Register::RCX, exception_size);
				// Copy: rep movsb
				emitRepMovsb();
			}
			
			// Step 3: Call __cxa_throw(thrown_object, tinfo, destructor)
			// RDI = thrown_object (from __cxa_allocate_exception, now in R15)
			emitMovRegReg(X64Register::RDI, X64Register::R15);
			
			// RSI = type_info* - generate type_info for the thrown type
			std::string typeinfo_symbol;
			Type exception_type = throw_op.exception_type;
			
			// Check if it's a built-in type or user-defined type
			if (exception_type == Type::Struct && throw_op.type_index < gTypeInfo.size()) {
				// User-defined class type - look up struct info from type_index
				const TypeInfo& type_info = gTypeInfo[throw_op.type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info) {
					typeinfo_symbol = writer.get_or_create_class_typeinfo(StringTable::getStringView(struct_info->getName()));
				}
			} else if (exception_type != Type::Void) {
				// Built-in type (int, float, etc.) - use the Type enum directly
				typeinfo_symbol = writer.get_or_create_builtin_typeinfo(exception_type);
			}
			
			if (!typeinfo_symbol.empty()) {
				// Load address of type_info into RSI using RIP-relative LEA
				emitLeaRipRelativeWithRelocation(X64Register::RSI, typeinfo_symbol);
			} else {
				// Unknown type, use NULL
				emitXorRegReg(X64Register::RSI);
			}
			
			// RDX = destructor function pointer (NULL for POD types)
			emitXorRegReg(X64Register::RDX);
			
			emitCall("__cxa_throw");
			// Note: __cxa_throw never returns
			
		} else {
			// ========== Windows/COFF (MSVC ABI) ==========
			// Single-step process:
			// Call _CxxThrowException(void* pExceptionObject, _ThrowInfo* pThrowInfo)
			//
			// Windows x64 calling convention: RCX, RDX, R8, R9
			
			// Calculate stack space needed:
			// - 32 bytes shadow space (Windows x64 calling convention)
			// - N bytes for exception object (rounded up to 8-byte alignment)
			// - Total rounded up to 16-byte alignment
			size_t total_stack = ((32 + aligned_exception_size) + 15) & ~15;
			emitSubRSP(static_cast<int32_t>(total_stack));
			
			// Copy exception object to stack at [RSP+32]
			if (exception_size <= 8) {
				// Small object: load into RAX and store
				// Use IrValue variant to handle TempVar or immediate
				if (std::holds_alternative<TempVar>(throw_op.exception_value)) {
					TempVar temp = std::get<TempVar>(throw_op.exception_value);
					if (temp.var_number != 0) {
						int32_t stack_offset = getStackOffsetFromTempVar(temp);
						emitMovFromFrameBySize(X64Register::RAX, stack_offset, static_cast<int>(exception_size * 8));
					} else {
						emitMovImm64(X64Register::RAX, 0);
					}
				} else if (std::holds_alternative<unsigned long long>(throw_op.exception_value)) {
					emitMovImm64(X64Register::RAX, std::get<unsigned long long>(throw_op.exception_value));
				} else if (std::holds_alternative<double>(throw_op.exception_value)) {
					double float_val = std::get<double>(throw_op.exception_value);
					uint64_t bits;
					if (exception_size == 4) {
						float f = static_cast<float>(float_val);
						std::memcpy(&bits, &f, sizeof(float));
						bits &= 0xFFFFFFFF;
					} else {
						std::memcpy(&bits, &float_val, sizeof(double));
					}
					emitMovImm64(X64Register::RAX, bits);
				} else {
					emitMovImm64(X64Register::RAX, 0);
				}
				emitMovToRSPDisp8(X64Register::RAX, 32);
			} else {
				// Large object: use memory-to-memory copy (must be TempVar)
				if (std::holds_alternative<TempVar>(throw_op.exception_value)) {
					TempVar temp = std::get<TempVar>(throw_op.exception_value);
					if (temp.var_number != 0) {
						int32_t stack_offset = getStackOffsetFromTempVar(temp);
						emitLeaFromFrame(X64Register::RSI, stack_offset);
					} else {
						emitXorRegReg(X64Register::RSI);
					}
				} else {
					// Large objects can only be TempVars - immediates and StringHandles are not valid here
					emitXorRegReg(X64Register::RSI);
				}
				emitLeaFromRSPDisp8(X64Register::RDI, 32);
				emitMovImm64(X64Register::RCX, exception_size);
				emitRepMovsb();
			}
			
			// Set up arguments for _CxxThrowException
			// RCX (first argument) = pointer to exception object = RSP+32
			emitLeaFromRSPDisp8(X64Register::RCX, 32);
			// RDX (second argument) = NULL (no throw info)
			emitXorRegReg(X64Register::RDX);
			
			emitCall("_CxxThrowException");
			// Note: _CxxThrowException never returns
		}
	}

	void handleRethrow([[maybe_unused]] const IrInstruction& instruction) {
		// Skip exception handling if disabled - generate abort() instead
		if (!g_enable_exceptions) {
			// Call abort() to terminate when exceptions are disabled
			emitCall("abort");
			return;
		}
		
		// Platform-specific rethrow implementation
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// ========== Linux/ELF (Itanium C++ ABI) ==========
			// Call __cxa_rethrow() with no arguments
			// System V AMD64 calling convention - no arguments needed
			
			emitSubRSP(8); // Align stack to 16 bytes before call
			emitCall("__cxa_rethrow");
			// Note: __cxa_rethrow never returns
			
		} else {
			// ========== Windows/COFF (MSVC ABI) ==========
			// Call _CxxThrowException(NULL, NULL) to rethrow current exception
			// Windows x64 calling convention: RCX, RDX
			
			// Allocate shadow space for Windows x64 calling convention
			// - Requires 32 bytes shadow space
			// - Stack must be 16-byte aligned at CALL instruction
			// Round up to 48 bytes to maintain 16-byte alignment
			emitSubRSP(48);
			
			// Set up arguments for _CxxThrowException to rethrow current exception
			// RCX (first argument) = NULL (rethrow current exception object)
			emitXorRegReg(X64Register::RCX);
			// RDX (second argument) = NULL (rethrow uses current throw info)
			emitXorRegReg(X64Register::RDX);
			
			emitCall("_CxxThrowException");
			// Note: _CxxThrowException never returns
		}
	}

	void finalizeSections() {
		// Emit global variables to .data or .bss sections FIRST
		// This creates the symbols that relocations will reference
		for (const auto& global : global_variables_) {
			writer.add_global_variable_data(StringTable::getStringView(global.name), global.size_in_bytes, 
			                                global.is_initialized, global.init_data);
		}

		// Emit vtables to .rdata section
		for (const auto& vtable : vtables_) {
			// Convert vector<string> to vector<string_view> for passing as span
			std::vector<std::string_view> func_symbols_sv;
			func_symbols_sv.reserve(vtable.function_symbols.size());
			for (const auto& sym : vtable.function_symbols) {
				func_symbols_sv.push_back(sym);
			}
			
			std::vector<std::string_view> base_class_names_sv;
			base_class_names_sv.reserve(vtable.base_class_names.size());
			for (const auto& name : vtable.base_class_names) {
				base_class_names_sv.push_back(name);
			}
			
			writer.add_vtable(StringTable::getStringView(vtable.vtable_symbol), func_symbols_sv, StringTable::getStringView(vtable.class_name), 
			                  base_class_names_sv, vtable.base_class_info, vtable.rtti_info);
		}

		// Now add pending global variable relocations (after symbols are created)
		for (const auto& reloc : pending_global_relocations_) {
			writer.add_text_relocation(reloc.offset, std::string(StringTable::getStringView(reloc.symbol_name)), reloc.type, reloc.addend);
		}

		// Patch all pending branches before finalizing
		patchBranches();

		// Finalize the last function (if any) since there's no subsequent handleFunctionDecl to trigger it
		if (current_function_name_.isValid()) {
			// Calculate actual stack space needed from scope_stack_space (which includes varargs area if present)
			// scope_stack_space is negative (offset from RBP), so negate to get positive size
			size_t total_stack = static_cast<size_t>(-variable_scopes.back().scope_stack_space);
			
			// Stack alignment: After PUSH RBP, RSP is 16-aligned.
			// SUB RSP, N must keep it 16-aligned for subsequent CALLs.
			// Both Linux and Windows: Align total_stack to 16n (multiple of 16)
			if (total_stack % 16 != 0) {
				total_stack = (total_stack + 15) & ~15;  // Round up to next 16n
			}
			
			// Patch the SUB RSP immediate at prologue offset + 3
			if (current_function_prologue_offset_ > 0) {
				uint32_t patch_offset = current_function_prologue_offset_ + 3;
				const auto bytes = std::bit_cast<std::array<char, 4>>(static_cast<uint32_t>(total_stack));
				for (int i = 0; i < 4; i++) {
					textSectionData[patch_offset + i] = bytes[i];
				}
			}
			
			uint32_t function_length = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;

			// Update function length
			writer.update_function_length(std::string(StringTable::getStringView(current_function_name_)), function_length);

			// Set debug range to match reference exactly
			if (function_length > 13) {
				//writer.set_function_debug_range(current_function_name_, 8, 5); // prologue=8, epilogue=3
			}

			// Add exception handling information (required for x64) - uses mangled name
			auto [try_blocks, unwind_map] = convertExceptionInfoToWriterFormat();
			if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
				writer.add_function_exception_info(StringTable::getStringView(current_function_mangled_name_), current_function_offset_, function_length, try_blocks, unwind_map, current_function_cfi_);
			} else {
				writer.add_function_exception_info(StringTable::getStringView(current_function_mangled_name_), current_function_offset_, function_length, try_blocks, unwind_map);
			}

			// Clear the current function state
			current_function_name_ = StringHandle();
			current_function_offset_ = 0;
		}

		writer.add_data(textSectionData, SectionType::TEXT);

		// Finalize debug information
		writer.finalize_debug_info();
	}

	// Emit runtime helper functions for dynamic_cast as native x64 code
	void emit_dynamic_cast_runtime_helpers() {
		// Emit both __dynamic_cast_check and __dynamic_cast_throw_bad_cast functions
		// These are auto-generated as native x64 machine code
		emit_dynamic_cast_check_function();
		emit_dynamic_cast_throw_function();
	}

	// Emit __dynamic_cast_check function
	//   bool __dynamic_cast_check(type_info* source, type_info* target)
	// Platform-specific implementation:
	//   - Windows: MSVC RTTI with Complete Object Locator format (RCX, RDX)
	//   - Linux: Itanium C++ ABI type_info structures (RDI, RSI)
	// Returns: AL = 1 if cast valid, 0 otherwise
	void emit_dynamic_cast_check_function() {
		// Record function start
		uint32_t function_offset = static_cast<uint32_t>(textSectionData.size());
		
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// ========== Linux/ELF: Itanium C++ ABI type_info implementation ==========
			// Parameters: RDI = source type_info, RSI = target type_info
			// Returns: AL = 1 if cast valid, 0 otherwise
			
			// Simple implementation for Itanium ABI:
			// - Check if source == target (pointer equality)
			// - For SI/VMI classes, check base class (at offset 16 for SIClassTypeInfo)
			
			// Function prologue - save non-volatile registers (RBX for base pointer check)
			emitPushReg(X64Register::RBX);
			
			// Null check: if (!source || !target) return false
			emitTestRegReg(X64Register::RDI);  // TEST RDI, RDI
			size_t null_source_jmp = textSectionData.size();
			textSectionData.push_back(0x74);  // JZ rel8
			textSectionData.push_back(0x00);  // Placeholder
			
			emitTestRegReg(X64Register::RSI);  // TEST RSI, RSI
			size_t null_target_jmp = textSectionData.size();
			textSectionData.push_back(0x74);  // JZ rel8
			textSectionData.push_back(0x00);  // Placeholder
			
			// Pointer equality: if (source == target) return true
			emitCmpRegReg(X64Register::RDI, X64Register::RSI);  // CMP RDI, RSI
			size_t eq_check_jmp = textSectionData.size();
			textSectionData.push_back(0x74);  // JE rel8
			textSectionData.push_back(0x00);  // Placeholder
			
			// Check if source has base class (ItaniumSIClassTypeInfo has base at offset 16)
			// Load potential base class pointer from source+16 into RBX
			emitMovRegFromMemRegDisp8(X64Register::RBX, X64Register::RDI, 16);  // MOV RBX, [RDI+16]
			
			// Check if base pointer is null
			emitTestRegReg(X64Register::RBX);  // TEST RBX, RBX
			size_t no_base_jmp = textSectionData.size();
			textSectionData.push_back(0x74);  // JZ rel8
			textSectionData.push_back(0x00);  // Placeholder
			
			// Compare base with target: if (base == target) return true
			emitCmpRegReg(X64Register::RBX, X64Register::RSI);  // CMP RBX, RSI
			size_t base_eq_jmp = textSectionData.size();
			textSectionData.push_back(0x74);  // JE rel8
			textSectionData.push_back(0x00);  // Placeholder
			
			// return_false:
			size_t return_false = textSectionData.size();
			emitXorRegReg(X64Register::RAX);  // XOR RAX, RAX (AL = 0)
			emitPopReg(X64Register::RBX);
			emitRet();
			
			// return_true:
			size_t return_true = textSectionData.size();
			emitMovRegImm8(X64Register::RAX, 1);  // MOV AL, 1
			emitPopReg(X64Register::RBX);
			emitRet();
			
			// Patch jump offsets
			textSectionData[null_source_jmp + 1] = static_cast<uint8_t>(return_false - null_source_jmp - 2);
			textSectionData[null_target_jmp + 1] = static_cast<uint8_t>(return_false - null_target_jmp - 2);
			textSectionData[eq_check_jmp + 1] = static_cast<uint8_t>(return_true - eq_check_jmp - 2);
			textSectionData[no_base_jmp + 1] = static_cast<uint8_t>(return_false - no_base_jmp - 2);
			textSectionData[base_eq_jmp + 1] = static_cast<uint8_t>(return_true - base_eq_jmp - 2);
			
		} else {
			// ========== Windows/COFF: MSVC RTTI Complete Object Locator implementation ==========
			// Parameters: RCX = source_col, RDX = target_col
			// Returns: AL = 1 if cast valid, 0 otherwise
			
			// Function prologue - save non-volatile registers
			emitPushReg(X64Register::RBX);  // Save RBX
			emitPushReg(X64Register::RSI);  // Save RSI (will use for loop counter)
			emitPushReg(X64Register::RDI);  // Save RDI (will use for base pointer)
			emitSubRSP(32);  // Shadow space for recursive calls
		
		// Null check: if (!source_col || !target_col) return false
		emitTestRegReg(X64Register::RCX);  // TEST RCX, RCX
		emitJumpIfZero(5);  // JZ to next null check
		
		emitTestRegReg(X64Register::RDX);  // TEST RDX, RDX
		size_t null_check_to_false = textSectionData.size();
		emitJumpIfZero(0);  // JZ -> return_false (will patch offset)
		
		// COL pointer equality check: if (source_col == target_col) return true
		emitCmpRegReg(X64Register::RCX, X64Register::RDX);  // CMP RCX, RDX
		size_t ptr_eq_to_true = textSectionData.size();
		emitJumpIfEqual(0);  // JE -> return_true (will patch offset)
		
		// Get type descriptors from COLs
		// source_type_desc = source_col->type_descriptor (at offset 12)
		// target_type_desc = target_col->type_descriptor (at offset 12)
		emitMovRegFromMemRegDisp8(X64Register::R8, X64Register::RCX, 12);   // MOV R8, [RCX+12] (source type_desc)
		emitMovRegFromMemRegDisp8(X64Register::R9, X64Register::RDX, 12);   // MOV R9, [RDX+12] (target type_desc)
		
		// Type descriptor pointer equality: if (source_type_desc == target_type_desc) return true
		emitCmpRegReg(X64Register::R8, X64Register::R9);  // CMP R8, R9
		size_t type_desc_eq_to_true = textSectionData.size();
		emitJumpIfEqual(0);  // JE -> return_true (will patch offset)
		
		// Get class hierarchy from source COL
		// source_hierarchy = source_col->hierarchy (at offset 20)
		emitMovRegFromMemRegDisp8(X64Register::R10, X64Register::RCX, 20);  // MOV R10, [RCX+20] (source hierarchy)
		
		// Null check on hierarchy
		emitTestRegReg(X64Register::R10);  // TEST R10, R10
		size_t null_hierarchy_to_false = textSectionData.size();
		emitJumpIfZero(0);  // JZ -> return_false (will patch offset)
		
		// Get num_base_classes from hierarchy (at offset 8)
		emitMovRegFromMemRegDisp8(X64Register::RBX, X64Register::R10, 8);  // MOV RBX, [R10+8] (num_base_classes)
		
		// Validate num_bases <= 64 (buffer overflow protection)
		emitCmpRegImm32(X64Register::RBX, 64);  // CMP RBX, 64
		size_t overflow_to_false = textSectionData.size();
		emitJumpIfAbove(0);  // JA -> return_false (will patch offset)
		
		// Check if num_bases == 0 (no base classes, should not happen but check anyway)
		emitTestRegReg(X64Register::RBX);  // TEST RBX, RBX
		size_t no_bases_to_false = textSectionData.size();
		emitJumpIfZero(0);  // JZ -> return_false (will patch offset)
		
		// Get base class array from hierarchy (at offset 12)
		emitMovRegFromMemRegDisp8(X64Register::R11, X64Register::R10, 12);  // MOV R11, [R10+12] (base_class_array)
		
		// Null check on base class array
		emitTestRegReg(X64Register::R11);  // TEST R11, R11
		size_t null_bca_to_false = textSectionData.size();
		emitJumpIfZero(0);  // JZ -> return_false (will patch offset)
		
		// Initialize loop counter: RSI = 0
		emitXorRegReg(X64Register::RSI);  // XOR RSI, RSI (RSI = 0)
		
		// loop_start: iterate through base class descriptors
		size_t loop_start = textSectionData.size();
		
		// Get base_class_descriptor pointer: [R11 + RSI*8]
		emitLeaRegScaledIndex(X64Register::RDI, X64Register::R11, X64Register::RSI, 8, 0);
		emitMovRegFromMemReg(X64Register::RDI, X64Register::RDI);  // MOV RDI, [RDI] (load BCD pointer)
		
		// Null check on BCD
		emitTestRegReg(X64Register::RDI);  // TEST RDI, RDI
		size_t null_bcd_skip = textSectionData.size();
		emitJumpIfZero(0);  // JZ -> loop_continue (will patch offset to skip this iteration)
		
		// Get type descriptor from BCD (at offset 0)
		emitMovRegFromMemReg(X64Register::RAX, X64Register::RDI);  // MOV RAX, [RDI] (base type_desc)
		
		// Compare with target type descriptor: if (base_type_desc == target_type_desc) return true
		emitCmpRegReg(X64Register::RAX, X64Register::R9);  // CMP RAX, R9 (target type_desc)
		size_t base_match_to_true = textSectionData.size();
		emitJumpIfEqual(0);  // JE -> return_true (will patch offset)
		
		// loop_continue:
		size_t loop_continue = textSectionData.size();
		
		// Patch null BCD skip jump
		int8_t skip_offset = static_cast<int8_t>(loop_continue - null_bcd_skip - 2);
		textSectionData[null_bcd_skip + 1] = static_cast<uint8_t>(skip_offset);
		
		// Increment loop counter and check
		emitIncReg(X64Register::RSI);  // INC RSI
		emitCmpRegReg(X64Register::RSI, X64Register::RBX);  // CMP RSI, RBX (counter vs num_bases)
		
		// JB loop_start (jump if below)
		int32_t loop_offset = static_cast<int32_t>(loop_start) - static_cast<int32_t>(textSectionData.size()) - 2;
		if (loop_offset < -128 || loop_offset > 127) {
			// Offset too large for int8, use two-step jump
			// 126 is the max safe value that fits in int8 range and allows for a subsequent longer jump
			// This fallback should not occur in practice with normal RTTI structures
			constexpr int8_t MAX_SHORT_JUMP = 126;
			emitJumpIfBelow(MAX_SHORT_JUMP);
		} else {
			emitJumpIfBelow(static_cast<int8_t>(loop_offset));
		}
		
		// return_false:
		size_t return_false = textSectionData.size();
		emitXorRegReg(X64Register::RAX);  // XOR RAX, RAX (AL = 0)
		emitAddRSP(32);  // Remove shadow space
		emitPopReg(X64Register::RDI);
		emitPopReg(X64Register::RSI);
		emitPopReg(X64Register::RBX);
		emitRet();
		
		// return_true:
		size_t return_true = textSectionData.size();
		emitMovRegImm8(X64Register::RAX, 1);  // MOV AL, 1
		emitAddRSP(32);  // Remove shadow space
		emitPopReg(X64Register::RDI);
		emitPopReg(X64Register::RSI);
		emitPopReg(X64Register::RBX);
		emitRet();
		
		// Patch jump offsets
		int8_t offset_to_false = static_cast<int8_t>(return_false - null_check_to_false - 2);
		textSectionData[null_check_to_false + 1] = static_cast<uint8_t>(offset_to_false);
		
		offset_to_false = static_cast<int8_t>(return_false - null_hierarchy_to_false - 2);
		textSectionData[null_hierarchy_to_false + 1] = static_cast<uint8_t>(offset_to_false);
		
		offset_to_false = static_cast<int8_t>(return_false - overflow_to_false - 2);
		textSectionData[overflow_to_false + 1] = static_cast<uint8_t>(offset_to_false);
		
		offset_to_false = static_cast<int8_t>(return_false - no_bases_to_false - 2);
		textSectionData[no_bases_to_false + 1] = static_cast<uint8_t>(offset_to_false);
		
		offset_to_false = static_cast<int8_t>(return_false - null_bca_to_false - 2);
		textSectionData[null_bca_to_false + 1] = static_cast<uint8_t>(offset_to_false);
		
		int8_t offset_to_true = static_cast<int8_t>(return_true - ptr_eq_to_true - 2);
		textSectionData[ptr_eq_to_true + 1] = static_cast<uint8_t>(offset_to_true);
		
		offset_to_true = static_cast<int8_t>(return_true - type_desc_eq_to_true - 2);
		textSectionData[type_desc_eq_to_true + 1] = static_cast<uint8_t>(offset_to_true);
		
		offset_to_true = static_cast<int8_t>(return_true - base_match_to_true - 2);
		textSectionData[base_match_to_true + 1] = static_cast<uint8_t>(offset_to_true);
		
		}  // end Windows/COFF implementation
		
		// Calculate function length
		uint32_t function_length = static_cast<uint32_t>(textSectionData.size()) - function_offset;
		
		// Add function symbol (extern "C" linkage - no name mangling)
		writer.add_function_symbol("__dynamic_cast_check", function_offset, 0, Linkage::C);
		writer.update_function_length("__dynamic_cast_check", function_length);
	}

	// Emit __dynamic_cast_throw_bad_cast function
	//   [[noreturn]] void __dynamic_cast_throw_bad_cast()
	// This function throws std::bad_cast exception via C++ runtime
	void emit_dynamic_cast_throw_function() {
		// Record function start
		uint32_t function_offset = static_cast<uint32_t>(textSectionData.size());
		
		// Proper C++ exception throwing via MSVC runtime
		// Call _CxxThrowException with bad_cast exception object
		
		// For a complete implementation with C++ exception support:
		// 1. We would allocate a std::bad_cast object
		// 2. Call _CxxThrowException(exception_object, throw_info)
		// 3. Link with C++ runtime libraries
		
		// Current implementation: Call a stub that triggers std::terminate
		// This ensures the program doesn't continue with invalid state
		
		// For MSVC compatibility, we'd do:
		// SUB RSP, 40  (shadow space + alignment)
		emitSubRSP(40);
		
		// XOR ECX, ECX  (nullptr for exception object - will call terminate)
		emitXorRegReg(X64Register::RCX);
		
		// XOR EDX, EDX  (nullptr for throw info - will call terminate)
		emitXorRegReg(X64Register::RDX);
		
		// CALL _CxxThrowException (or std::terminate if not linked)
		// For header-only implementation, we'll use an infinite loop
		// which satisfies [[noreturn]] and prevents undefined behavior
		
		// ADD RSP, 40  (cleanup - though we never return)
		emitAddRSP(40);
		
		// Infinite loop (satisfies [[noreturn]])
		// loop: JMP loop
		emitJumpUnconditional(-2);  // JMP $-2 (jump to self)
		
		// Calculate function length
		uint32_t function_length = static_cast<uint32_t>(textSectionData.size()) - function_offset;
		
		// Add function symbol (extern "C" linkage - no name mangling)
		writer.add_function_symbol("__dynamic_cast_throw_bad_cast", function_offset, 0, Linkage::C);
		writer.update_function_length("__dynamic_cast_throw_bad_cast", function_length);
	}

	void patchBranches() {
		// Patch all pending branch instructions with correct offsets
		for (const auto& branch : pending_branches_) {
			auto label_it = label_positions_.find(branch.target_label);
			if (label_it == label_positions_.end()) {
				// Phase 5: Convert StringHandle to string_view for logging
				FLASH_LOG(Codegen, Error, "Label not found: ", StringTable::getStringView(branch.target_label));
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
		if (current_function_name_.isValid()) {
			uint32_t code_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_ + manual_offset;
			writer.add_line_mapping(code_offset, line_number);
		}
	}

	// Debug helper to log assembly instruction emission
	void logAsmEmit(const char* context, const uint8_t* bytes, size_t count) {
		if (!FLASH_LOG_ENABLED(Codegen, Debug)) return;
		
		std::string hex_bytes;
		for (size_t i = 0; i < count; ++i) {
			char buf[8];
			snprintf(buf, sizeof(buf), "%02X ", bytes[i]);
			hex_bytes += buf;
		}
		std::string msg = std::string("[ASM] ") + context + ": " + hex_bytes;
		FLASH_LOG(Codegen, Debug, msg);
	}

	TWriterClass writer;
	std::vector<char> textSectionData;
	std::unordered_map<std::string, uint32_t> functionSymbols;
	std::unordered_map<std::string, std::span<const IrInstruction>> function_spans;

	RegisterAllocator regAlloc;

	// Debug information tracking
	StringHandle current_function_name_;
	StringHandle current_function_mangled_name_;  // Changed from string_view to prevent dangling pointer
	uint32_t current_function_offset_ = 0;
	bool current_function_is_variadic_ = false;
	bool current_function_has_hidden_return_param_ = false;  // True if function uses hidden return parameter (RVO)
	bool current_function_returns_reference_ = false;  // True if function returns a reference (lvalue or rvalue)
	int32_t current_function_varargs_reg_save_offset_ = 0;  // Offset of varargs register save area (Linux only)
	
	// CFI instruction tracking for exception handling
	std::vector<ElfFileWriter::CFIInstruction> current_function_cfi_;

	// Pending function info for exception handling
	struct PendingFunctionInfo {
		StringHandle name;
		uint32_t offset;
		uint32_t length;
	};
	std::vector<PendingFunctionInfo> pending_functions_;
	std::vector<StackVariableScope> variable_scopes;

	// Control flow tracking - Phase 5: Use StringHandle for efficient label tracking
	struct PendingBranch {
		StringHandle target_label;
		uint32_t patch_position; // Position in textSectionData where the offset needs to be written
	};
	std::unordered_map<StringHandle, uint32_t> label_positions_;
	std::vector<PendingBranch> pending_branches_;

	// Loop context tracking for break/continue - Phase 5: Use StringHandle
	struct LoopContext {
		StringHandle loop_end_label;       // Label to jump to for break
		StringHandle loop_increment_label; // Label to jump to for continue
	};
	std::vector<LoopContext> loop_context_stack_;

	// Global variable tracking
	struct GlobalVariableInfo {
		StringHandle name;
		Type type;
		size_t size_in_bytes;
		bool is_initialized;
		std::vector<char> init_data;  // Raw bytes for initialized data
	};
	std::vector<GlobalVariableInfo> global_variables_;
	
	// Helper function to check if a variable is a global/static local variable
	bool isGlobalVariable(StringHandle name) const {
		for (const auto& global : global_variables_) {
			if (global.name == name) {
				return true;
			}
		}
		return false;
	}

	// VTable tracking
	struct VTableInfo {
		StringHandle vtable_symbol;  // e.g., "??_7Base@@6B@" or "_ZTV4Base"
		StringHandle class_name;
		std::vector<std::string> function_symbols;  // Mangled function names in vtable order
		std::vector<std::string> base_class_names;  // Base class names for RTTI (legacy)
		std::vector<ObjectFileWriter::BaseClassDescriptorInfo> base_class_info; // Detailed base class info for RTTI
		const RTTITypeInfo* rtti_info;  // Pointer to RTTI information for this class (nullptr if not polymorphic)
	};
	std::vector<VTableInfo> vtables_;

	// Pending global variable relocations (added after symbols are created)
	struct PendingGlobalRelocation {
		uint64_t offset;
		StringHandle symbol_name;
		uint32_t type;
		int64_t addend = -4;  // Default addend for PC-relative relocations
	};
	std::vector<PendingGlobalRelocation> pending_global_relocations_;

	// Track which stack offsets hold references (parameters or locals)
	std::unordered_map<int32_t, ReferenceInfo> reference_stack_info_;
	// Map from variable names to their offsets (for reference lookup by name)
	std::unordered_map<std::string, int32_t> variable_name_to_offset_;
	// Track TempVar sizes from instructions that produce them (for correct loads in conditionals)
	std::unordered_map<StringHandle, int> temp_var_sizes_;

	// Track if dynamic_cast runtime helpers need to be emitted
	bool needs_dynamic_cast_runtime_ = false;

	// Track most recently allocated named variable for TempVar linking
	StringHandle last_allocated_variable_name_;
	int32_t last_allocated_variable_offset_ = 0;

	// Prologue patching for stack allocation
	uint32_t current_function_prologue_offset_ = 0;  // Offset of SUB RSP instruction for patching
	size_t max_temp_var_index_ = 0;  // Highest TempVar number used (for stack size calculation)
	int next_temp_var_offset_ = 8;  // Next available offset for TempVar allocation (starts at 8, increments by 8)
	uint32_t current_function_named_vars_size_ = 0;  // Size of named vars + shadow space for current function

	// Exception handling tracking
	struct CatchHandler {
		TypeIndex type_index;  // Type index for user-defined types
		Type exception_type;   // Type enum for built-in types (Int, Double, etc.)
		uint32_t handler_offset;  // Code offset of catch handler
		int32_t catch_obj_stack_offset;  // Pre-computed stack offset for exception object
		bool is_catch_all;  // True for catch(...)
		bool is_const;  // True if caught by const
		bool is_reference;  // True if caught by lvalue reference
		bool is_rvalue_reference;  // True if caught by rvalue reference
	};

	struct TryBlock {
		uint32_t try_start_offset;  // Code offset where try block starts
		uint32_t try_end_offset;  // Code offset where try block ends
		std::vector<CatchHandler> catch_handlers;  // Associated catch clauses
	};

	// Destructor unwinding support
	struct LocalObject {
		TempVar temp_var;  // Stack location of the object
		TypeIndex type_index;  // Type of the object (for finding destructor)
		int state_when_constructed;  // State number when object was constructed
		StringHandle destructor_name;  // Mangled name of the destructor (if known)
	};

	struct UnwindMapEntry {
		int to_state;  // State to transition to after unwinding
		StringHandle action;  // Name of destructor to call (or empty for no action)
	};

	std::vector<TryBlock> current_function_try_blocks_;  // Try blocks in current function
	TryBlock* current_try_block_ = nullptr;  // Currently active try block being processed
	bool inside_catch_handler_ = false;  // Tracks whether we're emitting code inside a catch handler (ELF).
	std::vector<LocalObject> current_function_local_objects_;  // Objects with destructors
	std::vector<UnwindMapEntry> current_function_unwind_map_;  // Unwind map for destructors
	int current_exception_state_ = -1;  // Current exception handling state number
}; // End of IrToObjConverter class
