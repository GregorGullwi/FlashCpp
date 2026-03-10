// ObjFileWriter_Debug.cpp - Out-of-line method definitions for ObjectFileWriter
// Part of ObjectFileWriter class (unity build)



// --- Helper structs for exception info sub-functions ---




// --- Helper static methods ---

void ObjectFileWriter::patch_xdata_u32(std::vector<char>& xdata, uint32_t offset, uint32_t value) {
	xdata[offset + 0] = static_cast<char>(value & 0xFF);
	xdata[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
	xdata[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
	xdata[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

void ObjectFileWriter::appendLE_xdata(std::vector<char>& buf, uint32_t value) {
	buf.push_back(static_cast<char>(value & 0xFF));
	buf.push_back(static_cast<char>((value >> 8) & 0xFF));
	buf.push_back(static_cast<char>((value >> 16) & 0xFF));
	buf.push_back(static_cast<char>((value >> 24) & 0xFF));
}

// --- Exception info sub-functions ---

ObjectFileWriter::UnwindCodeResult ObjectFileWriter::build_unwind_codes(bool is_cpp, uint32_t stack_frame_size) {
	// Build unwind codes array dynamically based on actual prologue.
	// For C++ EH functions (split-frame prologue):
	//   Offset 0:   push rbp                 (1 byte)
	//   Offset 1:   sub rsp, primary         (7 bytes)
	//   Offset 8:   lea rbp, [rsp+primary]   (8 bytes)
	//   Offset 16:  sub rsp, extra           (7 bytes, omitted from unwind info when extra==0)
	//   Prologue size in unwind info: 16 or 23 bytes depending on whether extra!=0
	//   FrameOffset = primary / 16, where primary <= 240
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
	result.effective_frame_size = stack_frame_size;
	auto appendAllocCode = [&](uint8_t code_offset, uint32_t alloc_size) {
		if (alloc_size == 0) {
			return;
		}
		if (alloc_size <= 128) {
			uint8_t info = static_cast<uint8_t>(alloc_size / 8 - 1);
			result.codes.push_back(code_offset);
			result.codes.push_back(static_cast<uint8_t>((info << 4) | 0x02));
		} else {
			result.codes.push_back(code_offset);
			result.codes.push_back(0x01);
			uint16_t size_in_8bytes = static_cast<uint16_t>(alloc_size / 8);
			result.codes.push_back(static_cast<uint8_t>(size_in_8bytes & 0xFF));
			result.codes.push_back(static_cast<uint8_t>((size_in_8bytes >> 8) & 0xFF));
		}
	};

	if (is_cpp) {
		uint8_t frame_offset = static_cast<uint8_t>(std::min(stack_frame_size / 16, uint32_t(15)));
		result.effective_frame_size = static_cast<uint32_t>(frame_offset) * 16;
		uint32_t extra_stack_size = stack_frame_size - result.effective_frame_size;
		result.prolog_size = extra_stack_size > 0 ? 23 : 16;
		result.frame_reg_and_offset = static_cast<uint8_t>((frame_offset << 4) | 0x05); // RBP=5

		// If needed, unwind the post-frame allocation first.
		appendAllocCode(0x17, extra_stack_size);

		// UWOP_SET_FPREG at offset 16 (after lea rbp, [rsp+N])
		result.codes.push_back(0x10);  // offset 16
		result.codes.push_back(0x03);  // UWOP_SET_FPREG, info=0

		// Unwind the establisher-frame allocation.
		appendAllocCode(0x08, result.effective_frame_size);

		// UWOP_PUSH_NONVOL(RBP) at offset 1
		result.codes.push_back(0x01);
		result.codes.push_back(static_cast<uint8_t>(0x05 << 4 | 0x00));
	} else {
		// Traditional prologue: push rbp(1) + mov rbp,rsp(3) + sub rsp(7) = 11
		result.prolog_size = 11;
		result.frame_reg_and_offset = 0x05; // RBP=5, FrameOffset=0

		appendAllocCode(result.prolog_size, stack_frame_size);

		// UWOP_SET_FPREG at offset 4 (after mov rbp, rsp)
		result.codes.push_back(0x04);
		result.codes.push_back(0x03);  // UWOP_SET_FPREG, info=0

		// UWOP_PUSH_NONVOL(RBP) at offset 1 (after push rbp)
		result.codes.push_back(0x01);
		result.codes.push_back(static_cast<uint8_t>(0x05 << 4 | 0x00));
	}

	result.count_of_codes = static_cast<uint8_t>(result.codes.size() / 2);

	// Pad to DWORD alignment (even number of unwind code slots)
	if (result.codes.size() % 4 != 0) {
		while (result.codes.size() % 4 != 0) {
			result.codes.push_back(0x00);
		}
	}

	return result;
}

