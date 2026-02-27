// IRConverter_Encoding.h - x86-64 opcode byte encoding helpers and generate* functions
// Part of IRConverter.h unity build - do not add #pragma once
struct OpCodeWithSize {
	std::array<uint8_t, MAX_MOV_INSTRUCTION_SIZE> op_codes{};  // Zero-initialize
	size_t size_in_bytes = 0;
};

/// Calculate the ModR/M mod field for RBP-relative addressing.
/// RBP always needs at least a disp8 (even for offset 0), so mod=0x00 is never used.
inline uint8_t calcModField(int32_t offset) {
	return (offset >= -128 && offset <= 127) ? 0x01 : 0x02;
}

/// Encode displacement bytes into the instruction buffer.
/// For disp8 (mod=0x01): 1 byte. For disp32 (mod=0x02): 4 bytes little-endian.
inline void encodeDisplacement(uint8_t*& ptr, size_t& size, int32_t offset, uint8_t mod_field) {
	if (mod_field == 0x01) {
		*ptr++ = static_cast<uint8_t>(offset);
		size++;
	} else {
		*ptr++ = static_cast<uint8_t>(offset & 0xFF);
		*ptr++ = static_cast<uint8_t>((offset >> 8) & 0xFF);
		*ptr++ = static_cast<uint8_t>((offset >> 16) & 0xFF);
		*ptr++ = static_cast<uint8_t>((offset >> 24) & 0xFF);
		size += 4;
	}
}

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
 * @brief Generates a properly encoded SSE instruction with an optional prefix byte.
 * 
 * Unified SSE instruction encoder that handles all three encoding forms:
 * - With mandatory prefix (F3/F2): prefix [REX] 0F opcode ModR/M  (e.g., addss, addsd)
 * - Without prefix:                [REX] 0F opcode ModR/M          (e.g., comiss)
 * - With 0x66 override:            66 [REX] 0F opcode ModR/M       (e.g., comisd)
 * 
 * REX prefix format: 0100WRXB where:
 * - W is 0 for most SSE ops (legacy SSE, not 64-bit extension)
 * - R extends the ModR/M reg field (for xmm_dst >= XMM8)
 * - X extends the SIB index field (not used for reg-reg ops)
 * - B extends the ModR/M r/m field (for xmm_src >= XMM8)
 * 
 * @param prefix Optional prefix byte (0xF3, 0xF2, 0x66, or 0 for none)
 * @param opcode1 First opcode byte (usually 0x0F for two-byte opcodes)
 * @param opcode2 Second opcode byte (e.g., 0x58 for addss/addsd, 0x2F for comiss/comisd)
 * @param xmm_dst Destination XMM register
 * @param xmm_src Source XMM register
 * @return An OpCodeWithSize struct containing the instruction bytes
 */
inline OpCodeWithSize generateSSEInstructionWithPrefix(uint8_t prefix, uint8_t opcode1, uint8_t opcode2, 
                                                        X64Register xmm_dst, X64Register xmm_src) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;
	uint8_t* ptr = result.op_codes.data();
	
	uint8_t dst_index = xmm_modrm_bits(xmm_dst);
	uint8_t src_index = xmm_modrm_bits(xmm_src);
	
	bool needs_rex = (dst_index >= 8) || (src_index >= 8);
	
	// Emit prefix byte if present (comes before REX)
	if (prefix != 0) {
		*ptr++ = prefix;
		result.size_in_bytes++;
	}
	
	// REX prefix comes after any prefix but before opcode bytes
	if (needs_rex) {
		uint8_t rex = REX_BASE;
		if (dst_index >= 8) rex |= 0x04;  // REX.R
		if (src_index >= 8) rex |= 0x01;  // REX.B
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

// Convenience wrappers for backward compatibility
inline OpCodeWithSize generateSSEInstruction(uint8_t prefix1, uint8_t opcode1, uint8_t opcode2, 
                                              X64Register xmm_dst, X64Register xmm_src) {
	return generateSSEInstructionWithPrefix(prefix1, opcode1, opcode2, xmm_dst, xmm_src);
}

inline OpCodeWithSize generateSSEInstructionNoPrefix(uint8_t opcode1, uint8_t opcode2, 
                                                      X64Register xmm_dst, X64Register xmm_src) {
	return generateSSEInstructionWithPrefix(0, opcode1, opcode2, xmm_dst, xmm_src);
}

inline OpCodeWithSize generateSSEInstructionDouble(uint8_t opcode1, uint8_t opcode2, 
                                                    X64Register xmm_dst, X64Register xmm_src) {
	return generateSSEInstructionWithPrefix(0x66, opcode1, opcode2, xmm_dst, xmm_src);
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

	uint8_t mod_field = calcModField(offset);

	// RBP encoding is 0x05, no SIB needed for RBP-relative addressing
	modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05; // 0x05 for RBP
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- Displacement ---
	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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

	uint8_t mod_field = calcModField(offset);

	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05;
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// Displacement
	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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
	uint8_t mod_field = calcModField(offset);
	uint8_t modrm = static_cast<uint8_t>((mod_field << 6) | (reg_bits << 3) | 0x05); // Base = RBP
	*current_byte_ptr++ = modrm;
	result.size_in_bytes++;

	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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
	uint8_t mod_field = calcModField(offset);

	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05;
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// Displacement
	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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
	uint8_t mod_field = calcModField(offset);

	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05;
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// Displacement
	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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
	uint8_t mod_field = calcModField(offset);

	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05;
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// Displacement
	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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
	uint8_t mod_field = calcModField(offset);

	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05;
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// Displacement
	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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
	uint8_t mod_field = calcModField(offset);

	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05;
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// Displacement
	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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
	uint8_t mod_field = calcModField(offset);
	uint8_t modrm = (mod_field << 6) | ((xmm_reg & 0x07) << 3) | 0x05; // Reg=XMM, R/M=101 (RBP)
	*current_byte_ptr++ = modrm;
	result.size_in_bytes++;

	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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
	uint8_t mod_field = calcModField(offset);
	uint8_t modrm = (mod_field << 6) | ((xmm_reg & 0x07) << 3) | 0x05; // Reg=XMM, R/M=101 (RBP)
	*current_byte_ptr++ = modrm;
	result.size_in_bytes++;

	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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

	uint8_t mod_field = calcModField(offset);

	// RBP encoding is 0x05, no SIB needed for RBP-relative addressing
	modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05; // 0x05 for RBP
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- Displacement ---
	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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

	uint8_t mod_field = calcModField(offset);

	// RBP encoding is 0x05, no SIB needed for RBP-relative addressing
	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05; // 0x05 for RBP
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- Displacement ---
	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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
	uint8_t mod_field = calcModField(offset);

	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05;
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// Displacement
	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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
	uint8_t mod_field = calcModField(offset);

	uint8_t modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05;
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// Displacement
	encodeDisplacement(current_byte_ptr, result.size_in_bytes, offset, mod_field);

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
inline void emitAddRegImm32(std::vector<uint8_t>& textSectionData, X64Register reg, int32_t immediate) {
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
