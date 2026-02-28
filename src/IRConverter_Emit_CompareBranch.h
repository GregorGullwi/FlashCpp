// IRConverter_Emit_CompareBranch.h - Class-internal emit helper methods (compare, branch, mov, float, etc.)
// Included inside IrToObjConverter class body - do not add #pragma once
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

	// Helper to emit CMP dword [rbp+offset], imm32 for exception selector dispatch
	void emitCmpFrameImm32(int32_t frame_offset, int32_t imm_value) {
		// CMP dword [rbp+disp32], imm32: 81 BD <disp32> <imm32>
		textSectionData.push_back(0x81);
		textSectionData.push_back(0xBD);  // ModR/M: mod=10, reg=7(/7=CMP), rm=5(rbp)
		auto disp = std::bit_cast<std::array<uint8_t, 4>>(frame_offset);
		textSectionData.insert(textSectionData.end(), disp.begin(), disp.end());
		auto imm = std::bit_cast<std::array<uint8_t, 4>>(imm_value);
		textSectionData.insert(textSectionData.end(), imm.begin(), imm.end());
	}

	// Allocate an anonymous stack slot for ELF exception dispatch temporaries
	int32_t allocateElfTempStackSlot(int size_bytes) {
		size_bytes = (size_bytes + 7) & ~7;  // 8-byte align
		next_temp_var_offset_ += size_bytes;
		int32_t offset = -(static_cast<int32_t>(current_function_named_vars_size_) + next_temp_var_offset_);
		int32_t end_offset = offset;
		if (!variable_scopes.empty() && end_offset < variable_scopes.back().scope_stack_space) {
			variable_scopes.back().scope_stack_space = end_offset;
		}
		return offset;
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
		uint8_t dest_enc = static_cast<uint8_t>(destinationRegister);
		textSectionData.push_back(0x48 | (((dest_enc >> 3) & 0x01) << 2)); // REX.W | REX.R branchless
		textSectionData.push_back(0x8D); // LEA opcode
		
		// ModR/M byte: mod=00 (indirect), reg=destination, r/m=101 ([RIP+disp32])
		uint8_t dest_bits = dest_enc & 0x07;
		textSectionData.push_back(0x05 | (dest_bits << 3)); // ModR/M: [RIP + disp32]
		
		// Add placeholder for the displacement (will be filled by relocation)
		uint32_t relocation_offset = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		
		return relocation_offset;
	}

	// Helper to generate and emit MOV to frame with explicit size
	void emitMovToFrame(X64Register sourceRegister, int32_t offset, int size_in_bits) {
		auto opcodes = generateMovToFrameBySize(sourceRegister, offset, size_in_bits);
		
		// Only build debug string and log if Codegen is set to Debug or higher
		if (IS_FLASH_LOG_ENABLED(Codegen, Debug)) {
			std::string bytes_str;
			for (size_t i = 0; i < static_cast<size_t>(opcodes.size_in_bytes); i++) {
				bytes_str += std::format("{:02x} ", static_cast<uint8_t>(opcodes.op_codes[i]));
			}
			FLASH_LOG_FORMAT(Codegen, Debug, "emitMovToFrame: reg={} offset={} size_bits={} bytes={}", static_cast<int>(sourceRegister), offset, size_in_bits, bytes_str);
		}
		
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

	// Helper to emit MOVSS/MOVSD for XMM register-to-register moves
	void emitFloatMovRegToReg(X64Register xmm_dest, X64Register xmm_src, bool is_double) {
		uint8_t src_xmm_num = static_cast<uint8_t>(xmm_src) - static_cast<uint8_t>(X64Register::XMM0);
		uint8_t dst_xmm_num = static_cast<uint8_t>(xmm_dest) - static_cast<uint8_t>(X64Register::XMM0);
		textSectionData.push_back(is_double ? 0xF2 : 0xF3);
		// REX prefix needed when either register is XMM8-XMM15
		if (dst_xmm_num >= 8 || src_xmm_num >= 8) {
			uint8_t rex = 0x40;
			if (dst_xmm_num >= 8) rex |= 0x04;  // REX.R
			if (src_xmm_num >= 8) rex |= 0x01;  // REX.B
			textSectionData.push_back(rex);
		}
		textSectionData.push_back(0x0F);
		textSectionData.push_back(0x10);
		textSectionData.push_back(static_cast<uint8_t>(0xC0 | ((dst_xmm_num & 0x07) << 3) | (src_xmm_num & 0x07)));
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

	// Helper to emit AND reg, imm64 for bitfield masking
	void emitAndImm64(X64Register reg, uint64_t mask) {
		uint8_t reg_enc = static_cast<uint8_t>(reg);
		uint8_t rex = 0x48 | ((reg_enc >> 3) & 0x01); // REX.W + REX.B branchless
		if (mask <= 0x7F) {
			// AND r/m64, imm8 (sign-extended)
			textSectionData.push_back(rex);
			textSectionData.push_back(0x83);
			textSectionData.push_back(0xE0 | (reg_enc & 0x07));
			textSectionData.push_back(static_cast<uint8_t>(mask));
		} else if (mask <= 0x7FFFFFFF) {
			// AND r/m64, imm32 (sign-extended)
			textSectionData.push_back(rex);
			textSectionData.push_back(0x81);
			textSectionData.push_back(0xE0 | (reg_enc & 0x07));
			uint32_t m = static_cast<uint32_t>(mask);
			textSectionData.push_back(m & 0xFF);
			textSectionData.push_back((m >> 8) & 0xFF);
			textSectionData.push_back((m >> 16) & 0xFF);
			textSectionData.push_back((m >> 24) & 0xFF);
		} else if (mask >= 0xFFFFFFFFFFFFFF80ULL) {
			// AND r/m64, imm8 (sign-extended negative, e.g. 0xFFFFFFFFFFFFFFF8 -> imm8=0xF8)
			textSectionData.push_back(rex);
			textSectionData.push_back(0x83);
			textSectionData.push_back(0xE0 | (reg_enc & 0x07));
			textSectionData.push_back(static_cast<uint8_t>(mask & 0xFF));
		} else if (mask >= 0xFFFFFFFF80000000ULL) {
			// AND r/m64, imm32 (sign-extended negative, e.g. 0xFFFFFFFF80000000 -> imm32=0x80000000)
			textSectionData.push_back(rex);
			textSectionData.push_back(0x81);
			textSectionData.push_back(0xE0 | (reg_enc & 0x07));
			uint32_t m = static_cast<uint32_t>(mask & 0xFFFFFFFF);
			textSectionData.push_back(m & 0xFF);
			textSectionData.push_back((m >> 8) & 0xFF);
			textSectionData.push_back((m >> 16) & 0xFF);
			textSectionData.push_back((m >> 24) & 0xFF);
		} else {
			// Full 64-bit: MOV scratch, imm64; AND reg, scratch
			X64Register scratch = (reg == X64Register::RAX) ? X64Register::RCX : X64Register::RAX;
			uint8_t scratch_enc = static_cast<uint8_t>(scratch);
			// Save scratch if it might be in use - use a simple push/pop
			textSectionData.push_back(0x50 + (scratch_enc & 0x07)); // PUSH scratch
			emitMovImm64(scratch, mask);
			uint8_t rex2 = 0x48 | (((scratch_enc >> 3) & 0x01) << 2) | ((reg_enc >> 3) & 0x01); // REX.W + REX.R(scratch) + REX.B(reg) branchless
			textSectionData.push_back(rex2);
			textSectionData.push_back(0x21); // AND r/m64, r64
			textSectionData.push_back(0xC0 | ((scratch_enc & 0x07) << 3) | (reg_enc & 0x07));
			textSectionData.push_back(0x58 + (scratch_enc & 0x07)); // POP scratch
		}
	}

	// Helper to emit SHL reg, imm8 for bitfield shifting
	void emitShlImm(X64Register reg, uint8_t shift_amount) {
		uint8_t reg_enc = static_cast<uint8_t>(reg);
		textSectionData.push_back(0x48 | ((reg_enc >> 3) & 0x01)); // REX.W + REX.B branchless
		textSectionData.push_back(0xC1); // SHL r/m64, imm8
		textSectionData.push_back(0xE0 | (reg_enc & 0x07));
		textSectionData.push_back(shift_amount);
	}

	// Helper to emit OR dest, src for bitfield combining
	void emitOrReg(X64Register dest, X64Register src) {
		uint8_t dest_enc = static_cast<uint8_t>(dest);
		uint8_t src_enc = static_cast<uint8_t>(src);
		textSectionData.push_back(0x48 | (((src_enc >> 3) & 0x01) << 2) | ((dest_enc >> 3) & 0x01)); // REX.W + REX.R + REX.B branchless
		textSectionData.push_back(0x09); // OR r/m64, r64
		textSectionData.push_back(0xC0 | ((src_enc & 0x07) << 3) | (dest_enc & 0x07));
	}

	// Helper to emit SHR reg, imm8 for bitfield extraction
	void emitShrImm(X64Register reg, uint8_t shift_amount) {
		uint8_t reg_enc = static_cast<uint8_t>(reg);
		textSectionData.push_back(0x48 | ((reg_enc >> 3) & 0x01)); // REX.W + REX.B branchless
		textSectionData.push_back(0xC1); // SHR r/m64, imm8
		textSectionData.push_back(0xE8 | (reg_enc & 0x07));
		textSectionData.push_back(shift_amount);
	}

	// Compute the mask for a bitfield of the given width in bits.
	static uint64_t bitfieldMask(size_t width) {
		return (width < 64) ? ((1ULL << width) - 1) : ~0ULL;
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

	// Helper to emit size-aware MOV/MOVZX for dereferencing: dest = [src_addr]
	// Handles 8-bit (MOVZX), 16-bit, 32-bit, and 64-bit loads
	// Correctly handles RBP/R13 and RSP/R12 special cases
	void emitMovRegFromMemRegSized(X64Register dest, X64Register src_addr, int size_in_bits) {
		uint8_t src_encoding = static_cast<uint8_t>(src_addr) & 0x07;
		bool needs_disp = (src_encoding == 0x05);  // RBP or R13
		bool needs_sib = (src_encoding == 0x04);   // RSP or R12
		uint8_t mod_field = needs_disp ? 0x01 : 0x00;
		
		if (size_in_bits == 8) {
			// MOVZX dest, byte ptr [src_addr]
			// For 8-bit loads, always zero-extend into 32-bit register (dest will be RAX for simplicity in handleDereference)
			assert(dest == X64Register::RAX && "8-bit dereference should use RAX as destination");
			
			// REX prefix if src_addr is R8-R15
			if (static_cast<uint8_t>(src_addr) >= 8) {
				textSectionData.push_back(0x41); // REX with B bit
			}
			
			// MOVZX opcode: 0F B6
			textSectionData.push_back(0x0F);
			textSectionData.push_back(0xB6);
			
			// ModR/M: mod depends on disp, reg=0 (RAX/AL), r/m=src_addr
			uint8_t modrm = (mod_field << 6) | (0x00 << 3) | src_encoding;
			textSectionData.push_back(modrm);
			
			if (needs_sib) {
				textSectionData.push_back(0x24); // SIB for RSP/R12
			}
			if (needs_disp) {
				textSectionData.push_back(0x00); // disp8 = 0 for RBP/R13
			}
		} else if (size_in_bits == 16) {
			// MOV dest, word ptr [src_addr] - needs 0x66 prefix
			textSectionData.push_back(0x66); // Operand size override
			
			// REX prefix for extended registers
			uint8_t rex = 0x40;
			if (static_cast<uint8_t>(dest) >= 8) rex |= 0x04;  // R bit
			if (static_cast<uint8_t>(src_addr) >= 8) rex |= 0x01;  // B bit
			if (rex != 0x40) {
				textSectionData.push_back(rex);
			}
			
			textSectionData.push_back(0x8B); // MOV opcode
			
			uint8_t modrm = (mod_field << 6);
			modrm |= ((static_cast<uint8_t>(dest) & 0x07) << 3);
			modrm |= src_encoding;
			textSectionData.push_back(modrm);
			
			if (needs_sib) {
				textSectionData.push_back(0x24);
			}
			if (needs_disp) {
				textSectionData.push_back(0x00);
			}
		} else if (size_in_bits == 32) {
			// MOV dest, dword ptr [src_addr]
			uint8_t rex = 0x40;
			if (static_cast<uint8_t>(dest) >= 8) rex |= 0x04;  // R bit
			if (static_cast<uint8_t>(src_addr) >= 8) rex |= 0x01;  // B bit
			
			// Only emit REX if we need it for extended registers
			if (rex != 0x40) {
				textSectionData.push_back(rex);
			}
			
			textSectionData.push_back(0x8B); // MOV opcode
			
			uint8_t modrm = (mod_field << 6);
			modrm |= ((static_cast<uint8_t>(dest) & 0x07) << 3);
			modrm |= src_encoding;
			textSectionData.push_back(modrm);
			
			if (needs_sib) {
				textSectionData.push_back(0x24);
			}
			if (needs_disp) {
				textSectionData.push_back(0x00);
			}
		} else {
			// 64-bit (default): MOV dest, qword ptr [src_addr]
			uint8_t rex = 0x48; // REX.W for 64-bit
			if (static_cast<uint8_t>(dest) >= 8) rex |= 0x04;  // R bit
			if (static_cast<uint8_t>(src_addr) >= 8) rex |= 0x01;  // B bit
			
			textSectionData.push_back(rex);
			textSectionData.push_back(0x8B); // MOV opcode
			
			uint8_t modrm = (mod_field << 6);
			modrm |= ((static_cast<uint8_t>(dest) & 0x07) << 3);
			modrm |= src_encoding;
			textSectionData.push_back(modrm);
			
			if (needs_sib) {
				textSectionData.push_back(0x24);
			}
			if (needs_disp) {
				textSectionData.push_back(0x00);
			}
		}
	}

	// Helper to emit TEST reg, reg
	void emitTestRegReg(X64Register reg) {
		uint8_t rex = 0x48; // REX.W
		if (static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8)) {
			// TEST r/m64, r64 uses both ModR/M reg and r/m fields. For TEST reg,reg
			// with an extended register (R8-R15), set both REX.R and REX.B.
			rex |= 0x04; // REX.R
			rex |= 0x01; // REX.B
		}
		textSectionData.push_back(rex);
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
		// For extended registers (R8-R15), REX.R (0x44) must precede the 0F Bx opcode
		uint8_t needs_rex_r = (dest_val >> 3) & 0x01; // 1 if R8-R15, else 0
		if (size_in_bits <= 8) {
			// MOVZX r32, byte ptr [RIP + disp32]: [44] 0F B6 05+reg [disp32]
			size_t base = textSectionData.size();
			textSectionData.resize(base + 7 + needs_rex_r);
			uint8_t* p = textSectionData.data() + base;
			if (needs_rex_r) *p++ = 0x44; // REX.R for R8-R15
			p[0] = 0x0F;
			p[1] = 0xB6; // MOVZX r32, r/m8
			p[2] = 0x05 | (dest_bits << 3); // ModR/M: reg, [RIP + disp32]
			p[3] = 0x00; // disp32 placeholder
			p[4] = 0x00;
			p[5] = 0x00;
			p[6] = 0x00;
			return static_cast<uint32_t>(base + 3 + needs_rex_r);
		} else if (size_in_bits == 16) {
			// MOVZX r32, word ptr [RIP + disp32]: [44] 0F B7 05+reg [disp32]
			size_t base = textSectionData.size();
			textSectionData.resize(base + 7 + needs_rex_r);
			uint8_t* p = textSectionData.data() + base;
			if (needs_rex_r) *p++ = 0x44; // REX.R for R8-R15
			p[0] = 0x0F;
			p[1] = 0xB7; // MOVZX r32, r/m16
			p[2] = 0x05 | (dest_bits << 3); // ModR/M: reg, [RIP + disp32]
			p[3] = 0x00; // disp32 placeholder
			p[4] = 0x00;
			p[5] = 0x00;
			p[6] = 0x00;
			return static_cast<uint32_t>(base + 3 + needs_rex_r);
		}
		
		// For 32-bit and 64-bit, use regular MOV
		// For RIP-relative MOV, destination is in the REG field of ModR/M (R/M=101 is fixed),
		// so REX.R (bit 2 = 0x04) extends it — not REX.B (bit 0 = 0x01)
		uint8_t needs_rex_w = (size_in_bits == 64) ? 0x08 : 0x00;
		uint8_t rex_r_bit = needs_rex_r << 2; // shift flag to REX.R position (bit 2 = 0x04)
		uint8_t rex = 0x40 | needs_rex_w | rex_r_bit;
		// Only emit REX if any bits are set beyond base 0x40
		uint8_t emit_rex = (needs_rex_w | rex_r_bit) != 0;
		// Use reserve + direct writes for branchless emission
		size_t base = textSectionData.size();
		textSectionData.resize(base + 6 + emit_rex);
		uint8_t* p = textSectionData.data() + base;
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
		// For XMM8-XMM15: F2 44 0F 10 05 [disp32] (REX.R prefix needed)
		textSectionData.push_back(is_float ? 0xF3 : 0xF2);
		
		// REX prefix if XMM8-XMM15
		if (xmm_needs_rex(xmm_dest)) {
			textSectionData.push_back(0x44); // REX.R for XMM8-XMM15
		}
		
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
		// For 16-bit: MOV WORD PTR [RIP + disp32], AX - 66 [44] 89 05 [disp32]
		// For 8-bit:  MOV BYTE PTR [RIP + disp32], AL - [40|44] 88 05 [disp32]
		uint8_t src_val = static_cast<uint8_t>(src);
		uint8_t src_bits = src_val & 0x07;

		if (size_in_bits <= 8) {
			// MOV BYTE PTR [RIP + disp32], reg8: [REX] 88 ModRM d0 d1 d2 d3
			// REX needed for reg >= 4: 0x40 base, plus REX.R (0x04) if extended (reg >= 8)
			bool needs_rex = src_val >= 4;
			uint8_t rex_byte = static_cast<uint8_t>(0x40 | ((src_val >> 3) << 2));
			size_t base = textSectionData.size();
			textSectionData.resize(base + 6 + needs_rex);
			uint8_t* p = textSectionData.data() + base;
			if (needs_rex) *p++ = rex_byte;
			p[0] = 0x88; // MOV r/m8, r8
			p[1] = 0x05 | (src_bits << 3); // ModR/M: reg, [RIP + disp32]
			p[2] = 0x00; // disp32 placeholder
			p[3] = 0x00;
			p[4] = 0x00;
			p[5] = 0x00;
			return static_cast<uint32_t>(base + 2 + needs_rex);
		} else if (size_in_bits == 16) {
			// MOV WORD PTR [RIP + disp32], reg16: 66 [REX] 89 ModRM d0 d1 d2 d3
			uint8_t needs_rex_r = (src_val >> 3) & 0x01; // 1 if R8-R15
			size_t base = textSectionData.size();
			textSectionData.resize(base + 7 + needs_rex_r);
			uint8_t* p = textSectionData.data() + base;
			*p++ = 0x66; // operand-size prefix
			if (needs_rex_r) *p++ = 0x44; // REX.R for R8-R15
			p[0] = 0x89; // MOV r/m16, r16
			p[1] = 0x05 | (src_bits << 3); // ModR/M: reg, [RIP + disp32]
			p[2] = 0x00; // disp32 placeholder
			p[3] = 0x00;
			p[4] = 0x00;
			p[5] = 0x00;
			return static_cast<uint32_t>(base + 3 + needs_rex_r);
		}

		// For 32-bit and 64-bit, use regular MOV
		// For RIP-relative MOV, source is in the REG field of ModR/M (R/M=101 is fixed),
		// so REX.R (bit 2 = 0x04) extends it — not REX.B (bit 0 = 0x01)
		uint8_t needs_rex_w = (size_in_bits == 64) ? 0x08 : 0x00;
		uint8_t needs_rex_r = ((src_val >> 3) & 0x01) << 2; // 0x04 if R8-R15, else 0
		uint8_t rex = 0x40 | needs_rex_w | needs_rex_r;
		// Only emit REX if any bits are set beyond base 0x40
		uint8_t emit_rex = (needs_rex_w | needs_rex_r) != 0;
		// Use reserve + direct writes for branchless emission
		size_t base = textSectionData.size();
		textSectionData.resize(base + 6 + emit_rex);
		uint8_t* p = textSectionData.data() + base;
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

	/// Check if an argument is a two-register struct under System V AMD64 ABI (9-16 bytes, by value).
	bool isTwoRegisterStruct(const TypedValue& arg) const {
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			return arg.type == Type::Struct && arg.size_in_bits > 64 && arg.size_in_bits <= 128 && !arg.is_reference();
		}
		return false;
	}

	/// Determine if a struct argument should be passed by address (pointer) based on ABI.
	bool shouldPassStructByAddress(const TypedValue& arg) const {
		if (arg.type != Type::Struct || arg.is_reference()) return false;
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			return arg.size_in_bits > 128;  // Linux: structs > 16 bytes pass by pointer
		} else {
			return arg.size_in_bits > 64;   // Windows: structs > 8 bytes pass by pointer
		}
	}

