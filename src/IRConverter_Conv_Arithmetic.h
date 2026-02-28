	void handleBinaryArithmetic(const IrInstruction& instruction, uint8_t opcode, const char* description) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, description);
		emitBinaryOpInstruction(opcode, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.operand_size_in_bits);
		storeArithmeticResult(ctx);
		regAlloc.release(ctx.rhs_physical_reg);
	}

	void handleAdd(const IrInstruction& instruction) {
		handleBinaryArithmetic(instruction, 0x01, "addition"); // ADD dst, src
	}

	void handleSubtract(const IrInstruction& instruction) {
		handleBinaryArithmetic(instruction, 0x29, "subtraction"); // SUB dst, src
	}

	void handleMultiply(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "multiplication");

		// Perform the multiplication operation: IMUL dst, src (opcode 0x0F 0xAF)
		// Determine if we need a REX prefix
		bool needs_rex = (ctx.operand_size_in_bits == 64);
		uint8_t rex_prefix = (ctx.operand_size_in_bits == 64) ? 0x48 : 0x40;
		
		// Check if registers need REX extensions
		if (static_cast<uint8_t>(ctx.result_physical_reg) >= 8) {
			rex_prefix |= 0x04; // Set REX.R for result_physical_reg (reg field)
			needs_rex = true;
		}
		if (static_cast<uint8_t>(ctx.rhs_physical_reg) >= 8) {
			rex_prefix |= 0x01; // Set REX.B for rhs_physical_reg (rm field)
			needs_rex = true;
		}
		
		// Build ModR/M byte
		uint8_t modrm_byte = 0xC0 | ((static_cast<uint8_t>(ctx.result_physical_reg) & 0x07) << 3) | (static_cast<uint8_t>(ctx.rhs_physical_reg) & 0x07);
		
		// Emit the instruction (IMUL is a two-byte opcode: 0x0F 0xAF)
		if (needs_rex) {
			textSectionData.push_back(rex_prefix);
		}
		textSectionData.push_back(0x0F);
		textSectionData.push_back(0xAF);
		textSectionData.push_back(modrm_byte);

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);

		// Release the RHS register (we're done with it)
		regAlloc.release(ctx.rhs_physical_reg);
		// Note: Do NOT release result_physical_reg here - it may be holding a temp variable
	}

	void handleDivide(const IrInstruction& instruction) {
		flushAllDirtyRegisters();	// we do this so that RDX is free to use

		regAlloc.release(X64Register::RAX);
		regAlloc.allocateSpecific(X64Register::RAX, INT_MIN);

		regAlloc.release(X64Register::RDX);
		regAlloc.allocateSpecific(X64Register::RDX, INT_MIN);

		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "division");

		// Division requires special handling: dividend must be in RAX
		// Move result_physical_reg to RAX (dividend must be in RAX for idiv)
		auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, ctx.result_physical_reg, ctx.result_value.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);

		// Sign extend RAX into RDX:RAX (CQO for 64-bit)
		if (ctx.result_value.size_in_bits == 64) {
			// CQO - sign extend RAX into RDX:RAX (fills RDX with 0 or -1)
			std::array<uint8_t, 2> cqoInst = { 0x48, 0x99 }; // REX.W + CQO
			textSectionData.insert(textSectionData.end(), cqoInst.begin(), cqoInst.end());
		} else {
			// CDQ - sign extend EAX into EDX:EAX (for 32-bit)
			std::array<uint8_t, 1> cdqInst = { 0x99 };
			textSectionData.insert(textSectionData.end(), cdqInst.begin(), cdqInst.end());
		}

	    // idiv rhs_physical_reg
		uint8_t rex = 0x40; // Base REX prefix
		if (ctx.result_value.size_in_bits == 64) {
			rex |= 0x08; // Set REX.W for 64-bit operation
		}

		// Check if we need REX.B for the divisor register
		if (static_cast<uint8_t>(ctx.rhs_physical_reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex |= 0x01; // Set REX.B
		}

		std::array<uint8_t, 3> divInst = {
			rex,
			0xF7,  // Opcode for IDIV
			static_cast<uint8_t>(0xF8 + (static_cast<uint8_t>(ctx.rhs_physical_reg) & 0x07))  // ModR/M: 11 111 reg (opcode extension 7 for IDIV)
		};
		textSectionData.insert(textSectionData.end(), divInst.begin(), divInst.end());

		// Store the result from RAX (quotient) to the appropriate destination
		storeArithmeticResult(ctx, X64Register::RAX);

		regAlloc.release(X64Register::RDX);
	}

	void handleShiftLeft(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift left");

		// Shift operations require the shift count to be in CL (lower 8 bits of RCX)
		// Move rhs_physical_reg to RCX
		auto movRhsToCx = regAlloc.get_reg_reg_move_op_code(X64Register::RCX, ctx.rhs_physical_reg, ctx.result_value.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movRhsToCx.op_codes.begin(), movRhsToCx.op_codes.begin() + movRhsToCx.size_in_bytes);

		// Perform the shift left operation: shl r/m, cl
		emitOpcodeExtInstruction(0xD3, X64OpcodeExtension::SHL, ctx.result_physical_reg, ctx.result_value.size_in_bits);

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleShiftRight(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift right");

		// Shift operations require the shift count to be in CL (lower 8 bits of RCX)
		// Move rhs_physical_reg to RCX
		auto movRhsToCx = regAlloc.get_reg_reg_move_op_code(X64Register::RCX, ctx.rhs_physical_reg, ctx.result_value.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movRhsToCx.op_codes.begin(), movRhsToCx.op_codes.begin() + movRhsToCx.size_in_bytes);

		// Perform the shift right operation: sar r/m, cl (arithmetic right shift)
		// Note: Using SAR (arithmetic) instead of SHR (logical) to preserve sign for signed integers
		emitOpcodeExtInstruction(0xD3, X64OpcodeExtension::SAR, ctx.result_physical_reg, ctx.result_value.size_in_bits);

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleUnsignedDivide(const IrInstruction& instruction) {
		flushAllDirtyRegisters();	// we do this so that RDX is free to use

		regAlloc.release(X64Register::RAX);
		regAlloc.allocateSpecific(X64Register::RAX, INT_MIN);

		regAlloc.release(X64Register::RDX);
		regAlloc.allocateSpecific(X64Register::RDX, INT_MIN);

		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned division");

		// Division requires special handling: dividend must be in RAX
		// Move result_physical_reg to RAX (dividend must be in RAX for div)
		auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, ctx.result_physical_reg, ctx.result_value.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);

		// xor edx, edx - clear upper 32 bits of dividend for unsigned division
		std::array<uint8_t, 2> xorEdxInst = { 0x31, 0xD2 };
		textSectionData.insert(textSectionData.end(), xorEdxInst.begin(), xorEdxInst.end());

		// div rhs_physical_reg (unsigned division)
		emitOpcodeExtInstruction(0xF7, X64OpcodeExtension::DIV, ctx.rhs_physical_reg, ctx.result_value.size_in_bits);

		// Store the result from RAX (quotient) to the appropriate destination
		storeArithmeticResult(ctx, X64Register::RAX);

		regAlloc.release(X64Register::RDX);
	}

	void handleUnsignedShiftRight(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned shift right");

		// Shift operations require the shift count to be in CL (lower 8 bits of RCX)
		// Move rhs_physical_reg to RCX
		auto movRhsToCx = regAlloc.get_reg_reg_move_op_code(X64Register::RCX, ctx.rhs_physical_reg, ctx.result_value.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movRhsToCx.op_codes.begin(), movRhsToCx.op_codes.begin() + movRhsToCx.size_in_bytes);

		// Perform the unsigned shift right operation: shr r/m, cl (logical right shift)
		// Note: Using SHR (logical) instead of SAR (arithmetic) for unsigned integers
		emitOpcodeExtInstruction(0xD3, X64OpcodeExtension::SHR, ctx.result_physical_reg, ctx.result_value.size_in_bits);

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseArithmetic(const IrInstruction& instruction, uint8_t opcode, const char* description) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, description);
		emitBinaryOpInstruction(opcode, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.operand_size_in_bits);
		storeArithmeticResult(ctx);
		regAlloc.release(ctx.rhs_physical_reg);
	}

	void handleBitwiseAnd(const IrInstruction& instruction) {
		handleBitwiseArithmetic(instruction, 0x21, "bitwise AND"); // AND dst, src
	}

	void handleBitwiseOr(const IrInstruction& instruction) {
		handleBitwiseArithmetic(instruction, 0x09, "bitwise OR"); // OR dst, src
	}

	void handleBitwiseXor(const IrInstruction& instruction) {
		handleBitwiseArithmetic(instruction, 0x31, "bitwise XOR"); // XOR dst, src
	}

	void handleModulo(const IrInstruction& instruction) {
		flushAllDirtyRegisters();	// we do this so that RDX is free to use

		regAlloc.release(X64Register::RAX);
		regAlloc.allocateSpecific(X64Register::RAX, INT_MIN);

		regAlloc.release(X64Register::RDX);
		regAlloc.allocateSpecific(X64Register::RDX, INT_MIN);

		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "modulo");

		// For x86-64, modulo is implemented using division
		// idiv instruction computes both quotient (RAX) and remainder (RDX)
		// We need the remainder in RDX

		// Move dividend to RAX (dividend must be in RAX for idiv)
		auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, ctx.result_physical_reg, ctx.result_value.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);

		// Release the original result register since we moved its value to RAX
		regAlloc.release(ctx.result_physical_reg);

		// Sign extend RAX into RDX:RAX
		if (ctx.result_value.size_in_bits == 64) {
			// CQO - sign extend RAX into RDX:RAX (fills RDX with 0 or -1)
			std::array<uint8_t, 2> cqoInst = { 0x48, 0x99 }; // REX.W + CQO
			textSectionData.insert(textSectionData.end(), cqoInst.begin(), cqoInst.end());
		} else {
			// CDQ - sign extend EAX into EDX:EAX (for 32-bit)
			std::array<uint8_t, 1> cdqInst = { 0x99 };
			textSectionData.insert(textSectionData.end(), cdqInst.begin(), cdqInst.end());
		}

	    // idiv rhs_physical_reg
		uint8_t rex = 0x40; // Base REX prefix
		if (ctx.result_value.size_in_bits == 64) {
			rex |= 0x08; // Set REX.W for 64-bit operation
		}

		// Check if we need REX.B for the divisor register
		if (static_cast<uint8_t>(ctx.rhs_physical_reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex |= 0x01; // Set REX.B
		}

		std::array<uint8_t, 3> divInst = {
			rex,
			0xF7,  // Opcode for IDIV
			static_cast<uint8_t>(0xF8 + (static_cast<uint8_t>(ctx.rhs_physical_reg) & 0x07))  // ModR/M: 11 111 reg (opcode extension 7 for IDIV)
		};
		textSectionData.insert(textSectionData.end(), divInst.begin(), divInst.end());

		// Manually store remainder from RDX to the result variable's stack location
		// Don't use storeArithmeticResult because it tries to be too clever with register tracking
		if (std::holds_alternative<StringHandle>(ctx.result_value.value)) {
			int final_result_offset = variable_scopes.back().variables[std::get<StringHandle>(ctx.result_value.value)].offset;
			emitMovToFrameSized(
				SizedRegister{X64Register::RDX, 64, false},  // source: RDX register
				SizedStackSlot{final_result_offset, ctx.result_value.size_in_bits, isSignedType(ctx.result_value.type)}  // dest
			);
		} else if (std::holds_alternative<TempVar>(ctx.result_value.value)) {
			auto res_var_op = std::get<TempVar>(ctx.result_value.value);
			auto res_stack_var_addr = getStackOffsetFromTempVar(res_var_op, ctx.result_value.size_in_bits);
			emitMovToFrameSized(
				SizedRegister{X64Register::RDX, 64, false},  // source: RDX register
				SizedStackSlot{res_stack_var_addr, ctx.result_value.size_in_bits, isSignedType(ctx.result_value.type)}  // dest
			);
		}

		regAlloc.release(X64Register::RDX);
	}

	void handleEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "equal comparison");
		emitComparisonInstruction(ctx, 0x94); // SETE
	}

	void handleNotEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "not equal comparison");
		emitComparisonInstruction(ctx, 0x95); // SETNE
	}

	void handleLessThan(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "less than comparison");
		emitComparisonInstruction(ctx, 0x9C); // SETL
	}

	void handleLessEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "less than or equal comparison");
		emitComparisonInstruction(ctx, 0x9E); // SETLE
	}

	void handleGreaterThan(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "greater than comparison");
		emitComparisonInstruction(ctx, 0x9F); // SETG
	}

	void handleGreaterEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "greater than or equal comparison");
		emitComparisonInstruction(ctx, 0x9D); // SETGE
	}

	void handleUnsignedLessThan(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned less than comparison");
		emitComparisonInstruction(ctx, 0x92); // SETB
	}

	void handleUnsignedLessEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned less than or equal comparison");
		emitComparisonInstruction(ctx, 0x96); // SETBE
	}

	void handleUnsignedGreaterThan(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned greater than comparison");
		emitComparisonInstruction(ctx, 0x97); // SETA
	}

	void handleUnsignedGreaterEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned greater than or equal comparison");
		emitComparisonInstruction(ctx, 0x93); // SETAE
	}

	void handleLogicalAnd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "logical AND");

		// For logical AND, we need to implement short-circuit evaluation
		// For now, implement as bitwise AND on boolean values
		auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg);
		std::array<uint8_t, 3> andInst = { encoding.rex_prefix, 0x21, encoding.modrm_byte };
		logAsmEmit("handleLogicalAnd AND", andInst.data(), andInst.size());
		textSectionData.insert(textSectionData.end(), andInst.begin(), andInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLogicalOr(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "logical OR");

		// For logical OR, we need to implement short-circuit evaluation
		// For now, implement as bitwise OR on boolean values
		auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg);
		std::array<uint8_t, 3> orInst = { encoding.rex_prefix, 0x09, encoding.modrm_byte };
		textSectionData.insert(textSectionData.end(), orInst.begin(), orInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLogicalNot(const IrInstruction& instruction) {
		handleUnaryOperation(instruction, UnaryOperation::LogicalNot);
	}

	void handleBitwiseNot(const IrInstruction& instruction) {
		handleUnaryOperation(instruction, UnaryOperation::BitwiseNot);
	}

	void handleNegate(const IrInstruction& instruction) {
		handleUnaryOperation(instruction, UnaryOperation::Negate);
	}

	void storeUnaryResult(const IrOperand& result_operand, X64Register result_physical_reg, int size_in_bits) {
		if (std::holds_alternative<TempVar>(result_operand)) {
			auto result_var = std::get<TempVar>(result_operand);
			auto result_stack_var_addr = getStackOffsetFromTempVar(result_var);
			if (auto res_reg = regAlloc.tryGetStackVariableRegister(result_stack_var_addr); res_reg.has_value()) {
				if (res_reg != result_physical_reg) {
					auto moveOp = regAlloc.get_reg_reg_move_op_code(res_reg.value(), result_physical_reg, size_in_bits / 8);
					textSectionData.insert(textSectionData.end(), moveOp.op_codes.begin(), moveOp.op_codes.begin() + moveOp.size_in_bytes);
				}
			} else {
				auto mov_opcodes = generatePtrMovToFrame(result_physical_reg, result_stack_var_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
			}
		} else if (std::holds_alternative<StringHandle>(result_operand)) {
			StringHandle result_var_name = std::get<StringHandle>(result_operand);
			auto var_id = variable_scopes.back().variables.find(result_var_name);
			if (var_id != variable_scopes.back().variables.end()) {
				auto store_opcodes = generatePtrMovToFrame(result_physical_reg, var_id->second.offset);
				textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(), store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
			}
		}
	}

	void handleFloatAdd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point addition");

		// Use SSE addss (scalar single-precision) or addsd (scalar double-precision)
		// Now properly handles XMM8-XMM15 registers with REX prefix
		if (ctx.result_value.type == Type::Float) {
			// addss xmm_dst, xmm_src (F3 [REX] 0F 58 /r)
			auto inst = generateSSEInstruction(0xF3, 0x0F, 0x58, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.result_value.type == Type::Double) {
			// addsd xmm_dst, xmm_src (F2 [REX] 0F 58 /r)
			auto inst = generateSSEInstruction(0xF2, 0x0F, 0x58, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatSubtract(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point subtraction");

		// Use SSE subss (scalar single-precision) or subsd (scalar double-precision)
		// Now properly handles XMM8-XMM15 registers with REX prefix
		if (ctx.result_value.type == Type::Float) {
			// subss xmm_dst, xmm_src (F3 [REX] 0F 5C /r)
			auto inst = generateSSEInstruction(0xF3, 0x0F, 0x5C, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.result_value.type == Type::Double) {
			// subsd xmm_dst, xmm_src (F2 [REX] 0F 5C /r)
			auto inst = generateSSEInstruction(0xF2, 0x0F, 0x5C, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatMultiply(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point multiplication");

		// Use SSE mulss (scalar single-precision) or mulsd (scalar double-precision)
		// Now properly handles XMM8-XMM15 registers with REX prefix
		if (ctx.result_value.type == Type::Float) {
			// mulss xmm_dst, xmm_src (F3 [REX] 0F 59 /r)
			auto inst = generateSSEInstruction(0xF3, 0x0F, 0x59, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.result_value.type == Type::Double) {
			// mulsd xmm_dst, xmm_src (F2 [REX] 0F 59 /r)
			auto inst = generateSSEInstruction(0xF2, 0x0F, 0x59, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatDivide(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point division");

		// Use SSE divss (scalar single-precision) or divsd (scalar double-precision)
		// Now properly handles XMM8-XMM15 registers with REX prefix
		if (ctx.result_value.type == Type::Float) {
			// divss xmm_dst, xmm_src (F3 [REX] 0F 5E /r)
			auto inst = generateSSEInstruction(0xF3, 0x0F, 0x5E, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else if (ctx.result_value.type == Type::Double) {
			// divsd xmm_dst, xmm_src (F2 [REX] 0F 5E /r)
			auto inst = generateSSEInstruction(0xF2, 0x0F, 0x5E, ctx.result_physical_reg, ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point equal comparison");
		emitFloatComparisonInstruction(ctx, 0x94); // SETE
	}

	void handleFloatNotEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point not equal comparison");
		emitFloatComparisonInstruction(ctx, 0x95); // SETNE
	}

	void handleFloatLessThan(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point less than comparison");
		emitFloatComparisonInstruction(ctx, 0x92); // SETB
	}

	void handleFloatLessEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point less than or equal comparison");
		emitFloatComparisonInstruction(ctx, 0x96); // SETBE
	}

	void handleFloatGreaterThan(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point greater than comparison");
		emitFloatComparisonInstruction(ctx, 0x97); // SETA
	}

	void handleFloatGreaterEqual(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point greater than or equal comparison");
		emitFloatComparisonInstruction(ctx, 0x93); // SETAE
	}

	// Helper: Load operand value (TempVar or variable name) into a register
	X64Register loadOperandIntoRegister(const IrInstruction& instruction, size_t operand_index) {
		X64Register reg = X64Register::Count;
		
		if (instruction.isOperandType<TempVar>(operand_index)) {
			auto temp = instruction.getOperandAs<TempVar>(operand_index);
			auto stack_addr = getStackOffsetFromTempVar(temp);
			if (auto ref_it = reference_stack_info_.find(stack_addr); ref_it != reference_stack_info_.end()) {
				reg = allocateRegisterWithSpilling();
				loadValueFromReferenceSlot(stack_addr, ref_it->second, reg);
				return reg;
			}
			if (auto reg_opt = regAlloc.tryGetStackVariableRegister(stack_addr); reg_opt.has_value()) {
				reg = reg_opt.value();
			} else {
				reg = allocateRegisterWithSpilling();
				auto mov_opcodes = generatePtrMovFromFrame(reg, stack_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), 
					mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				regAlloc.flushSingleDirtyRegister(reg);
			}
		} else if (instruction.isOperandType<StringHandle>(operand_index)) {
			auto var_name = instruction.getOperandAs<StringHandle>(operand_index);
			auto var_id = variable_scopes.back().variables.find(var_name);
			if (var_id != variable_scopes.back().variables.end()) {
				if (auto ref_it = reference_stack_info_.find(var_id->second.offset); ref_it != reference_stack_info_.end()) {
					reg = allocateRegisterWithSpilling();
					loadValueFromReferenceSlot(var_id->second.offset, ref_it->second, reg);
					return reg;
				}
				if (auto reg_opt = regAlloc.tryGetStackVariableRegister(var_id->second.offset); reg_opt.has_value()) {
					reg = reg_opt.value();
				} else {
					reg = allocateRegisterWithSpilling();
					auto mov_opcodes = generatePtrMovFromFrame(reg, var_id->second.offset);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), 
						mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(reg);
				}
			}
		}
		
		return reg;
	}

	X64Register loadTypedValueIntoRegister(const TypedValue& typed_value) {
		X64Register reg = X64Register::Count;
		bool is_signed = isSignedType(typed_value.type);
		
		if (std::holds_alternative<TempVar>(typed_value.value)) {
			auto temp = std::get<TempVar>(typed_value.value);
			auto stack_addr = getStackOffsetFromTempVar(temp);
			if (auto ref_it = reference_stack_info_.find(stack_addr); ref_it != reference_stack_info_.end()) {
				reg = allocateRegisterWithSpilling();
				loadValueFromReferenceSlot(stack_addr, ref_it->second, reg);
				return reg;
			}
			if (auto reg_opt = regAlloc.tryGetStackVariableRegister(stack_addr); reg_opt.has_value()) {
				reg = reg_opt.value();
			} else {
				reg = allocateRegisterWithSpilling();
				// Use size-aware loading: source (stack slot) -> destination (64-bit register)
				emitMovFromFrameSized(
					SizedRegister{reg, 64, false},  // dest: 64-bit register
					SizedStackSlot{stack_addr, typed_value.size_in_bits, is_signed}  // source: sized stack slot
				);
				regAlloc.flushSingleDirtyRegister(reg);
			}
		} else if (std::holds_alternative<StringHandle>(typed_value.value)) {
			StringHandle var_name = std::get<StringHandle>(typed_value.value);
			auto var_id = variable_scopes.back().variables.find(var_name);
			if (var_id != variable_scopes.back().variables.end()) {
				if (auto ref_it = reference_stack_info_.find(var_id->second.offset); ref_it != reference_stack_info_.end()) {
					reg = allocateRegisterWithSpilling();
					loadValueFromReferenceSlot(var_id->second.offset, ref_it->second, reg);
					return reg;
				}
				if (auto reg_opt = regAlloc.tryGetStackVariableRegister(var_id->second.offset); reg_opt.has_value()) {
					reg = reg_opt.value();
				} else {
					reg = allocateRegisterWithSpilling();
					// Use size-aware loading: source (stack slot) -> destination (64-bit register)
					emitMovFromFrameSized(
						SizedRegister{reg, 64, false},  // dest: 64-bit register
						SizedStackSlot{var_id->second.offset, typed_value.size_in_bits, is_signed}  // source: sized stack slot
					);
					regAlloc.flushSingleDirtyRegister(reg);
				}
			}
		} else if (std::holds_alternative<unsigned long long>(typed_value.value)) {
			// Load immediate value
			reg = allocateRegisterWithSpilling();
			unsigned long long imm_value = std::get<unsigned long long>(typed_value.value);
			// MOV reg, immediate (64-bit)
			uint8_t rex = 0x48; // REX.W
			if (static_cast<uint8_t>(reg) >= 8) rex |= 0x01; // REX.B
			textSectionData.push_back(rex);
			textSectionData.push_back(0xB8 + (static_cast<uint8_t>(reg) & 0x07)); // MOV reg, imm64
			for (int i = 0; i < 8; ++i) {
				textSectionData.push_back(static_cast<uint8_t>((imm_value >> (i * 8)) & 0xFF));
			}
		}
		
		return reg;
	}

	const VariableInfo* findVariableInfo(StringHandle name) const {
		for (auto scope_it = variable_scopes.rbegin(); scope_it != variable_scopes.rend(); ++scope_it) {
			auto found = scope_it->variables.find(name);
			if (found != scope_it->variables.end()) {
				return &found->second;
			}
		}
		return nullptr;
	}

	std::optional<int32_t> findIdentifierStackOffset(StringHandle name) const {
		const VariableInfo* info = findVariableInfo(name);
		return info ? std::optional<int32_t>(info->offset) : std::nullopt;
	}

	enum class IncDecKind { PreIncrement, PostIncrement, PreDecrement, PostDecrement };

	struct UnaryOperandLocation {
		enum class Kind { Stack, Global };
		Kind kind = Kind::Stack;
		int32_t stack_offset = 0;
		StringHandle global_name;

		static UnaryOperandLocation stack(int32_t offset) {
			UnaryOperandLocation loc;
			loc.kind = Kind::Stack;
			loc.stack_offset = offset;
			return loc;
		}

		static UnaryOperandLocation global(StringHandle name) {
			UnaryOperandLocation loc;
			loc.kind = Kind::Global;
			loc.global_name = name;
			return loc;
		}
	};

	UnaryOperandLocation resolveUnaryOperandLocation(const IrInstruction& instruction, size_t operand_index) {
		if (instruction.isOperandType<TempVar>(operand_index)) {
			auto temp = instruction.getOperandAs<TempVar>(operand_index);
			return UnaryOperandLocation::stack(getStackOffsetFromTempVar(temp));
		}

		if (instruction.isOperandType<std::string_view>(operand_index)) {
			auto name = instruction.getOperandAs<std::string_view>(operand_index);
			if (auto offset = findIdentifierStackOffset(name); offset.has_value()) {
				return UnaryOperandLocation::stack(offset.value());
			}
			return UnaryOperandLocation::global(StringTable::getOrInternStringHandle(name));
		}

		if (instruction.isOperandType<std::string>(operand_index)) {
			return UnaryOperandLocation::global(StringTable::getOrInternStringHandle(instruction.getOperandAs<std::string>(operand_index)));
		}

		throw std::runtime_error("Unsupported operand type for unary operation");
		return UnaryOperandLocation::stack(0);
	}

	void appendRipRelativePlaceholder(StringHandle global_name) {
		uint32_t reloc_offset = static_cast<uint32_t>(textSectionData.size());
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		textSectionData.push_back(0x00);
		pending_global_relocations_.push_back({reloc_offset, global_name, IMAGE_REL_AMD64_REL32});
	}

	void loadValueFromStack(int32_t offset, int size_in_bits, X64Register target_reg) {
		OpCodeWithSize load_opcodes;
		switch (size_in_bits) {
		case 64:
		case 32:
			emitMovFromFrameBySize(target_reg, offset, size_in_bits);
			break;
		case 16:
			load_opcodes = generateMovzxFromFrame16(target_reg, offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
			break;
		case 8:
			load_opcodes = generateMovzxFromFrame8(target_reg, offset);
			textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
			                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
			break;
		default:
			// Unsupported size (0, 24, 40, 48, 56, etc.) - skip quietly
			FLASH_LOG_FORMAT(Codegen, Warning, "loadValueFromStack: Unsupported size {} bits, skipping", size_in_bits);
			return;
		}
		textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(),
		                       load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
	}

	void emitStoreWordToFrame(X64Register source_reg, int32_t offset) {
		textSectionData.push_back(0x66); // Operand-size override for 16-bit
		bool needs_rex = static_cast<uint8_t>(source_reg) >= static_cast<uint8_t>(X64Register::R8);
		if (needs_rex) {
			uint8_t rex = 0x40 | (1 << 2); // REX.R
			textSectionData.push_back(rex);
		}
		textSectionData.push_back(0x89);
		uint8_t reg_bits = static_cast<uint8_t>(source_reg) & 0x07;
		uint8_t mod_field = (offset >= -128 && offset <= 127) ? 0x01 : 0x02;
		if (offset == 0) {
			mod_field = 0x01;
		}
		uint8_t modrm = (mod_field << 6) | (reg_bits << 3) | 0x05;
		textSectionData.push_back(modrm);
		if (mod_field == 0x01) {
			textSectionData.push_back(static_cast<uint8_t>(offset));
		} else {
			uint32_t offset_u32 = static_cast<uint32_t>(offset);
			textSectionData.push_back(offset_u32 & 0xFF);
			textSectionData.push_back((offset_u32 >> 8) & 0xFF);
			textSectionData.push_back((offset_u32 >> 16) & 0xFF);
			textSectionData.push_back((offset_u32 >> 24) & 0xFF);
		}
	}

	void emitStoreByteToFrame(X64Register source_reg, int32_t offset) {
		bool needs_rex = static_cast<uint8_t>(source_reg) >= static_cast<uint8_t>(X64Register::R8);
		if (needs_rex) {
			uint8_t rex = 0x40 | (1 << 2); // REX.R
			textSectionData.push_back(rex);
		}
		textSectionData.push_back(0x88);
		uint8_t reg_bits = static_cast<uint8_t>(source_reg) & 0x07;
		uint8_t mod_field = (offset >= -128 && offset <= 127) ? 0x01 : 0x02;
		if (offset == 0) {
			mod_field = 0x01;
		}
		uint8_t modrm = (mod_field << 6) | (reg_bits << 3) | 0x05;
		textSectionData.push_back(modrm);
		if (mod_field == 0x01) {
			textSectionData.push_back(static_cast<uint8_t>(offset));
		} else {
			uint32_t offset_u32 = static_cast<uint32_t>(offset);
			textSectionData.push_back(offset_u32 & 0xFF);
			textSectionData.push_back((offset_u32 >> 8) & 0xFF);
			textSectionData.push_back((offset_u32 >> 16) & 0xFF);
			textSectionData.push_back((offset_u32 >> 24) & 0xFF);
		}
	}

	void storeValueToStack(int32_t offset, int size_in_bits, X64Register source_reg) {
		switch (size_in_bits) {
		case 64:
		case 32:
			emitMovToFrameSized(
				SizedRegister{source_reg, 64, false},  // source: 64-bit register
				SizedStackSlot{offset, size_in_bits, false}  // dest: sized stack slot
			);
			break;
		case 16:
			emitStoreWordToFrame(source_reg, offset);
			break;
		case 8:
			emitStoreByteToFrame(source_reg, offset);
			break;
		default:
			// Unsupported size - skip quietly
			FLASH_LOG_FORMAT(Codegen, Warning, "storeValueToStack: Unsupported size {} bits, skipping", size_in_bits);
			break;
		}
	}

	void loadValueFromGlobal(StringHandle global_name, int size_in_bits, X64Register target_reg) {
		uint8_t reg_bits = static_cast<uint8_t>(target_reg) & 0x07;
		bool needs_rex = static_cast<uint8_t>(target_reg) >= static_cast<uint8_t>(X64Register::R8);
		switch (size_in_bits) {
		case 64: {
			uint8_t rex = 0x48;
			if (needs_rex) {
				rex |= (1 << 2); // REX.R
			}
			textSectionData.push_back(rex);
			textSectionData.push_back(0x8B);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		case 32: {
			if (needs_rex) {
				uint8_t rex = 0x40 | (1 << 2); // REX.R
				textSectionData.push_back(rex);
			}
			textSectionData.push_back(0x8B);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		case 16:
		case 8: {
			if (needs_rex) {
				uint8_t rex = 0x40 | (1 << 2); // REX.R
				textSectionData.push_back(rex);
			}
			textSectionData.push_back(0x0F);
			textSectionData.push_back(size_in_bits == 16 ? 0xB7 : 0xB6);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		default:
			// Unsupported size - skip quietly
			FLASH_LOG_FORMAT(Codegen, Warning, "loadValueFromGlobal: Unsupported size {} bits, skipping", size_in_bits);
			break;
		}
	}

	void moveImmediateToRegister(X64Register reg, uint64_t value) {
		uint8_t rex = 0x48;
		if (static_cast<uint8_t>(reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex |= 0x01;
		}
		textSectionData.push_back(rex);
		textSectionData.push_back(0xB8 + (static_cast<uint8_t>(reg) & 0x07));
		for (int i = 0; i < 8; ++i) {
			textSectionData.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
		}
	}

	void loadValuePointedByRegister(X64Register reg, int value_size_bits) {
		int element_size_bytes = value_size_bits / 8;
		if (value_size_bits <= 8) {
			element_size_bytes = 1;
		}
		if (element_size_bytes != 1 && element_size_bytes != 2 && element_size_bytes != 4 && element_size_bytes != 8) {
			// Unsupported size - skip quietly
			FLASH_LOG_FORMAT(Codegen, Warning, "loadValuePointedByRegister: Unsupported size {} bytes, skipping", element_size_bytes);
			return;
		}

		bool use_temp_reg = reg != X64Register::RAX;
		if (use_temp_reg) {
			auto mov_to_rax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, reg, 8);
			textSectionData.insert(textSectionData.end(), mov_to_rax.op_codes.begin(),
				               mov_to_rax.op_codes.begin() + mov_to_rax.size_in_bytes);
		}

		emitLoadFromAddressInRAX(textSectionData, element_size_bytes);

		if (use_temp_reg) {
			auto mov_back = regAlloc.get_reg_reg_move_op_code(reg, X64Register::RAX, 8);
			textSectionData.insert(textSectionData.end(), mov_back.op_codes.begin(),
				               mov_back.op_codes.begin() + mov_back.size_in_bytes);
		}
	}

	void loadValueFromReferenceSlot(int32_t offset, const ReferenceInfo& ref_info, X64Register target_reg) {
		auto load_ptr = generatePtrMovFromFrame(target_reg, offset);
		textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(),
			               load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
		loadValuePointedByRegister(target_reg, ref_info.value_size_bits);
	}

	bool loadAddressForOperand(const IrInstruction& instruction, size_t operand_index, X64Register target_reg) {
		if (instruction.isOperandType<std::string_view>(operand_index)) {
			auto name = instruction.getOperandAs<std::string_view>(operand_index);
			auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(name));
			if (it == variable_scopes.back().variables.end()) {
				return false;
			}
			auto lea = generateLeaFromFrame(target_reg, it->second.offset);
			textSectionData.insert(textSectionData.end(), lea.op_codes.begin(), lea.op_codes.begin() + lea.size_in_bytes);
			return true;
		}
		if (instruction.isOperandType<std::string>(operand_index)) {
			auto name = instruction.getOperandAs<std::string>(operand_index);
			auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(name));
			if (it == variable_scopes.back().variables.end()) {
				return false;
			}
			auto lea = generateLeaFromFrame(target_reg, it->second.offset);
			textSectionData.insert(textSectionData.end(), lea.op_codes.begin(), lea.op_codes.begin() + lea.size_in_bytes);
			return true;
		}
		if (instruction.isOperandType<TempVar>(operand_index)) {
			auto temp = instruction.getOperandAs<TempVar>(operand_index);
			int32_t src_offset = getStackOffsetFromTempVar(temp);
			if (auto ref_it = reference_stack_info_.find(src_offset); ref_it != reference_stack_info_.end()) {
				auto load_ptr = generatePtrMovFromFrame(target_reg, src_offset);
				textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(),
				               load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
				return true;
			}
			auto lea = generateLeaFromFrame(target_reg, src_offset);
			textSectionData.insert(textSectionData.end(), lea.op_codes.begin(), lea.op_codes.begin() + lea.size_in_bytes);
			return true;
		}
		return false;
	}

	void storeValueToGlobal(StringHandle global_name, int size_in_bits, X64Register source_reg) {
		uint8_t reg_bits = static_cast<uint8_t>(source_reg) & 0x07;
		bool needs_rex = static_cast<uint8_t>(source_reg) >= static_cast<uint8_t>(X64Register::R8);
		switch (size_in_bits) {
		case 64: {
			uint8_t rex = 0x48;
			if (needs_rex) {
				rex |= (1 << 2);
			}
			textSectionData.push_back(rex);
			textSectionData.push_back(0x89);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		case 32: {
			if (needs_rex) {
				uint8_t rex = 0x40 | (1 << 2);
				textSectionData.push_back(rex);
			}
			textSectionData.push_back(0x89);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		case 16: {
			textSectionData.push_back(0x66);
			if (needs_rex) {
				uint8_t rex = 0x40 | (1 << 2);
				textSectionData.push_back(rex);
			}
			textSectionData.push_back(0x89);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		case 8: {
			if (needs_rex) {
				uint8_t rex = 0x40 | (1 << 2);
				textSectionData.push_back(rex);
			}
			textSectionData.push_back(0x88);
			uint8_t modrm = 0x05 | (reg_bits << 3);
			textSectionData.push_back(modrm);
			appendRipRelativePlaceholder(global_name);
			break;
		}
		default:
			// Unsupported size - skip quietly
			FLASH_LOG_FORMAT(Codegen, Warning, "storeValueToGlobal: Unsupported size {} bits, skipping", size_in_bits);
			break;
		}
	}

	void loadUnaryOperandValue(const UnaryOperandLocation& location, int size_in_bits, X64Register target_reg) {
		switch (location.kind) {
			case UnaryOperandLocation::Kind::Stack:
				loadValueFromStack(location.stack_offset, size_in_bits, target_reg);
				break;
			case UnaryOperandLocation::Kind::Global:
				loadValueFromGlobal(location.global_name, size_in_bits, target_reg);
				break;
			default:
				throw std::runtime_error("Unhandled UnaryOperandLocation kind in loadUnaryOperandValue");
				break;
		}
	}

	void storeUnaryOperandValue(const UnaryOperandLocation& location, int size_in_bits, X64Register source_reg) {
		switch (location.kind) {
			case UnaryOperandLocation::Kind::Stack:
				storeValueToStack(location.stack_offset, size_in_bits, source_reg);
				break;
			case UnaryOperandLocation::Kind::Global:
				storeValueToGlobal(location.global_name, size_in_bits, source_reg);
				break;
			default:
				throw std::runtime_error("Unhandled UnaryOperandLocation kind in storeUnaryOperandValue");
				break;
		}
	}

	void storeIncDecResultValue(TempVar result_var, X64Register source_reg, int size_in_bits) {
		// getStackOffsetFromTempVar automatically allocates stack space if needed
		int32_t offset = getStackOffsetFromTempVar(result_var);
		storeValueToStack(offset, size_in_bits, source_reg);
	}

	UnaryOperandLocation resolveTypedValueLocation(const TypedValue& typed_value) {
		if (std::holds_alternative<TempVar>(typed_value.value)) {
			auto temp = std::get<TempVar>(typed_value.value);
			return UnaryOperandLocation::stack(getStackOffsetFromTempVar(temp));
		}

		if (std::holds_alternative<StringHandle>(typed_value.value)) {
			StringHandle name = std::get<StringHandle>(typed_value.value);
			if (auto offset = findIdentifierStackOffset(name); offset.has_value()) {
				return UnaryOperandLocation::stack(offset.value());
			}
			return UnaryOperandLocation::global(name);
		}

		// IrValue can also contain immediate values (unsigned long long, double)
		// For inc/dec operations, these should not occur
		throw std::runtime_error("Unsupported typed value for unary operand location (immediate values not allowed)");
		return UnaryOperandLocation::stack(0);
	}

	void emitIncDecInstruction(X64Register target_reg, bool is_increment) {
		uint8_t rex = 0x48;
		if (static_cast<uint8_t>(target_reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex |= 0x01; // Extend r/m field for high registers
		}
		textSectionData.push_back(rex);
		textSectionData.push_back(0x83);
		uint8_t opcode_base = is_increment ? 0xC0 : 0xE8;
		textSectionData.push_back(opcode_base + (static_cast<uint8_t>(target_reg) & 0x07));
		textSectionData.push_back(0x01);
	}

	void handleIncDecCommon(const IrInstruction& instruction, IncDecKind kind) {
		// Extract UnaryOp from typed payload
		const UnaryOp& unary_op = instruction.getTypedPayload<UnaryOp>();

		int size_in_bits = unary_op.value.size_in_bits;
		UnaryOperandLocation operand_location = resolveTypedValueLocation(unary_op.value);
		X64Register target_reg = X64Register::RAX;
		loadUnaryOperandValue(operand_location, size_in_bits, target_reg);

		bool is_post = (kind == IncDecKind::PostIncrement || kind == IncDecKind::PostDecrement);
		bool is_increment = (kind == IncDecKind::PreIncrement || kind == IncDecKind::PostIncrement);

		if (is_post) {
			storeIncDecResultValue(unary_op.result, target_reg, size_in_bits);
		}

		emitIncDecInstruction(target_reg, is_increment);
		storeUnaryOperandValue(operand_location, size_in_bits, target_reg);

		if (!is_post) {
			storeIncDecResultValue(unary_op.result, target_reg, size_in_bits);
		}
	}

	// Helper: Associate result register with result TempVar's stack offset
	void storeConversionResult(const IrInstruction& instruction, X64Register result_reg, int size_in_bits) {
		TempVar result_var;
		// Try to get result from typed payload first
		if (instruction.hasTypedPayload()) {
			const auto& op = instruction.getTypedPayload<TypeConversionOp>();
			result_var = op.result;
		} else {
			result_var = instruction.getOperandAs<TempVar>(0);
		}
		int32_t result_offset = getStackOffsetFromTempVar(result_var);
		regAlloc.set_stack_variable_offset(result_reg, result_offset, size_in_bits);
		// Don't store to memory yet - keep the value in the register for efficiency
	}

	void handlePreIncrement(const IrInstruction& instruction) {
		handleIncDecCommon(instruction, IncDecKind::PreIncrement);
	}

	void handlePostIncrement(const IrInstruction& instruction) {
		handleIncDecCommon(instruction, IncDecKind::PostIncrement);
	}

	void handlePreDecrement(const IrInstruction& instruction) {
		handleIncDecCommon(instruction, IncDecKind::PreDecrement);
	}

	void handlePostDecrement(const IrInstruction& instruction) {
		handleIncDecCommon(instruction, IncDecKind::PostDecrement);
	}

	void handleUnaryOperation(const IrInstruction& instruction, UnaryOperation op) {
		// Extract UnaryOp from typed payload
		const UnaryOp& unary_op = instruction.getTypedPayload<UnaryOp>();

		[[maybe_unused]] Type type = unary_op.value.type;
		int size_in_bits = unary_op.value.size_in_bits;

		// Load the operand into a register
		X64Register result_physical_reg;
		if (std::holds_alternative<TempVar>(unary_op.value.value)) {
			auto temp_var = std::get<TempVar>(unary_op.value.value);
			auto stack_offset = getStackOffsetFromTempVar(temp_var);
			if (auto reg_opt = regAlloc.tryGetStackVariableRegister(stack_offset); reg_opt.has_value()) {
				result_physical_reg = reg_opt.value();
			} else {
				result_physical_reg = allocateRegisterWithSpilling();
				emitMovFromFrameBySize(result_physical_reg, stack_offset, size_in_bits);
				regAlloc.flushSingleDirtyRegister(result_physical_reg);
			}
		} else if (std::holds_alternative<unsigned long long>(unary_op.value.value)) {
			// Load immediate value
			result_physical_reg = allocateRegisterWithSpilling();
			auto imm_value = std::get<unsigned long long>(unary_op.value.value);
			uint8_t rex_prefix = 0x48;
			uint8_t reg_num = static_cast<uint8_t>(result_physical_reg);
			if (reg_num >= 8) {
				rex_prefix |= 0x01;
				reg_num &= 0x07;
			}
			std::array<uint8_t, 10> movInst = { rex_prefix, static_cast<uint8_t>(0xB8 + reg_num), 0, 0, 0, 0, 0, 0, 0, 0 };
			std::memcpy(&movInst[2], &imm_value, sizeof(imm_value));
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		} else if (std::holds_alternative<StringHandle>(unary_op.value.value)) {
			// Load from variable (could be local or global)
			result_physical_reg = allocateRegisterWithSpilling();
			StringHandle var_name = std::get<StringHandle>(unary_op.value.value);
		
			// Check if it's a local variable first
			auto var_id = variable_scopes.back().variables.find(var_name);
			if (var_id != variable_scopes.back().variables.end()) {
				// It's a local variable on the stack - use the correct size
				auto stack_offset = var_id->second.offset;
				emitMovFromFrameBySize(result_physical_reg, stack_offset, size_in_bits);
			} else {
				// It's a global variable - this shouldn't happen for unary ops on locals
				// but we need to handle it for completeness
				throw std::runtime_error("Global variables not yet supported in unary operations");
			}
			regAlloc.flushSingleDirtyRegister(result_physical_reg);
		} else {
			throw std::runtime_error("Unsupported operand type for unary operation");
			result_physical_reg = X64Register::RAX;
		}
		
		// Perform the specific unary operation
		switch (op) {
			case UnaryOperation::LogicalNot: {
				// Compare with 0: cmp reg, 0 (using full instruction encoding with REX support)
				uint8_t reg_num = static_cast<uint8_t>(result_physical_reg);
				uint8_t rex_prefix = 0x48; // REX.W for 64-bit operation
				if (reg_num >= 8) {
					rex_prefix |= 0x01; // Set REX.B for R8-R15
					reg_num &= 0x07;
				}
				uint8_t modrm = 0xF8 | reg_num; // mod=11, opcode_ext=111 (CMP), r/m=reg
				std::array<uint8_t, 4> cmpInst = { rex_prefix, 0x83, modrm, 0x00 };
				textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

				// Set result to 1 if zero (sete), 0 otherwise
				uint8_t sete_rex = 0x00;
				uint8_t sete_reg = static_cast<uint8_t>(result_physical_reg);
				if (sete_reg >= 8) {
					sete_rex = 0x41; // REX with B bit for R8-R15
					sete_reg &= 0x07;
				} else if (sete_reg >= 4) {
					// RSP, RBP, RSI, RDI need REX to access low byte
					sete_rex = 0x40;
				}
				if (sete_rex != 0) {
					textSectionData.push_back(sete_rex);
				}
				std::array<uint8_t, 3> seteInst = { 0x0F, 0x94, static_cast<uint8_t>(0xC0 | sete_reg) };
				textSectionData.insert(textSectionData.end(), seteInst.begin(), seteInst.end());
				break;
			}
			case UnaryOperation::BitwiseNot:
			case UnaryOperation::Negate: {
				// Unified NOT/NEG instruction: REX.W F7 /opcode_ext r64
				uint8_t opcode_ext = static_cast<uint8_t>(op);
				std::array<uint8_t, 3> unaryInst = { 0x48, 0xF7, 0xC0 };
				unaryInst[2] = 0xC0 + (opcode_ext << 3) + static_cast<uint8_t>(result_physical_reg);
				textSectionData.insert(textSectionData.end(), unaryInst.begin(), unaryInst.end());
				break;
			}
		}

		// Store the result - associate register with result temp variable's stack offset
		int32_t result_offset = getStackOffsetFromTempVar(unary_op.result);
		regAlloc.set_stack_variable_offset(result_physical_reg, result_offset, size_in_bits);
	}

	void handleSignExtend(const IrInstruction& instruction) {
		// Sign extension: movsx dest, src
		const ConversionOp& conv_op = instruction.getTypedPayload<ConversionOp>();
		int fromSize = conv_op.from.size_in_bits;
		int toSize = conv_op.to_size;

		// Get source value into a register
		X64Register source_reg = loadTypedValueIntoRegister(conv_op.from);
		
		// Allocate result register
		X64Register result_reg = allocateRegisterWithSpilling();

		// Generate movsx instruction based on size combination
		if (fromSize == 8 && (toSize == 32 || toSize == 64)) {
			// movsx r32/r64, r8: REX 0F BE /r (sign-extend byte to dword/qword)
			uint8_t rex = (toSize == 64) ? 0x48 : 0x40;
			if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04; // REX.R
			if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01; // REX.B
			
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
			std::array<uint8_t, 4> movsx = { rex, 0x0F, 0xBE, modrm };
			textSectionData.insert(textSectionData.end(), movsx.begin(), movsx.end());
		} else if (fromSize == 16 && (toSize == 32 || toSize == 64)) {
			// movsx r32/r64, r16: REX 0F BF /r (sign-extend word to dword/qword)
			uint8_t rex = (toSize == 64) ? 0x48 : 0x40;
			if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04; // REX.R
			if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01; // REX.B
			
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
			std::array<uint8_t, 4> movsx = { rex, 0x0F, 0xBF, modrm };
			textSectionData.insert(textSectionData.end(), movsx.begin(), movsx.end());
		} else if (fromSize == 32 && toSize == 64) {
			// movsxd r64, r32: REX.W 63 /r (sign-extend dword to qword)
			uint8_t rex = 0x48; // REX.W
			if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04; // REX.R
			if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01; // REX.B
			
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
			std::array<uint8_t, 3> movsx = { rex, 0x63, modrm };
			textSectionData.insert(textSectionData.end(), movsx.begin(), movsx.end());
		} else {
			// Fallback or no extension needed: just copy
			auto encoding = encodeRegToRegInstruction(result_reg, source_reg);
			std::array<uint8_t, 3> mov = { encoding.rex_prefix, 0x89, encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
		}

		// Store result - associate register with result temp variable's stack offset
		int32_t result_offset = getStackOffsetFromTempVar(conv_op.result);
		regAlloc.set_stack_variable_offset(result_reg, result_offset, toSize);
	}

	void handleZeroExtend(const IrInstruction& instruction) {
		// Zero extension: movzx dest, src
		const ConversionOp& conv_op = instruction.getTypedPayload<ConversionOp>();
		int fromSize = conv_op.from.size_in_bits;
		int toSize = conv_op.to_size;

		// If source size is 0 (unknown/auto type) or equal to target size, this is a no-op.
		// The value is already in the correct format, just ensure register tracking.
		if (fromSize == 0 || fromSize == toSize) {
			// Get source value's register (or load it if needed)
			X64Register source_reg = loadTypedValueIntoRegister(conv_op.from);
			// Associate it with the result TempVar - no code generation needed
			int32_t result_offset = getStackOffsetFromTempVar(conv_op.result);
			regAlloc.set_stack_variable_offset(source_reg, result_offset, toSize);
			return;
		}

		// Get source value into a register
		X64Register source_reg = loadTypedValueIntoRegister(conv_op.from);
		
		// Allocate result register
		X64Register result_reg = allocateRegisterWithSpilling();

		// Generate movzx instruction
		if (fromSize == 8 && toSize == 32) {
			// movzx r32, r8: 0F B6 /r
			auto encoding = encodeRegToRegInstruction(result_reg, source_reg);
			std::array<uint8_t, 4> movzx = { encoding.rex_prefix, 0x0F, 0xB6, encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), movzx.begin(), movzx.end());
		} else if (fromSize == 16 && toSize == 32) {
			// movzx r32, r16: 0F B7 /r
			auto encoding = encodeRegToRegInstruction(result_reg, source_reg);
			std::array<uint8_t, 4> movzx = { encoding.rex_prefix, 0x0F, 0xB7, encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), movzx.begin(), movzx.end());
		} else if (fromSize == 32 && toSize == 64) {
			// mov r32, r32 (implicitly zero-extends to 64 bits on x86-64)
			std::array<uint8_t, 2> mov = { 0x89, 0xC0 };
			mov[1] = 0xC0 + (static_cast<uint8_t>(source_reg) << 3) + static_cast<uint8_t>(result_reg);
			textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
		} else {
			// Fallback: just copy
			auto encoding = encodeRegToRegInstruction(result_reg, source_reg);
			std::array<uint8_t, 3> mov = { encoding.rex_prefix, 0x89, encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
		}

		// Store result - associate register with result temp variable's stack offset
		int32_t result_offset = getStackOffsetFromTempVar(conv_op.result);
		regAlloc.set_stack_variable_offset(result_reg, result_offset, toSize);
	}

	void handleTruncate(const IrInstruction& instruction) {
		// Truncation: just use the lower bits by moving to a smaller register
		const ConversionOp& conv_op = instruction.getTypedPayload<ConversionOp>();
		int toSize = conv_op.to_size;

		// Get source value into a register
		X64Register source_reg = loadTypedValueIntoRegister(conv_op.from);
		
		// Allocate result register
		X64Register result_reg = allocateRegisterWithSpilling();

		// Generate appropriate MOV instruction based on target size
		// On x86-64, moving to a smaller register automatically truncates
		if (toSize == 8) {
			// mov r8, r8 (byte to byte) - just copy the low byte
			// Use movzx to ensure we only get the low byte
			uint8_t rex = 0x40;
			if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04; // REX.R
			if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01; // REX.B
			
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
			std::array<uint8_t, 4> movzx = { rex, 0x0F, 0xB6, modrm };
			logAsmEmit("handleTruncate 8-bit MOVZX", movzx.data(), movzx.size());
			textSectionData.insert(textSectionData.end(), movzx.begin(), movzx.end());
		} else if (toSize == 16) {
			// mov r16, r16 (word to word)
			// Use movzx to ensure we only get the low word
			uint8_t rex = 0x40;
			if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04; // REX.R
			if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01; // REX.B
			
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
			std::array<uint8_t, 4> movzx = { rex, 0x0F, 0xB7, modrm };
			textSectionData.insert(textSectionData.end(), movzx.begin(), movzx.end());
		} else if (toSize == 32) {
			// mov r32, r32 (dword to dword) - implicitly zero-extends on x86-64
			// For MOV r/m32, r32 (opcode 89): reg field is SOURCE, r/m field is DEST
			// So we put source_reg in reg field and result_reg in r/m field
			uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(source_reg) & 0x07) << 3) | (static_cast<uint8_t>(result_reg) & 0x07);
			
			// Check if we need REX prefix
			if (static_cast<uint8_t>(result_reg) >= 8 || static_cast<uint8_t>(source_reg) >= 8) {
				uint8_t rex = 0x40;
				if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x04; // REX.R for source in reg field
				if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x01; // REX.B for dest in r/m field
				std::array<uint8_t, 3> mov = { rex, 0x89, modrm };
				textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
			} else {
				std::array<uint8_t, 2> mov = { 0x89, modrm };
				textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
			}
		} else {
			// 64-bit or fallback: just copy the whole register
			// For MOV r/m64, r64 (opcode 89): reg field is SOURCE, r/m field is DEST
			auto encoding = encodeRegToRegInstruction(source_reg, result_reg);
			std::array<uint8_t, 3> mov = { encoding.rex_prefix, 0x89, encoding.modrm_byte };
			textSectionData.insert(textSectionData.end(), mov.begin(), mov.end());
		}

		// Store result - associate register with result temp variable's stack offset
		int32_t result_offset = getStackOffsetFromTempVar(conv_op.result);
		regAlloc.set_stack_variable_offset(result_reg, result_offset, toSize);
	}

	void handleFloatToInt(const IrInstruction& instruction) {
		// FloatToInt: convert float/double to integer
		auto& op = instruction.getTypedPayload<TypeConversionOp>();
		
		// Load source value into XMM register
		X64Register source_xmm = X64Register::Count;
		if (std::holds_alternative<TempVar>(op.from.value)) {
			auto temp_var = std::get<TempVar>(op.from.value);
			auto stack_offset = getStackOffsetFromTempVar(temp_var);
			// Check if the value is already in an XMM register
			if (auto existing_reg = regAlloc.tryGetStackVariableRegister(stack_offset); existing_reg.has_value()) {
				source_xmm = existing_reg.value();
			} else {
				source_xmm = allocateXMMRegisterWithSpilling();
				bool is_float = (op.from.type == Type::Float);
				emitFloatMovFromFrame(source_xmm, stack_offset, is_float);
			}
		} else {
			assert(std::holds_alternative<StringHandle>(op.from.value) && "Expected StringHandle or TempVar type");
			StringHandle var_name = std::get<StringHandle>(op.from.value);
			auto var_it = variable_scopes.back().variables.find(var_name);
			assert(var_it != variable_scopes.back().variables.end() && "Variable not found in variables");
			// Check if the value is already in an XMM register
			if (auto existing_reg = regAlloc.tryGetStackVariableRegister(var_it->second.offset); existing_reg.has_value()) {
				source_xmm = existing_reg.value();
			} else {
				source_xmm = allocateXMMRegisterWithSpilling();
				bool is_float = (op.from.type == Type::Float);
				emitFloatMovFromFrame(source_xmm, var_it->second.offset, is_float);
			}
		}

		// Allocate result GPR
		X64Register result_reg = allocateRegisterWithSpilling();

		// cvttss2si (float to int) or cvttsd2si (double to int)
		// For 32-bit: F3 0F 2C /r (cvttss2si r32, xmm) or F2 0F 2C /r (cvttsd2si r32, xmm)
		// For 64-bit: F3 REX.W 0F 2C /r (cvttss2si r64, xmm) or F2 REX.W 0F 2C /r (cvttsd2si r64, xmm)
		bool is_float = (op.from.type == Type::Float);
		uint8_t prefix = is_float ? 0xF3 : 0xF2;
		
		// Only use REX.W for 64-bit result
		bool need_rex_w = (op.to_size_in_bits == 64);
		uint8_t rex = need_rex_w ? 0x48 : 0x40;
		
		// Add REX.R if result register >= 8
		if (static_cast<uint8_t>(result_reg) >= 8) rex |= 0x04;
		
		// Add REX.B if XMM register >= 8
		uint8_t xmm_bits = static_cast<uint8_t>(source_xmm) - static_cast<uint8_t>(X64Register::XMM0);
		if (xmm_bits >= 8) rex |= 0x01;

		uint8_t modrm = 0xC0 | ((static_cast<uint8_t>(result_reg) & 0x07) << 3) | (xmm_bits & 0x07);
		
		// Only emit REX prefix if needed (64-bit or extended registers)
		if (rex != 0x40) {
			std::array<uint8_t, 5> cvtt = { prefix, rex, 0x0F, 0x2C, modrm };
			textSectionData.insert(textSectionData.end(), cvtt.begin(), cvtt.end());
		} else {
			std::array<uint8_t, 4> cvtt = { prefix, 0x0F, 0x2C, modrm };
			textSectionData.insert(textSectionData.end(), cvtt.begin(), cvtt.end());
		}

		// Release XMM register
		regAlloc.release(source_xmm);

		// Store result
		storeConversionResult(instruction, result_reg, op.to_size_in_bits);
	}

	void handleIntToFloat(const IrInstruction& instruction) {
		// IntToFloat: convert integer to float/double
		auto& op = instruction.getTypedPayload<TypeConversionOp>();

		// Load source value into GPR
		X64Register source_reg = loadTypedValueIntoRegister(op.from);

		// Allocate result XMM register
		X64Register result_xmm = allocateXMMRegisterWithSpilling();

		// cvtsi2ss (int to float) or cvtsi2sd (int to double)
		// Opcode: F3 REX.W 0F 2A /r (cvtsi2ss xmm, r64) for float
		// Opcode: F2 REX.W 0F 2A /r (cvtsi2sd xmm, r64) for double
		bool is_float = (op.to_type == Type::Float);
		uint8_t prefix = is_float ? 0xF3 : 0xF2;
		
		uint8_t rex = 0x48;  // REX.W for 64-bit source
		uint8_t xmm_bits = static_cast<uint8_t>(result_xmm) - static_cast<uint8_t>(X64Register::XMM0);
		if (xmm_bits >= 8) rex |= 0x04;  // REX.R
		if (static_cast<uint8_t>(source_reg) >= 8) rex |= 0x01;  // REX.B

		uint8_t modrm = 0xC0 | ((xmm_bits & 0x07) << 3) | (static_cast<uint8_t>(source_reg) & 0x07);
		std::array<uint8_t, 5> cvt = { prefix, rex, 0x0F, 0x2A, modrm };
		textSectionData.insert(textSectionData.end(), cvt.begin(), cvt.end());

		// Release source GPR
		regAlloc.release(source_reg);

		// Store result XMM to stack
		auto result_offset = getStackOffsetFromTempVar(op.result);
		emitFloatStoreToAddressWithOffset(textSectionData, result_xmm, X64Register::RBP, result_offset, is_float);
		regAlloc.set_stack_variable_offset(result_xmm, result_offset, op.to_size_in_bits);
	}

	void handleFloatToFloat(const IrInstruction& instruction) {
		// FloatToFloat: convert float <-> double
		auto& op = instruction.getTypedPayload<TypeConversionOp>();

		// Load source value into XMM register
		X64Register source_xmm = X64Register::Count;
		if (std::holds_alternative<TempVar>(op.from.value)) {
			auto temp_var = std::get<TempVar>(op.from.value);
			auto stack_offset = getStackOffsetFromTempVar(temp_var);
			source_xmm = allocateXMMRegisterWithSpilling();
			bool is_float = (op.from.type == Type::Float);
			emitFloatMovFromFrame(source_xmm, stack_offset, is_float);
		} else if (std::holds_alternative<StringHandle>(op.from.value)) {
			StringHandle var_name = std::get<StringHandle>(op.from.value);
			auto var_it = variable_scopes.back().variables.find(var_name);
			assert(var_it != variable_scopes.back().variables.end());
			source_xmm = allocateXMMRegisterWithSpilling();
			bool is_float = (op.from.type == Type::Float);
			emitFloatMovFromFrame(source_xmm, var_it->second.offset, is_float);
		}

		// Allocate result XMM register
		X64Register result_xmm = allocateXMMRegisterWithSpilling();

		// cvtss2sd (float to double) or cvtsd2ss (double to float)
		// Now properly handles XMM8-XMM15 registers with REX prefix
		if (op.from.type == Type::Float && op.to_type == Type::Double) {
			// cvtss2sd xmm, xmm (F3 [REX] 0F 5A /r)
			auto inst = generateSSEInstruction(0xF3, 0x0F, 0x5A, result_xmm, source_xmm);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		} else {
			// cvtsd2ss xmm, xmm (F2 [REX] 0F 5A /r)
			auto inst = generateSSEInstruction(0xF2, 0x0F, 0x5A, result_xmm, source_xmm);
			textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
		}

		// Release source XMM
		regAlloc.release(source_xmm);

		// Store result XMM to stack
		auto result_offset = getStackOffsetFromTempVar(op.result);
		bool is_float_result = (op.to_type == Type::Float);
		emitFloatStoreToAddressWithOffset(textSectionData, result_xmm, X64Register::RBP, result_offset, is_float_result);
		regAlloc.set_stack_variable_offset(result_xmm, result_offset, op.to_size_in_bits);
	}

	void handleAddAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "add assignment");
		
		// Check if this is floating-point addition
		if (ctx.result_value.type == Type::Float || ctx.result_value.type == Type::Double) {
			// Use SSE addss (scalar single-precision) or addsd (scalar double-precision)
			if (ctx.result_value.type == Type::Float) {
				// addss xmm_dst, xmm_src (F3 [REX] 0F 58 /r)
				auto inst = generateSSEInstruction(0xF3, 0x0F, 0x58, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			} else {
				// addsd xmm_dst, xmm_src (F2 [REX] 0F 58 /r)
				auto inst = generateSSEInstruction(0xF2, 0x0F, 0x58, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			}
		} else {
			// Integer addition: Use correct register size based on operand size
			// Pass include_rex_w=false for 32-bit operations
			bool include_rex_w = (ctx.operand_size_in_bits == 64);
			auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg, include_rex_w);
			
			// Only emit REX prefix if needed (will be 0 for 32-bit with regs < 8)
			if (encoding.rex_prefix != 0) {
				textSectionData.push_back(encoding.rex_prefix);
			}
			textSectionData.push_back(0x01); // ADD opcode
			textSectionData.push_back(encoding.modrm_byte);
		}
		storeArithmeticResult(ctx);
	}

	void handleSubAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "subtract assignment");
		
		// Check if this is floating-point subtraction
		if (ctx.result_value.type == Type::Float || ctx.result_value.type == Type::Double) {
			// Use SSE subss (scalar single-precision) or subsd (scalar double-precision)
			if (ctx.result_value.type == Type::Float) {
				// subss xmm_dst, xmm_src (F3 [REX] 0F 5C /r)
				auto inst = generateSSEInstruction(0xF3, 0x0F, 0x5C, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			} else {
				// subsd xmm_dst, xmm_src (F2 [REX] 0F 5C /r)
				auto inst = generateSSEInstruction(0xF2, 0x0F, 0x5C, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			}
		} else {
			// Integer subtraction: Use correct register size based on operand size
			// Pass include_rex_w=false for 32-bit operations
			bool include_rex_w = (ctx.operand_size_in_bits == 64);
			auto encoding = encodeRegToRegInstruction(ctx.rhs_physical_reg, ctx.result_physical_reg, include_rex_w);
			
			// Only emit REX prefix if needed (will be 0 for 32-bit with regs < 8)
			if (encoding.rex_prefix != 0) {
				textSectionData.push_back(encoding.rex_prefix);
			}
			textSectionData.push_back(0x29); // SUB opcode
			textSectionData.push_back(encoding.modrm_byte);
		}
		storeArithmeticResult(ctx);
	}

	void handleMulAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "multiply assignment");
		
		// Check if this is floating-point multiplication
		if (ctx.result_value.type == Type::Float || ctx.result_value.type == Type::Double) {
			// Use SSE mulss (scalar single-precision) or mulsd (scalar double-precision)
			if (ctx.result_value.type == Type::Float) {
				// mulss xmm_dst, xmm_src (F3 [REX] 0F 59 /r)
				auto inst = generateSSEInstruction(0xF3, 0x0F, 0x59, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			} else {
				// mulsd xmm_dst, xmm_src (F2 [REX] 0F 59 /r)
				auto inst = generateSSEInstruction(0xF2, 0x0F, 0x59, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			}
		} else {
			// Integer multiplication: IMUL r64, r/m64
			// Use correct register size based on operand size
			// Note: For IMUL, the reg field is the destination (result) and rm is the source (rhs)
			bool include_rex_w = (ctx.operand_size_in_bits == 64);
			auto encoding = encodeRegToRegInstruction(ctx.result_physical_reg, ctx.rhs_physical_reg, include_rex_w);
			
			// Only emit REX prefix if needed
			if (encoding.rex_prefix != 0) {
				textSectionData.push_back(encoding.rex_prefix);
			}
			textSectionData.push_back(0x0F);
			textSectionData.push_back(0xAF);
			textSectionData.push_back(encoding.modrm_byte);
		}
		storeArithmeticResult(ctx);
	}

	void handleDivAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "divide assignment");
		
		// Check if this is floating-point division
		if (ctx.result_value.type == Type::Float || ctx.result_value.type == Type::Double) {
			// Use SSE divss (scalar single-precision) or divsd (scalar double-precision)
			if (ctx.result_value.type == Type::Float) {
				// divss xmm_dst, xmm_src (F3 [REX] 0F 5E /r)
				auto inst = generateSSEInstruction(0xF3, 0x0F, 0x5E, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			} else {
				// divsd xmm_dst, xmm_src (F2 [REX] 0F 5E /r)
				auto inst = generateSSEInstruction(0xF2, 0x0F, 0x5E, ctx.result_physical_reg, ctx.rhs_physical_reg);
				textSectionData.insert(textSectionData.end(), inst.op_codes.begin(), inst.op_codes.begin() + inst.size_in_bytes);
			}
		} else {
			// Integer division
			// Use correct register size based on operand size
			bool include_rex_w = (ctx.operand_size_in_bits == 64);
			
			// mov rax, result_reg (move dividend to RAX)
			auto mov_to_rax = encodeRegToRegInstruction(ctx.result_physical_reg, X64Register::RAX, include_rex_w);
			if (mov_to_rax.rex_prefix != 0) {
				textSectionData.push_back(mov_to_rax.rex_prefix);
			}
			textSectionData.push_back(0x89);
			textSectionData.push_back(mov_to_rax.modrm_byte);
			
			// Sign extend based on operand size
			if (ctx.operand_size_in_bits == 64) {
				// cqo (sign extend RAX to RDX:RAX)
				std::array<uint8_t, 2> cqoInst = { 0x48, 0x99 };
				textSectionData.insert(textSectionData.end(), cqoInst.begin(), cqoInst.end());
			} else {
				// cdq (sign extend EAX to EDX:EAX) - 32-bit
				textSectionData.push_back(0x99);
			}
			
			// idiv rhs_reg (divide RDX:RAX by rhs_reg, quotient in RAX)
			emitOpcodeExtInstruction(0xF7, X64OpcodeExtension::IDIV, ctx.rhs_physical_reg, ctx.operand_size_in_bits);
			
			// mov result_reg, rax (move quotient to result)
			auto mov_from_rax = encodeRegToRegInstruction(X64Register::RAX, ctx.result_physical_reg, include_rex_w);
			if (mov_from_rax.rex_prefix != 0) {
				textSectionData.push_back(mov_from_rax.rex_prefix);
			}
			textSectionData.push_back(0x89);
			textSectionData.push_back(mov_from_rax.modrm_byte);
		}
		
		storeArithmeticResult(ctx);
	}

	void handleModAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "modulo assignment");
		
		// Use correct register size based on operand size
		bool include_rex_w = (ctx.operand_size_in_bits == 64);
		
		// mov rax, result_reg (move dividend to RAX)
		auto mov_to_rax = encodeRegToRegInstruction(ctx.result_physical_reg, X64Register::RAX, include_rex_w);
		if (mov_to_rax.rex_prefix != 0) {
			textSectionData.push_back(mov_to_rax.rex_prefix);
		}
		textSectionData.push_back(0x89);
		textSectionData.push_back(mov_to_rax.modrm_byte);
		
		// Sign extend based on operand size
		if (ctx.operand_size_in_bits == 64) {
			// cqo (sign extend RAX to RDX:RAX)
			std::array<uint8_t, 2> cqoInst = { 0x48, 0x99 };
			textSectionData.insert(textSectionData.end(), cqoInst.begin(), cqoInst.end());
		} else {
			// cdq (sign extend EAX to EDX:EAX) - 32-bit
			textSectionData.push_back(0x99);
		}
		
		// idiv rhs_reg (divide RDX:RAX by rhs_reg, remainder in RDX)
		emitOpcodeExtInstruction(0xF7, X64OpcodeExtension::IDIV, ctx.rhs_physical_reg, ctx.operand_size_in_bits);
		
		// mov result_reg, rdx (move remainder to result)
		auto mov_from_rdx = encodeRegToRegInstruction(X64Register::RDX, ctx.result_physical_reg, include_rex_w);
		if (mov_from_rdx.rex_prefix != 0) {
			textSectionData.push_back(mov_from_rdx.rex_prefix);
		}
		textSectionData.push_back(0x89);
		textSectionData.push_back(mov_from_rdx.modrm_byte);
		
		storeArithmeticResult(ctx);
	}

	void handleAndAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise and assignment");
		emitBinaryOpInstruction(0x21, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.result_value.size_in_bits);
		storeArithmeticResult(ctx);
	}

	void handleOrAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise or assignment");
		emitBinaryOpInstruction(0x09, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.result_value.size_in_bits);
		storeArithmeticResult(ctx);
	}

	void handleXorAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise xor assignment");
		emitBinaryOpInstruction(0x31, ctx.rhs_physical_reg, ctx.result_physical_reg, ctx.result_value.size_in_bits);
		storeArithmeticResult(ctx);
	}

	void handleShlAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift left assignment");
		auto bin_op = *getTypedPayload<BinaryOp>(instruction);
		
		// Move RHS to CL register (using RHS size for the move)
		emitMovRegToReg(ctx.rhs_physical_reg, X64Register::RCX, bin_op.rhs.size_in_bits);
		
		// Emit SHL instruction with correct size
		emitOpcodeExtInstruction(0xD3, X64OpcodeExtension::SHL, ctx.result_physical_reg, ctx.result_value.size_in_bits);
		
		storeArithmeticResult(ctx);
	}

	void handleShrAssign(const IrInstruction& instruction) {
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift right assignment");
		auto bin_op = *getTypedPayload<BinaryOp>(instruction);
		
		// Move RHS to CL register (using RHS size for the move)
		emitMovRegToReg(ctx.rhs_physical_reg, X64Register::RCX, bin_op.rhs.size_in_bits);
		
		// Emit SAR instruction with correct size
		emitOpcodeExtInstruction(0xD3, X64OpcodeExtension::SAR, ctx.result_physical_reg, ctx.result_value.size_in_bits);
		
		storeArithmeticResult(ctx);
	}

	void handleAssignment(const IrInstruction& instruction) {
		// Use typed payload format
		const AssignmentOp& op = instruction.getTypedPayload<AssignmentOp>();
		FLASH_LOG(Codegen, Debug, "handleAssignment called");
		Type lhs_type = op.lhs.type;
		//int lhs_size_bits = instruction.getOperandAs<int>(2);

		// Special handling for pointer store (assignment through pointer)
		if (op.is_pointer_store) {
			// LHS is a pointer (TempVar), RHS is the value to store
			// Load the pointer into a register
			X64Register ptr_reg = allocateRegisterWithSpilling();
			if (std::holds_alternative<TempVar>(op.lhs.value)) {
				TempVar ptr_var = std::get<TempVar>(op.lhs.value);
				int32_t ptr_offset = getStackOffsetFromTempVar(ptr_var);
				emitMovFromFrame(ptr_reg, ptr_offset);
			} else {
				throw std::runtime_error("Pointer store LHS must be a TempVar");
				return;
			}
			
			// Get the value to store
			X64Register value_reg = allocateRegisterWithSpilling();
			int value_size_bytes = op.rhs.size_in_bits / 8;
			
			if (std::holds_alternative<unsigned long long>(op.rhs.value)) {
				// Immediate integer value
				unsigned long long imm_value = std::get<unsigned long long>(op.rhs.value);
				if (value_size_bytes == 8) {
					emitMovImm64(value_reg, imm_value);
				} else {
					moveImmediateToRegister(value_reg, static_cast<int32_t>(imm_value));
				}
			} else if (std::holds_alternative<double>(op.rhs.value)) {
				// Immediate double value
				double double_value = std::get<double>(op.rhs.value);
				uint64_t bits;
				std::memcpy(&bits, &double_value, sizeof(bits));
				emitMovImm64(value_reg, bits);
			} else if (std::holds_alternative<TempVar>(op.rhs.value)) {
				// Load from temp var
				TempVar rhs_var = std::get<TempVar>(op.rhs.value);
				int32_t rhs_offset = getStackOffsetFromTempVar(rhs_var);
				emitMovFromFrameBySize(value_reg, rhs_offset, op.rhs.size_in_bits);
			} else {
				throw std::runtime_error("Unsupported RHS type for pointer store");
				return;
			}
			
			// Store through the pointer: [ptr_reg] = value_reg
			emitStoreToMemory(textSectionData, value_reg, ptr_reg, 0, value_size_bytes);
			
			regAlloc.release(ptr_reg);
			regAlloc.release(value_reg);
			return;
		}

		// Special handling for function pointer assignment
		if (lhs_type == Type::FunctionPointer) {
			// Get LHS destination
			int32_t lhs_offset = -1;

			if (std::holds_alternative<StringHandle>(op.lhs.value)) {
				StringHandle lhs_var_name_handle = std::get<StringHandle>(op.lhs.value);
			std::string_view lhs_var_name = StringTable::getStringView(lhs_var_name_handle);
				auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(lhs_var_name));
				if (it != variable_scopes.back().variables.end()) {
					lhs_offset = it->second.offset;
				}
			} else if (std::holds_alternative<TempVar>(op.lhs.value)) {
				TempVar lhs_var = std::get<TempVar>(op.lhs.value);
				lhs_offset = getStackOffsetFromTempVar(lhs_var);
			}

			if (lhs_offset == -1) {
				throw std::runtime_error("LHS variable not found in function pointer assignment");
				return;
			}

			// Get RHS source (function address or nullptr)
			X64Register source_reg = X64Register::RAX;

			if (std::holds_alternative<TempVar>(op.rhs.value)) {
				TempVar rhs_var = std::get<TempVar>(op.rhs.value);
				int32_t rhs_offset = getStackOffsetFromTempVar(rhs_var);

				// Load function address from RHS stack location into RAX
				emitMovFromFrame(source_reg, rhs_offset);
			} else if (std::holds_alternative<unsigned long long>(op.rhs.value)) {
				// RHS is an immediate value (e.g., nullptr = 0)
				unsigned long long rhs_value = std::get<unsigned long long>(op.rhs.value);
				emitMovImm64(source_reg, rhs_value);
			}
			
			// Store RAX to LHS stack location (8 bytes for function pointer - always 64-bit)
			emitMovToFrameSized(
				SizedRegister{source_reg, 64, false},  // source: 64-bit register
				SizedStackSlot{lhs_offset, 64, false}  // dest: 64-bit for function pointer
			);

			// Clear any stale register associations for this stack offset
			// This ensures subsequent loads will actually load from memory instead of using stale cached values
			regAlloc.clearStackVariableAssociations(lhs_offset);

			return;
		}
	
		// Special handling for struct assignment
		if (lhs_type == Type::Struct) {
			// For struct assignment, we need to copy the entire struct value
			// LHS is the destination (should be a variable name or TempVar)
			// RHS is the source (should be a TempVar from function return, or another variable)

			// Get LHS destination
			int32_t lhs_offset = -1;

			if (std::holds_alternative<StringHandle>(op.lhs.value)) {
				StringHandle lhs_var_name_handle = std::get<StringHandle>(op.lhs.value);
			std::string_view lhs_var_name = StringTable::getStringView(lhs_var_name_handle);
				auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(lhs_var_name));
				if (it != variable_scopes.back().variables.end()) {
					lhs_offset = it->second.offset;
				}
			} else if (std::holds_alternative<TempVar>(op.lhs.value)) {
				TempVar lhs_var = std::get<TempVar>(op.lhs.value);
				lhs_offset = getStackOffsetFromTempVar(lhs_var);
			}

			if (lhs_offset == -1) {
				throw std::runtime_error("LHS variable not found in struct assignment");
				return;
			}

			// Get RHS source offset
			int32_t rhs_offset = -1;
			if (std::holds_alternative<StringHandle>(op.rhs.value)) {
				StringHandle rhs_var_name_handle = std::get<StringHandle>(op.rhs.value);
			std::string_view rhs_var_name = StringTable::getStringView(rhs_var_name_handle);
				auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(rhs_var_name));
				if (it != variable_scopes.back().variables.end()) {
					rhs_offset = it->second.offset;
				}
			} else if (std::holds_alternative<TempVar>(op.rhs.value)) {
				TempVar rhs_var = std::get<TempVar>(op.rhs.value);
				rhs_offset = getStackOffsetFromTempVar(rhs_var);
			}

			if (rhs_offset == -1) {
				throw std::runtime_error("RHS variable not found in struct assignment");
				return;
			}

			// Get struct size in bytes from TypedValue (round up to handle partial bytes)
			int struct_size_bytes = (op.lhs.size_in_bits + 7) / 8;
			
			// Copy struct using 8-byte chunks, then handle remaining bytes
			int offset = 0;
			while (offset + 8 <= struct_size_bytes) {
				// Load 8 bytes from RHS: MOV RAX, [RBP + rhs_offset + offset]
				emitMovFromFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{rhs_offset + offset, 64, false}
				);
				// Store 8 bytes to LHS: MOV [RBP + lhs_offset + offset], RAX
				emitMovToFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{lhs_offset + offset, 64, false}
				);
				offset += 8;
			}
			
			// Handle remaining bytes (4, 2, 1)
			if (offset + 4 <= struct_size_bytes) {
				emitMovFromFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{rhs_offset + offset, 32, false}
				);
				emitMovToFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{lhs_offset + offset, 32, false}
				);
				offset += 4;
			}
			if (offset + 2 <= struct_size_bytes) {
				emitMovFromFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{rhs_offset + offset, 16, false}
				);
				emitMovToFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{lhs_offset + offset, 16, false}
				);
				offset += 2;
			}
			if (offset + 1 <= struct_size_bytes) {
				emitMovFromFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{rhs_offset + offset, 8, false}
				);
				emitMovToFrameSized(
					SizedRegister{X64Register::RAX, 64, false},
					SizedStackSlot{lhs_offset + offset, 8, false}
				);
			}
			return;
		}

		// For non-struct types, we need to copy the value from RHS to LHS
		// Get LHS destination
		int32_t lhs_offset = -1;

		if (std::holds_alternative<StringHandle>(op.lhs.value)) {
			StringHandle lhs_var_name_handle = std::get<StringHandle>(op.lhs.value);
			std::string_view lhs_var_name = StringTable::getStringView(lhs_var_name_handle);
			auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(lhs_var_name));
			if (it != variable_scopes.back().variables.end()) {
				lhs_offset = it->second.offset;
			} else {
				FLASH_LOG(Codegen, Error, "String LHS variable '", lhs_var_name, "' not found in variables map");
			}
		} else if (std::holds_alternative<TempVar>(op.lhs.value)) {
			TempVar lhs_var = std::get<TempVar>(op.lhs.value);
			// TempVar(0) is a sentinel value indicating an invalid/uninitialized temp variable
			// This can happen with template functions that have reference parameters
			// In this case, the assignment should not have been generated - report error and skip
			if (lhs_var.var_number == 0) {
				FLASH_LOG(Codegen, Error, "Invalid assignment to sentinel TempVar(0) - likely a code generation bug with template reference parameters");
				return;  // Skip this invalid assignment
			}
			lhs_offset = getStackOffsetFromTempVar(lhs_var);
			if (lhs_offset == -1) {
				FLASH_LOG(Codegen, Error, "TempVar LHS with var_number=", lhs_var.var_number, " (name='", lhs_var.name(), "') not found");
			}
		} else if (std::holds_alternative<unsigned long long>(op.lhs.value)) {
			unsigned long long lhs_value = std::get<unsigned long long>(op.lhs.value);
			std::ostringstream rhs_str;
			printTypedValue(rhs_str, op.rhs);
			FLASH_LOG(Codegen, Error, "[Line ", instruction.getLineNumber(), "] LHS is an immediate value (", lhs_value, ") - invalid for assignment. RHS: ", rhs_str.str());
			return;
		} else if (std::holds_alternative<double>(op.lhs.value)) {
			double lhs_value = std::get<double>(op.lhs.value);
			std::ostringstream rhs_str;
			printTypedValue(rhs_str, op.rhs);
			FLASH_LOG(Codegen, Error, "[Line ", instruction.getLineNumber(), "] LHS is an immediate value (", lhs_value, ") - invalid for assignment. RHS: ", rhs_str.str());
			return;
		} else {
			FLASH_LOG(Codegen, Error, "LHS value has completely unexpected type in variant");
			return;
		}

		if (lhs_offset == -1) {
			FLASH_LOG(Codegen, Error, "LHS variable not found in assignment - skipping");
			return;
		}

		// Check if LHS is a reference - if so, we're initializing a reference binding
		auto lhs_ref_it = reference_stack_info_.find(lhs_offset);
		
		// Debug: check what type LHS is
		if (std::holds_alternative<StringHandle>(op.lhs.value)) {
			FLASH_LOG(Codegen, Debug, "LHS is string_view: '", std::get<StringHandle>(op.lhs.value), "'");
		} else if (std::holds_alternative<TempVar>(op.lhs.value)) {
			FLASH_LOG(Codegen, Debug, "LHS is TempVar: '", std::get<TempVar>(op.lhs.value).name(), "'");
		} else {
			FLASH_LOG(Codegen, Debug, "LHS is other type");
		}
		
		// If not found with TempVar offset and LHS is a TempVar, try looking up by name
		if (lhs_ref_it == reference_stack_info_.end() && std::holds_alternative<TempVar>(op.lhs.value)) {
			TempVar lhs_var = std::get<TempVar>(op.lhs.value);
			std::string_view var_name = lhs_var.name();
			FLASH_LOG(Codegen, Debug, "LHS is TempVar with name: '", var_name, "'");
			// Remove the '%' prefix if present
			if (!var_name.empty() && var_name[0] == '%') {
				var_name = var_name.substr(1);
				FLASH_LOG(Codegen, Debug, "After removing %, name: '", var_name, "'");
			}
			auto named_var_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(var_name));
			if (named_var_it != variable_scopes.back().variables.end()) {
				int32_t named_offset = named_var_it->second.offset;
				FLASH_LOG(Codegen, Debug, "Found in named vars at offset: ", named_offset);
				lhs_ref_it = reference_stack_info_.find(named_offset);
				if (lhs_ref_it != reference_stack_info_.end()) {
					// Found it! Update lhs_offset to use the named variable offset
					lhs_offset = named_offset;
					FLASH_LOG(Codegen, Debug, "Found reference info at named offset!");
				}
			} else {
				FLASH_LOG(Codegen, Debug, "Not found in named vars");
			}
		}
		
		FLASH_LOG(Codegen, Debug, "Assignment: lhs_offset=", lhs_offset, ", is_reference=", (lhs_ref_it != reference_stack_info_.end()), ", lhs.is_reference=", op.lhs.is_reference());
		
		// Check if LHS is a reference - either from reference_stack_info_ or from the TypedValue metadata
		bool lhs_is_reference = (lhs_ref_it != reference_stack_info_.end()) || op.lhs.is_reference();
		
		if (lhs_is_reference) {
			// LHS is a reference variable
			// In C++, references cannot be rebound after initialization
			// Any assignment to a reference should modify the object it refers to (dereference semantics)
			// Example: int x = 10; int& ref = x; ref = 20; // This modifies x, not ref
			
			// Step 1: Load the address stored in the reference variable (LHS)
			X64Register ref_addr_reg = allocateRegisterWithSpilling();
			emitMovFromFrame(ref_addr_reg, lhs_offset);
			FLASH_LOG(Codegen, Debug, "Reference assignment: Loaded address from reference variable at offset ", lhs_offset);
			
			// Step 2: Load or compute the value to store (RHS)
			X64Register value_reg = allocateRegisterWithSpilling();
			
			// Get reference value type and size
			Type value_type;
			int value_size_bits;
			if (lhs_ref_it != reference_stack_info_.end()) {
				value_type = lhs_ref_it->second.value_type;
				value_size_bits = lhs_ref_it->second.value_size_bits;
			} else {
				// Use TypedValue metadata
				value_type = op.lhs.type;
				value_size_bits = op.lhs.size_in_bits;
			}
			int value_size_bytes = value_size_bits / 8;
			
			if (std::holds_alternative<unsigned long long>(op.rhs.value)) {
				// RHS is an immediate value
				uint64_t imm_value = std::get<unsigned long long>(op.rhs.value);
				FLASH_LOG(Codegen, Debug, "Reference assignment: RHS is immediate value: ", imm_value);
				moveImmediateToRegister(value_reg, imm_value);
			} else if (std::holds_alternative<StringHandle>(op.rhs.value)) {
				// RHS is a variable name
				StringHandle rhs_var_name_handle = std::get<StringHandle>(op.rhs.value);
				std::string_view rhs_var_name = StringTable::getStringView(rhs_var_name_handle);
				FLASH_LOG(Codegen, Debug, "Reference assignment: RHS is variable: '", rhs_var_name, "'");
				auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(rhs_var_name));
				if (it != variable_scopes.back().variables.end()) {
					int32_t rhs_offset = it->second.offset;
					// Check if RHS is also a reference
					auto rhs_ref_it = reference_stack_info_.find(rhs_offset);
					if (rhs_ref_it != reference_stack_info_.end()) {
						// RHS is a reference - dereference it to get the value
						X64Register rhs_addr_reg = allocateRegisterWithSpilling();
						emitMovFromFrame(rhs_addr_reg, rhs_offset);  // Load pointer from reference
						emitMovFromMemory(value_reg, rhs_addr_reg, 0, value_size_bytes);  // Dereference
						regAlloc.release(rhs_addr_reg);
					} else {
						// RHS is a regular variable - load its value
						emitMovFromFrameSized(
							SizedRegister{value_reg, value_size_bits, isSignedType(value_type)},
							SizedStackSlot{rhs_offset, value_size_bits, isSignedType(value_type)}
						);
					}
				} else {
					FLASH_LOG(Codegen, Error, "RHS variable '", rhs_var_name, "' not found for reference assignment");
					regAlloc.release(ref_addr_reg);
					regAlloc.release(value_reg);
					return;
				}
			} else if (std::holds_alternative<TempVar>(op.rhs.value)) {
				// RHS is a TempVar
				TempVar rhs_var = std::get<TempVar>(op.rhs.value);
				FLASH_LOG(Codegen, Debug, "Reference assignment: RHS is TempVar: '", rhs_var.name(), "'");
				int32_t rhs_offset = getStackOffsetFromTempVar(rhs_var);
				// Check if RHS is a reference
				auto rhs_ref_it = reference_stack_info_.find(rhs_offset);
				if (rhs_ref_it != reference_stack_info_.end()) {
					// RHS is a reference - dereference it
					X64Register rhs_addr_reg = allocateRegisterWithSpilling();
					emitMovFromFrame(rhs_addr_reg, rhs_offset);
					emitMovFromMemory(value_reg, rhs_addr_reg, 0, value_size_bytes);
					regAlloc.release(rhs_addr_reg);
				} else {
					// Load value from TempVar
					emitMovFromFrameSized(
						SizedRegister{value_reg, value_size_bits, isSignedType(value_type)},
						SizedStackSlot{rhs_offset, value_size_bits, isSignedType(value_type)}
					);
				}
			} else {
				FLASH_LOG(Codegen, Error, "Unsupported RHS type for reference assignment");
				regAlloc.release(ref_addr_reg);
				regAlloc.release(value_reg);
				return;
			}
			
			// Step 3: Store the value to the address pointed to by the reference (dereference and store)
			emitStoreToMemory(textSectionData, value_reg, ref_addr_reg, 0, value_size_bytes);
			FLASH_LOG(Codegen, Debug, "Reference assignment: Stored value to dereferenced address");
			
			regAlloc.release(ref_addr_reg);
			regAlloc.release(value_reg);
			
			return;  // Done with reference assignment
		}

		// For non-reference LHS, proceed with normal assignment
		// Get RHS source
		Type rhs_type = op.rhs.type;
		X64Register source_reg = X64Register::RAX;

		// Load RHS value into a register
		if (std::holds_alternative<StringHandle>(op.rhs.value)) {
			StringHandle rhs_var_name_handle = std::get<StringHandle>(op.rhs.value);
			std::string_view rhs_var_name = StringTable::getStringView(rhs_var_name_handle);
			auto it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(rhs_var_name));
			if (it != variable_scopes.back().variables.end()) {
				int32_t rhs_offset = it->second.offset;
				
				// Check if RHS is a reference - if so, dereference it (unless explicitly disabled)
				// Skip dereferencing if holds_address_only is true (AddressOf results)
				auto rhs_ref_it = reference_stack_info_.find(rhs_offset);
				if (rhs_ref_it != reference_stack_info_.end() && op.dereference_rhs_references && !rhs_ref_it->second.holds_address_only) {
					// RHS is a reference - load pointer and dereference
					X64Register ptr_reg = allocateRegisterWithSpilling();
					emitMovFromFrame(ptr_reg, rhs_offset);  // Load the pointer
					// Dereference to get the value
					int value_size_bytes = rhs_ref_it->second.value_size_bits / 8;
					emitMovFromMemory(ptr_reg, ptr_reg, 0, value_size_bytes);
					source_reg = ptr_reg;
				} else if (is_floating_point_type(rhs_type)) {
					source_reg = allocateXMMRegisterWithSpilling();
					bool is_float = (rhs_type == Type::Float);
					emitFloatMovFromFrame(source_reg, rhs_offset, is_float);
				} else {
					// Load from RHS stack location: source (sized stack slot) -> dest (64-bit register)
					emitMovFromFrameSized(
						SizedRegister{source_reg, 64, false},  // dest: 64-bit register
						SizedStackSlot{rhs_offset, op.rhs.size_in_bits, isSignedType(rhs_type)}  // source: sized stack slot
					);
				}
			}
		} else if (std::holds_alternative<TempVar>(op.rhs.value)) {
			TempVar rhs_var = std::get<TempVar>(op.rhs.value);
			int32_t rhs_offset = getStackOffsetFromTempVar(rhs_var);

			// Check if RHS is a reference - if so, dereference it
			auto rhs_ref_it = reference_stack_info_.find(rhs_offset);
			
			// If not found with TempVar offset, try looking up by name
			// This handles the case where TempVar offset differs from named variable offset
			if (rhs_ref_it == reference_stack_info_.end()) {
				std::string_view var_name = rhs_var.name();
				// Remove the '%' prefix if present
				if (!var_name.empty() && var_name[0] == '%') {
					var_name = var_name.substr(1);
				}
				// Only try to match if this looks like it could be a named variable
				// (not a pure temporary like "temp_10")
				if (!var_name.empty() && var_name.find("temp_") != 0) {
					auto named_var_it = variable_scopes.back().variables.find(StringTable::getOrInternStringHandle(var_name));
					if (named_var_it != variable_scopes.back().variables.end()) {
						int32_t named_offset = named_var_it->second.offset;
						rhs_ref_it = reference_stack_info_.find(named_offset);
						if (rhs_ref_it != reference_stack_info_.end()) {
							// Found it! Update rhs_offset to use the named variable offset
							rhs_offset = named_offset;
						}
					}
				}
			}
			
			if (rhs_ref_it != reference_stack_info_.end() && op.dereference_rhs_references && !rhs_ref_it->second.holds_address_only) {
				// RHS is a reference - load pointer and dereference
				X64Register ptr_reg = allocateRegisterWithSpilling();
				emitMovFromFrame(ptr_reg, rhs_offset);  // Load the pointer
				// Dereference to get the value
				int value_size_bytes = rhs_ref_it->second.value_size_bits / 8;
				emitMovFromMemory(ptr_reg, ptr_reg, 0, value_size_bytes);
				source_reg = ptr_reg;
			} else if (auto rhs_reg = regAlloc.tryGetStackVariableRegister(rhs_offset); rhs_reg.has_value()) {
				// Check if the value is already in a register
				source_reg = rhs_reg.value();
			} else {
				if (is_floating_point_type(rhs_type)) {
					source_reg = allocateXMMRegisterWithSpilling();
					bool is_float = (rhs_type == Type::Float);
					emitFloatMovFromFrame(source_reg, rhs_offset, is_float);
				} else {
					// Load from RHS stack location: source (sized stack slot) -> dest (64-bit register)
					emitMovFromFrameSized(
						SizedRegister{source_reg, 64, false},  // dest: 64-bit register
						SizedStackSlot{rhs_offset, op.rhs.size_in_bits, isSignedType(rhs_type)}  // source: sized stack slot
					);
				}
			}
		} else if (std::holds_alternative<unsigned long long>(op.rhs.value)) {
			// RHS is an immediate value
			unsigned long long rhs_value = std::get<unsigned long long>(op.rhs.value);
			// MOV RAX, imm64
			emitMovImm64(X64Register::RAX, rhs_value);
		} else if (std::holds_alternative<double>(op.rhs.value)) {
			// RHS is a floating-point immediate value
			double double_value = std::get<double>(op.rhs.value);
			// Allocate an XMM register and load the double into it
			source_reg = allocateXMMRegisterWithSpilling();
			// Convert double to uint64_t bit representation
			uint64_t bits;
			std::memcpy(&bits, &double_value, sizeof(bits));
			// Load bits into a general-purpose register first
			emitMovImm64(X64Register::RAX, bits);
			// Move from RAX to XMM register using movq instruction
			emitMovqGprToXmm(X64Register::RAX, source_reg);
		}
		
		// Store source register to LHS stack location
		// Check if LHS is a reference parameter that needs dereferencing
		auto ref_it = reference_stack_info_.find(lhs_offset);
		if (ref_it != reference_stack_info_.end()) {
			// LHS is a reference - need to dereference it before storing
			// First, load the pointer (reference address) into a temporary register
			X64Register ptr_reg = allocateRegisterWithSpilling();
			auto load_ptr = generatePtrMovFromFrame(ptr_reg, lhs_offset);
			textSectionData.insert(textSectionData.end(), load_ptr.op_codes.begin(), load_ptr.op_codes.begin() + load_ptr.size_in_bytes);
			
			// Now store the value to the address pointed to by ptr_reg
			int value_size_bits = ref_it->second.value_size_bits;
			int size_bytes = value_size_bits / 8;
			
			if (is_floating_point_type(rhs_type)) {
				// For floating-point, use SSE store instruction helper
				bool is_float = (rhs_type == Type::Float);
				auto store_inst = generateFloatMovToMemory(source_reg, ptr_reg, is_float);
				textSectionData.insert(textSectionData.end(), store_inst.op_codes.begin(), 
				                       store_inst.op_codes.begin() + store_inst.size_in_bytes);
			} else {
				// For integer types, use the existing emitStoreToMemory helper
				emitStoreToMemory(textSectionData, source_reg, ptr_reg, 0, size_bytes);
			}
			
			// Release the pointer register
			regAlloc.release(ptr_reg);
		} else {
			// Normal (non-reference) assignment - store directly to stack location
			if (is_floating_point_type(rhs_type)) {
				bool is_float = (rhs_type == Type::Float);
				emitFloatMovToFrame(source_reg, lhs_offset, is_float);
			} else {
				emitMovToFrameSized(
					SizedRegister{source_reg, 64, false},  // source: 64-bit register
					SizedStackSlot{lhs_offset, op.lhs.size_in_bits, isSignedType(lhs_type)}  // dest: sized stack slot
				);
				// Clear any stale register associations for this stack offset
				regAlloc.clearStackVariableAssociations(lhs_offset);
			}
		}
	}

