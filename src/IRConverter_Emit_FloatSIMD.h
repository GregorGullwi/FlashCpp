// IRConverter_Emit_FloatSIMD.h - Floating-point and SIMD emit helper functions (free functions)
// Part of IRConverter.h unity build - do not add #pragma once

/**
 * @brief Emits floating-point load from address in a register.
 * Loads from [addr_reg] into xmm_dest using MOVSD (double) or MOVSS (float).
 *
 * @param textSectionData The vector to append opcodes to
 * @param xmm_dest The destination XMM register for the loaded value
 * @param addr_reg The general-purpose register containing the memory address to load from
 * @param is_float True for float (MOVSS), false for double (MOVSD)
 */
inline void emitFloatLoadFromAddressInReg(std::vector<uint8_t>& textSectionData, X64Register xmm_dest, X64Register addr_reg, bool is_float) {
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
inline void emitFloatLoadFromAddressWithOffset(std::vector<uint8_t>& textSectionData, X64Register xmm_dest, X64Register addr_reg, int32_t offset, bool is_float) {
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
inline void emitFloatStoreToAddressWithOffset(std::vector<uint8_t>& textSectionData, X64Register xmm_src, X64Register addr_reg, int32_t offset, bool is_float) {
	uint8_t xmm_bits = static_cast<uint8_t>(xmm_src) & 0x07;
	uint8_t addr_bits = static_cast<uint8_t>(addr_reg) & 0x07;
	bool addr_extended = static_cast<uint8_t>(addr_reg) >= static_cast<uint8_t>(X64Register::R8);
	bool xmm_extended = static_cast<uint8_t>(xmm_src) >= static_cast<uint8_t>(X64Register::XMM8);
	
	// MOVSD [addr_reg + disp], xmm: F2 [REX] 0F 11 modrm [disp]
	// MOVSS [addr_reg + disp], xmm: F3 [REX] 0F 11 modrm [disp]
	textSectionData.push_back(is_float ? 0xF3 : 0xF2);
	if (addr_extended || xmm_extended) {
		uint8_t rex = 0x40;
		if (xmm_extended) rex |= 0x04; // REX.R for extended XMM register
		if (addr_extended) rex |= 0x01; // REX.B for extended addr_reg
		textSectionData.push_back(rex);
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
inline void emitMovqXmmToGpr(std::vector<uint8_t>& textSectionData, X64Register xmm_src, X64Register gpr_dest) {
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
inline void emitMovqGprToXmm(std::vector<uint8_t>& textSectionData, X64Register gpr_src, X64Register xmm_dest) {
	textSectionData.push_back(0x66);
	textSectionData.push_back(0x48);
	textSectionData.push_back(0x0F);
	textSectionData.push_back(0x6E);
	uint8_t xmm_bits = static_cast<uint8_t>(xmm_dest) & 0x07;
	uint8_t gpr_bits = static_cast<uint8_t>(gpr_src) & 0x07;
	textSectionData.push_back(0xC0 | (xmm_bits << 3) | gpr_bits);
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
inline void emitFloatStoreToRSP(std::vector<uint8_t>& textSectionData, X64Register xmm_reg, int32_t offset, bool is_float) {
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
