	void handleMemberAccess(const IrInstruction& instruction) {
		// MemberAccess: %result = member_access [MemberType][MemberSize] %object, member_name, offset
		
		// Extract typed payload - all MemberAccess instructions use typed payloads
		const MemberLoadOp& op = std::any_cast<const MemberLoadOp&>(instruction.getTypedPayload());

		// Get the object's base stack offset or pointer
		int32_t object_base_offset = 0;
		bool is_pointer_access = false;  // true if object is 'this' or a reference parameter (both are pointers)
		bool is_global_access = false;   // true if object is a global variable
		StringHandle global_object_name;  // name for global variable access
		const StackVariableScope& current_scope = variable_scopes.back();

		// Get object base offset
		if (std::holds_alternative<StringHandle>(op.object)) {
			StringHandle object_name_handle = std::get<StringHandle>(op.object);
			auto it = current_scope.variables.find(object_name_handle);
			if (it == current_scope.variables.end()) {
				// Not found in local scope - check if it's a global variable
				bool found_global = false;
				for (const auto& global : global_variables_) {
					if (global.name == object_name_handle) {
						found_global = true;
						is_global_access = true;
						global_object_name = global.name;
						break;
					}
				}
				if (!found_global) {
					FLASH_LOG(Codegen, Error, "MemberAccess missing object: ", StringTable::getStringView(object_name_handle), "\n");
					throw std::runtime_error("Struct object not found in scope or globals");
					return;
				}
			} else {
				object_base_offset = it->second.offset;

				// Check if this is the 'this' pointer or a reference parameter (both need dereferencing)
				bool is_this = (StringTable::getStringView(object_name_handle) == "this"sv);
				bool in_ref_stack_info = (reference_stack_info_.count(object_base_offset) > 0);
				FLASH_LOG(Codegen, Debug, "MemberAccess check: object='", StringTable::getStringView(object_name_handle),
				          "' offset=", object_base_offset,
				          " is_this=", is_this,
				          " in_ref_stack_info=", in_ref_stack_info,
				          " is_pointer_to_member=", op.is_pointer_to_member);
				if (is_this || in_ref_stack_info || op.is_pointer_to_member) {
					is_pointer_access = true;
				}
			}
		} else {
			// Nested case: object is the result of a previous member access
			auto object_temp = std::get<TempVar>(op.object);
			object_base_offset = getStackOffsetFromTempVar(object_temp);
			
			// Check if this temp var holds a pointer/address (from large member access) or is pointer-to-member
			if (reference_stack_info_.count(object_base_offset) > 0 || op.is_pointer_to_member) {
				is_pointer_access = true;
			}
		}

		// Calculate the member's actual stack offset
		int32_t member_stack_offset;
		if (is_pointer_access) {
			member_stack_offset = 0;  // Not used for pointer access
		} else {
			// For a struct at [RBP - 8] with member at offset 4: member is at [RBP - 8 + 4] = [RBP - 4]
			member_stack_offset = object_base_offset + op.offset;
		}

		// Calculate member size in bytes
		int member_size_bytes = op.result.size_in_bits / 8;
		bool unresolved_user_defined_member = (member_size_bytes == 0 &&
			op.result.type == Type::UserDefined &&
			op.result.type_index == 0);

		// Flush all dirty registers to ensure values are saved before allocating
		flushAllDirtyRegisters();

		// Get the result variable's stack offset (needed for both paths)
		auto result_var = std::get<TempVar>(op.result.value);
		int32_t result_offset;
		StringHandle result_var_handle = StringTable::getOrInternStringHandle(result_var.name());
		auto it = current_scope.variables.find(result_var_handle);
		if (it != current_scope.variables.end() && it->second.offset != INT_MIN) {
			result_offset = it->second.offset;
		} else {
			// Allocate stack space for the result TempVar (or if offset is sentinel INT_MIN)
			result_offset = allocateStackSlotForTempVar(result_var.var_number);
			// Note: allocateStackSlotForTempVar already updates the variables map
		}

		// For large members (> 8 bytes), we can't load the value into a register
		// Instead, we compute and store the ADDRESS for later nested member access
		if (member_size_bytes > 8) {
			// Allocate a register to compute the address
			X64Register addr_reg = allocateRegisterWithSpilling();
			
			if (is_global_access) {
				// LEA addr_reg, [RIP + global_name]
				// REX.W + 8D /r for LEA r64, m
				uint8_t rex = 0x48; // REX.W
				if (static_cast<uint8_t>(addr_reg) >= 8) {
					rex |= 0x04; // REX.R
				}
				textSectionData.push_back(rex);
				textSectionData.push_back(0x8D);  // LEA opcode
				
				// ModR/M: mod=00 (RIP-relative), reg=addr_reg, r/m=101 (RIP+disp32)
				uint8_t modrm = 0x05 | ((static_cast<uint8_t>(addr_reg) & 0x7) << 3);
				textSectionData.push_back(modrm);
				
				// Placeholder for relocation (disp32)
				uint32_t reloc_offset = static_cast<uint32_t>(textSectionData.size());
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				textSectionData.push_back(0x00);
				pending_global_relocations_.push_back({reloc_offset, global_object_name, IMAGE_REL_AMD64_REL32});
				
				// If offset != 0, add it to addr_reg
				if (op.offset != 0) {
					emitAddRegImm32(textSectionData, addr_reg, op.offset);
				}
			} else if (is_pointer_access) {
				// Load pointer into addr_reg, then add offset if needed
				auto load_ptr = generatePtrMovFromFrame(addr_reg, object_base_offset);
				textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(),
				                       load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
				if (op.offset != 0) {
					emitAddRegImm32(textSectionData, addr_reg, op.offset);
				}
			} else {
				// LEA addr_reg, [RBP + member_stack_offset]
				int32_t effective_offset = object_base_offset + op.offset;
				auto lea_opcodes = generateLeaFromFrame(addr_reg, effective_offset);
				textSectionData.insert(textSectionData.end(), lea_opcodes.op_codes.begin(),
				                       lea_opcodes.op_codes.begin() + lea_opcodes.size_in_bytes);
			}
			
			// Store the address to result_offset
			auto store_addr = generatePtrMovToFrame(addr_reg, result_offset);
			textSectionData.insert(textSectionData.end(), store_addr.op_codes.begin(),
			                       store_addr.op_codes.begin() + store_addr.size_in_bytes);
			regAlloc.release(addr_reg);
			
			// Mark this temp var as containing a pointer/address
			setReferenceInfo(result_offset, op.result.type, op.result.size_in_bits, false, result_var);
			return;
		}

		// Allocate a register for loading the member value
		X64Register temp_reg = allocateRegisterWithSpilling();

		if (is_global_access) {
			// LEA temp_reg, [RIP + global] with relocation
			uint32_t reloc_offset = emitLeaRipRelative(temp_reg);
			pending_global_relocations_.push_back({reloc_offset, global_object_name, IMAGE_REL_AMD64_REL32});
			
			// Load member from [temp_reg + offset]
			bool is_float_type = (op.result.type == Type::Float || op.result.type == Type::Double);
			
			if (is_float_type) {
				// For floating-point: load into XMM and store to stack
				X64Register xmm_reg = X64Register::XMM0;
				bool is_float = (op.result.type == Type::Float);
				emitFloatLoadFromAddressWithOffset(textSectionData, xmm_reg, temp_reg, op.offset, is_float);
				
				int32_t float_result_offset = allocateStackSlotForTempVar(result_var.var_number);
				auto store_opcodes = generateFloatMovToFrame(xmm_reg, float_result_offset, is_float);
				textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
				                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
				regAlloc.release(temp_reg);
				variable_scopes.back().variables[StringTable::getOrInternStringHandle(result_var.name())].offset = float_result_offset;
				return;
			} else {
				// For integers: use standard integer load
				OpCodeWithSize load_opcodes;
				if (member_size_bytes == 8) {
					load_opcodes = generateMovFromMemory(temp_reg, temp_reg, op.offset);
				} else if (member_size_bytes == 4) {
					load_opcodes = generateMovFromMemory32(temp_reg, temp_reg, op.offset);
				} else if (member_size_bytes == 2) {
					load_opcodes = generateMovFromMemory16(temp_reg, temp_reg, op.offset);
				} else if (member_size_bytes == 1) {
					load_opcodes = generateMovFromMemory8(temp_reg, temp_reg, op.offset);
				} else {
					// Unsupported member size (0, 3, 5, 6, 7, etc.) - skip quietly
					if (unresolved_user_defined_member) {
						regAlloc.release(temp_reg);
						return;
					}
					FLASH_LOG_FORMAT(Codegen, Warning,
						"MemberAccess: Unsupported member size {} bytes for '{}' (type={}, ptr_depth={}, type_index={}), skipping",
						member_size_bytes, StringTable::getStringView(op.member_name), static_cast<int>(op.result.type),
						op.result.pointer_depth, op.result.type_index);
					regAlloc.release(temp_reg);
					return;
				}
				textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
				                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				
				// Extract bitfield value if this is a bitfield member
				if (op.bitfield_width.has_value()) {
					size_t bit_offset = op.bitfield_bit_offset;
					size_t width = *op.bitfield_width;
					if (bit_offset > 0) {
						emitShrImm(temp_reg, static_cast<uint8_t>(bit_offset));
					}
					uint64_t mask = bitfieldMask(width);
					emitAndImm64(temp_reg, mask);
				}

				// Store loaded value to result_offset for later use (e.g., indirect_call)
				emitMovToFrame(temp_reg, result_offset, member_size_bytes * 8);
				regAlloc.release(temp_reg);
				variable_scopes.back().variables[result_var_handle].offset = result_offset;
				return;
			}
		} else if (is_pointer_access) {
			// Load pointer into an allocated register, then load from [ptr_reg + offset]
			FLASH_LOG_FORMAT(Codegen, Debug,
				"MemberAccess pointer path: object_base_offset={}, op.offset={}, member_size_bytes={}",
				object_base_offset, op.offset, member_size_bytes);
			X64Register ptr_reg = allocateRegisterWithSpilling();
			emitMovFromFrame(ptr_reg, object_base_offset);
			
			// Load from [ptr_reg + offset] into temp_reg
			OpCodeWithSize load_opcodes;
			if (member_size_bytes == 8) {
				load_opcodes = generateMovFromMemory(temp_reg, ptr_reg, op.offset);
			} else if (member_size_bytes == 4) {
				load_opcodes = generateMovFromMemory32(temp_reg, ptr_reg, op.offset);
			} else if (member_size_bytes == 2) {
				load_opcodes = generateMovFromMemory16(temp_reg, ptr_reg, op.offset);
			} else if (member_size_bytes == 1) {
				load_opcodes = generateMovFromMemory8(temp_reg, ptr_reg, op.offset);
			} else {
				// Unsupported member size (0, 3, 5, 6, 7, etc.) - skip quietly
				if (unresolved_user_defined_member) {
					regAlloc.release(temp_reg);
					regAlloc.release(ptr_reg);
					return;
				}
				FLASH_LOG_FORMAT(Codegen, Warning,
					"MemberAccess pointer path: Unsupported member size {} bytes for '{}' (type={}, ptr_depth={}, type_index={}), skipping",
					member_size_bytes, StringTable::getStringView(op.member_name), static_cast<int>(op.result.type),
					op.result.pointer_depth, op.result.type_index);
				regAlloc.release(temp_reg);
				regAlloc.release(ptr_reg);
				return;
			}
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
			
			// Release pointer register - no longer needed
			regAlloc.release(ptr_reg);
			
			// Extract bitfield value if this is a bitfield member
			if (op.bitfield_width.has_value()) {
				size_t bit_offset = op.bitfield_bit_offset;
				size_t width = *op.bitfield_width;
				if (bit_offset > 0) {
					emitShrImm(temp_reg, static_cast<uint8_t>(bit_offset));
				}
				uint64_t mask = bitfieldMask(width);
				emitAndImm64(temp_reg, mask);
			}

			// Store loaded value to result_offset for later use (e.g., indirect_call)
			emitMovToFrame(temp_reg, result_offset, member_size_bytes * 8);
			regAlloc.release(temp_reg);
			variable_scopes.back().variables[result_var_handle].offset = result_offset;
			return;
		} else {
			// For regular struct variables on the stack, load from computed offset
			emitLoadFromFrame(textSectionData, temp_reg, member_stack_offset, member_size_bytes);
		}

		// Extract bitfield value if this is a bitfield member
		if (op.bitfield_width.has_value()) {
			size_t bit_offset = op.bitfield_bit_offset;
			size_t width = *op.bitfield_width;
			if (bit_offset > 0) {
				// SHR temp_reg, bit_offset
				emitShrImm(temp_reg, static_cast<uint8_t>(bit_offset));
			}
			// AND temp_reg, (1 << width) - 1
			uint64_t mask = bitfieldMask(width);
			emitAndImm64(temp_reg, mask);
		}

		if (op.is_reference) {
			emitMovToFrame(temp_reg, result_offset, 64);
			regAlloc.release(temp_reg);
			setReferenceInfo(result_offset, op.result.type, op.result.size_in_bits, op.is_rvalue_reference, result_var);
			return;
		}

		// Store the loaded value into the temp slot so subsequent uses read the value,
		// avoiding aliasing the TempVar to the struct member location.
		emitMovToFrame(temp_reg, result_offset, member_size_bytes * 8);
		regAlloc.release(temp_reg);
		variable_scopes.back().variables[result_var_handle].offset = result_offset;
		return;
	}

	void handleMemberStore(const IrInstruction& instruction) {
		// MemberStore: member_store [MemberType][MemberSize] %object, member_name, offset, %value
		
		// Extract typed payload - all MemberStore instructions use typed payloads
		const MemberStoreOp& op = std::any_cast<const MemberStoreOp&>(instruction.getTypedPayload());

		// Check if this is a vtable pointer initialization (vptr)
		if (op.vtable_symbol.isValid()) {
			// This is a vptr initialization - load vtable address and store to offset 0
			// Get the object's base stack offset
			int32_t object_base_offset = 0;
			const StackVariableScope& current_scope = variable_scopes.back();
			
			if (std::holds_alternative<StringHandle>(op.object)) {
				StringHandle object_name_handle = std::get<StringHandle>(op.object);
				auto it = current_scope.variables.find(object_name_handle);
				if (it == current_scope.variables.end()) {
					throw std::runtime_error("Struct object not found in scope");
					return;
				}
				object_base_offset = it->second.offset;
			}
			
			// Load vtable address using LEA with relocation
			// The vtable symbol (_ZTV...) already points to the function pointer array
			// (the ElfFileWriter's add_vtable creates the symbol at offset +16 past the RTTI header)
			// So we just need a standard PC-relative relocation with the default addend
			uint32_t relocation_offset = emitLeaRipRelative(X64Register::RAX);
			
			// Add a relocation for the vtable symbol
			writer.add_relocation(relocation_offset, StringTable::getStringView(op.vtable_symbol));
			
			// Store vtable pointer to [RCX + 0] (this pointer is in RCX, vptr is at offset 0)
			// First load 'this' pointer into RCX
			emitMovFromFrame(X64Register::RCX, object_base_offset);
			
			// Store RAX (vtable address) to [RCX + 0]
			emitStoreToMemory(textSectionData, X64Register::RAX, X64Register::RCX, 0, 8);
			
			return;  // Done with vptr initialization
		}

		// Now process the MemberStoreOp
		// Get the value - it could be a TempVar, a literal (unsigned long long, double), or a string_view (variable name)
		bool is_literal = false;
		int64_t literal_value = 0;
		double literal_double_value = 0.0;
		bool is_double_literal = false;
		bool is_variable = false;
		StringHandle variable_name;

		if (std::holds_alternative<TempVar>(op.value.value)) {
			// TempVar - handled below
		} else if (std::holds_alternative<unsigned long long>(op.value.value)) {
			is_literal = true;
			literal_value = static_cast<int64_t>(std::get<unsigned long long>(op.value.value));
		} else if (std::holds_alternative<double>(op.value.value)) {
			is_literal = true;
			is_double_literal = true;
			literal_double_value = std::get<double>(op.value.value);
		} else if (std::holds_alternative<StringHandle>(op.value.value)) {
			is_variable = true;
			variable_name = std::get<StringHandle>(op.value.value);
		} else {
			throw std::runtime_error("Value must be TempVar, unsigned long long, double, or StringHandle");
			return;
		}

		// Get the object's base stack offset or pointer
		int32_t object_base_offset = 0;
		bool is_pointer_access = false;  // true if object is 'this' (a pointer)
		const StackVariableScope& current_scope = variable_scopes.back();

		if (std::holds_alternative<StringHandle>(op.object)) {
			StringHandle object_name_handle = std::get<StringHandle>(op.object);
			
			// First check if this is a global variable
			bool is_global_variable = false;
			for (const auto& global : global_variables_) {
				if (global.name == object_name_handle) {
					is_global_variable = true;
					break;
				}
			}
			
			if (is_global_variable) {
				// Handle global struct member assignment using RIP-relative addressing
				// Load the value into a register first
				X64Register value_reg = allocateRegisterWithSpilling();
				
				if (is_literal) {
					if (is_double_literal) {
						uint64_t bits;
						std::memcpy(&bits, &literal_double_value, sizeof(bits));
						emitMovImm64(value_reg, bits);
					} else {
						uint64_t imm64 = static_cast<uint64_t>(literal_value);
						emitMovImm64(value_reg, imm64);
					}
				} else if (is_variable) {
					auto it = current_scope.variables.find(variable_name);
					if (it == current_scope.variables.end()) {
						throw std::runtime_error("Variable not found in scope");
						return;
					}
					int32_t value_offset = it->second.offset;
					emitMovFromFrameBySize(value_reg, value_offset, op.value.size_in_bits);
				} else {
					auto value_var = std::get<TempVar>(op.value.value);
					int32_t value_offset = getStackOffsetFromTempVar(value_var);
					emitMovFromFrameBySize(value_reg, value_offset, op.value.size_in_bits);
				}
				
				// Now store to the global struct member using RIP-relative addressing with offset
				// For doubles: MOVSD [RIP + disp32 + offset], XMM
				// For integers: MOV [RIP + disp32 + offset], reg
				bool is_floating_point = (op.value.type == Type::Float || op.value.type == Type::Double);
				bool is_float = (op.value.type == Type::Float);
				
				if (is_floating_point) {
					// Move to XMM register for floating-point stores
					X64Register xmm_reg = X64Register::XMM0;
					// MOVQ XMM0, value_reg (reinterpret bits)
					emitMovqGprToXmm(value_reg, xmm_reg);
					
					// MOVSD/MOVSS [RIP + disp32], XMM0
					textSectionData.push_back(is_float ? 0xF3 : 0xF2);
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0x11);
					uint8_t xmm_bits = static_cast<uint8_t>(xmm_reg) & 0x07;
					textSectionData.push_back(0x05 | (xmm_bits << 3));
					
					// Placeholder for displacement - will be patched by relocation
					uint32_t reloc_offset = static_cast<uint32_t>(textSectionData.size());
					// The actual displacement should account for the member offset
					int32_t disp_with_offset = op.offset;
					textSectionData.push_back((disp_with_offset >> 0) & 0xFF);
					textSectionData.push_back((disp_with_offset >> 8) & 0xFF);
					textSectionData.push_back((disp_with_offset >> 16) & 0xFF);
					textSectionData.push_back((disp_with_offset >> 24) & 0xFF);
					
					// Add relocation for the global variable (with offset already included in displacement)
					pending_global_relocations_.push_back({reloc_offset, object_name_handle, IMAGE_REL_AMD64_REL32, op.offset - 4});
				} else {
					// Integer store
					int member_size_bytes = op.value.size_in_bits / 8;
					FLASH_LOG_FORMAT(Codegen, Debug, "MemberStore global: size_in_bits={}, member_size_bytes={}", op.value.size_in_bits, member_size_bytes);
					assert(member_size_bytes > 0 && "Global bitfield RMW: op.value.size_in_bits must be storage unit size (>= 8 bits), not bitfield width");
					if (op.bitfield_width.has_value()) {
						// Bitfield global write: read-modify-write via register-based addressing
						size_t width = *op.bitfield_width;
						size_t bit_offset = op.bitfield_bit_offset;
						uint64_t mask = bitfieldMask(width);

						// LEA addr_reg, [RIP + global]
						X64Register addr_reg = allocateRegisterWithSpilling();
						uint32_t reloc_offset_lea = emitLeaRipRelative(addr_reg);
						pending_global_relocations_.push_back({reloc_offset_lea, object_name_handle, IMAGE_REL_AMD64_REL32});

						// Load existing storage unit from [addr_reg + op.offset] into temp_reg
						X64Register temp_reg = allocateRegisterWithSpilling();
						emitMovFromMemory(temp_reg, addr_reg, op.offset, member_size_bytes);

						// Clear the bitfield bits in temp_reg
						emitAndImm64(temp_reg, ~(mask << bit_offset));

						// Shift value into position and mask it
						if (bit_offset > 0) {
							emitShlImm(value_reg, static_cast<uint8_t>(bit_offset));
						}
						emitAndImm64(value_reg, mask << bit_offset);

						// OR value into storage unit
						emitOrReg(temp_reg, value_reg);

						// Store back to [addr_reg + op.offset]
						emitStoreToMemory(textSectionData, temp_reg, addr_reg, op.offset, member_size_bytes);

						regAlloc.release(temp_reg);
						regAlloc.release(addr_reg);
					} else {
						// Non-bitfield integer store: MOV [RIP + disp32], reg
						int size_in_bits = op.value.size_in_bits;
						uint8_t src_val = static_cast<uint8_t>(value_reg);
						uint8_t src_bits = src_val & 0x07;
						uint8_t needs_rex_w = (size_in_bits == 64) ? 0x08 : 0x00;
						uint8_t needs_rex_b = (src_val >> 3) & 0x01;
						uint8_t rex = 0x40 | needs_rex_w | needs_rex_b;
						uint8_t emit_rex = (needs_rex_w | needs_rex_b) != 0;
						
						if (emit_rex) {
							textSectionData.push_back(rex);
						}
						
						if (size_in_bits == 8) {
							textSectionData.push_back(0x88); // MOV r/m8, r8
						} else {
							textSectionData.push_back(0x89); // MOV r/m32/r/m64, r32/r64
						}
						
						textSectionData.push_back(0x05 | (src_bits << 3));
						
						// Placeholder for displacement with member offset
						uint32_t reloc_offset = static_cast<uint32_t>(textSectionData.size());
						int32_t disp_with_offset = op.offset;
						textSectionData.push_back((disp_with_offset >> 0) & 0xFF);
						textSectionData.push_back((disp_with_offset >> 8) & 0xFF);
						textSectionData.push_back((disp_with_offset >> 16) & 0xFF);
						textSectionData.push_back((disp_with_offset >> 24) & 0xFF);
						
						// Add relocation
						pending_global_relocations_.push_back({reloc_offset, object_name_handle, IMAGE_REL_AMD64_REL32, op.offset - 4});
					}
				}
				
				regAlloc.release(value_reg);
				return;  // Done with global member store
			}
			
			// Not a global - look in local scope
			auto it = current_scope.variables.find(object_name_handle);
			if (it == current_scope.variables.end()) {
				throw std::runtime_error("Struct object not found in scope");
				return;
			}
			object_base_offset = it->second.offset;

			// Check if this is the 'this' pointer or a reference parameter or pointer-to-member access
			if (StringTable::getStringView(object_name_handle) == "this"sv || 
			    reference_stack_info_.count(object_base_offset) > 0 ||
			    op.is_pointer_to_member) {
				is_pointer_access = true;
			}
		} else {
			// Nested case: object is the result of a previous member access
			auto object_temp = std::get<TempVar>(op.object);
			object_base_offset = getStackOffsetFromTempVar(object_temp);
			
			// Check if this temp var holds a pointer/address (from large member access) or is pointer-to-member
			if (reference_stack_info_.count(object_base_offset) > 0 || op.is_pointer_to_member) {
				is_pointer_access = true;
			}
		}

		// Calculate the member's actual stack offset
		int32_t member_stack_offset;
		if (is_pointer_access) {
			member_stack_offset = 0;  // Not used for pointer access
		} else {
			member_stack_offset = object_base_offset + op.offset;
		}

		// Calculate member size in bytes
		int member_size_bytes = op.value.size_in_bits / 8;

		// Load the value into a register - allocate through register allocator to avoid conflicts
		X64Register value_reg = allocateRegisterWithSpilling();

		if (op.is_reference) {
			// value_reg already allocated above
			bool pointer_loaded = false;
			if (is_variable) {
				// Check if this variable is itself a reference (e.g., reference parameter)
				auto it = current_scope.variables.find(variable_name);
				if (it != current_scope.variables.end()) {
					int32_t var_offset = it->second.offset;
					// Check if this stack variable is a reference
					auto ref_it = reference_stack_info_.find(var_offset);
					if (ref_it != reference_stack_info_.end()) {
						// This variable is a reference - it already holds a pointer
						// MOV the pointer value, don't take its address
						emitMovFromFrame(value_reg, var_offset);
					} else {
						// This variable is not a reference - take its address
						emitLeaFromFrame(value_reg, var_offset);
					}
					pointer_loaded = true;
				}
			} else if (!is_literal) {
				// TempVar - load its value (which is already a pointer from addressof)
				auto value_var = std::get<TempVar>(op.value.value);
				int32_t value_offset = getStackOffsetFromTempVar(value_var);
				// The TempVar contains an address, so we MOV (load value) not LEA (load address of)
				emitMovFromFrame(value_reg, value_offset);
				pointer_loaded = true;
			}
			if (!pointer_loaded) {
				if (is_literal && literal_value == 0) {
					moveImmediateToRegister(value_reg, 0);
					pointer_loaded = true;
				}
			}
			if (!pointer_loaded) {
				FLASH_LOG(Codegen, Error, "Reference member initializer must be an lvalue");
				throw std::runtime_error("Reference member initializer must be an lvalue");
			}
		} else if (is_literal) {
			if (is_double_literal) {
				uint64_t bits;
				std::memcpy(&bits, &literal_double_value, sizeof(bits));
				emitMovImm64(value_reg, bits);
			} else {
				uint64_t imm64 = static_cast<uint64_t>(literal_value);
				emitMovImm64(value_reg, imm64);
			}
		} else if (is_variable) {
			// Check if this is a vtable symbol (check vtable_symbol field in MemberStoreOp)
			// This will be handled separately below
			auto it = current_scope.variables.find(variable_name);
			if (it == current_scope.variables.end()) {
				throw std::runtime_error("Variable not found in scope");
				return;
			}
			int32_t value_offset = it->second.offset;
			// If pointer_depth > 0, we need to store the address of the variable (LEA)
			// not the value at that address (MOV). This is used for initializer_list
			// backing arrays where we need to store &array[0], not array[0].
			if (op.value.pointer_depth > 0) {
				emitLeaFromFrame(value_reg, value_offset);
			} else {
				emitMovFromFrameBySize(value_reg, value_offset, op.value.size_in_bits);
			}
		} else {
			auto value_var = std::get<TempVar>(op.value.value);
			int32_t value_offset = getStackOffsetFromTempVar(value_var);
			auto existing_reg = regAlloc.findRegisterForStackOffset(value_offset);
			if (existing_reg.has_value()) {
				value_reg = existing_reg.value();
			} else {
				emitMovFromFrameBySize(value_reg, value_offset, op.value.size_in_bits);
			}
		}

		// Store the value to the member's location
		if (op.bitfield_width.has_value()) {
			// Bitfield store: read-modify-write to preserve other bitfields in the storage unit
			size_t width = *op.bitfield_width;
			size_t bit_offset = op.bitfield_bit_offset;
			uint64_t mask = bitfieldMask(width);

			// Allocate a temp register for read-modify-write
			X64Register temp_reg = allocateRegisterWithSpilling();

			if (is_pointer_access) {
				X64Register base_reg = allocateRegisterWithSpilling();
				auto load_ptr_opcodes = generatePtrMovFromFrame(base_reg, object_base_offset);
				textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
				                       load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);

				// Load existing storage unit from [base_reg + op.offset] into temp_reg
				emitMovFromMemory(temp_reg, base_reg, op.offset, member_size_bytes);

				// Clear the bitfield bits: AND temp_reg, ~(mask << bit_offset)
				uint64_t clear_mask = ~(mask << bit_offset);
				emitAndImm64(temp_reg, clear_mask);

				// Shift value into position: SHL value_reg, bit_offset
				if (bit_offset > 0) {
					emitShlImm(value_reg, static_cast<uint8_t>(bit_offset));
				}
				// Mask value to width: AND value_reg, (mask << bit_offset)
				emitAndImm64(value_reg, mask << bit_offset);

				// OR value into storage unit: OR temp_reg, value_reg
				emitOrReg(temp_reg, value_reg);

				// Store back to [base_reg + op.offset]
				emitStoreToMemory(textSectionData, temp_reg, base_reg, op.offset, member_size_bytes);

				regAlloc.release(base_reg);
			} else {
				// Load existing storage unit from [RBP + member_stack_offset] into temp_reg
				emitMovFromFrameBySize(temp_reg, member_stack_offset, member_size_bytes * 8);

				// Clear the bitfield bits: AND temp_reg, ~(mask << bit_offset)
				uint64_t clear_mask = ~(mask << bit_offset);
				emitAndImm64(temp_reg, clear_mask);

				// Shift value into position: SHL value_reg, bit_offset
				if (bit_offset > 0) {
					emitShlImm(value_reg, static_cast<uint8_t>(bit_offset));
				}
				// Mask value to width: AND value_reg, (mask << bit_offset)
				emitAndImm64(value_reg, mask << bit_offset);

				// OR value into storage unit: OR temp_reg, value_reg
				emitOrReg(temp_reg, value_reg);

				// Store back to [RBP + member_stack_offset]
				emitStoreToMemory(textSectionData, temp_reg, X64Register::RBP, member_stack_offset, member_size_bytes);
			}

			regAlloc.release(temp_reg);
		} else if (is_pointer_access) {
			// For 'this' pointer or reference: load pointer into base_reg, then store to [base_reg + offset]
			// IMPORTANT: Allocate a register for the base pointer to avoid clobbering value_reg
			X64Register base_reg = allocateRegisterWithSpilling();
			auto load_ptr_opcodes = generatePtrMovFromFrame(base_reg, object_base_offset);
			textSectionData.insert(textSectionData.end(), load_ptr_opcodes.op_codes.begin(),
			                       load_ptr_opcodes.op_codes.begin() + load_ptr_opcodes.size_in_bytes);
			
			// Store value_reg to [base_reg + op.offset] using helper function
			emitStoreToMemory(textSectionData, value_reg, base_reg, op.offset, member_size_bytes);
			
			// Release the base register
			regAlloc.release(base_reg);
		} else {
			// For regular struct variables on the stack: store to [RBP + member_stack_offset]
			emitStoreToMemory(textSectionData, value_reg, X64Register::RBP, member_stack_offset, member_size_bytes);
		}

		// Release value_reg - we allocated it above
		regAlloc.release(value_reg);
	}

	void handleAddressOf(const IrInstruction& instruction) {
		// Address-of: &x
		// Check for typed payload
		if (instruction.hasTypedPayload()) {
			const auto& op = instruction.getTypedPayload<AddressOfOp>();
			
			int32_t var_offset;
			const StackVariableScope& current_scope = variable_scopes.back();
			// Use register allocator instead of directly using RAX to avoid clobbering dirty registers
			X64Register target_reg = allocateRegisterWithSpilling();
			bool is_global = false;
			StringHandle global_name_handle;
			
			// Get operand (variable to take address of) from TypedValue
			if (std::holds_alternative<TempVar>(op.operand.value)) {
				// Taking address of a temporary variable (e.g., for rvalue references)
				TempVar temp = std::get<TempVar>(op.operand.value);
				var_offset = getStackOffsetFromTempVar(temp);
			} else {
				// Taking address of a named variable
				std::string_view operand_str;
				if (std::holds_alternative<StringHandle>(op.operand.value)) {
					operand_str = StringTable::getStringView(std::get<StringHandle>(op.operand.value));
					global_name_handle = std::get<StringHandle>(op.operand.value);
				} else {
					assert(std::holds_alternative<TempVar>(op.operand.value) && "AddressOf operand must be StringHandle or TempVar");
					return;
				}

				// First, check if this is a global/static local variable
				is_global = isGlobalVariable(global_name_handle);
				
				if (!is_global) {
					auto it = current_scope.variables.find(StringTable::getOrInternStringHandle(operand_str));
					if (it == current_scope.variables.end()) {
						// Special case: This might be taking address of a class member (e.g., &Point::x)
						// which is only valid for pointer-to-member types
						// For now, stub this out - full implementation would need to generate
						// a pointer-to-member constant value
						FLASH_LOG(Codegen, Debug, "AddressOf operand '", operand_str, "' not found in scope - might be pointer-to-member, stubbing with zero");
						// Store zero as a placeholder for pointer-to-member
						emitMovImm64(target_reg, 0);
						return;
					}
					var_offset = it->second.offset;
				}
			}

			// Calculate the address
			if (is_global) {
				// Global/static local variable - use LEA with RIP-relative addressing
				uint32_t reloc_offset = emitLeaRipRelative(target_reg);
				pending_global_relocations_.push_back({reloc_offset, global_name_handle, IMAGE_REL_AMD64_REL32});
			} else {
				// If the variable is a reference, it already holds an address - use MOV to load it
				// Otherwise, use LEA to compute the address of the variable
				auto ref_it = reference_stack_info_.find(var_offset);
				if (ref_it != reference_stack_info_.end()) {
					// Variable is a reference - load the address it contains
					emitMovFromFrame(target_reg, var_offset);
				} else {
					// Regular variable - compute its address
					emitLeaFromFrame(target_reg, var_offset);
				}
			}

			// Store the address to result_var (pointer is always 64-bit)
			int32_t result_offset = getStackOffsetFromTempVar(op.result);
			emitMovToFrameSized(
				SizedRegister{target_reg, 64, false},  // source: 64-bit register
				SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
			);
			
			// NOTE: The result of addressof is a POINTER value, not a reference.
			// However, we mark it in reference_stack_info_ so that subsequent operations
			// know this TempVar holds a pointer and should be loaded with MOV, not LEA.
			// This is needed for proper handling when passing AddressOf results to functions.
			reference_stack_info_[result_offset] = ReferenceInfo{
				.value_type = op.operand.type,
				.value_size_bits = op.operand.size_in_bits,
				.is_rvalue_reference = false,  // AddressOf result is a pointer, not a reference
				.holds_address_only = true
			};
			
			// Release the register since the address has been stored to memory
			regAlloc.release(target_reg);
			
			return;
		}
		
		// Legacy format: Operands: [result_var, type, size, operand]
		assert(instruction.getOperandCount() == 4 && "AddressOf must have 4 operands");

		int32_t var_offset = 0;
		// Use register allocator instead of directly using RAX to avoid clobbering dirty registers
		X64Register target_reg = allocateRegisterWithSpilling();
		bool is_global = false;
		StringHandle global_name_handle;
		
		// Get operand (variable to take address of) - can be string_view, string, or TempVar
		if (instruction.isOperandType<TempVar>(3)) {
			// Taking address of a temporary variable (e.g., for rvalue references)
			TempVar temp = instruction.getOperandAs<TempVar>(3);
			var_offset = getStackOffsetFromTempVar(temp);
		} else {
			// Taking address of a named variable
			assert(instruction.isOperandType<StringHandle>(3) && "AddressOf operand must be string_view, string, or TempVar");
			global_name_handle = instruction.getOperandAs<StringHandle>(3);
			
			// First, check if this is a global/static local variable
			is_global = isGlobalVariable(global_name_handle);
			
			if (!is_global) {
				const StackVariableScope& current_scope = variable_scopes.back();
				auto it = current_scope.variables.find(global_name_handle);
				if (it == current_scope.variables.end()) {
					throw std::runtime_error("Variable not found in scope");
					return;
				}
				var_offset = it->second.offset;
			}
		}

		// Calculate the address
		if (is_global) {
			// Global/static local variable - use LEA with RIP-relative addressing
			uint32_t reloc_offset = emitLeaRipRelative(target_reg);
			pending_global_relocations_.push_back({reloc_offset, global_name_handle, IMAGE_REL_AMD64_REL32});
		} else {
			// Regular local variable - LEA RAX, [RBP + offset]
			emitLeaFromFrame(target_reg, var_offset);
		}
		
		// Store the address to result_var (pointer is always 64-bit)
		auto result_var = instruction.getOperandAs<TempVar>(0);
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		emitMovToFrameSized(
			SizedRegister{target_reg, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
		);
		
		// Release the register since the address has been stored to memory
		regAlloc.release(target_reg);
	}
	
	void handleAddressOfMember(const IrInstruction& instruction) {
		// AddressOfMember: &obj.member
		// Calculate address of struct member directly: LEA result, [RBP + obj_offset + member_offset]
		
		const AddressOfMemberOp& op = std::any_cast<const AddressOfMemberOp&>(instruction.getTypedPayload());
		
		// Look up the base object's stack offset
		const StackVariableScope& current_scope = variable_scopes.back();
		auto it = current_scope.variables.find(op.base_object);
		if (it == current_scope.variables.end()) {
			throw std::runtime_error("Base object not found in scope for AddressOfMember");
			return;
		}
		
		int32_t obj_offset = it->second.offset;
		int32_t combined_offset = obj_offset + op.member_offset;
		
		// Calculate the address: LEA target_reg, [RBP + combined_offset]
		// Use register allocator to avoid clobbering dirty registers
		X64Register target_reg = allocateRegisterWithSpilling();
		emitLeaFromFrame(target_reg, combined_offset);
		
		// Store the address to result_var (pointer is always 64-bit)
		int32_t result_offset = getStackOffsetFromTempVar(op.result);
		emitMovToFrameSized(
			SizedRegister{target_reg, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
		);
		
		// Release the register since the address has been stored to memory
		regAlloc.release(target_reg);
		
		// DO NOT mark as reference - this is a plain pointer value for use in arithmetic
	}

	void handleComputeAddress(const IrInstruction& instruction) {
		// ComputeAddress: One-pass address calculation for complex expressions
		// Handles: &arr[i].member1.member2, &arr[i][j], &arr[i].inner_arr[j].member
		// Algorithm: address = base + (index1 * elem_size1) + (index2 * elem_size2) + ... + member_offset
		
		const ComputeAddressOp& op = std::any_cast<const ComputeAddressOp&>(instruction.getTypedPayload());
		
		// Step 1: Load base address into RAX
		int64_t base_offset = 0;
		bool base_is_reference = false;
		bool base_is_pointer = false;  // For 'this' and other pointers
		if (std::holds_alternative<StringHandle>(op.base)) {
			// Variable name - look up its stack offset
			StringHandle base_name = std::get<StringHandle>(op.base);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(base_name);
			if (it == current_scope.variables.end()) {
				throw std::runtime_error("Base variable not found in scope for ComputeAddress");
				return;
			}
			base_offset = it->second.offset;
			
			// Check if base is 'this' - it's a pointer, so we need to load its value
			// instead of computing the address of the 'this' variable
			std::string_view base_name_str = StringTable::getStringView(base_name);
			if (base_name_str == "this") {
				base_is_pointer = true;
			}
			
			// Check if base is a reference - if so, we need to load the address it contains
			// instead of computing the address of the variable itself
			auto ref_it = reference_stack_info_.find(static_cast<int32_t>(base_offset));
			base_is_reference = (ref_it != reference_stack_info_.end());
		} else {
			// TempVar - get its stack offset
			TempVar base_temp = std::get<TempVar>(op.base);
			base_offset = getStackOffsetFromTempVar(base_temp);
			
			// Check if TempVar is a reference
			auto ref_it = reference_stack_info_.find(static_cast<int32_t>(base_offset));
			base_is_reference = (ref_it != reference_stack_info_.end());
		}
		
		if (base_is_reference || base_is_pointer) {
			// Base is a reference or pointer - load the address it contains (MOV, not LEA)
			emitMovFromFrame(X64Register::RAX, base_offset);
		} else {
			// Base is a regular variable - compute its address (LEA)
			emitLeaFromFrame(X64Register::RAX, base_offset);
		}
		
		// Step 2: Process each array index
		for (const auto& arr_idx : op.array_indices) {
			int element_size_bytes = arr_idx.element_size_bits / 8;
			
			// Load index into RCX
			if (std::holds_alternative<unsigned long long>(arr_idx.index)) {
				// Constant index
				uint64_t index_value = std::get<unsigned long long>(arr_idx.index);
				int64_t offset = index_value * element_size_bytes;
				
				// Add constant offset to RAX
				if (offset != 0) {
					emitAddImmToReg(textSectionData, X64Register::RAX, offset);
				}
			} else if (std::holds_alternative<TempVar>(arr_idx.index)) {
				// Variable index from temp
				TempVar index_var = std::get<TempVar>(arr_idx.index);
				int64_t index_offset = getStackOffsetFromTempVar(index_var);
				
				// Load index into RCX with proper size and sign extension
				bool is_signed = isSignedType(arr_idx.index_type);
				emitMovFromFrameSized(
					SizedRegister{X64Register::RCX, 64, false},
					SizedStackSlot{static_cast<int32_t>(index_offset), arr_idx.index_size_bits, is_signed}
				);
				
				// Multiply RCX by element size
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);
				
				// Add RCX to RAX
				emitAddRAXRCX(textSectionData);
			} else if (std::holds_alternative<StringHandle>(arr_idx.index)) {
				// Variable index from variable name
				StringHandle index_var_name = std::get<StringHandle>(arr_idx.index);
				const StackVariableScope& current_scope = variable_scopes.back();
				auto it = current_scope.variables.find(index_var_name);
				if (it == current_scope.variables.end()) {
					throw std::runtime_error("Index variable not found in scope");
					return;
				}
				int64_t index_offset = it->second.offset;
				
				// Load index into RCX with proper size and sign extension
				bool is_signed = isSignedType(arr_idx.index_type);
				emitMovFromFrameSized(
					SizedRegister{X64Register::RCX, 64, false},
					SizedStackSlot{static_cast<int32_t>(index_offset), arr_idx.index_size_bits, is_signed}
				);
				
				// Multiply RCX by element size
				emitMultiplyRCXByElementSize(textSectionData, element_size_bytes);
				
				// Add RCX to RAX
				emitAddRAXRCX(textSectionData);
			}
		}
		
		// Step 3: Add accumulated member offset (if any)
		if (op.total_member_offset > 0) {
			emitAddImmToReg(textSectionData, X64Register::RAX, op.total_member_offset);
		}
		
		// Step 4: Store result
		int32_t result_offset = getStackOffsetFromTempVar(op.result);
		emitMovToFrameSized(
			SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}     // dest: 64-bit for pointer
		);
	}

	void handleDereference(const IrInstruction& instruction) {
		// Dereference: *ptr
		// Check for typed payload
		if (instruction.hasTypedPayload()) {
			const auto& op = instruction.getTypedPayload<DereferenceOp>();
			
			// THE FIX: Use pointer_depth to determine the correct dereference size
			// If pointer_depth > 1, we're dereferencing a multi-level pointer (e.g., int*** -> int**)
			// and the result is still a pointer (64 bits).
			// If pointer_depth == 1, we're dereferencing to the final value (use pointer.size_in_bits).
			int value_size;
			if (op.pointer.pointer_depth > 1) {
				value_size = 64;  // Result is still a pointer
			} else {
				// Final dereference - use the pointee size (stored in size_in_bits of the pointer's type)
				value_size = op.pointer.size_in_bits;
			}
			
			// Load the pointer into a register
			X64Register ptr_reg;
			const StackVariableScope& current_scope = variable_scopes.back();
			
			if (std::holds_alternative<TempVar>(op.pointer.value)) {
				TempVar temp = std::get<TempVar>(op.pointer.value);
				int32_t temp_offset = getStackOffsetFromTempVar(temp);
				
				// Check if the TempVar is already in a register (e.g., from a previous operation)
				if (auto reg_opt = regAlloc.tryGetStackVariableRegister(temp_offset); reg_opt.has_value()) {
					ptr_reg = reg_opt.value();
				} else {
					// Not in a register, load from stack
					ptr_reg = allocateRegisterWithSpilling();
					emitMovFromFrame(ptr_reg, temp_offset);
				}
			} else {
				StringHandle var_name_handle = std::get<StringHandle>(op.pointer.value);
				auto it = current_scope.variables.find(var_name_handle);
				if (it == current_scope.variables.end()) {
					throw std::runtime_error("Pointer variable not found");
					return;
				}
				
				// Check if the variable is already in a register
				if (auto reg_opt = regAlloc.tryGetStackVariableRegister(it->second.offset); reg_opt.has_value()) {
					ptr_reg = reg_opt.value();
				} else {
					ptr_reg = allocateRegisterWithSpilling();
					emitMovFromFrame(ptr_reg, it->second.offset);
				}
			}

			// Check if we're dereferencing a float/double type - use XMM register and MOVSD/MOVSS
			bool is_float_type = (op.pointer.type == Type::Float || op.pointer.type == Type::Double);
			
			if (is_float_type && op.pointer.pointer_depth <= 1) {
				// Only use float instructions for final dereference
				// Use XMM0 as the destination register for float loads
				X64Register xmm_reg = X64Register::XMM0;
				bool is_float = (op.pointer.type == Type::Float);
				
				// Load float/double from memory into XMM register
				emitFloatMovFromMemory(xmm_reg, ptr_reg, 0, is_float);
				
				// Store the XMM value to the result location
				int32_t result_offset = getStackOffsetFromTempVar(op.result);
				emitFloatMovToFrame(xmm_reg, result_offset, is_float);
				return;
			}

			// Handle struct types (values > 64 bits) by doing a multi-step memory copy
			if (value_size > 64 && op.pointer.pointer_depth <= 1) {
				int32_t result_offset = getStackOffsetFromTempVar(op.result);
				int struct_size_bytes = (value_size + 7) / 8;
				
				// Copy from [ptr_reg] to [rbp + result_offset] in 8-byte chunks
				for (int offset = 0; offset < struct_size_bytes; ) {
					if (offset + 8 <= struct_size_bytes) {
						// Copy 8 bytes
						X64Register temp_reg = allocateRegisterWithSpilling();
						emitMovFromMemory(temp_reg, ptr_reg, offset, 8);
						emitMovToFrameSized(
							SizedRegister{temp_reg, 64, false},
							SizedStackSlot{result_offset + offset, 64, false}
						);
						regAlloc.release(temp_reg);
						offset += 8;
					} else if (offset + 4 <= struct_size_bytes) {
						// Copy 4 bytes
						X64Register temp_reg = allocateRegisterWithSpilling();
						emitMovFromMemory(temp_reg, ptr_reg, offset, 4);
						emitMovToFrameSized(
							SizedRegister{temp_reg, 32, false},
							SizedStackSlot{result_offset + offset, 32, false}
						);
						regAlloc.release(temp_reg);
						offset += 4;
					} else if (offset + 2 <= struct_size_bytes) {
						// Copy 2 bytes
						X64Register temp_reg = allocateRegisterWithSpilling();
						emitMovFromMemory(temp_reg, ptr_reg, offset, 2);
						emitMovToFrameSized(
							SizedRegister{temp_reg, 16, false},
							SizedStackSlot{result_offset + offset, 16, false}
						);
						regAlloc.release(temp_reg);
						offset += 2;
					} else {
						// Copy 1 byte
						X64Register temp_reg = allocateRegisterWithSpilling();
						emitMovFromMemory(temp_reg, ptr_reg, offset, 1);
						emitMovToFrameSized(
							SizedRegister{temp_reg, 8, false},
							SizedStackSlot{result_offset + offset, 8, false}
						);
						regAlloc.release(temp_reg);
						offset += 1;
					}
				}
				return;
			}

			// Track which register holds the dereferenced value (may differ from ptr_reg for MOVZX)
			X64Register value_reg = ptr_reg;

			// Use emit helper function to generate dereference instruction
			// This handles all sizes (8, 16, 32, 64-bit) and special cases (RBP/R13, RSP/R12)
			if (value_size == 8) {
				// For 8-bit, MOVZX always uses RAX as destination
				value_reg = X64Register::RAX;
			}
			
			emitMovRegFromMemRegSized(value_reg, ptr_reg, value_size);

			// Store the dereferenced value to result_var
			int32_t result_offset = getStackOffsetFromTempVar(op.result);
			auto result_store = generateMovToFrameBySize(value_reg, result_offset, value_size);
			textSectionData.insert(textSectionData.end(), result_store.op_codes.begin(),
			                       result_store.op_codes.begin() + result_store.size_in_bytes);
			
			// CRITICAL FIX: Clear the register association with the old pointer offset
			// After dereferencing, value_reg no longer holds the pointer - it holds the dereferenced value
			// However, we need to be careful: if value_reg was reused from ptr_reg and ptr_reg had
			// an old association with a different offset, we must clear that to prevent corruption.
			// But we don't want to release the register completely, as it might be needed.
			// So we only clear the stack variable offset if it doesn't match the result offset.
			for (auto& reg_info : regAlloc.registers) {
				if (reg_info.reg == value_reg && reg_info.stackVariableOffset != result_offset) {
					reg_info.stackVariableOffset = INT_MIN;
					reg_info.isDirty = false;
				}
			}
			return;
		}
		
		// Legacy format: Operands: [result_var, type, size, operand]
		assert(instruction.getOperandCount() == 4 && "Dereference must have 4 operands");

		[[maybe_unused]] Type value_type = instruction.getOperandAs<Type>(1);
		int value_size = instruction.getOperandAs<int>(2);

		// Load the pointer operand into a register
		X64Register ptr_reg = loadOperandIntoRegister(instruction, 3);

		// Track which register holds the dereferenced value (may differ from ptr_reg for MOVZX)
		X64Register value_reg = ptr_reg;

		// Use emit helper function to generate dereference instruction
		// This handles all sizes (8, 16, 32, 64-bit) and special cases (RBP/R13, RSP/R12)
		if (value_size == 8) {
			// For 8-bit, MOVZX always uses RAX as destination
			value_reg = X64Register::RAX;
		}
		
		emitMovRegFromMemRegSized(value_reg, ptr_reg, value_size);

		// Store the dereferenced value to result_var
		auto result_var = instruction.getOperandAs<TempVar>(0);
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		auto result_store = generateMovToFrameBySize(value_reg, result_offset, value_size);
		textSectionData.insert(textSectionData.end(), result_store.op_codes.begin(),
		                       result_store.op_codes.begin() + result_store.size_in_bytes);
		
		// CRITICAL FIX: Clear the register association with the old pointer offset
		// After dereferencing, value_reg no longer holds the pointer - it holds the dereferenced value
		// However, we need to be careful: if value_reg was reused from ptr_reg and ptr_reg had
		// an old association with a different offset, we must clear that to prevent corruption.
		// But we don't want to release the register completely, as it might be needed.
		// So we only clear the stack variable offset if it doesn't match the result offset.
		for (auto& reg_info : regAlloc.registers) {
			if (reg_info.reg == value_reg && reg_info.stackVariableOffset != result_offset) {
				reg_info.stackVariableOffset = INT_MIN;
				reg_info.isDirty = false;
			}
		}
	}

	void handleDereferenceStore(const IrInstruction& instruction) {
		// DereferenceStore: *ptr = value
		// Store a value through a pointer
		assert(instruction.hasTypedPayload() && "DereferenceStore instruction must use typed payload");
		const auto& op = instruction.getTypedPayload<DereferenceStoreOp>();
		
		// Flush all dirty registers before loading values from stack
		// This ensures that any values computed in previous instructions (like ADD) 
		// are written to their stack locations before we try to load them
		flushAllDirtyRegisters();
		
		int value_size = op.value.size_in_bits;
		int value_size_bytes = value_size / 8;
		
		// Allocate registers through the register allocator to avoid conflicts
		X64Register ptr_reg = allocateRegisterWithSpilling();
		const StackVariableScope& current_scope = variable_scopes.back();
		
		if (std::holds_alternative<TempVar>(op.pointer.value)) {
			TempVar temp = std::get<TempVar>(op.pointer.value);
			int32_t temp_offset = getStackOffsetFromTempVar(temp);
			emitMovFromFrame(ptr_reg, temp_offset);
		} else {
			StringHandle var_name_handle = std::get<StringHandle>(op.pointer.value);
			auto it = current_scope.variables.find(var_name_handle);
			if (it == current_scope.variables.end()) {
				throw std::runtime_error("Pointer variable not found in DereferenceStore");
				return;
			}
			emitMovFromFrame(ptr_reg, it->second.offset);
		}
		
		// Allocate a second register for the value - must be different from ptr_reg
		X64Register value_reg = allocateRegisterWithSpilling();

		if (std::holds_alternative<unsigned long long>(op.value.value)) {
			uint64_t imm_value = std::get<unsigned long long>(op.value.value);
			emitMovImm64(value_reg, imm_value);
		} else if (std::holds_alternative<double>(op.value.value)) {
			double double_value = std::get<double>(op.value.value);
			uint64_t bits;
			std::memcpy(&bits, &double_value, sizeof(double));
			emitMovImm64(value_reg, bits);
		} else if (std::holds_alternative<TempVar>(op.value.value)) {
			TempVar value_temp = std::get<TempVar>(op.value.value);
			int32_t value_offset = getStackOffsetFromTempVar(value_temp);
			emitMovFromFrameSized(
				SizedRegister{value_reg, static_cast<uint8_t>(value_size), isSignedType(op.value.type)},
				SizedStackSlot{value_offset, value_size, isSignedType(op.value.type)}
			);
		} else if (std::holds_alternative<StringHandle>(op.value.value)) {
			StringHandle var_name_handle = std::get<StringHandle>(op.value.value);
			auto it = current_scope.variables.find(var_name_handle);
			if (it != current_scope.variables.end()) {
				emitMovFromFrameSized(
					SizedRegister{value_reg, static_cast<uint8_t>(value_size), isSignedType(op.value.type)},
					SizedStackSlot{static_cast<int32_t>(it->second.offset), value_size, isSignedType(op.value.type)}
				);
			}
		}
		
		// Store value_reg to [ptr_reg] with appropriate size
		emitStoreToMemory(textSectionData, value_reg, ptr_reg, 0, value_size_bytes);
	}

	void handleConditionalBranch(const IrInstruction& instruction) {
		// Conditional branch: test condition, jump if false to else_label, fall through to then_label
		assert(instruction.hasTypedPayload() && "ConditionalBranch instruction must use typed payload");
		const auto& cond_branch_op = instruction.getTypedPayload<CondBranchOp>();
		IrValue condition_value = cond_branch_op.condition.value;
		auto then_label = cond_branch_op.getLabelTrue();
		auto else_label = cond_branch_op.getLabelFalse();
		// Flush all dirty registers before branching
		flushAllDirtyRegisters();

		// Load condition value into a register
		X64Register condition_reg = X64Register::RAX;

		if (std::holds_alternative<TempVar>(condition_value)) {
			auto temp_var = std::get<TempVar>(condition_value);
			int var_offset = getStackOffsetFromTempVar(temp_var);
			
			// Look up the actual size of this temp var (default to 32 if not found)
			int load_size = 32;
			auto size_it = temp_var_sizes_.find(StringTable::getOrInternStringHandle(temp_var.name()));
			if (size_it != temp_var_sizes_.end()) {
				load_size = size_it->second;
			}
			
			// For narrow conditions (bool8/16/32), always reload into RAX using size-aware MOV
			// to canonicalize upper bits before TEST.
			if (load_size < 64) {
				emitMovFromFrameBySize(X64Register::RAX, var_offset, load_size);
				condition_reg = X64Register::RAX;
			} else {
				// Check if temp var is already in a register
				if (auto reg = regAlloc.tryGetStackVariableRegister(var_offset); reg.has_value()) {
					condition_reg = reg.value();
				} else {
					// Load from memory with correct size
					emitMovFromFrameBySize(X64Register::RAX, var_offset, load_size);
					condition_reg = X64Register::RAX;
				}
			}
		} else if (std::holds_alternative<StringHandle>(condition_value)) {
			StringHandle var_name = std::get<StringHandle>(condition_value);

			// Search from innermost to outermost scope so branch conditions can reference
			// parameters/locals declared in parent scopes.
			const VariableInfo* var_info = findVariableInfo(var_name);

			if (var_info) {
				// Use the size stored in the variable info, default to 32 if 0 (shouldn't happen)
				int load_size = var_info->size_in_bits > 0 ? var_info->size_in_bits : 32;
				
				// For narrow conditions (bool8/16/32), always reload into RAX using size-aware MOV
				// to canonicalize upper bits before TEST.
				if (load_size < 64) {
					emitMovFromFrameBySize(X64Register::RAX, var_info->offset, load_size);
					condition_reg = X64Register::RAX;
				} else {
					// Check if variable is already in a register
					if (auto reg = regAlloc.tryGetStackVariableRegister(var_info->offset); reg.has_value()) {
						condition_reg = reg.value();
					} else {
						emitMovFromFrameBySize(X64Register::RAX, var_info->offset, load_size);
						condition_reg = X64Register::RAX;
					}
				}
			}
		} else if (std::holds_alternative<unsigned long long>(condition_value)) {
			// Immediate value
			unsigned long long value = std::get<unsigned long long>(condition_value);

			// MOV RAX, imm64
			emitMovImm64(X64Register::RAX, value);
			condition_reg = X64Register::RAX;
		}

		// Test if condition is non-zero: TEST reg, reg
		emitTestRegReg(condition_reg);

		// Check if then_label is a backward reference (already defined)
		// This happens in do-while loops where we jump back to the start when true
		bool then_is_backward = label_positions_.find(then_label) != label_positions_.end();
		
		if (then_is_backward) {
			// For do-while: then_label is backward (jump to loop start), else_label is forward (fall through to end)
			// Use JNZ (jump if not zero) to then_label, fall through to else_label
			textSectionData.push_back(0x0F); // Two-byte opcode prefix
			textSectionData.push_back(0x85); // JNZ/JNE rel32

			uint32_t then_patch_position = static_cast<uint32_t>(textSectionData.size());
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);

			pending_branches_.push_back({then_label, then_patch_position});
			// Fall through to else block (loop end)
		} else {
			// For while/if: then_label is forward (fall through to body), else_label is forward (jump to end)
			// Use JZ (jump if zero) to else_label, fall through to then_label
			textSectionData.push_back(0x0F); // Two-byte opcode prefix
			textSectionData.push_back(0x84); // JZ/JE rel32

			uint32_t else_patch_position = static_cast<uint32_t>(textSectionData.size());
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);
			textSectionData.push_back(0x00);

			pending_branches_.push_back({else_label, else_patch_position});
			// Fall through to then block
		}
	}
	
	void handleFunctionAddress(const IrInstruction& instruction) {
		// Extract typed payload - all FunctionAddress instructions use typed payloads
		const FunctionAddressOp& op = std::any_cast<const FunctionAddressOp&>(instruction.getTypedPayload());

		flushAllDirtyRegisters();

		auto result_var = std::get<TempVar>(op.result.value);

		// Get result offset
		int result_offset = getStackOffsetFromTempVar(result_var);

		// Load the address of the function into RAX using RIP-relative addressing
		// LEA RAX, [RIP + function_name]  (position-independent code, uses REL32 relocation)
		textSectionData.push_back(0x48); // REX.W
		textSectionData.push_back(0x8D); // LEA
		textSectionData.push_back(0x05); // ModR/M: RAX, [RIP + disp32]

		// Add placeholder for the displacement (will be filled by relocation)
		uint32_t reloc_position = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Add REL32 relocation for the function address (RIP-relative)
		// All FunctionAddress instructions should now have the mangled name pre-computed
		// Phase 4: Use helper
		std::string_view mangled = StringTable::getStringView(op.getMangledName());
		assert(!mangled.empty() && "FunctionAddress instruction missing mangled_name");
		writer.add_relocation(reloc_position, mangled, IMAGE_REL_AMD64_REL32);

		// Store RAX to result variable
		auto store_opcodes = generatePtrMovToFrame(X64Register::RAX, result_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

		regAlloc.reset();
	}

	void handleIndirectCall(const IrInstruction& instruction) {
		// IndirectCall: Call through function pointer
		auto& op = instruction.getTypedPayload<IndirectCallOp>();

		flushAllDirtyRegisters();

		// Get result offset
		int result_offset = getStackOffsetFromTempVar(op.result);
		variable_scopes.back().variables[StringTable::getOrInternStringHandle(op.result.name())].offset = result_offset;

		// Load function pointer into RAX
		X64Register func_ptr_reg;
		if (std::holds_alternative<TempVar>(op.function_pointer)) {
			TempVar func_ptr_temp = std::get<TempVar>(op.function_pointer);
			int func_ptr_offset = getStackOffsetFromTempVar(func_ptr_temp);
			func_ptr_reg = X64Register::RAX;
			emitMovFromFrame(func_ptr_reg, func_ptr_offset);
		} else {
			// Function pointer is a variable name
			StringHandle var_name_handle = std::get<StringHandle>(op.function_pointer);
			int func_ptr_offset = variable_scopes.back().variables[var_name_handle].offset;
			func_ptr_reg = X64Register::RAX;
			emitMovFromFrame(func_ptr_reg, func_ptr_offset);
		}
		// Process arguments (if any)
		for (size_t i = 0; i < op.arguments.size() && i < 4; ++i) {
			const auto& arg = op.arguments[i];
			Type argType = arg.type;

			// Determine if this is a floating-point argument
			bool is_float_arg = is_floating_point_type(argType);

			// Determine the target register for the argument
			X64Register target_reg = is_float_arg ? getFloatParamReg<TWriterClass>(i) : getIntParamReg<TWriterClass>(i);

			// Load argument into target register
			if (std::holds_alternative<TempVar>(arg.value)) {
				const TempVar temp_var = std::get<TempVar>(arg.value);
				int arg_offset = getStackOffsetFromTempVar(temp_var);
				if (is_float_arg) {
					bool is_float = (argType == Type::Float);
					emitFloatMovFromFrame(target_reg, arg_offset, is_float);
				} else {
					// Use size-aware load: source (sized stack slot) -> dest (64-bit register)
					emitMovFromFrameSized(
						SizedRegister{target_reg, 64, false},  // dest: 64-bit register
						SizedStackSlot{arg_offset, arg.size_in_bits, isSignedType(argType)}  // source: sized stack slot
					);
				}
			} else if (std::holds_alternative<StringHandle>(arg.value)) {
				StringHandle arg_var_name_handle = std::get<StringHandle>(arg.value);
				int arg_offset = variable_scopes.back().variables[arg_var_name_handle].offset;
				if (is_float_arg) {
					bool is_float = (argType == Type::Float);
					auto load_opcodes = generateFloatMovFromFrame(target_reg, arg_offset, is_float);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
					                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
				} else {
					// Use size-aware load: source (sized stack slot) -> dest (64-bit register)
					emitMovFromFrameSized(
						SizedRegister{target_reg, 64, false},  // dest: 64-bit register
						SizedStackSlot{arg_offset, arg.size_in_bits, isSignedType(argType)}  // source: sized stack slot
					);
				}
			} else if (std::holds_alternative<unsigned long long>(arg.value)) {
				// Immediate value
				unsigned long long value = std::get<unsigned long long>(arg.value);
				emitMovImm64(target_reg, value);
			}
		}

		// Call through function pointer in RAX
		// CALL RAX
		textSectionData.push_back(0xFF); // CALL r/m64
		textSectionData.push_back(0xD0); // ModR/M: RAX

		// Store return value from RAX to result variable
		auto store_opcodes = generatePtrMovToFrame(X64Register::RAX, result_offset);
		textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
		                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

		regAlloc.reset();
	}

	// ============================================================================
	// Exception Handling Implementation
	// ============================================================================
	// Implementation status:
	// [X] Exceptions are thrown via _CxxThrowException (proper MSVC C++ runtime call)
	// [X] SEH frames exist via PDATA/XDATA sections with __CxxFrameHandler3 reference
	// [X] Stack unwinding works via unwind codes in XDATA
	// [X] FuncInfo structures generated with try-block maps and catch handlers
	// [X] Catch blocks execute for thrown exceptions
	// [X] Type-specific exception matching with type descriptors
	//
	// What works:
	// - throw statement properly calls _CxxThrowException with exception object
	// - throw; (rethrow) properly calls _CxxThrowException with NULL
	// - Stack unwinding occurs correctly during exception propagation
	// - Programs terminate properly for uncaught exceptions
	// - Try/catch blocks with catch handlers execute when exceptions are thrown
	// - catch(...) catches all exception types
	// - Type descriptors (??_R0) generated for caught exception types
	// - Type-specific catch blocks match based on exception type
	// - catch by const (catch(const int&)) supported via adjectives field
	// - catch by lvalue reference (catch(int&)) supported
	// - catch by rvalue reference (catch(int&&)) supported
	// - Destructor unwinding infrastructure: UnwindMap entries can track local objects with destructors
	//
	// Current implementation:
	// - Type descriptors created in .rdata for each unique exception type
	// - HandlerType pType field points to appropriate type descriptor
	// - MSVC name mangling used for built-in types (int, char, double, etc.)
	// - Simple mangling for class/struct types (V<name>@@)
	// - Adjectives field set for const/reference catch clauses
	//   - 0x01 = const
	//   - 0x08 = lvalue reference (&)
	//   - 0x10 = rvalue reference (&&)
	// - State-based exception handling through tryLow/tryHigh/catchHigh state numbers
	//   - __CxxFrameHandler3 uses states to determine active try blocks
	// - UnwindMap data structure generation in XDATA
	//   - Infrastructure in place for tracking local objects with destructors
	//   - UnwindMapEntry: toState (next state) + action (destructor RVA)
	//
	// Limitations:
	// - Automatic destructor calls not yet connected (need parser/codegen to track object lifetimes)
	// - Template type mangling is simplified (not full MSVC encoding)
	//
	// For full C++ exception semantics, the following enhancements could be added:
	// - Automatic tracking of object construction/destruction in parser/codegen
	// - Connection of destructor calls to unwind map entries
	// - Full MSVC template type mangling with argument encoding
	// ============================================================================
	

#include "IRConverter_Emit_EHSeh.h"

	void finalizeSections() {
		// Emit global variables to .data or .bss sections FIRST
		// This creates the symbols that relocations will reference
		for (const auto& global : global_variables_) {
			writer.add_global_variable_data(StringTable::getStringView(global.name), global.size_in_bytes, 
			                                global.is_initialized, global.init_data);
		}

		// Emit data section relocations for pointer/reference globals initialized with &symbol
		for (const auto& global : global_variables_) {
			if (global.reloc_target.isValid()) {
				writer.add_data_relocation(StringTable::getStringView(global.name),
				                           StringTable::getStringView(global.reloc_target));
			}
		}

		// Emit vtables to .rdata section
		for (const auto& vtable : vtables_) {
			// Convert vector<string> to vector<string_view> for passing as span
			std::vector<std::string_view> func_symbols_sv;
			func_symbols_sv.reserve(vtable.function_symbols.size());
			for (const auto& sym : vtable.function_symbols) {
				func_symbols_sv.push_back(sym);
			}
			
			std::vector<std::string_view> base_class_names_sv;
			base_class_names_sv.reserve(vtable.base_class_names.size());
			for (const auto& name : vtable.base_class_names) {
				base_class_names_sv.push_back(name);
			}
			
			writer.add_vtable(StringTable::getStringView(vtable.vtable_symbol), func_symbols_sv, StringTable::getStringView(vtable.class_name), 
			                  base_class_names_sv, vtable.base_class_info, vtable.rtti_info);
		}

		// Now add pending global variable relocations (after symbols are created)
		// First, remove stale relocations from any error-skipped last function
		if (skip_previous_function_finalization_) {
			std::erase_if(pending_global_relocations_, [this](const PendingGlobalRelocation& r) {
				return r.offset >= current_function_offset_;
			});
			// Truncate textSectionData back to the start of the failed function
			textSectionData.resize(current_function_offset_);
		}
		for (const auto& reloc : pending_global_relocations_) {
			writer.add_text_relocation(reloc.offset, std::string(StringTable::getStringView(reloc.symbol_name)), reloc.type, reloc.addend);
		}

		// Patch all pending branches before finalizing
		// Skip patching if the last function was error-skipped (branches may reference unresolved labels)
		if (!skip_previous_function_finalization_) {
			patchBranches();
		} else {
			pending_branches_.clear();
			label_positions_.clear();
		}

		// Finalize the last function (if any) since there's no subsequent handleFunctionDecl to trigger it
		if (current_function_name_.isValid() && !skip_previous_function_finalization_) {
			auto [try_blocks, unwind_map] = convertExceptionInfoToWriterFormat();
			auto seh_try_blocks = convertSehInfoToWriterFormat();

			// Calculate actual stack space needed from scope_stack_space (which includes varargs area if present)
			// scope_stack_space is negative (offset from RBP), so negate to get positive size
			size_t total_stack = static_cast<size_t>(-variable_scopes.back().scope_stack_space);

			// Ensure stack frame also covers any catch object slot used by FH3 materialization.
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
			if (current_function_has_cpp_eh_) {
				size_t vars_used = static_cast<size_t>(-variable_scopes.back().scope_stack_space);
				if (total_stack < vars_used + 32) {
					total_stack = vars_used + 32;
				}
			}

			// Stack alignment: After PUSH RBP, RSP is 16-aligned.
			// SUB RSP, N must keep it 16-aligned for subsequent CALLs.
			// Both Linux and Windows: Align total_stack to 16n (multiple of 16)
			if (total_stack % 16 != 0) {
				total_stack = (total_stack + 15) & ~15;  // Round up to next 16n
			}
			
			// Patch the SUB RSP immediate at prologue offset + 3
			if (current_function_prologue_offset_ > 0) {
				uint32_t patch_offset = current_function_prologue_offset_ + 3;
				const auto bytes = std::bit_cast<std::array<uint8_t, 4>>(static_cast<uint32_t>(total_stack));
				for (int i = 0; i < 4; i++) {
					textSectionData[patch_offset + i] = bytes[i];
				}
			}

			// Patch catch continuation LEA RBP instructions (reuses sub_rsp_patches for LEA)
			for (auto fixup_patch_offset : catch_continuation_sub_rsp_patches_) {
				const auto bytes = std::bit_cast<std::array<char, 4>>(static_cast<uint32_t>(total_stack));
				for (int i = 0; i < 4; i++) {
					textSectionData[fixup_patch_offset + i] = bytes[i];
				}
			}
			catch_continuation_sub_rsp_patches_.clear();

			// Patch C++ EH prologue LEA RBP, [RSP + total_stack]
			if (eh_prologue_lea_rbp_offset_ > 0) {
				uint32_t lea_patch_offset = eh_prologue_lea_rbp_offset_ + 4;
				const auto bytes = std::bit_cast<std::array<char, 4>>(static_cast<uint32_t>(total_stack));
				for (int i = 0; i < 4; i++) {
					textSectionData[lea_patch_offset + i] = bytes[i];
				}
			}

			// Patch catch funclet LEA RBP, [RDX + total_stack] instructions
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
			writer.update_function_length(std::string(StringTable::getStringView(current_function_name_)), function_length);

			// Set debug range to match reference exactly
			if (function_length > 13) {
				//writer.set_function_debug_range(current_function_name_, 8, 5); // prologue=8, epilogue=3
			}

			// Add exception handling information (required for x64) - uses mangled name
			if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
				patchElfCatchFilterValues(try_blocks);
				writer.add_function_exception_info(StringTable::getStringView(current_function_mangled_name_), current_function_offset_, function_length, try_blocks, unwind_map, current_function_cfi_);
				elf_catch_filter_patches_.clear();
			} else {
				writer.add_function_exception_info(StringTable::getStringView(current_function_mangled_name_), current_function_offset_, function_length, try_blocks, unwind_map, seh_try_blocks, static_cast<uint32_t>(total_stack));
			}

			// Clear the current function state
			current_function_name_ = StringHandle();
			current_function_offset_ = 0;
			current_catch_handler_ = nullptr;
			in_catch_funclet_ = false;
			catch_funclet_return_slot_offset_ = 0;
			catch_funclet_return_flag_slot_offset_ = 0;
			catch_funclet_return_label_counter_ = 0;
			catch_funclet_terminated_by_return_ = false;
			current_catch_continuation_label_ = StringHandle();
			catch_return_bridges_.clear();
		}

		writer.add_data(textSectionData, SectionType::TEXT);

		// Finalize debug information
		writer.finalize_debug_info();
	}

	// Emit runtime helper functions for dynamic_cast as native x64 code
