	void convert(const Ir& ir, const std::string_view filename, const std::string_view source_filename = "", bool show_timing = false) {
		
		// High-level timing (always enabled when show_timing=true)
		auto convert_start = std::chrono::high_resolution_clock::now();

		// Pre-allocate text section buffer based on IR instruction count.
		// Empirical worst case: ~33 bytes of machine code per IR instruction
		// (variadic functions with complex calling conventions).
		// Use 36 bytes/instr to guarantee no reallocations during codegen.
		const size_t ir_count = ir.getInstructions().size();
		constexpr size_t BYTES_PER_IR_INSTRUCTION = 36;
		textSectionData.reserve(ir_count * BYTES_PER_IR_INSTRUCTION);

		// Group instructions by function for stack space calculation
		{
			ProfilingTimer timer("Group instructions by function", show_timing);
			groupInstructionsByFunction(ir);
		}

		// Detailed profiling accumulators (only active when ENABLE_DETAILED_PROFILING is set)
		#if ENABLE_DETAILED_PROFILING
		ProfilingAccumulator funcDecl_accum("FunctionDecl instructions");
		ProfilingAccumulator varDecl_accum("VariableDecl instructions");
		ProfilingAccumulator return_accum("Return instructions");
		ProfilingAccumulator funcCall_accum("FunctionCall instructions");
		ProfilingAccumulator arithmetic_accum("Arithmetic instructions");
		ProfilingAccumulator comparison_accum("Comparison instructions");
		ProfilingAccumulator control_flow_accum("Control flow instructions");
		ProfilingAccumulator memory_accum("Memory access instructions");
		#endif

		auto ir_processing_start = std::chrono::high_resolution_clock::now();
		const auto& instructions = ir.getInstructions();
		bool skipping_function = false;  // When true, skip until next FunctionDecl
		for (size_t ir_idx = 0; ir_idx < instructions.size(); ++ir_idx) {
			const auto& instruction = instructions[ir_idx];
			
			// If we're skipping a failed function, only stop at the next FunctionDecl
			if (skipping_function) {
				if (instruction.getOpcode() == IrOpcode::FunctionDecl) {
					skipping_function = false;
					// fall through to process this FunctionDecl
				} else {
					continue;
				}
			}

			#if ENABLE_DETAILED_PROFILING
			auto instr_start = std::chrono::high_resolution_clock::now();
			#endif
			
			// Add line mapping for debug information if line number is available
			if (instruction.getOpcode() != IrOpcode::FunctionDecl && instruction.getOpcode() != IrOpcode::Return && instruction.getLineNumber() > 0) {
				addLineMapping(instruction.getLineNumber());
			}

			try {
			switch (instruction.getOpcode()) {
			case IrOpcode::FunctionDecl:
				FLASH_LOG(Codegen, Debug, "Processing IrOpcode::FunctionDecl");
				handleFunctionDecl(instruction);
				break;
			case IrOpcode::VariableDecl:
				FLASH_LOG(Codegen, Debug, "Processing IrOpcode::VariableDecl");
				handleVariableDecl(instruction);
				break;
			case IrOpcode::Return:
				FLASH_LOG(Codegen, Debug, "Processing IrOpcode::Return");
				handleReturn(instruction);
				break;
			case IrOpcode::FunctionCall:
				FLASH_LOG(Codegen, Debug, "Processing IrOpcode::FunctionCall");
				handleFunctionCall(instruction);
				break;
			case IrOpcode::StackAlloc:
				handleStackAlloc(instruction);
				break;
			case IrOpcode::Add:
				handleAdd(instruction);
				break;
			case IrOpcode::Subtract:
				handleSubtract(instruction);
				break;
			case IrOpcode::Multiply:
				handleMultiply(instruction);
				break;
			case IrOpcode::Divide:
				handleDivide(instruction);
				break;
			case IrOpcode::UnsignedDivide:
				handleUnsignedDivide(instruction);
				break;
			case IrOpcode::ShiftLeft:
				handleShiftLeft(instruction);
				break;
			case IrOpcode::ShiftRight:
				handleShiftRight(instruction);
				break;
			case IrOpcode::UnsignedShiftRight:
				handleUnsignedShiftRight(instruction);
				break;
			case IrOpcode::BitwiseAnd:
				handleBitwiseAnd(instruction);
				break;
			case IrOpcode::BitwiseOr:
				handleBitwiseOr(instruction);
				break;
			case IrOpcode::BitwiseXor:
				handleBitwiseXor(instruction);
				break;
			case IrOpcode::Modulo:
				handleModulo(instruction);
				break;
			case IrOpcode::FloatAdd:
				handleFloatAdd(instruction);
				break;
			case IrOpcode::FloatSubtract:
				handleFloatSubtract(instruction);
				break;
			case IrOpcode::FloatMultiply:
				handleFloatMultiply(instruction);
				break;
			case IrOpcode::FloatDivide:
				handleFloatDivide(instruction);
				break;
			case IrOpcode::Equal:
				handleEqual(instruction);
				break;
			case IrOpcode::NotEqual:
				handleNotEqual(instruction);
				break;
			case IrOpcode::LessThan:
				handleLessThan(instruction);
				break;
			case IrOpcode::LessEqual:
				handleLessEqual(instruction);
				break;
			case IrOpcode::GreaterThan:
				handleGreaterThan(instruction);
				break;
			case IrOpcode::GreaterEqual:
				handleGreaterEqual(instruction);
				break;
			case IrOpcode::UnsignedLessThan:
				handleUnsignedLessThan(instruction);
				break;
			case IrOpcode::UnsignedLessEqual:
				handleUnsignedLessEqual(instruction);
				break;
			case IrOpcode::UnsignedGreaterThan:
				handleUnsignedGreaterThan(instruction);
				break;
			case IrOpcode::UnsignedGreaterEqual:
				handleUnsignedGreaterEqual(instruction);
				break;
			case IrOpcode::FloatEqual:
				handleFloatEqual(instruction);
				break;
			case IrOpcode::FloatNotEqual:
				handleFloatNotEqual(instruction);
				break;
			case IrOpcode::FloatLessThan:
				handleFloatLessThan(instruction);
				break;
			case IrOpcode::FloatLessEqual:
				handleFloatLessEqual(instruction);
				break;
			case IrOpcode::FloatGreaterThan:
				handleFloatGreaterThan(instruction);
				break;
			case IrOpcode::FloatGreaterEqual:
				handleFloatGreaterEqual(instruction);
				break;
			case IrOpcode::LogicalAnd:
				handleLogicalAnd(instruction);
				break;
			case IrOpcode::LogicalOr:
				handleLogicalOr(instruction);
				break;
			case IrOpcode::LogicalNot:
				handleLogicalNot(instruction);
				break;
			case IrOpcode::BitwiseNot:
				handleBitwiseNot(instruction);
				break;
			case IrOpcode::Negate:
				handleNegate(instruction);
				break;
			case IrOpcode::SignExtend:
				handleSignExtend(instruction);
				break;
			case IrOpcode::ZeroExtend:
				handleZeroExtend(instruction);
				break;
			case IrOpcode::Truncate:
				handleTruncate(instruction);
				break;
			case IrOpcode::FloatToInt:
				handleFloatToInt(instruction);
				break;
			case IrOpcode::IntToFloat:
				handleIntToFloat(instruction);
				break;
			case IrOpcode::FloatToFloat:
				handleFloatToFloat(instruction);
				break;
			case IrOpcode::AddAssign:
				handleAddAssign(instruction);
				break;
			case IrOpcode::SubAssign:
				handleSubAssign(instruction);
				break;
			case IrOpcode::MulAssign:
				handleMulAssign(instruction);
				break;
			case IrOpcode::DivAssign:
				handleDivAssign(instruction);
				break;
			case IrOpcode::ModAssign:
				handleModAssign(instruction);
				break;
			case IrOpcode::AndAssign:
				handleAndAssign(instruction);
				break;
			case IrOpcode::OrAssign:
				handleOrAssign(instruction);
				break;
			case IrOpcode::XorAssign:
				handleXorAssign(instruction);
				break;
			case IrOpcode::ShlAssign:
				handleShlAssign(instruction);
				break;
			case IrOpcode::ShrAssign:
				FLASH_LOG(Codegen, Debug, "Processing IrOpcode::ShrAssign");
				handleShrAssign(instruction);
				break;
			case IrOpcode::Assignment:
				FLASH_LOG(Codegen, Debug, "Processing IrOpcode::Assignment");
				handleAssignment(instruction);
				break;
			case IrOpcode::Label:
				handleLabel(instruction);
				break;
			case IrOpcode::Branch:
				handleBranch(instruction);
				break;
			case IrOpcode::ConditionalBranch:
				handleConditionalBranch(instruction);
				break;
			case IrOpcode::LoopBegin:
				handleLoopBegin(instruction);
				break;
			case IrOpcode::LoopEnd:
				handleLoopEnd(instruction);
				break;
			case IrOpcode::ScopeBegin:
				// No code generation needed - just a marker
				break;
			case IrOpcode::ScopeEnd:
				// No code generation needed - destructors are already emitted before this
				break;
			case IrOpcode::Break:
				handleBreak(instruction);
				break;
			case IrOpcode::Continue:
				handleContinue(instruction);
				break;
			case IrOpcode::ArrayAccess:
				handleArrayAccess(instruction);
				break;
			case IrOpcode::ArrayStore:
				handleArrayStore(instruction);
				break;
			case IrOpcode::ArrayElementAddress:
				handleArrayElementAddress(instruction);
				break;
			case IrOpcode::StringLiteral:
				handleStringLiteral(instruction);
				break;
			case IrOpcode::PreIncrement:
				handlePreIncrement(instruction);
				break;
			case IrOpcode::PostIncrement:
				handlePostIncrement(instruction);
				break;
			case IrOpcode::PreDecrement:
				handlePreDecrement(instruction);
				break;
			case IrOpcode::PostDecrement:
				handlePostDecrement(instruction);
				break;
			case IrOpcode::AddressOf:
				handleAddressOf(instruction);
				break;
			case IrOpcode::AddressOfMember:
				handleAddressOfMember(instruction);
				break;
			case IrOpcode::ComputeAddress:
				handleComputeAddress(instruction);
				break;
			case IrOpcode::Dereference:
				handleDereference(instruction);
				break;
			case IrOpcode::DereferenceStore:
				handleDereferenceStore(instruction);
				break;
			case IrOpcode::MemberAccess:
				handleMemberAccess(instruction);
				break;
			case IrOpcode::MemberStore:
				handleMemberStore(instruction);
				break;
			case IrOpcode::ConstructorCall:
				handleConstructorCall(instruction);
				break;
			case IrOpcode::DestructorCall:
				handleDestructorCall(instruction);
				break;
			case IrOpcode::VirtualCall:
				handleVirtualCall(instruction);
				break;
			case IrOpcode::HeapAlloc:
				handleHeapAlloc(instruction);
				break;
			case IrOpcode::HeapAllocArray:
				handleHeapAllocArray(instruction);
				break;
			case IrOpcode::HeapFree:
				handleHeapFree(instruction);
				break;
			case IrOpcode::HeapFreeArray:
				handleHeapFreeArray(instruction);
				break;
			case IrOpcode::PlacementNew:
				handlePlacementNew(instruction);
				break;
			case IrOpcode::Typeid:
				handleTypeid(instruction);
				break;
			case IrOpcode::DynamicCast:
				handleDynamicCast(instruction);
				break;
			case IrOpcode::GlobalVariableDecl:
				handleGlobalVariableDecl(instruction);
				break;
			case IrOpcode::GlobalLoad:
				handleGlobalLoad(instruction);
				break;
			case IrOpcode::GlobalStore:
				handleGlobalStore(instruction);
				break;
			case IrOpcode::FunctionAddress:
				handleFunctionAddress(instruction);
				break;
			case IrOpcode::IndirectCall:
				handleIndirectCall(instruction);
				break;
			case IrOpcode::TryBegin:
				handleTryBegin(instruction);
				break;
			case IrOpcode::TryEnd:
				handleTryEnd(instruction);
				break;
			case IrOpcode::CatchBegin:
				handleCatchBegin(instruction);
				break;
			case IrOpcode::CatchEnd:
				handleCatchEnd(instruction);
				break;
			case IrOpcode::Throw:
				handleThrow(instruction);
				break;
			case IrOpcode::Rethrow:
				handleRethrow(instruction);
				break;
			// Windows SEH (Structured Exception Handling)
			case IrOpcode::SehTryBegin:
				handleSehTryBegin(instruction);
				break;
			case IrOpcode::SehTryEnd:
				handleSehTryEnd(instruction);
				break;
			case IrOpcode::SehExceptBegin:
				handleSehExceptBegin(instruction);
				break;
			case IrOpcode::SehExceptEnd:
				handleSehExceptEnd(instruction);
				break;
			case IrOpcode::SehFinallyBegin:
				handleSehFinallyBegin(instruction);
				break;
			case IrOpcode::SehFinallyEnd:
				handleSehFinallyEnd(instruction);
				break;
			case IrOpcode::SehFinallyCall:
				handleSehFinallyCall(instruction);
				break;
			case IrOpcode::SehFilterBegin:
				handleSehFilterBegin(instruction);
				break;
			case IrOpcode::SehFilterEnd:
				handleSehFilterEnd(instruction);
				break;
			case IrOpcode::SehLeave:
				handleSehLeave(instruction);
				break;
			case IrOpcode::SehGetExceptionCode:
				handleSehGetExceptionCode(instruction);
				break;
			case IrOpcode::SehGetExceptionInfo:
				handleSehGetExceptionInfo(instruction);
				break;
			case IrOpcode::SehSaveExceptionCode:
				handleSehSaveExceptionCode(instruction);
				break;
			case IrOpcode::SehGetExceptionCodeBody:
				handleSehGetExceptionCodeBody(instruction);
				break;
			case IrOpcode::SehAbnormalTermination:
				handleSehAbnormalTermination(instruction);
				break;
			default:
				throw InternalError("Not implemented yet");
				break;
			}
			} catch (const CompileError&) {
				// Semantic errors must propagate — they are real compilation failures
				throw;
			} catch (const InternalError& e) {
				// Per-function error recovery: skip to the next function declaration
				FLASH_LOG(Codegen, Error, "Code generation error in function, skipping: ", e.what());
				skipping_function = true;
				skip_previous_function_finalization_ = true;
				continue;
			} catch (const std::exception& e) {
				// Per-function error recovery: skip to the next function declaration
				FLASH_LOG(Codegen, Error, "Code generation error in function, skipping: ", e.what());
				skipping_function = true;
				skip_previous_function_finalization_ = true;
				continue;
			}

			#if ENABLE_DETAILED_PROFILING
			auto instr_end = std::chrono::high_resolution_clock::now();
			auto instr_duration = std::chrono::duration_cast<std::chrono::microseconds>(instr_end - instr_start);

			// Categorize and accumulate timing
			switch (instruction.getOpcode()) {
				case IrOpcode::FunctionDecl:
					funcDecl_accum.add(instr_duration);
					break;
				case IrOpcode::VariableDecl:
				case IrOpcode::StackAlloc:
					varDecl_accum.add(instr_duration);
					break;
				case IrOpcode::Return:
					return_accum.add(instr_duration);
					break;
				case IrOpcode::FunctionCall:
					funcCall_accum.add(instr_duration);
					break;
				case IrOpcode::Add:
				case IrOpcode::Subtract:
				case IrOpcode::Multiply:
				case IrOpcode::Divide:
				case IrOpcode::UnsignedDivide:
				case IrOpcode::Modulo:
				case IrOpcode::FloatAdd:
				case IrOpcode::FloatSubtract:
				case IrOpcode::FloatMultiply:
				case IrOpcode::FloatDivide:
				case IrOpcode::ShiftLeft:
				case IrOpcode::ShiftRight:
				case IrOpcode::UnsignedShiftRight:
				case IrOpcode::BitwiseAnd:
				case IrOpcode::BitwiseOr:
				case IrOpcode::BitwiseXor:
				case IrOpcode::BitwiseNot:
				case IrOpcode::LogicalNot:
				case IrOpcode::Negate:
				case IrOpcode::PreIncrement:
				case IrOpcode::PostIncrement:
				case IrOpcode::PreDecrement:
				case IrOpcode::PostDecrement:
					arithmetic_accum.add(instr_duration);
					break;
				case IrOpcode::Equal:
				case IrOpcode::NotEqual:
				case IrOpcode::LessThan:
				case IrOpcode::LessEqual:
				case IrOpcode::GreaterThan:
				case IrOpcode::GreaterEqual:
				case IrOpcode::UnsignedLessThan:
				case IrOpcode::UnsignedLessEqual:
				case IrOpcode::UnsignedGreaterThan:
				case IrOpcode::UnsignedGreaterEqual:
				case IrOpcode::FloatEqual:
				case IrOpcode::FloatNotEqual:
				case IrOpcode::FloatLessThan:
				case IrOpcode::FloatLessEqual:
				case IrOpcode::FloatGreaterThan:
				case IrOpcode::FloatGreaterEqual:
					comparison_accum.add(instr_duration);
					break;
				case IrOpcode::Label:
				case IrOpcode::Jump:
				case IrOpcode::JumpIfZero:
				case IrOpcode::JumpIfNotZero:
					control_flow_accum.add(instr_duration);
					break;
				case IrOpcode::AddressOf:
				case IrOpcode::Dereference:
				case IrOpcode::MemberAccess:
				case IrOpcode::MemberStore:
				case IrOpcode::ArrayAccess:
					memory_accum.add(instr_duration);
					break;
				case IrOpcode::ConstructorCall:
				case IrOpcode::DestructorCall:
					funcCall_accum.add(instr_duration);
					break;
				default:
					break;
			}
			#endif // ENABLE_DETAILED_PROFILING
		}

		auto ir_processing_end = std::chrono::high_resolution_clock::now();
	
		if (show_timing) {
			auto ir_duration = std::chrono::duration_cast<std::chrono::microseconds>(ir_processing_end - ir_processing_start);
			printf("    IR instruction processing: %8.3f ms\n", ir_duration.count() / 1000.0);
			printf("    Text section: %zu bytes generated, %zu reserved (%.1f%% utilization, %zu IR instructions, %.1f bytes/instr)\n",
				textSectionData.size(), textSectionData.capacity(),
				textSectionData.capacity() > 0 ? (100.0 * textSectionData.size() / textSectionData.capacity()) : 0.0,
				ir_count,
				ir_count > 0 ? (double)textSectionData.size() / ir_count : 0.0);
			if (textSectionData.size() > ir_count * BYTES_PER_IR_INSTRUCTION) {
				printf("    WARNING: textSectionData exceeded reserve! Consider increasing BYTES_PER_IR_INSTRUCTION (currently %zu)\n",
					BYTES_PER_IR_INSTRUCTION);
			}
		}

		#if ENABLE_DETAILED_PROFILING
		printf("\n  Detailed instruction timing:\n");
		funcDecl_accum.print();
		varDecl_accum.print();
		return_accum.print();
		funcCall_accum.print();
		arithmetic_accum.print();
		comparison_accum.print();
		control_flow_accum.print();
		memory_accum.print();
		printf("\n");
		#endif

		// Use the provided source filename, or fall back to a default if not provided
		std::string actual_source_file = source_filename.empty() ? "test_debug.cpp" : std::string(source_filename);
		{
			ProfilingTimer timer("Add source file", show_timing);
			writer.add_source_file(actual_source_file);
		}

		// Emit dynamic_cast runtime helpers if needed
		if (needs_dynamic_cast_runtime_) {
			ProfilingTimer timer("Emit dynamic_cast runtime helpers", show_timing);
			emit_dynamic_cast_runtime_helpers();
		}

		{
			ProfilingTimer timer("Finalize sections", show_timing);
			finalizeSections();
		}

		// Clean up the last function's variable scope AFTER finalizeSections has used it
		// for stack size patching
		if (!variable_scopes.empty()) {
			variable_scopes.pop_back();
		}

		{
			ProfilingTimer timer("Write object file", show_timing);
			writer.write(std::string(filename));
		}

		if (show_timing) {
			auto convert_end = std::chrono::high_resolution_clock::now();
			auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(convert_end - convert_start);
			printf("    Total code generation:     %8.3f ms\n", total_duration.count() / 1000.0);
		}
	}
