// IRConverter_ABI.h - Win64/SysV ABI register maps, helper templates, and RegisterAllocator
// Part of IRConverter.h unity build - do not add #pragma once

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
		if (reg == X64Register::Count)
			throw std::runtime_error(std::string("flushSingleDirtyRegister: invalid register (Count), caller must not pass sentinel value"));
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
		if (reg == X64Register::Count || registers[static_cast<int>(reg)].isAllocated) {
			throw std::runtime_error("allocateSpecific: invalid register or already allocated");
		}
		registers[static_cast<int>(reg)].isAllocated = true;
		registers[static_cast<int>(reg)].stackVariableOffset = stackVariableOffset;
	}

	void release(X64Register reg) {
		if (reg == X64Register::Count) return; // No register to release
		registers[static_cast<int>(reg)] = AllocatedRegister{ .reg = reg };
	}

	bool is_allocated(X64Register reg) const {
		return registers[static_cast<size_t>(reg)].isAllocated;
	}

	void mark_reg_dirty(X64Register reg) {
		if (reg == X64Register::Count || !registers[static_cast<int>(reg)].isAllocated) {
			throw std::runtime_error("mark_reg_dirty: register not allocated");
		}
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
		if (reg == X64Register::Count || !registers[static_cast<int>(reg)].isAllocated) {
			throw std::runtime_error("set_stack_variable_offset: register not allocated");
		}
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


