	// Helper to convert internal try blocks to ObjectFileWriter format
	// Used during function finalization to prepare exception handling information
	std::pair<std::vector<ObjectFileWriter::TryBlockInfo>, std::vector<ObjectFileWriter::UnwindMapEntryInfo>>
	convertExceptionInfoToWriterFormat() {
		std::vector<ObjectFileWriter::TryBlockInfo> try_blocks;
		for (const auto& try_block : current_function_try_blocks_) {
			ObjectFileWriter::TryBlockInfo block_info;
			block_info.try_start_offset = try_block.try_start_offset;
			block_info.try_end_offset = try_block.try_end_offset;
			for (const auto& handler : try_block.catch_handlers) {
				ObjectFileWriter::CatchHandlerInfo handler_info;
				handler_info.type_index = static_cast<uint32_t>(handler.type_index);
				handler_info.handler_offset = handler.handler_offset;
				handler_info.handler_end_offset = handler.handler_end_offset;
				handler_info.funclet_entry_offset = handler.funclet_entry_offset;
				handler_info.funclet_end_offset = handler.funclet_end_offset;
				handler_info.is_catch_all = handler.is_catch_all;
				handler_info.is_const = handler.is_const;
				handler_info.is_reference = handler.is_reference;
				handler_info.is_rvalue_reference = handler.is_rvalue_reference;
				
				// Use pre-computed frame offset for caught exception object
				handler_info.catch_obj_offset = handler.catch_obj_stack_offset;
				
				// Get type name for type descriptor generation
				if (!handler.is_catch_all) {
					// For built-in types, use the Type enum; for user-defined types, use gTypeInfo
					if (handler.exception_type != Type::Void && handler.exception_type != Type::UserDefined && handler.exception_type != Type::Struct) {
						// Built-in type - get name from Type enum
						handler_info.type_name = getTypeName(handler.exception_type);
					} else if (handler.type_index < gTypeInfo.size()) {
						// User-defined type - get name from gTypeInfo
						handler_info.type_name = StringTable::getStringView(gTypeInfo[handler.type_index].name());
					}
				}
				
				block_info.catch_handlers.push_back(handler_info);
			}
			try_blocks.push_back(block_info);
		}
		
		std::vector<ObjectFileWriter::UnwindMapEntryInfo> unwind_map;
		for (const auto& unwind_entry : current_function_unwind_map_) {
			ObjectFileWriter::UnwindMapEntryInfo entry_info;
			entry_info.to_state = unwind_entry.to_state;
			entry_info.action = unwind_entry.action.isValid() ? std::string(StringTable::getStringView(unwind_entry.action)) : std::string();
			unwind_map.push_back(entry_info);
		}
		
		return {std::move(try_blocks), std::move(unwind_map)};
	}

	// Helper to convert internal SEH try blocks to ObjectFileWriter format
	// Used during function finalization to prepare SEH exception handling information
	std::vector<ObjectFileWriter::SehTryBlockInfo> convertSehInfoToWriterFormat() {
		std::vector<ObjectFileWriter::SehTryBlockInfo> seh_try_blocks;

		for (const auto& seh_try_block : current_function_seh_try_blocks_) {
			ObjectFileWriter::SehTryBlockInfo block_info;
			block_info.try_start_offset = seh_try_block.try_start_offset;
			block_info.try_end_offset = seh_try_block.try_end_offset;

			// Check if this try block has an __except handler
			if (seh_try_block.except_handler.has_value()) {
				block_info.has_except_handler = true;
				block_info.except_handler.handler_offset = seh_try_block.except_handler->handler_offset;
				block_info.except_handler.filter_result = seh_try_block.except_handler->filter_result;
				block_info.except_handler.is_constant_filter = seh_try_block.except_handler->is_constant_filter;
				block_info.except_handler.constant_filter_value = seh_try_block.except_handler->constant_filter_value;
				block_info.except_handler.filter_funclet_offset = seh_try_block.except_handler->filter_funclet_offset;
			} else {
				block_info.has_except_handler = false;
			}

			// Check if this try block has a __finally handler
			if (seh_try_block.finally_handler.has_value()) {
				block_info.has_finally_handler = true;
				block_info.finally_handler.handler_offset = seh_try_block.finally_handler->handler_offset;
			} else {
				block_info.has_finally_handler = false;
			}

			seh_try_blocks.push_back(block_info);
		}

		// Reverse order: innermost scope entries must come first in the scope table
		// __C_specific_handler walks entries linearly and for nested __try blocks,
		// inner handlers (__finally) must be processed before outer handlers (__except)
		std::reverse(seh_try_blocks.begin(), seh_try_blocks.end());

		return seh_try_blocks;
	}

	// Shared arithmetic operation context
	struct ArithmeticOperationContext {
		TypedValue result_value;
		X64Register result_physical_reg;
		X64Register rhs_physical_reg;
		Type operand_type;  // Type of the operands (for comparisons, different from result_value.type)
		int operand_size_in_bits;  // Size of the operands (for comparisons, different from result_value.size_in_bits)
	};

	// Setup and load operands for arithmetic operations - validates operands, extracts common data, and loads into registers
	// Helper function to generate REX prefix and ModR/M byte for register-to-register operations
	// x86-64 opcode extensions for instructions that encode the operation in the reg field of ModR/M
	enum class X64OpcodeExtension : uint8_t {
		ROL = 0,  // Rotate left
		ROR = 1,  // Rotate right
		RCL = 2,  // Rotate through carry left
		RCR = 3,  // Rotate through carry right
		SHL = 4,  // Shift left (same as SAL)
		SHR = 5,  // Shift right logical
		SAL = 6,  // Shift arithmetic left (same as SHL)
		SAR = 7,  // Shift arithmetic right
		
		TEST = 0, // TEST instruction (F6/F7)
		NOT = 2,  // NOT instruction
		NEG = 3,  // NEG instruction
		MUL = 4,  // Unsigned multiply
		IMUL = 5, // Signed multiply
		DIV = 6,  // Unsigned divide
		IDIV = 7  // Signed divide
	};

	// Used by arithmetic, bitwise, and comparison operations with R8-R15 support
	struct RegToRegEncoding {
		uint8_t rex_prefix;
		uint8_t modrm_byte;
	};

	// Enum for unary operations to enable helper function
	// BitwiseNot and Negate use opcode extensions 2 and 3 respectively
	enum class UnaryOperation {
		LogicalNot,
		BitwiseNot = 2,
		Negate = 3
	};
	
	RegToRegEncoding encodeRegToRegInstruction(X64Register reg_field, X64Register rm_field, bool include_rex_w = true) {
		RegToRegEncoding result;
		
		// Determine if we need REX prefix
		bool needs_rex = include_rex_w; // Always need REX for 64-bit (REX.W)
		
		// Start with appropriate REX prefix
		result.rex_prefix = include_rex_w ? 0x48 : 0x40; // REX.W for 64-bit, base REX for 32-bit
		
		// Set REX.R if reg_field (source in Reg field of ModR/M) is R8-R15
		if (static_cast<uint8_t>(reg_field) >= 8) {
			result.rex_prefix |= 0x04; // Set REX.R bit
			needs_rex = true;
		}
		
		// Set REX.B if rm_field (destination in R/M field of ModR/M) is R8-R15
		if (static_cast<uint8_t>(rm_field) >= 8) {
			result.rex_prefix |= 0x01; // Set REX.B bit
			needs_rex = true;
		}
		
		// If we don't need REX prefix (32-bit op with registers < 8), set to 0
		// The caller should check if rex_prefix is 0 and skip emitting it
		if (!needs_rex) {
			result.rex_prefix = 0;
		}
		
		// Build ModR/M byte: Mod=11 (register-to-register), Reg=reg_field[2:0], R/M=rm_field[2:0]
		result.modrm_byte = 0xC0 + 
			((static_cast<uint8_t>(reg_field) & 0x07) << 3) + 
			(static_cast<uint8_t>(rm_field) & 0x07);
		
		return result;
	}
	
	// Helper for instructions with opcode extension (reg field is a constant, rm is the register)
	// Used by shift instructions and division which encode the operation in the reg field
	void emitOpcodeExtInstruction(uint8_t opcode, X64OpcodeExtension opcode_ext, X64Register rm_field, int size_in_bits) {
		// Determine if we need REX.W based on operand size
		uint8_t rex_prefix = (size_in_bits == 64) ? 0x48 : 0x40;
		
		// Check if rm_field needs REX.B (registers R8-R15)
		if (static_cast<uint8_t>(rm_field) >= 8) {
			rex_prefix |= 0x01; // Set REX.B
		}
		
		// Build ModR/M byte: 11 (register mode) + opcode extension in reg field + rm bits
		uint8_t ext_value = static_cast<uint8_t>(opcode_ext);
		uint8_t modrm_byte = 0xC0 | ((ext_value & 0x07) << 3) | (static_cast<uint8_t>(rm_field) & 0x07);
		
		// Emit the instruction
		textSectionData.push_back(rex_prefix);
		textSectionData.push_back(opcode);
		textSectionData.push_back(modrm_byte);
	}
	
	// Helper function to emit a binary operation instruction (reg-to-reg)
	void emitBinaryOpInstruction(uint8_t opcode, X64Register src_reg, X64Register dst_reg, int size_in_bits) {
		// Determine if we need a REX prefix
		bool needs_rex = (size_in_bits == 64);  // Always need REX for 64-bit (REX.W)
		uint8_t rex_prefix = (size_in_bits == 64) ? 0x48 : 0x40;  // REX.W for 64-bit, base REX for 32-bit
		
		// Check if registers need REX extensions
		if (static_cast<uint8_t>(src_reg) >= 8) {
			rex_prefix |= 0x04; // Set REX.R for source (reg field)
			needs_rex = true;
		}
		if (static_cast<uint8_t>(dst_reg) >= 8) {
			rex_prefix |= 0x01; // Set REX.B for destination (rm field)
			needs_rex = true;
		}
		
		// Build ModR/M byte: 11 (register mode) + src in reg field + dst in rm field
		uint8_t modrm_byte = 0xC0 | ((static_cast<uint8_t>(src_reg) & 0x07) << 3) | (static_cast<uint8_t>(dst_reg) & 0x07);
		
		// Emit the instruction
		if (needs_rex) {
			textSectionData.push_back(rex_prefix);
		}
		textSectionData.push_back(opcode);
		textSectionData.push_back(modrm_byte);
	}
	
	// Helper function to emit MOV reg, reg instruction with size awareness
	void emitMovRegToReg(X64Register src_reg, X64Register dst_reg, int src_size_in_bits) {
		emitBinaryOpInstruction(0x89, src_reg, dst_reg, src_size_in_bits);
	}
	
	// Helper function to emit a comparison instruction (CMP + SETcc + MOVZX)
	void emitComparisonInstruction(const ArithmeticOperationContext& ctx, uint8_t setcc_opcode) {
		// Compare operands: CMP dst, src (opcode 0x39)
		// Use the operand size to determine whether to use 32-bit or 64-bit operation
		emitBinaryOpInstruction(0x39, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.operand_size_in_bits);

		// Set result based on condition: setcc r8
		// IMPORTANT: Always use REX prefix (at least 0x40) for byte operations
		// Without REX, registers 4-7 map to AH, CH, DH, BH (high bytes)
		// With REX, registers 4-7 map to SPL, BPL, SIL, DIL (low bytes)
		// For registers 8-15, we need REX.B (0x41)
		uint8_t setcc_rex = (static_cast<uint8_t>(ctx.result_physical_reg) >= 8) ? 0x41 : 0x40;
		textSectionData.push_back(setcc_rex);
		std::array<uint8_t, 3> setccInst = { 0x0F, setcc_opcode, static_cast<uint8_t>(0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) & 0x07)) };
		textSectionData.insert(textSectionData.end(), setccInst.begin(), setccInst.end());

		// Zero-extend the low byte to full register: movzx r64, r8
		auto movzx_encoding = encodeRegToRegInstruction(ctx.result_physical_reg, ctx.result_physical_reg);
		std::array<uint8_t, 4> movzxInst = { movzx_encoding.rex_prefix, 0x0F, 0xB6, movzx_encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), movzxInst.begin(), movzxInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	// Helper function to emit a floating-point comparison instruction (comiss/comisd + SETcc)
	// Consolidates the repeated pattern across handleFloatEqual, handleFloatNotEqual, etc.
	void emitFloatComparisonInstruction(ArithmeticOperationContext& ctx, uint8_t setcc_opcode) {
		// Use SSE comiss/comisd for comparison
		// Properly handles XMM8-XMM15 registers with REX prefix
		if (ctx.operand_type == Type::Float) {
			// comiss xmm1, xmm2 ([REX] 0F 2F /r)
			auto inst = generateSSEInstructionNoPrefix(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.operand_type == Type::Double) {
			// comisd xmm1, xmm2 (66 [REX] 0F 2F /r)
			auto inst = generateSSEInstructionDouble(0x0F, 0x2F, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Allocate a general-purpose register for the boolean result
		X64Register bool_reg = allocateRegisterWithSpilling();

		// Set result based on condition flags: SETcc r8
		// IMPORTANT: Always use REX prefix for byte operations to avoid high-byte registers
		uint8_t setcc_rex = (static_cast<uint8_t>(bool_reg) >= 8) ? REX_B : REX_BASE;
		textSectionData.push_back(setcc_rex);
		std::array<uint8_t, 3> setccInst = { 0x0F, setcc_opcode, static_cast<uint8_t>(0xC0 | (static_cast<uint8_t>(bool_reg) & 0x07)) };
		textSectionData.insert(textSectionData.end(), setccInst.begin(), setccInst.end());

		// Update context for boolean result (1 byte)
		ctx.result_value.type = Type::Bool;
		ctx.result_value.size_in_bits = 8;
		ctx.result_physical_reg = bool_reg;

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	// Helper function to load a global variable into a register
	// Handles both integer/pointer and floating-point types
	// Returns the allocated register, or X64Register::Count on error
	X64Register loadGlobalVariable(StringHandle var_handle, std::string_view var_name, 
	                                Type operand_type, int operand_size_in_bits, 
	                                std::optional<X64Register> exclude_reg = std::nullopt) {
		FLASH_LOG(Codegen, Debug, "StringHandle not found in local vars: '", var_name, "', checking global variables");
		
		const GlobalVariableInfo* global_info = nullptr;
		std::vector<const GlobalVariableInfo*> suffix_matches;
		
		for (const auto& global : global_variables_) {
			std::string_view global_name = StringTable::getStringView(global.name);
			
			// Match either exact name or qualified name ending with ::member_name
			// This handles cases like "value" matching "int_constant<-5>::value"
			if (global.name == var_handle) {
				global_info = &global;
				break;
			}
			
			// Check if global name ends with "::" + var_name using StringBuilder
			StringBuilder suffix_builder;
			suffix_builder.append("::"sv).append(var_name);
			std::string_view suffix = suffix_builder.preview();
			
			if (global_name.size() > suffix.size() &&
			    global_name.substr(global_name.size() - suffix.size()) == suffix) {
				suffix_matches.push_back(&global);
				FLASH_LOG(Codegen, Debug, "  Potential suffix match: '", global_name, "' ends with '", suffix, "'");
			}
			
			suffix_builder.reset();
		}
		
		// If no exact match but exactly one suffix match, use it
		if (!global_info && suffix_matches.size() == 1) {
			global_info = suffix_matches[0];
			FLASH_LOG(Codegen, Debug, "  Using unique suffix match: '", StringTable::getStringView(global_info->name), "'");
		} else if (!global_info && suffix_matches.size() > 1) {
			FLASH_LOG(Codegen, Warning, "  Ambiguous: ", suffix_matches.size(), " globals match suffix '", var_name, "'");
			
			// Try to disambiguate by preferring the shortest qualified name (most specific match)
			// This heuristic assumes that the most specific match (e.g., "Foo::value" over "ns::Foo::value")
			// is more likely to be the intended target in the current context
			const GlobalVariableInfo* best_match = suffix_matches[0];
			size_t shortest_length = StringTable::getStringView(best_match->name).size();
			
			for (const auto* candidate : suffix_matches) {
				size_t candidate_length = StringTable::getStringView(candidate->name).size();
				if (candidate_length < shortest_length) {
					best_match = candidate;
					shortest_length = candidate_length;
				}
			}
			
			global_info = best_match;
			FLASH_LOG(Codegen, Debug, "  Disambiguated to shortest match: '", StringTable::getStringView(global_info->name), "'");
		}
		
		if (!global_info) {
			FLASH_LOG(Codegen, Error, "Missing variable name: '", var_name, "', not in local or global scope");
			return X64Register::Count;
		}
		
		FLASH_LOG(Codegen, Debug, "Found global variable: '", StringTable::getStringView(global_info->name), "'");
		
		X64Register result_reg;
		
		// Handle floating-point vs integer/pointer types
		if (is_floating_point_type(operand_type)) {
			// For float/double, allocate an XMM register
			result_reg = allocateXMMRegisterWithSpilling();
			bool is_float = (operand_type == Type::Float);
			uint32_t reloc_offset = emitFloatMovRipRelative(result_reg, is_float);
			
			// Add pending relocation for this global variable reference
			pending_global_relocations_.push_back({reloc_offset, global_info->name, IMAGE_REL_AMD64_REL32});
		} else {
			// For integers/pointers, allocate a general-purpose register
			if (exclude_reg.has_value()) {
				result_reg = allocateRegisterWithSpilling(exclude_reg.value());
			} else {
				result_reg = allocateRegisterWithSpilling();
			}
			
			// Emit MOV instruction with RIP-relative addressing
			uint32_t reloc_offset = emitMovRipRelative(result_reg, operand_size_in_bits);
			
			// Add pending relocation for this global variable reference
			pending_global_relocations_.push_back({reloc_offset, global_info->name, IMAGE_REL_AMD64_REL32});
			
			regAlloc.flushSingleDirtyRegister(result_reg);
		}
		
		return result_reg;
	}

	ArithmeticOperationContext setupAndLoadArithmeticOperation(const IrInstruction& instruction, const char* operation_name) {
		const BinaryOp& bin_op = *getTypedPayload<BinaryOp>(instruction);
		
		// Determine result type based on operation
		// For comparisons, result is bool (8 bits for code generation)
		// For arithmetic operations, result type matches operand type
		Type result_type = bin_op.lhs.type;
		int result_size = bin_op.lhs.size_in_bits;
		
		auto opcode = instruction.getOpcode();
		bool is_comparison = (opcode == IrOpcode::Equal || opcode == IrOpcode::NotEqual ||
		                      opcode == IrOpcode::LessThan || opcode == IrOpcode::LessEqual ||
		                      opcode == IrOpcode::GreaterThan || opcode == IrOpcode::GreaterEqual ||
		                      opcode == IrOpcode::UnsignedLessThan || opcode == IrOpcode::UnsignedLessEqual ||
		                      opcode == IrOpcode::UnsignedGreaterThan || opcode == IrOpcode::UnsignedGreaterEqual ||
		                      opcode == IrOpcode::FloatEqual || opcode == IrOpcode::FloatNotEqual ||
		                      opcode == IrOpcode::FloatLessThan || opcode == IrOpcode::FloatLessEqual ||
		                      opcode == IrOpcode::FloatGreaterThan || opcode == IrOpcode::FloatGreaterEqual);
		
		// Store the operand type and size for register allocation and loading decisions
		Type operand_type = bin_op.lhs.type;
		int operand_size = bin_op.lhs.size_in_bits;
		
		if (is_comparison) {
			result_type = Type::Bool;
			result_size = 8;  // We store bool as 8 bits for register operations
		}
		
		// Create context with correct result type
		ArithmeticOperationContext ctx = {
			.result_value = TypedValue{result_type, result_size, bin_op.result},
			.result_physical_reg = X64Register::Count,
			.rhs_physical_reg = X64Register::RCX,
			.operand_type = operand_type,
			.operand_size_in_bits = operand_size
		};

		// Support integer, boolean, and floating-point operations
		if (!is_integer_type(ctx.result_value.type) && !is_bool_type(ctx.result_value.type) && !is_floating_point_type(ctx.result_value.type)) {
			throw std::runtime_error(std::string("Only integer/boolean/floating-point ") + operation_name + " is supported");
		}

		ctx.result_physical_reg = X64Register::Count;
		if (std::holds_alternative<StringHandle>(bin_op.lhs.value)) {
			StringHandle lhs_var_op = std::get<StringHandle>(bin_op.lhs.value);
			auto lhs_var_id = variable_scopes.back().variables.find(lhs_var_op);
			if (lhs_var_id != variable_scopes.back().variables.end()) {
				if (auto var_reg = regAlloc.tryGetStackVariableRegister(lhs_var_id->second.offset); var_reg.has_value()) {
					ctx.result_physical_reg = var_reg.value();	// value is already in a register, we can use it without a move!
				}
				else {
					assert(variable_scopes.back().scope_stack_space <= lhs_var_id->second.offset);

					if (is_floating_point_type(operand_type)) {
						// For float/double, allocate an XMM register
						ctx.result_physical_reg = allocateXMMRegisterWithSpilling();
						bool is_float = (operand_type == Type::Float);
						auto mov_opcodes = generateFloatMovFromFrame(ctx.result_physical_reg, lhs_var_id->second.offset, is_float);
						textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					} else {
						// Check if this is a reference - if so, we need to dereference it
						auto ref_it = reference_stack_info_.find(lhs_var_id->second.offset);
						if (ref_it != reference_stack_info_.end()) {
							// This is a reference - load the pointer first, then dereference
							ctx.result_physical_reg = allocateRegisterWithSpilling();
							// Load the pointer into the register
							emitMovFromFrame(ctx.result_physical_reg, lhs_var_id->second.offset);
							// Now dereference: load from [register + 0]
							int value_size_bytes = ref_it->second.value_size_bits / 8;
							emitMovFromMemory(ctx.result_physical_reg, ctx.result_physical_reg, 0, value_size_bytes);
						} else if (lhs_var_id->second.is_array) {
							// Source is an array - use LEA to get its address (array-to-pointer decay)
							ctx.result_physical_reg = allocateRegisterWithSpilling();
							emitLeaFromFrame(ctx.result_physical_reg, lhs_var_id->second.offset);
						} else {
							// Not a reference, load normally
							// For integers, use regular MOV
							ctx.result_physical_reg = allocateRegisterWithSpilling();
							emitMovFromFrameBySize(ctx.result_physical_reg, lhs_var_id->second.offset, ctx.operand_size_in_bits);
						}
						regAlloc.flushSingleDirtyRegister(ctx.result_physical_reg);
					}
				}
			}
			else {
				// Not found in local variables - check if it's a global variable
				std::string_view lhs_var_name = StringTable::getStringView(lhs_var_op);
				ctx.result_physical_reg = loadGlobalVariable(lhs_var_op, lhs_var_name, operand_type, ctx.operand_size_in_bits);
				
				if (ctx.result_physical_reg == X64Register::Count) {
					throw std::runtime_error("Missing variable name"); // TODO: Error handling
				}
			}
		}
		else if (std::holds_alternative<TempVar>(bin_op.lhs.value)) {
			auto lhs_var_op = std::get<TempVar>(bin_op.lhs.value);
			auto lhs_stack_var_addr = getStackOffsetFromTempVar(lhs_var_op, bin_op.lhs.size_in_bits);
			if (auto lhs_reg = regAlloc.tryGetStackVariableRegister(lhs_stack_var_addr); lhs_reg.has_value()) {
				ctx.result_physical_reg = lhs_reg.value();
			}
			else {
				assert(variable_scopes.back().scope_stack_space <= lhs_stack_var_addr);

				if (is_floating_point_type(operand_type)) {
					// For float/double, allocate an XMM register
					ctx.result_physical_reg = allocateXMMRegisterWithSpilling();
					bool is_float = (operand_type == Type::Float);
					auto mov_opcodes = generateFloatMovFromFrame(ctx.result_physical_reg, lhs_stack_var_addr, is_float);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				} else {
					// Check if this is a reference - if so, we need to dereference it
					auto ref_it = reference_stack_info_.find(lhs_stack_var_addr);
					
					// If not found with TempVar offset, try looking up by name
					if (ref_it == reference_stack_info_.end()) {
						std::string_view var_name = lhs_var_op.name();
						// Remove the '%' prefix if present
						if (!var_name.empty() && var_name[0] == '%') {
							var_name = var_name.substr(1);
						}
						auto named_var_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(var_name));
						if (named_var_it != variable_scopes.back().variables.end()) {
							int32_t named_offset = named_var_it->second.offset;
							ref_it = reference_stack_info_.find(named_offset);
							if (ref_it != reference_stack_info_.end()) {
								// Found it! Update lhs_stack_var_addr to use the named variable offset
								lhs_stack_var_addr = named_offset;
							}
						}
					}
					
					if (ref_it != reference_stack_info_.end() && !ref_it->second.holds_address_only) {
						// This is a reference - load the pointer first, then dereference
						ctx.result_physical_reg = allocateRegisterWithSpilling();
						// Load the pointer into the register
						auto load_ptr = generatePtrMovFromFrame(ctx.result_physical_reg, lhs_stack_var_addr);
						textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(), load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
						// Now dereference: load from [register + 0]
						int value_size_bits = ref_it->second.value_size_bits;
						OpCodeWithSize deref_opcodes;
						if (value_size_bits == 64) {
							deref_opcodes = generateMovFromMemory(ctx.result_physical_reg, ctx.result_physical_reg, 0);
						} else if (value_size_bits == 32) {
							deref_opcodes = generateMovFromMemory32(ctx.result_physical_reg, ctx.result_physical_reg, 0);
						} else if (value_size_bits == 16) {
							deref_opcodes = generateMovFromMemory16(ctx.result_physical_reg, ctx.result_physical_reg, 0);
						} else if (value_size_bits == 8) {
							deref_opcodes = generateMovFromMemory8(ctx.result_physical_reg, ctx.result_physical_reg, 0);
						} else {
							// Unsupported size - return default context
							FLASH_LOG_FORMAT(Codegen, Warning, "handleBinaryOp: Unsupported reference value size {} bits, skipping", value_size_bits);
							return ctx;
						}
						textSectionData.insert(textSectionData.end(), deref_opcodes.op_codes.begin(), deref_opcodes.op_codes.begin() + deref_opcodes.size_in_bytes);
					} else if (ref_it != reference_stack_info_.end() && ref_it->second.holds_address_only) {
						// This holds an address value directly (from addressof) - load without dereferencing
						ctx.result_physical_reg = allocateRegisterWithSpilling();
						auto load_ptr = generatePtrMovFromFrame(ctx.result_physical_reg, lhs_stack_var_addr);
						textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(), load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
					} else {
						// Not a reference, load normally with correct size
						ctx.result_physical_reg = allocateRegisterWithSpilling();
						emitMovFromFrameBySize(ctx.result_physical_reg, lhs_stack_var_addr, ctx.operand_size_in_bits);
					}
					regAlloc.flushSingleDirtyRegister(ctx.result_physical_reg);
				}
			}
		}
		else if (std::holds_alternative<unsigned long long>(bin_op.lhs.value)) {
			// LHS is a literal value
			auto lhs_value = std::get<unsigned long long>(bin_op.lhs.value);
			ctx.result_physical_reg = allocateRegisterWithSpilling();

			// Load the literal value into the register
			// Use the correct operand size for the move instruction
			uint8_t reg_num = static_cast<uint8_t>(ctx.result_physical_reg);
			
			if (ctx.operand_size_in_bits == 64) {
				// 64-bit: mov reg, imm64 with REX.W
				uint8_t rex_prefix = 0x48; // REX.W
				
				// For R8-R15, set REX.B bit
				if (reg_num >= 8) {
					rex_prefix |= 0x01; // Set REX.B
					reg_num &= 0x07; // Use lower 3 bits for opcode
				}
				
				std::array<uint8_t, 10> movInst = { rex_prefix, static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0, 0, 0, 0, 0 };
				std::memcpy(&movInst[2], &lhs_value, sizeof(lhs_value));
				textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
			} else {
				// 32-bit (or smaller): mov r32, imm32
				// Only use REX if we need extended registers (R8-R15)
				bool needs_rex = (reg_num >= 8);
				
				if (needs_rex) {
					uint8_t rex_prefix = 0x40; // Base REX (no REX.W for 32-bit)
					rex_prefix |= 0x01; // Set REX.B
					textSectionData.push_back(rex_prefix);
					reg_num &= 0x07; // Use lower 3 bits for opcode
				}
				
				// mov r32, imm32: opcode B8+r, imm32
				textSectionData.push_back(static_cast<uint8_t>(0xB8 + reg_num));
				uint32_t imm32 = static_cast<uint32_t>(lhs_value);
				textSectionData.push_back(imm32 & 0xFF);
				textSectionData.push_back((imm32 >> 8) & 0xFF);
				textSectionData.push_back((imm32 >> 16) & 0xFF);
				textSectionData.push_back((imm32 >> 24) & 0xFF);
			}
		}
		else if (instruction.isOperandType<double>(3)) {
			// LHS is a floating-point literal value
			auto lhs_value = instruction.getOperandAs<double>(3);
			ctx.result_physical_reg = allocateXMMRegisterWithSpilling();

			// For floating-point, we need to load the value into an XMM register
			// Strategy: Load the bit pattern as integer into a GPR, then move to XMM
			// 1. Load double bits into a GPR using movabs
			// 2. Move from GPR to XMM using movq

			uint64_t bits;
			std::memcpy(&bits, &lhs_value, sizeof(bits));

			// Allocate a temporary GPR for the bit pattern
			X64Register temp_gpr = allocateRegisterWithSpilling();

			// movabs temp_gpr, imm64 (load bit pattern)
			uint8_t rex_prefix = 0x48; // REX.W
			uint8_t reg_num = static_cast<uint8_t>(temp_gpr);
			
			// For R8-R15, set REX.B bit
			if (reg_num >= 8) {
				rex_prefix |= 0x01; // Set REX.B
				reg_num &= 0x07; // Use lower 3 bits for opcode
			}
			
			std::array<uint8_t, 10> movInst = { rex_prefix, static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0, 0, 0, 0, 0 };
			std::memcpy(&movInst[2], &bits, sizeof(bits));
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());

			// movq xmm, r64 (66 REX.W 0F 6E /r) - move from GPR to XMM
			std::array<uint8_t, 5> movqInst = { 0x66, 0x48, 0x0F, 0x6E, 0xC0 };
			movqInst[4] = 0xC0 + (xmm_modrm_bits(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(temp_gpr);
			textSectionData.insert(textSectionData.end(), movqInst.begin(), movqInst.end());

			// Release the temporary GPR
			regAlloc.release(temp_gpr);
		}
		
		ctx.rhs_physical_reg = X64Register::Count;
		if (std::holds_alternative<StringHandle>(bin_op.rhs.value)) {
			StringHandle rhs_var_op = std::get<StringHandle>(bin_op.rhs.value);
			auto rhs_var_id = variable_scopes.back().variables.find(rhs_var_op);
			if (rhs_var_id != variable_scopes.back().variables.end()) {
				if (auto var_reg = regAlloc.tryGetStackVariableRegister(rhs_var_id->second.offset); var_reg.has_value()) {
					ctx.rhs_physical_reg = var_reg.value();	// value is already in a register, we can use it without a move!
				}
				else {
					assert(variable_scopes.back().scope_stack_space <= rhs_var_id->second.offset);

					if (is_floating_point_type(operand_type)) {
						// For float/double, allocate an XMM register
						ctx.rhs_physical_reg = allocateXMMRegisterWithSpilling();
						bool is_float = (operand_type == Type::Float);
						auto mov_opcodes = generateFloatMovFromFrame(ctx.rhs_physical_reg, rhs_var_id->second.offset, is_float);
						textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					} else {
						// Check if this is a reference - if so, we need to dereference it
						auto ref_it = reference_stack_info_.find(rhs_var_id->second.offset);
						if (ref_it != reference_stack_info_.end()) {
							// This is a reference - load the pointer first, then dereference
							ctx.rhs_physical_reg = allocateRegisterWithSpilling();
							
							// If RHS register conflicts with result register, we need to handle it
							// Strategy: Keep LHS in its register, allocate a fresh register for RHS
							if (ctx.rhs_physical_reg == ctx.result_physical_reg) {
								// Allocate a NEW register for RHS, excluding the LHS register
								ctx.rhs_physical_reg = allocateRegisterWithSpilling(ctx.result_physical_reg);
							}
							
							// Load the pointer into the register
							emitMovFromFrame(ctx.rhs_physical_reg, rhs_var_id->second.offset);
							// Now dereference: load from [register + 0]
							int value_size_bytes = ref_it->second.value_size_bits / 8;
							emitMovFromMemory(ctx.rhs_physical_reg, ctx.rhs_physical_reg, 0, value_size_bytes);
						} else {
							// Not a reference, load normally
							// For integers, use regular MOV
							ctx.rhs_physical_reg = allocateRegisterWithSpilling();
							
							// If RHS register conflicts with result register, we need to handle it
							// Strategy: Keep LHS in its register, allocate a fresh register for RHS
							if (ctx.rhs_physical_reg == ctx.result_physical_reg) {
								// Allocate a NEW register for RHS, excluding the LHS register
								ctx.rhs_physical_reg = allocateRegisterWithSpilling(ctx.result_physical_reg);
							}
							
							// Use the RHS's actual size for loading, not the LHS/operand size
							// This is important when types are mixed (e.g., int + long)
							emitMovFromFrameBySize(ctx.rhs_physical_reg, rhs_var_id->second.offset, bin_op.rhs.size_in_bits);
						}
						regAlloc.flushSingleDirtyRegister(ctx.rhs_physical_reg);
					}
				}
			}
			else {
				// Not found in local variables - check if it's a global variable
				std::string_view rhs_var_name = StringTable::getStringView(rhs_var_op);
				ctx.rhs_physical_reg = loadGlobalVariable(rhs_var_op, rhs_var_name, operand_type, bin_op.rhs.size_in_bits, ctx.result_physical_reg);
				
				if (ctx.rhs_physical_reg == X64Register::Count) {
					throw std::runtime_error("Missing variable name"); // TODO: Error handling
				}
			}
		}
		else if (std::holds_alternative<TempVar>(bin_op.rhs.value)) {
			auto rhs_var_op = std::get<TempVar>(bin_op.rhs.value);
			auto rhs_stack_var_addr = getStackOffsetFromTempVar(rhs_var_op, bin_op.rhs.size_in_bits);
			if (auto rhs_reg = regAlloc.tryGetStackVariableRegister(rhs_stack_var_addr); rhs_reg.has_value()) {
				ctx.rhs_physical_reg = rhs_reg.value();
			}
			else {
				assert(variable_scopes.back().scope_stack_space <= rhs_stack_var_addr);

				if (is_floating_point_type(operand_type)) {
					// For float/double, allocate an XMM register
					ctx.rhs_physical_reg = allocateXMMRegisterWithSpilling();
					bool is_float = (operand_type == Type::Float);
					auto mov_opcodes = generateFloatMovFromFrame(ctx.rhs_physical_reg, rhs_stack_var_addr, is_float);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				} else {
					// Check if this is a reference - if so, we need to dereference it
					auto ref_it = reference_stack_info_.find(rhs_stack_var_addr);
					
					// If not found with TempVar offset, try looking up by name
					if (ref_it == reference_stack_info_.end()) {
						std::string_view var_name = rhs_var_op.name();
						// Remove the '%' prefix if present
						if (!var_name.empty() && var_name[0] == '%') {
							var_name = var_name.substr(1);
						}
						auto named_var_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(var_name));
						if (named_var_it != variable_scopes.back().variables.end()) {
							int32_t named_offset = named_var_it->second.offset;
							ref_it = reference_stack_info_.find(named_offset);
							if (ref_it != reference_stack_info_.end()) {
								// Found it! Update rhs_stack_var_addr to use the named variable offset
								rhs_stack_var_addr = named_offset;
							}
						}
					}
					
					if (ref_it != reference_stack_info_.end()) {
						// This is a reference - load the pointer first, then dereference
						ctx.rhs_physical_reg = allocateRegisterWithSpilling();
						
						// If RHS register conflicts with result register, we need to handle it
						// Strategy: Keep LHS in its register, allocate a fresh register for RHS
						if (ctx.rhs_physical_reg == ctx.result_physical_reg) {
							// Allocate a NEW register for RHS, excluding the LHS register
							ctx.rhs_physical_reg = allocateRegisterWithSpilling(ctx.result_physical_reg);
						}
						
						// Load the pointer into the register
						emitMovFromFrame(ctx.rhs_physical_reg, rhs_stack_var_addr);
						// Now dereference: load from [register + 0]
						int value_size_bytes = ref_it->second.value_size_bits / 8;
						emitMovFromMemory(ctx.rhs_physical_reg, ctx.rhs_physical_reg, 0, value_size_bytes);
					} else {
						// Not a reference, load normally with correct size
						ctx.rhs_physical_reg = allocateRegisterWithSpilling();
						
						// If RHS register conflicts with result register, we need to handle it
						// Strategy: Keep LHS in its register, allocate a fresh register for RHS
						if (ctx.rhs_physical_reg == ctx.result_physical_reg) {
							// Allocate a NEW register for RHS, excluding the LHS register
							ctx.rhs_physical_reg = allocateRegisterWithSpilling(ctx.result_physical_reg);
						}
						
						// Use the RHS's actual size for loading, not the LHS/operand size
						// This is important when types are mixed (e.g., int + long)
						emitMovFromFrameBySize(ctx.rhs_physical_reg, rhs_stack_var_addr, bin_op.rhs.size_in_bits);
					}
					regAlloc.flushSingleDirtyRegister(ctx.rhs_physical_reg);
				}
			}
		}
		else if (std::holds_alternative<unsigned long long>(bin_op.rhs.value)) {
			// RHS is a literal value
			auto rhs_value = std::get<unsigned long long>(bin_op.rhs.value);
			ctx.rhs_physical_reg = allocateRegisterWithSpilling();

			// If RHS register conflicts with result register, we need to handle it
			// Strategy: Keep LHS in its register, allocate a fresh register for RHS
			if (ctx.rhs_physical_reg == ctx.result_physical_reg) {
				// Allocate a NEW register for RHS, excluding the LHS register
				ctx.rhs_physical_reg = allocateRegisterWithSpilling(ctx.result_physical_reg);
			}

			// Load the literal value into the register
			// Use the correct operand size for the move instruction
			uint8_t reg_num = static_cast<uint8_t>(ctx.rhs_physical_reg);
			
			if (ctx.operand_size_in_bits == 64) {
				// 64-bit: mov reg, imm64 with REX.W
				uint8_t rex_prefix = 0x48; // REX.W
				
				// For R8-R15, set REX.B bit
				if (reg_num >= 8) {
					rex_prefix |= 0x01; // Set REX.B
					reg_num &= 0x07; // Use lower 3 bits for opcode
				}
				
				std::array<uint8_t, 10> movInst = { rex_prefix, static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0, 0, 0, 0, 0 };
				std::memcpy(&movInst[2], &rhs_value, sizeof(rhs_value));
				textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
			} else {
				// 32-bit (or smaller): mov r32, imm32
				// Only use REX if we need extended registers (R8-R15)
				bool needs_rex = (reg_num >= 8);
				
				if (needs_rex) {
					uint8_t rex_prefix = 0x40; // Base REX (no REX.W for 32-bit)
					rex_prefix |= 0x01; // Set REX.B
					textSectionData.push_back(rex_prefix);
					reg_num &= 0x07; // Use lower 3 bits for opcode
				}
				
				// mov r32, imm32: opcode B8+r, imm32
				textSectionData.push_back(static_cast<uint8_t>(0xB8 + reg_num));
				uint32_t imm32 = static_cast<uint32_t>(rhs_value);
				textSectionData.push_back(imm32 & 0xFF);
				textSectionData.push_back((imm32 >> 8) & 0xFF);
				textSectionData.push_back((imm32 >> 16) & 0xFF);
				textSectionData.push_back((imm32 >> 24) & 0xFF);
			}
		}
		else if (std::holds_alternative<double>(bin_op.rhs.value)) {
			// RHS is a floating-point literal value
			auto rhs_value = std::get<double>(bin_op.rhs.value);
			ctx.rhs_physical_reg = allocateXMMRegisterWithSpilling();

			// For floating-point, we need to load the value into an XMM register
			// Strategy: Load the bit pattern as integer into a GPR, then move to XMM
			// 1. Load bits into a GPR using movabs
			// 2. Move from GPR to XMM using movq (for double) or movd (for float)

			// Allocate a temporary GPR for the bit pattern
			X64Register temp_gpr = allocateRegisterWithSpilling();

			if (operand_type == Type::Float) {
				// For float (single precision), convert double to float and get 32-bit representation
				float float_value = static_cast<float>(rhs_value);
				uint32_t bits;
				std::memcpy(&bits, &float_value, sizeof(bits));

				// mov temp_gpr_32, imm32 (load 32-bit bit pattern)
				uint8_t reg_num = static_cast<uint8_t>(temp_gpr);
				
				// For R8-R15, we need a REX prefix with REX.B set
				if (reg_num >= 8) {
					textSectionData.push_back(0x41); // REX.B
					reg_num &= 0x07; // Use lower 3 bits for opcode
				}
				
				std::array<uint8_t, 5> movInst = { static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0 };
				std::memcpy(&movInst[1], &bits, sizeof(bits));
				textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());

				// movd xmm, r32 (66 0F 6E /r) - move 32-bit from GPR to XMM
				std::array<uint8_t, 4> movdInst = { 0x66, 0x0F, 0x6E, 0xC0 };
				// Add REX prefix if either XMM or GPR is extended
				uint8_t xmm_num = xmm_modrm_bits(ctx.rhs_physical_reg);
				uint8_t gpr_num = static_cast<uint8_t>(temp_gpr);
				if (xmm_num >= 8 || gpr_num >= 8) {
					uint8_t rex = 0x40;
					if (xmm_num >= 8) rex |= 0x04; // REX.R
					if (gpr_num >= 8) rex |= 0x01; // REX.B
					textSectionData.push_back(rex);
				}
				movdInst[3] = 0xC0 + ((xmm_num & 0x07) << 3) + (gpr_num & 0x07);
				textSectionData.insert(textSectionData.end(), movdInst.begin(), movdInst.end());
			} else {
				// For double, load 64-bit representation
				uint64_t bits;
				std::memcpy(&bits, &rhs_value, sizeof(bits));

				// movabs temp_gpr, imm64 (load bit pattern)
				uint8_t rex_prefix = 0x48; // REX.W
				uint8_t reg_num = static_cast<uint8_t>(temp_gpr);
				
				// For R8-R15, set REX.B bit
				if (reg_num >= 8) {
					rex_prefix |= 0x01; // Set REX.B
					reg_num &= 0x07; // Use lower 3 bits for opcode
				}
				
				std::array<uint8_t, 10> movInst = { rex_prefix, static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0, 0, 0, 0, 0 };
				std::memcpy(&movInst[2], &bits, sizeof(bits));
				textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());

				// movq xmm, r64 (66 REX.W 0F 6E /r) - move from GPR to XMM
				std::array<uint8_t, 5> movqInst = { 0x66, 0x48, 0x0F, 0x6E, 0xC0 };
				movqInst[4] = 0xC0 + (xmm_modrm_bits(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(temp_gpr);
				textSectionData.insert(textSectionData.end(), movqInst.begin(), movqInst.end());
			}

			// Release the temporary GPR
			regAlloc.release(temp_gpr);
		}
		
		// If result register hasn't been allocated yet (e.g., LHS is a literal), allocate one now
		if (ctx.result_physical_reg == X64Register::Count) {
			if (is_floating_point_type(ctx.result_value.type)) {
				ctx.result_physical_reg = allocateXMMRegisterWithSpilling();
			} else {
				ctx.result_physical_reg = allocateRegisterWithSpilling();
			}
		}

		if (std::holds_alternative<TempVar>(ctx.result_value.value)) {
			const TempVar temp_var = std::get<TempVar>(ctx.result_value.value);
			const int32_t stack_offset = getStackOffsetFromTempVar(temp_var);
			StringHandle reassign_handle = StringTable::getOrInternStringHandle(temp_var.name());
			variable_scopes.back().variables[reassign_handle].offset = stack_offset;
			// Only set stack variable offset for allocated registers (not XMM0/XMM1 used directly)
			if (ctx.result_physical_reg < X64Register::XMM0 || regAlloc.is_allocated(ctx.result_physical_reg)) {
				// IMPORTANT: Before reassigning this register to the result TempVar's offset,
				// we must flush its current value to the OLD offset if it was dirty.
				// This happens when the LHS operand was in a register that we're reusing for the result.
				// Without flushing, the LHS value would be lost (crucial for post-increment).
				auto& reg_info = regAlloc.registers[static_cast<int>(ctx.result_physical_reg)];
				if (reg_info.isDirty && reg_info.stackVariableOffset != INT_MIN && 
				    reg_info.stackVariableOffset != stack_offset) {
					FLASH_LOG_FORMAT(Codegen, Debug, "FLUSHING dirty reg {} from old offset {} to new offset {}, size={}", 
						static_cast<int>(ctx.result_physical_reg), reg_info.stackVariableOffset, stack_offset, reg_info.size_in_bits);
					// Use the actual register size from reg_info, not hardcoded 64 bits
					emitMovToFrameSized(
						SizedRegister{ctx.result_physical_reg, static_cast<uint8_t>(reg_info.size_in_bits), false},
						SizedStackSlot{reg_info.stackVariableOffset, reg_info.size_in_bits, false}
					);
				}
				regAlloc.set_stack_variable_offset(ctx.result_physical_reg, stack_offset, ctx.result_value.size_in_bits);
			}
		}

		// Final safety check: if LHS and RHS ended up in the same register, we need to fix it
		// This can happen when all registers are in use and spilling picks the same register twice
		if (ctx.result_physical_reg == ctx.rhs_physical_reg && !is_floating_point_type(ctx.result_value.type)) {
			// Get the LHS variable's stack location and reload it into a different register
			auto& reg_info = regAlloc.registers[static_cast<int>(ctx.result_physical_reg)];
			if (reg_info.stackVariableOffset != INT_MIN) {
				// Allocate a fresh register for LHS and reload it from the stack
				X64Register new_lhs_reg = allocateRegisterWithSpilling();
				emitMovFromFrameBySize(new_lhs_reg, reg_info.stackVariableOffset, reg_info.size_in_bits);
				
				// Update tracking: the new register now holds the LHS variable
				regAlloc.set_stack_variable_offset(new_lhs_reg, reg_info.stackVariableOffset, reg_info.size_in_bits);
				regAlloc.registers[static_cast<int>(new_lhs_reg)].isDirty = reg_info.isDirty;
				
				// Clear the old register's tracking (it now only holds RHS)
				regAlloc.registers[static_cast<int>(ctx.result_physical_reg)].stackVariableOffset = INT_MIN;
				regAlloc.registers[static_cast<int>(ctx.result_physical_reg)].isDirty = false;
				
				ctx.result_physical_reg = new_lhs_reg;
			}
		}

		return ctx;
	}

	// Store the result of arithmetic operations to the appropriate destination
	void storeArithmeticResult(const ArithmeticOperationContext& ctx, X64Register source_reg = X64Register::Count) {
		// Use the result register by default, or the specified source register (e.g., RAX for division)
		X64Register actual_source_reg = (source_reg == X64Register::Count) ? ctx.result_physical_reg : source_reg;

		// Check if we're dealing with floating-point types
		bool is_float_type = (ctx.result_value.type == Type::Float || ctx.result_value.type == Type::Double);

		// Track whether we should release the source register after storing
		bool should_release_source = false;

		// Determine the final destination of the result (register or memory)
		if (std::holds_alternative<StringHandle>(ctx.result_value.value)) {
			// If the result is a named variable, find its stack offset - Phase 5: Convert to StringHandle
			int final_result_offset = variable_scopes.back().variables[std::get<StringHandle>(ctx.result_value.value)].offset;

			// Check if this is a reference - if so, we need to store through the pointer
			auto ref_it = reference_stack_info_.find(final_result_offset);
			if (ref_it != reference_stack_info_.end()) {
				// This is a reference - load the pointer, then store the value through it
				X64Register ptr_reg = allocateRegisterWithSpilling();
				// Load the pointer into the register
				auto load_ptr = generatePtrMovFromFrame(ptr_reg, final_result_offset);
				textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(), load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
				// Now store the value through the pointer: [ptr_reg + 0] = actual_source_reg
				int value_size_bits = ref_it->second.value_size_bits;
				int value_size_bytes = value_size_bits / 8;
				emitStoreToMemory(textSectionData, actual_source_reg, ptr_reg, 0, value_size_bytes);
				regAlloc.release(ptr_reg);
			} else {
				// Not a reference, store normally
				// Store the computed result from actual_source_reg to memory
				if (is_float_type) {
					// Use SSE movss/movsd for float/double
					bool is_single_precision = (ctx.result_value.type == Type::Float);
					auto store_opcodes = generateFloatMovToFrame(actual_source_reg, final_result_offset, is_single_precision);
					textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(),
					                       store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
				} else {
					emitMovToFrameSized(
						SizedRegister{actual_source_reg, 64, false},  // source: 64-bit register
						SizedStackSlot{final_result_offset, ctx.result_value.size_in_bits, isSignedType(ctx.result_value.type)}  // dest
					);
				}
			}
			// For named variables, we can release the source register since the value is now in memory
			should_release_source = true;
		}
		else if (std::holds_alternative<TempVar>(ctx.result_value.value)) {
			auto res_var_op = std::get<TempVar>(ctx.result_value.value);
			auto res_stack_var_addr = getStackOffsetFromTempVar(res_var_op, ctx.result_value.size_in_bits);
			
			// Check if this is a reference - if so, we need to store through the pointer
			auto ref_it = reference_stack_info_.find(res_stack_var_addr);
			if (ref_it != reference_stack_info_.end()) {
				// This is a reference - load the pointer, then store the value through it
				X64Register ptr_reg = allocateRegisterWithSpilling();
				// Load the pointer into the register
				emitMovFromFrame(ptr_reg, res_stack_var_addr);
				// Now store the value through the pointer: [ptr_reg + 0] = actual_source_reg
				int value_size_bits = ref_it->second.value_size_bits;
				int value_size_bytes = value_size_bits / 8;
				emitStoreToMemory(textSectionData, actual_source_reg, ptr_reg, 0, value_size_bytes);
				regAlloc.release(ptr_reg);
				should_release_source = true;
			} else {
				// Not a reference, handle as before
				// IMPORTANT: Clear any stale register mappings for this stack variable BEFORE checking
				// This prevents using an old register value that was from a previous unrelated operation
				for (size_t i = 0; i < regAlloc.registers.size(); ++i) {
					auto& r = regAlloc.registers[i];
					if (r.stackVariableOffset == res_stack_var_addr && r.reg != actual_source_reg) {
						r.stackVariableOffset = INT_MIN; // Clear the mapping
						r.isDirty = false;
					}
				}
				
				if (auto res_reg = regAlloc.tryGetStackVariableRegister(res_stack_var_addr); res_reg.has_value()) {
					if (res_reg != actual_source_reg) {
						if (is_float_type) {
							// For float types, use SSE mov instructions for register-to-register moves
							// TODO: Implement SSE register-to-register moves if needed
							// For now, assert false since we shouldn't hit this path with current code
							throw std::runtime_error("Float register-to-register move not yet implemented");
						} else {
							auto moveFromRax = regAlloc.get_reg_reg_move_op_code(res_reg.value(), actual_source_reg, ctx.result_value.size_in_bits / 8);
							textSectionData.insert(textSectionData.end(), moveFromRax.op_codes.begin(), moveFromRax.op_codes.begin() + moveFromRax.size_in_bytes);
						}
					}
					// Result is already in the correct register, no move needed
					// For floating-point types, we MUST also write to memory even when register is correct
					// because the return handling will load from memory (XMM registers aren't fully tracked)
					if (is_float_type) {
						bool is_single_precision = (ctx.result_value.type == Type::Float);
						emitFloatMovToFrame(actual_source_reg, res_stack_var_addr, is_single_precision);
					} else {
						emitMovToFrameSized(
							SizedRegister{actual_source_reg, 64, false},
							SizedStackSlot{res_stack_var_addr, ctx.result_value.size_in_bits, isSignedType(ctx.result_value.type)}
						);
					}
					// Can release source register since result is now tracked in the destination register
					should_release_source = true;
				}
				else {
					// Temp variable not currently in a register - keep it in actual_source_reg instead of spilling
					// NOTE: The flushing of old register values is now handled in setupAndLoadArithmeticOperation
					// before the register is reassigned to the result TempVar's offset.
					
					// Tell the register allocator that this register now holds this temp variable
					assert(variable_scopes.back().scope_stack_space <= res_stack_var_addr);
					regAlloc.set_stack_variable_offset(actual_source_reg, res_stack_var_addr, ctx.result_value.size_in_bits);
					
					// For floating-point types, we MUST write to memory immediately because the register
					// allocator doesn't properly track XMM registers across all operations.
					// Without this, subsequent loads from the stack location will read garbage.
					if (is_float_type) {
						bool is_single_precision = (ctx.result_value.type == Type::Float);
						emitFloatMovToFrame(actual_source_reg, res_stack_var_addr, is_single_precision);
					} else {
						emitMovToFrameSized(
							SizedRegister{actual_source_reg, 64, false},
							SizedStackSlot{res_stack_var_addr, ctx.result_value.size_in_bits, isSignedType(ctx.result_value.type)}
						);
					}
					// Keep the value in the register for subsequent operations
					// DON'T release the source register for integer temps - keeping value in register for optimization
					should_release_source = false;
				}
			}

		}
		else {
			throw std::runtime_error("Unhandled destination type");
		}

		if (source_reg != X64Register::Count && should_release_source) {
			regAlloc.release(source_reg);
		}
	}

	// Group IR instructions by function for analysis
	void groupInstructionsByFunction(const Ir& ir) {
		function_spans.clear();
		std::string_view current_func_name;
		size_t current_func_start = 0;

		const auto& instructions = ir.getInstructions();

		for (size_t i = 0; i < instructions.size(); ++i) {
			const auto& instruction = instructions[i];
			if (instruction.getOpcode() == IrOpcode::FunctionDecl) {
				// Save previous function's span
				if (!current_func_name.empty()) {
					function_spans[std::string(current_func_name)] = std::span<const IrInstruction>(
						&instructions[current_func_start], i - current_func_start
					);
				}

				// Extract function name from typed payload
				const auto& func_decl = instruction.getTypedPayload<FunctionDeclOp>();
				// Use mangled name if available (for member functions like lambda operator()),
				// otherwise use function_name (Phase 4: Use helpers)
				StringHandle mangled_handle = func_decl.getMangledName();
				StringHandle func_name_handle = func_decl.getFunctionName();
				current_func_name = mangled_handle.handle != 0 ? StringTable::getStringView(mangled_handle) : StringTable::getStringView(func_name_handle);
				current_func_start = i + 1; // Instructions start after FunctionDecl
			}
		}

		// Save the last function's span
		if (!current_func_name.empty()) {
			function_spans[std::string(current_func_name)] = std::span<const IrInstruction>(
				&instructions[current_func_start], instructions.size() - current_func_start
			);
		}
	}

	// Calculate the total stack space needed for a function by analyzing its IR instructions
	struct StackSpaceSize {
		uint16_t temp_vars_size = 0;
		uint16_t named_vars_size = 0;
		uint16_t shadow_stack_space = 0;
		uint16_t outgoing_args_space = 0;  // Space for largest outgoing function call
	};
	struct VariableInfo {
		int offset = INT_MIN;  // Stack offset from RBP (INT_MIN = unallocated)
		int size_in_bits = 0;  // Size in bits
		bool is_array = false; // True if this is an array declaration (enables array-to-pointer decay in expressions and assignments)
	};

	struct StackVariableScope
	{
		int scope_stack_space = 0;
		std::unordered_map<StringHandle, VariableInfo> variables;  // Phase 5: StringHandle for integer-based lookups
	};

	struct ReferenceInfo {
		Type value_type = Type::Invalid;
		int value_size_bits = 0;
		bool is_rvalue_reference = false;
		// When true (e.g., AddressOf results), this TempVar holds a raw address/pointer value,
		// not a reference that should be implicitly dereferenced.
		bool holds_address_only = false;
	};
	
	// Helper function to set reference information in both storage systems
	// This ensures metadata stays synchronized between stack offset tracking and TempVar metadata
	void setReferenceInfo(int32_t stack_offset, Type value_type, int value_size_bits, bool is_rvalue_ref, TempVar temp_var = TempVar()) {
		// Always update the stack offset map (for named variables and legacy lookups)
		reference_stack_info_[stack_offset] = ReferenceInfo{
			.value_type = value_type,
			.value_size_bits = value_size_bits,
			.is_rvalue_reference = is_rvalue_ref,
			.holds_address_only = false
		};
		
		// If we have a valid TempVar, also update its metadata
		if (temp_var.var_number != 0) {
			setTempVarMetadata(temp_var, TempVarMetadata::makeReference(value_type, value_size_bits, is_rvalue_ref));
		}
	}
	
	// Helper function to check if a TempVar or stack offset is a reference
	// Checks TempVar metadata first (preferred), then falls back to stack offset lookup
	bool isReference(TempVar temp_var, int32_t stack_offset) const {
		// Check TempVar metadata first (more reliable, travels with the value)
		if (temp_var.var_number != 0 && isTempVarReference(temp_var)) {
			return true;
		}
		
		// Fall back to stack offset lookup (for named variables or legacy code)
		return reference_stack_info_.find(stack_offset) != reference_stack_info_.end();
	}
	
	// Helper function to get reference info for a TempVar or stack offset
	// Returns the reference info from TempVar metadata if available, otherwise from stack offset map
	std::optional<ReferenceInfo> getReferenceInfo(TempVar temp_var, int32_t stack_offset) const {
		// Check TempVar metadata first
		if (temp_var.var_number != 0 && isTempVarReference(temp_var)) {
			return ReferenceInfo{
				.value_type = getTempVarValueType(temp_var),
				.value_size_bits = getTempVarValueSizeBits(temp_var),
				.is_rvalue_reference = isTempVarRValueReference(temp_var),
				.holds_address_only = false
			};
		}
		
		// Fall back to stack offset lookup
		auto it = reference_stack_info_.find(stack_offset);
		if (it != reference_stack_info_.end()) {
			return it->second;
		}
		
		return std::nullopt;
	}
	
	StackSpaceSize calculateFunctionStackSpace(std::string_view func_name, StackVariableScope& var_scope, size_t param_count) {
		StackSpaceSize func_stack_space{};

		auto it = function_spans.find(func_name);
		if (it == function_spans.end()) {
			return func_stack_space; // No instructions found for this function
		}

		struct VarDecl {
			StringHandle var_name{};  // Phase 5: StringHandle for efficient storage
			int size_in_bits{};
			size_t alignment{};  // Custom alignment from alignas(n), 0 = use natural alignment
			bool is_array{};     // True if this variable is an array (for array-to-pointer decay)
		};
		std::vector<VarDecl> local_vars;

		// Clear temp_var_sizes for this function
		temp_var_sizes_.clear();

		// Pre-scan: detect C++ exception handling (try/catch) in this function
		current_function_has_cpp_eh_ = false;
		for (const auto& instruction : it->second) {
			if (instruction.getOpcode() == IrOpcode::TryBegin) {
				current_function_has_cpp_eh_ = true;
				break;
			}
		}

		// Track maximum outgoing call argument space needed
		size_t max_outgoing_arg_bytes = 0;

		for (const auto& instruction : it->second) {
			// Look for TempVar operands in the instruction
			func_stack_space.shadow_stack_space |= (0x20 * !(instruction.getOpcode() != IrOpcode::FunctionCall));
			
			// Track outgoing call argument space
			if (instruction.getOpcode() == IrOpcode::FunctionCall && instruction.hasTypedPayload()) {
				if (const CallOp* call_op = std::any_cast<CallOp>(&instruction.getTypedPayload())) {
					// For Windows variadic calls: ALL args on stack starting at RSP+0
					// For Windows normal calls: Args beyond 4 on stack starting at RSP+32 (shadow space)
					// For Linux: Args beyond 6 on stack starting at RSP+0
					constexpr bool is_coff_format = !std::is_same_v<TWriterClass, ElfFileWriter>;
					size_t arg_count = call_op->args.size();
					size_t outgoing_bytes = 0;
					
					if (is_coff_format) {
						if (call_op->is_variadic) {
							// Windows variadic: ALL args on stack, starting at RSP+0
							// Need at least 32 bytes shadow space for first 4 register params
							// Align to 16 bytes for stack alignment requirements
							outgoing_bytes = std::max(arg_count * 8, static_cast<size_t>(32));
							outgoing_bytes = (outgoing_bytes + 15) & ~static_cast<size_t>(15);
						} else {
							// Windows normal: First 4 in registers, rest on stack starting at RSP+32
							if (arg_count > 4) {
								outgoing_bytes = 32 + (arg_count - 4) * 8;
							} else {
								outgoing_bytes = 32;  // Shadow space even if all args in registers
							}
						}
					} else {
						// Linux: First 6 in registers, rest on stack starting at RSP+0
						if (arg_count > 6) {
							outgoing_bytes = (arg_count - 6) * 8;
						}
						// No shadow space on Linux
					}
					
					if (outgoing_bytes > max_outgoing_arg_bytes) {
						max_outgoing_arg_bytes = outgoing_bytes;
					}
				}
			}

			if (instruction.getOpcode() == IrOpcode::VariableDecl) {
				const VariableDeclOp& op = std::any_cast<const VariableDeclOp&>(instruction.getTypedPayload());
				auto size_in_bits = op.size_in_bits;
				// Get variable name (Phase 4: Use helper)
				std::string_view var_name = op.getVarName();
				size_t custom_alignment = op.custom_alignment;

				bool is_reference = op.is_reference;
				bool is_array = op.is_array;
				int total_size_bits = size_in_bits;
				if (is_reference) {
					total_size_bits = 64;
				}
				if (is_array && op.array_count.has_value()) {
					uint64_t array_size = op.array_count.value();
					total_size_bits = size_in_bits * static_cast<int>(array_size);
				}
				
				func_stack_space.named_vars_size += (total_size_bits / 8);
				// Phase 5: Store StringHandle directly for efficient variable tracking
				local_vars.push_back(VarDecl{ .var_name = StringTable::getOrInternStringHandle(var_name), .size_in_bits = total_size_bits, .alignment = custom_alignment, .is_array = is_array });
			}
			else {
				// Track TempVars and their sizes from typed payloads or legacy operand format
				bool handled_by_typed_payload = false;
				
				// For typed payload instructions, try common payload types
				if (instruction.hasTypedPayload()) {
					try {
						// Try BinaryOp (arithmetic, comparisons, logic)
						if (const BinaryOp* bin_op = std::any_cast<BinaryOp>(&instruction.getTypedPayload())) {
							if (std::holds_alternative<TempVar>(bin_op->result)) {
								auto temp_var = std::get<TempVar>(bin_op->result);
								// Phase 5: Convert temp var name to StringHandle
								// For comparison operations, result is always bool (8 bits)
								// For arithmetic/logical operations, result size matches operand size
								auto opcode = instruction.getOpcode();
								bool is_comparison = (opcode == IrOpcode::Equal || opcode == IrOpcode::NotEqual ||
								                      opcode == IrOpcode::LessThan || opcode == IrOpcode::LessEqual ||
								                      opcode == IrOpcode::GreaterThan || opcode == IrOpcode::GreaterEqual ||
								                      opcode == IrOpcode::UnsignedLessThan || opcode == IrOpcode::UnsignedLessEqual ||
								                      opcode == IrOpcode::UnsignedGreaterThan || opcode == IrOpcode::UnsignedGreaterEqual ||
								                      opcode == IrOpcode::FloatEqual || opcode == IrOpcode::FloatNotEqual ||
								                      opcode == IrOpcode::FloatLessThan || opcode == IrOpcode::FloatLessEqual ||
								                      opcode == IrOpcode::FloatGreaterThan || opcode == IrOpcode::FloatGreaterEqual);
								int result_size = is_comparison ? 8 : bin_op->lhs.size_in_bits;
								temp_var_sizes_[StringTable::getOrInternStringHandle(temp_var.name())] = result_size;
								handled_by_typed_payload = true;
							}
						}
						// Try UnaryOp (logical not, bitwise not, negate)
						else if (const UnaryOp* unary_op = std::any_cast<UnaryOp>(&instruction.getTypedPayload())) {
							// Phase 5: Convert temp var name to StringHandle
							// For logical not, result is always bool (8 bits)
							// For bitwise not and negate, result size matches operand size
							temp_var_sizes_[StringTable::getOrInternStringHandle(unary_op->result.name())] = unary_op->value.size_in_bits;
							handled_by_typed_payload = true;
						}
						// Try CallOp (function calls)
						else if (const CallOp* call_op = std::any_cast<CallOp>(&instruction.getTypedPayload())) {
							// Phase 5: Convert temp var name to StringHandle
							temp_var_sizes_[StringTable::getOrInternStringHandle(call_op->result.name())] = call_op->return_size_in_bits;
							handled_by_typed_payload = true;
						}
						// Try ArrayAccessOp (array element load)
						else if (const ArrayAccessOp* array_op = std::any_cast<ArrayAccessOp>(&instruction.getTypedPayload())) {
							// Phase 5: Convert temp var name to StringHandle
							temp_var_sizes_[StringTable::getOrInternStringHandle(array_op->result.name())] = array_op->element_size_in_bits;
							handled_by_typed_payload = true;
						}
						// Try ArrayElementAddressOp (get address of array element)
						else if (const ArrayElementAddressOp* addr_op = std::any_cast<ArrayElementAddressOp>(&instruction.getTypedPayload())) {
							// Phase 5: Convert temp var name to StringHandle
							temp_var_sizes_[StringTable::getOrInternStringHandle(addr_op->result.name())] = 64; // Pointer is always 64-bit
							handled_by_typed_payload = true;
						}
						// Try DereferenceOp (for dereferencing pointers/references)
						else if (const DereferenceOp* deref_op = std::any_cast<DereferenceOp>(&instruction.getTypedPayload())) {
							// Phase 5: Convert temp var name to StringHandle
							// Determine size based on pointer depth: if depth > 1, result is a pointer (64 bits)
							int result_size = (deref_op->pointer.pointer_depth > 1) ? 64 : deref_op->pointer.size_in_bits;
							temp_var_sizes_[StringTable::getOrInternStringHandle(deref_op->result.name())] = result_size;
							handled_by_typed_payload = true;
						}
						// Try AssignmentOp (for materializing literals to temporaries)
						else if (const AssignmentOp* assign_op = std::any_cast<AssignmentOp>(&instruction.getTypedPayload())) {
							// Track the LHS TempVar if it's a TempVar
							if (std::holds_alternative<TempVar>(assign_op->lhs.value)) {
								auto temp_var = std::get<TempVar>(assign_op->lhs.value);
								// Phase 5: Convert temp var name to StringHandle
								temp_var_sizes_[StringTable::getOrInternStringHandle(temp_var.name())] = assign_op->lhs.size_in_bits;
								handled_by_typed_payload = true;
							}
						}
						// Try AddressOfOp (for taking address of temporaries)
						else if (const AddressOfOp* addr_of_op = std::any_cast<AddressOfOp>(&instruction.getTypedPayload())) {
							// Phase 5: Convert temp var name to StringHandle
							temp_var_sizes_[StringTable::getOrInternStringHandle(addr_of_op->result.name())] = 64; // Pointer is always 64-bit
							handled_by_typed_payload = true;
						}
						// Try GlobalLoadOp (for loading global variables)
						else if (const GlobalLoadOp* global_load_op = std::any_cast<GlobalLoadOp>(&instruction.getTypedPayload())) {
							if (std::holds_alternative<TempVar>(global_load_op->result.value)) {
								auto temp_var = std::get<TempVar>(global_load_op->result.value);
								temp_var_sizes_[StringTable::getOrInternStringHandle(temp_var.name())] = global_load_op->result.size_in_bits;
								handled_by_typed_payload = true;
							}
						}
						// Add more payload types here as they produce TempVars
					} catch (const std::exception& e) {
						FLASH_LOG(Codegen, Warning, "[calculateFunctionStackSpace]: Exception while processing typed payload for opcode ", 
						          static_cast<int>(instruction.getOpcode()), ": ", e.what());
					} catch (...) {
						FLASH_LOG(Codegen, Warning, "[calculateFunctionStackSpace]: Unknown exception while processing typed payload for opcode ", 
						          static_cast<int>(instruction.getOpcode()));
					}
				}
				
				// Fallback: Track TempVars from legacy operand format
				// Most arithmetic/logic instructions have format: [result_var, type, size, ...]
				// where operand 0 is result, operand 1 is type, operand 2 is size
				if (!handled_by_typed_payload && 
				    instruction.getOperandCount() >= 3 && 
				    instruction.isOperandType<TempVar>(0) &&
				    instruction.isOperandType<int>(2)) {
					auto temp_var = instruction.getOperandAs<TempVar>(0);
					int size_in_bits = instruction.getOperandAs<int>(2);
					temp_var_sizes_[StringTable::getOrInternStringHandle(temp_var.name())] = size_in_bits;
				}
			}
		}

		// TempVars are now allocated dynamically via formula, not pre-allocated
		
		// Start stack allocation AFTER parameter home space
		// Windows x64 ABI: first 4 parameters get home space at [rbp-8], [rbp-16], [rbp-24], [rbp-32]
		// Additional parameters are passed on the stack at positive RBP offsets
		// Local variables start AFTER the parameter home space
		int param_home_space = std::max(static_cast<int>(param_count), 4) * 8;  // At least 32 bytes for register parameters
		// For C++ EH functions, reserve [rbp-8] for the FH3 unwind help state variable.
		// Shift parameter home space down by 8 bytes so it starts at [rbp-16].
		int eh_state_reserve = (current_function_has_cpp_eh_ && !std::is_same_v<TWriterClass, ElfFileWriter>) ? 8 : 0;
		int_fast32_t stack_offset = -(param_home_space + eh_state_reserve);
		
		for (const VarDecl& local_var : local_vars) {
			// Apply alignment if specified, otherwise use natural alignment (8 bytes for x64)
			size_t var_alignment = local_var.alignment > 0 ? local_var.alignment : 8;

			// Align the stack offset down to the required alignment
			// Stack grows downward, so we need to align down (toward more negative values)
			int_fast32_t aligned_offset = stack_offset;
			if (var_alignment > 1) {
				// Round down to nearest multiple of alignment
				// For negative offsets: (-16 & ~15) = -16, (-15 & ~15) = -16, (-17 & ~15) = -32
				aligned_offset = (stack_offset - static_cast<int_fast32_t>(var_alignment) + 1) & ~(static_cast<int_fast32_t>(var_alignment) - 1);
			}

			// Allocate space for the variable
			stack_offset = aligned_offset - (local_var.size_in_bits / 8);

			// Store both offset and size in unified structure, including is_array flag
			var_scope.variables.insert_or_assign(local_var.var_name, VariableInfo{static_cast<int>(stack_offset), local_var.size_in_bits, local_var.is_array});
		}

		// Calculate space needed for TempVars
		// Each TempVar uses 8 bytes (64-bit alignment)
		// Calculate space for temp vars using actual sizes, not just count * 8
		int temp_var_space = 0;
		for (const auto& [temp_var_name, size_bits] : temp_var_sizes_) {
			int size_in_bytes = (size_bits + 7) / 8;
			size_in_bytes = (size_in_bytes + 7) & ~7;  // 8-byte alignment
			temp_var_space += size_in_bytes;
		}

		// Don't subtract from stack_offset - TempVars are allocated separately via getStackOffsetFromTempVar

		// Store TempVar sizes for later use during code generation
		// TempVars will have their offsets set when actually allocated via getStackOffsetFromTempVar
		// Use INT_MIN as a sentinel value to indicate "not yet allocated"
		for (const auto& [temp_var_name, size_bits] : temp_var_sizes_) {
			// Initialize with sentinel offset (INT_MIN), actual offset set later
			var_scope.variables.insert_or_assign(temp_var_name, VariableInfo{INT_MIN, size_bits});
		}

		// Calculate total stack space needed
		func_stack_space.temp_vars_size = temp_var_space;  // TempVar space (added to total separately)
		func_stack_space.named_vars_size = -stack_offset;  // Just named variables space
		func_stack_space.outgoing_args_space = static_cast<uint16_t>(max_outgoing_arg_bytes);  // Outgoing call argument space
		
		// if we are a leaf function (don't call other functions), we can get by with just register if we don't have more than 8 * 64 bytes of values to store
		//if (shadow_stack_space == 0 && max_temp_var_index <= 8) {
			//return 0;
		//}

		return func_stack_space;
	}

	// Helper function to get or reserve a stack slot for a temporary variable.
	// This is now just a thin wrapper around getStackOffsetFromTempVar which 
	// handles stack space tracking and offset registration.
	int allocateStackSlotForTempVar(int32_t index, int size_in_bits = 64) {
		TempVar tempVar(index);
		return getStackOffsetFromTempVar(tempVar, size_in_bits);
	}

	// Get stack offset for a TempVar using formula-based allocation.
	// TempVars are allocated within the pre-allocated temp_vars space.
	// The space starts after named_vars + shadow_space.
	// 
	// This function also:
	// - Extends scope_stack_space if the offset exceeds current tracked allocation
	// - Registers the TempVar in variables for consistent subsequent lookups
	int32_t getStackOffsetFromTempVar(TempVar tempVar, int size_in_bits = 64) {
		// Check if this TempVar was pre-allocated (named variables or previously computed TempVars)
		if (!variable_scopes.empty()) {
			StringHandle lookup_handle = StringTable::getOrInternStringHandle(tempVar.name());
			auto& current_scope = variable_scopes.back();
			auto it = current_scope.variables.find(lookup_handle);
			if (it != current_scope.variables.end() && it->second.offset != INT_MIN) {
				int existing_offset = it->second.offset;
				
				// Check if we need to extend the allocation for a larger size
				// This can happen when a TempVar is first allocated with default size,
				// then later used for a large struct (e.g., constructor call result)
				int size_in_bytes = (size_in_bits + 7) / 8;
				size_in_bytes = (size_in_bytes + 7) & ~7;  // 8-byte alignment
				
				int32_t end_offset = existing_offset - size_in_bytes;
				if (end_offset < variable_scopes.back().scope_stack_space) {
					FLASH_LOG_FORMAT(Codegen, Debug,
						"Extending scope_stack_space from {} to {} for pre-allocated {} (offset={}, size={})",
						variable_scopes.back().scope_stack_space, end_offset, tempVar.name(), existing_offset, size_in_bytes);
					variable_scopes.back().scope_stack_space = end_offset;
				}
				
				FLASH_LOG_FORMAT(Codegen, Debug,
					"TempVar {} already allocated at offset {}, size={} bytes", 
					tempVar.name(), existing_offset, size_in_bytes);
				return existing_offset;  // Use pre-allocated offset (if it's been properly set)
			}
			
			// CRITICAL FIX: If TempVar entry has INT_MIN, check if it corresponds to the most recently
			// allocated named variable (tracked in handleVariableDecl)
			// This handles the duplicate entry problem where named variables get both a name entry
			// and a TempVar entry
			if (it != variable_scopes.back().variables.end() && it->second.offset == INT_MIN) {
				if (last_allocated_variable_name_.isValid() && last_allocated_variable_offset_ != 0) {
					// Use the last allocated variable's offset for this TempVar
					// Update the TempVar entry so future lookups are O(1)
					it->second.offset = last_allocated_variable_offset_;
					return last_allocated_variable_offset_;
				}
			}
		}
		// Allocate TempVars sequentially after named_vars + shadow space
		// Use next_temp_var_offset_ to track the next available slot
		// Each TempVar gets size_in_bits bytes (rounded up to 8-byte alignment)
		// Check temp_var_sizes_ for pre-calculated size (from calculateFunctionStackSpace)
		// This ensures large struct returns are allocated with correct size from the start
		StringHandle temp_var_handle = StringTable::getOrInternStringHandle(tempVar.name());
		auto size_it = temp_var_sizes_.find(temp_var_handle);
		int actual_size_in_bits = size_in_bits;
		if (size_it != temp_var_sizes_.end() && size_it->second > size_in_bits) {
			actual_size_in_bits = size_it->second;  // Use pre-calculated size if larger
		}
		
		int size_in_bytes = (actual_size_in_bits + 7) / 8;  // Round up to nearest byte
		size_in_bytes = (size_in_bytes + 7) & ~7;    // Round up to 8-byte alignment
		
		// Advance next_temp_var_offset_ FIRST to reserve space for this allocation
		// This ensures large structs don't overlap with previously allocated variables
		// The offset points to the BASE of the struct (lowest address), and the struct
		// extends UPWARD in memory by size_in_bytes
		next_temp_var_offset_ += size_in_bytes;
		int32_t offset = -(static_cast<int32_t>(current_function_named_vars_size_) + next_temp_var_offset_);
		
		// Track the maximum TempVar index for stack size calculation
		if (tempVar.var_number > max_temp_var_index_) {
			max_temp_var_index_ = tempVar.var_number;
		}

		// Extend scope_stack_space if the computed offset exceeds current allocation
		// This ensures assertions checking scope_stack_space <= offset remain valid
		// NOTE: offset is the LOWEST address of the allocation (next_temp_var_offset_ was
		// already incremented above), so it is itself the end_offset we must track.
		int32_t end_offset = offset;
		if (end_offset < variable_scopes.back().scope_stack_space) {
			FLASH_LOG_FORMAT(Codegen, Debug,
				"Extending scope_stack_space from {} to {} for {} (offset={}, size={})",
				variable_scopes.back().scope_stack_space, end_offset, tempVar.name(), offset, size_in_bytes);
			variable_scopes.back().scope_stack_space = end_offset;
		}
		
		// Register the TempVar's offset in variables map so subsequent lookups
		// return the same offset even if scope_stack_space changes
		// Note: temp_var_handle was already created above for the size lookup
		variable_scopes.back().variables[temp_var_handle].offset = offset;
		
		return offset;
	}

	void flushAllDirtyRegisters()
	{
		regAlloc.flushAllDirtyRegisters([this](X64Register reg, int32_t stackVariableOffset, int size_in_bits)
			{
				// Always flush dirty registers to stack, regardless of offset alignment.
				// This fixes the register flush bug where non-8-byte-aligned offsets
				// (from structured bindings) would cause getTempVarFromOffset to return
				// nullopt, preventing the register from being flushed.
				
				// Note: stackVariableOffset should be within allocated space (scope_stack_space <= stackVariableOffset <= 0)
				// However, during code generation, constructors may create additional TempVars beyond pre-calculated space.
				// Extend scope_stack_space dynamically if needed.
				if (stackVariableOffset < variable_scopes.back().scope_stack_space) {
					variable_scopes.back().scope_stack_space = stackVariableOffset;
				}
				assert(variable_scopes.back().scope_stack_space <= stackVariableOffset && stackVariableOffset <= 0);

				// Store the computed result from register to stack using size-appropriate MOV
				emitMovToFrameSized(
					SizedRegister{reg, 64, false},  // source: 64-bit register
					SizedStackSlot{stackVariableOffset, size_in_bits, false}  // dest: sized stack slot
				);
			});
	}

	// Helper to generate and emit size-appropriate MOV to frame (legacy - prefer emitMovToFrameSized)

#include "IRConverter_Emit_CompareBranch.h"

