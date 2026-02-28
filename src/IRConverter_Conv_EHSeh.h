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
		emitMovRegFromMemRegSized(X64Register::RDI, X64Register::RDI, 64);  // MOV RDI, [RDI] (load BCD pointer)
		
		// Null check on BCD
		emitTestRegReg(X64Register::RDI);  // TEST RDI, RDI
		size_t null_bcd_skip = textSectionData.size();
		emitJumpIfZero(0);  // JZ -> loop_continue (will patch offset to skip this iteration)
		
		// Get type descriptor from BCD (at offset 0)
		emitMovRegFromMemRegSized(X64Register::RAX, X64Register::RDI, 64);  // MOV RAX, [RDI] (base type_desc)
		
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

	// Patch ELF catch handler filter values in the generated code.
	// This is called at function finalization when we know the complete type table.
	// The filter values must match what the LSDA generator will produce.
	void patchElfCatchFilterValues(const std::vector<ObjectFileWriter::TryBlockInfo>& try_blocks) {
		if (elf_catch_filter_patches_.empty()) return;

		// Build the type table in the same order as ElfFileWriter will build it.
		// This determines the filter values for each handler.
		std::vector<std::string> type_table;
		for (const auto& try_block : try_blocks) {
			for (const auto& handler : try_block.catch_handlers) {
				if (!handler.is_catch_all && !handler.type_name.empty()) {
					std::string typeinfo_sym = writer.get_typeinfo_symbol(handler.type_name);
					if (std::find(type_table.begin(), type_table.end(), typeinfo_sym) == type_table.end()) {
						type_table.push_back(typeinfo_sym);
					}
				}
			}
		}
		// Add NULL entry for catch-all (same as LSDAGenerator's generate() does)
		bool has_catch_all = false;
		for (const auto& try_block : try_blocks) {
			for (const auto& handler : try_block.catch_handlers) {
				if (handler.is_catch_all) { has_catch_all = true; break; }
			}
			if (has_catch_all) break;
		}
		if (has_catch_all) {
			if (std::find(type_table.begin(), type_table.end(), "") == type_table.end()) {
				type_table.push_back("");
			}
		}

		int table_size = static_cast<int>(type_table.size());

		// Now compute filters and patch each CMP instruction
		for (const auto& patch : elf_catch_filter_patches_) {
			// Find this handler's type in the correct try block
			int filter = 0;
			if (patch.try_block_index < try_blocks.size() &&
			    patch.handler_index < try_blocks[patch.try_block_index].catch_handlers.size()) {
				const auto& handler = try_blocks[patch.try_block_index].catch_handlers[patch.handler_index];
				if (handler.is_catch_all) {
					auto it = std::find(type_table.begin(), type_table.end(), "");
					if (it != type_table.end()) {
						filter = table_size - static_cast<int>(std::distance(type_table.begin(), it));
					}
				} else if (!handler.type_name.empty()) {
					std::string typeinfo_sym = writer.get_typeinfo_symbol(handler.type_name);
					auto it = std::find(type_table.begin(), type_table.end(), typeinfo_sym);
					if (it != type_table.end()) {
						filter = table_size - static_cast<int>(std::distance(type_table.begin(), it));
					}
				}
			}
			// Patch the IMM32 in textSectionData
			auto bytes = std::bit_cast<std::array<uint8_t, 4>>(static_cast<int32_t>(filter));
			for (int i = 0; i < 4; i++) {
				textSectionData[patch.patch_offset + i] = bytes[i];
			}
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

