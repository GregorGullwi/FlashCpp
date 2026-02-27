// IRConverter_Emit_EHSeh.h - Exception handling and SEH emit/handle methods
// Included inside IrToObjConverter class body - do not add #pragma once
	void handleTryBegin([[maybe_unused]] const IrInstruction& instruction) {
		// Skip exception handling if disabled
		if (!g_enable_exceptions) {
			return;
		}
		
		// TryBegin marks the start of a try block
		// Record the current code offset as the start of the try block
		
		TryBlock try_block;
		try_block.try_start_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
		try_block.try_end_offset = 0;  // Will be set in handleTryEnd
		
		current_function_try_blocks_.push_back(try_block);
		size_t new_index = current_function_try_blocks_.size() - 1;
		try_block_nesting_stack_.push_back(new_index);
		current_try_block_ = &current_function_try_blocks_[new_index];

		// Note: The instruction has a BranchOp typed payload with the handler label,
		// but we don't need it for code generation - we track offsets directly.
	}

	void handleTryEnd([[maybe_unused]] const IrInstruction& instruction) {
		// Skip exception handling if disabled
		if (!g_enable_exceptions) {
			return;
		}
		
		// TryEnd marks the end of a try block
		// Record the current code offset as the end of the try block
		
		if (!try_block_nesting_stack_.empty()) {
			size_t try_index = try_block_nesting_stack_.back();
			try_block_nesting_stack_.pop_back();
			current_function_try_blocks_[try_index].try_end_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
			pending_catch_try_index_ = try_index;

			// Restore current_try_block_ to outer try block (if any)
			if (!try_block_nesting_stack_.empty()) {
				current_try_block_ = &current_function_try_blocks_[try_block_nesting_stack_.back()];
			} else {
				current_try_block_ = nullptr;
			}
		}
	}

	void handleCatchBegin(const IrInstruction& instruction) {
		// Skip exception handling if disabled
		if (!g_enable_exceptions) {
			return;
		}
		
		// CatchBegin marks the start of a catch handler
		// Record this catch handler in the most recent try block
		
		if (pending_catch_try_index_ != SIZE_MAX && pending_catch_try_index_ < current_function_try_blocks_.size()) {
			// Get the try block that just ended (tracked by pending_catch_try_index_)
			TryBlock& try_block = current_function_try_blocks_[pending_catch_try_index_];
			
			CatchHandler handler;
			handler.handler_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
			handler.handler_end_offset = 0;
			handler.funclet_entry_offset = handler.handler_offset;
			handler.funclet_end_offset = 0;
			
			// Extract data from typed payload
			const auto& catch_op = instruction.getTypedPayload<CatchBeginOp>();
			handler.type_index = catch_op.type_index;
			handler.exception_type = catch_op.exception_type;  // Copy the Type enum
			handler.is_const = catch_op.is_const;
			handler.is_reference = catch_op.is_reference;
			handler.is_rvalue_reference = catch_op.is_rvalue_reference;
			handler.is_catch_all = catch_op.is_catch_all;  // Use the flag from IR, not derive from type_index
			
			// Pre-compute stack offset for exception object during IR processing.
			// This is necessary because variable_scopes may be cleared by the time
			// we call convertExceptionInfoToWriterFormat() during finalization.
			// var_number == 0 indicates catch(...) which has no exception variable,
			// or an unnamed catch parameter like catch(int) without a variable name.
			if (!handler.is_catch_all && catch_op.exception_temp.var_number != 0) {
				handler.catch_obj_stack_offset = getStackOffsetFromTempVar(catch_op.exception_temp);
			} else {
				handler.catch_obj_stack_offset = 0;
			}
			
			try_block.catch_handlers.push_back(handler);
			current_catch_handler_ = &try_block.catch_handlers.back();
		}
		
		// Platform-specific landing pad code generation
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			inside_catch_handler_ = true;
			// ========== Linux/ELF (Itanium C++ ABI) ==========
			// Landing pad: call __cxa_begin_catch to get the exception object
			//
			// For try blocks with MULTIPLE catch handlers, the personality routine
			// enters the landing pad with:
			//   RAX = exception object pointer
			//   EDX = selector (type_filter of matched action)
			// We must save these, dispatch to the correct handler based on selector,
			// then call __cxa_begin_catch in the matched handler.
			
			const auto& catch_op = instruction.getTypedPayload<CatchBeginOp>();
			
			// Determine handler index within this try block
			size_t handler_index = 0;
			bool is_multi_handler = false;
			if (pending_catch_try_index_ != SIZE_MAX && pending_catch_try_index_ < current_function_try_blocks_.size()) {
				const TryBlock& try_block = current_function_try_blocks_[pending_catch_try_index_];
				handler_index = try_block.catch_handlers.size() - 1;  // Just added
				// We don't know total count yet, but if handler_index > 0, we're multi
				is_multi_handler = (handler_index > 0);
			}
			
			if (handler_index == 0) {
				// First handler: save RAX (exception ptr) and EDX (selector) to stack
				// These will be used by subsequent handlers for dispatch.
				// We allocate two 8-byte slots for these.
				elf_exc_ptr_offset_ = allocateElfTempStackSlot(8);
				elf_selector_offset_ = allocateElfTempStackSlot(4);
				// mov [rbp+exc_ptr_offset], rax
				emitMovToFrame(X64Register::RAX, elf_exc_ptr_offset_, 64);
				// mov [rbp+selector_offset], edx (32-bit)
				emitMovToFrameBySize(X64Register::RDX, elf_selector_offset_, 32);
			}
			
			// For non-last handlers, emit selector comparison + skip jump.
			// We always emit it (even for potentially-last handlers) because we don't
			// know if more handlers follow. If this IS the last handler, the personality
			// routine guarantees the selector matches, so the JNE never fires.
			if (!catch_op.is_catch_all) {
				// Load selector from stack and compare with this handler's filter.
				// The actual filter value will be patched at function finalization.
				// CMP dword [rbp+offset], imm32
				emitCmpFrameImm32(elf_selector_offset_, 0);  // placeholder 0
				// Record patch position: the IMM32 is at the last 4 bytes we just wrote
				uint32_t filter_patch_pos = static_cast<uint32_t>(textSectionData.size()) - 4;
				elf_catch_filter_patches_.push_back({filter_patch_pos, pending_catch_try_index_, handler_index});
				
				// JNE catch_end_label (skip this handler if selector doesn't match)
				StringHandle catch_end_handle = StringTable::getOrInternStringHandle(catch_op.catch_end_label);
				textSectionData.push_back(0x0F);  // Two-byte opcode prefix
				textSectionData.push_back(0x85);  // JNE rel32
				uint32_t jne_patch = static_cast<uint32_t>(textSectionData.size());
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				pending_branches_.push_back({catch_end_handle, jne_patch});
			}
			
			// Load exception pointer from saved slot and call __cxa_begin_catch
			if (is_multi_handler || !catch_op.is_catch_all) {
				// Multi-handler or typed: use saved exception pointer
				emitMovFromFrameBySize(X64Register::RDI, elf_exc_ptr_offset_, 64);
			} else {
				// Single catch-all handler: RAX still has the exception pointer
				emitMovRegReg(X64Register::RDI, X64Register::RAX);
			}
			emitCall("__cxa_begin_catch");
			
			// Result in RAX is pointer to the actual exception object
			// Store it to the catch variable's stack location
			if (catch_op.exception_temp.var_number != 0) {
				int32_t stack_offset = getStackOffsetFromTempVar(catch_op.exception_temp);
				
				if (g_enable_debug_output) {
					std::cerr << "[DEBUG][Codegen] CatchBegin: is_ref=" << catch_op.is_reference
					          << " is_rvalue_ref=" << catch_op.is_rvalue_reference
					          << " type_index=" << catch_op.type_index
					          << " stack_offset=" << stack_offset << std::endl;
				}
				
				// For POD types, dereference and copy the value
				// For references, store the pointer itself
				if (catch_op.is_reference || catch_op.is_rvalue_reference) {
					// Store the pointer (RAX) directly (64-bit pointer)
					if (g_enable_debug_output) {
						std::cerr << "[DEBUG][Codegen] CatchBegin: storing pointer (reference type)" << std::endl;
					}
					emitMovToFrame(X64Register::RAX, stack_offset, 64);
				} else {
					// Get type size for dereferencing
					// For built-in types, use get_type_size_bits directly
					// For user-defined types, look up in gTypeInfo
					int type_size_bits = 0;
					
					// Check if this is a built-in type that get_type_size_bits can handle
					// These are types where the Type enum directly maps to a size
					bool is_builtin = false;
					switch (catch_op.exception_type) {
						case Type::Bool:
						case Type::Char:
						case Type::UnsignedChar:
						case Type::Short:
						case Type::UnsignedShort:
						case Type::Int:
						case Type::UnsignedInt:
						case Type::Long:
						case Type::UnsignedLong:
						case Type::LongLong:
						case Type::UnsignedLongLong:
						case Type::Float:
						case Type::Double:
						case Type::LongDouble:
						case Type::FunctionPointer:
						case Type::MemberFunctionPointer:
						case Type::MemberObjectPointer:
						case Type::Nullptr:
							is_builtin = true;
							break;
						default:
							is_builtin = false;
							break;
					}
					
					if (is_builtin) {
						// Built-in type - use direct lookup
						type_size_bits = get_type_size_bits(catch_op.exception_type);
					} else if (catch_op.type_index != 0 && catch_op.type_index < gTypeInfo.size()) {
						// User-defined type - look up in gTypeInfo
						const TypeInfo& type_info = gTypeInfo[catch_op.type_index];
						type_size_bits = type_info.type_size_;
					}
					size_t type_size = type_size_bits / 8;  // Convert bits to bytes
					
					if (g_enable_debug_output) {
						std::cerr << "[DEBUG][Codegen] CatchBegin: exception_type=" << static_cast<int>(catch_op.exception_type)
						          << " type_size_bits=" << type_size_bits
						          << " type_size=" << type_size << std::endl;
					}
					
					// Load value from exception object and store to catch variable
					if (type_size <= 8 && type_size > 0) {
						// Small POD: load from [RAX] and store to frame
						// Move value from [RAX] to RCX
						if (g_enable_debug_output) {
							std::cerr << "[DEBUG][Codegen] CatchBegin: dereferencing exception value" << std::endl;
						}
						emitMovFromMemory(X64Register::RCX, X64Register::RAX, 0, type_size);
						emitMovToFrameBySize(X64Register::RCX, stack_offset, type_size_bits);
					} else {
						// Large type or unknown size: just store pointer (64-bit pointer)
						if (g_enable_debug_output) {
							std::cerr << "[DEBUG][Codegen] CatchBegin: storing pointer (large or unknown type)" << std::endl;
						}
						emitMovToFrame(X64Register::RAX, stack_offset, 64);
					}
				}
			}
			
		} else {
			// ========== Windows/COFF (MSVC ABI) ==========
			// Catch handler is a real FH3 funclet entered with establisher-frame in RDX.
			// Emit a funclet prologue so the runtime can unwind it as an independent
			// range and all frame-relative accesses resolve against the parent frame.
			const auto& catch_op = instruction.getTypedPayload<CatchBeginOp>();
			// Save establisher frame (RDX) to caller's shadow space before push.
			// Clang emits this and the CRT may rely on it during unwinding.
			// mov [rsp+10h], rdx  (48 89 54 24 10)
			textSectionData.push_back(0x48);
			textSectionData.push_back(0x89);
			textSectionData.push_back(0x54);
			textSectionData.push_back(0x24);
			textSectionData.push_back(0x10);
			emitPushReg(X64Register::RBP);
			emitSubRSP(32);
			// LEA RBP, [RDX + imm32] — RDX is the establisher frame (RSP after parent prologue)
			// Adding total_stack gives RBP = establisher + N = RSP_after_sub + N = S-8.
			// This matches the parent function's RBP so frame-relative accesses work.
			// Encoding: 48 8D AA XX XX XX XX (REX.W LEA RBP, [RDX+disp32])
			catch_funclet_lea_rbp_patches_.push_back(static_cast<uint32_t>(textSectionData.size()));
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x8D); // LEA
			textSectionData.push_back(0xAA); // ModR/M: mod=10, reg=101(RBP), r/m=010(RDX)
			textSectionData.push_back(0x00); // disp32 placeholder (patched at function end)
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			in_catch_funclet_ = true;
			catch_funclet_terminated_by_return_ = false;
			current_catch_continuation_label_ = StringTable::getOrInternStringHandle(catch_op.continuation_label);
		}
	}

	void handleCatchEnd(const IrInstruction& instruction) {
		// Skip exception handling if disabled
		if (!g_enable_exceptions) {
			return;
		}
		
		if constexpr (!std::is_same_v<TWriterClass, ElfFileWriter>) {
			if (catch_funclet_terminated_by_return_) {
				catch_funclet_terminated_by_return_ = false;
				in_catch_funclet_ = false;
				current_catch_continuation_label_ = StringHandle();
				current_catch_handler_ = nullptr;
				return;
			}
		}

		// CatchEnd marks the end of a catch handler
		if (current_catch_handler_) {
			current_catch_handler_->handler_end_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
		}
		
		// Platform-specific cleanup
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// ========== Linux/ELF (Itanium C++ ABI) ==========
			// Call __cxa_end_catch to complete exception handling
			// This cleans up the exception object if needed
			
			emitCall("__cxa_end_catch");
			inside_catch_handler_ = false;
			
		} else {
			// ========== Windows/COFF (MSVC ABI) ==========
			// Return continuation address in RAX, then emit funclet epilogue.
			// After the funclet ret, emit a fixup stub for the catch continuation path.
			flushAllDirtyRegisters();

			StringHandle continuation_handle;
			StringHandle fixup_handle;
			bool has_continuation = false;

			if (instruction.hasTypedPayload()) {
				const auto& catch_end_op = instruction.getTypedPayload<CatchEndOp>();
				continuation_handle = StringTable::getOrInternStringHandle(catch_end_op.continuation_label);
				has_continuation = true;

				// Create a unique fixup label for the catch continuation entry point.
				std::string fixup_name = "__catch_fixup_" + std::to_string(textSectionData.size());
				fixup_handle = StringTable::getOrInternStringHandle(fixup_name);

				// LEA RAX, [fixup_label] — return fixup entry address to the CRT
				textSectionData.push_back(0x48);
				textSectionData.push_back(0x8D);
				textSectionData.push_back(0x05);
				uint32_t lea_patch = static_cast<uint32_t>(textSectionData.size());
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				pending_branches_.push_back({fixup_handle, lea_patch});
			} else {
				emitXorRegReg(X64Register::RAX);
			}

			// Funclet epilogue
			emitAddRSP(32);
			emitPopReg(X64Register::RBP);
			textSectionData.push_back(0xC3);  // ret

			// Record funclet end BEFORE the fixup stub (fixup is parent code, not funclet)
			if (current_catch_handler_) {
				current_catch_handler_->funclet_end_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
			}

			// Emit catch continuation entry point (in parent function code space).
			// With the clang-style EH prologue (push rbp; sub rsp, N; lea rbp, [rsp+N]),
			// the establisher frame = RSP_after_prologue = S-8-N.
			// After _JumpToContinuation: RSP = S-8-N (fully allocated frame).
			// We just need to restore RBP (corrupted by CRT) and jump to normal code.
			if (has_continuation) {
				// Define the fixup label position
				label_positions_[fixup_handle] = static_cast<uint32_t>(textSectionData.size());

				// LEA RBP, [RSP + total_stack] — restore frame pointer
				// Encoding: 48 8D AC 24 XX XX XX XX (patched at function end with total_stack)
				catch_continuation_sub_rsp_patches_.push_back(static_cast<uint32_t>(textSectionData.size()) + 4);
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0x8D); // LEA
				textSectionData.push_back(0xAC); // ModR/M: mod=10, reg=101(RBP), r/m=100(SIB)
				textSectionData.push_back(0x24); // SIB: base=RSP, index=none
				textSectionData.push_back(0x00); // disp32 placeholder
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);

				// jmp continuation_label  (E9 xx xx xx xx) — join normal code path
				textSectionData.push_back(0xE9);
				uint32_t jmp_patch = static_cast<uint32_t>(textSectionData.size());
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				pending_branches_.push_back({continuation_handle, jmp_patch});
			}

			in_catch_funclet_ = false;
			catch_funclet_terminated_by_return_ = false;
			current_catch_continuation_label_ = StringHandle();
		}

		if (current_catch_handler_) {
			// For ELF, record funclet_end_offset here. For Windows/COFF, it was already
			// recorded inside the platform-specific block (before the fixup stub).
			if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
				current_catch_handler_->funclet_end_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
			}
			current_catch_handler_ = nullptr;
		}
	}

	void handleThrow(const IrInstruction& instruction) {
		// Skip exception handling if disabled - generate abort() instead
		if (!g_enable_exceptions) {
			// Call abort() to terminate when exceptions are disabled
			emitCall("abort");
			return;
		}
		
		// Extract data from typed payload
		const auto& throw_op = instruction.getTypedPayload<ThrowOp>();
		
		size_t exception_size = throw_op.size_in_bytes;
		if (exception_size == 0) exception_size = 8; // Minimum size
		
		// Round exception size up to 8-byte alignment
		size_t aligned_exception_size = (exception_size + 7) & ~7;
		
		// Platform-specific exception handling
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// ========== Linux/ELF (Itanium C++ ABI) ==========
			// Two-step process:
			// 1. Call __cxa_allocate_exception(size_t thrown_size) -> returns void*
			// 2. Copy exception object to allocated memory
			// 3. Call __cxa_throw(void* thrown_object, type_info* tinfo, void (*dest)(void*))
			//
			// System V AMD64 calling convention: RDI, RSI, RDX, RCX, R8, R9
			
			// Step 1: Call __cxa_allocate_exception(exception_size)
			// Argument: RDI = exception_size
			emitMovImm64(X64Register::RDI, aligned_exception_size);
			emitSubRSP(8); // Align stack to 16 bytes before call
			emitCall("__cxa_allocate_exception");
			emitAddRSP(8); // Clean up alignment
			
			// Result is in RAX (pointer to allocated exception object)
			// Save it to R15 (callee-saved register)
			emitMovRegReg(X64Register::R15, X64Register::RAX);
			
			// Step 2: Copy exception object to allocated memory
			if (exception_size <= 8) {
				// Small object: load value into RCX and store to [R15]
				// Use IrValue variant to handle TempVar, integer literal, or float literal
				if (std::holds_alternative<double>(throw_op.exception_value)) {
					// Float immediate - convert to bit representation
					double float_val = std::get<double>(throw_op.exception_value);
					uint64_t bits;
					if (exception_size == 4) {
						float f = static_cast<float>(float_val);
						std::memcpy(&bits, &f, sizeof(float));
						bits &= 0xFFFFFFFF; // Clear upper bits
					} else {
						std::memcpy(&bits, &float_val, sizeof(double));
					}
					emitMovImm64(X64Register::RCX, bits);
				} else if (std::holds_alternative<unsigned long long>(throw_op.exception_value)) {
					// Integer immediate - load directly
					emitMovImm64(X64Register::RCX, std::get<unsigned long long>(throw_op.exception_value));
				} else if (std::holds_alternative<TempVar>(throw_op.exception_value)) {
					// TempVar - load from stack
					TempVar temp = std::get<TempVar>(throw_op.exception_value);
					if (temp.var_number != 0) {
						int32_t stack_offset = getStackOffsetFromTempVar(temp);
						emitMovFromFrameBySize(X64Register::RCX, stack_offset, static_cast<int>(exception_size * 8));
					} else {
						emitMovImm64(X64Register::RCX, 0);
					}
				} else {
					// StringHandle is not a valid exception value type - IrValue includes it for
					// other contexts (like variable names), but throw expressions only use TempVar
					// or immediate values. Default to zero as a safety fallback.
					emitMovImm64(X64Register::RCX, 0);
				}
				// Store exception value to allocated memory [R15 + 0]
				emitStoreToMemory(textSectionData, X64Register::RCX, X64Register::R15, 0, static_cast<int>(exception_size));
			} else {
				// Large object: memory-to-memory copy (must be TempVar)
				if (std::holds_alternative<TempVar>(throw_op.exception_value)) {
					TempVar temp = std::get<TempVar>(throw_op.exception_value);
					if (temp.var_number != 0) {
						int32_t stack_offset = getStackOffsetFromTempVar(temp);
						emitLeaFromFrame(X64Register::RSI, stack_offset);
					} else {
						emitXorRegReg(X64Register::RSI);
					}
				} else {
					// Large objects can only be TempVars - immediates and StringHandles are not valid here
					emitXorRegReg(X64Register::RSI);
				}
				// RDI = destination (allocated memory in R15)
				emitMovRegReg(X64Register::RDI, X64Register::R15);
				// RCX = count
				emitMovImm64(X64Register::RCX, exception_size);
				// Copy: rep movsb
				emitRepMovsb();
			}
			
			// Step 3: Call __cxa_throw(thrown_object, tinfo, destructor)
			// RDI = thrown_object (from __cxa_allocate_exception, now in R15)
			emitMovRegReg(X64Register::RDI, X64Register::R15);
			
			// RSI = type_info* - generate type_info for the thrown type
			std::string typeinfo_symbol;
			Type exception_type = throw_op.exception_type;
			
			// Check if it's a built-in type or user-defined type
			if (exception_type == Type::Struct && throw_op.type_index < gTypeInfo.size()) {
				// User-defined class type - look up struct info from type_index
				const TypeInfo& type_info = gTypeInfo[throw_op.type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info) {
					typeinfo_symbol = writer.get_or_create_class_typeinfo(StringTable::getStringView(struct_info->getName()));
				}
			} else if (exception_type != Type::Void) {
				// Built-in type (int, float, etc.) - use the Type enum directly
				typeinfo_symbol = writer.get_or_create_builtin_typeinfo(exception_type);
			}
			
			if (!typeinfo_symbol.empty()) {
				// Load address of type_info into RSI using RIP-relative LEA
				emitLeaRipRelativeWithRelocation(X64Register::RSI, typeinfo_symbol);
			} else {
				// Unknown type, use NULL
				emitXorRegReg(X64Register::RSI);
			}
			
			// RDX = destructor function pointer (NULL for POD types)
			emitXorRegReg(X64Register::RDX);
			
			emitCall("__cxa_throw");
			// Note: __cxa_throw never returns
			
		} else {
			// ========== Windows/COFF (MSVC ABI) ==========
			// Single-step process:
			// Call _CxxThrowException(void* pExceptionObject, _ThrowInfo* pThrowInfo)
			//
			// Windows x64 calling convention: RCX, RDX, R8, R9

			// Allocate a frame-relative slot for the exception object.
			// Using [RSP+32] is unsafe because it can overlap with the saved RBP
			// when the stack frame is small (e.g., a function that only throws).
			// Instead, allocate a proper slot via the temp var mechanism.
			int32_t throw_temp_size = static_cast<int32_t>((aligned_exception_size + 7) & ~7);
			next_temp_var_offset_ += throw_temp_size;
			int32_t throw_slot_offset = -(static_cast<int32_t>(current_function_named_vars_size_) + next_temp_var_offset_);
			// Extend scope_stack_space to account for this allocation
			if (!variable_scopes.empty()) {
				auto& scope = variable_scopes.back();
				int32_t required = -(throw_slot_offset);
				if (required > -scope.scope_stack_space) {
					scope.scope_stack_space = -required;
				}
			}

			// Copy exception object to frame slot at [RBP+throw_slot_offset]
			if (exception_size <= 8) {
				// Small object: load into RAX and store
				if (std::holds_alternative<TempVar>(throw_op.exception_value)) {
					TempVar temp = std::get<TempVar>(throw_op.exception_value);
					if (temp.var_number != 0) {
						int32_t stack_offset = getStackOffsetFromTempVar(temp);
						emitMovFromFrameBySize(X64Register::RAX, stack_offset, static_cast<int>(exception_size * 8));
					} else {
						emitMovImm64(X64Register::RAX, 0);
					}
				} else if (std::holds_alternative<unsigned long long>(throw_op.exception_value)) {
					emitMovImm64(X64Register::RAX, std::get<unsigned long long>(throw_op.exception_value));
				} else if (std::holds_alternative<double>(throw_op.exception_value)) {
					double float_val = std::get<double>(throw_op.exception_value);
					uint64_t bits;
					if (exception_size == 4) {
						float f = static_cast<float>(float_val);
						std::memcpy(&bits, &f, sizeof(float));
						bits &= 0xFFFFFFFF;
					} else {
						std::memcpy(&bits, &float_val, sizeof(double));
					}
					emitMovImm64(X64Register::RAX, bits);
				} else {
					emitMovImm64(X64Register::RAX, 0);
				}
				emitMovToFrame(X64Register::RAX, throw_slot_offset, static_cast<int>(exception_size * 8));
			} else {
				// Large object: use memory-to-memory copy (must be TempVar)
				if (std::holds_alternative<TempVar>(throw_op.exception_value)) {
					TempVar temp = std::get<TempVar>(throw_op.exception_value);
					if (temp.var_number != 0) {
						int32_t stack_offset = getStackOffsetFromTempVar(temp);
						emitLeaFromFrame(X64Register::RSI, stack_offset);
					} else {
						emitXorRegReg(X64Register::RSI);
					}
				} else {
					emitXorRegReg(X64Register::RSI);
				}
				emitLeaFromFrame(X64Register::RDI, throw_slot_offset);
				emitMovImm64(X64Register::RCX, exception_size);
				emitRepMovsb();
			}

			// Set up arguments for _CxxThrowException
			// RCX (first argument) = pointer to exception object on the frame
			emitLeaFromFrame(X64Register::RCX, throw_slot_offset);
			// RDX (second argument) = pointer to _ThrowInfo metadata
			std::string throw_type_name;
			if (throw_op.exception_type == Type::Struct && throw_op.type_index < gTypeInfo.size()) {
				throw_type_name = std::string(StringTable::getStringView(gTypeInfo[throw_op.type_index].name()));
			} else {
				throw_type_name = std::string(getTypeName(throw_op.exception_type));
			}

			std::string throw_info_symbol;
			if (!throw_type_name.empty() && throw_type_name != "void") {
				bool is_simple_type = (throw_op.exception_type != Type::Struct);
				throw_info_symbol = writer.get_or_create_exception_throw_info(throw_type_name, exception_size, is_simple_type);
			}

			if (!throw_info_symbol.empty()) {
				emitLeaRipRelativeWithRelocation(X64Register::RDX, throw_info_symbol);
			} else {
				emitXorRegReg(X64Register::RDX);
			}
			
			emitCall("_CxxThrowException");
			// _CxxThrowException is [[noreturn]], but the call pushes a return address on the stack.
			// That return address must fall WITHIN this function's PDATA range [begin, end).
			// Without the int3, return_addr == PDATA_end (exclusive boundary) → the unwinder
			// cannot find this function's PDATA and skips main's try/catch.
			// Emitting int 3 extends the function code by 1 byte so return_addr < PDATA_end.
			textSectionData.push_back(static_cast<char>(0xCC)); // int 3 (unreachable)
		}
	}

	void handleRethrow([[maybe_unused]] const IrInstruction& instruction) {
		// Skip exception handling if disabled - generate abort() instead
		if (!g_enable_exceptions) {
			// Call abort() to terminate when exceptions are disabled
			emitCall("abort");
			return;
		}
		
		// Platform-specific rethrow implementation
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// ========== Linux/ELF (Itanium C++ ABI) ==========
			// Call __cxa_rethrow() with no arguments
			// System V AMD64 calling convention - no arguments needed
			
			emitSubRSP(8); // Align stack to 16 bytes before call
			emitCall("__cxa_rethrow");
			// Note: __cxa_rethrow never returns
			
		} else {
			// ========== Windows/COFF (MSVC ABI) ==========
			// Call _CxxThrowException(NULL, NULL) to rethrow current exception
			// Windows x64 calling convention: RCX, RDX
			
			// Set up arguments for _CxxThrowException to rethrow current exception
			// RCX (first argument) = NULL (rethrow current exception object)
			emitXorRegReg(X64Register::RCX);
			// RDX (second argument) = NULL (rethrow uses current throw info)
			emitXorRegReg(X64Register::RDX);
			
			emitCall("_CxxThrowException");
			// Same PDATA range fix: emit int 3 so the return address is within the function range.
			textSectionData.push_back(static_cast<char>(0xCC)); // int 3 (unreachable)
		}
	}

	// ============================================================================
	// Windows SEH (Structured Exception Handling) Handlers
	// ============================================================================

	void handleSehTryBegin([[maybe_unused]] const IrInstruction& instruction) {
		// SehTryBegin marks the start of a __try block
		// Create a new SEH try block and record the current code offset

		SehTryBlock seh_try_block;
		seh_try_block.try_start_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
		seh_try_block.try_end_offset = 0;  // Will be set in handleSehTryEnd

		current_function_seh_try_blocks_.push_back(seh_try_block);
		seh_try_block_stack_.push_back(current_function_seh_try_blocks_.size() - 1);

		FLASH_LOG(Codegen, Debug, "SEH __try block begin at offset ", seh_try_block.try_start_offset);
	}

	void handleSehTryEnd([[maybe_unused]] const IrInstruction& instruction) {
		// SehTryEnd marks the end of a __try block
		// Record the current code offset as the end of the SEH try block

		if (!seh_try_block_stack_.empty()) {
			auto& block = current_function_seh_try_blocks_[seh_try_block_stack_.back()];
			block.try_end_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;

			FLASH_LOG(Codegen, Debug, "SEH __try block end at offset ", block.try_end_offset);

			// Don't pop from stack yet - we need the block for the handler
		}
	}

	void handleSehExceptBegin(const IrInstruction& instruction) {
		// SehExceptBegin marks the start of a __except handler
		// Record this handler in the most recent SEH try block

		const auto& except_op = instruction.getTypedPayload<SehExceptBeginOp>();

		if (!seh_try_block_stack_.empty()) {
			auto& block = current_function_seh_try_blocks_[seh_try_block_stack_.back()];
			SehExceptHandler handler;
			handler.handler_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
			handler.filter_result = except_op.filter_result.var_number;
			handler.is_constant_filter = except_op.is_constant_filter;
			handler.constant_filter_value = except_op.constant_filter_value;

			// For non-constant filters, use the most recently recorded filter funclet offset
			if (!except_op.is_constant_filter) {
				handler.filter_funclet_offset = current_seh_filter_funclet_offset_;
			}

			block.except_handler = handler;

			FLASH_LOG(Codegen, Debug, "SEH __except handler begin at offset ", handler.handler_offset,
			          " is_constant=", handler.is_constant_filter,
			          " constant_value=", handler.constant_filter_value,
			          " filter_result=", handler.filter_result,
			          " filter_funclet=", handler.filter_funclet_offset);
		}
	}

	void handleSehExceptEnd([[maybe_unused]] const IrInstruction& instruction) {
		// SehExceptEnd marks the end of a __except handler
		// Pop this try block from the stack

		FLASH_LOG(Codegen, Debug, "SEH __except handler end at offset ",
		          static_cast<uint32_t>(textSectionData.size()) - current_function_offset_);

		if (!seh_try_block_stack_.empty()) {
			seh_try_block_stack_.pop_back();
		}
	}

	void handleSehFinallyCall(const IrInstruction& instruction) {
		// SehFinallyCall: normal-flow call to the __finally funclet
		// Emits: xor ecx,ecx; mov rdx,rbp; call funclet_label; jmp end_label
		// ECX=0 means "normal termination" (AbnormalTermination() returns false)
		// RDX=frame pointer (establisher frame for the funclet)

		const auto& op = instruction.getTypedPayload<SehFinallyCallOp>();

		FLASH_LOG(Codegen, Debug, "SEH __finally call: funclet=", op.funclet_label, " end=", op.end_label);

		// Flush all dirty registers before the call
		flushAllDirtyRegisters();

		// xor ecx, ecx (AbnormalTermination = false)
		// Use 32-bit XOR (no REX.W) since we only need ECX zeroed
		textSectionData.push_back(0x31); // XOR r/m32, r32
		textSectionData.push_back(0xC9); // ModR/M: ECX, ECX

		// mov rdx, rbp (establisher frame)
		emitMovRegReg(X64Register::RDX, X64Register::RBP);

		// call funclet_label (CALL rel32 - patched later)
		textSectionData.push_back(0xE8);
		uint32_t call_patch = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		pending_branches_.push_back({StringTable::getOrInternStringHandle(op.funclet_label), call_patch});

		// jmp end_label (JMP rel32 - patched later)
		textSectionData.push_back(0xE9);
		uint32_t jmp_patch = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		pending_branches_.push_back({StringTable::getOrInternStringHandle(op.end_label), jmp_patch});
	}

	void handleSehFinallyBegin([[maybe_unused]] const IrInstruction& instruction) {
		// SehFinallyBegin marks the start of a __finally funclet
		// This is both the normal-flow entry and the unwind-phase entry.
		// __C_specific_handler calls it with: ECX=AbnormalTermination, RDX=EstablisherFrame
		// Normal flow calls it with: ECX=0, RDX=RBP

		// Record handler offset in the current SEH try block
		if (!seh_try_block_stack_.empty()) {
			auto& block = current_function_seh_try_blocks_[seh_try_block_stack_.back()];
			SehFinallyHandler handler;
			handler.handler_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;

			block.finally_handler = handler;

			FLASH_LOG(Codegen, Debug, "SEH __finally funclet begin at offset ", handler.handler_offset);
		}

		// Emit funclet prologue:
		//   push rbp
		//   sub rsp, 32     (shadow space for any calls within __finally)
		//   mov rbp, rdx    (set RBP to establisher frame so local vars are accessible)
		//   mov [rsp+8], ecx  (save AbnormalTermination flag for _abnormal_termination() intrinsic)
		emitPushReg(X64Register::RBP);
		emitSubRSP(32);
		emitMovRegReg(X64Register::RBP, X64Register::RDX);

		// Save AbnormalTermination (ECX) to shadow space slot 2 ([rsp+0x08])
		// _abnormal_termination() will load it from there.
		// mov dword ptr [rsp+8], ecx  (89 4C 24 08)
		textSectionData.push_back(static_cast<char>(0x89));
		textSectionData.push_back(static_cast<char>(0x4C));
		textSectionData.push_back(0x24);
		textSectionData.push_back(0x08);
	}

	void handleSehFinallyEnd([[maybe_unused]] const IrInstruction& instruction) {
		// SehFinallyEnd marks the end of a __finally funclet
		// Emit funclet epilogue + ret

		FLASH_LOG(Codegen, Debug, "SEH __finally funclet end at offset ",
		          static_cast<uint32_t>(textSectionData.size()) - current_function_offset_);

		// Flush all dirty registers before returning
		flushAllDirtyRegisters();

		// Emit funclet epilogue:
		//   add rsp, 32
		//   pop rbp
		//   ret
		emitAddRSP(32);
		emitPopReg(X64Register::RBP);
		textSectionData.push_back(0xC3); // RET

		// Pop this try block from the stack
		if (!seh_try_block_stack_.empty()) {
			seh_try_block_stack_.pop_back();
		}
	}

	void handleSehFilterBegin([[maybe_unused]] const IrInstruction& instruction) {
		// SehFilterBegin marks the start of a filter funclet
		// __C_specific_handler calls it with: RCX=EXCEPTION_POINTERS*, RDX=EstablisherFrame
		// The filter must return the filter result in EAX

		// Record filter funclet offset
		current_seh_filter_funclet_offset_ = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;

		FLASH_LOG(Codegen, Debug, "SEH filter funclet begin at offset ", current_seh_filter_funclet_offset_);

		// Emit funclet prologue:
		//   push rbp
		//   sub rsp, 32     (shadow space)
		//   mov rbp, rdx    (set RBP to establisher frame so local vars are accessible)
		emitPushReg(X64Register::RBP);
		emitSubRSP(32);
		emitMovRegReg(X64Register::RBP, X64Register::RDX);

		// Save EXCEPTION_POINTERS* (RCX) to shadow space slot 2 ([rsp+0x08])
		// so GetExceptionCode() / GetExceptionInformation() can load it after RCX may be clobbered.
		// mov qword ptr [rsp+8], rcx  (48 89 4C 24 08)
		textSectionData.push_back(0x48);
		textSectionData.push_back(static_cast<char>(0x89));
		textSectionData.push_back(static_cast<char>(0x4C));
		textSectionData.push_back(0x24);
		textSectionData.push_back(0x08);
	}

	void handleSehGetExceptionCode(const IrInstruction& instruction) {
		// SehGetExceptionCode: GetExceptionCode() intrinsic in filter funclet
		// EXCEPTION_POINTERS* was saved to [rsp+0x08] in handleSehFilterBegin
		// EXCEPTION_POINTERS->ExceptionRecord = [RCX+0] (pointer to EXCEPTION_RECORD)
		// EXCEPTION_RECORD->ExceptionCode     = [ExceptionRecord+0] (DWORD at offset 0)
		const auto& op = instruction.getTypedPayload<SehExceptionIntrinsicOp>();

		// mov rax, [rsp+0x08]   ; EXCEPTION_POINTERS* (48 8B 44 24 08)
		textSectionData.push_back(0x48);
		textSectionData.push_back(static_cast<char>(0x8B));
		textSectionData.push_back(0x44);
		textSectionData.push_back(0x24);
		textSectionData.push_back(0x08);
		// mov rax, [rax]        ; ExceptionRecord* (48 8B 00)
		textSectionData.push_back(0x48);
		textSectionData.push_back(static_cast<char>(0x8B));
		textSectionData.push_back(0x00);
		// mov eax, [rax]        ; ExceptionCode (DWORD) (8B 00)
		textSectionData.push_back(static_cast<char>(0x8B));
		textSectionData.push_back(0x00);

		// Store 32-bit result to the result temp var (accessible via parent's RBP)
		int32_t result_offset = getStackOffsetFromTempVar(op.result, 32);
		emitMovToFrameBySize(X64Register::RAX, result_offset, 32);

		FLASH_LOG(Codegen, Debug, "SehGetExceptionCode: result temp at [rbp+", result_offset, "]");
	}

	void handleSehGetExceptionInfo(const IrInstruction& instruction) {
		// SehGetExceptionInfo: GetExceptionInformation() intrinsic in filter funclet
		// Returns the EXCEPTION_POINTERS* that was saved to [rsp+0x08] in handleSehFilterBegin
		const auto& op = instruction.getTypedPayload<SehExceptionIntrinsicOp>();

		// mov rax, [rsp+0x08]   ; EXCEPTION_POINTERS* (48 8B 44 24 08)
		textSectionData.push_back(0x48);
		textSectionData.push_back(static_cast<char>(0x8B));
		textSectionData.push_back(0x44);
		textSectionData.push_back(0x24);
		textSectionData.push_back(0x08);

		// Store 64-bit pointer to the result temp var (accessible via parent's RBP)
		int32_t result_offset = getStackOffsetFromTempVar(op.result, 64);
		emitMovToFrameBySize(X64Register::RAX, result_offset, 64);

		FLASH_LOG(Codegen, Debug, "SehGetExceptionInfo: result temp at [rbp+", result_offset, "]");
	}

	void handleSehFilterEnd(const IrInstruction& instruction) {
		// SehFilterEnd marks the end of a filter funclet
		// Move filter result to EAX and return

		const auto& op = instruction.getTypedPayload<SehFilterEndOp>();

		FLASH_LOG(Codegen, Debug, "SEH filter funclet end, result temp=", op.filter_result.var_number,
		          " is_constant=", op.is_constant_result);

		// Flush all dirty registers to ensure filter result is on the stack
		flushAllDirtyRegisters();

		// Load the filter result into EAX
		if (op.is_constant_result) {
			// Constant filter result (e.g. from a comma expression ending in a literal)
			// mov eax, imm32
			emitMovImm32(X64Register::RAX, static_cast<uint32_t>(op.constant_result));
		} else {
			// Load the filter result from its stack slot via RBP-relative addressing
			int32_t filter_offset = getStackOffsetFromTempVar(op.filter_result, 32);
			emitMovFromFrameBySize(X64Register::RAX, filter_offset, 32);
		}

		// Emit funclet epilogue:
		//   add rsp, 32
		//   pop rbp
		//   ret
		emitAddRSP(32);
		emitPopReg(X64Register::RBP);
		textSectionData.push_back(0xC3); // RET
	}

	void handleSehSaveExceptionCode(const IrInstruction& instruction) {
		// SehSaveExceptionCode: called at start of filter funclet.
		// EXCEPTION_POINTERS* was saved to [rsp+0x08] in handleSehFilterBegin.
		// This handler reads ExceptionCode and writes it to a parent-frame slot so
		// GetExceptionCode() works in the __except body (not just the filter expression).
		const auto& op = instruction.getTypedPayload<SehSaveExceptionCodeOp>();

		// mov rax, [rsp+0x08]   ; EXCEPTION_POINTERS* (48 8B 44 24 08)
		textSectionData.push_back(0x48);
		textSectionData.push_back(static_cast<char>(0x8B));
		textSectionData.push_back(0x44);
		textSectionData.push_back(0x24);
		textSectionData.push_back(0x08);
		// mov rax, [rax]        ; ExceptionRecord* (48 8B 00)
		textSectionData.push_back(0x48);
		textSectionData.push_back(static_cast<char>(0x8B));
		textSectionData.push_back(0x00);
		// mov eax, [rax]        ; ExceptionCode (DWORD) (8B 00)
		textSectionData.push_back(static_cast<char>(0x8B));
		textSectionData.push_back(0x00);

		// Store 32-bit ExceptionCode to the parent-frame slot (accessible via RBP)
		int32_t saved_offset = getStackOffsetFromTempVar(op.saved_var, 32);
		emitMovToFrameBySize(X64Register::RAX, saved_offset, 32);

		FLASH_LOG(Codegen, Debug, "SehSaveExceptionCode: saved to [rbp+", saved_offset, "]");
	}

	void handleSehGetExceptionCodeBody(const IrInstruction& instruction) {
		// SehGetExceptionCodeBody: load ExceptionCode from parent-frame slot
		// (the slot was written by handleSehSaveExceptionCode in the filter funclet)
		const auto& op = instruction.getTypedPayload<SehGetExceptionCodeBodyOp>();

		// Load saved ExceptionCode from parent-frame slot
		int32_t saved_offset = getStackOffsetFromTempVar(op.saved_var, 32);
		emitMovFromFrameBySize(X64Register::RAX, saved_offset, 32);

		// Store to result slot
		int32_t result_offset = getStackOffsetFromTempVar(op.result, 32);
		emitMovToFrameBySize(X64Register::RAX, result_offset, 32);

		FLASH_LOG(Codegen, Debug, "SehGetExceptionCodeBody: from [rbp+", saved_offset, "] -> result [rbp+", result_offset, "]");
	}

	void handleSehAbnormalTermination(const IrInstruction& instruction) {
		// SehAbnormalTermination: _abnormal_termination() / AbnormalTermination() intrinsic
		// Only valid inside a __finally funclet.
		// ECX was saved to [rsp+0x08] in handleSehFinallyBegin.
		// Returns: 0 if __finally running due to normal control flow,
		//          non-zero if __finally running during exception unwind.
		const auto& op = instruction.getTypedPayload<SehAbnormalTerminationOp>();

		// mov eax, [rsp+0x08]   ; AbnormalTermination flag (8B 44 24 08)
		textSectionData.push_back(static_cast<char>(0x8B));
		textSectionData.push_back(0x44);
		textSectionData.push_back(0x24);
		textSectionData.push_back(0x08);

		// Store 32-bit result to result slot
		int32_t result_offset = getStackOffsetFromTempVar(op.result, 32);
		emitMovToFrameBySize(X64Register::RAX, result_offset, 32);

		FLASH_LOG(Codegen, Debug, "SehAbnormalTermination: result at [rbp+", result_offset, "]");
	}

	void handleSehLeave(const IrInstruction& instruction) {
		// SehLeave implements the __leave statement
		// This should jump to the end of the current __try block (or __finally if present)

		const auto& leave_op = instruction.getTypedPayload<SehLeaveOp>();
		std::string_view target_label = leave_op.target_label;

		FLASH_LOG(Codegen, Debug, "SEH __leave statement at offset ",
		          static_cast<uint32_t>(textSectionData.size()) - current_function_offset_,
		          " target=", target_label);

		// Flush all dirty registers before jumping
		flushAllDirtyRegisters();

		// Generate JMP instruction (E9 + 32-bit relative offset)
		// We'll use a placeholder offset and fix it up later
		textSectionData.push_back(0xE9); // JMP rel32

		// Store position where we need to patch the offset
		uint32_t patch_position = static_cast<uint32_t>(textSectionData.size());

		// Add placeholder offset (will be patched later)
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Record this jump for later patching (convert string_view to StringHandle)
		pending_branches_.push_back({StringTable::getOrInternStringHandle(target_label), patch_position});
	}

