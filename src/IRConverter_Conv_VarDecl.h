	void handleGlobalVariableDecl(const IrInstruction& instruction) {
		// Extract typed payload
		const GlobalVariableDeclOp& op = std::any_cast<const GlobalVariableDeclOp&>(instruction.getTypedPayload());

		// Store global variable info for later use
		GlobalVariableInfo global_info;
		global_info.name = op.var_name;
		global_info.type = op.type;
		global_info.is_initialized = op.is_initialized;
		global_info.size_in_bytes = (op.size_in_bits / 8) * op.element_count;
		global_info.reloc_target = op.reloc_target;
		
		// Copy raw init data if present
		if (op.is_initialized) {
			global_info.init_data = op.init_data;
		}

		global_variables_.push_back(global_info);
	}

	void handleGlobalLoad(const IrInstruction& instruction) {
		// BUGFIX: GlobalLoad requires a function context for stack allocation.
		// If we're outside a function (e.g., in global initializer context), skip this instruction.
		// This can happen when the IR generator emits GlobalLoad for built-in function references
		// that appear in global variable initializers.
		if (variable_scopes.empty()) {
			// Extract the global name to provide a better diagnostic
			const GlobalLoadOp& op = std::any_cast<const GlobalLoadOp&>(instruction.getTypedPayload());
			std::string_view global_name = StringTable::getStringView(op.getGlobalName());
			
			// Silently skip builtin functions - they are expected to be evaluated at compile time
			// but can end up here when templates are instantiated with dependent arguments
			if (global_name.starts_with("__builtin")) {
				FLASH_LOG(Codegen, Debug, "Skipping GlobalLoad for builtin '", global_name, "' outside function context");
				return;
			}
			
			FLASH_LOG(Codegen, Warning, "GlobalLoad instruction for '", global_name, "' found outside function context - skipping");
			return;
		}
		
		// Extract typed payload - all GlobalLoad instructions use typed payloads
		const GlobalLoadOp& op = std::any_cast<const GlobalLoadOp&>(instruction.getTypedPayload());
		
		TempVar result_temp = std::get<TempVar>(op.result.value);
		StringHandle global_name_handle = op.getGlobalName();
		int size_in_bits = op.result.size_in_bits;
		Type result_type = op.result.type;
		bool is_floating_point = (result_type == Type::Float || result_type == Type::Double);
		bool is_float = (result_type == Type::Float);

		// BUGFIX: Before using RAX or XMM0, flush them if they hold dirty data
		// This prevents overwriting intermediate results in chained operations
		X64Register target_reg = is_floating_point ? X64Register::XMM0 : X64Register::RAX;
		auto& reg_info = regAlloc.registers[static_cast<size_t>(target_reg)];
		if (reg_info.isDirty && reg_info.stackVariableOffset != INT_MIN) {
			// Flush the register to memory before overwriting it
			auto tempVarIndex = getTempVarFromOffset(reg_info.stackVariableOffset);
			if (tempVarIndex.has_value()) {
				int32_t stackVariableOffset = reg_info.stackVariableOffset;
				int flush_size_in_bits = reg_info.size_in_bits;
				
				// Extend scope_stack_space if needed
				if (stackVariableOffset < variable_scopes.back().scope_stack_space) {
					variable_scopes.back().scope_stack_space = stackVariableOffset;
				}
				
				// Store the register value to stack
				emitMovToFrameSized(
					SizedRegister{target_reg, 64, false},
					SizedStackSlot{stackVariableOffset, flush_size_in_bits, false}
				);
			}
			reg_info.isDirty = false;
			// Clear the register allocation so it won't be reused without reloading
			reg_info.stackVariableOffset = INT_MIN;
		}

		// Load the global value/address using RIP-relative addressing
		uint32_t reloc_offset;
		if (op.is_array) {
			// For arrays: use LEA to get the address of the global
			reloc_offset = emitLeaRipRelative(X64Register::RAX);
		} else if (is_floating_point) {
			// For floating-point scalars: use MOVSD/MOVSS to load into XMM0
			reloc_offset = emitFloatMovRipRelative(X64Register::XMM0, is_float);
		} else {
			// For integer scalars: load the value using MOV
			reloc_offset = emitMovRipRelative(X64Register::RAX, size_in_bits);
		}

		// Add a pending relocation for this global variable reference
		pending_global_relocations_.push_back({reloc_offset, global_name_handle, IMAGE_REL_AMD64_REL32});

		// Store the loaded value/address to the stack
		int result_offset = allocateStackSlotForTempVar(result_temp.var_number);
		
		if (is_floating_point && !op.is_array) {
			// For floating-point: use emitFloatMovToFrame
			emitFloatMovToFrame(X64Register::XMM0, result_offset, is_float);
		} else {
			// For integers/pointers: use emitMovToFrameBySize
			int store_size = op.is_array ? 64 : size_in_bits;
			emitMovToFrameBySize(X64Register::RAX, result_offset, store_size);
		}
	}

	void handleGlobalStore(const IrInstruction& instruction) {
		// Format: [global_name, source_temp]
		assert(instruction.getOperandCount() == 2 && "GlobalStore must have exactly 2 operands");

		StringHandle global_name = instruction.getOperandAs<StringHandle>(0);
		TempVar source_temp = instruction.getOperandAs<TempVar>(1);

		// Determine the size and type of the global variable
		// We need to look it up in the global variables vector
		const GlobalVariableInfo* global_info = nullptr;
		for (const auto& global : global_variables_) {
			if (global.name == global_name) {
				global_info = &global;
				break;
			}
		}
		
		if (!global_info) {
			FLASH_LOG(Codegen, Error, "Global variable not found: ", global_name);
			throw InternalError("Global variable not found during GlobalStore");
			return;
		}

		int size_in_bits = static_cast<int>(global_info->size_in_bytes * 8);
		Type var_type = global_info->type;
		bool is_floating_point = (var_type == Type::Float || var_type == Type::Double);
		bool is_float = (var_type == Type::Float);

		// Load the source value from stack into a register
		int source_offset = getStackOffsetFromTempVar(source_temp);
		
		if (is_floating_point) {
			// Load floating-point value into XMM0
			emitFloatMovFromFrame(X64Register::XMM0, source_offset, is_float);
			// Store to global using RIP-relative addressing
			uint32_t reloc_offset = emitFloatMovRipRelativeStore(X64Register::XMM0, is_float);
			pending_global_relocations_.push_back({reloc_offset, global_name, IMAGE_REL_AMD64_REL32});
		} else {
			// Load integer value into RAX
			emitMovFromFrameBySize(X64Register::RAX, source_offset, size_in_bits);
			// Store to global using RIP-relative addressing
			uint32_t reloc_offset = emitMovRipRelativeStore(X64Register::RAX, size_in_bits);
			pending_global_relocations_.push_back({reloc_offset, global_name, IMAGE_REL_AMD64_REL32});
		}
	}

	void handleVariableDecl(const IrInstruction& instruction) {
		// Extract typed payload
		const VariableDeclOp& op = std::any_cast<const VariableDeclOp&>(instruction.getTypedPayload());

		// Get variable name as StringHandle
		StringHandle var_name_handle = op.var_name;
		std::string var_name_str = std::string(StringTable::getStringView(var_name_handle));

		Type var_type = op.type;
		StackVariableScope& current_scope = variable_scopes.back();
		auto var_it = current_scope.variables.find(var_name_handle);
		assert(var_it != current_scope.variables.end());

		bool is_reference = op.is_reference;
		bool is_rvalue_reference = op.is_rvalue_reference;
		[[maybe_unused]] bool is_array = op.is_array;
		bool is_initialized = op.initializer.has_value();

		FLASH_LOG(Codegen, Debug, "handleVariableDecl: var='", var_name_str, "', is_reference=", is_reference, ", offset=", var_it->second.offset, ", is_initialized=", is_initialized, ", type=", static_cast<int>(var_type));

		// Store mapping from variable name to offset for reference lookups
		variable_name_to_offset_[var_name_str] = var_it->second.offset;

		// REMOVED: Flawed TempVar linking heuristic
		// Track the most recently allocated named variable for TempVar linking
		//last_allocated_variable_name_ = var_name_str;
		//last_allocated_variable_offset_ = var_it->second.offset;

		if (is_reference) {
			// For references, we need to determine the size of the VALUE being referenced,
			// not the size of the reference itself (which is always 64 bits for a pointer)
			int value_size_bits = op.size_in_bits;
			
			// If size_in_bits is 64 and the type is not a 64-bit type, we need to calculate the actual size
			// This happens for structured bindings where size_in_bits is set to 64 (pointer size)
			if (op.size_in_bits == 64) {
				// Try to get the actual size from the type
				int calculated_size = get_type_size_bits(var_type);
				if (calculated_size > 0 && calculated_size != 64) {
					value_size_bits = calculated_size;
					FLASH_LOG(Codegen, Debug, "Reference variable: Calculated value_size_bits=", value_size_bits, " from type=", static_cast<int>(var_type));
				}
			}
			
			setReferenceInfo(var_it->second.offset, var_type, value_size_bits, is_rvalue_reference);
			int32_t dst_offset = var_it->second.offset;
			X64Register pointer_reg = allocateRegisterWithSpilling();
			bool pointer_initialized = false;
			if (is_initialized) {
				// For reference initialization from typed payload
				// We need to handle TempVar or string_view in the initializer value
				const TypedValue& init = op.initializer.value();
				if (std::holds_alternative<TempVar>(init.value)) {
					auto temp_var = std::get<TempVar>(init.value);
					int src_offset = getStackOffsetFromTempVar(temp_var);
					FLASH_LOG(Codegen, Debug, "Reference init from TempVar: src_offset=", src_offset, 
					          " init.type=", static_cast<int>(init.type), 
					          " init.size_in_bits=", init.size_in_bits);
					// Check if source is itself a pointer/reference - if so, load the value
					// Otherwise, take the address
					auto src_ref_it = reference_stack_info_.find(src_offset);
					if (src_ref_it != reference_stack_info_.end()) {
						// Source is a reference - copy the pointer value
						FLASH_LOG(Codegen, Debug, "Source is in reference_stack_info, using MOV");
						emitMovFromFrame(pointer_reg, src_offset);
					} else {
						// Check if it's a 64-bit value (likely a pointer)
						// For __range_begin_ and similar, which are int64 pointers
						// Also check for struct types that returned as pointers (reference returns)
						// And function pointers which are always 64-bit addresses
						bool is_likely_pointer = (init.size_in_bits == 64 && 
						                          (init.type == Type::Long || init.type == Type::Int || 
						                           init.type == Type::UnsignedLong || init.type == Type::LongLong ||
						                           init.type == Type::Struct || init.type == Type::FunctionPointer));  // Struct references and function references return 64-bit pointers
						FLASH_LOG(Codegen, Debug, "is_likely_pointer=", is_likely_pointer);
						if (is_likely_pointer) {
							// Load the pointer value
							emitMovFromFrame(pointer_reg, src_offset);
						} else {
							// Load address of the source variable
							emitLeaFromFrame(pointer_reg, src_offset);
						}
					}
					pointer_initialized = true;
				} else if (std::holds_alternative<StringHandle>(init.value)) {
					StringHandle rvalue_var_name_handle = std::get<StringHandle>(init.value);
					
					auto src_it = current_scope.variables.find(rvalue_var_name_handle);
					if (src_it != current_scope.variables.end()) {
						FLASH_LOG(Codegen, Debug, "Initializing reference from: '", StringTable::getStringView(rvalue_var_name_handle), "', type=", static_cast<int>(init.type), ", size=", init.size_in_bits);
						// Check if source is a reference
						auto src_ref_it = reference_stack_info_.find(src_it->second.offset);
						if (src_ref_it != reference_stack_info_.end()) {
							// Source is a reference - copy the pointer value
							FLASH_LOG(Codegen, Debug, "Using MOV (source is reference)");
							emitMovFromFrame(pointer_reg, src_it->second.offset);
						} else {
							// Named variable: take its address via LEA.
							// This is correct for all types including pointer variables
							// (int*& pr = p; needs the address OF p, not p's value).
							FLASH_LOG(Codegen, Debug, "Using LEA (named variable)");
							emitLeaFromFrame(pointer_reg, src_it->second.offset);
						}
						pointer_initialized = true;
					}
				} else if (std::holds_alternative<unsigned long long>(init.value) ||
				           std::holds_alternative<double>(init.value)) {
					// Literal initializer for reference: materialize a temporary.
					// C++ allows binding rvalue references and const lvalue references
					// to literals (e.g., int&& rr = 42; const int& cr = 42;) by
					// extending the lifetime of a temporary.
					int lit_size = op.size_in_bits;
					if (lit_size == 64) {
						// For references, size_in_bits is 64 (pointer size);
						// use the actual value size from get_type_size_bits
						int actual = get_type_size_bits(var_type);
						if (actual > 0 && actual != 64) lit_size = actual;
					}
					int lit_bytes = (lit_size + 7) / 8;
					lit_bytes = (lit_bytes + 7) & ~7;  // 8-byte aligned

					// Allocate hidden stack space for the temporary
					next_temp_var_offset_ += lit_bytes;
					int32_t temp_offset = -(static_cast<int32_t>(current_function_named_vars_size_) + next_temp_var_offset_);
					if (temp_offset < variable_scopes.back().scope_stack_space)
						variable_scopes.back().scope_stack_space = temp_offset;


					// Store the literal value into the temporary
					X64Register lit_reg = allocateRegisterWithSpilling();
					if (std::holds_alternative<double>(init.value)) {
						double value = std::get<double>(init.value);
						uint64_t bits;
						if (var_type == Type::Float) {
							float fv = static_cast<float>(value);
							uint32_t fb; std::memcpy(&fb, &fv, sizeof(fb));
							emitMovDwordPtrImmToRegOffset(X64Register::RBP, temp_offset, fb);
						} else {
							std::memcpy(&bits, &value, sizeof(bits));
							emitMovImm64(lit_reg, bits);
							emitMovToFrameSized(
								SizedRegister{lit_reg, 64, false},
								SizedStackSlot{temp_offset, lit_size, false}
							);
						}
					} else {
						uint64_t value = std::get<unsigned long long>(init.value);
						emitMovImm64(lit_reg, value);
						emitMovToFrameSized(
							SizedRegister{lit_reg, 64, false},
							SizedStackSlot{temp_offset, lit_size, isSignedType(var_type)}
						);
					}
					regAlloc.release(lit_reg);

					// Take address of the temporary
					FLASH_LOG(Codegen, Debug, "Materializing temporary for reference literal at offset=", temp_offset);
					emitLeaFromFrame(pointer_reg, temp_offset);
					pointer_initialized = true;
				}
				if (!pointer_initialized) {
					FLASH_LOG(Codegen, Error, "Reference initializer is not an addressable lvalue");
					throw InternalError("Reference initializer must be an lvalue");
				}
			} else {
				moveImmediateToRegister(pointer_reg, 0);
			}
			auto store_ptr = generatePtrMovToFrame(pointer_reg, dst_offset);
			textSectionData.insert(textSectionData.end(), store_ptr.op_codes.begin(),
				               store_ptr.op_codes.begin() + store_ptr.size_in_bytes);
			regAlloc.release(pointer_reg);
			return;
		}

		X64Register allocated_reg_val = X64Register::RAX; // Default

		if (is_initialized) {
			auto dst_offset = var_it->second.offset;
			const TypedValue& init = op.initializer.value();

			// Check if the initializer is a literal value
			bool is_literal = std::holds_alternative<unsigned long long>(init.value) ||
			                  std::holds_alternative<double>(init.value);

			if (is_literal) {
				if (std::holds_alternative<double>(init.value)) {
					// Handle double/float literals
					double value = std::get<double>(init.value);

					FLASH_LOG(Codegen, Debug, "Initializing ", (var_type == Type::Float ? "float" : "double"), " literal: ", value);

					// Convert double to bit pattern
					uint64_t bits;
					
					// If the variable type is Float (32-bit), convert the double to float first
					if (var_type == Type::Float) {
						float float_value = static_cast<float>(value);
						uint32_t float_bits;
						std::memcpy(&float_bits, &float_value, sizeof(float_bits));
						
						FLASH_LOG(Codegen, Debug, "Storing float immediate to [RBP+", dst_offset, "], bits=0x", std::hex, float_bits, std::dec);
						
						// For 32-bit floats, store immediate directly to memory
						// This is more efficient and avoids register allocation
						emitMovDwordPtrImmToRegOffset(X64Register::RBP, dst_offset, float_bits);
					} else {
						// For 64-bit doubles, load into GPR then store to memory
						std::memcpy(&bits, &value, sizeof(bits));
						
						FLASH_LOG(Codegen, Debug, "Storing double via GPR to [RBP+", dst_offset, "], bits=0x", std::hex, bits, std::dec);
						
						// Allocate a GPR temporarily
						allocated_reg_val = allocateRegisterWithSpilling();
						
						// MOV reg, imm64 (load bit pattern) - use emit function
						emitMovImm64(allocated_reg_val, bits);
						
						// Store the 64-bit value to stack
						emitMovToFrameSized(
							SizedRegister{allocated_reg_val, 64, false},
							SizedStackSlot{dst_offset, 64, false}
						);
						
						// Release the register
						regAlloc.release(allocated_reg_val);
					}
				} else if (std::holds_alternative<unsigned long long>(init.value)) {
					uint64_t value = std::get<unsigned long long>(init.value);

					// For integer literals, allocate a register temporarily
					allocated_reg_val = allocateRegisterWithSpilling();
					
					// MOV reg, imm64 - use emit function
					emitMovImm64(allocated_reg_val, value);

					// Store the value from register to stack (size-aware)
					emitMovToFrameSized(
						SizedRegister{allocated_reg_val, 64, false},  // source: 64-bit register
						SizedStackSlot{dst_offset, op.size_in_bits, isSignedType(op.type)}  // dest
					);

					// Release the register since the value is now in the stack
					regAlloc.release(allocated_reg_val);
				}
			} else {
				// Load from memory (TempVar or variable)
				// For non-literal initialization, we don't allocate a register
				// We just copy the value from source to destination on the stack
				int src_offset = 0;
				bool src_is_pointer = false;  // Track if source is a pointer to the actual data
				if (std::holds_alternative<TempVar>(init.value)) {
					auto temp_var = std::get<TempVar>(init.value);
					src_offset = getStackOffsetFromTempVar(temp_var);
					// Check if this temp_var is a reference/pointer to the actual struct
					// For RVO struct returns, temp_var holds the address of the constructed struct
					auto ref_it = reference_stack_info_.find(src_offset);
					if (ref_it != reference_stack_info_.end()) {
						// This is a reference - need to dereference it
						src_is_pointer = true;
					}
				} else if (std::holds_alternative<StringHandle>(init.value)) {
					StringHandle rvalue_var_name_handle = std::get<StringHandle>(init.value);
					auto src_it = current_scope.variables.find(rvalue_var_name_handle);
					if (src_it == current_scope.variables.end()) {
						FLASH_LOG(Codegen, Error, "Variable '", StringTable::getStringView(rvalue_var_name_handle), "' not found in symbol table");
						FLASH_LOG(Codegen, Error, "Available variables in current scope:");
						for (const auto& [name, var_info] : current_scope.variables) {
							// Phase 5: Convert StringHandle to string_view for logging
							FLASH_LOG(Codegen, Error, "  - ", StringTable::getStringView(name), " at var_info.offset ");
						}
					}
					assert(src_it != current_scope.variables.end());
					src_offset = src_it->second.offset;
					
					// Check if source is an array - for array-to-pointer decay, we need LEA
					if (src_it->second.is_array) {
						// Source is an array being assigned to a pointer - use LEA to get address
						X64Register addr_reg = allocateRegisterWithSpilling();
						emitLeaFromFrame(addr_reg, src_offset);
						emitMovToFrameSized(
							SizedRegister{addr_reg, 64, false},
							SizedStackSlot{dst_offset, 64, false}
						);
						regAlloc.release(addr_reg);
						return;  // Early return - we've handled this case
					}
				}

				if (auto src_reg = regAlloc.tryGetStackVariableRegister(src_offset); src_reg.has_value()) {
					// Source value is already in a register (e.g., from function return or arithmetic)
					// Store it directly to the destination stack location
					if (is_floating_point_type(var_type)) {
						// For floating-point types, the value is in an XMM register
						// Use float mov instructions instead of integer mov
						bool is_float = (var_type == Type::Float);
						emitFloatMovToFrame(src_reg.value(), dst_offset, is_float);
					} else {
						// For integer types, use regular mov
						// Use the actual size from the variable type, not hardcoded 64 bits
						emitMovToFrameSized(
							SizedRegister{src_reg.value(), static_cast<uint8_t>(op.size_in_bits), false},
							SizedStackSlot{dst_offset, op.size_in_bits, isSignedType(op.type)}
						);
					}
				} else {
					// Source is on the stack, load it to a temporary register and store to destination
					if (var_type == Type::Struct) {
						// For struct types, copy entire struct using 8-byte chunks
						int struct_size_bytes = (op.size_in_bits + 7) / 8;
						
						FLASH_LOG(Codegen, Info, "==================== STRUCT COPY IN HANDLEVARIABLE ====================");
						FLASH_LOG(Codegen, Info, "size_bytes=", struct_size_bytes, ", src_offset=", src_offset, ", dst_offset=", dst_offset, ", src_is_pointer=", src_is_pointer);
						
						// Determine actual source address
						int32_t actual_src_offset;
						if (src_is_pointer) {
							// Source is a pointer to the struct - dereference it
							// Load the pointer value into a register
							X64Register ptr_reg = allocateRegisterWithSpilling();
							emitMovFromFrame(ptr_reg, src_offset);
							FLASH_LOG(Codegen, Debug, "Struct copy (via pointer): size_in_bits=", op.size_in_bits, ", size_bytes=", struct_size_bytes, ", ptr_at_offset=", src_offset, ", dst_offset=", dst_offset);
							
							// Now copy from the address in ptr_reg to dst_offset
							// We need to use memory-to-memory copy via a temporary register
							for (int offset = 0; offset < struct_size_bytes; ) {
								if (offset + 8 <= struct_size_bytes) {
									// Copy 8 bytes: load from [ptr_reg + offset], store to [rbp + dst_offset + offset]
									X64Register temp_reg = allocateRegisterWithSpilling();
									// MOV temp_reg, [ptr_reg + offset]
									emitMovFromMemory(temp_reg, ptr_reg, offset, 8);
									// MOV [rbp + dst_offset + offset], temp_reg
									emitMovToFrameSized(
										SizedRegister{temp_reg, 64, false},
										SizedStackSlot{dst_offset + offset, 64, false}
									);
									regAlloc.release(temp_reg);
									offset += 8;
								} else if (offset + 4 <= struct_size_bytes) {
									X64Register temp_reg = allocateRegisterWithSpilling();
									emitMovFromMemory(temp_reg, ptr_reg, offset, 4);
									emitMovToFrameSized(
										SizedRegister{temp_reg, 64, false},
										SizedStackSlot{dst_offset + offset, 32, false}
									);
									regAlloc.release(temp_reg);
									offset += 4;
								} else if (offset + 2 <= struct_size_bytes) {
									X64Register temp_reg = allocateRegisterWithSpilling();
									emitMovFromMemory(temp_reg, ptr_reg, offset, 2);
									emitMovToFrameSized(
										SizedRegister{temp_reg, 64, false},
										SizedStackSlot{dst_offset + offset, 16, false}
									);
									regAlloc.release(temp_reg);
									offset += 2;
								} else if (offset + 1 <= struct_size_bytes) {
									X64Register temp_reg = allocateRegisterWithSpilling();
									emitMovFromMemory(temp_reg, ptr_reg, offset, 1);
									emitMovToFrameSized(
										SizedRegister{temp_reg, 64, false},
										SizedStackSlot{dst_offset + offset, 8, false}
									);
									regAlloc.release(temp_reg);
									offset += 1;
								}
							}
							regAlloc.release(ptr_reg);
						} else {
							// Source is the struct itself on the stack
							actual_src_offset = src_offset;
							FLASH_LOG(Codegen, Debug, "Struct copy (direct): size_in_bits=", op.size_in_bits, ", size_bytes=", struct_size_bytes, ", src_offset=", src_offset, ", dst_offset=", dst_offset);
							
							// Copy struct using 8-byte chunks, then handle remaining bytes
							// Use an allocated register to avoid clobbering dirty registers
							X64Register copy_reg = allocateRegisterWithSpilling();
							int offset = 0;
							while (offset + 8 <= struct_size_bytes) {
								// Load 8 bytes from source
								emitMovFromFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{actual_src_offset + offset, 64, false}
								);
								// Store 8 bytes to destination
								emitMovToFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{dst_offset + offset, 64, false}
								);
								offset += 8;
							}
							
							// Handle remaining bytes (4, 2, 1)
							if (offset + 4 <= struct_size_bytes) {
								emitMovFromFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{actual_src_offset + offset, 32, false}
								);
								emitMovToFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{dst_offset + offset, 32, false}
								);
								offset += 4;
							}
							if (offset + 2 <= struct_size_bytes) {
								emitMovFromFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{actual_src_offset + offset, 16, false}
								);
								emitMovToFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{dst_offset + offset, 16, false}
								);
								offset += 2;
							}
							if (offset + 1 <= struct_size_bytes) {
								emitMovFromFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{actual_src_offset + offset, 8, false}
								);
								emitMovToFrameSized(
									SizedRegister{copy_reg, 64, false},
									SizedStackSlot{dst_offset + offset, 8, false}
								);
							}
							regAlloc.release(copy_reg);
						}
					} else if (is_floating_point_type(var_type)) {
						// For floating-point types, use XMM register and float moves
						allocated_reg_val = allocateXMMRegisterWithSpilling();
						bool is_float = (var_type == Type::Float);
						emitFloatMovFromFrame(allocated_reg_val, src_offset, is_float);
						emitFloatMovToFrame(allocated_reg_val, dst_offset, is_float);
						regAlloc.release(allocated_reg_val);
					} else {
						// For integer types, use GPR and integer moves
						allocated_reg_val = allocateRegisterWithSpilling();
						emitMovFromFrameBySize(allocated_reg_val, src_offset, op.size_in_bits);
						emitMovToFrameSized(
							SizedRegister{allocated_reg_val, 64, false},
							SizedStackSlot{dst_offset, op.size_in_bits, isSignedType(op.type)}
						);
						regAlloc.release(allocated_reg_val);
					}
				}
			} // end else (not literal)
		} // end if (is_initialized)
		
		// Add debug information for the local variable
		if (current_function_name_.isValid()) {
			uint32_t type_index;
			switch (var_type) {
				case Type::Int: type_index = 0x74; break;
				case Type::Float: type_index = 0x40; break;
				case Type::Double: type_index = 0x41; break;
				case Type::Char: type_index = 0x10; break;
				case Type::Bool: type_index = 0x30; break;
				default: type_index = 0x74; break;
			}

			std::vector<CodeView::VariableLocation> locations;
			uint32_t start_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;

			if (is_initialized) {
				CodeView::VariableLocation loc;
				loc.type = CodeView::VariableLocation::REGISTER;
				loc.offset = var_it->second.offset;  // Provide stack offset as fallback for DWARF
				loc.start_offset = start_offset;
				loc.length = 100; // Placeholder until lifetime analysis is implemented
				loc.register_code = getX64RegisterCodeViewCode(allocated_reg_val);
				locations.push_back(loc);
			} else {
				CodeView::VariableLocation loc;
				loc.type = CodeView::VariableLocation::STACK_RELATIVE;
				loc.offset = var_it->second.offset;
				loc.start_offset = start_offset;
				loc.length = 100; // Placeholder
				loc.register_code = 0;
				locations.push_back(loc);
			}

			uint16_t flags = 0;
			writer.add_local_variable(var_name_str, type_index, flags, locations);
		}
	}

	uint16_t getX64RegisterCodeViewCode(X64Register reg) {
		switch (reg) {
			case X64Register::RAX: return 0;
			case X64Register::RCX: return 1;
			case X64Register::RDX: return 2;
			case X64Register::RBX: return 3;
			case X64Register::RSP: return 4;
			case X64Register::RBP: return 5;
			case X64Register::RSI: return 6;
			case X64Register::RDI: return 7;
			case X64Register::R8: return 8;
			case X64Register::R9: return 9;
			case X64Register::R10: return 10;
			case X64Register::R11: return 11;
			case X64Register::R12: return 12;
			case X64Register::R13: return 13;
			case X64Register::R14: return 14;
			case X64Register::R15: return 15;
			// XMM registers (SSE/AVX)
			case X64Register::XMM0: return 154;  // CV_AMD64_XMM0
			case X64Register::XMM1: return 155;  // CV_AMD64_XMM1
			case X64Register::XMM2: return 156;  // CV_AMD64_XMM2
			case X64Register::XMM3: return 157;  // CV_AMD64_XMM3
			case X64Register::XMM4: return 158;  // CV_AMD64_XMM4
			case X64Register::XMM5: return 159;  // CV_AMD64_XMM5
			case X64Register::XMM6: return 160;  // CV_AMD64_XMM6
			case X64Register::XMM7: return 161;  // CV_AMD64_XMM7
			case X64Register::XMM8: return 162;  // CV_AMD64_XMM8
			case X64Register::XMM9: return 163;  // CV_AMD64_XMM9
			case X64Register::XMM10: return 164; // CV_AMD64_XMM10
			case X64Register::XMM11: return 165; // CV_AMD64_XMM11
			case X64Register::XMM12: return 166; // CV_AMD64_XMM12
			case X64Register::XMM13: return 167; // CV_AMD64_XMM13
			case X64Register::XMM14: return 168; // CV_AMD64_XMM14
			case X64Register::XMM15: return 169; // CV_AMD64_XMM15
			default: throw InternalError("Unsupported X64Register");
		}
	}

	// Reset per-function state between function declarations
	void resetFunctionState() {
		max_temp_var_index_ = 0;
		next_temp_var_offset_ = 8;
		current_function_try_blocks_.clear();
		current_try_block_ = nullptr;
		try_block_nesting_stack_.clear();
		pending_catch_try_index_ = SIZE_MAX;
		current_catch_handler_ = nullptr;
		current_function_local_objects_.clear();
		current_function_unwind_map_.clear();
		current_exception_state_ = -1;
		current_function_seh_try_blocks_.clear();
		seh_try_block_stack_.clear();
		current_seh_filter_funclet_offset_ = 0;
		in_catch_funclet_ = false;
		catch_funclet_return_slot_offset_ = 0;
		catch_funclet_return_flag_slot_offset_ = 0;
		catch_funclet_return_label_counter_ = 0;
		catch_funclet_terminated_by_return_ = false;
		current_catch_continuation_label_ = StringHandle();
		catch_return_bridges_.clear();
		catch_continuation_fixup_map_.clear();
		catch_continuation_sub_rsp_patches_.clear();
		eh_prologue_lea_rbp_offset_ = 0;
		catch_funclet_lea_rbp_patches_.clear();
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			current_function_cfi_.clear();
		}
	}

	void handleFunctionDecl(const IrInstruction& instruction) {
		assert(instruction.hasTypedPayload() && "FunctionDecl instruction must use typed payload");
		
		// Reset register allocator state for the new function
		// This ensures registers from previous functions don't interfere
		regAlloc.reset();
		
		// Use typed payload path
		const auto& func_decl = instruction.getTypedPayload<FunctionDeclOp>();
		
		// Use mangled name if available (for member functions like lambda operator()),
		// otherwise use function_name. This is important for nested lambdas where multiple
		// operator() functions would otherwise have the same name.
		// Phase 4: Use helpers
		StringHandle mangled_handle = func_decl.getMangledName();
		StringHandle func_name_handle = func_decl.getFunctionName();
		StringHandle struct_name_handle = func_decl.getStructName();
		std::string_view mangled = StringTable::getStringView(mangled_handle);
		std::string_view func_name = mangled_handle.handle != 0 ? mangled : StringTable::getStringView(func_name_handle);
		std::string_view struct_name = StringTable::getStringView(struct_name_handle);
		
		// Construct return type
		TypeSpecifierNode return_type(func_decl.return_type, TypeQualifier::None, static_cast<unsigned char>(func_decl.return_size_in_bits));
		for (int i = 0; i < func_decl.return_pointer_depth; ++i) {
			return_type.add_pointer_level();
		}
		
		// Extract parameters
		std::vector<TypeSpecifierNode> parameter_types;
		for (const auto& param : func_decl.parameters) {
			TypeSpecifierNode param_type(param.type, TypeQualifier::None, static_cast<unsigned char>(param.size_in_bits));
			for (int i = 0; i < param.pointer_depth; ++i) {
				param_type.add_pointer_level();
			}
			parameter_types.push_back(param_type);
		}
		
		Linkage linkage = func_decl.linkage;
		bool is_variadic = func_decl.is_variadic;
		std::string_view mangled_name = StringTable::getStringView(func_decl.getMangledName());  // Phase 4: Use helper

		// Add function signature to the object file writer (still needed for debug info)
		// but use the pre-computed mangled name instead of regenerating it
		bool is_inline = func_decl.is_inline;
		if (!struct_name.empty()) {
			// Member function - include struct name
			writer.addFunctionSignature(func_name, return_type, parameter_types, struct_name, linkage, is_variadic, mangled_name, is_inline);
		} else {
			// Regular function
			writer.addFunctionSignature(func_name, return_type, parameter_types, linkage, is_variadic, mangled_name, is_inline);
		}
		
		// Finalize previous function before starting new one
		if (current_function_name_.isValid() && !skip_previous_function_finalization_) {
			auto [try_blocks, unwind_map] = convertExceptionInfoToWriterFormat();
			auto seh_try_blocks = convertSehInfoToWriterFormat();

			// Calculate actual stack space needed from scope_stack_space (which includes varargs area if present)
			// scope_stack_space is negative (offset from RBP), so negate to get positive size
			size_t total_stack = static_cast<size_t>(-variable_scopes.back().scope_stack_space);

			// Ensure stack frame also covers any catch object slot used by FH3 materialization.
			// Some catch temp offsets are reserved through EH paths and may not be reflected in
			// scope_stack_space at this point.
			for (const auto& try_block : try_blocks) {
				for (const auto& handler : try_block.catch_handlers) {
					if (handler.catch_obj_offset < 0) {
						size_t required_stack = static_cast<size_t>(-handler.catch_obj_offset);
						if (required_stack > total_stack) {
							total_stack = required_stack;
						}
					}
				}
			}
			
			// For C++ EH functions with the establisher-frame model (FrameOffset>0),
			// ensure 32 bytes of shadow/home space at the bottom of the frame.
			// The CRT's exception processing may clobber the first 32 bytes of the
			// establisher frame (shadow space for callee use), so all meaningful
			// variables must be allocated above that region.
			if (current_function_has_cpp_eh_) {
				size_t vars_used = static_cast<size_t>(-variable_scopes.back().scope_stack_space);
				if (total_stack < vars_used + 32) {
					total_stack = vars_used + 32;
				}
			}

			// Align stack so that after `push rbp; sub rsp, total_stack` the stack is 16-byte aligned
			// System V AMD64 / MS x64: after `push rbp`, RSP is misaligned by 8 bytes.
			// Subtracting a 16-byte-aligned stack size keeps RSP % 16 == 8 at call sites,
			// so align total_stack up to the next 16-byte boundary.
			if (total_stack % 16 != 0) {
				total_stack = (total_stack + 15) & ~static_cast<size_t>(15);
			}
			
			// Patch the SUB RSP immediate at prologue offset + 3 (skip REX.W, opcode, ModR/M)
			if (current_function_prologue_offset_ > 0) {
				uint32_t patch_offset = current_function_prologue_offset_ + 3;
				const auto bytes = std::bit_cast<std::array<uint8_t, 4>>(static_cast<uint32_t>(total_stack));
				for (int i = 0; i < 4; i++) {
					textSectionData[patch_offset + i] = bytes[i];
				}
			}

			// Patch catch continuation fixup SUB RSP instructions with the same stack size
			for (auto fixup_patch_offset : catch_continuation_sub_rsp_patches_) {
				const auto bytes = std::bit_cast<std::array<char, 4>>(static_cast<uint32_t>(total_stack));
				for (int i = 0; i < 4; i++) {
					textSectionData[fixup_patch_offset + i] = bytes[i];
				}
			}
			catch_continuation_sub_rsp_patches_.clear();

			// Patch C++ EH prologue LEA RBP, [RSP + total_stack]
			// The LEA instruction is: 48 8D AC 24 XX XX XX XX
			// The imm32 starts at eh_prologue_lea_rbp_offset_ + 4
			if (eh_prologue_lea_rbp_offset_ > 0) {
				uint32_t lea_patch_offset = eh_prologue_lea_rbp_offset_ + 4;
				const auto bytes = std::bit_cast<std::array<char, 4>>(static_cast<uint32_t>(total_stack));
				for (int i = 0; i < 4; i++) {
					textSectionData[lea_patch_offset + i] = bytes[i];
				}
			}

			// Patch catch funclet LEA RBP, [RDX + total_stack] instructions
			// The LEA instruction is: 48 8D AA XX XX XX XX
			// The imm32 starts at offset + 3
			for (auto funclet_lea_offset : catch_funclet_lea_rbp_patches_) {
				uint32_t lea_patch_offset = funclet_lea_offset + 3;
				const auto bytes = std::bit_cast<std::array<char, 4>>(static_cast<uint32_t>(total_stack));
				for (int i = 0; i < 4; i++) {
					textSectionData[lea_patch_offset + i] = bytes[i];
				}
			}
			catch_funclet_lea_rbp_patches_.clear();
			
			uint32_t function_length = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;

			// Update function length
			writer.update_function_length(mangled, function_length);
			writer.set_function_debug_range(mangled, 0, 0);	// doesn't seem needed

			// Add exception handling information (required for x64) - uses mangled name
			if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
				// Patch ELF catch handler selector filter values before passing to writer.
				// The filter values must match the LSDA type table ordering.
				patchElfCatchFilterValues(try_blocks);
				writer.add_function_exception_info(StringTable::getStringView(current_function_mangled_name_), current_function_offset_, function_length, try_blocks, unwind_map, current_function_cfi_);
				elf_catch_filter_patches_.clear();
			} else {
				writer.add_function_exception_info(StringTable::getStringView(current_function_mangled_name_), current_function_offset_, function_length, try_blocks, unwind_map, seh_try_blocks, static_cast<uint32_t>(total_stack));
			}

			// Clean up the previous function's variable scope
			// This happens when we start a NEW function, ensuring the previous function's scope is removed
			if (!variable_scopes.empty()) {
				variable_scopes.pop_back();
			}
			
			// Reset for new function
			resetFunctionState();
		} else if (skip_previous_function_finalization_) {
			// Previous function was skipped due to codegen error - just clean up state
			if (!variable_scopes.empty()) {
				variable_scopes.pop_back();
			}
			// Truncate textSectionData back to the start of the failed function
			textSectionData.resize(current_function_offset_);
			// Remove stale relocations from the failed function
			std::erase_if(pending_global_relocations_, [this](const PendingGlobalRelocation& r) {
				return r.offset >= current_function_offset_;
			});
			resetFunctionState();
			// Clear pending branches/labels from the skipped function
			pending_branches_.clear();
			label_positions_.clear();
			elf_catch_filter_patches_.clear();
			skip_previous_function_finalization_ = false;
		}

		// align the function to 16 bytes
		static constexpr uint8_t nop = 0x90;
		const uint32_t nop_count = 16 - (textSectionData.size() % 16);
		if (nop_count < 16)
			textSectionData.insert(textSectionData.end(), nop_count, nop);

		// Windows x64 calling convention: Functions must provide home space for parameters
		// Calculate param_count BEFORE calling calculateFunctionStackSpace so it can allocate
		// local variables/temp vars AFTER the parameter home space
		size_t param_count = parameter_types.size();
		if (!struct_name.empty() && !func_decl.is_static_member) {
			param_count++;  // Count 'this' pointer for non-static member functions
		}

		// Function debug info is now added in add_function_symbol() with length 0
		// Create std::string where needed for function calls that require it
		std::string func_name_str(func_name);
		
		// Pop the previous function's scope before creating the new one
		// The finalization code above has already used the previous scope, so it's safe to pop now
		if (!variable_scopes.empty()) {
			variable_scopes.pop_back();
		}
		
		StackVariableScope& var_scope = variable_scopes.emplace_back();
		const auto func_stack_space = calculateFunctionStackSpace(func_name_str, var_scope, param_count);
		
		// TempVars are now pre-counted in calculateFunctionStackSpace, include them in total
		// Also include outgoing_args_space for function calls made from this function
		// Note: named_vars_size already includes parameter home space, so don't add shadow_stack_space
		uint32_t total_stack_space = func_stack_space.named_vars_size + func_stack_space.temp_vars_size + func_stack_space.outgoing_args_space;

		
		// Even if parameters stay in registers, we need space to spill them if needed
		// Member functions have implicit 'this' pointer as first parameter
		if (param_count > 0 && total_stack_space < param_count * 8) {
			total_stack_space = static_cast<uint32_t>(param_count * 8);
		}
		
		// Ensure stack alignment to 16 bytes
		// System V AMD64 (Linux): After push rbp, RSP is at 16n. We need RSP at 16m+8 before calls.
		// So total_stack_space should be 16k+8 (rounds up to next 16k+8)
		// Windows x64: Different alignment rules, keep existing 16-byte alignment
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// Round up to 16k + 8 form for System V AMD64
			total_stack_space = ((total_stack_space + 7) & -16) + 8;
		} else {
			// Round up to 16k form for Windows x64
			total_stack_space = (total_stack_space + 15) & -16;
		}
		
		// Save function prologue information before setup
		current_function_offset_ = static_cast<uint32_t>(textSectionData.size());
		current_function_name_ = func_name_handle;
		current_function_prologue_offset_ = 0;
		
		uint32_t func_offset = static_cast<uint32_t>(textSectionData.size());
		writer.add_function_symbol(mangled_name, func_offset, total_stack_space, linkage);
		functionSymbols[std::string(func_name)] = func_offset;

		// Track function for debug information
		current_function_name_ = func_name_handle;
		current_function_mangled_name_ = mangled_handle;
		current_function_offset_ = func_offset;
		current_function_is_variadic_ = is_variadic;
		current_function_has_hidden_return_param_ = func_decl.has_hidden_return_param;  // Track for return statement handling
		current_function_returns_reference_ = func_decl.returns_reference;  // Track if function returns a reference

		// Patch pending branches from previous function before clearing
		if (!pending_branches_.empty()) {
			patchBranches();
		}

		// Clear control flow tracking for new function
		label_positions_.clear();
		pending_branches_.clear();

		// Set up debug information for this function
		// For now, use file ID 0 (first source file)
		writer.set_current_function_for_debug(std::string(func_name), 0);

		// If this is a member function, check if we need to register vtable for this class
		if (!struct_name.empty()) {
			// Look up the struct type info
			auto struct_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
			if (struct_it != gTypesByName.end()) {
				const TypeInfo* type_info = struct_it->second;
				const StructTypeInfo* struct_info = type_info->getStructInfo();
				
				if (struct_info && struct_info->has_vtable) {
					// Use the pre-generated vtable symbol from struct_info
					std::string_view vtable_symbol = struct_info->vtable_symbol;
					
					// Check if we've already registered this vtable
					bool vtable_exists = false;
					for (const auto& vt : vtables_) {
						if (vt.vtable_symbol == StringTable::getOrInternStringHandle(vtable_symbol)) {
							vtable_exists = true;
							break;
						}
					}
					
					if (!vtable_exists) {
						// Register this vtable - we'll populate function symbols as we encounter them
						VTableInfo vtable_info;
						vtable_info.vtable_symbol = StringTable::getOrInternStringHandle(vtable_symbol);
						vtable_info.class_name = StringTable::getOrInternStringHandle(struct_name);
						
						// Reserve space for vtable entries
						vtable_info.function_symbols.resize(struct_info->vtable.size());
						
						// Initialize vtable entries with appropriate function symbols:
						// - Pure virtual functions: __cxa_pure_virtual / _purecall
						// - Inherited functions (from base classes): base class's mangled function name
						// - Overridden functions: will be updated when we process the derived class's function definition
						const std::string_view pure_virtual_symbol = []()
						{
							if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
								return "__cxa_pure_virtual"sv;
							}
							return "_purecall"sv;
						}();
						for (size_t i = 0; const auto* vfunc : struct_info->vtable) {
							if (vfunc) {
								if (vfunc->is_pure_virtual) {
									vtable_info.function_symbols[i] = pure_virtual_symbol;
								} else {
									// Generate mangled name for this function
									// The function_decl contains information about which class owns this function
									std::string_view owning_struct_name;
									[[maybe_unused]] std::string_view vtable_func_name;
									
									if (vfunc->is_destructor) {
										// Destructor - get struct name from DestructorDeclarationNode
										const auto& dtor_node = vfunc->function_decl.as<DestructorDeclarationNode>();
										owning_struct_name = StringTable::getStringView(dtor_node.struct_name());
										
										// Generate mangled destructor name
										auto dtor_mangled = NameMangling::generateMangledNameFromNode(dtor_node);
										vtable_info.function_symbols[i] = dtor_mangled.view();
									} else if (!vfunc->is_constructor) {
										// Regular virtual function - get struct name from FunctionDeclarationNode
										const auto& func_node = vfunc->function_decl.as<FunctionDeclarationNode>();
										owning_struct_name = func_node.parent_struct_name();
										vtable_func_name = StringTable::getStringView(vfunc->getName());
										
										// Generate mangled function name using the function's owning struct
										auto vfunc_return_type = func_node.decl_node().type_node().as<TypeSpecifierNode>();
										const auto& vfunc_params = func_node.parameter_nodes();
										std::vector<std::string_view> empty_ns_path;
										auto vfunc_mangled = NameMangling::generateMangledName(
											vtable_func_name, vfunc_return_type, vfunc_params, false, 
											owning_struct_name, empty_ns_path, Linkage::CPlusPlus
										);
										vtable_info.function_symbols[i] = vfunc_mangled.view();
									}
								}
							}
							++i;
						}
						
						// Populate base class names for RTTI
						for (const auto& base : struct_info->base_classes) {
							if (base.type_index < gTypeInfo.size()) {
								const TypeInfo& base_type = gTypeInfo[base.type_index];
								if (base_type.isStruct()) {
									const StructTypeInfo* base_struct = base_type.getStructInfo();
									if (base_struct) {
										vtable_info.base_class_names.push_back(std::string(StringTable::getStringView(base_struct->getName())));
										
										// Add detailed base class info
										ObjectFileWriter::BaseClassDescriptorInfo bci;
										bci.name = std::string(StringTable::getStringView(base_struct->getName()));
										bci.num_contained_bases = static_cast<uint32_t>(base_struct->base_classes.size());
										bci.offset = static_cast<uint32_t>(base.offset);
										bci.is_virtual = base.is_virtual;
										vtable_info.base_class_info.push_back(bci);
									}
								}
							}
						}
						
						// Add RTTI information for this class
						vtable_info.rtti_info = struct_info->rtti_info;
						
						vtables_.push_back(std::move(vtable_info));
					}
					
					// Check if this function is virtual and add it to the vtable
					const StructMemberFunction* member_func = nullptr;
					// Use the unmangled function name for lookup (member_functions store unmangled names)
					// Phase 4: Use helper
					StringHandle unmangled_func_name_handle = func_decl.getFunctionName();
					for (const auto& func : struct_info->member_functions) {
						if (func.getName() == unmangled_func_name_handle) {
							member_func = &func;
							break;
						}
					}
					
					if (member_func && member_func->vtable_index >= 0) {
						// Find the vtable entry and update it with the mangled name
						for (auto& vt : vtables_) {
							if (vt.vtable_symbol == vtable_symbol) {
								if (member_func->vtable_index < static_cast<int>(vt.function_symbols.size())) {
									vt.function_symbols[member_func->vtable_index] = mangled_name;
									FLASH_LOG(Codegen, Debug, "  Added virtual function ", func_name, 
									          " at vtable index ", member_func->vtable_index);
								}
								break;
							}
						}
					}
				}
			}
		}

		// Add line mapping for function declaration (now that current function is set)
		if (instruction.getLineNumber() > 0) {
			// Also add line mapping for function opening brace (next line)
			addLineMapping(instruction.getLineNumber() + 1);
		}

		// Create a new function scope
		regAlloc.reset();

		// MSVC-style prologue.
		// For C++ EH functions (Windows): push rbp; sub rsp, N; lea rbp, [rsp+N]
		//   This makes establisher_frame = RBP - FrameOffset*16 = RSP_after_prologue,
		//   so _JumpToContinuation restores RSP to the fully-allocated frame level.
		// For non-EH functions: push rbp; mov rbp, rsp; sub rsp, N (traditional style).
		// Always generate prologue - even if total_stack_space is 0, we need RBP for parameter access
		textSectionData.push_back(0x55); // push rbp
		
		// Track CFI: After push rbp, CFA = RSP+16, RBP at CFA-16
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			current_function_cfi_.push_back({
				ElfFileWriter::CFIInstruction::PUSH_RBP,
				static_cast<uint32_t>(textSectionData.size() - current_function_offset_),
				0
			});
		}

		bool use_eh_prologue_style = false;
		if constexpr (!std::is_same_v<TWriterClass, ElfFileWriter>) {
			use_eh_prologue_style = current_function_has_cpp_eh_;
		}

		if (use_eh_prologue_style) {
			// C++ EH prologue: push rbp(1); sub rsp, N(7); lea rbp, [rsp+N](8)
			// Total: 16 bytes. RBP = RSP_after_push + N_sub - N_sub + N_lea = S-8.
			// FrameOffset = N/16 in UNWIND_INFO, establisher = RBP - N = RSP after sub.

			// SUB RSP, imm32 (7 bytes) - placeholder, patched at function end
			current_function_prologue_offset_ = static_cast<uint32_t>(textSectionData.size());
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x81); // SUB with 32-bit immediate
			textSectionData.push_back(0xEC); // RSP
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);

			// LEA RBP, [RSP + imm32] (8 bytes) - placeholder, patched at function end
			// Encoding: 48 8D AC 24 XX XX XX XX (REX.W LEA RBP, [RSP+disp32])
			eh_prologue_lea_rbp_offset_ = static_cast<uint32_t>(textSectionData.size());
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x8D); // LEA
			textSectionData.push_back(0xAC); // ModR/M: RBP, [SIB+disp32]
			textSectionData.push_back(0x24); // SIB: base=RSP, index=none
			textSectionData.push_back(0x00); // disp32 placeholder
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
		} else {
			// Traditional prologue: push rbp(1); mov rbp, rsp(3); sub rsp, N(7)
			textSectionData.push_back(0x48); textSectionData.push_back(0x8B); textSectionData.push_back(0xEC); // mov rbp, rsp
			
			// Track CFI: After mov rbp, rsp, CFA = RBP+16
			if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
				current_function_cfi_.push_back({
					ElfFileWriter::CFIInstruction::MOV_RSP_RBP,
					static_cast<uint32_t>(textSectionData.size() - current_function_offset_),
					0
				});
			}

			// SUB RSP, imm32 (7 bytes) - placeholder, patched at function end
			current_function_prologue_offset_ = static_cast<uint32_t>(textSectionData.size());
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x81); // SUB with 32-bit immediate
			textSectionData.push_back(0xEC); // RSP
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);

			eh_prologue_lea_rbp_offset_ = 0; // Not used for non-EH functions
		}

		// For C++ EH functions on Windows, initialize the FH3 unwind help state variable at [rbp-8] to -2.
		// FH3 reads this via dispUnwindHelp; value -2 means "use IP-to-state map" for lookup.
		if constexpr (!std::is_same_v<TWriterClass, ElfFileWriter>) {
			if (current_function_has_cpp_eh_) {
				// mov qword [rbp-8], -2  (8 bytes: 48 C7 45 F8 FE FF FF FF)
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0xC7); // MOV r/m64, imm32
				textSectionData.push_back(0x45); // [rbp + disp8]
				textSectionData.push_back(0xF8); // disp8 = -8
				textSectionData.push_back(0xFE); // imm32 = 0xFFFFFFFE = -2
				textSectionData.push_back(0xFF);
				textSectionData.push_back(0xFF);
				textSectionData.push_back(0xFF);
			}
		}

		// For RBP-relative addressing, we start with negative offset after total allocated space
		if (variable_scopes.empty()) {
			FLASH_LOG(Codegen, Error, "FATAL: variable_scopes is EMPTY!");
			std::abort();
		}
		// Set scope_stack_space to include ALL pre-allocated space (named + shadow + temp_vars)
		// TempVars are allocated within this space, not extending beyond it
		variable_scopes.back().scope_stack_space = -total_stack_space;
		
		// Store named_vars size for TempVar offset calculation
		// Note: named_vars_size already includes parameter home space
		// IMPORTANT: Don't include outgoing_args_space here - TempVars go AFTER named vars but BEFORE outgoing args
		current_function_named_vars_size_ = func_stack_space.named_vars_size;

		// Handle parameters
		struct ParameterInfo {
			Type param_type;
			int param_size;
			std::string_view param_name;
			int paramNumber;
			int offset;
			X64Register src_reg;
			int pointer_depth;
			bool is_reference;
		};
		std::vector<ParameterInfo> parameters;

		// For member functions, add implicit 'this' pointer as first parameter
		int param_offset_adjustment = 0;
		
		// For functions returning struct by value, add hidden return parameter FIRST
		// This comes BEFORE all other parameters (including 'this' for member functions)
		// System V AMD64: hidden param in RDI (first register)
		// Windows x64: hidden param in RCX (first register)
		if (func_decl.has_hidden_return_param) {
			int return_slot_offset = -8;  // Hidden return parameter is always first, so offset -8
			variable_scopes.back().variables[StringTable::getOrInternStringHandle("__return_slot")].offset = return_slot_offset;
			
			X64Register return_slot_reg = getIntParamReg<TWriterClass>(0);  // Always first register
			parameters.push_back({Type::Struct, 64, "__return_slot", 0, return_slot_offset, return_slot_reg, 1, false});
			regAlloc.allocateSpecific(return_slot_reg, return_slot_offset);
			
			param_offset_adjustment = 1;  // Shift other parameters (including 'this') by 1
			
			FLASH_LOG_FORMAT(Codegen, Debug,
				"Function {} has hidden return parameter at offset {} in register {}",
				func_name, return_slot_offset, static_cast<int>(return_slot_reg));
		}
		
		// For non-static member functions, add 'this' pointer parameter
		// This comes after hidden return parameter (if present)
		// Static member functions have no 'this' pointer
		int this_offset_saved = 0;  // Will be set if this is a member function
		if (!struct_name.empty() && !func_decl.is_static_member) {
			// 'this' offset depends on whether there's a hidden return parameter
			int this_offset = (param_offset_adjustment + 1) * -8;
			this_offset_saved = this_offset;  // Save for later reference_stack_info_ registration
			variable_scopes.back().variables[StringTable::getOrInternStringHandle("this")].offset = this_offset;

			// Add 'this' parameter to debug information
			writer.add_function_parameter("this", 0x603, this_offset);  // 0x603 = T_64PVOID (pointer type)

			// Store 'this' parameter info (register depends on param_offset_adjustment)
			X64Register this_reg = getIntParamReg<TWriterClass>(param_offset_adjustment);
			parameters.push_back({Type::Struct, 64, "this", param_offset_adjustment, this_offset, this_reg, 1, false});
			regAlloc.allocateSpecific(this_reg, this_offset);

			param_offset_adjustment++;  // Shift regular parameters by 1 more
		}

		// Use separate counters for integer and float parameter registers (System V AMD64 ABI)
		// For member functions, 'this' was already added above and consumed index 0,
		// so we start counting from param_offset_adjustment (which is 1 for member functions)
		// These counters are used to compute gp_offset/fp_offset for variadic functions
		size_t int_param_reg_index = param_offset_adjustment;
		size_t float_param_reg_index = 0;
		
		// First pass: collect all parameter information
		if (!instruction.hasTypedPayload()) {
			// Operand-based path: extract parameters from operands
			size_t paramIndex = FunctionDeclLayout::FIRST_PARAM_INDEX;
			// Clear reference parameter tracking from previous function
			reference_stack_info_.clear();
			
			// Register 'this' as a pointer in reference_stack_info_ (AFTER the clear)
			// This is critical for member function calls that pass 'this' as an argument
			// Without this, the codegen would use LEA (address-of) instead of MOV (load)
			// Set holds_address_only = true because 'this' is a pointer, not a reference -
			// when we return 'this', we should return the pointer value itself, not dereference it
			if (!struct_name.empty() && !func_decl.is_static_member) {
				setReferenceInfo(this_offset_saved, Type::Struct, 64, false);
				reference_stack_info_[this_offset_saved].holds_address_only = true;
			}
			
			while (paramIndex + FunctionDeclLayout::OPERANDS_PER_PARAM <= instruction.getOperandCount()) {
				auto param_type = instruction.getOperandAs<Type>(paramIndex + FunctionDeclLayout::PARAM_TYPE);
				auto param_size = instruction.getOperandAs<int>(paramIndex + FunctionDeclLayout::PARAM_SIZE);
				auto param_pointer_depth = instruction.getOperandAs<int>(paramIndex + FunctionDeclLayout::PARAM_POINTER_DEPTH);
				// Fetch is_reference early since it affects register selection (references use integer regs, not float regs)
				bool is_reference = instruction.getOperandAs<bool>(paramIndex + FunctionDeclLayout::PARAM_IS_REFERENCE);

				// Calculate parameter number using FunctionDeclLayout helper
				size_t param_index_in_list = (paramIndex - FunctionDeclLayout::FIRST_PARAM_INDEX) / FunctionDeclLayout::OPERANDS_PER_PARAM;
				int paramNumber = static_cast<int>(param_index_in_list) + param_offset_adjustment;
			
				// Platform-specific and type-aware offset calculation
				size_t max_int_regs = getMaxIntParamRegs<TWriterClass>();
				size_t max_float_regs = getMaxFloatParamRegs<TWriterClass>();
				// Reference parameters (including rvalue references) are passed as pointers,
				// so they should use integer registers regardless of the underlying type
				bool is_float_param = (param_type == Type::Float || param_type == Type::Double) && param_pointer_depth == 0 && !is_reference;
			
				// Determine the register count threshold for this parameter type
				size_t reg_threshold = is_float_param ? max_float_regs : max_int_regs;
				size_t type_specific_index = is_float_param ? float_param_reg_index : int_param_reg_index;
			
				// Calculate offset based on whether this parameter comes from a register or stack
				// For Windows variadic functions: ALL parameters are on caller's stack starting at [RBP+16]
				constexpr bool is_coff_format = !std::is_same_v<TWriterClass, ElfFileWriter>;
				int offset;
				if (is_variadic && is_coff_format) {
					// Windows x64 variadic: ALL params at positive offsets from RBP
					// paramNumber is 0-based, so first param is at +16, second at +24, etc.
					offset = 16 + (paramNumber - param_offset_adjustment) * 8;
				} else if (type_specific_index < reg_threshold) {
					// Parameter comes from register - allocate home/shadow space
					// Use paramNumber for sequential stack allocation (not type_specific_index)
					// This ensures int and float parameters don't overlap on the stack
					offset = (paramNumber + 1) * -8;
				} else {
					// Parameter comes from stack - calculate positive offset
					// Stack parameters start at [rbp+16] (after return address at [rbp+8] and saved rbp at [rbp+0])
					// Stack params start after: saved rbp [+0], return addr [+8], shadow space (32 on Win64, 0 on SysV)
					offset = 16 + static_cast<int>(getShadowSpaceSize<TWriterClass>()) + static_cast<int>(type_specific_index - reg_threshold) * 8;
				}

				StringHandle param_name_handle = instruction.getOperandAs<StringHandle>(paramIndex + FunctionDeclLayout::PARAM_NAME);
				std::string_view param_name = StringTable::getStringView(param_name_handle);
				variable_scopes.back().variables[param_name_handle].offset = offset;
				variable_scopes.back().variables[param_name_handle].size_in_bits = param_size;

				// Track reference parameters by their stack offset (they need pointer dereferencing like 'this')
				// Also track large struct parameters (> 64 bits) which are passed by pointer
				// NOTE: Pointer parameters (T*) are NOT tracked here. They hold pointer VALUES directly
				// on the stack, not references. Accessing a pointer param should yield the pointer value;
				// explicit dereference (*ptr) is handled by handleDereference which loads from stack directly.
				// Registering pointers here caused auto-dereferencing in comparisons (e.g., ptr == 0 crashes).
				bool is_passed_by_reference = is_reference ||
				                              (param_type == Type::Struct && param_size > 64);
				if (is_passed_by_reference) {
					setReferenceInfo(offset, param_type, param_size,
						instruction.getOperandAs<bool>(paramIndex + FunctionDeclLayout::PARAM_IS_RVALUE_REFERENCE));
				}

				// Add parameter to debug information
				uint32_t param_type_index = 0x74; // T_INT4 for int parameters
				if (param_pointer_depth > 0) {
					param_type_index = 0x603;  // T_64PVOID for pointer types
				} else {
					switch (param_type) {
						case Type::Int: param_type_index = 0x74; break;  // T_INT4
						case Type::Float: param_type_index = 0x40; break; // T_REAL32
						case Type::Double: param_type_index = 0x41; break; // T_REAL64
						case Type::Char: param_type_index = 0x10; break;  // T_CHAR
						case Type::Bool: param_type_index = 0x30; break;  // T_BOOL08
						case Type::Struct: param_type_index = 0x603; break;  // T_64PVOID for struct pointers
						default: param_type_index = 0x74; break;
					}
				}
				writer.add_function_parameter(std::string(param_name), param_type_index, offset);

				// Check if parameter fits in a register using separate int/float counters
				bool use_register = false;
				X64Register src_reg = X64Register::Count;
				if (is_float_param) {
					if (float_param_reg_index < max_float_regs) {
						src_reg = getFloatParamReg<TWriterClass>(float_param_reg_index++);
						use_register = true;
					} else {
						float_param_reg_index++;  // Still increment counter for stack params
					}
				} else {
					if (int_param_reg_index < max_int_regs) {
						src_reg = getIntParamReg<TWriterClass>(int_param_reg_index++);
						use_register = true;
					} else {
						int_param_reg_index++;  // Still increment counter for stack params
					}
				}
			
				if (use_register) {

					// Don't allocate XMM registers in the general register allocator
					if (!is_float_param) {
						regAlloc.allocateSpecific(src_reg, offset);
					}

					// Store parameter info for later processing
					parameters.push_back({param_type, param_size, param_name, paramNumber, offset, src_reg, param_pointer_depth, is_reference});
				}

				paramIndex += FunctionDeclLayout::OPERANDS_PER_PARAM;
			}
		} else {
			// Typed payload path: build ParameterInfo from already-extracted parameter_types
			[[maybe_unused]] const auto& typed_func_decl = instruction.getTypedPayload<FunctionDeclOp>();
			reference_stack_info_.clear();
			
			// Register 'this' as a pointer in reference_stack_info_ (AFTER the clear)
			// This is critical for member function calls that pass 'this' as an argument
			// Without this, the codegen would use LEA (address-of) instead of MOV (load)
			// Set holds_address_only = true because 'this' is a pointer, not a reference -
			// when we return 'this', we should return the pointer value itself, not dereference it
			if (!struct_name.empty() && !func_decl.is_static_member) {
				setReferenceInfo(this_offset_saved, Type::Struct, 64, false);
				reference_stack_info_[this_offset_saved].holds_address_only = true;
			}
		
			// Reset counters for this code path (they start at param_offset_adjustment for int, 0 for float)
			// The counters were already declared before the if/else block
			int_param_reg_index = param_offset_adjustment;
			float_param_reg_index = 0;
		
			for (size_t i = 0; i < func_decl.parameters.size(); ++i) {
				const auto& param = func_decl.parameters[i];
				int paramNumber = static_cast<int>(i) + param_offset_adjustment;
			
				// Platform-specific and type-aware offset calculation
				size_t max_int_regs = getMaxIntParamRegs<TWriterClass>();
				size_t max_float_regs = getMaxFloatParamRegs<TWriterClass>();
				// Reference parameters (including rvalue references) are passed as pointers,
				// so they should use integer registers regardless of the underlying type
				bool is_float_param = (param.type == Type::Float || param.type == Type::Double) && param.pointer_depth == 0 && !param.is_reference;
			
				// Determine the register count threshold for this parameter type
				size_t reg_threshold = is_float_param ? max_float_regs : max_int_regs;
				size_t type_specific_index = is_float_param ? float_param_reg_index : int_param_reg_index;
			
				// Calculate offset based on whether this parameter comes from a register or stack
				// For Windows variadic functions: ALL parameters are on caller's stack starting at [RBP+16]
				constexpr bool is_coff_format = !std::is_same_v<TWriterClass, ElfFileWriter>;
				int offset;
				if (is_variadic && is_coff_format) {
					// Windows x64 variadic: ALL params at positive offsets from RBP
					// paramNumber is 0-based, so first param is at +16, second at +24, etc.
					offset = 16 + (paramNumber - param_offset_adjustment) * 8;
				} else if (type_specific_index < reg_threshold) {
					// Parameter comes from register - allocate home/shadow space
					// Use paramNumber for sequential stack allocation (not type_specific_index)
					// This ensures int and float parameters don't overlap on the stack
					offset = (paramNumber + 1) * -8;
				} else {
					// Parameter comes from stack - calculate positive offset
					// Stack params start after: saved rbp [+0], return addr [+8], shadow space (32 on Win64, 0 on SysV)
					offset = 16 + static_cast<int>(getShadowSpaceSize<TWriterClass>()) + static_cast<int>(type_specific_index - reg_threshold) * 8;
				}

				// Phase 4: Use helper to get param name for map key
				variable_scopes.back().variables[param.getName()].offset = offset;
				variable_scopes.back().variables[param.getName()].size_in_bits = param.size_in_bits;

				// Track reference parameters by their stack offset (they need pointer dereferencing)
				// Also track large struct parameters (> 64 bits) which are passed by pointer
				// NOTE: Pointer parameters (T*) are NOT tracked - they hold pointer VALUES directly.
				// Explicit dereference (*ptr) is handled by handleDereference which loads from stack directly.
				bool is_passed_by_reference = param.is_reference ||
				                              (param.type == Type::Struct && param.size_in_bits > 64);
				if (is_passed_by_reference) {
					setReferenceInfo(offset, param.type, param.size_in_bits, param.is_rvalue_reference);
				}

				// Add parameter to debug information
				uint32_t param_type_index = 0x74; // T_INT4 for int parameters
				if (param.pointer_depth > 0) {
					param_type_index = 0x603;  // T_64PVOID for pointer types
				} else {
					switch (param.type) {
						case Type::Int: param_type_index = 0x74; break;  // T_INT4
						case Type::Float: param_type_index = 0x40; break; // T_REAL32
						case Type::Double: param_type_index = 0x41; break; // T_REAL64
						case Type::Char: param_type_index = 0x10; break;  // T_CHAR
						case Type::Bool: param_type_index = 0x30; break;  // T_BOOL08
						case Type::Struct: param_type_index = 0x603; break;  // T_64PVOID for struct pointers
						default: param_type_index = 0x74; break;
					}
				}
				// Phase 4: Use helper to get param name
				std::string param_name_str(StringTable::getStringView(param.getName()));
				writer.add_function_parameter(param_name_str, param_type_index, offset);

				// Check if parameter fits in a register using separate int/float counters
				bool use_register = false;
				X64Register src_reg = X64Register::Count;
				if (is_float_param) {
					if (float_param_reg_index < max_float_regs) {
						src_reg = getFloatParamReg<TWriterClass>(float_param_reg_index++);
						use_register = true;
					} else {
						float_param_reg_index++;  // Still increment counter for stack params
					}
				} else {
					if (int_param_reg_index < max_int_regs) {
						src_reg = getIntParamReg<TWriterClass>(int_param_reg_index++);
						use_register = true;
					} else {
						int_param_reg_index++;  // Still increment counter for stack params
					}
				}
			
				if (use_register) {

					if (!is_float_param && !regAlloc.is_allocated(src_reg)) {
						regAlloc.allocateSpecific(src_reg, offset);
					}

					parameters.push_back({param.type, param.size_in_bits, StringTable::getStringView(param.getName()), paramNumber, offset, src_reg, param.pointer_depth, param.is_reference});
				}
			}
		}

		// Second pass: generate parameter storage code in the correct order

		// The callee is always responsible for homing its register parameters to the shadow space
		// (Windows x64) or its local frame (Linux). This ensures va_list/va_arg can walk a
		// contiguous memory region and that parameter values are accessible at their assigned offsets.
		constexpr bool is_coff_format_spill = !std::is_same_v<TWriterClass, ElfFileWriter>;
		if (is_variadic && is_coff_format_spill) {
			// Windows x64 variadic: home ALL register arg slots (named + variadic) to shadow space.
			// The caller must NOT pre-populate shadow space (doing so corrupts caller locals that
			// share those addresses). The callee owns shadow space homing per the x64 ABI.
			const size_t max_regs = getMaxIntParamRegs<TWriterClass>();
			for (size_t i = 0; i < max_regs; ++i) {
				int slot_offset = 16 + static_cast<int>(i) * 8;
				emitMovToFrame(getIntParamReg<TWriterClass>(i), slot_offset, 64);
			}
		} else {
			for (const auto& param : parameters) {
				// MSVC-STYLE: Store parameters using RBP-relative addressing
				bool is_float_param = (param.param_type == Type::Float || param.param_type == Type::Double) && param.pointer_depth == 0;

				if (is_float_param) {
					// For floating-point parameters, use movss/movsd to store from XMM register
					bool is_float = (param.param_type == Type::Float);
					emitFloatMovToFrame(param.src_reg, param.offset, is_float);
				} else {
					// For integer parameters, use size-appropriate MOV
					// References are always passed as 64-bit pointers regardless of the type they refer to
					// Large struct parameters (> 64 bits) are passed by pointer according to System V AMD64 ABI
					bool is_passed_by_pointer = param.is_reference || param.pointer_depth > 0 ||
					                            (param.param_type == Type::Struct && param.param_size > 64);
					int store_size = is_passed_by_pointer ? 64 : param.param_size;
					emitMovToFrameSized(
						SizedRegister{param.src_reg, 64, false},  // source: 64-bit register
						SizedStackSlot{param.offset, store_size, isSignedType(param.param_type)}  // dest
					);
				
					// Release the parameter register from the register allocator
					// Parameters are now on the stack, so the register allocator should not
					// think they're still in registers
					regAlloc.release(param.src_reg);
				}
			}
		}
		
		// For Linux (System V AMD64) variadic functions: Create register save area and va_list structure
		// On System V AMD64, variadic arguments are passed in registers, so we need to
		// save all potential variadic argument registers to a register save area and
		// create a va_list structure to track offsets
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			if (is_variadic) {
				// System V AMD64 ABI register save area layout:
				// Integer registers: RDI, RSI, RDX, RCX, R8, R9  (6 registers * 8 bytes = 48 bytes)
				// Float registers: XMM0-XMM7  (8 registers * 16 bytes = 128 bytes, need full 16 for alignment)
				// Total register save area: 176 bytes
				//
				// Additionally, we need a va_list structure (compatible with System V AMD64):
				// struct __va_list_tag {
				//     unsigned int gp_offset;       // 4 bytes - offset into integer registers (0-48)
				//     unsigned int fp_offset;       // 4 bytes - offset into float registers (48-176)  
				//     void *overflow_arg_area;      // 8 bytes - stack overflow area
				//     void *reg_save_area;          // 8 bytes - pointer to register save area
				// };  // Total: 24 bytes
				
				// Calculate layout offsets
				constexpr int INT_REG_AREA_SIZE = 6 * 8;      // 48 bytes for integer registers
				constexpr int FLOAT_REG_AREA_SIZE = 8 * 16;   // 128 bytes for XMM registers  
				constexpr int REG_SAVE_AREA_SIZE = INT_REG_AREA_SIZE + FLOAT_REG_AREA_SIZE;  // 176 bytes
				constexpr int VA_LIST_STRUCT_SIZE = 24;        // Size of va_list structure
				
				// Allocate space: register save area first, then va_list structure
				int32_t reg_save_area_base = variable_scopes.back().scope_stack_space - REG_SAVE_AREA_SIZE;
				int32_t va_list_struct_base = reg_save_area_base - VA_LIST_STRUCT_SIZE;
				current_function_varargs_reg_save_offset_ = reg_save_area_base;
				
				// Update the scope stack space to include both areas
				variable_scopes.back().scope_stack_space = va_list_struct_base;
				
				// Save all integer registers: RDI, RSI, RDX, RCX, R8, R9 at offsets 0-47
				// (RDI is the first fixed param but we save it for completeness)
				constexpr X64Register int_regs[] = {
					X64Register::RDI,  // Offset 0
					X64Register::RSI,  // Offset 8
					X64Register::RDX,  // Offset 16
					X64Register::RCX,  // Offset 24
					X64Register::R8,   // Offset 32
					X64Register::R9    // Offset 40
				};
				constexpr size_t INT_REG_COUNT = sizeof(int_regs) / sizeof(int_regs[0]);
				static_assert(INT_REG_COUNT == 6, "System V AMD64 ABI has exactly 6 integer argument registers");
				
				// Number of XMM registers saved in register save area (System V AMD64 ABI)
				constexpr size_t FLOAT_REG_COUNT = 8;
				
				for (size_t i = 0; i < INT_REG_COUNT; ++i) {
					int32_t offset = reg_save_area_base + static_cast<int32_t>(i * 8);
					emitMovToFrameSized(
						SizedRegister{int_regs[i], 64, false},  // source: 64-bit register
						SizedStackSlot{offset, 64, false}  // dest: 64-bit stack slot
					);
				}
				
				// Save all float registers: XMM0-XMM7 at offsets 48-175
				// Use full 16 bytes per register for proper alignment
				for (size_t i = 0; i < FLOAT_REG_COUNT; ++i) {
					X64Register xmm_reg = static_cast<X64Register>(static_cast<int>(X64Register::XMM0) + i);
					int32_t offset = reg_save_area_base + INT_REG_AREA_SIZE + static_cast<int32_t>(i * 16);
					emitMovdquToFrame(xmm_reg, offset);
				}
				
				// Register special variables for va_list structure and register save area
				variable_scopes.back().variables[StringTable::getOrInternStringHandle("__varargs_va_list_struct__")].offset = va_list_struct_base;
				variable_scopes.back().variables[StringTable::getOrInternStringHandle("__varargs_reg_save_area__")].offset = reg_save_area_base;
				
				// Initialize the va_list structure fields directly in the function prologue
				// This avoids IR complexity with pointer arithmetic and dereferencing
				// Structure layout (24 bytes total):
				//   unsigned int gp_offset;       // offset 0 (4 bytes): Skip fixed integer parameters in registers
				//   unsigned int fp_offset;       // offset 4 (4 bytes): Skip fixed float parameters in registers  
				//   void *overflow_arg_area;      // offset 8 (8 bytes): NULL for now (not used for register args)
				//   void *reg_save_area;          // offset 16 (8 bytes): Pointer to register save area base
				
				// Calculate gp_offset: skip registers used by fixed integer parameters
				// Each integer register slot is 8 bytes, capped at 6 (INT_REG_COUNT)
				size_t fixed_int_params = std::min(int_param_reg_index, static_cast<size_t>(INT_REG_COUNT));
				int initial_gp_offset = static_cast<int>(fixed_int_params * 8);
				
				// Calculate fp_offset: skip registers used by fixed float parameters
				// Float registers start at offset 48 (after integer registers), each is 16 bytes
				size_t fixed_float_params = std::min(float_param_reg_index, FLOAT_REG_COUNT);
				int initial_fp_offset = INT_REG_AREA_SIZE + static_cast<int>(fixed_float_params * 16);
				
				// Load va_list structure base address into RAX
				emitLeaFromFrame(X64Register::RAX, va_list_struct_base);
				
				// Store gp_offset at [RAX + 0]
				emitMovDwordPtrImmToRegOffset(X64Register::RAX, 0, initial_gp_offset);
				
				// Store fp_offset at [RAX + 4]
				emitMovDwordPtrImmToRegOffset(X64Register::RAX, 4, initial_fp_offset);
				
				// Store overflow_arg_area at [RAX + 8]
				// For System V AMD64 ABI, overflow arguments are passed on the stack
				// by the caller. They start at [RBP+16] (after saved RBP and return address).
				// LEA RCX, [RBP + 16] then store to [RAX + 8]
				emitLeaFromFrame(X64Register::RCX, 16);  // overflow args are at RBP+16
				emitMovQwordPtrRegToRegOffset(X64Register::RAX, 8, X64Register::RCX);
				
				// Store reg_save_area pointer at [RAX + 16]
				// Load register save area address into RCX
				emitLeaFromFrame(X64Register::RCX, reg_save_area_base);
				emitMovQwordPtrRegToRegOffset(X64Register::RAX, 16, X64Register::RCX);
			}
		}
	}

	// Helper function to get the actual size of a variable for proper zero/sign-extension
	int getActualVariableSize(StringHandle var_name, int default_size) const {
		if (variable_scopes.empty()) {
			return default_size;
		}
		
		const auto& current_scope = variable_scopes.back();
		auto var_it = current_scope.variables.find(var_name);
		if (var_it != current_scope.variables.end()) {
			// Return the stored size if it's non-zero, otherwise use default
			if (var_it->second.size_in_bits > 0) {
				return var_it->second.size_in_bits;
			}
		}
		
		return default_size;
	}

	int32_t ensureCatchFuncletReturnSlot() {
		if (catch_funclet_return_slot_offset_ != 0) {
			return catch_funclet_return_slot_offset_;
		}

		if (variable_scopes.empty()) {
			catch_funclet_return_slot_offset_ = -8;
			return catch_funclet_return_slot_offset_;
		}

		StackVariableScope& current_scope = variable_scopes.back();
		int32_t reserved_slot = static_cast<int32_t>(current_scope.scope_stack_space - 8);
		current_scope.scope_stack_space = reserved_slot;
		catch_funclet_return_slot_offset_ = reserved_slot;
		return catch_funclet_return_slot_offset_;
	}

	void handleReturn(const IrInstruction& instruction) {
		FLASH_LOG(Codegen, Debug, "handleReturn called");
		
		if (variable_scopes.empty()) {
			FLASH_LOG(Codegen, Error, "FATAL [handleReturn]: variable_scopes is EMPTY!");
			std::abort();
		}
		
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			if (inside_catch_handler_ && g_enable_exceptions) {
				emitCall("__cxa_end_catch");
				inside_catch_handler_ = false;
			}
		}

		// Add line mapping for the return statement itself (only for functions without function calls)
		// For functions with function calls (like main), the closing brace is already mapped in handleFunctionCall
		if (instruction.getLineNumber() > 0 && current_function_name_ != StringTable::getOrInternStringHandle("main")) {	// TODO: Is main special case still needed here?
			addLineMapping(instruction.getLineNumber());
		}

		// Check for typed payload first
		if (instruction.hasTypedPayload()) {
			const auto& ret_op = instruction.getTypedPayload<ReturnOp>();
			
			// Void return - no value to return
			if (!ret_op.return_value.has_value()) {
				// Fall through to epilogue generation below
			}
			else {
				// Return with value
				const auto& ret_val = ret_op.return_value.value();
				
				// Debug: log what type of return value we have
				if (std::holds_alternative<unsigned long long>(ret_val)) {
					FLASH_LOG(Codegen, Debug, "Return value type: unsigned long long");
				} else if (std::holds_alternative<TempVar>(ret_val)) {
					FLASH_LOG(Codegen, Debug, "Return value type: TempVar");
				} else if (std::holds_alternative<StringHandle>(ret_val)) {
					FLASH_LOG(Codegen, Debug, "Return value type: StringHandle");
				} else if (std::holds_alternative<double>(ret_val)) {
					FLASH_LOG(Codegen, Debug, "Return value type: double");
				} else {
					FLASH_LOG(Codegen, Debug, "Return value type: UNKNOWN");
				}
				
				if (std::holds_alternative<unsigned long long>(ret_val)) {
					unsigned long long returnValue = std::get<unsigned long long>(ret_val);
					
					// Check if this is actually a negative number stored as unsigned long long
					if (returnValue > std::numeric_limits<uint32_t>::max()) {
						uint32_t lower32 = static_cast<uint32_t>(returnValue);
						if ((returnValue >> 32) == 0xFFFFFFFF) {
							returnValue = lower32;
						} else {
							throw InternalError("Return value exceeds 32-bit limit");
						}
					}

					// mov eax, immediate instruction has a fixed size of 5 bytes
					std::array<uint8_t, 5> movEaxImmedInst = { 0xB8, 0, 0, 0, 0 };
					for (size_t i = 0; i < 4; ++i) {
						movEaxImmedInst[i + 1] = (returnValue >> (8 * i)) & 0xFF;
					}
					textSectionData.insert(textSectionData.end(), movEaxImmedInst.begin(), movEaxImmedInst.end());
				}
				else if (std::holds_alternative<TempVar>(ret_val)) {
					// Handle temporary variable (stored on stack)
					auto return_var = std::get<TempVar>(ret_val);
					auto temp_var_name = StringTable::getOrInternStringHandle(return_var.name());
					const StackVariableScope& current_scope = variable_scopes.back();
					auto it = current_scope.variables.find(temp_var_name);
					
					FLASH_LOG_FORMAT(Codegen, Debug,
						"handleReturn TempVar path: return_var={}, found_in_scope={}",
						return_var.name(), (it != current_scope.variables.end()));
					
					// Check if return type is float/double
					bool is_float_return = ret_op.return_type.has_value() && 
					                        is_floating_point_type(ret_op.return_type.value());

					bool handled_reference_return = false;
					{
						auto lv_info_opt = getTempVarLValueInfo(return_var);
						auto return_meta = getTempVarMetadata(return_var);
						FLASH_LOG(Codegen, Debug, "handleReturn: lvalue metadata present=", lv_info_opt.has_value(), ", returns_reference=", current_function_returns_reference_, ", is_address=", return_meta.is_address);
						if (lv_info_opt.has_value() && (current_function_returns_reference_ || return_meta.is_address)) {
							const LValueInfo& lv_info = lv_info_opt.value();
							auto loadBaseAddress = [&](const std::variant<StringHandle, TempVar>& base, bool base_is_pointer) -> bool {
								int base_offset = 0;
								if (std::holds_alternative<StringHandle>(base)) {
									auto base_name = std::get<StringHandle>(base);
									auto offset_opt = findIdentifierStackOffset(base_name);
									if (!offset_opt.has_value()) {
										return false;
									}
									base_offset = offset_opt.value();
								} else {
									base_offset = getStackOffsetFromTempVar(std::get<TempVar>(base));
								}

								if (base_is_pointer) {
									emitMovFromFrame(X64Register::RAX, base_offset);
								} else {
									emitLeaFromFrame(X64Register::RAX, base_offset);
								}
								return true;
							};

							switch (lv_info.kind) {
								case LValueInfo::Kind::Indirect: {
									if (loadBaseAddress(lv_info.base, true)) {
										if (lv_info.offset != 0) {
											emitAddImmToReg(textSectionData, X64Register::RAX, lv_info.offset);
										}
										handled_reference_return = true;
									}
									break;
								}
								case LValueInfo::Kind::Direct: {
									if (loadBaseAddress(lv_info.base, false)) {
										if (lv_info.offset != 0) {
											emitAddImmToReg(textSectionData, X64Register::RAX, lv_info.offset);
										}
										handled_reference_return = true;
									}
									break;
								}
								case LValueInfo::Kind::Member: {
									bool base_is_pointer = lv_info.is_pointer_to_member;
									if (loadBaseAddress(lv_info.base, base_is_pointer)) {
										if (!base_is_pointer) {
											emitAddImmToReg(textSectionData, X64Register::RAX, lv_info.offset);
										} else if (lv_info.offset != 0) {
											emitAddImmToReg(textSectionData, X64Register::RAX, lv_info.offset);
										}
										handled_reference_return = true;
									}
									break;
								}
								default:
									break;
							}

							if (handled_reference_return) {
								regAlloc.flushSingleDirtyRegister(X64Register::RAX);
							}
						}
					}
					
					if (handled_reference_return) {
						// Address already loaded into RAX for reference return
					}
					else if (it != current_scope.variables.end()) {
						int var_offset = it->second.offset;
						
						// Ensure stack space is allocated for large structs being returned
						// The TempVar might have been pre-allocated with default size, so re-check with actual size
						if (ret_op.return_size > 64) {
							// Call getStackOffsetFromTempVar with the correct size to extend scope if needed
							getStackOffsetFromTempVar(return_var, ret_op.return_size);
						}
						
					// Check if this is a reference variable - if so, dereference it
						// EXCEPT when the function itself returns a reference - in that case, return the address as-is
						// Also dereference rvalue references (from std::move) when returning by value
						auto ref_it = reference_stack_info_.find(var_offset);
						if (ref_it != reference_stack_info_.end() && 
							(ref_it->second.is_rvalue_reference || !ref_it->second.holds_address_only) && 
							!current_function_returns_reference_) {
							// This is a reference and function returns by value
							// Check if function uses hidden return parameter (struct return)
							if (current_function_has_hidden_return_param_) {
								// Returning via rvalue reference (std::move) to a struct-returning function
								// Need to copy the struct from the referenced location to the return slot
								FLASH_LOG(Codegen, Debug, "handleReturn: Copying struct via rvalue reference at offset ", var_offset);
								
								// Load return slot address from __return_slot parameter
								auto return_slot_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle("__return_slot"));
								if (return_slot_it != variable_scopes.back().variables.end()) {
									int return_slot_param_offset = return_slot_it->second.offset;
									
									// Load the source address (where the rvalue reference points)
									X64Register src_reg = allocateRegisterWithSpilling();
									emitMovFromFrame(src_reg, var_offset);  // Load the pointer from rvalue reference
									
									// Load the destination address (return slot)
									X64Register dest_reg = allocateRegisterWithSpilling();
									emitMovFromFrame(dest_reg, return_slot_param_offset);
									
									// Get struct size from the return operation, not the reference info
									// ref_it->second.value_size_bits would be 64 (pointer size), but we need
									// the actual struct size that the function returns
									int struct_size_bytes = ret_op.return_size / 8;
									FLASH_LOG_FORMAT(Codegen, Debug,
										"Copying struct via rvalue ref: size={} bytes, from ref at offset {}, return_slot at offset {}",
										struct_size_bytes, var_offset, return_slot_param_offset);
									
									// Copy struct from source to destination
									int bytes_copied = 0;
									X64Register temp_reg = allocateRegisterWithSpilling();
									
									// Copy 8-byte chunks
									while (bytes_copied + 8 <= struct_size_bytes) {
										emitMovFromMemory(temp_reg, src_reg, bytes_copied, 8);
										emitStoreToMemory(textSectionData, temp_reg, dest_reg, bytes_copied, 8);
										bytes_copied += 8;
									}
									
									// Handle remaining bytes (4, 2, 1)
									if (bytes_copied + 4 <= struct_size_bytes) {
										emitMovFromMemory(temp_reg, src_reg, bytes_copied, 4);
										emitStoreToMemory(textSectionData, temp_reg, dest_reg, bytes_copied, 4);
										bytes_copied += 4;
									}
									if (bytes_copied + 2 <= struct_size_bytes) {
										emitMovFromMemory(temp_reg, src_reg, bytes_copied, 2);
										emitStoreToMemory(textSectionData, temp_reg, dest_reg, bytes_copied, 2);
										bytes_copied += 2;
									}
									if (bytes_copied + 1 <= struct_size_bytes) {
										emitMovFromMemory(temp_reg, src_reg, bytes_copied, 1);
										emitStoreToMemory(textSectionData, temp_reg, dest_reg, bytes_copied, 1);
										bytes_copied += 1;
									}
									
									regAlloc.release(temp_reg);
									regAlloc.release(dest_reg);
									regAlloc.release(src_reg);
									
									// For struct return, RAX should contain the return slot address (per ABI)
									emitMovFromFrame(X64Register::RAX, return_slot_param_offset);
									
									FLASH_LOG_FORMAT(Codegen, Debug,
										"Struct copy via rvalue ref complete: copied {} bytes", bytes_copied);
								}
							} else {
								// Scalar return by value - load pointer and dereference
								FLASH_LOG(Codegen, Debug, "handleReturn: Dereferencing reference at offset ", var_offset);
								X64Register ptr_reg = X64Register::RAX;
								emitMovFromFrame(ptr_reg, var_offset);  // Load the pointer
								// Dereference to get the value
								int value_size_bytes = ref_it->second.value_size_bits / 8;
								emitMovFromMemory(ptr_reg, ptr_reg, 0, value_size_bytes);
								// Value is now in RAX, ready to return
							}
						} else if (ref_it != reference_stack_info_.end() && current_function_returns_reference_) {
							// This is a reference and function returns a reference - return the address itself
							FLASH_LOG(Codegen, Debug, "handleReturn: Returning reference address from offset ", var_offset);
							X64Register ptr_reg = X64Register::RAX;
							emitMovFromFrame(ptr_reg, var_offset);  // Load the pointer (address)
							// Address is now in RAX, ready to return
						} else {
							// Not a reference - normal variable return
							// Get the actual size of the variable being returned
							int var_size = getActualVariableSize(temp_var_name, ret_op.return_size);
							
							// Check if function uses hidden return parameter (RVO/NRVO)
							// Only skip copy if this specific return value is RVO-eligible (was constructed via RVO)
							bool is_rvo_eligible = isTempVarRVOEligible(return_var);
							FLASH_LOG_FORMAT(Codegen, Debug,
								"Return statement check: hidden_param={}, rvo_eligible={}, return_var={}",
								current_function_has_hidden_return_param_, is_rvo_eligible, return_var.name());
							
							if (current_function_has_hidden_return_param_ && is_rvo_eligible) {
								FLASH_LOG_FORMAT(Codegen, Debug,
									"Return statement in function with hidden return parameter - RVO-eligible struct already in return slot at offset {}",
									var_offset);
								// Struct already constructed in return slot via RVO
								// For System V ABI: must return the hidden parameter (return slot address) in RAX
								auto return_slot_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle("__return_slot"));
								if (return_slot_it != variable_scopes.back().variables.end()) {
									int return_slot_param_offset = return_slot_it->second.offset;
									emitMovFromFrame(X64Register::RAX, return_slot_param_offset);
								}
							} else if (current_function_has_hidden_return_param_) {
								// Function uses hidden return param but this value is NOT RVO-eligible
								// Need to copy the struct to the return slot
								FLASH_LOG_FORMAT(Codegen, Debug,
									"Return statement: copying non-RVO struct from offset {} to return slot (var_size={} bits)",
									var_offset, var_size);
								
								// Load return slot address from __return_slot parameter
								auto return_slot_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle("__return_slot"));
								if (return_slot_it != variable_scopes.back().variables.end()) {
									int return_slot_param_offset = return_slot_it->second.offset;
									// Load the address from __return_slot into a register
									X64Register dest_reg = X64Register::RDI;
									emitMovFromFrame(dest_reg, return_slot_param_offset);
									
									FLASH_LOG_FORMAT(Codegen, Debug,
										"Copying struct: size={} bytes, from offset {}, return_slot_param at offset {}",
										var_size / 8, var_offset, return_slot_param_offset);
									
									// Copy struct from var_offset to address in dest_reg
									// Copy in 8-byte chunks, then handle remaining bytes (4, 2, 1)
									int struct_size_bytes = var_size / 8;
									int bytes_copied = 0;
									
									// Copy 8-byte chunks
									while (bytes_copied + 8 <= struct_size_bytes) {
										emitMovFromFrame(X64Register::RAX, var_offset + bytes_copied);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 8);
										bytes_copied += 8;
									}
									
									// Handle remaining bytes (4, 2, 1)
									if (bytes_copied + 4 <= struct_size_bytes) {
										emitMovFromFrameBySize(X64Register::RAX, var_offset + bytes_copied, 32);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 4);
										bytes_copied += 4;
									}
									if (bytes_copied + 2 <= struct_size_bytes) {
										emitMovFromFrameBySize(X64Register::RAX, var_offset + bytes_copied, 16);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 2);
										bytes_copied += 2;
									}
									if (bytes_copied + 1 <= struct_size_bytes) {
										emitMovFromFrameBySize(X64Register::RAX, var_offset + bytes_copied, 8);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 1);
										bytes_copied += 1;
									}
									
									FLASH_LOG_FORMAT(Codegen, Debug,
										"Struct copy complete: copied {} bytes", bytes_copied);
								}
							} else if (is_float_return) {
								// Load floating-point value into XMM0
								bool is_float = (ret_op.return_size == 32);
								emitFloatMovFromFrame(X64Register::XMM0, var_offset, is_float);
							} else if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
								// SystemV AMD64 ABI: check if this is a two-register struct return (9-16 bytes)
								if (ret_op.return_type.has_value() && ret_op.return_type.value() == Type::Struct && 
									var_size > 64 && var_size <= 128) {
									// Two-register struct return: first 8 bytes in RAX, next 8 bytes in RDX
									emitMovFromFrame(X64Register::RAX, var_offset);  // Load low 8 bytes
									emitMovFromFrame(X64Register::RDX, var_offset + 8);  // Load high 8 bytes
									FLASH_LOG_FORMAT(Codegen, Debug,
										"TempVar two-register struct return ({} bits): RAX from offset {}, RDX from offset {}",
										var_size, var_offset, var_offset + 8);
									regAlloc.flushSingleDirtyRegister(X64Register::RAX);
									regAlloc.flushSingleDirtyRegister(X64Register::RDX);
								} else {
									// Single-register return (≤64 bits) in RAX - integer/pointer return
									if (auto reg_var = regAlloc.tryGetStackVariableRegister(var_offset); reg_var.has_value()) {
										if (reg_var.value() != X64Register::RAX) {
											auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, reg_var.value(), ret_op.return_size / 8);
											for (size_t i = 0; i < movResultToRax.size_in_bytes; ++i) {
											}
											logAsmEmit("handleReturn mov to RAX", movResultToRax.op_codes.data(), movResultToRax.size_in_bytes);
											textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
										} else {
										}
									}
									else {
										// Load from stack using RBP-relative addressing
										// Use actual variable size for proper zero/sign extension
										emitMovFromFrameBySize(X64Register::RAX, var_offset, var_size);
										regAlloc.flushSingleDirtyRegister(X64Register::RAX);
									}
								}
							} else {
								// Windows x64 ABI: small structs (≤64 bits) return in RAX only - integer/pointer return
								if (auto reg_var = regAlloc.tryGetStackVariableRegister(var_offset); reg_var.has_value()) {
									if (reg_var.value() != X64Register::RAX) {
										auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, reg_var.value(), ret_op.return_size / 8);
										for (size_t i = 0; i < movResultToRax.size_in_bytes; ++i) {
										}
										logAsmEmit("handleReturn mov to RAX", movResultToRax.op_codes.data(), movResultToRax.size_in_bytes);
										textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
									} else {
									}
								}
								else {
									// Load from stack using RBP-relative addressing
									// Use actual variable size for proper zero/sign extension
									emitMovFromFrameBySize(X64Register::RAX, var_offset, var_size);
									regAlloc.flushSingleDirtyRegister(X64Register::RAX);
								}
							}
						}
					} else {
						// Value not in variables - use fallback offset calculation
						int var_offset = getStackOffsetFromTempVar(return_var);
						
						// Get the actual size of the variable being returned
						int var_size = getActualVariableSize(temp_var_name, ret_op.return_size);
						
						// Check if function uses hidden return parameter (RVO/NRVO)
						// For System V ABI: must return the hidden parameter (return slot address) in RAX
						if (current_function_has_hidden_return_param_) {
							FLASH_LOG(Codegen, Debug,
								"Return statement (fallback): function has hidden return parameter, loading return slot address into RAX");
							auto return_slot_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle("__return_slot"));
							if (return_slot_it != variable_scopes.back().variables.end()) {
								int return_slot_param_offset = return_slot_it->second.offset;
								emitMovFromFrame(X64Register::RAX, return_slot_param_offset);
							}
						} else if (is_float_return) {
							// Load floating-point value into XMM0
							bool is_float = (ret_op.return_size == 32);
							emitFloatMovFromFrame(X64Register::XMM0, var_offset, is_float);
						} else if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
							// SystemV AMD64 ABI: check if this is a two-register struct return (9-16 bytes)
							if (ret_op.return_type.has_value() && ret_op.return_type.value() == Type::Struct && 
								var_size > 64 && var_size <= 128) {
								// Two-register struct return: first 8 bytes in RAX, next 8 bytes in RDX
								emitMovFromFrame(X64Register::RAX, var_offset);  // Load low 8 bytes
								emitMovFromFrame(X64Register::RDX, var_offset + 8);  // Load high 8 bytes
								FLASH_LOG_FORMAT(Codegen, Debug,
									"Fallback two-register struct return ({} bits): RAX from offset {}, RDX from offset {}",
									var_size, var_offset, var_offset + 8);
								regAlloc.flushSingleDirtyRegister(X64Register::RAX);
								regAlloc.flushSingleDirtyRegister(X64Register::RDX);
							} else {
								// Single-register return (≤64 bits) in RAX
								emitMovFromFrameBySize(X64Register::RAX, var_offset, var_size);
								regAlloc.flushSingleDirtyRegister(X64Register::RAX);
							}
						} else {
							// Windows x64 ABI: small structs (≤64 bits) return in RAX only
							emitMovFromFrameBySize(X64Register::RAX, var_offset, var_size);
							regAlloc.flushSingleDirtyRegister(X64Register::RAX);
						}
					}
				}
				else if (std::holds_alternative<StringHandle>(ret_val)) {
					// Handle named variable
					StringHandle var_name_handle = std::get<StringHandle>(ret_val);
					const StackVariableScope& current_scope = variable_scopes.back();
					auto it = current_scope.variables.find(var_name_handle);
					if (it != current_scope.variables.end()) {
						int var_offset = it->second.offset;
						
						// Check if this is a reference variable - if so, dereference it
						// EXCEPT when the function itself returns a reference - in that case, return the address as-is
						// ALSO skip dereferencing if this is 'this' or holds_address_only is set (pointer, not reference)
						auto ref_it = reference_stack_info_.find(var_offset);
						if (ref_it != reference_stack_info_.end() && !ref_it->second.holds_address_only && !current_function_returns_reference_) {
							// This is a reference and function does not return a reference - load pointer and dereference to get value
							FLASH_LOG(Codegen, Debug, "handleReturn: Dereferencing named reference '", StringTable::getStringView(var_name_handle), "' at offset ", var_offset);
							X64Register ptr_reg = X64Register::RAX;
							emitMovFromFrame(ptr_reg, var_offset);  // Load the pointer
							// Dereference to get the value
							int value_size_bytes = ref_it->second.value_size_bits / 8;
							emitMovFromMemory(ptr_reg, ptr_reg, 0, value_size_bytes);
							// Value is now in RAX, ready to return
						} else if (ref_it != reference_stack_info_.end() && !ref_it->second.holds_address_only && current_function_returns_reference_) {
							// This is a reference and function returns a reference - return the address itself
							FLASH_LOG(Codegen, Debug, "handleReturn: Returning named reference address '", StringTable::getStringView(var_name_handle), "' at offset ", var_offset);
							X64Register ptr_reg = X64Register::RAX;
							emitMovFromFrame(ptr_reg, var_offset);  // Load the pointer (address)
							// Address is now in RAX, ready to return
						} else {
							// Not a reference - normal variable return
							// Get the actual size of the variable being returned
							int var_size = getActualVariableSize(var_name_handle, ret_op.return_size);
							
							// Check if return type is float/double
							bool is_float_return = ret_op.return_type.has_value() && 
							                        is_floating_point_type(ret_op.return_type.value());
							
							// Check if function uses hidden return parameter (for struct returns)
							if (current_function_has_hidden_return_param_) {
								// Function uses hidden return param - need to copy struct to return slot
								FLASH_LOG_FORMAT(Codegen, Debug,
									"Return statement (StringHandle): copying struct '{}' from offset {} to return slot (size={} bits)",
									StringTable::getStringView(var_name_handle), var_offset, var_size);
								
								// Load return slot address from __return_slot parameter
								auto return_slot_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle("__return_slot"));
								if (return_slot_it != variable_scopes.back().variables.end()) {
									int return_slot_param_offset = return_slot_it->second.offset;
									// Load the address from __return_slot into a register
									X64Register dest_reg = X64Register::RDI;
									emitMovFromFrame(dest_reg, return_slot_param_offset);
									
									FLASH_LOG_FORMAT(Codegen, Debug,
										"Copying struct: size={} bytes, from offset {}, return_slot_param at offset {}",
										var_size / 8, var_offset, return_slot_param_offset);
									
									// Copy struct from var_offset to address in dest_reg
									// Copy in 8-byte chunks, then handle remaining bytes (4, 2, 1)
									int struct_size_bytes = var_size / 8;
									int bytes_copied = 0;
									
									// Copy 8-byte chunks
									while (bytes_copied + 8 <= struct_size_bytes) {
										emitMovFromFrame(X64Register::RAX, var_offset + bytes_copied);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 8);
										bytes_copied += 8;
									}
									
									// Handle remaining bytes (4, 2, 1)
									if (bytes_copied + 4 <= struct_size_bytes) {
										emitMovFromFrameBySize(X64Register::RAX, var_offset + bytes_copied, 32);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 4);
										bytes_copied += 4;
									}
									if (bytes_copied + 2 <= struct_size_bytes) {
										emitMovFromFrameBySize(X64Register::RAX, var_offset + bytes_copied, 16);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 2);
										bytes_copied += 2;
									}
									if (bytes_copied + 1 <= struct_size_bytes) {
										emitMovFromFrameBySize(X64Register::RAX, var_offset + bytes_copied, 8);
										emitStoreToMemory(textSectionData, X64Register::RAX, dest_reg, bytes_copied, 1);
										bytes_copied += 1;
									}
									
									FLASH_LOG_FORMAT(Codegen, Debug,
										"Struct copy complete: copied {} bytes", bytes_copied);
								}
							} else if (is_float_return) {
								// Load floating-point value into XMM0
								bool is_float = (ret_op.return_size == 32);
								emitFloatMovFromFrame(X64Register::XMM0, var_offset, is_float);
							} else {
								// Load integer/pointer value into RAX
								// Use actual variable size for proper zero/sign extension
								emitMovFromFrameBySize(X64Register::RAX, var_offset, var_size);
								regAlloc.flushSingleDirtyRegister(X64Register::RAX);
							}
						}
					}
				}
				else if (std::holds_alternative<double>(ret_val)) {
					// Floating point return in XMM0
					double returnValue = std::get<double>(ret_val);
					
					// Determine if this is float or double based on return_size
					bool is_float = (ret_op.return_size == 32);
					
					// We need a temporary location on the stack to load from
					// Use the shadow space / spill area at the end of the frame
					// This is safe because we're about to return
					int literal_offset = -8; // Use first slot in shadow space
					
					// Store the literal bits to the stack via RAX
					uint64_t bits;
					if (is_float) {
						float float_val = static_cast<float>(returnValue);
						std::memcpy(&bits, &float_val, sizeof(float));
					} else {
						std::memcpy(&bits, &returnValue, sizeof(double));
					}
					
					// mov rax, immediate64
					textSectionData.push_back(0x48);
					textSectionData.push_back(0xB8);
					for (int i = 0; i < 8; ++i) {
						textSectionData.push_back(static_cast<uint8_t>(bits & 0xFF));
						bits >>= 8;
					}
					
					// mov [rbp + offset], rax (store to stack - 64-bit)
					emitMovToFrameSized(
						SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
						SizedStackSlot{literal_offset, 64, false}  // dest: 64-bit for float bits
					);
					
					// Load from stack to XMM0
					// movss/movsd xmm0, [rbp + offset]
					emitFloatMovFromFrame(X64Register::XMM0, literal_offset, is_float);
				}
			}
		}

		if constexpr (!std::is_same_v<TWriterClass, ElfFileWriter>) {
			if (g_enable_exceptions && in_catch_funclet_) {
				bool has_float_return = false;
				bool has_return_value = false;
				if (instruction.hasTypedPayload()) {
					const auto& catch_return_op = instruction.getTypedPayload<ReturnOp>();
					has_return_value = catch_return_op.return_value.has_value();
					has_float_return = catch_return_op.return_type.has_value() &&
						is_floating_point_type(catch_return_op.return_type.value());
				}
				int32_t catch_return_slot = 0;
				if (!has_float_return && has_return_value) {
					catch_return_slot = ensureCatchFuncletReturnSlot();
					emitMovToFrame(X64Register::RAX, catch_return_slot, 64);
				}

				flushAllDirtyRegisters();

				StringBuilder return_trampoline_sb;
				return_trampoline_sb.append("__catch_return_trampoline_").append(static_cast<uint64_t>(catch_funclet_return_label_counter_++));
				StringHandle return_trampoline_handle = StringTable::getOrInternStringHandle(return_trampoline_sb.commit());

				textSectionData.push_back(0x48);
				textSectionData.push_back(0x8D);
				textSectionData.push_back(0x05);
				uint32_t lea_patch = static_cast<uint32_t>(textSectionData.size());
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				pending_branches_.push_back({return_trampoline_handle, lea_patch});

				emitAddRSP(32);
				emitPopReg(X64Register::RBP);
				textSectionData.push_back(0xC3);

				uint32_t catch_funclet_end_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
				if (current_catch_handler_) {
					current_catch_handler_->handler_end_offset = catch_funclet_end_offset;
					current_catch_handler_->funclet_end_offset = catch_funclet_end_offset;
				}

				label_positions_[return_trampoline_handle] = static_cast<uint32_t>(textSectionData.size());

				// After _JumpToContinuation: RSP = establisher = S-8-N (correct frame level)
				// RBP is corrupted by CRT. Restore it via LEA RBP, [RSP + N].
				catch_continuation_sub_rsp_patches_.push_back(static_cast<uint32_t>(textSectionData.size()) + 4);
				textSectionData.push_back(0x48); // REX.W
				textSectionData.push_back(0x8D); // LEA
				textSectionData.push_back(0xAC); // ModR/M: mod=10, reg=101(RBP), r/m=100(SIB)
				textSectionData.push_back(0x24); // SIB: base=RSP, index=none
				textSectionData.push_back(0x00); // disp32 placeholder (patched with total_stack)
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);

				if (catch_return_slot != 0) {
					emitMovFromFrame(X64Register::RAX, catch_return_slot);
				}

				// Standard epilogue: mov rsp, rbp; pop rbp; ret
				textSectionData.push_back(0x48);
				textSectionData.push_back(0x89);
				textSectionData.push_back(0xEC);
				textSectionData.push_back(0x5D);
				textSectionData.push_back(0xC3);

				catch_funclet_terminated_by_return_ = true;

				in_catch_funclet_ = false;
				return;
			}
		}

		// MSVC-style epilogue
		//int32_t total_stack_space = variable_scopes.back().scope_stack_space;

		// Always generate epilogue since we always generate prologue
		// mov rsp, rbp (restore stack pointer)
		textSectionData.push_back(0x48);
		textSectionData.push_back(0x89);
		textSectionData.push_back(0xEC);

		// pop rbp (restore caller's base pointer)
		textSectionData.push_back(0x5D);

		// Track CFI: Wrap epilogue in remember/restore state to handle early returns.
		// Without this, the POP_RBP CFI would affect subsequent code in the function
		// (e.g., throw statements after an if-return), making the unwinder think the
		// frame is gone when it's still active.
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// Save CFI state before epilogue
			current_function_cfi_.push_back({
				ElfFileWriter::CFIInstruction::REMEMBER_STATE,
				static_cast<uint32_t>(textSectionData.size() - current_function_offset_ - 4), // before mov rsp,rbp
				0
			});
			// After pop rbp, CFA = RSP+8 (back to call site state)
			current_function_cfi_.push_back({
				ElfFileWriter::CFIInstruction::POP_RBP,
				static_cast<uint32_t>(textSectionData.size() - current_function_offset_),
				0
			});
		}

		// ret (return to caller)
		textSectionData.push_back(0xC3);

		// Track CFI: Restore state after ret so subsequent code has correct frame info
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			current_function_cfi_.push_back({
				ElfFileWriter::CFIInstruction::RESTORE_STATE,
				static_cast<uint32_t>(textSectionData.size() - current_function_offset_),
				0
			});
		}

		// NOTE: We do NOT pop variable_scopes here because there may be multiple
		// return statements in a function (e.g., early returns in if statements).
		// The scope will be popped when we finish processing the entire function.
	}

	void handleStackAlloc([[maybe_unused]] const IrInstruction& instruction) {
		// StackAlloc is not used in the current implementation
		// Variables are allocated in handleVariableDecl instead
		// Just return without doing anything
		return;

		// Get the size of the allocation
		/*auto sizeInBytes = instruction.getOperandAs<int>(1) / 8;

		// Ensure the stack remains aligned to 16 bytes
		sizeInBytes = (sizeInBytes + 15) & -16;

		// Generate the opcode for `sub rsp, imm32`
		std::array<uint8_t, 7> subRspInst = { 0x48, 0x81, 0xEC };
		std::memcpy(subRspInst.data() + 3, &sizeInBytes, sizeof(sizeInBytes));

		// Add the instruction to the .text section
		textSectionData.insert(textSectionData.end(), subRspInst.begin(), subRspInst.end());

		// Add the identifier and its stack offset to the current scope
		// With RBP-relative addressing, local variables use NEGATIVE offsets
		StackVariableScope& current_scope = variable_scopes.back();
		current_scope.current_stack_offset -= sizeInBytes;  // Move to next slot (going more negative)
		int stack_offset = current_scope.current_stack_offset;
		current_scope.variables[instruction.getOperandAs<std::string_view>(2)].offset = stack_offset;*/
	}

