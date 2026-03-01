	void handleFunctionCall(const IrInstruction& instruction) {
		// Use typed payload
		if (instruction.hasTypedPayload()) {
			const auto& call_op = instruction.getTypedPayload<CallOp>();
			
			flushAllDirtyRegisters();
			
			// Determine effective return size; fall back to type size if not provided
			int return_size_bits = call_op.return_size_in_bits;
			if (return_size_bits == 0) {
				int computed_size = get_type_size_bits(call_op.return_type);
				if (computed_size > 0) {
					return_size_bits = computed_size;
				} else {
					// Default to pointer size to ensure unique stack slot
					return_size_bits = static_cast<int>(sizeof(void*) * 8);
				}
			}
			
			// Get result offset - use actual return size for proper stack allocation
			FLASH_LOG_FORMAT(Codegen, Debug,
				"handleFunctionCall: allocating result {} (var_number={}) with return_size_in_bits={}",
				call_op.result.name(), call_op.result.var_number, return_size_bits);
			int result_offset = allocateStackSlotForTempVar(call_op.result.var_number, return_size_bits);
			FLASH_LOG_FORMAT(Codegen, Debug,
				"handleFunctionCall: result_offset={} for {} (var_number={})",
				result_offset, call_op.result.name(), call_op.result.var_number);
			variable_scopes.back().variables[StringTable::getOrInternStringHandle(call_op.result.name())].offset = result_offset;
			
			// Platform-specific format check for ABI differences
			constexpr bool is_coff_format = !std::is_same_v<TWriterClass, ElfFileWriter>;
			
			// For functions returning struct by value, prepare hidden return parameter
			// The return slot address will be passed as the first argument
			int param_shift = 0;  // Tracks how many parameters to shift (for hidden return param)
			if (call_op.usesReturnSlot()) {
				param_shift = 1;  // Regular parameters shift by 1 to make room for hidden return param
				
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Function call uses return slot - will pass address of temp_{} in first parameter register",
					call_op.result.var_number);
			}
			
			// IMPORTANT: Process stack arguments (beyond register count) FIRST, before loading register arguments.
			// To prevent loadTypedValueIntoRegister from clobbering parameter registers,
			// we reserve all parameter registers before processing stack arguments.
			// Platform-specific: Windows has 4 int regs, Linux has 6 int regs
			size_t max_int_regs = getMaxIntParamRegs<TWriterClass>();
			size_t max_float_regs = getMaxFloatParamRegs<TWriterClass>();
			size_t shadow_space = getShadowSpaceSize<TWriterClass>();
			
			// Reserve parameter registers to prevent them from being allocated as temporaries
			// Only reserve registers that aren't already allocated
			std::vector<X64Register> reserved_regs;
			for (size_t i = 0; i < max_int_regs; ++i) {
				X64Register reg = getIntParamReg<TWriterClass>(i);
				if (!regAlloc.is_allocated(reg)) {
					regAlloc.allocateSpecific(reg, -1);  // Reserve with dummy offset
					reserved_regs.push_back(reg);
				}
			}
			
			// Enhanced stack overflow logic: Track both int and float register usage independently
			// to correctly identify which arguments overflow to stack.
			// For variadic functions, register-passed args (first 4 on Windows, 6 on Linux) go in
			// registers as normal. Only args beyond the register count go on the stack at RSP+32+
			// (Windows) or RSP+0+ (Linux). The callee is responsible for homing its own register
			// parameters to shadow space; the caller must not pre-populate shadow space since it
			// overlaps with local variable storage in the caller's frame.
			//
			// Windows x64 ABI uses a UNIFIED position counter: position 0 is always RCX or XMM0,
			// position 1 is always RDX or XMM1, etc. — float and int arguments share the same
			// 4 register slots. Linux SysV AMD64 uses SEPARATE banks (6 int + 8 float).
			size_t temp_int_idx = 0;
			size_t temp_float_idx = 0;
			size_t stack_arg_count = 0;
			
			for (size_t i = 0; i < call_op.args.size(); ++i) {
				const auto& arg = call_op.args[i];
				// Reference arguments (including rvalue references) are passed as pointers,
				// so they should use integer registers, not floating-point registers
				bool is_float_arg = is_floating_point_type(arg.type) && !arg.is_reference();
				bool is_two_reg_struct = isTwoRegisterStruct(arg);
				
				// Determine if this argument goes on stack (overflows register file)
				bool goes_on_stack = false;
				if (is_coff_format && call_op.is_variadic) {
					// Windows x64 VARIADIC: unified position counter — int and float share the same 4 slots.
					// Position i uses RCX/XMM0 (i=0), RDX/XMM1 (i=1), R8/XMM2 (i=2), R9/XMM3 (i=3).
					// Any arg at position i >= max_int_regs goes to the stack.
					goes_on_stack = (i + param_shift >= max_int_regs);
					if (is_float_arg) temp_float_idx++;
					else temp_int_idx++;
				} else {
					// Linux SysV (all calls) and Windows non-variadic: separate register banks.
					// Both caller and callee agree on this sequential convention, so it works.
					if (is_float_arg) {
						if (temp_float_idx >= max_float_regs) {
							goes_on_stack = true;
						}
						temp_float_idx++;
					} else {
						// For two-register structs, need two consecutive int registers
						size_t regs_needed = is_two_reg_struct ? 2 : 1;
						if (temp_int_idx + regs_needed > max_int_regs) {
							goes_on_stack = true;
						}
						temp_int_idx += regs_needed;
					}
				}
				
				if (goes_on_stack) {
					// Stack args placement:
					// Windows: RSP+32 (shadow space) + stack_arg_count*8
					// Linux: RSP+0 (no shadow space) + stack_arg_count*8
					int stack_offset = static_cast<int>(shadow_space + stack_arg_count * 8);

					// Determine if this stack argument needs to pass an address instead of its value
					bool stack_pass_address = arg.is_reference() || shouldPassStructByAddress(arg);

					if (stack_pass_address) {
						// Store address of the argument on the stack
						X64Register temp_reg = allocateRegisterWithSpilling();
						if (std::holds_alternative<StringHandle>(arg.value)) {
							StringHandle var_handle = std::get<StringHandle>(arg.value);
							int var_offset = variable_scopes.back().variables[var_handle].offset;
							auto ref_it = reference_stack_info_.find(var_offset);
							if (ref_it != reference_stack_info_.end()) {
								// Already holds a pointer (e.g., reference variable) - load it
								emitMovFromFrame(temp_reg, var_offset);
							} else {
								// Take address of the variable
								emitLeaFromFrame(temp_reg, var_offset);
							}
						} else if (std::holds_alternative<TempVar>(arg.value)) {
							const auto& temp_var = std::get<TempVar>(arg.value);
							int var_offset = getStackOffsetFromTempVar(temp_var);
							auto ref_it = reference_stack_info_.find(var_offset);
							if (ref_it != reference_stack_info_.end()) {
								emitMovFromFrame(temp_reg, var_offset);
							} else {
								emitLeaFromFrame(temp_reg, var_offset);
							}
						}
						emitStoreToRSP(textSectionData, temp_reg, stack_offset);
						regAlloc.release(temp_reg);
					} else if (is_float_arg) {
						// For floating-point arguments, load into XMM register and store with float instruction
						X64Register temp_xmm = allocateXMMRegisterWithSpilling();

						// Load the float value into XMM register
						if (std::holds_alternative<double>(arg.value)) {
							// Handle floating-point literal
							double float_value = std::get<double>(arg.value);
							uint64_t bits;
							if (arg.type == Type::Float) {
								float float_val = static_cast<float>(float_value);
								uint32_t float_bits;
								std::memcpy(&float_bits, &float_val, sizeof(float_bits));
								bits = float_bits;
							} else {
								std::memcpy(&bits, &float_value, sizeof(bits));
							}

							// Load bit pattern into temp GPR first
							X64Register temp_gpr = allocateRegisterWithSpilling();
							emitMovImm64(temp_gpr, bits);

							// Move from GPR to XMM register
							emitMovqGprToXmm(temp_gpr, temp_xmm);

							regAlloc.release(temp_gpr);
						} else if (std::holds_alternative<TempVar>(arg.value)) {
							const auto& temp_var = std::get<TempVar>(arg.value);
							int var_offset = getStackOffsetFromTempVar(temp_var);
							bool is_float = (arg.type == Type::Float);
							emitFloatMovFromFrame(temp_xmm, var_offset, is_float);
						} else if (std::holds_alternative<StringHandle>(arg.value)) {
							StringHandle var_name_handle = std::get<StringHandle>(arg.value);
							int var_offset = variable_scopes.back().variables[var_name_handle].offset;
							bool is_float = (arg.type == Type::Float);
							emitFloatMovFromFrame(temp_xmm, var_offset, is_float);
						}

						// Store XMM register to stack using float store instruction
						bool is_float = (arg.type == Type::Float);
						emitFloatStoreToRSP(textSectionData, temp_xmm, stack_offset, is_float);

						regAlloc.release(temp_xmm);
					} else {
						// For integer arguments, use the existing code path
						X64Register temp_reg = loadTypedValueIntoRegister(arg);
						emitStoreToRSP(textSectionData, temp_reg, stack_offset);
						regAlloc.release(temp_reg);
					}
					stack_arg_count++;
				}
			}
			
			// Release reserved parameter registers now that stack arguments are processed
			for (X64Register reg : reserved_regs) {
				regAlloc.release(reg);
			}
			
			// Now process register arguments (platform-specific: 4 on Windows, 6 on Linux for integers)
			// Note: max_int_regs and max_float_regs already declared above for stack arg processing
			// Use separate counters for integer and float registers (System V AMD64 ABI requirement)
			// If function uses return slot, start at index param_shift to leave room for hidden parameter
			size_t int_reg_index = param_shift;  // Start at param_shift if hidden return param present
			size_t float_reg_index = 0;
			
			for (size_t i = 0; i < call_op.args.size(); ++i) {
				const auto& arg = call_op.args[i];
				
				// Determine if this is a floating-point argument
				// Reference arguments (including rvalue references) are passed as pointers (addresses),
				// so they should use integer registers regardless of the underlying type
				bool is_float_arg = is_floating_point_type(arg.type) && !arg.is_reference();
				bool is_potential_two_reg_struct = isTwoRegisterStruct(arg);
				
				// Check if this argument fits in a register (accounting for param_shift)
				// Windows x64 variadic: unified position counter — int and float share the same 4 slots.
				// Windows x64 non-variadic + Linux SysV: separate integer and float register banks.
				bool use_register = false;
				if (is_coff_format && call_op.is_variadic) {
					// Windows x64 VARIADIC: position (i + param_shift) determines register use
					use_register = (i + param_shift < max_int_regs);
				} else if (is_float_arg) {
					if (float_reg_index < max_float_regs) {
						use_register = true;
					}
				} else {
					// For two-register structs, need two consecutive int registers
					size_t regs_needed = is_potential_two_reg_struct ? 2 : 1;
					if (int_reg_index + regs_needed <= max_int_regs) {
						use_register = true;
					}
				}
				
				// Skip arguments that go on stack (already handled)
				if (!use_register) {
					if (is_float_arg) float_reg_index++;
					else int_reg_index++;
					continue;
				}
				
				// Get the platform-specific calling convention register
				// Windows x64 variadic: position-aligned registers (position = i + param_shift)
				// Windows x64 non-variadic + Linux SysV: separate int and float indices
				X64Register target_reg;
				if (is_coff_format && call_op.is_variadic) {
					// Windows x64 VARIADIC: both int and float use the same position counter.
					// This ensures the shadow-space homing + va_arg walking lines up correctly.
					size_t position = i + param_shift;
					target_reg = is_float_arg
						? getFloatParamReg<TWriterClass>(position)
						: getIntParamReg<TWriterClass>(position);
					if (is_float_arg) float_reg_index++;
					else int_reg_index++;
				} else {
					target_reg = is_float_arg
						? getFloatParamReg<TWriterClass>(float_reg_index++)
						: getIntParamReg<TWriterClass>(int_reg_index++);
				}

				
				// Special handling for passing addresses (this pointer or large struct references)
				// For member functions: first arg is always "this" pointer (pass address)
				// System V AMD64 ABI (Linux): 
				//   - Structs ≤8 bytes: pass by value in one register
				//   - Structs 9-16 bytes: pass by value in TWO consecutive registers
				//   - Structs >16 bytes: pass by pointer
				// x64 Windows ABI:
				//   - Structs of 1, 2, 4, or 8 bytes: pass by value in one register
				//   - All other structs: pass by pointer
				bool should_pass_address = false;
				bool is_two_register_struct = false;
				if (call_op.is_member_function && i == 0) {
					// First argument of member function is always "this" pointer
					should_pass_address = true;
				} else if (arg.is_reference()) {
					// Parameter is explicitly a reference - always pass by address
					should_pass_address = true;
				} else if (shouldPassStructByAddress(arg)) {
					should_pass_address = true;
				} else {
					is_two_register_struct = is_potential_two_reg_struct;
				}
				
				if (should_pass_address && std::holds_alternative<StringHandle>(arg.value)) {
					// Load ADDRESS of object using LEA or MOV depending on whether it's a reference
					StringHandle object_name_handle = std::get<StringHandle>(arg.value);
					int object_offset = variable_scopes.back().variables[object_name_handle].offset;
					
					// Check if this variable is itself a reference (e.g., rvalue reference variable)
					// If so, it already holds a pointer, so load it with MOV instead of LEA
					auto ref_it = reference_stack_info_.find(object_offset);
					if (ref_it != reference_stack_info_.end()) {
						// Variable is a reference - it already holds a pointer, load it
						emitMovFromFrame(target_reg, object_offset);
					} else {
						// Variable is not a reference - take its address with LEA
						emitLEAFromFrame(textSectionData, target_reg, object_offset);
					}
					continue;
				}
				
				// Handle System V AMD64 ABI: Structs 9-16 bytes passed in TWO consecutive registers
				if (is_two_register_struct && std::holds_alternative<StringHandle>(arg.value)) {
					StringHandle object_name_handle = std::get<StringHandle>(arg.value);
					int object_offset = variable_scopes.back().variables[object_name_handle].offset;
					
					// Load first 8 bytes into target_reg (already allocated)
					emitMovFromFrame(target_reg, object_offset);
					
					// Check if we have a second register available
					if (int_reg_index < max_int_regs) {
						// Load second 8 bytes into next integer register
						X64Register second_reg = getIntParamReg<TWriterClass>(int_reg_index++);
						emitMovFromFrame(second_reg, object_offset + 8);
					} else {
						// No second register available - need to spill to stack
						// This case should be rare in practice
						FLASH_LOG(Codegen, Warning, "Two-register struct has no second register available");
					}
					continue;
				}
				
				// Handle TempVar arguments that should pass an address (e.g., constructor calls passed to rvalue reference params)
				if (should_pass_address && std::holds_alternative<TempVar>(arg.value)) {
					// When should_pass_address is true, the TempVar can be either:
					// 1. An object value that needs its address taken (like Widget(42)) - use LEA
					// 2. A pointer value from AddressOf or cast (like result of (Widget&&)w1) - use MOV
					//
					// To distinguish:
					// - Case 2: The TempVar was written by AddressOf/cast and holds a pointer
					// - Case 1: The TempVar holds the actual object
					//
					// Since we can't easily tell from the IR alone, use a simple heuristic:
					// Check reference_stack_info_ to see if this variable is marked as holding a reference/pointer
					const auto& temp_var = std::get<TempVar>(arg.value);
					int var_offset = getStackOffsetFromTempVar(temp_var);
					
					auto ref_it = reference_stack_info_.find(var_offset);
					if (ref_it != reference_stack_info_.end()) {
						// Variable is marked as holding a pointer/reference - load it with MOV
						emitMovFromFrame(target_reg, var_offset);
					} else {
						// Variable holds an object value - take its address with LEA
						emitLeaFromFrame(target_reg, var_offset);
					}
					continue;
				}
				
				// Handle System V AMD64 ABI: TempVar structs 9-16 bytes passed in TWO consecutive registers
				if (is_two_register_struct && std::holds_alternative<TempVar>(arg.value)) {
					const auto& temp_var = std::get<TempVar>(arg.value);
					int var_offset = getStackOffsetFromTempVar(temp_var);
					
					// Load first 8 bytes into target_reg (already allocated)
					emitMovFromFrame(target_reg, var_offset);
					
					// Check if we have a second register available
					if (int_reg_index < max_int_regs) {
						// Load second 8 bytes into next integer register
						X64Register second_reg = getIntParamReg<TWriterClass>(int_reg_index++);
						emitMovFromFrame(second_reg, var_offset + 8);
					} else {
						FLASH_LOG(Codegen, Warning, "Two-register TempVar struct has no second register available");
					}
					continue;
				}
				
				// Handle floating-point immediate values (double literals)
				if (is_float_arg && std::holds_alternative<double>(arg.value)) {
					// Load floating-point literal into XMM register
					double float_value = std::get<double>(arg.value);
					
					// For float (32-bit), we need to convert the double to float first
					uint64_t bits;
					if (arg.type == Type::Float) {
						float float_val = static_cast<float>(float_value);
						uint32_t float_bits;
						std::memcpy(&float_bits, &float_val, sizeof(float_bits));
						bits = float_bits; // Zero-extend to 64-bit
					} else {
						std::memcpy(&bits, &float_value, sizeof(bits));
					}
					
					// Allocate a temporary GPR for the bit pattern
					X64Register temp_gpr = allocateRegisterWithSpilling();
					
					// Load bit pattern into temp GPR
					emitMovImm64(temp_gpr, bits);
					
					// movq xmm, r64 (66 REX.W 0F 6E /r) - move from GPR to XMM
					textSectionData.push_back(0x66);
					uint8_t xmm_idx = xmm_modrm_bits(target_reg);
					uint8_t rex_movq = 0x48;  // REX.W
					if (xmm_idx >= 8) {
						rex_movq |= 0x04; // REX.R for XMM8-XMM15 destination
					}
					if (static_cast<uint8_t>(temp_gpr) >= static_cast<uint8_t>(X64Register::R8)) {
						rex_movq |= 0x01; // REX.B for source GPR
					}
					textSectionData.push_back(rex_movq);
					textSectionData.push_back(0x0F);
					textSectionData.push_back(0x6E);
					uint8_t modrm_movq = 0xC0 + ((xmm_idx & 0x07) << 3) + (static_cast<uint8_t>(temp_gpr) & 0x07);
					textSectionData.push_back(modrm_movq);
					
					// For varargs functions, Windows x64 requires copying XMM value to the
					// corresponding integer register at the same position (for shadow-space homing).
					// System V AMD64 does NOT require this - floats stay in XMM registers only.
					// Use position = i + param_shift for the correct integer register slot.
					if (call_op.is_variadic && (i + param_shift) < max_int_regs && is_coff_format) {
						emitMovqXmmToGpr(target_reg, getIntParamReg<TWriterClass>(i + param_shift));
					}
					
					// Release the temporary GPR
					regAlloc.release(temp_gpr);
					continue;
				}
				
				// Load value into target register
				if (std::holds_alternative<unsigned long long>(arg.value)) {
					// Load immediate directly into target register
					unsigned long long value = std::get<unsigned long long>(arg.value);
					// Use 32-bit mov for 32-bit arguments (automatically zero-extends to 64-bit)
					// This ensures proper handling of signed 32-bit values like -1
					if (arg.size_in_bits == 32) {
						// Cast to uint32_t truncates to lower 32 bits (intended behavior)
						// For signed values like -1 (0xFFFFFFFFFFFFFFFF), this gives 0xFFFFFFFF
						emitMovImm32(target_reg, static_cast<uint32_t>(value));
					} else {
						emitMovImm64(target_reg, value);
					}
				} else if (std::holds_alternative<TempVar>(arg.value)) {
					// Load from stack
					const auto& temp_var = std::get<TempVar>(arg.value);
					int var_offset = getStackOffsetFromTempVar(temp_var);
					if (is_float_arg) {
						// For floating-point, use movsd/movss into XMM register
						bool is_float = (arg.type == Type::Float);
						emitFloatMovFromFrame(target_reg, var_offset, is_float);
						
						// For varargs: floats must be promoted to double (C standard)
						if (call_op.is_variadic && is_float) {
							emitCvtss2sd(target_reg, target_reg);
						}
						
						// For varargs: also copy to corresponding INT register (Windows x64 only)
						// System V AMD64 ABI does NOT require this
						if (call_op.is_variadic && (i + param_shift) < max_int_regs && is_coff_format) {
							emitMovqXmmToGpr(target_reg, getIntParamReg<TWriterClass>(i + param_shift));
						}
					} else {
						// Use size-aware load: source (stack slot) -> destination (register)
						// Both sizes are explicit for clarity
						emitMovFromFrameSized(
							SizedRegister{target_reg, 64, false},  // dest: always load into 64-bit register
							SizedStackSlot{var_offset, arg.size_in_bits, isSignedType(arg.type)}  // source: sized stack slot
						);
						regAlloc.flushSingleDirtyRegister(target_reg);
					}
				} else if (std::holds_alternative<StringHandle>(arg.value)) {
					// Load variable
					StringHandle var_name_handle = std::get<StringHandle>(arg.value);
			[[maybe_unused]] std::string_view var_name = StringTable::getStringView(var_name_handle);
					int var_offset = variable_scopes.back().variables[var_name_handle].offset;
					if (is_float_arg) {
						// For floating-point, use movsd/movss into XMM register
						bool is_float = (arg.type == Type::Float);
						emitFloatMovFromFrame(target_reg, var_offset, is_float);
						
						// For varargs: floats must be promoted to double (C standard)
						if (call_op.is_variadic && is_float) {
							emitCvtss2sd(target_reg, target_reg);
						}
						
						// For varargs: also copy to corresponding INT register (Windows x64 only)
						// System V AMD64 ABI does NOT require this
						if (call_op.is_variadic && (i + param_shift) < max_int_regs && is_coff_format) {
							emitMovqXmmToGpr(target_reg, getIntParamReg<TWriterClass>(i + param_shift));
						}
					} else {
						// Use size-aware load: source (stack slot) -> destination (register)
						emitMovFromFrameSized(
							SizedRegister{target_reg, 64, false},  // dest: always load into 64-bit register
							SizedStackSlot{var_offset, arg.size_in_bits, isSignedType(arg.type)}  // source: sized stack slot
						);
						regAlloc.flushSingleDirtyRegister(target_reg);
					}
				}
			}
			
			// For varargs functions on System V AMD64, set AL to number of XMM registers actually used
			if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
				if (call_op.is_variadic) {
					// Count XMM registers actually allocated (need to track float_reg_index)
					size_t xmm_count = 0;
					size_t va_temp_float_idx = 0;
					for (size_t i = 0; i < call_op.args.size(); ++i) {
						const auto& arg = call_op.args[i];
						if (is_floating_point_type(arg.type)) {
							if (va_temp_float_idx < max_float_regs) {
								xmm_count++;
								va_temp_float_idx++;
							}
						}
					}
					// Set AL (lower 8 bits of RAX) to the count
					// MOV AL, imm8: B0 + imm8
					textSectionData.push_back(0xB0);
					textSectionData.push_back(static_cast<uint8_t>(xmm_count));
				}
			}
			
			// If function uses return slot, pass the address of the result location as hidden first parameter
			if (call_op.usesReturnSlot()) {
				// Load address of return slot (result_offset) into first integer parameter register
				X64Register return_slot_reg = getIntParamReg<TWriterClass>(0);
				
				// LEA return_slot_reg, [RBP + result_offset]
				emitLeaFromFrame(return_slot_reg, result_offset);
				
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Passing return slot address (offset {}) in register {} for struct return",
					result_offset, static_cast<int>(return_slot_reg));
			}
			
			// Generate call instruction
			if (call_op.is_indirect_call) {
				// Indirect call: the function_name is actually the variable name holding the function pointer
				// Allocate a register using the register allocator, load the function pointer, then call through it
				StringHandle func_ptr_name = call_op.getFunctionName();
				int func_ptr_offset = variable_scopes.back().variables[func_ptr_name].offset;
				
				// Note: Both function pointers and function references are handled the same way here.
				// The reference variable holds the function address directly (function references
				// decay to function pointers, so we just load the 64-bit function address from the
				// stack location and call through it).
				
				// Allocate a scratch register for the indirect call
				X64Register call_reg = allocateRegisterWithSpilling();
				
				// Load the function pointer/reference value
				emitMovFromFrame(call_reg, func_ptr_offset);
				
				// Emit indirect call through the allocated register
				emitCallReg(textSectionData, call_reg);
				
				// Release the register after the call
				regAlloc.release(call_reg);
				
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Generated indirect call through {} at offset {}",
					static_cast<int>(call_reg), func_ptr_offset);
			} else {
				// Direct call: E8 + 32-bit relative offset
				std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
				textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());
				
				// Add relocation for function name (Phase 4: Use helper)
				StringHandle func_name_handle = call_op.getFunctionName();
				std::string mangled_name(StringTable::getStringView(func_name_handle));
				writer.add_relocation(textSectionData.size() - 4, mangled_name);
			}
			
			// Invalidate caller-saved registers (function calls clobber them)
			regAlloc.invalidateCallerSavedRegisters();
			
			// Phase 5: Copy elision opportunity detection
			// Check if this is a prvalue return being used to initialize a variable
			bool is_prvalue_return = isTempVarPRValue(call_op.result);
			
			FLASH_LOG_FORMAT(Codegen, Debug,
				"FunctionCall result: {} is_prvalue={}", 
				call_op.result.name(), is_prvalue_return);
			
			// Store return value - RAX for integers, XMM0 for floats
			// For struct returns using return slot, the struct is already in place - no copy needed
			if (call_op.return_type != Type::Void && !call_op.usesReturnSlot()) {
				if (is_floating_point_type(call_op.return_type)) {
					// Float return value is in XMM0
					bool is_float = (call_op.return_type == Type::Float);
					emitFloatMovToFrame(X64Register::XMM0, result_offset, is_float);
				} else if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
					// SystemV AMD64 ABI: structs 9-16 bytes return in RAX (low 8 bytes) and RDX (high 8 bytes)
					if (call_op.return_type == Type::Struct && return_size_bits > 64 && return_size_bits <= 128) {
						// Two-register struct return: first 8 bytes in RAX, next 8 bytes in RDX
						emitMovToFrame(X64Register::RAX, result_offset, return_size_bits);  // Store low 8 bytes
						emitMovToFrame(X64Register::RDX, result_offset + 8, return_size_bits - 64);  // Store high 8 bytes
						FLASH_LOG_FORMAT(Codegen, Debug,
							"Storing two-register struct return ({} bits): RAX->offset {}, RDX->offset {}",
							return_size_bits, result_offset, result_offset + 8);
					} else {
						// Single-register return (≤64 bits) in RAX
						emitMovToFrameSized(
							SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
							SizedStackSlot{result_offset, return_size_bits, isSignedType(call_op.return_type)}  // dest
						);
					}
				} else {
					// Windows x64 ABI: small structs (≤64 bits) return in RAX only
					emitMovToFrameSized(
						SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
						SizedStackSlot{result_offset, return_size_bits, isSignedType(call_op.return_type)}  // dest
					);
				}
			} else if (call_op.usesReturnSlot()) {
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Struct return using return slot - struct already constructed at offset {}",
					result_offset);
			}
			
			// Mark rvalue reference returns in reference_stack_info_ so they are treated as pointers
			// This is needed for proper handling when passing rvalue reference results to other functions
			if (call_op.returns_rvalue_reference) {
				reference_stack_info_[result_offset] = ReferenceInfo{
					.value_type = call_op.return_type,
					.value_size_bits = call_op.return_size_in_bits,
					.is_rvalue_reference = true,
					.holds_address_only = true  // The function returned a pointer/address
				};
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Marked function call result at offset {} as rvalue reference (holds address)",
					result_offset);
			}
			
			// No stack cleanup needed after call:
			// - Windows x64 ABI: Uses pre-allocated shadow space, not PUSH
			// - Linux System V AMD64: Arguments in registers or pushed before call, stack pointer already adjusted
			
			return;
		}
		
		// All function calls should use typed payload (CallOp)
		// Legacy operand-based path has been removed for better maintainability
		throw InternalError("Function call without typed payload - should not happen");
	}

	void handleConstructorCall(const IrInstruction& instruction) {
		// Constructor call format: ConstructorCallOp {struct_name, object, arguments}
		const ConstructorCallOp& ctor_op = instruction.getTypedPayload<ConstructorCallOp>();

		flushAllDirtyRegisters();

		std::string_view struct_name = StringTable::getStringView(ctor_op.struct_name);

		// Get the object's stack offset
		int object_offset = 0;
		bool object_is_pointer = false;  // Declare early so RVO branch can set it
		
		// If using return slot (RVO), get offset from return_slot_offset or look up __return_slot
		if (ctor_op.use_return_slot) {
			if (ctor_op.return_slot_offset.has_value()) {
				object_offset = ctor_op.return_slot_offset.value();
			} else {
				// Look up __return_slot in the variables map
				auto return_slot_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle("__return_slot"));
				if (return_slot_it != variable_scopes.back().variables.end()) {
					// __return_slot holds the address where we should construct
					// Load this address into RDI for the constructor call
					int return_slot_param_offset = return_slot_it->second.offset;
					X64Register dest_reg = X64Register::RDI;
					emitMovFromFrame(dest_reg, return_slot_param_offset);
					
					// Store the address in a temp location so we can use it as object_offset
					// Actually, we'll pass it differently - set object_is_pointer flag
					object_offset = return_slot_param_offset;
					object_is_pointer = true;  // The offset holds a pointer to where object should be
					
					FLASH_LOG_FORMAT(Codegen, Debug,
						"Constructor using RVO: loading return slot address from __return_slot at offset {}",
						return_slot_param_offset);
				} else {
					FLASH_LOG(Codegen, Error,
						"Constructor marked for RVO but __return_slot not found in variables");
					// Fall through to regular handling
				}
			}
			
			FLASH_LOG_FORMAT(Codegen, Debug,
				"Constructor using return slot (RVO) at offset {}",
				object_offset);
		} else if (std::holds_alternative<TempVar>(ctor_op.object)) {
			const TempVar temp_var = std::get<TempVar>(ctor_op.object);
			
			// Get struct size for proper stack allocation
			int struct_size_bits = 64;  // Default to 8 bytes
			auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
			if (struct_type_it != gTypesByName.end()) {
				const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
				if (struct_info) {
					struct_size_bits = static_cast<int>(struct_info->total_size * 8);  // Convert bytes to bits
					FLASH_LOG_FORMAT(Codegen, Debug,
						"Constructor for {} found struct_info with size {} bits",
						struct_name, struct_size_bits);
				} else {
					FLASH_LOG_FORMAT(Codegen, Debug,
						"Constructor for {} found in gTypesByName but no struct_info",
						struct_name);
				}
			} else {
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Constructor for {} NOT found in gTypesByName",
					struct_name);
			}
			
			// TempVars can be either stack-allocated or heap-allocated
			// Use is_heap_allocated flag to distinguish:
			// - Heap-allocated (from new): TempVar holds a pointer, use MOV to load it
			// - Stack-allocated (RVO/NRVO): TempVar is the object location, use LEA to get address
			object_offset = getStackOffsetFromTempVar(temp_var, struct_size_bits);
			object_is_pointer = ctor_op.is_heap_allocated;
		} else {
			StringHandle var_name_handle = std::get<StringHandle>(ctor_op.object);
			auto it = variable_scopes.back().variables.find(var_name_handle);
			if (it == variable_scopes.back().variables.end()) {
				throw InternalError("Constructor call: variable not found in variables map: " + std::string(StringTable::getStringView(var_name_handle)));
			}
			object_offset = it->second.offset;
			object_is_pointer = (StringTable::getStringView(var_name_handle) == "this");
			
			// If this is an array element constructor call, adjust offset for the specific element
			if (ctor_op.array_index.has_value()) {
				// Look up struct size to calculate element offset
				auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
				if (struct_type_it != gTypesByName.end()) {
					const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
					if (struct_info) {
						size_t element_size = struct_info->total_size;
						size_t index = ctor_op.array_index.value();
						// Adjust offset: base_offset + (index * element_size)
						object_offset += static_cast<int>(index * element_size);
					}
				}
			}
		}

		// Load the address of the object into the first parameter register ('this' pointer)
		// Use platform-specific register: RDI on Linux, RCX on Windows
		X64Register this_reg = getIntParamReg<TWriterClass>(0);
		
		FLASH_LOG_FORMAT(Codegen, Debug,
			"Constructor call for {}: object_is_pointer={}, object_offset={}, base_class_offset={}",
			struct_name, object_is_pointer, object_offset, ctor_op.base_class_offset);
		
		if (object_is_pointer) {
			// For pointers (this, heap-allocated): reload the pointer value (not its address)
			// MOV this_reg, [RBP + object_offset]
			emitMovFromFrame(this_reg, object_offset);
			// Add base_class_offset for multiple inheritance (adjust pointer to base subobject)
			if (ctor_op.base_class_offset != 0) {
				emitAddRegImm32(textSectionData, this_reg, ctor_op.base_class_offset);
			}
		} else {
			// For regular stack objects: get the address
			// LEA this_reg, [RBP + object_offset + base_class_offset]
			// The base_class_offset adjusts for multiple inheritance
			auto lea_inst = generateLeaFromFrame(this_reg, object_offset + ctor_op.base_class_offset);
			textSectionData.insert(textSectionData.end(), lea_inst.op_codes.begin(), lea_inst.op_codes.begin() + lea_inst.size_in_bytes);
		}

		// Process constructor parameters (if any) - similar to function call
		const size_t num_params = ctor_op.arguments.size();

		// Look up the struct type once for use in both loops
		auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));

		// Find the actual constructor to get the correct parameter types
		// This is more reliable than trying to infer from argument types
		const ConstructorDeclarationNode* actual_ctor = nullptr;
		if (struct_type_it != gTypesByName.end()) {
			const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
			if (struct_info) {
				// FIRST: If we have exactly one parameter that's a reference to the same struct type,
				// prefer the copy constructor over other single-parameter constructors
				if (num_params == 1 && !ctor_op.arguments.empty()) {
					const TypedValue& arg = ctor_op.arguments[0];
					bool arg_is_same_struct = (arg.type == Type::Struct && 
					                           arg.type_index == struct_type_it->second->type_index_);
					bool arg_is_ref_or_pointer = (arg.is_reference() || arg.size_in_bits == 64);
					
					if (arg_is_same_struct && arg_is_ref_or_pointer) {
						// Try to find copy constructor
						const StructMemberFunction* copy_ctor = struct_info->findCopyConstructor();
						if (copy_ctor && copy_ctor->function_decl.is<ConstructorDeclarationNode>()) {
							actual_ctor = &copy_ctor->function_decl.as<ConstructorDeclarationNode>();
							FLASH_LOG_FORMAT(Codegen, Debug,
								"Constructor call for {}: matched copy constructor",
								struct_name);
						}
					}
				}
				
				// SECOND: If no copy constructor matched, look for other constructors with matching parameter count
				if (!actual_ctor) {
				// Look for a constructor with matching number of parameters
				for (const auto& func : struct_info->member_functions) {
					if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
						const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
						const auto& params = ctor_node.parameter_nodes();
						
						// Skip implicit copy/move constructors when the argument
						// isn't the same struct type (e.g., aggregate init my_type{0})
						if (ctor_node.is_implicit() && params.size() == 1 && num_params == 1 &&
						    params[0].is<DeclarationNode>()) {
							const auto& param_type = params[0].as<DeclarationNode>().type_node();
							if (param_type.is<TypeSpecifierNode>()) {
								const auto& pts = param_type.as<TypeSpecifierNode>();
								if ((pts.is_reference() || pts.is_rvalue_reference()) &&
								    (pts.type() == Type::Struct || pts.type() == Type::UserDefined)) {
									// Check if the argument is actually the same struct type
									const TypedValue& arg = ctor_op.arguments[0];
									if (arg.type != Type::Struct || arg.type_index != struct_type_it->second->type_index_) {
										continue;  // Skip implicit copy/move ctor - arg isn't same struct
									}
								}
							}
						}
						
						if (params.size() == num_params) {
							actual_ctor = &ctor_node;
							break;
						}
					}
				}
				}
			}
		}

		// Extract parameter types for overload resolution
		std::vector<TypeSpecifierNode> parameter_types;
		
		// If we found the actual constructor, use its parameter types directly
		if (actual_ctor) {
			const auto& ctor_params = actual_ctor->parameter_nodes();
			for (size_t i = 0; i < num_params && i < ctor_params.size(); ++i) {
				if (ctor_params[i].is<DeclarationNode>()) {
					const auto& param_decl = ctor_params[i].as<DeclarationNode>();
					if (param_decl.type_node().is<TypeSpecifierNode>()) {
						auto param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();
						parameter_types.push_back(param_type_spec);
						continue;
					}
				}
				// Fallback: if we can't get the param type, create a default one
				const TypedValue& arg = ctor_op.arguments[i];
				parameter_types.push_back(TypeSpecifierNode(arg.type, TypeQualifier::None, static_cast<unsigned char>(arg.size_in_bits), Token{}));
			}
		} else {
			// Fallback to old logic: infer from argument types
			for (size_t i = 0; i < num_params; ++i) {
				const TypedValue& arg = ctor_op.arguments[i];
				Type paramType = arg.type;
				int paramSize = arg.size_in_bits;
				TypeIndex arg_type_index = arg.type_index;
				bool arg_is_reference = arg.is_reference();  // Check if marked as reference
				int arg_pointer_depth = arg.pointer_depth;
				CVQualifier arg_cv_qualifier = arg.cv_qualifier;
				
				// Build TypeSpecifierNode for this parameter
				// For pointers, use the base type size, not the pointer size (64 bits)
				int actual_size = paramSize;
				if (arg_pointer_depth > 0) {
					// This is a pointer - set size to pointee type size
					// For basic types, use get_type_size_bits
					int basic_size = get_type_size_bits(paramType);
					if (basic_size > 0) {
						actual_size = basic_size;
					}
					// For struct types, keep the size as-is (basic_size will be 0)
				}
				
				TypeSpecifierNode param_type(paramType, TypeQualifier::None, static_cast<unsigned char>(actual_size), Token{}, arg_cv_qualifier);
				
				// Add pointer levels
				for (int p = 0; p < arg_pointer_depth; ++p) {
					param_type.add_pointer_level(CVQualifier::None);
				}
				
				// If the argument is marked as a reference, set it as such
				if (arg_is_reference) {
					param_type.set_reference_qualifier(arg.ref_qualifier);
				}
				
				// For copy/move constructors: if parameter is the same struct type, it should be a reference
				// Copy constructor: Type(Type& other) or Type(const Type& other) -> paramType == Type::Struct and same as struct_name
				// We detect this by checking if paramType is Struct and num_params == 1 AND the type_index matches
				bool is_same_struct_type = false;
				if (struct_type_it != gTypesByName.end() && arg_type_index != 0) {
					is_same_struct_type = (arg_type_index == struct_type_it->second->type_index_);
				}
				
				if (num_params == 1 && paramType == Type::Struct && is_same_struct_type && !arg_is_reference) {
					// This is likely a copy constructor, but arg_is_reference wasn't set
					// Determine the actual CV qualifier from the constructor signature
					auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
					if (type_it != gTypesByName.end()) {
						TypeIndex struct_type_index = type_it->second->type_index_;
						const StructTypeInfo* struct_info = type_it->second->getStructInfo();
						
						// Default to const reference (standard implicit copy constructor)
						CVQualifier copy_ctor_cv = CVQualifier::Const;
						
						// Check if there's an explicit copy constructor with a different signature
						if (struct_info) {
							const StructMemberFunction* copy_ctor = struct_info->findCopyConstructor();
							if (copy_ctor && copy_ctor->function_decl.is<ConstructorDeclarationNode>()) {
								const auto& ctor_node = copy_ctor->function_decl.as<ConstructorDeclarationNode>();
								const auto& params = ctor_node.parameter_nodes();
								if (params.size() == 1 && params[0].is<DeclarationNode>()) {
									const auto& param_decl = params[0].as<DeclarationNode>();
									if (param_decl.type_node().is<TypeSpecifierNode>()) {
										auto ctor_param_type = param_decl.type_node().as<TypeSpecifierNode>();
										copy_ctor_cv = ctor_param_type.cv_qualifier();
									}
								}
							}
						}
						
						param_type = TypeSpecifierNode(paramType, struct_type_index, static_cast<unsigned char>(actual_size), Token{}, copy_ctor_cv);
						param_type.set_reference_qualifier(ReferenceQualifier::LValueReference);  // set_reference(false) creates an lvalue reference (not rvalue)
					}
				} else if (paramType == Type::Struct && arg_type_index != 0) {
					// Not a copy constructor, but still a struct parameter - set the type_index
					param_type = TypeSpecifierNode(paramType, arg_type_index, static_cast<unsigned char>(actual_size), Token{}, arg_cv_qualifier);
					// Add pointer levels (rebuild after creating with type_index)
					for (int p = 0; p < arg_pointer_depth; ++p) {
						param_type.add_pointer_level(CVQualifier::None);
					}
					// Also preserve the reference flag if it was set
					if (arg_is_reference) {
						param_type.set_reference_qualifier(arg.ref_qualifier);
					}
				}
				
				parameter_types.push_back(param_type);
			}  // End of fallback for loop
		}  // End of if (actual_ctor) else block

		// Process constructor parameters: first handle stack overflow args, then register args
		const size_t max_int_regs = getMaxIntParamRegs<TWriterClass>();
		const size_t max_float_regs = getMaxFloatParamRegs<TWriterClass>();
		size_t shadow_space = getShadowSpaceSize<TWriterClass>();

		// First pass: identify and place stack arguments (params that don't fit in registers)
		// Register index 0 is used by 'this', so effective int reg capacity is max_int_regs - 1
		{
			size_t temp_int_idx = 1;  // Start at 1 because 'this' uses register 0
			size_t temp_float_idx = 0;
			size_t stack_arg_count = 0;

			for (size_t i = 0; i < num_params; ++i) {
				const TypedValue& arg = ctor_op.arguments[i];
				bool is_float_arg = (arg.type == Type::Float || arg.type == Type::Double) && !arg.is_reference();

				bool goes_on_stack = false;
				if (is_float_arg) {
					if (temp_float_idx >= max_float_regs) {
						goes_on_stack = true;
					}
					temp_float_idx++;
				} else {
					if (temp_int_idx >= max_int_regs) {
						goes_on_stack = true;
					}
					temp_int_idx++;
				}

				if (goes_on_stack) {
					int stack_offset = static_cast<int>(shadow_space + stack_arg_count * 8);

					if (is_float_arg) {
						X64Register temp_xmm = allocateXMMRegisterWithSpilling();
						if (std::holds_alternative<double>(arg.value)) {
							double float_value = std::get<double>(arg.value);
							uint64_t bits;
							if (arg.type == Type::Float) {
								float float_val = static_cast<float>(float_value);
								uint32_t float_bits;
								std::memcpy(&float_bits, &float_val, sizeof(float_bits));
								bits = float_bits;
							} else {
								std::memcpy(&bits, &float_value, sizeof(bits));
							}
							X64Register temp_gpr = allocateRegisterWithSpilling();
							emitMovImm64(temp_gpr, bits);
							emitMovqGprToXmm(temp_gpr, temp_xmm);
							regAlloc.release(temp_gpr);
						} else if (std::holds_alternative<TempVar>(arg.value)) {
							int var_offset = getStackOffsetFromTempVar(std::get<TempVar>(arg.value));
							emitFloatMovFromFrame(temp_xmm, var_offset, arg.type == Type::Float);
						} else if (std::holds_alternative<StringHandle>(arg.value)) {
							int var_offset = variable_scopes.back().variables[std::get<StringHandle>(arg.value)].offset;
							emitFloatMovFromFrame(temp_xmm, var_offset, arg.type == Type::Float);
						}
						emitFloatStoreToRSP(textSectionData, temp_xmm, stack_offset, arg.type == Type::Float);
						regAlloc.release(temp_xmm);
					} else {
						X64Register temp_reg = loadTypedValueIntoRegister(arg);
						emitStoreToRSP(textSectionData, temp_reg, stack_offset);
						regAlloc.release(temp_reg);
					}
					stack_arg_count++;
				}
			}
		}

		// Second pass: load register arguments
		// Integer regs: index 0 is 'this', so start at index 1 for first explicit param
		// Float regs: XMM0-XMM7 for floating-point parameters
		size_t int_reg_index = 1;  // Start at 1 because index 0 (RDI/RCX) is 'this' pointer
		size_t float_reg_index = 0;

		for (size_t i = 0; i < num_params; ++i) {
			const TypedValue& arg = ctor_op.arguments[i];
			Type paramType = arg.type;
			int paramSize = arg.size_in_bits;
			TypeIndex arg_type_index = arg.type_index;
			const IrValue& paramValue = arg.value;
			bool arg_is_reference = arg.is_reference();  // Check if marked as reference

			// Check if this is a floating-point parameter
			bool is_float_arg = (paramType == Type::Float || paramType == Type::Double);

			// Check if this is a reference parameter (copy/move constructor - same struct type, OR marked as reference)
			bool is_same_struct_type = false;
			if (struct_type_it != gTypesByName.end() && arg_type_index != 0) {
				is_same_struct_type = (arg_type_index == struct_type_it->second->type_index_);
			}
			bool is_reference_param = arg_is_reference || (num_params == 1 && paramType == Type::Struct && is_same_struct_type);

			// Determine which register to use based on parameter type
			if (is_float_arg && float_reg_index < max_float_regs) {
				// Use XMM register for floating-point parameters
				X64Register target_xmm = getFloatParamReg<TWriterClass>(float_reg_index++);

				// Handle floating-point immediate values (double literals)
				if (std::holds_alternative<double>(paramValue)) {
					double float_value = std::get<double>(paramValue);

					// Convert to appropriate bit pattern (float or double)
					uint64_t bits;
					if (paramType == Type::Float) {
						float float_val = static_cast<float>(float_value);
						uint32_t float_bits;
						std::memcpy(&float_bits, &float_val, sizeof(float_bits));
						bits = float_bits; // Zero-extend to 64-bit
					} else {
						std::memcpy(&bits, &float_value, sizeof(bits));
					}

					// Load bit pattern into temp GPR first
					X64Register temp_gpr = allocateRegisterWithSpilling();
					emitMovImm64(temp_gpr, bits);

					// Move from GPR to XMM register using movq
					emitMovqGprToXmm(temp_gpr, target_xmm);

					regAlloc.release(temp_gpr);
				} else if (std::holds_alternative<TempVar>(paramValue)) {
					// Load from temp variable
					const TempVar temp_var = std::get<TempVar>(paramValue);
					int param_offset = getStackOffsetFromTempVar(temp_var);
					bool is_float = (paramType == Type::Float);
					emitFloatMovFromFrame(target_xmm, param_offset, is_float);
				} else if (std::holds_alternative<StringHandle>(paramValue)) {
					// Load from variable
					StringHandle var_name_handle = std::get<StringHandle>(paramValue);
					auto it = variable_scopes.back().variables.find(var_name_handle);
					if (it != variable_scopes.back().variables.end()) {
						int param_offset = it->second.offset;
						bool is_float = (paramType == Type::Float);
						emitFloatMovFromFrame(target_xmm, param_offset, is_float);
					}
				}
			} else if (!is_float_arg && int_reg_index < max_int_regs) {
				// Use integer register for non-floating-point parameters
				X64Register target_reg = getIntParamReg<TWriterClass>(int_reg_index++);

				if (std::holds_alternative<unsigned long long>(paramValue)) {
					// Immediate value
					unsigned long long value = std::get<unsigned long long>(paramValue);
					
					// For 32-bit parameters, use 32-bit MOV to properly handle signed values
					// For negative values stored as 64-bit unsigned, truncate to 32-bit
					if (paramSize == 32) {
						uint32_t value32 = static_cast<uint32_t>(value);
						emitMovImm32(target_reg, value32);
					} else {
						// For 64-bit parameters or other sizes, use 64-bit MOV
						emitMovImm64(target_reg, value);
					}
				} else if (std::holds_alternative<TempVar>(paramValue)) {
					// Load from temp variable
					const TempVar temp_var = std::get<TempVar>(paramValue);
					int param_offset = getStackOffsetFromTempVar(temp_var);
					if (is_reference_param) {
						// For reference parameters, check if the temp var already holds a pointer
						// (e.g., from addressof operation). If so, load the pointer value (MOV),
						// otherwise take the address of the variable (LEA).
						auto ref_it = reference_stack_info_.find(param_offset);
						if (ref_it != reference_stack_info_.end()) {
							// Temp var holds a pointer - load it
							emitMovFromFrame(target_reg, param_offset);
						} else {
							// Temp var holds a value - take its address
							emitLeaFromFrame(target_reg, param_offset);
						}
					} else {
						// For value parameters: source (sized stack slot) -> dest (64-bit register)
						emitMovFromFrameSized(
							SizedRegister{target_reg, 64, false},  // dest: 64-bit register
							SizedStackSlot{param_offset, paramSize, isSignedType(paramType)}  // source: sized stack slot
						);
					}
				} else if (std::holds_alternative<StringHandle>(paramValue)) {
					// Load from variable
					StringHandle var_name_handle = std::get<StringHandle>(paramValue);
					auto it = variable_scopes.back().variables.find(var_name_handle);
					if (it != variable_scopes.back().variables.end()) {
						int param_offset = it->second.offset;
						// For large struct parameters (> 64 bits), pass by pointer according to System V AMD64 ABI
						// This includes std::initializer_list which is 128 bits (16 bytes)
						bool pass_by_pointer = is_reference_param || (paramType == Type::Struct && paramSize > 64);
						if (pass_by_pointer) {
							// For reference parameters or large structs, load address (LEA)
							// LEA target_reg, [RBP + param_offset]
							emitLeaFromFrame(target_reg, param_offset);
						} else {
							// For value parameters: source (sized stack slot) -> dest (64-bit register)
							emitMovFromFrameSized(
								SizedRegister{target_reg, 64, false},  // dest: 64-bit register
								SizedStackSlot{param_offset, paramSize, isSignedType(paramType)}  // source: sized stack slot
							);
						}
					}
				}
			}
			// Args that don't fit in registers were already placed on the stack in the first pass above
		}

		// Generate the call instruction
		// For constructors, the function name is the last component of the class name
		// For nested classes like "Outer::Inner", function_name="Inner" and class_name="Outer::Inner"
		std::string function_name;
		std::string class_name;
		size_t last_colon_pos = struct_name.rfind("::");
		if (last_colon_pos != std::string::npos) {
			// Nested class: "Outer::Inner" -> function="Inner", class="Outer::Inner" (full name)
			function_name = struct_name.substr(last_colon_pos + 2);
			class_name = struct_name;  // Keep full name for proper constructor detection
		} else {
			// Regular class: function_name = class_name = struct_name
			function_name = struct_name;
			class_name = struct_name;
			// Check if the struct's constructors are registered under a namespace-qualified name.
			// This happens when a struct is defined inside a namespace (e.g., std::my_type)
			// but the ctor_op.struct_name only has the unqualified name (e.g., "my_type").
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
			if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
				const StructTypeInfo* si = type_it->second->getStructInfo();
				if (si && !si->member_functions.empty()) {
					for (const auto& mf : si->member_functions) {
						if (mf.is_constructor && mf.function_decl.is<ConstructorDeclarationNode>()) {
							std::string_view ctor_struct = StringTable::getStringView(
								mf.function_decl.as<ConstructorDeclarationNode>().struct_name());
							if (!ctor_struct.empty() && ctor_struct.find("::") != std::string_view::npos) {
								class_name = std::string(ctor_struct);
								break;
							}
						}
					}
				}
			}
		}
		
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());

		// Build FunctionSignature for proper overload resolution
		TypeSpecifierNode void_return(Type::Void, TypeQualifier::None, 0, Token{});
		ObjectFileWriter::FunctionSignature sig(void_return, parameter_types);
		sig.class_name = class_name;

		// Generate the correct mangled name for this specific constructor overload
		auto mangled_name = writer.generateMangledName(function_name, sig);
		writer.add_relocation(textSectionData.size() - 4, mangled_name);
		
		// Invalidate caller-saved registers (function calls clobber them)
		regAlloc.invalidateCallerSavedRegisters();

		regAlloc.reset();
	}

	void handleDestructorCall(const IrInstruction& instruction) {
		// Destructor call format: DestructorCallOp {struct_name, object}
		const DestructorCallOp& dtor_op = instruction.getTypedPayload<DestructorCallOp>();

		flushAllDirtyRegisters();

		std::string_view struct_name = StringTable::getStringView(dtor_op.struct_name);

		// Get the object's stack offset
		int object_offset = 0;
		if (std::holds_alternative<TempVar>(dtor_op.object)) {
			const TempVar temp_var = std::get<TempVar>(dtor_op.object);
			object_offset = getStackOffsetFromTempVar(temp_var);
		} else {
			StringHandle var_name_handle = std::get<StringHandle>(dtor_op.object);
			const VariableInfo* var_info = findVariableInfo(var_name_handle);
			if (!var_info) {
				throw InternalError("Destructor call: variable not found in variables map: " + std::string(StringTable::getStringView(var_name_handle)));
			}
			object_offset = var_info->offset;
		}

		// Check if the object is a pointer (needs to be loaded, not addressed)
		// This includes 'this' pointer, TempVars from heap_alloc, and pointer variables from delete
		bool object_is_pointer = false;
		if (std::holds_alternative<TempVar>(dtor_op.object)) {
			// TempVars are always pointers in destructor calls (from heap_free)
			object_is_pointer = true;
		} else if (std::holds_alternative<StringHandle>(dtor_op.object)) {
			StringHandle obj_handle = std::get<StringHandle>(dtor_op.object);
			object_is_pointer = dtor_op.object_is_pointer || (StringTable::getStringView(obj_handle) == "this");
		}

		// Load the address of the object into the first parameter register ('this' pointer)
		// Use platform-specific register: RDI on Linux, RCX on Windows
		X64Register this_reg = getIntParamReg<TWriterClass>(0);
		if (object_is_pointer) {
			// For pointers (this, heap-allocated): reload the pointer value (not its address)
			// MOV this_reg, [RBP + object_offset]
			emitMovFromFrame(this_reg, object_offset);
		} else {
			// For regular stack objects: get the address
			// LEA this_reg, [RBP + object_offset]
			emitLeaFromFrame(this_reg, object_offset);
		}

		// Generate the call instruction
		// For nested classes, split "Outer::Inner" into class="Outer" and function="~Inner"
		std::string function_name;
		std::string class_name;
		size_t last_colon_pos = struct_name.rfind("::");
		if (last_colon_pos != std::string::npos) {
			// Nested class: "Outer::Inner" -> class="Outer", function="~Inner"
			class_name = std::string(struct_name.substr(0, last_colon_pos));
			function_name = std::string("~") + std::string(struct_name.substr(last_colon_pos + 2));
		} else {
			// Regular class: function_name = "~ClassName", class_name = struct_name
			function_name = std::string("~") + std::string(struct_name);
			class_name = std::string(struct_name);
		}
		
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());

		// Build FunctionSignature for destructor (destructors take no parameters and return void)
		std::vector<TypeSpecifierNode> empty_params;  // Destructors have no parameters
		TypeSpecifierNode void_return(Type::Void, TypeQualifier::None, 0, Token{});
		ObjectFileWriter::FunctionSignature sig(void_return, empty_params);
		sig.class_name = class_name;

		// Generate the correct mangled name for the destructor
		auto mangled_name = writer.generateMangledName(function_name, sig);
		writer.add_relocation(textSectionData.size() - 4, mangled_name);
		
		// Invalidate caller-saved registers (function calls clobber them)
		regAlloc.invalidateCallerSavedRegisters();

		regAlloc.reset();
	}

	void handleVirtualCall(const IrInstruction& instruction) {
		// Extract VirtualCallOp typed payload
		const VirtualCallOp& op = instruction.getTypedPayload<VirtualCallOp>();

		flushAllDirtyRegisters();

		// Get result offset
		assert(std::holds_alternative<TempVar>(op.result.value) && "VirtualCallOp result must be a TempVar");
		const TempVar& result_var = std::get<TempVar>(op.result.value);
		int result_offset = getStackOffsetFromTempVar(result_var);
		variable_scopes.back().variables[StringTable::getOrInternStringHandle(result_var.name())].offset = result_offset;

		// Get object offset
		int object_offset = 0;
		if (std::holds_alternative<TempVar>(op.object)) {
			const TempVar& temp_var = std::get<TempVar>(op.object);
			object_offset = getStackOffsetFromTempVar(temp_var);
		} else {
			StringHandle var_name_handle = std::get<StringHandle>(op.object);
			[[maybe_unused]] std::string_view var_name = StringTable::getStringView(var_name_handle);
			object_offset = variable_scopes.back().variables[var_name_handle].offset;
		}

		// Virtual call sequence varies based on whether object is a pointer or direct:
		// For pointers (is_pointer_access == true, e.g., ptr->method()):
		//   1. Load object pointer value → 2. Load vptr from [pointer] → 3. Load func from [vptr + index*8] → 4. Call
		// For direct objects (is_pointer_access == false, e.g., obj.method()):
		//   1. Get object address → 2. Load vptr from [address] → 3. Load func from [vptr + index*8] → 4. Call

		const X64Register this_reg = getIntParamReg<TWriterClass>(0); // First parameter register

		// Use is_pointer_access flag to determine if object is a pointer or direct object
		// Previously we used (op.object_size == 64) but that's wrong for small structs (like those with only vptr)
		bool is_pointer_object = op.is_pointer_access;

		if (is_pointer_object) {
			// Step 1a: Load pointer value from stack into this_reg
			// MOV this_reg, [RBP + object_offset]
			emitMovFromFrame(this_reg, object_offset);

			// Step 2a: Load vptr from object (dereference the pointer)
			// MOV RAX, [this_reg + 0]
			emitMovRegFromMemRegSized(X64Register::RAX, this_reg, 64);
		} else {
			// Step 1b: Load object address into this_reg
			// LEA this_reg, [RBP + object_offset]
			emitLeaFromFrame(this_reg, object_offset);

			// Step 2b: Load vptr from object (object address is in this_reg)
			// MOV RAX, [this_reg + 0]
			emitMovRegFromMemRegSized(X64Register::RAX, this_reg, 64);
		}

		// Step 3: Load function pointer from vtable into RAX
		// MOV RAX, [RAX + vtable_index * 8]
		int vtable_offset = op.vtable_index * 8;
		if (vtable_offset == 0) {
			// No offset, use simple dereference
			emitMovRegFromMemRegSized(X64Register::RAX, X64Register::RAX, 64);
		} else if (vtable_offset >= -128 && vtable_offset <= 127) {
			// Use 8-bit displacement
			emitMovRegFromMemRegDisp8(X64Register::RAX, X64Register::RAX, static_cast<int8_t>(vtable_offset));
		} else {
			// Use 32-bit displacement with emitMovFromMemory
			emitMovFromMemory(X64Register::RAX, X64Register::RAX, vtable_offset, 8);
		}

		// Step 4: 'this' pointer is already in the correct register from Step 1
		// No need to recalculate or reload - it's preserved throughout

		// Step 5: Handle additional function arguments (beyond 'this')
		// Virtual member functions have 'this' as first parameter (already in this_reg)
		// Additional arguments start at parameter index 1
		if (!op.arguments.empty()) {
			// Get platform-specific parameter counts
			size_t max_int_regs = getMaxIntParamRegs<TWriterClass>();
			size_t max_float_regs = getMaxFloatParamRegs<TWriterClass>();
			size_t shadow_space = getShadowSpaceSize<TWriterClass>();
			
			// Start at index 1 because 'this' is already in parameter register 0
			size_t int_reg_index = 1;
			size_t float_reg_index = 0;
			size_t stack_arg_count = 0;
			
			// First pass: handle stack arguments
			for (size_t i = 0; i < op.arguments.size(); ++i) {
				const auto& arg = op.arguments[i];
				bool is_float_arg = is_floating_point_type(arg.type);
				
				bool use_register = false;
				if (is_float_arg) {
					use_register = (float_reg_index < max_float_regs);
					float_reg_index++;
				} else {
					use_register = (int_reg_index < max_int_regs);
					int_reg_index++;
				}
				
				if (!use_register) {
					// Argument goes on stack
					int stack_offset = static_cast<int>(shadow_space + stack_arg_count * 8);
					X64Register temp_reg = loadTypedValueIntoRegister(arg);
					emitStoreToRSP(textSectionData, temp_reg, stack_offset);
					regAlloc.release(temp_reg);
					stack_arg_count++;
				}
			}
			
			// Second pass: handle register arguments
			int_reg_index = 1;  // Reset, 'this' is in register 0
			float_reg_index = 0;
			
			for (size_t i = 0; i < op.arguments.size(); ++i) {
				const auto& arg = op.arguments[i];
				bool is_float_arg = is_floating_point_type(arg.type);
				
				bool use_register = false;
				X64Register target_reg;
				if (is_float_arg) {
					if (float_reg_index < max_float_regs) {
						use_register = true;
						target_reg = getFloatParamReg<TWriterClass>(float_reg_index);
					}
					float_reg_index++;
				} else {
					if (int_reg_index < max_int_regs) {
						use_register = true;
						target_reg = getIntParamReg<TWriterClass>(int_reg_index);
					}
					int_reg_index++;
				}
				
				if (use_register) {
					// Load argument into parameter register
					if (is_float_arg) {
						// Handle float arguments
						if (std::holds_alternative<double>(arg.value)) {
							double float_value = std::get<double>(arg.value);
							uint64_t bits;
							if (arg.type == Type::Float) {
								float float_val = static_cast<float>(float_value);
								uint32_t float_bits;
								std::memcpy(&float_bits, &float_val, sizeof(float_bits));
								bits = float_bits;
							} else {
								std::memcpy(&bits, &float_value, sizeof(bits));
							}
							X64Register temp_gpr = allocateRegisterWithSpilling();
							emitMovImm64(temp_gpr, bits);
							emitMovqGprToXmm(temp_gpr, target_reg);
							regAlloc.release(temp_gpr);
						} else if (std::holds_alternative<TempVar>(arg.value)) {
							const auto& temp_var = std::get<TempVar>(arg.value);
							int var_offset = getStackOffsetFromTempVar(temp_var);
							bool is_float = (arg.type == Type::Float);
							emitFloatMovFromFrame(target_reg, var_offset, is_float);
						} else if (std::holds_alternative<StringHandle>(arg.value)) {
							StringHandle var_name_handle = std::get<StringHandle>(arg.value);
							int var_offset = variable_scopes.back().variables[var_name_handle].offset;
							bool is_float = (arg.type == Type::Float);
							emitFloatMovFromFrame(target_reg, var_offset, is_float);
						}
					} else {
						// Handle integer/pointer arguments
						if (std::holds_alternative<unsigned long long>(arg.value)) {
							uint64_t imm_value = std::get<unsigned long long>(arg.value);
							emitMovImm64(target_reg, imm_value);
						} else if (std::holds_alternative<TempVar>(arg.value)) {
							const auto& temp_var = std::get<TempVar>(arg.value);
							int var_offset = getStackOffsetFromTempVar(temp_var);
							emitMovFromFrame(target_reg, var_offset);
						} else if (std::holds_alternative<StringHandle>(arg.value)) {
							StringHandle var_name_handle = std::get<StringHandle>(arg.value);
							int var_offset = variable_scopes.back().variables[var_name_handle].offset;
							emitMovFromFrame(target_reg, var_offset);
						}
					}
				}
			}
		}

		// Step 6: Call through function pointer in RAX
		// CALL RAX
		textSectionData.push_back(0xFF); // CALL r/m64
		textSectionData.push_back(0xD0); // ModR/M: RAX

		// Step 7: Store return value from RAX to result variable using the correct size
		if (op.result.type != Type::Void) {
			emitMovToFrameSized(
				SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
				SizedStackSlot{result_offset, op.result.size_in_bits, isSignedType(op.result.type)}  // dest
			);
		}

		regAlloc.reset();
	}

	void handleHeapAlloc(const IrInstruction& instruction) {
		const HeapAllocOp& op = instruction.getTypedPayload<HeapAllocOp>();

		flushAllDirtyRegisters();

		// Call malloc(size)
		// Use platform-correct first parameter register (RDI on Linux, RCX on Windows)
		constexpr X64Register alloc_param_reg = getIntParamReg<TWriterClass>(0);

		// Move size into first parameter register
		emitMovImm64(alloc_param_reg, static_cast<uint64_t>(op.size_in_bytes));

		// Call malloc
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());
		writer.add_relocation(textSectionData.size() - 4, "malloc");
		
		// Invalidate caller-saved registers (function calls clobber them)
		regAlloc.invalidateCallerSavedRegisters();

		// Result is in RAX, store it to the result variable (pointer is always 64-bit)
		int result_offset = getStackOffsetFromTempVar(op.result);

		emitMovToFrameSized(
			SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
		);

		regAlloc.reset();
	}

	void handleHeapAllocArray(const IrInstruction& instruction) {
		const HeapAllocArrayOp& op = instruction.getTypedPayload<HeapAllocArrayOp>();

		flushAllDirtyRegisters();

		// Load count into RAX - handle TempVar, std::string_view (identifier), and constant values
		// Array counts are typically size_t (unsigned 64-bit on x64)
		if (std::holds_alternative<TempVar>(op.count)) {
			// Count is a TempVar - load from stack (assume 64-bit for size_t)
			TempVar count_var = std::get<TempVar>(op.count);
			int count_offset = getStackOffsetFromTempVar(count_var);
			emitMovFromFrameSized(
				SizedRegister{X64Register::RAX, 64, false},
				SizedStackSlot{count_offset, 64, false}  // size_t is 64-bit unsigned
			);
		} else if (std::holds_alternative<StringHandle>(op.count)) {
			// Count is an identifier (variable name) - load from stack
			StringHandle count_name_handle = std::get<StringHandle>(op.count);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(count_name_handle);
			if (it == current_scope.variables.end()) {
				throw InternalError("Array size variable not found in scope");
				return;
			}
			int count_offset = it->second.offset;
			emitMovFromFrameSized(
				SizedRegister{X64Register::RAX, 64, false},
				SizedStackSlot{count_offset, 64, false}  // size_t is 64-bit unsigned
			);
		} else if (std::holds_alternative<unsigned long long>(op.count)) {
			// Count is a constant - load immediate value
			uint64_t count_value = std::get<unsigned long long>(op.count);

			// MOV RAX, immediate
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0xB8); // MOV RAX, imm64
			for (int i = 0; i < 8; ++i) {
				textSectionData.push_back(static_cast<uint8_t>((count_value >> (i * 8)) & 0xFF));
			}
		} else {
			throw InternalError("Count must be TempVar, std::string_view, or unsigned long long");
		}

		// Multiply count by element_size: IMUL RAX, element_size
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x69); // IMUL r64, r/m64, imm32
		textSectionData.push_back(0xC0); // ModR/M: RAX, RAX
		for (int i = 0; i < 4; ++i) {
			textSectionData.push_back(static_cast<uint8_t>((op.size_in_bytes >> (i * 8)) & 0xFF));
		}

		if (op.needs_cookie) {
			// Add 8 bytes for the array count cookie: ADD RAX, 8
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x83); // ADD r/m64, imm8
			textSectionData.push_back(0xC0); // ModR/M: RAX
			textSectionData.push_back(0x08); // imm8 = 8
		}

		// Move result to first parameter register for malloc
		// Use platform-correct register (RDI on Linux, RCX on Windows)
		constexpr X64Register alloc_param_reg = getIntParamReg<TWriterClass>(0);
		emitMovRegReg(alloc_param_reg, X64Register::RAX);

		// Save count in RCX/RSI before clobbering it with the malloc call
		// We'll need it to store in the cookie after malloc returns.
		// Save count operand to a second parameter register (not clobbered by malloc result).
		// We reload the count after malloc since RAX is the only volatile we care about here.

		// Call malloc
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());
		writer.add_relocation(textSectionData.size() - 4, "malloc");
		
		// Invalidate caller-saved registers (function calls clobber them)
		regAlloc.invalidateCallerSavedRegisters();

		if (op.needs_cookie) {
			// Store the element count at [RAX]: MOV [RAX], count
			// Re-load the count into RCX (2nd param reg) or another temp register
			X64Register count_reg = getIntParamReg<TWriterClass>(1);  // RDX on Linux, RDX on Windows
			// Load count value into count_reg
			if (std::holds_alternative<TempVar>(op.count)) {
				TempVar count_var = std::get<TempVar>(op.count);
				int count_offset = getStackOffsetFromTempVar(count_var);
				emitMovFromFrameSized(SizedRegister{count_reg, 64, false}, SizedStackSlot{count_offset, 64, false});
			} else if (std::holds_alternative<StringHandle>(op.count)) {
				StringHandle count_name_handle = std::get<StringHandle>(op.count);
				auto it = variable_scopes.back().variables.find(count_name_handle);
				if (it != variable_scopes.back().variables.end()) {
					emitMovFromFrameSized(SizedRegister{count_reg, 64, false}, SizedStackSlot{it->second.offset, 64, false});
				}
			} else if (std::holds_alternative<unsigned long long>(op.count)) {
				emitMovImm64(count_reg, std::get<unsigned long long>(op.count));
			}
			// MOV QWORD PTR [RAX], count_reg
			emitStoreToMemory(textSectionData, count_reg, X64Register::RAX, 0, 8);
			// Advance RAX past the cookie: ADD RAX, 8
			textSectionData.push_back(0x48); // REX.W
			textSectionData.push_back(0x83); // ADD r/m64, imm8
			textSectionData.push_back(0xC0); // ModR/M: RAX
			textSectionData.push_back(0x08); // imm8 = 8
		}

		// Result is in RAX (user pointer, past cookie if applicable)
		int result_offset = getStackOffsetFromTempVar(op.result);

		emitMovToFrameSized(
			SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
		);

		regAlloc.reset();
	}

	void handleHeapFree(const IrInstruction& instruction) {
		const HeapFreeOp& op = instruction.getTypedPayload<HeapFreeOp>();

		flushAllDirtyRegisters();

		// Get the pointer offset (from either TempVar or identifier)
		int ptr_offset = 0;
		if (std::holds_alternative<TempVar>(op.pointer)) {
			TempVar ptr_var = std::get<TempVar>(op.pointer);
			ptr_offset = getStackOffsetFromTempVar(ptr_var);
		} else if (std::holds_alternative<StringHandle>(op.pointer)) {
			StringHandle var_name_handle = std::get<StringHandle>(op.pointer);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(var_name_handle);
			if (it == current_scope.variables.end()) {
				throw InternalError("Variable not found in scope");
				return;
			}
			ptr_offset = it->second.offset;
		} else {
			throw InternalError("HeapFree pointer must be TempVar or std::string_view");
			return;
		}

		// Load pointer from stack into first parameter register for free
		// Use platform-correct register (RDI on Linux, RCX on Windows)
		constexpr X64Register free_param_reg = getIntParamReg<TWriterClass>(0);
		emitMovFromFrame(free_param_reg, ptr_offset);

		// Call free
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());
		writer.add_relocation(textSectionData.size() - 4, "free");
		
		// Invalidate caller-saved registers (function calls clobber them)
		regAlloc.invalidateCallerSavedRegisters();

		regAlloc.reset();
	}

	void handleHeapFreeArray(const IrInstruction& instruction) {
		const HeapFreeArrayOp& op = instruction.getTypedPayload<HeapFreeArrayOp>();

		flushAllDirtyRegisters();

		// Get the pointer offset (from either TempVar or identifier)
		int ptr_offset = 0;
		if (std::holds_alternative<TempVar>(op.pointer)) {
			TempVar ptr_var = std::get<TempVar>(op.pointer);
			ptr_offset = getStackOffsetFromTempVar(ptr_var);
		} else if (std::holds_alternative<StringHandle>(op.pointer)) {
			StringHandle var_name_handle = std::get<StringHandle>(op.pointer);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(var_name_handle);
			if (it == current_scope.variables.end()) {
				throw InternalError("Variable not found in scope");
				return;
			}
			ptr_offset = it->second.offset;
		} else {
			throw InternalError("HeapFreeArray pointer must be TempVar or std::string_view");
			return;
		}

		// Load pointer from stack into first parameter register for free
		// Use platform-correct register (RDI on Linux, RCX on Windows)
		constexpr X64Register free_param_reg = getIntParamReg<TWriterClass>(0);
		emitMovFromFrame(free_param_reg, ptr_offset);

		if (op.has_cookie) {
			// Adjust pointer back past the cookie: SUB free_param_reg, 8
			uint8_t rex = 0x48;
			uint8_t rm = static_cast<uint8_t>(free_param_reg);
			if (rm >= 8) { rex |= 0x01; rm &= 0x07; }
			textSectionData.push_back(rex);
			textSectionData.push_back(0x83);           // SUB r/m64, imm8
			textSectionData.push_back(0xE8 | rm);      // ModR/M: mod=11 /5 rm=reg
			textSectionData.push_back(0x08);            // imm8 = 8
		}

		// Call free
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());
		writer.add_relocation(textSectionData.size() - 4, "free");
		
		// Invalidate caller-saved registers (function calls clobber them)
		regAlloc.invalidateCallerSavedRegisters();

		regAlloc.reset();
	}

	void handlePlacementNew(const IrInstruction& instruction) {
		const PlacementNewOp& op = instruction.getTypedPayload<PlacementNewOp>();

		flushAllDirtyRegisters();

		// Load the placement address into RAX
		// The address can be a TempVar, identifier, or constant
		if (std::holds_alternative<TempVar>(op.address)) {
			// Address is a TempVar - load from stack
			TempVar address_var = std::get<TempVar>(op.address);
			int address_offset = getStackOffsetFromTempVar(address_var);
			emitMovFromFrame(X64Register::RAX, address_offset);
		} else if (std::holds_alternative<StringHandle>(op.address)) {
			// Address is an identifier (variable name)
			StringHandle address_name_handle = std::get<StringHandle>(op.address);
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(address_name_handle);
			if (it == current_scope.variables.end()) {
				throw InternalError("Placement address variable not found in scope");
				return;
			}
			int address_offset = it->second.offset;
			// Arrays decay to pointers, so we compute their base address (LEA).
			// Regular pointer variables store an address value that needs to be loaded (MOV).
			if (it->second.is_array) {
				emitLeaFromFrame(X64Register::RAX, address_offset);
			} else {
				emitMovFromFrame(X64Register::RAX, address_offset);
			}
		} else if (std::holds_alternative<unsigned long long>(op.address)) {
			// Address is a constant - load immediate value
			uint64_t address_value = std::get<unsigned long long>(op.address);
			emitMovImm64(X64Register::RAX, address_value);
		} else {
			throw InternalError("Placement address must be TempVar, identifier, or unsigned long long");
			return;
		}

		// Store the placement address to the result variable (pointer is always 64-bit)
		// No malloc call - we just use the provided address
		int result_offset = getStackOffsetFromTempVar(op.result);
		emitMovToFrameSized(
			SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
		);

		regAlloc.reset();
	}

	void handleTypeid(const IrInstruction& instruction) {
		// Typeid: returns pointer to type_info
		auto& op = instruction.getTypedPayload<TypeidOp>();

		flushAllDirtyRegisters();

		if (op.is_type) {
			// typeid(Type) - compile-time constant
			// For now, return a dummy pointer (in a full implementation, we'd have a .rdata section with type_info)
			StringHandle type_name_handle = std::get<StringHandle>(op.operand);
			std::string_view type_name = StringTable::getStringView(type_name_handle);

			// Load address of type_info into RAX (using a placeholder address for now)
			// In a real implementation, we'd have a symbol for each type's RTTI data
			// Use a hash of the type name as a placeholder address
			size_t type_hash = std::hash<std::string_view>{}(type_name);
			emitMovImm64(X64Register::RAX, type_hash);
		} else {
			// typeid(expr) - may need runtime lookup for polymorphic types
			// For polymorphic types, RTTI pointer is at vtable[-1]
			// For non-polymorphic types, return compile-time constant

			// Load the expression result (should be a pointer to object)
			TempVar expr_var = std::get<TempVar>(op.operand);
			int expr_offset = getStackOffsetFromTempVar(expr_var);

			// Load object pointer into RAX
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x8B); // MOV r64, r/m64
			if (expr_offset >= -128 && expr_offset <= 127) {
				textSectionData.push_back(0x45); // ModR/M: RAX, [RBP + disp8]
				textSectionData.push_back(static_cast<uint8_t>(expr_offset));
			} else {
				textSectionData.push_back(0x85); // ModR/M: RAX, [RBP + disp32]
				for (int j = 0; j < 4; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(expr_offset & 0xFF));
					expr_offset >>= 8;
				}
			}

			// Load vtable pointer from object (first 8 bytes)
			// MOV RAX, [RAX]
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x8B); // MOV r64, r/m64
			textSectionData.push_back(0x00); // ModR/M: RAX, [RAX]

			// Load RTTI pointer from vtable[-1] (8 bytes before vtable)
			// MOV RAX, [RAX - 8]
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x8B); // MOV r64, r/m64
			textSectionData.push_back(0x40); // ModR/M: RAX, [RAX + disp8]
			textSectionData.push_back(static_cast<uint8_t>(-8)); // -8 offset
		}

		// Store result to stack
		int result_offset = getStackOffsetFromTempVar(op.result);
		textSectionData.push_back(0x48); // REX.W prefix
		textSectionData.push_back(0x89); // MOV r/m64, r64
		if (result_offset >= -128 && result_offset <= 127) {
			textSectionData.push_back(0x45); // ModR/M: [RBP + disp8], RAX
			textSectionData.push_back(static_cast<uint8_t>(result_offset));
		} else {
			textSectionData.push_back(0x85); // ModR/M: [RBP + disp32], RAX
			for (int j = 0; j < 4; ++j) {
				textSectionData.push_back(static_cast<uint8_t>(result_offset & 0xFF));
				result_offset >>= 8;
			}
		}

		regAlloc.reset();
	}

	void handleDynamicCast(const IrInstruction& instruction) {
		// DynamicCast: Returns nullptr for failed pointer casts, throws for failed reference casts
		auto& op = instruction.getTypedPayload<DynamicCastOp>();

		flushAllDirtyRegisters();

		// Mark that we need the dynamic_cast runtime helpers
		needs_dynamic_cast_runtime_ = true;

		// Implementation using auto-generated runtime helper __dynamic_cast_check
		// (Generated at end of compilation - see emit_dynamic_cast_check_function)
		// 
		// C++ equivalent logic:
		//   bool __dynamic_cast_check(RTTIInfo* source, RTTIInfo* target) {
		//     if (!source || !target) return false;
		//     if (source == target) return true;
		//     if (source->class_hash == target->class_hash) return true;
		//     // Check each base class recursively
		//     for (size_t i = 0; i < source->num_bases && i < 64; i++) {
		//       if (__dynamic_cast_check(source->base_ptrs[i], target)) return true;
		//     }
		//     return false;
		//   }
		//
		// Calling convention: Windows x64 (first 4 args in RCX, RDX, R8, R9)
		// Arguments:
		//   RCX = source RTTI pointer (loaded from vtable[-1])
		//   RDX = target RTTI pointer
		// Returns: RAX = 1 if cast is valid, 0 otherwise
		
		// Step 1: Load source pointer from stack
		int source_offset = getStackOffsetFromTempVar(op.source);
		emitMovFromFrame(X64Register::RAX, source_offset);

		// Step 2: Save source pointer to R8 (we'll need it later if cast succeeds)
		emitMovRegReg(X64Register::R8, X64Register::RAX);

		// Step 3: Check if source pointer is null
		emitTestRegReg(X64Register::RAX);

		// JZ to null_result (if source is null, return null)
		textSectionData.push_back(0x0F); // Two-byte opcode prefix
		textSectionData.push_back(0x84); // JZ rel32
		size_t null_check_offset = textSectionData.size();
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Step 4: Load vtable pointer from object (first 8 bytes)
		emitMovRegFromMemRegSized(X64Register::RAX, X64Register::RAX, 64);

		// Step 5: Load source RTTI pointer from vtable[-1] into first parameter register
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// Linux: First parameter in RDI
			emitMovRegFromMemRegDisp8(X64Register::RDI, X64Register::RAX, -8);
		} else {
			// Windows: First parameter in RCX
			emitMovRegFromMemRegDisp8(X64Register::RCX, X64Register::RAX, -8);
		}

		// Step 6: Load target RTTI pointer into second parameter register
		// Generate platform-specific RTTI symbol
		StringBuilder sb;
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// Linux/ELF: Use Itanium C++ ABI typeinfo symbol: _ZTI<length><classname>
			// Example: class "Derived" -> "_ZTI7Derived"
			sb.append("_ZTI");
			sb.append(op.target_type_name.length());
			sb.append(op.target_type_name);
		} else {
			// Windows/COFF: Use MSVC Complete Object Locator symbol: ??_R4.?AV<classname>@@6B@
			sb.append("??_R4.?AV");
			sb.append(op.target_type_name);
			sb.append("@@6B@");
		}
		std::string_view target_rtti_symbol = sb.commit();
		if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
			// Linux: Second parameter in RSI
			emitLeaRipRelativeWithRelocation(X64Register::RSI, target_rtti_symbol);
		} else {
			// Windows: Second parameter in RDX
			emitLeaRipRelativeWithRelocation(X64Register::RDX, target_rtti_symbol);
		}

		// Step 7: Call __dynamic_cast_check(source_rtti, target_rtti)
		if constexpr (!std::is_same_v<TWriterClass, ElfFileWriter>) {
			emitSubRSP(32);  // Shadow space for Windows x64 calling convention
		}
		emitCall("__dynamic_cast_check");
		if constexpr (!std::is_same_v<TWriterClass, ElfFileWriter>) {
			emitAddRSP(32);  // Restore stack
		}

		// Step 8: Check return value (RAX contains 0 or 1)
		emitTestAL();

		// JZ to null_result (if check failed, return null)
		textSectionData.push_back(0x0F); // Two-byte opcode prefix
		textSectionData.push_back(0x84); // JZ rel32
		size_t check_failed_offset = textSectionData.size();
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);

		// Step 9: Cast succeeded - return source pointer (which we saved in R8)
		emitMovRegReg(X64Register::RAX, X64Register::R8);

		// JMP to end
		textSectionData.push_back(0xEB); // JMP rel8
		size_t success_jmp_offset = textSectionData.size();
		textSectionData.push_back(0x00); // Placeholder

		// null_result label:
		size_t null_result_offset = textSectionData.size();
		
		// Check if this is a reference cast (needs to throw exception on failure)
		if (op.is_reference) {
			// For reference casts, throw std::bad_cast instead of returning nullptr
			// Call __dynamic_cast_throw_bad_cast (no arguments, never returns)
			if constexpr (!std::is_same_v<TWriterClass, ElfFileWriter>) {
				emitSubRSP(32);  // Shadow space for Windows x64 calling convention
			}
			emitCall("__dynamic_cast_throw_bad_cast");
			// Note: We don't restore RSP or add code after this because __dynamic_cast_throw_bad_cast never returns
		} else {
			// For pointer casts, return nullptr
			// XOR RAX, RAX  ; set result to nullptr
			textSectionData.push_back(0x48); // REX.W prefix
			textSectionData.push_back(0x31); // XOR r64, r64
			textSectionData.push_back(0xC0); // ModR/M: RAX, RAX
		}

		// end label:
		size_t end_offset = textSectionData.size();

		// Patch jump offsets
		int32_t null_check_delta = static_cast<int32_t>(null_result_offset - null_check_offset - 4);
		textSectionData[null_check_offset + 0] = static_cast<uint8_t>(null_check_delta & 0xFF);
		textSectionData[null_check_offset + 1] = static_cast<uint8_t>((null_check_delta >> 8) & 0xFF);
		textSectionData[null_check_offset + 2] = static_cast<uint8_t>((null_check_delta >> 16) & 0xFF);
		textSectionData[null_check_offset + 3] = static_cast<uint8_t>((null_check_delta >> 24) & 0xFF);

		int32_t check_failed_delta = static_cast<int32_t>(null_result_offset - check_failed_offset - 4);
		textSectionData[check_failed_offset + 0] = static_cast<uint8_t>(check_failed_delta & 0xFF);
		textSectionData[check_failed_offset + 1] = static_cast<uint8_t>((check_failed_delta >> 8) & 0xFF);
		textSectionData[check_failed_offset + 2] = static_cast<uint8_t>((check_failed_delta >> 16) & 0xFF);
		textSectionData[check_failed_offset + 3] = static_cast<uint8_t>((check_failed_delta >> 24) & 0xFF);

		int8_t success_jmp_delta = static_cast<int8_t>(end_offset - success_jmp_offset - 1);
		textSectionData[success_jmp_offset] = static_cast<uint8_t>(success_jmp_delta);

		// Step 10: Store result to stack (pointer is always 64-bit)
		int result_offset = getStackOffsetFromTempVar(op.result);
		emitMovToFrameSized(
			SizedRegister{X64Register::RAX, 64, false},  // source: 64-bit register
			SizedStackSlot{result_offset, 64, false}  // dest: 64-bit for pointer
		);

		regAlloc.reset();
	}

