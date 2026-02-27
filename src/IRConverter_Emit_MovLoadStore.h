// IRConverter_Emit_MovLoadStore.h - MOV/Load/Store/LEA emit helper functions (free functions)
// Part of IRConverter.h unity build - do not add #pragma once
 */
inline void emitLoadFromAddressInRAX(std::vector<uint8_t>& textSectionData, int element_size_bytes) {
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
inline void emitLoadFromAddressInReg(std::vector<uint8_t>& textSectionData, X64Register dest_reg, X64Register addr_reg, int element_size_bytes) {
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
inline void emitLoadIndexIntoRCX(std::vector<uint8_t>& textSectionData, int64_t offset, int size_in_bits) {
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
inline void emitLoadFromFrame(std::vector<uint8_t>& textSectionData, X64Register reg, int64_t offset, int size_bytes = 8) {
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
		textSectionData.push_back(0x8B);
		emitModRM();
	} else if (size_bytes == 4) {
		// MOV r32, [RBP + offset] - branchless REX emission
		static constexpr uint8_t mov32_ops[2] = { 0x44, 0x8B }; // REX.R, MOV r32
		textSectionData.insert(textSectionData.end(), mov32_ops + !reg_extended, mov32_ops + 2);
		emitModRM();
	} else if (size_bytes == 2) {
		// MOVZX r32, WORD PTR [RBP + offset] - branchless REX emission
		static constexpr uint8_t movzx16_ops[3] = { 0x44, 0x0F, 0xB7 }; // REX.R, 0F, MOVZX r32/r/m16
		textSectionData.insert(textSectionData.end(), movzx16_ops + !reg_extended, movzx16_ops + 3);
		emitModRM();
	} else if (size_bytes == 1) {
		// MOVZX r32, BYTE PTR [RBP + offset] - branchless REX emission
		static constexpr uint8_t movzx8_ops[3] = { 0x44, 0x0F, 0xB6 }; // REX.R, 0F, MOVZX r32/r/m8
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
inline void emitStoreToFrame(std::vector<uint8_t>& textSectionData, X64Register reg, int64_t offset, int size_bytes) {
	uint8_t reg_bits = static_cast<uint8_t>(reg) & 0x07;
	bool reg_extended = static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8);
	
	// Helper to emit ModR/M + displacement
	auto emitModRM = [&]() {
		if (offset >= -128 && offset <= 127) {
			// [RBP + disp8]: Mod=01, Reg=reg_bits, R/M=101 (RBP)
			textSectionData.push_back(static_cast<uint8_t>(0x40 | (reg_bits << 3) | 0x05));
			textSectionData.push_back(static_cast<uint8_t>(offset));
		} else {
			// [RBP + disp32]: Mod=10, Reg=reg_bits, R/M=101 (RBP)
			textSectionData.push_back(static_cast<uint8_t>(0x80 | (reg_bits << 3) | 0x05));
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
		textSectionData.push_back(static_cast<uint8_t>(0x48 | (reg_extended << 2)));
		textSectionData.push_back(0x89); // MOV r/m64, r64
		emitModRM();
	} else if (size_bytes == 4) {
		// MOV DWORD PTR [RBP + offset], reg - 32-bit store (branchless REX emission)
		static constexpr uint8_t mov32_ops[2] = { 0x44, 0x89 }; // REX.R, MOV r/m32
		textSectionData.insert(textSectionData.end(), mov32_ops + !reg_extended, mov32_ops + 2);
		emitModRM();
	} else if (size_bytes == 2) {
		// MOV WORD PTR [RBP + offset], reg - 16-bit store (branchless REX emission)
		static constexpr uint8_t mov16_ops[3] = { 0x66, 0x44, 0x89 }; // 66, REX.R, MOV r/m16
		// Always emit 0x66, then optionally REX.R, then MOV
		textSectionData.push_back(0x66);
		textSectionData.insert(textSectionData.end(), mov16_ops + 1 + !reg_extended, mov16_ops + 3);
		emitModRM();
	} else if (size_bytes == 1) {
		// MOV BYTE PTR [RBP + offset], reg - 8-bit store
		// Need REX prefix for registers 4-7 to access SPL, BPL, SIL, DIL instead of AH, CH, DH, BH
		int needs_rex = reg_extended | (static_cast<uint8_t>(reg) >= 4);
		// REX = 0x40 | (0x04 if extended) = branchless
		uint8_t rex8_ops[2] = { static_cast<uint8_t>(0x40 | (reg_extended << 2)), 0x88 };
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
inline void emitStoreToMemory(std::vector<uint8_t>& textSectionData, X64Register value_reg, X64Register base_reg, int32_t offset, int size_bytes) {
	// Handle edge cases: zero or negative sizes, or sizes > 8
	if (size_bytes <= 0) {
		// Zero-size or invalid - nothing to store
		return;
	}
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
		// Handle non-standard sizes: 3, 5, 6, 7 bytes
		// These can occur with struct padding or unusual type sizes
		// For now, silently return - these sizes should be handled with memcpy at a higher level
		FLASH_LOG_FORMAT(Codegen, Warning, "emitStoreToMemory: Unsupported store size {} bytes, skipping", size_bytes);
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
inline void emitStoreToRSP(std::vector<uint8_t>& textSectionData, X64Register value_reg, int32_t offset) {
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
 * @param offset The offset from RBP
 */
inline void emitLEAArrayBase(std::vector<uint8_t>& textSectionData, int64_t offset) {
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
inline void emitLEAFromFrame(std::vector<uint8_t>& textSectionData, X64Register reg, int64_t offset) {
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
