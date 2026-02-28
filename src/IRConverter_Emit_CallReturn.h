// IRConverter_Emit_CallReturn.h - Call/Return/Stack emit helper functions (free functions)
// Part of IRConverter.h unity build - do not add #pragma once

/**
 * @brief Emits PUSH reg instruction (branchless).
 * @param textSectionData The vector to append opcodes to
 * @param reg The register to push
 */
inline void emitPush(std::vector<uint8_t>& textSectionData, X64Register reg) {
	uint8_t reg_bits = static_cast<uint8_t>(reg) & 0x07;
	int reg_extended = static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8);
	// Branchless: emit [REX.B, PUSH+reg] and skip REX.B if not extended
	uint8_t opcodes[2] = { 0x41, static_cast<uint8_t>(0x50 + reg_bits) };
	textSectionData.insert(textSectionData.end(), opcodes + !reg_extended, opcodes + 2);
}

/**
 * @brief Emits POP reg instruction (branchless).
 * @param textSectionData The vector to append opcodes to
 * @param reg The register to pop into
 */
inline void emitPop(std::vector<uint8_t>& textSectionData, X64Register reg) {
	uint8_t reg_bits = static_cast<uint8_t>(reg) & 0x07;
	int reg_extended = static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8);
	// Branchless: emit [REX.B, POP+reg] and skip REX.B if not extended
	uint8_t opcodes[2] = { 0x41, static_cast<uint8_t>(0x58 + reg_bits) };
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
inline void emitCallReg(std::vector<uint8_t>& textSectionData, X64Register reg) {
	uint8_t reg_bits = static_cast<uint8_t>(reg) & 0x07;
	bool reg_extended = static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8);
	
	if (reg_extended) {
		textSectionData.push_back(0x41);  // REX.B prefix
	}
	textSectionData.push_back(0xFF);  // Opcode for CALL r/m64
	// ModR/M: mod=11 (register), reg=010 (/2), r/m=reg_bits
	textSectionData.push_back(static_cast<uint8_t>(0xD0 + reg_bits));
}
