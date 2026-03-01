	void handleLabel(const IrInstruction& instruction) {
		// Label instruction: mark a position in code for jumps
			assert(instruction.hasTypedPayload() && "Label instruction must use typed payload");
		const auto& label_op = instruction.getTypedPayload<LabelOp>();
		std::string_view label_name = StringTable::getStringView(label_op.getLabelName());  // Phase 4: Use helper

		// Store the current code offset for this label
		uint32_t label_offset = static_cast<uint32_t>(textSectionData.size());

		// Track label positions for later resolution
		std::string label_name_str(label_name);
		if (label_positions_.find(StringTable::getOrInternStringHandle(label_name_str)) == label_positions_.end()) {
			label_positions_[StringTable::getOrInternStringHandle(label_name_str)] = label_offset;
		}

		if constexpr (!std::is_same_v<TWriterClass, ElfFileWriter>) {
			StringHandle label_handle = StringTable::getOrInternStringHandle(label_name_str);
			auto bridge_it = catch_return_bridges_.find(label_handle);
			if (bridge_it != catch_return_bridges_.end()) {
				const CatchReturnBridge& bridge = bridge_it->second;
				emitMovFromFrameBySize(X64Register::RCX, bridge.flag_slot_offset, 64);
				emitTestRegReg(X64Register::RCX);

				textSectionData.push_back(0x0F);
				textSectionData.push_back(0x84);
				uint32_t skip_patch = static_cast<uint32_t>(textSectionData.size());
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);

				emitXorRegReg(X64Register::RCX);
				emitMovToFrame(X64Register::RCX, bridge.flag_slot_offset, 64);
				if (!bridge.is_float) {
					emitMovFromFrameBySize(X64Register::RAX, bridge.return_slot_offset, bridge.return_size_bits);
				}
				textSectionData.push_back(0x48);
				textSectionData.push_back(0x89);
				textSectionData.push_back(0xEC);
				textSectionData.push_back(0x5D);
				textSectionData.push_back(0xC3);

				uint32_t skip_target = static_cast<uint32_t>(textSectionData.size());
				int32_t rel = static_cast<int32_t>(skip_target) - static_cast<int32_t>(skip_patch + 4);
				textSectionData[skip_patch + 0] = static_cast<uint8_t>(rel & 0xFF);
				textSectionData[skip_patch + 1] = static_cast<uint8_t>((rel >> 8) & 0xFF);
				textSectionData[skip_patch + 2] = static_cast<uint8_t>((rel >> 16) & 0xFF);
				textSectionData[skip_patch + 3] = static_cast<uint8_t>((rel >> 24) & 0xFF);
			}
		}

		// Flush all dirty registers at label boundaries to ensure correct state
		flushAllDirtyRegisters();
		
		// Release all register allocations at merge points (labels)
		// Different execution paths may have left different values in registers,
		// so we can't trust that a register still holds a particular variable
		regAlloc.reset();
	}

	void handleBranch(const IrInstruction& instruction) {
	// Unconditional branch: jmp label
	assert(instruction.hasTypedPayload() && "Branch instruction must use typed payload");
	const auto& branch_op = instruction.getTypedPayload<BranchOp>();
	std::string_view target_label = StringTable::getStringView(branch_op.getTargetLabel());  // Phase 4: Use helper
		// Flush all dirty registers before branching
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

		// Record this branch for later patching
		pending_branches_.push_back({StringTable::getOrInternStringHandle(target_label), patch_position});
	}

	void handleLoopBegin(const IrInstruction& instruction) {
		// LoopBegin marks the start of a loop and provides labels for break/continue
		assert(instruction.hasTypedPayload() && "LoopBegin must use typed payload");
		const auto& op = instruction.getTypedPayload<LoopBeginOp>();
		//StringHandle loop_start_label = op.loop_start_label;
		StringHandle loop_end_label = op.loop_end_label;
		StringHandle loop_increment_label = op.loop_increment_label;
		
		// Push loop context onto stack for break/continue handling
		loop_context_stack_.push_back({loop_end_label, 
		                                loop_increment_label});

		// Flush all dirty registers at loop boundaries
		flushAllDirtyRegisters();
	}

	void handleLoopEnd(const IrInstruction& instruction) {
		// LoopEnd marks the end of a loop
		assert(instruction.getOperandCount() == 0 && "LoopEnd must have 0 operands");

		// Pop loop context from stack
		if (!loop_context_stack_.empty()) {
			loop_context_stack_.pop_back();
		}

		// Flush all dirty registers at loop boundaries
		flushAllDirtyRegisters();
	}

	void handleBreak(const IrInstruction& instruction) {
		// Break: unconditional jump to loop end label
		assert(instruction.getOperandCount() == 0 && "Break must have 0 operands");
		assert(!loop_context_stack_.empty() && "Break must be inside a loop");

		auto& loop_context = loop_context_stack_.back();
		auto target_label = loop_context.loop_end_label;

		// Flush all dirty registers before branching
		flushAllDirtyRegisters();

		// Generate JMP instruction to loop end
		textSectionData.push_back(0xE9); // JMP rel32

		// Store position where we need to patch the offset
		uint32_t patch_position = static_cast<uint32_t>(textSectionData.size());

		// Add placeholder offset (will be patched later)
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Record this branch for later patching
		pending_branches_.push_back({target_label, patch_position});
	}

	void handleContinue(const IrInstruction& instruction) {
		// Continue: unconditional jump to loop increment label
		assert(instruction.getOperandCount() == 0 && "Continue must have 0 operands");
		assert(!loop_context_stack_.empty() && "Continue must be inside a loop");

		auto& loop_context = loop_context_stack_.back();
		auto target_label = loop_context.loop_increment_label;

		// Flush all dirty registers before branching
		flushAllDirtyRegisters();

		// Generate JMP instruction to loop increment
		textSectionData.push_back(0xE9); // JMP rel32

		// Store position where we need to patch the offset
		uint32_t patch_position = static_cast<uint32_t>(textSectionData.size());

		// Add placeholder offset (will be patched later)
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Record this branch for later patching
		pending_branches_.push_back({target_label, patch_position});
	}

	void handleArrayAccess(const IrInstruction& instruction) {
		assert(instruction.hasTypedPayload() && "ArrayAccess without typed payload - should not happen");
		
		// Flush all registers to memory before array access
		// This ensures any previously computed values in registers are saved
		flushAllDirtyRegisters();
		
		const ArrayAccessOp& op = std::any_cast<const ArrayAccessOp&>(instruction.getTypedPayload());
			
		TempVar result_var = op.result;
		int element_size_bits = op.element_size_in_bits;
		int element_size_bytes = element_size_bits / 8;
		Type element_type = op.element_type;
		bool is_floating_point = (element_type == Type::Float || element_type == Type::Double);
		bool is_float = (element_type == Type::Float);
		bool is_struct = is_struct_type(element_type);
		
		// Phase 5 Optimization: Use value category metadata for LEA vs MOV decision
		// For struct types, always use LEA (original behavior)
		// For primitive lvalues, we could use LEA but need to handle dereferencing correctly
		// For now, only optimize struct types to avoid breaking existing code
		bool result_is_lvalue = isTempVarLValue(result_var);
		bool optimize_lea = is_struct;  // Conservative: only struct types for now
		
		FLASH_LOG_FORMAT(Codegen, Debug, 
			"ArrayAccess: is_struct={} is_lvalue={} optimize_lea={}",
			is_struct, result_is_lvalue, optimize_lea);
		
		// For floating-point, we'll use XMM0 for the loaded value
		// For integers and struct addresses, we allocate a general-purpose register
		X64Register base_reg = allocateRegisterWithSpilling();
		
		// Get the array base address (from stack or register)
		int64_t array_base_offset = 0;
		bool is_array_pointer = op.is_pointer_to_array;  // Use flag from codegen
		StringHandle array_name_handle;
		std::string_view array_name_view;
			
		if (std::holds_alternative<StringHandle>(op.array)) {
			array_name_handle = std::get<StringHandle>(op.array);
			array_name_view = StringTable::getStringView(array_name_handle);
		} else if (std::holds_alternative<TempVar>(op.array)) {
			TempVar array_temp_var = std::get<TempVar>(op.array);
			array_base_offset = getStackOffsetFromTempVar(array_temp_var);
			is_array_pointer = true;  // TempVar always means pointer
		}
			
		// Check if this is a member array access (object.member format)
		bool is_member_array = false;
		std::string_view object_name;
		std::string_view member_name;
		int64_t member_offset = op.member_offset;  // Get from payload
			
		// Check if the object (not the array) is a pointer (like 'this' or a reference)
		bool is_object_pointer = false;
		
		if (!array_name_view.empty()) {
			is_member_array = array_name_view.find('.') != std::string::npos;
			if (is_member_array) {
				// Parse object.member
				size_t dot_pos = array_name_view.find('.');
				object_name = array_name_view.substr(0, dot_pos);
				member_name = array_name_view.substr(dot_pos + 1);
				// Update array_base_offset to point to the object
				StringHandle object_name_handle = StringTable::getOrInternStringHandle(object_name);
				array_base_offset = variable_scopes.back().variables[object_name_handle].offset;
				
				// Check if object is 'this' or a reference parameter (both need pointer dereferencing)
				if (object_name == "this"sv || reference_stack_info_.count(array_base_offset) > 0) {
					is_object_pointer = true;
				}
			} else {
				// Regular array/pointer - get offset directly
				array_base_offset = variable_scopes.back().variables[array_name_handle].offset;
			}
		}
			
		// Get the result storage location
		int64_t result_offset = getStackOffsetFromTempVar(result_var);
			
		// Handle index value from TypedValue
		if (std::holds_alternative<unsigned long long>(op.index.value)) {
			// Constant index
			uint64_t index_value = std::get<unsigned long long>(op.index.value);

			if (is_array_pointer || is_object_pointer) {
				// Array is a pointer/temp var, or member array of a pointer object (like this.values[i])
				// Load pointer and compute address
				auto load_ptr_opcodes = generatePtrMovFromFrame(base_reg, array_base_offset);
				textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
					                    load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);

				// Add member offset + index offset to pointer
				// For is_object_pointer: total offset = member_offset + (index * element_size)
				// For is_array_pointer: total offset = index * element_size (member_offset is 0)
				int64_t offset_bytes = member_offset + (index_value * element_size_bytes);
				if (offset_bytes != 0) {
					emitAddImmToReg(textSectionData, base_reg, offset_bytes);
				}

				// Phase 5: Use optimize_lea for LEA vs MOV decision
				// For struct types or lvalues, keep the address in base_reg
				// For primitive prvalues, load the value
				if (!optimize_lea) {
					// Load value from [base_reg] with appropriate instruction
					if (is_floating_point) {
						emitFloatLoadFromAddressInReg(textSectionData, X64Register::XMM0, base_reg, is_float);
					} else {
						emitLoadFromAddressInReg(textSectionData, base_reg, base_reg, element_size_bytes);
					}
				}
			} else {
				// Array is a regular variable - use direct stack offset
				int64_t element_offset = array_base_offset + member_offset + (index_value * element_size_bytes);
					
				// Phase 5: Use optimize_lea for LEA vs MOV decision
				if (optimize_lea) {
					// For struct types or lvalues, compute the address using LEA
					emitLEAFromFrame(textSectionData, base_reg, element_offset);
				} else {
					// Load from [RBP + offset] with appropriate instruction
					if (is_floating_point) {
						emitFloatMovFromFrame(X64Register::XMM0, static_cast<int32_t>(element_offset), is_float);
					} else {
						emitMovFromFrameSized(
							SizedRegister{base_reg, 64, false},
							SizedStackSlot{static_cast<int32_t>(element_offset), element_size_bits, isSignedType(op.element_type)}
						);
					}
				}
			}
		} else if (std::holds_alternative<TempVar>(op.index.value)) {
			// Variable index - need to compute address at runtime
			TempVar index_var = std::get<TempVar>(op.index.value);
			int64_t index_var_offset = getStackOffsetFromTempVar(index_var);
			
			// Allocate a second register for the index, excluding base_reg to avoid conflicts
			X64Register index_reg = allocateRegisterWithSpilling(base_reg);
			FLASH_LOG_FORMAT(Codegen, Debug, "ArrayAccess TempVar: base_reg={}, index_reg={}, array_base_offset={}, index_var_offset={}",
				static_cast<int>(base_reg), static_cast<int>(index_reg), array_base_offset, index_var_offset);

			if (is_array_pointer || is_object_pointer) {
				// Array is a pointer/temp var, or member array of a pointer object (like this.values[i])
				auto load_ptr_opcodes = generatePtrMovFromFrame(base_reg, array_base_offset);
				textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
					                    load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);

				// Add member offset for pointer objects (e.g., this->member)
				if (is_object_pointer && member_offset != 0) {
					emitAddImmToReg(textSectionData, base_reg, member_offset);
				}

				// Load index with proper sign extension based on index type
				bool is_signed = isSignedType(op.index.type);
				emitMovFromFrameSized(
					SizedRegister{index_reg, 64, false},
					SizedStackSlot{static_cast<int32_t>(index_var_offset), op.index.size_in_bits, is_signed}
				);
				emitMultiplyRegByElementSize(textSectionData, index_reg, element_size_bytes);
				emitAddRegs(textSectionData, base_reg, index_reg);
				
				// Phase 5: Use optimize_lea for LEA vs MOV decision
				// For struct types or lvalues, keep the address in base_reg
				// For primitive prvalues, load the value
				if (!optimize_lea) {
					if (is_floating_point) {
						emitFloatLoadFromAddressInReg(textSectionData, X64Register::XMM0, base_reg, is_float);
					} else {
						emitLoadFromAddressInReg(textSectionData, base_reg, base_reg, element_size_bytes);
					}
				}
			} else {
				// Array is a regular variable
				// Load index with proper sign extension based on index type
				bool is_signed = isSignedType(op.index.type);
				emitMovFromFrameSized(
					SizedRegister{index_reg, 64, false},
					SizedStackSlot{static_cast<int32_t>(index_var_offset), op.index.size_in_bits, is_signed}
				);
				emitMultiplyRegByElementSize(textSectionData, index_reg, element_size_bytes);
					
				int64_t combined_offset = array_base_offset + member_offset;
				emitLEAFromFrame(textSectionData, base_reg, combined_offset);
				emitAddRegs(textSectionData, base_reg, index_reg);
				
				// Phase 5: Use optimize_lea for LEA vs MOV decision
				// For struct types or lvalues, keep the address in base_reg
				// For primitive prvalues, load the value
				if (!optimize_lea) {
					if (is_floating_point) {
						emitFloatLoadFromAddressInReg(textSectionData, X64Register::XMM0, base_reg, is_float);
					} else {
						emitLoadFromAddressInReg(textSectionData, base_reg, base_reg, element_size_bytes);
					}
				}
			}
			
			// Release the index register
			regAlloc.release(index_reg);
		} else if (std::holds_alternative<StringHandle>(op.index.value)) {
			// Variable index stored as identifier name
			StringHandle index_var_name_handle = std::get<StringHandle>(op.index.value);
			auto index_it = variable_scopes.back().variables.find(index_var_name_handle);
			assert(index_it != variable_scopes.back().variables.end() && "Index variable not found");
			int64_t index_var_offset = index_it->second.offset;
			
			// Allocate a second register for the index
			X64Register index_reg = allocateRegisterWithSpilling();

			if (is_array_pointer || is_object_pointer) {
				// Array is a pointer/temp var, or member array of a pointer object
				auto load_ptr_opcodes = generatePtrMovFromFrame(base_reg, array_base_offset);
				textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
					                    load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);
				
				// Add member offset for pointer objects (e.g., this->member)
				if (is_object_pointer && member_offset != 0) {
					emitAddImmToReg(textSectionData, base_reg, member_offset);
				}
			} else {
				int64_t combined_offset = array_base_offset + member_offset;
				emitLEAFromFrame(textSectionData, base_reg, combined_offset);
			}
			
			// Load index into index_reg with proper sign extension based on index type
			bool is_signed = isSignedType(op.index.type);
			emitMovFromFrameSized(
				SizedRegister{index_reg, 64, false},
				SizedStackSlot{static_cast<int32_t>(index_var_offset), op.index.size_in_bits, is_signed}
			);
			
			emitMultiplyRegByElementSize(textSectionData, index_reg, element_size_bytes);
			emitAddRegs(textSectionData, base_reg, index_reg);
			
			// Phase 5: Use optimize_lea for LEA vs MOV decision
			// For struct types or lvalues, keep the address in base_reg
			// For primitive prvalues, load the value
			if (!optimize_lea) {
				if (is_floating_point) {
					emitFloatLoadFromAddressInReg(textSectionData, X64Register::XMM0, base_reg, is_float);
				} else {
					emitLoadFromAddressInReg(textSectionData, base_reg, base_reg, element_size_bytes);
				}
			}
			
			// Release the index register
			regAlloc.release(index_reg);
		}
			
		// Store result in temp variable's stack location
		if (is_floating_point) {
			emitFloatMovToFrame(X64Register::XMM0, static_cast<int32_t>(result_offset), is_float);
		} else {
			emitMovToFrameSized(
				SizedRegister{base_reg, 64, false},  // source: 64-bit register
				SizedStackSlot{static_cast<int32_t>(result_offset), 64, false}  // dest: 64-bit
			);
		}
		
		// Phase 5: Mark the result temp var as holding a pointer/reference when using LEA
		// This allows subsequent operations to properly handle the address
		if (optimize_lea) {
			setReferenceInfo(result_offset, element_type, element_size_bits, false, result_var);
		}
		
		// Release the base register
		regAlloc.release(base_reg);
	}


	void handleArrayElementAddress(const IrInstruction& instruction) {
		// Flush dirty registers to ensure index values are in memory
		flushAllDirtyRegisters();
		
		// Try typed payload first
		if (instruction.hasTypedPayload()) {
			const ArrayElementAddressOp& op = std::any_cast<const ArrayElementAddressOp&>(instruction.getTypedPayload());
			
			TempVar result_var = op.result;
			int element_size_bits = op.element_size_in_bits;
			int element_size_bytes = element_size_bits / 8;
			bool is_pointer_to_array = op.is_pointer_to_array;
			
			// Get the array base address
			int64_t array_base_offset = 0;
			if (std::holds_alternative<StringHandle>(op.array)) {
				StringHandle array_name_handle = std::get<StringHandle>(op.array);
				array_base_offset = variable_scopes.back().variables[array_name_handle].offset;
			} else if (std::holds_alternative<TempVar>(op.array)) {
				TempVar array_temp = std::get<TempVar>(op.array);
				array_base_offset = getStackOffsetFromTempVar(array_temp);
			}
			
			// Get result storage location
			int64_t result_offset = getStackOffsetFromTempVar(result_var);
			
			// Handle constant or variable index
			if (std::holds_alternative<unsigned long long>(op.index.value)) {
				uint64_t index_value = std::get<unsigned long long>(op.index.value);

				if (is_pointer_to_array) {
					// Array is a pointer/reference - load it first, then add offset
					auto load_ptr_opcodes = generatePtrMovFromFrame(X64Register::RAX, array_base_offset);
					textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
						                    load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);

					// Add element offset to pointer
					int64_t offset_bytes = index_value * element_size_bytes;
					if (offset_bytes != 0) {
						emitAddImmToReg(textSectionData, X64Register::RAX, offset_bytes);
					}
				} else {
					// Array is a regular variable - use direct stack offset
					int64_t element_offset = array_base_offset + (index_value * element_size_bytes);
					
					// LEA RAX, [RBP + element_offset]
					textSectionData.push_back(0x48); // REX.W
					textSectionData.push_back(0x8D); // LEA r64, m
					
					if (element_offset >= -128 && element_offset <= 127) {
						textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
						textSectionData.push_back(static_cast<uint8_t>(element_offset));
					} else {
						textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
						uint32_t offset_u32 = static_cast<uint32_t>(static_cast<int32_t>(element_offset));
						textSectionData.push_back(offset_u32 & 0xFF);
						textSectionData.push_back((offset_u32 >> 8) & 0xFF);
						textSectionData.push_back((offset_u32 >> 16) & 0xFF);
						textSectionData.push_back((offset_u32 >> 24) & 0xFF);
					}
				}
			} else if (std::holds_alternative<TempVar>(op.index.value)) {
				TempVar index_var = std::get<TempVar>(op.index.value);
				int64_t index_offset = getStackOffsetFromTempVar(index_var);
				
				// Load index: source (sized stack slot) -> dest (64-bit RCX)
				emitMovFromFrameSized(
					SizedRegister{X64Register::RCX, 64, false},  // dest: 64-bit register
					SizedStackSlot{static_cast<int32_t>(index_offset), op.index.size_in_bits, isSignedType(op.index.type)}  // source: index from stack
				);
				
				// Multiply index by element size
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);
				
				if (is_pointer_to_array) {
					// Array is a pointer/reference - load the pointer value first
					auto load_ptr_opcodes = generatePtrMovFromFrame(X64Register::RAX, array_base_offset);
					textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
						                    load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);
				} else {
				// Load address of array base into RAX
				emitLeaFromFrame(X64Register::RAX, array_base_offset);
				}
				
				// Add offset to get final address
				emitAddRAXRCX(textSectionData);
			} else if (std::holds_alternative<StringHandle>(op.index.value)) {
				// Handle variable name (StringHandle) as index
				StringHandle index_var_name = std::get<StringHandle>(op.index.value);
				auto it = variable_scopes.back().variables.find(index_var_name);
				if (it == variable_scopes.back().variables.end()) {
					throw InternalError("Index variable not found in scope");
					return;
				}
				int64_t index_offset = it->second.offset;
				
				// Load index: source (sized stack slot) -> dest (64-bit RCX)
				emitMovFromFrameSized(
					SizedRegister{X64Register::RCX, 64, false},  // dest: 64-bit register
					SizedStackSlot{static_cast<int32_t>(index_offset), op.index.size_in_bits, isSignedType(op.index.type)}  // source: index from stack
				);
				
				// Multiply index by element size
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);
				
				if (is_pointer_to_array) {
					// Array is a pointer/reference - load the pointer value first
					auto load_ptr_opcodes = generatePtrMovFromFrame(X64Register::RAX, array_base_offset);
					textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
						                    load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);
				} else {
				// Load address of array base into RAX
				emitLeaFromFrame(X64Register::RAX, array_base_offset);
				}
				
				// Add offset to get final address
				emitAddRAXRCX(textSectionData);
			}
			
			// Store the computed address to result_var
			auto store_opcodes = generatePtrMovToFrame(X64Register::RAX, result_offset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
			                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
			return;
		}
		
		// Legacy operand-based format
		// All array element address now uses typed payload - no legacy code path
		throw InternalError("ArrayElementAddress without typed payload - should not happen");
	}


	void handleArrayStore(const IrInstruction& instruction) {
		// Ensure all computed values (especially indices from expressions) are spilled to stack
		// before we load them. This is necessary because variable indices (TempVars) may still
		// be in registers and not yet written to their stack locations.
		flushAllDirtyRegisters();
		
		// Try typed payload first
		if (instruction.hasTypedPayload()) {
			const ArrayStoreOp& op = std::any_cast<const ArrayStoreOp&>(instruction.getTypedPayload());
			
			int element_size_bits = op.element_size_in_bits;
			int element_size_bytes = element_size_bits / 8;
			bool is_pointer_to_array = op.is_pointer_to_array;
			
			// Get the array base address
			StringHandle array_name_handle;
			std::string_view array_name_view;
			int64_t array_base_offset = 0;
			bool array_is_tempvar = false;
			
			if (std::holds_alternative<StringHandle>(op.array)) {
				array_name_handle = std::get<StringHandle>(op.array);
				array_name_view = StringTable::getStringView(array_name_handle);
			} else if (std::holds_alternative<TempVar>(op.array)) {
				// Array is a TempVar (e.g., from member_access for struct.array_member)
				// The TempVar holds a pointer to the array base
				TempVar array_temp = std::get<TempVar>(op.array);
				array_base_offset = getStackOffsetFromTempVar(array_temp);
				array_is_tempvar = true;
			}
			
			// Check if this is a member array access (object.member format)
			bool is_member_array = array_name_view.find('.') != std::string::npos;
			std::string_view object_name;
			std::string_view member_name;
			int64_t member_offset = op.member_offset;  // Get from payload
			
			if (is_member_array) {
				// Parse object.member
				size_t dot_pos = array_name_view.find('.');
				object_name = array_name_view.substr(0, dot_pos);
				member_name = array_name_view.substr(dot_pos + 1);
			}
			
			// Get the value to store into RDX or XMM0 (we use RCX for index, RAX for address)
			bool is_float_store = is_floating_point_type(op.element_type);
			
			if (std::holds_alternative<unsigned long long>(op.value.value)) {
				// Constant value
				uint64_t value = std::get<unsigned long long>(op.value.value);
				if (is_float_store) {
					// For float constants, we need to load into XMM0
					// First load the bit pattern into RDX, then move to XMM0
					emitMovImm64(X64Register::RDX, value);
					// MOVD XMM0, RDX (0x66 0x48 0x0F 0x6E 0xC2)
					textSectionData.push_back(0x66);
					textSectionData.push_back(0x48);
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0x6E);
					textSectionData.push_back(0xC2);
				} else {
					emitMovImm64(X64Register::RDX, value);
				}
			} else if (std::holds_alternative<TempVar>(op.value.value)) {
				// Value from temp var: check if already in register, otherwise load from stack
				TempVar value_var = std::get<TempVar>(op.value.value);
				int64_t value_offset = getStackOffsetFromTempVar(value_var, op.value.size_in_bits);
				
				if (is_float_store) {
					// For floats, check if already in XMM register, otherwise load from stack
					if (auto value_reg = regAlloc.tryGetStackVariableRegister(value_offset); value_reg.has_value()) {
						// Value is already in a register
						// If it's an XMM register and not XMM0, move it
						if (value_reg.value() != X64Register::XMM0) {
							bool is_double = (op.value.size_in_bits == 64);
							emitFloatMovRegToReg(X64Register::XMM0, value_reg.value(), is_double);
						}
					} else {
						// Load float from stack into XMM0
						bool is_double = (op.value.size_in_bits == 64);
						emitFloatMovFromFrame(X64Register::XMM0, value_offset, !is_double);
					}
				} else {
					// Integer/pointer value
					// For pointer array elements, always use element_size_bits (64) not op.value.size_in_bits
					// This ensures pointers are loaded as 64-bit values, not sign-extended 32-bit ints
					int actual_size_bits = element_size_bits;
					
					// Check if value is already in a register
					if (auto value_reg = regAlloc.tryGetStackVariableRegister(value_offset); value_reg.has_value()) {
						// Value is already in a register - move it to RDX if not already there
						if (value_reg.value() != X64Register::RDX) {
							emitMovRegToReg(value_reg.value(), X64Register::RDX, actual_size_bits);
						}
						// If already in RDX, no move needed
					} else {
						// Not in register - load from stack
						// Use element_size_bits to ensure pointers are loaded correctly as 64-bit
						emitMovFromFrameSized(
							SizedRegister{X64Register::RDX, 64, false},  // dest: 64-bit register
							SizedStackSlot{static_cast<int32_t>(value_offset), actual_size_bits, false}  // source: Never sign-extend pointers!
						);
					}
				}
			} else if (std::holds_alternative<StringHandle>(op.value.value)) {
				// Value from named variable (e.g., array_store arr, 0, %pa where pa is a pointer variable)
				StringHandle value_name = std::get<StringHandle>(op.value.value);
				const StackVariableScope& current_scope = variable_scopes.back();
				auto it = current_scope.variables.find(value_name);
				if (it != current_scope.variables.end()) {
					int32_t value_offset = it->second.offset;
					if (is_float_store) {
						if (auto value_reg = regAlloc.tryGetStackVariableRegister(value_offset); value_reg.has_value()) {
							if (value_reg.value() != X64Register::XMM0) {
								bool is_double = (op.value.size_in_bits == 64);
								emitFloatMovRegToReg(X64Register::XMM0, value_reg.value(), is_double);
							}
						} else {
							bool is_double = (op.value.size_in_bits == 64);
							emitFloatMovFromFrame(X64Register::XMM0, value_offset, !is_double);
						}
					} else {
						if (auto value_reg = regAlloc.tryGetStackVariableRegister(value_offset); value_reg.has_value()) {
							if (value_reg.value() != X64Register::RDX) {
								emitMovRegToReg(value_reg.value(), X64Register::RDX, element_size_bits);
							}
						} else {
							emitMovFromFrameSized(
								SizedRegister{X64Register::RDX, 64, false},
								SizedStackSlot{value_offset, element_size_bits, false}
							);
						}
					}
				}
			}
			
			// Get array base offset (only needed if array is StringHandle, not TempVar)
			if (!array_is_tempvar) {
				StringHandle lookup_name_handle = is_member_array ? StringTable::getOrInternStringHandle(object_name) : array_name_handle;
				array_base_offset = variable_scopes.back().variables[lookup_name_handle].offset;
				// Fallback: if not found (offset == INT_MIN), try matching by string to tolerate handle mismatches
				if (array_base_offset == INT_MIN) {
					for (const auto& [handle, info] : variable_scopes.back().variables) {
						if (StringTable::getStringView(handle) == (is_member_array ? object_name : array_name_view)) {
							array_base_offset = info.offset;
							break;
						}
					}
				}
			}
			
			// Check if the object (not the array) is a pointer (like 'this' or a reference)
			bool is_object_pointer = false;
			if (is_member_array) {
				// Check if object is 'this' or a reference parameter
				if (object_name == "this"sv || reference_stack_info_.count(array_base_offset) > 0) {
					is_object_pointer = true;
				}
			}
			
			// When array is from a TempVar (member_access result), it holds a pointer to the array
			// We need to treat it like is_pointer_to_array case
			if (array_is_tempvar) {
				is_pointer_to_array = true;
			}
			
			FLASH_LOG_FORMAT(Codegen, Debug,
				"ArrayStore: is_member_array={}, object_name='{}', is_object_pointer={}, is_pointer_to_array={}, array_is_tempvar={}, array_base_offset={}, member_offset={}",
				is_member_array, (is_member_array ? object_name : "N/A"), is_object_pointer, is_pointer_to_array, array_is_tempvar, array_base_offset, member_offset);
			
			// Handle constant vs variable index
			if (std::holds_alternative<unsigned long long>(op.index.value)) {
				// Constant index
				uint64_t index_value = std::get<unsigned long long>(op.index.value);
				
				if (is_pointer_to_array) {
					// Load the pointer value first
					emitPtrMovFromFrame(X64Register::RAX, array_base_offset);
					
					// Add offset to pointer: ADD RAX, (index * element_size)
					int64_t offset_bytes = index_value * element_size_bytes;
					emitAddImmToReg(textSectionData, X64Register::RAX, offset_bytes);
					
					// Store to [RAX] with appropriate size
					if (is_float_store) {
						// MOVSS/MOVSD [RAX], XMM0
						bool is_double = (element_size_bits == 64);
						if (is_double) {
							textSectionData.push_back(0xF2);  // MOVSD prefix
						} else {
							textSectionData.push_back(0xF3);  // MOVSS prefix
						}
						textSectionData.push_back(0x0F);
						textSectionData.push_back(0x11);  // Store opcode
						textSectionData.push_back(0x00);  // ModR/M: [RAX]
					} else {
						emitStoreToMemory(textSectionData, X64Register::RDX, X64Register::RAX, 0, element_size_bytes);
					}
				} else if (is_object_pointer) {
					// Member array of a pointer object (like this.values[i])
					// Load the object pointer first
					emitPtrMovFromFrame(X64Register::RAX, array_base_offset);
					
					// Add member offset + index offset: ADD RAX, (member_offset + index * element_size)
					int64_t total_offset = member_offset + (index_value * element_size_bytes);
					
					FLASH_LOG_FORMAT(Codegen, Debug,
						"ArrayStore (const index): object_pointer path, base_offset={}, member_offset={}, index={}, elem_size={}, total_offset={}",
						array_base_offset, member_offset, index_value, element_size_bytes, total_offset);
					
					emitAddImmToReg(textSectionData, X64Register::RAX, total_offset);
					
					// Store to [RAX] with appropriate size
					if (is_float_store) {
						// MOVSS/MOVSD [RAX], XMM0
						bool is_double = (element_size_bits == 64);
						if (is_double) {
							textSectionData.push_back(0xF2);  // MOVSD prefix
						} else {
							textSectionData.push_back(0xF3);  // MOVSS prefix
						}
						textSectionData.push_back(0x0F);
						textSectionData.push_back(0x11);  // Store opcode
						textSectionData.push_back(0x00);  // ModR/M: [RAX]
					} else {
						emitStoreToMemory(textSectionData, X64Register::RDX, X64Register::RAX, 0, element_size_bytes);
					}
				} else {
					// Regular array - direct stack access
					int64_t element_offset = array_base_offset + member_offset + (index_value * element_size_bytes);
					
					// Store RDX to [RBP + offset] with appropriate size
					emitStoreToFrame(textSectionData, X64Register::RDX, element_offset, element_size_bytes);
				}
			} else if (std::holds_alternative<TempVar>(op.index.value)) {
				// Variable index - compute address at runtime
				TempVar index_var = std::get<TempVar>(op.index.value);
				int64_t index_var_offset = getStackOffsetFromTempVar(index_var, op.index.size_in_bits);
				
				// Load index into RCX (value is already in RDX)
				emitLoadIndexIntoRCX(textSectionData, index_var_offset, op.index.size_in_bits);
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);
				
				if (is_pointer_to_array) {
					// Load pointer into RAX
					emitPtrMovFromFrame(X64Register::RAX, array_base_offset);
					// RAX += RCX (add index offset to pointer)
					emitAddRAXRCX(textSectionData);
				} else if (is_object_pointer) {
					// Member array of a pointer object (like this.values[i])
					// Load the object pointer first
					emitPtrMovFromFrame(X64Register::RAX, array_base_offset);
					// Add member offset: ADD RAX, member_offset
					if (member_offset != 0) {
						FLASH_LOG_FORMAT(Codegen, Debug,
							"ArrayStore (var index): object_pointer path, base_offset={}, member_offset={}, elem_size={}",
							array_base_offset, member_offset, element_size_bytes);
						emitAddImmToReg(textSectionData, X64Register::RAX, member_offset);
					}
					// RAX += RCX (add index offset)
					emitAddRAXRCX(textSectionData);
				} else {
					// LEA RAX, [RBP + array_base_offset]
					int64_t combined_offset = array_base_offset + member_offset;
					emitLEAFromFrame(textSectionData, X64Register::RAX, combined_offset);
					// RAX += RCX
					emitAddRAXRCX(textSectionData);
				}
				
				// Store to [RAX]
				if (is_float_store) {
					// MOVSS/MOVSD [RAX], XMM0
					bool is_double = (element_size_bits == 64);
					if (is_double) {
						textSectionData.push_back(0xF2);  // MOVSD prefix
					} else {
						textSectionData.push_back(0xF3);  // MOVSS prefix
					}
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0x11);  // Store opcode
					textSectionData.push_back(0x00);  // ModR/M: [RAX]
				} else {
					emitStoreToMemory(textSectionData, X64Register::RDX, X64Register::RAX, 0, element_size_bytes);
				}
			} else if (std::holds_alternative<StringHandle>(op.index.value)) {
				// Index is a named variable - get its stack offset
				StringHandle index_handle = std::get<StringHandle>(op.index.value);
				auto it = variable_scopes.back().variables.find(index_handle);
				if (it == variable_scopes.back().variables.end()) {
					throw InternalError("Index variable not found in scope");
					return;
				}
				int64_t index_var_offset = it->second.offset;
				int index_size_in_bits = it->second.size_in_bits;
				
				// Load index into RCX (value is already in RDX)
				emitLoadIndexIntoRCX(textSectionData, index_var_offset, index_size_in_bits);
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);
				
				if (is_pointer_to_array) {
					// Load pointer into RAX
					emitPtrMovFromFrame(X64Register::RAX, array_base_offset);
					// RAX += RCX (add index offset to pointer)
					emitAddRAXRCX(textSectionData);
				} else if (is_object_pointer) {
					// Member array of a pointer object (like this.values[i])
					// Load the object pointer first
					emitPtrMovFromFrame(X64Register::RAX, array_base_offset);
					// Add member offset: ADD RAX, member_offset
					if (member_offset != 0) {
						emitAddImmToReg(textSectionData, X64Register::RAX, member_offset);
					}
					// RAX += RCX (add index offset)
					emitAddRAXRCX(textSectionData);
				} else {
					// LEA RAX, [RBP + array_base_offset]
					int64_t combined_offset = array_base_offset + member_offset;
					emitLEAFromFrame(textSectionData, X64Register::RAX, combined_offset);
					// RAX += RCX
					emitAddRAXRCX(textSectionData);
				}
				
				// Store to [RAX]
				if (is_float_store) {
					// MOVSS/MOVSD [RAX], XMM0
					bool is_double = (element_size_bits == 64);
					if (is_double) {
						textSectionData.push_back(0xF2);  // MOVSD prefix
					} else {
						textSectionData.push_back(0xF3);  // MOVSS prefix
					}
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0x11);  // Store opcode
					textSectionData.push_back(0x00);  // ModR/M: [RAX]
				} else {
					emitStoreToMemory(textSectionData, X64Register::RDX, X64Register::RAX, 0, element_size_bytes);
				}
			} else {
				throw InternalError("ArrayStore index must be constant, TempVar, or StringHandle");
			}
			return;
		}
		
		// All array store now uses typed payload - no legacy code path
		throw InternalError("ArrayStore without typed payload - should not happen");
	}


	void handleStringLiteral(const IrInstruction& instruction) {
		const StringLiteralOp& op = instruction.getTypedPayload<StringLiteralOp>();
		TempVar result_var = std::get<TempVar>(op.result);

		// Add string literal to .rdata and get symbol
		std::string_view symbol_name = writer.add_string_literal(op.content);
		int64_t stack_offset = getStackOffsetFromTempVar(result_var);
		variable_scopes.back().variables[StringTable::getOrInternStringHandle(result_var.name())].offset = stack_offset;

		// LEA RAX, [RIP + symbol] with relocation
		uint32_t reloc_offset = emitLeaRipRelative(X64Register::RAX);
		writer.add_relocation(reloc_offset, symbol_name);

		// Store address to stack (64-bit pointer)
		emitMovToFrame(X64Register::RAX, stack_offset, 64);
	}

