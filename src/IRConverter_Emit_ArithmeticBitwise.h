// IRConverter_Emit_ArithmeticBitwise.h - Arithmetic and bitwise emit helper functions (free functions)
// Part of IRConverter.h unity build - do not add #pragma once

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
inline void emitMultiplyRCXByElementSize(std::vector<uint8_t>& textSectionData, int element_size_bytes) {
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
inline void emitMultiplyRegByElementSize(std::vector<uint8_t>& textSectionData, X64Register reg, int element_size_bytes) {
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
inline void emitAddRAXRCX(std::vector<uint8_t>& textSectionData) {
	textSectionData.push_back(0x48); // REX.W prefix for 64-bit operation
	textSectionData.push_back(0x01); // ADD r/m64, r64
	textSectionData.push_back(0xC8); // ModR/M: RAX (dest), RCX (source)
}

/**
 * @brief Emits x64 opcodes for ADD dest_reg, src_reg.
 *
 * @param textSectionData The vector to append opcodes to
 * @param dest_reg The destination register
 * @param src_reg The source register to add
 */
inline void emitAddRegs(std::vector<uint8_t>& textSectionData, X64Register dest_reg, X64Register src_reg) {
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
inline void emitAddImmToReg(std::vector<uint8_t>& textSectionData, X64Register reg, int64_t imm) {
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
	uint8_t opcodes[2] = { 0x81, static_cast<uint8_t>(0xC0 | reg_bits) };
	opcodes[0] = is_rax ? 0x05 : 0x81;
	int opcode_size = 2 - is_rax; // 1 for RAX, 2 for others
	textSectionData.insert(textSectionData.end(), opcodes, opcodes + opcode_size);
	
	uint32_t imm_u32 = static_cast<uint32_t>(imm);
	textSectionData.push_back(imm_u32 & 0xFF);
	textSectionData.push_back((imm_u32 >> 8) & 0xFF);
	textSectionData.push_back((imm_u32 >> 16) & 0xFF);
	textSectionData.push_back((imm_u32 >> 24) & 0xFF);
}
