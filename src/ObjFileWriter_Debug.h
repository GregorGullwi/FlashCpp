
	// --- Helper structs for exception info sub-functions ---

	struct UnwindCodeResult {
		std::vector<uint8_t> codes;
		uint8_t prolog_size;
		uint8_t frame_reg_and_offset;
		uint8_t count_of_codes;
		uint32_t effective_frame_size;
	};

	struct ScopeTableReloc {
		uint32_t begin_offset;    // Offset of BeginAddress field within xdata
		uint32_t end_offset;      // Offset of EndAddress field within xdata
		uint32_t handler_offset;  // Offset of HandlerAddress field within xdata
		uint32_t jump_offset;     // Offset of JumpTarget field within xdata
		bool needs_handler_reloc; // True if HandlerAddress needs a relocation (RVA, not constant)
		bool needs_jump_reloc;    // True if JumpTarget needs a relocation (non-zero RVA)
	};

	struct PendingPdataEntry {
		uint32_t begin_rva;
		uint32_t end_rva;
		uint32_t unwind_rva;
	};

	// --- Helper static methods ---

	static void patch_xdata_u32(std::vector<char>& xdata, uint32_t offset, uint32_t value) {
		xdata[offset + 0] = static_cast<char>(value & 0xFF);
		xdata[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
		xdata[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
		xdata[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
	}

	static void appendLE_xdata(std::vector<char>& buf, uint32_t value) {
		buf.push_back(static_cast<char>(value & 0xFF));
		buf.push_back(static_cast<char>((value >> 8) & 0xFF));
		buf.push_back(static_cast<char>((value >> 16) & 0xFF));
		buf.push_back(static_cast<char>((value >> 24) & 0xFF));
	}

	// --- Exception info sub-functions ---

	UnwindCodeResult build_unwind_codes(bool is_cpp, uint32_t stack_frame_size) {
		// Build unwind codes array dynamically based on actual prologue.
		// For C++ EH functions (clang-style prologue):
		//   Offset 0:  push rbp              (1 byte)
		//   Offset 1:  sub rsp, imm32        (7 bytes)
		//   Offset 8:  lea rbp, [rsp+imm32]  (8 bytes)
		//   Total prologue size: 16 bytes
		//   FrameOffset = stack_frame_size / 16
		//
		// For non-EH functions (traditional prologue):
		//   Offset 0:  push rbp           (1 byte)
		//   Offset 1:  mov rbp, rsp       (3 bytes)
		//   Offset 4:  sub rsp, imm32     (7 bytes)
		//   Total prologue size: 11 bytes
		//   FrameOffset = 0
		//
		// Unwind codes are listed in REVERSE order of prologue operations:
		// Each UNWIND_CODE is 2 bytes: [offset_in_prolog, (info << 4) | operation]
		//   UWOP_PUSH_NONVOL = 0, UWOP_ALLOC_LARGE = 1, UWOP_ALLOC_SMALL = 2, UWOP_SET_FPREG = 3

		UnwindCodeResult result;
		// When FrameOffset is capped at 15, the unwinder computes EstablisherFrame = RBP - FrameOffset*16,
		// which differs from RBP - stack_frame_size. All EH displacements must use this capped value.
		result.effective_frame_size = stack_frame_size;

		if (is_cpp) {
			// C++ EH prologue: push rbp(1) + sub rsp(7) + lea rbp(8) = 16
			result.prolog_size = 16;
			uint8_t frame_offset = static_cast<uint8_t>(std::min(stack_frame_size / 16, uint32_t(15)));
			result.effective_frame_size = static_cast<uint32_t>(frame_offset) * 16;
			result.frame_reg_and_offset = static_cast<uint8_t>((frame_offset << 4) | 0x05); // RBP=5

			// UWOP_SET_FPREG at offset 16 (after lea rbp, [rsp+N])
			result.codes.push_back(0x10);  // offset 16
			result.codes.push_back(0x03);  // UWOP_SET_FPREG, info=0

			// UWOP_ALLOC at offset 8 (after sub rsp, N)
			if (stack_frame_size > 0) {
				if (stack_frame_size <= 128) {
					uint8_t info = static_cast<uint8_t>(stack_frame_size / 8 - 1);
					result.codes.push_back(0x08);  // offset 8
					result.codes.push_back(static_cast<uint8_t>((info << 4) | 0x02));  // UWOP_ALLOC_SMALL
				} else {
					result.codes.push_back(0x08);  // offset 8
					result.codes.push_back(0x01);  // UWOP_ALLOC_LARGE, info=0
					uint16_t size_in_8bytes = static_cast<uint16_t>(stack_frame_size / 8);
					result.codes.push_back(static_cast<uint8_t>(size_in_8bytes & 0xFF));
					result.codes.push_back(static_cast<uint8_t>((size_in_8bytes >> 8) & 0xFF));
				}
			}

			// UWOP_PUSH_NONVOL(RBP) at offset 1
			result.codes.push_back(0x01);
			result.codes.push_back(static_cast<uint8_t>(0x05 << 4 | 0x00));
		} else {
			// Traditional prologue: push rbp(1) + mov rbp,rsp(3) + sub rsp(7) = 11
			result.prolog_size = 11;
			result.frame_reg_and_offset = 0x05; // RBP=5, FrameOffset=0

			if (stack_frame_size > 0) {
				if (stack_frame_size <= 128) {
					uint8_t info = static_cast<uint8_t>(stack_frame_size / 8 - 1);
					result.codes.push_back(result.prolog_size);  // offset at end of sub rsp instruction
					result.codes.push_back(static_cast<uint8_t>((info << 4) | 0x02));  // UWOP_ALLOC_SMALL
				} else {
					result.codes.push_back(result.prolog_size);
					result.codes.push_back(0x01);  // UWOP_ALLOC_LARGE, info=0
					uint16_t size_in_8bytes = static_cast<uint16_t>(stack_frame_size / 8);
					result.codes.push_back(static_cast<uint8_t>(size_in_8bytes & 0xFF));
					result.codes.push_back(static_cast<uint8_t>((size_in_8bytes >> 8) & 0xFF));
				}
			}

			// UWOP_SET_FPREG at offset 4 (after mov rbp, rsp)
			result.codes.push_back(0x04);
			result.codes.push_back(0x03);  // UWOP_SET_FPREG, info=0

			// UWOP_PUSH_NONVOL(RBP) at offset 1 (after push rbp)
			result.codes.push_back(0x01);
			result.codes.push_back(static_cast<uint8_t>(0x05 << 4 | 0x00));
		}

		// Pad to DWORD alignment (even number of unwind code slots)
		if (result.codes.size() % 4 != 0) {
			while (result.codes.size() % 4 != 0) {
				result.codes.push_back(0x00);
			}
		}

		// Adjust count_of_codes to actual number of UNWIND_CODE entries (excluding padding)
		if (stack_frame_size > 0) {
			result.count_of_codes = (stack_frame_size <= 128) ? 3 : 4;
		} else {
			result.count_of_codes = 2;
		}

		return result;
	}

