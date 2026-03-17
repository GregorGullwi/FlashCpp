#pragma once

// IRConverter_ConvertMain.h - IrToObjConverter template class definition
// Self-contained class definition; no continuation-style nested includes.

template<class TWriterClass = ObjectFileWriter>
class IrToObjConverter {
public:
	void convert(const Ir& ir, const std::string_view filename, const std::string_view source_filename = "", bool show_timing = false);

private:

	// Helper to convert internal try blocks to ObjectFileWriter format
	// Used during function finalization to prepare exception handling information
	std::pair<std::vector<ObjectFileWriter::TryBlockInfo>, std::vector<ObjectFileWriter::UnwindMapEntryInfo>>
	convertExceptionInfoToWriterFormat();

	// Helper to convert internal SEH try blocks to ObjectFileWriter format
	// Used during function finalization to prepare SEH exception handling information
	std::vector<ObjectFileWriter::SehTryBlockInfo> convertSehInfoToWriterFormat();

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

	RegToRegEncoding encodeRegToRegInstruction(X64Register reg_field, X64Register rm_field, bool include_rex_w = true);

	// Helper for instructions with opcode extension (reg field is a constant, rm is the register)
	// Used by shift instructions and division which encode the operation in the reg field
	void emitOpcodeExtInstruction(uint8_t opcode, X64OpcodeExtension opcode_ext, X64Register rm_field, int size_in_bits);

	// Helper function to emit a binary operation instruction (reg-to-reg)
	void emitBinaryOpInstruction(uint8_t opcode, X64Register src_reg, X64Register dst_reg, int size_in_bits);

	// Helper function to emit MOV reg, reg instruction with size awareness
	void emitMovRegToReg(X64Register src_reg, X64Register dst_reg, int src_size_in_bits);

	// Helper function to emit a comparison instruction (CMP + SETcc + MOVZX)
	void emitComparisonInstruction(const ArithmeticOperationContext& ctx, uint8_t setcc_opcode);

	// Helper function to emit a floating-point comparison instruction (comiss/comisd + SETcc)
	// Consolidates the repeated pattern across handleFloatEqual, handleFloatNotEqual, etc.
	void emitFloatComparisonInstruction(ArithmeticOperationContext& ctx, uint8_t setcc_opcode);

	// Helper function to load a global variable into a register
	// Handles both integer/pointer and floating-point types
	// Returns the allocated register, or X64Register::Count on error
	X64Register loadGlobalVariable(StringHandle var_handle, std::string_view var_name,
	                                Type operand_type, int operand_size_in_bits,
	                                std::optional<X64Register> exclude_reg = std::nullopt);

	ArithmeticOperationContext setupAndLoadArithmeticOperation(const IrInstruction& instruction, const char* operation_name);

	// Store the result of arithmetic operations to the appropriate destination
	void storeArithmeticResult(const ArithmeticOperationContext& ctx, X64Register source_reg = X64Register::Count);

	// Group IR instructions by function for analysis
	void groupInstructionsByFunction(const Ir& ir);

	// Calculate the total stack space needed for a function by analyzing its IR instructions
	struct StackSpaceSize {
		uint16_t temp_vars_size = 0;
		uint16_t named_vars_size = 0;
		uint16_t shadow_stack_space = 0;
		uint16_t outgoing_args_space = 0;  // Space for largest outgoing function call
	};
	struct VariableInfo {
		int offset = INT_MIN;  // Stack offset from RBP (INT_MIN = unallocated)
		SizeInBits size_in_bits;  // Size in bits
		bool is_array = false; // True if this is an array declaration (enables array-to-pointer decay in expressions and assignments)
	};

	struct StackVariableScope
	{
		int scope_stack_space = 0;
		std::unordered_map<StringHandle, VariableInfo> variables;  // Phase 5: StringHandle for integer-based lookups
	};

	struct IndirectStorageInfo {
		Type value_type = Type::Invalid;
		IrType ir_type = IrType::Integer;  // Phase 4: parallel ir_type field, will replace value_type
		SizeInBits value_size_bits;
		bool is_rvalue_reference = false;
		// When true (e.g., AddressOf results), this TempVar holds a raw address/pointer value,
		// not a reference that should be implicitly dereferenced.
		bool holds_address_only = false;
	};

	void setIndirectStorageInfo(
		int32_t stack_offset,
		Type value_type,
		int value_size_bits,
		bool is_rvalue_ref,
		bool holds_address_only,
		TempVar temp_var = TempVar{0});

	// Clear all indirect storage tracking AND any TempVar reference/address-only metadata
	// that was set by setIndirectStorageInfo. Must be called at function boundaries so that
	// stale TempVar metadata from previous functions (which reuse the same var_numbers) does not
	// cause isTempVarReference/isTempVarAddressOnly to return stale true values.
	void clearFunctionTempVarMetadata();

	// Helper function to set reference information in both storage systems
	// This ensures metadata stays synchronized between stack offset tracking and TempVar metadata
	void setReferenceInfo(int32_t stack_offset, Type value_type, int value_size_bits, bool is_rvalue_ref, TempVar temp_var);

	void setAddressOnlyInfo(int32_t stack_offset, Type value_type, int value_size_bits, TempVar temp_var);

	std::optional<IndirectStorageInfo> getIndirectStackInfo(int32_t stack_offset) const;

	bool hasIndirectStackStorage(int32_t stack_offset) const;

	bool shouldImplicitlyDeref(const IndirectStorageInfo& info) const;

	// Check if the base is already a pointer (holds an address).
	// Returns true if:
	//   - stack_offset is registered in indirect_stack_info_ (true reference or address-only like 'this')
	//   - OR TempVar (if provided) is a true reference or address-only
	// This is used for member access / compute-address decisions:
	// - if true, emit MOV (load existing address) rather than LEA (compute address of stack slot)
	bool isPointerBaseStorage(int32_t stack_offset, TempVar temp_var = TempVar{0}) const;

	// Build the mangled symbol name for the complete-object destructor of a class.
	// Used by handleThrow (destructor arg to __cxa_throw) and handleDestructorCall.
	// Returns empty string when the struct has no user-defined destructor.
	std::string buildDestructorMangledName(const StructTypeInfo& si);

	// Build the mangled name for a destructor from a class-name string view.
	// Used by cleanup landing pad helpers (Phase 1 and Phase 2 stack unwinding)
	// and by buildDestructorMangledName(StructTypeInfo).
	std::string buildDestructorMangledNameFromString(std::string_view class_sv);

	// Helper function to check if a TempVar or stack offset uses indirect storage.
	// Returns true for true references (via TempVar path) and for any indirect storage
	// (including address-only values like 'this') via the stack-offset map.
	// Note: The TempVar path checks only true references (isTempVarReference excludes address-only),
	// while the fallback stack-offset path covers both references and address-only values.
	bool hasIndirectStorage(TempVar temp_var, int32_t stack_offset) const;

	// Helper function to get indirect-storage info for a TempVar or stack offset.
	// Returns TempVar metadata first when the temp is a true reference or address-only,
	// otherwise falls back to the stack-offset side table for named variables.
	std::optional<IndirectStorageInfo> getReferenceInfo(TempVar temp_var, int32_t stack_offset) const;

	StackSpaceSize calculateFunctionStackSpace(std::string_view func_name, StackVariableScope& var_scope, size_t param_count);

	// Helper function to get or reserve a stack slot for a temporary variable.
	// This is now just a thin wrapper around getStackOffsetFromTempVar which
	// handles stack space tracking and offset registration.
	int allocateStackSlotForTempVar(int32_t index, int size_in_bits = 64);

	// Get stack offset for a TempVar using formula-based allocation.
	// TempVars are allocated within the pre-allocated temp_vars space.
	// The space starts after named_vars + shadow_space.
	//
	// This function also:
	// - Extends scope_stack_space if the offset exceeds current tracked allocation
	// - Registers the TempVar in variables for consistent subsequent lookups
	int32_t getStackOffsetFromTempVar(TempVar tempVar, int size_in_bits = 64);

	void flushAllDirtyRegisters();

	// Helper to generate and emit size-appropriate MOV to frame (legacy - prefer emitMovToFrameSized)

// IRConverter_Emit_CompareBranch.h - Class-internal emit helper methods (compare, branch, mov, float, etc.)
// Included inside IrToObjConverter class body - do not add #pragma once
	void emitMovToFrameBySize(X64Register sourceRegister, int32_t offset, int size_in_bits);

	// Helper to generate and emit size-aware MOV to frame
	// Takes SizedRegister for source (register + size) and SizedStackSlot for destination (offset + size)
	// This makes both source and destination sizes explicit for clarity
	void emitMovToFrameSized(SizedRegister source, SizedStackSlot dest);

	// Helper to generate and emit size-appropriate MOV from frame
	void emitMovFromFrameBySize(X64Register destinationRegister, int32_t offset, int size_in_bits);

	// Helper to generate and emit 64-bit MOV from frame (for pointers/references)
	void emitMovFromFrame(X64Register destinationRegister, int32_t offset);

	// Helper to generate and emit pointer MOV from frame
	void emitPtrMovFromFrame(X64Register destinationRegister, int32_t offset);

	// Helper to emit CMP dword [rbp+offset], imm32 for exception selector dispatch
	void emitCmpFrameImm32(int32_t frame_offset, int32_t imm_value);

	// Allocate an anonymous stack slot for ELF exception dispatch temporaries
	int32_t allocateElfTempStackSlot(int size_bytes);

	// Helper to generate and emit size-aware MOV from frame
	// Takes SizedRegister for destination (register + size) and SizedStackSlot for source (offset + size)
	// This makes both source and destination sizes explicit for clarity
	void emitMovFromFrameSized(SizedRegister dest, SizedStackSlot source);

	// Helper to generate and emit LEA from frame
	void emitLeaFromFrame(X64Register destinationRegister, int32_t offset);

	// Helper to emit RIP-relative LEA for loading symbol addresses
	// Returns the offset where the relocation displacement should be added
	uint32_t emitLeaRipRelative(X64Register destinationRegister);

	// Helper to generate and emit MOV to frame with explicit size
	void emitMovToFrame(X64Register sourceRegister, int32_t offset, int size_in_bits);

	// Helper to emit MOVQ from XMM to GPR (for varargs: float args must be in both XMM and INT regs)
	// movq r64, xmm: 66 REX.W 0F 7E /r
	void emitMovqXmmToGpr(X64Register xmm_src, X64Register gpr_dest);

	// Helper to emit MOVQ from GPR to XMM (for moving float bits to XMM register)
	// movq xmm, r64: 66 REX.W 0F 6E /r
	void emitMovqGprToXmm(X64Register gpr_src, X64Register xmm_dest);

	// Helper to emit CVTSS2SD (convert float to double in XMM register)
	// For varargs: floats are promoted to double before passing
	// cvtss2sd xmm, xmm: F3 0F 5A /r
	void emitCvtss2sd(X64Register xmm_dest, X64Register xmm_src);

	// Helper to generate and emit float MOV from frame (movss/movsd)
	void emitFloatMovFromFrame(X64Register destinationRegister, int32_t offset, bool is_float);

	void emitFloatMovToFrame(X64Register sourceRegister, int32_t offset, bool is_float);

	// Helper to emit MOVSS/MOVSD from memory [reg + offset] into XMM register
	void emitFloatMovFromMemory(X64Register xmm_dest, X64Register base_reg, int32_t offset, bool is_float);

	// Helper to emit MOVSS/MOVSD for XMM register-to-register moves
	void emitFloatMovRegToReg(X64Register xmm_dest, X64Register xmm_src, bool is_double);

	// Helper to emit MOVDQU (unaligned 128-bit move) from XMM register to frame
	// Used for saving full XMM registers in variadic function register save areas
	void emitMovdquToFrame(X64Register xmm_src, int32_t offset);

	// Helper to emit MOV DWORD PTR [reg + offset], imm32
	void emitMovDwordPtrImmToRegOffset(X64Register base_reg, int32_t offset, uint32_t imm32);

	// Helper to emit MOV QWORD PTR [reg + offset], imm32 (sign-extended to 64-bit)
	void emitMovQwordPtrImmToRegOffset(X64Register base_reg, int32_t offset, uint32_t imm32);

	// Helper to emit MOV QWORD PTR [reg + offset], src_reg
	void emitMovQwordPtrRegToRegOffset(X64Register base_reg, int32_t offset, X64Register src_reg);

	// Helper to generate and emit MOV reg, imm64
	void emitMovImm32(X64Register destinationRegister, uint32_t immediate_value);

	void emitMovImm64(X64Register destinationRegister, uint64_t immediate_value);

	// Helper to emit SUB RSP, imm8 for stack allocation
	void emitSubRSP(uint8_t amount);

	// Helper to emit ADD RSP, imm8 for stack deallocation
	void emitAddRSP(uint8_t amount);

	// Helper to emit AND reg, imm64 for bitfield masking
	void emitAndImm64(X64Register reg, uint64_t mask);

	// Helper to emit SHL reg, imm8 for bitfield shifting
	void emitShlImm(X64Register reg, uint8_t shift_amount);

	// Helper to emit OR dest, src for bitfield combining
	void emitOrReg(X64Register dest, X64Register src);

	// Helper to emit SHR reg, imm8 for bitfield extraction
	void emitShrImm(X64Register reg, uint8_t shift_amount);

	// Compute the mask for a bitfield of the given width in bits.
	static uint64_t bitfieldMask(size_t width);

	// Helper to emit CALL instruction with relocation
	void emitCall(const std::string& symbol_name);

	// Helper to emit MOV reg, reg
	void emitMovRegReg(X64Register dest, X64Register src);

	// Helper to emit MOV dest, [base + offset] with size
	void emitMovFromMemory(X64Register dest, X64Register base, int32_t offset, size_t size_bytes);

	// Helper to emit MOV reg, [reg + disp8]
	void emitMovRegFromMemRegDisp8(X64Register dest, X64Register src_addr, int8_t disp);

	// Helper to emit size-aware MOV/MOVZX for dereferencing: dest = [src_addr]
	// Handles 8-bit (MOVZX), 16-bit, 32-bit, and 64-bit loads
	// Correctly handles RBP/R13 and RSP/R12 special cases
	void emitMovRegFromMemRegSized(X64Register dest, X64Register src_addr, int size_in_bits);

	// Helper to emit TEST reg, reg
	void emitTestRegReg(X64Register reg);

	// Helper to emit TEST AL, AL
	void emitTestAL();

	// Helper to emit LEA reg, [RIP + disp32] with relocation
	void emitLeaRipRelativeWithRelocation(X64Register dest, std::string_view symbol_name);

	// Helper to emit MOV reg, [RIP + disp32] for integer loads
	// Returns the offset where the displacement placeholder starts (for relocation)
	uint32_t emitMovRipRelative(X64Register dest, int size_in_bits);

	// Helper to emit MOVSD/MOVSS XMM, [RIP + disp32] for floating-point loads
	// Returns the offset where the displacement placeholder starts (for relocation)
	uint32_t emitFloatMovRipRelative(X64Register xmm_dest, bool is_float);

	// Helper to emit MOV [RIP + disp32], reg for integer stores
	// Returns the offset where the displacement placeholder starts (for relocation)
	uint32_t emitMovRipRelativeStore(X64Register src, int size_in_bits);

	// Helper to emit MOVSD/MOVSS [RIP + disp32], XMM for floating-point stores
	// Returns the offset where the displacement placeholder starts (for relocation)
	uint32_t emitFloatMovRipRelativeStore(X64Register xmm_src, bool is_float);

	// Additional emit helpers for dynamic_cast runtime generation

	void emitCmpRegReg(X64Register r1, X64Register r2);

	void emitCmpRegWithMem(X64Register reg, X64Register mem_base);

	void emitJumpIfZero(int8_t offset);

	void emitJumpIfEqual(int8_t offset);

	void emitJumpIfNotZero(int8_t offset);

	void emitJumpUnconditional(int8_t offset);

	void emitXorRegReg(X64Register reg);

	// Helper to emit REP MOVSB for memory copying
	// Copies RCX bytes from [RSI] to [RDI]
	void emitRepMovsb();

	// Helper to emit MOV [RSP+disp8], reg
	void emitMovToRSPDisp8(X64Register sourceRegister, int8_t displacement);

	// Helper to emit LEA reg, [RSP+disp8]
	void emitLeaFromRSPDisp8(X64Register destinationRegister, int8_t displacement);

	void emitRet();

	void emitMovRegImm8(X64Register reg, uint8_t imm);

	void emitPushReg(X64Register reg);

	void emitPopReg(X64Register reg);

	void emitIncReg(X64Register reg);

	void emitCmpRegImm32(X64Register reg, uint32_t imm);

	void emitJumpIfAbove(int8_t offset);

	void emitJumpIfBelow(int8_t offset);

	void emitLeaRegScaledIndex(X64Register dest, X64Register base, X64Register index, uint8_t scale, int8_t disp);

	// Allocate a register, spilling one to the stack if necessary
	X64Register allocateRegisterWithSpilling();

		bool isRestrictedCatchFuncletRegister(X64Register reg) const;

	// Allocate a register, spilling one to the stack if necessary, excluding a specific register
	X64Register allocateRegisterWithSpilling(X64Register exclude);

	// Allocate a register, spilling one to the stack if necessary, excluding two specific registers
	X64Register allocateRegisterWithSpilling(X64Register exclude1, X64Register exclude2);

	// Allocate an XMM register, spilling one to the stack if necessary
	X64Register allocateXMMRegisterWithSpilling();

	// Allocate an XMM register, spilling one to the stack if necessary, excluding a specific register
	X64Register allocateXMMRegisterWithSpilling(X64Register exclude);

	/// SysV AMD64 ABI: true for 9-16 byte by-value structs that must be passed in two
	/// consecutive general-purpose registers (INTEGER+INTEGER classification).
	/// This applies to both variadic and non-variadic calls.
	/// NOTE: For variadic *callees*, the register-save-area prologue handles these
	/// implicitly, so callee-side code guards this with !is_variadic separately.
	static bool isTwoRegisterStructRaw(IrType ir_type, int size_in_bits, bool is_reference, int pointer_depth);

	/// Check if an argument is a two-register struct under System V AMD64 ABI (9-16 bytes, by value).
	bool isTwoRegisterStruct(const TypedValue& arg, [[maybe_unused]] bool is_variadic_call = false) const;

	/// Determine if a struct argument should be passed by address (pointer) based on ABI.
	bool shouldPassStructByAddress(const TypedValue& arg, bool is_two_register_struct = false) const;

	// ── Two-register struct helpers ──────────────────────────────────────
	// SysV AMD64 ABI: 9-16 byte by-value structs are passed/received in two
	// consecutive general-purpose registers.  These helpers centralise the
	// codegen so that handleFunctionCall and handleConstructorCall don't
	// duplicate the same emit sequences.

	/// Resolve a TypedValue (StringHandle or TempVar) to its frame offset.
	int32_t getVariableOffsetOrThrow(StringHandle var_handle, std::string_view context) const;

	int resolveTypedValueFrameOffset(const TypedValue& arg);

	bool emitLoadAddressLikeArgument(X64Register target_reg, const TypedValue& arg, int32_t address_adjustment = 0);

	/// Emit code to store both 8-byte halves of a two-register struct to
	/// consecutive RSP-relative stack slots (used when the struct overflows
	/// the register file).
	void emitTwoRegStructToStack(const TypedValue& arg, int stack_offset);

	/// Emit code to load both 8-byte halves of a two-register struct into
	/// two consecutive integer parameter registers.  `target_reg` already
	/// holds the first register; `int_reg_index` is advanced for the second.
	void emitTwoRegStructToRegs(int src_offset, X64Register target_reg,
	                            size_t& int_reg_index, size_t max_int_regs);

	void handleFunctionCall(const IrInstruction& instruction);

	bool emitSameTypeCopyOrMoveConstructorCall(TypeIndex type_index, int object_offset, bool object_is_pointer, const TypedValue& source_arg, bool prefer_move = false);

	void handleConstructorCall(const IrInstruction& instruction);

	void handleDestructorCall(const IrInstruction& instruction);

	void handleVirtualCall(const IrInstruction& instruction);

	void handleHeapAlloc(const IrInstruction& instruction);

	void handleHeapAllocArray(const IrInstruction& instruction);

	void handleHeapFree(const IrInstruction& instruction);

	void handleHeapFreeArray(const IrInstruction& instruction);

	void handlePlacementNew(const IrInstruction& instruction);

	void handleTypeid(const IrInstruction& instruction);

	void handleDynamicCast(const IrInstruction& instruction);

	void spillAndInvalidateRegisterForManualOverwrite(X64Register target_reg);

	void handleGlobalVariableDecl(const IrInstruction& instruction);

	void handleGlobalLoad(const IrInstruction& instruction);

	void handleGlobalStore(const IrInstruction& instruction);

	void handleVariableDecl(const IrInstruction& instruction);

	uint16_t getX64RegisterCodeViewCode(X64Register reg);

	// Reset per-function state between function declarations
	void resetFunctionState();

	// Helper: emit a noexcept terminate landing pad (ELF only) if the current function is
	// declared noexcept and no cleanup LP was already emitted.
	// The LP is reached by the personality routine when an exception would escape a noexcept
	// function.  It calls __cxa_call_terminate(exception_ptr) which is [[noreturn]].
	// Sets current_function_cleanup_lp_offset_ so the caller can register a CleanupBlockInfo.
	void injectNoexceptTerminateLPIfNeeded();

	void handleFunctionDecl(const IrInstruction& instruction);

	// Helper function to get the actual size of a variable for proper zero/sign-extension
	int getActualVariableSize(StringHandle var_name, int default_size) const;

	int32_t ensureCatchFuncletReturnSlot();

	int32_t ensureCatchFuncletReturnFlagSlot();

	StringHandle getOrCreateCatchContinuationFixupLabel(StringHandle continuation_handle);

	void handleReturn(const IrInstruction& instruction);

	void handleStackAlloc([[maybe_unused]] const IrInstruction& instruction);

	void handleBinaryArithmetic(const IrInstruction& instruction, uint8_t opcode, const char* description);

	void reserveDivisionFixedRegisters();

	X64Register allocateDivisionScratchRegister(X64Register avoid1, X64Register avoid2 = X64Register::Count, X64Register avoid3 = X64Register::Count);

	// If the divisor is already in RAX (which IDIV needs for the dividend), move it
	// to a scratch register first so the dividend-to-RAX move doesn't clobber it.
	X64Register preserveDivisorAcrossRaxMove(X64Register divisor_reg, X64Register result_reg, int operand_size_in_bits);

	void handleAdd(const IrInstruction& instruction);

	void handleSubtract(const IrInstruction& instruction);

	void handleMultiply(const IrInstruction& instruction);

	void handleDivide(const IrInstruction& instruction);

	void handleShiftLeft(const IrInstruction& instruction);

	void handleShiftRight(const IrInstruction& instruction);

	void handleUnsignedDivide(const IrInstruction& instruction);

	void handleUnsignedShiftRight(const IrInstruction& instruction);

	// If the LHS (result) register is RCX, move it elsewhere before we overwrite
	// RCX with the shift count.  Both RCX and the RHS register are excluded from
	// allocation so the spill candidate search cannot evict either operand.
	void relocateLhsOutOfRCX(ArithmeticOperationContext& ctx);

	void handleBitwiseArithmetic(const IrInstruction& instruction, uint8_t opcode, const char* description);

	void handleBitwiseAnd(const IrInstruction& instruction);

	void handleBitwiseOr(const IrInstruction& instruction);

	void handleBitwiseXor(const IrInstruction& instruction);

	void handleModulo(const IrInstruction& instruction);

	void handleUnsignedModulo(const IrInstruction& instruction);

	void handleEqual(const IrInstruction& instruction);

	void handleNotEqual(const IrInstruction& instruction);

	void handleLessThan(const IrInstruction& instruction);

	void handleLessEqual(const IrInstruction& instruction);

	void handleGreaterThan(const IrInstruction& instruction);

	void handleGreaterEqual(const IrInstruction& instruction);

	void handleUnsignedLessThan(const IrInstruction& instruction);

	void handleUnsignedLessEqual(const IrInstruction& instruction);

	void handleUnsignedGreaterThan(const IrInstruction& instruction);

	void handleUnsignedGreaterEqual(const IrInstruction& instruction);

	void handleLogicalAnd(const IrInstruction& instruction);

	void handleLogicalOr(const IrInstruction& instruction);

	void handleLogicalNot(const IrInstruction& instruction);

	void handleBitwiseNot(const IrInstruction& instruction);

	void handleNegate(const IrInstruction& instruction);

	void storeUnaryResult(const IrOperand& result_operand, X64Register result_physical_reg, int size_in_bits);

	void handleFloatAdd(const IrInstruction& instruction);

	void handleFloatSubtract(const IrInstruction& instruction);

	void handleFloatMultiply(const IrInstruction& instruction);

	void handleFloatDivide(const IrInstruction& instruction);

	void handleFloatEqual(const IrInstruction& instruction);

	void handleFloatNotEqual(const IrInstruction& instruction);

	void handleFloatLessThan(const IrInstruction& instruction);

	void handleFloatLessEqual(const IrInstruction& instruction);

	void handleFloatGreaterThan(const IrInstruction& instruction);

	void handleFloatGreaterEqual(const IrInstruction& instruction);

	// Helper: Load a value from indirect storage (reference or address-only) into a register.
	// If the storage should be implicitly dereferenced, loads the pointed-to value;
	// otherwise loads the pointer itself (address-only case).
	X64Register loadFromIndirectStorage(int32_t stack_offset, const IndirectStorageInfo& ref_info);

	// Helper: Load operand value (TempVar or variable name) into a register
	X64Register loadOperandIntoRegister(const IrInstruction& instruction, size_t operand_index);

	X64Register loadTypedValueIntoRegister(const TypedValue& typed_value);

	const VariableInfo* findVariableInfo(StringHandle name) const;

	std::optional<int32_t> findIdentifierStackOffset(StringHandle name) const;

	enum class IncDecKind { PreIncrement, PostIncrement, PreDecrement, PostDecrement };

	struct UnaryOperandLocation {
		enum class Kind { Stack, Global };
		Kind kind = Kind::Stack;
		int32_t stack_offset = 0;
		StringHandle global_name;

		static UnaryOperandLocation stack(int32_t offset);

		static UnaryOperandLocation global(StringHandle name);
	};

	UnaryOperandLocation resolveUnaryOperandLocation(const IrInstruction& instruction, size_t operand_index);

	void appendRipRelativePlaceholder(StringHandle global_name);

	void loadValueFromStack(int32_t offset, int size_in_bits, X64Register target_reg);

	void emitStoreWordToFrame(X64Register source_reg, int32_t offset);

	void emitStoreByteToFrame(X64Register source_reg, int32_t offset);

	void storeValueToStack(int32_t offset, int size_in_bits, X64Register source_reg);

	void loadValueFromGlobal(StringHandle global_name, int size_in_bits, X64Register target_reg);

	void moveImmediateToRegister(X64Register reg, uint64_t value);

	void loadValuePointedByRegister(X64Register reg, int value_size_bits);

	void loadValueFromReferenceSlot(int32_t offset, const IndirectStorageInfo& ref_info, X64Register target_reg);

	bool loadAddressForOperand(const IrInstruction& instruction, size_t operand_index, X64Register target_reg);

	void storeValueToGlobal(StringHandle global_name, int size_in_bits, X64Register source_reg);

	void loadUnaryOperandValue(const UnaryOperandLocation& location, int size_in_bits, X64Register target_reg);

	void storeUnaryOperandValue(const UnaryOperandLocation& location, int size_in_bits, X64Register source_reg);

	void storeIncDecResultValue(TempVar result_var, X64Register source_reg, int size_in_bits);

	UnaryOperandLocation resolveTypedValueLocation(const TypedValue& typed_value);

	void emitIncDecInstruction(X64Register target_reg, bool is_increment);

	void handleIncDecCommon(const IrInstruction& instruction, IncDecKind kind);

	// Helper: Associate result register with result TempVar's stack offset
	void storeConversionResult(const IrInstruction& instruction, X64Register result_reg, int size_in_bits);

	void handlePreIncrement(const IrInstruction& instruction);

	void handlePostIncrement(const IrInstruction& instruction);

	void handlePreDecrement(const IrInstruction& instruction);

	void handlePostDecrement(const IrInstruction& instruction);

	void handleUnaryOperation(const IrInstruction& instruction, UnaryOperation op);

	void handleSignExtend(const IrInstruction& instruction);

	void handleZeroExtend(const IrInstruction& instruction);

	void handleTruncate(const IrInstruction& instruction);

	void handleFloatToInt(const IrInstruction& instruction);

	void handleIntToFloat(const IrInstruction& instruction);

	void handleFloatToFloat(const IrInstruction& instruction);

	void handleAddAssign(const IrInstruction& instruction);

	void handleSubAssign(const IrInstruction& instruction);

	void handleMulAssign(const IrInstruction& instruction);

	void handleDivAssign(const IrInstruction& instruction);

	void handleModAssign(const IrInstruction& instruction);

	void handleAndAssign(const IrInstruction& instruction);

	void handleOrAssign(const IrInstruction& instruction);

	void handleXorAssign(const IrInstruction& instruction);

	void handleShlAssign(const IrInstruction& instruction);

	void handleShrAssign(const IrInstruction& instruction);

	void handleAssignment(const IrInstruction& instruction);

	void handleLabel(const IrInstruction& instruction);

	void handleBranch(const IrInstruction& instruction);

	void handleLoopBegin(const IrInstruction& instruction);

	void handleLoopEnd(const IrInstruction& instruction);

	void handleBreak(const IrInstruction& instruction);

	void handleContinue(const IrInstruction& instruction);

	void handleArrayAccess(const IrInstruction& instruction);


	void handleArrayElementAddress(const IrInstruction& instruction);


	void handleArrayStore(const IrInstruction& instruction);


	void handleStringLiteral(const IrInstruction& instruction);

	void handleMemberAccess(const IrInstruction& instruction);

	void handleMemberStore(const IrInstruction& instruction);

	void handleAddressOf(const IrInstruction& instruction);

	void handleAddressOfMember(const IrInstruction& instruction);

	void handleComputeAddress(const IrInstruction& instruction);

	void handleDereference(const IrInstruction& instruction);

	void handleDereferenceStore(const IrInstruction& instruction);

	void handleConditionalBranch(const IrInstruction& instruction);

	void handleFunctionAddress(const IrInstruction& instruction);

	void handleIndirectCall(const IrInstruction& instruction);

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


// IRConverter_Emit_EHSeh.h - Exception handling and SEH emit/handle methods
// Included inside IrToObjConverter class body - do not add #pragma once
	void handleTryBegin([[maybe_unused]] const IrInstruction& instruction);

	void handleTryEnd([[maybe_unused]] const IrInstruction& instruction);

		void materializeCatchObjectFromRax(const CatchBeginOp& catch_op);

	bool currentFunctionReturnsFloatingPointInXmm0() const;

	bool currentFunctionHasCatchParentReturnValue() const;

	int getCatchParentReturnSpillSizeBits() const;

	void emitSavePendingCatchParentReturnValue();

	void emitRestorePendingCatchParentReturnValue();

	void emitRestorePendingCatchParentReturnValue(int32_t catch_return_slot_offset);

	void handleCatchBegin(const IrInstruction& instruction);

	void handleCatchEnd(const IrInstruction& instruction);

	void handleThrow(const IrInstruction& instruction);

	void handleRethrow([[maybe_unused]] const IrInstruction& instruction);

	// ============================================================================
	// Funclet prologue helper: LEA RBP, [RDX + disp32] with deferred patch
	// ============================================================================

		// Emits REX.W LEA RBP, [RDX+disp32] with a zero placeholder and records the
		// instruction offset so it can be patched later with the effective EH frame
		// size used by catch/cleanup funclets. Used by both catch funclet and cleanup
		// funclet prologues.
	void emitFuncletLeaRbpFromRdx(std::vector<uint32_t>& patch_list);

	// ============================================================================
	// Inline destructor call helper (shared by Phase 1 and Phase 2 cleanup LP)
	// ============================================================================

	void emitInlineDestructorCall(const std::pair<StringHandle, StringHandle>& cleanup_var);

		void emitWindowsCleanupFuncletsAndPopulateUnwindMap();

		void emitJmpToLabel(StringHandle target_label);

		void emitLeaLabelAddress(X64Register destination_register, StringHandle target_label);

		uint32_t emitJumpIfZeroRel32Placeholder();

		uint32_t emitSubRSPImm32Placeholder();

		uint32_t emitLeaFromRSPDisp32Placeholder(X64Register destination_register);

		void patchRel32(uint32_t patch_position, uint32_t target_position);

	// ============================================================================
		// ELF-only: "no catch matched" marker — emitted before handlers_end_label.
		// Generates code that loads the saved exception pointer and jumps to the
		// function's cleanup LP (which calls local dtors, then either resumes
		// unwinding or terminates for noexcept functions).
		// The LSDA is also patched (via has_cleanup) so the personality enters this
		// landing pad during phase-2 when no typed catch handler matches.
		// ============================================================================

		void handleElfCatchNoMatch([[maybe_unused]] const IrInstruction& instruction);

		// ============================================================================
		// Phase 2: Function-level cleanup landing pad (ELF/Linux only)
		// ============================================================================

		void handleFunctionCleanupLP(const IrInstruction& instruction);

	// ============================================================================
	// Windows SEH (Structured Exception Handling) Handlers
	// ============================================================================

	void handleSehTryBegin([[maybe_unused]] const IrInstruction& instruction);

	void handleSehTryEnd([[maybe_unused]] const IrInstruction& instruction);

	void handleSehExceptBegin(const IrInstruction& instruction);

	void handleSehExceptEnd([[maybe_unused]] const IrInstruction& instruction);

	void handleSehFinallyCall(const IrInstruction& instruction);

	void handleSehFinallyBegin([[maybe_unused]] const IrInstruction& instruction);

	void handleSehFinallyEnd([[maybe_unused]] const IrInstruction& instruction);

	void handleSehFilterBegin([[maybe_unused]] const IrInstruction& instruction);

	void handleSehGetExceptionCode(const IrInstruction& instruction);

	void handleSehGetExceptionInfo(const IrInstruction& instruction);

	void handleSehFilterEnd(const IrInstruction& instruction);

	void handleSehSaveExceptionCode(const IrInstruction& instruction);

	void handleSehGetExceptionCodeBody(const IrInstruction& instruction);

	void handleSehAbnormalTermination(const IrInstruction& instruction);

	void handleSehLeave(const IrInstruction& instruction);

	void finalizeSections();

	// Emit runtime helper functions for dynamic_cast as native x64 code

	void emit_dynamic_cast_runtime_helpers();

	// Emit __dynamic_cast_check function
	//   bool __dynamic_cast_check(type_info* source, type_info* target)
	// Platform-specific implementation:
	//   - Windows: MSVC RTTI with Complete Object Locator format (RCX, RDX)
	//   - Linux: Itanium C++ ABI type_info structures (RDI, RSI)
	// Returns: AL = 1 if cast valid, 0 otherwise
	void emit_dynamic_cast_check_function();

	// Emit __dynamic_cast_throw_bad_cast function
	//   [[noreturn]] void __dynamic_cast_throw_bad_cast()
	// This function throws std::bad_cast exception via C++ runtime
	void emit_dynamic_cast_throw_function();

	void patchBranches();

	void finalizeFunctionBranches();

	// Patch ELF catch handler filter values in the generated code.
	// This is called at function finalization when we know the complete type table.
	// The filter values must match what the LSDA generator will produce.
	void patchElfCatchFilterValues(const std::vector<ObjectFileWriter::TryBlockInfo>& try_blocks);

	// Debug information tracking
	void addLineMapping(uint32_t line_number, int32_t manual_offset = 0);

	// Debug helper to log assembly instruction emission
	void logAsmEmit(const char* context, const uint8_t* bytes, size_t count);

	TWriterClass writer;
	std::vector<uint8_t> textSectionData;
	std::unordered_map<std::string, uint32_t, TransparentStringHash, std::equal_to<>> functionSymbols;
	std::unordered_map<std::string, std::span<const IrInstruction>, TransparentStringHash, std::equal_to<>> function_spans;

	RegisterAllocator regAlloc;

	// Debug information tracking
	StringHandle current_function_name_;
	StringHandle current_function_mangled_name_;  // Changed from string_view to prevent dangling pointer
	uint32_t current_function_offset_ = 0;
	bool current_function_is_variadic_ = false;
	Type current_function_return_type_ = Type::Void;
	int current_function_return_size_in_bits_ = 0;
	bool current_function_has_hidden_return_param_ = false;  // True if function uses hidden return parameter (RVO)
	bool current_function_returns_reference_ = false;  // True if function returns a reference (lvalue or rvalue)
	int32_t current_function_this_offset_ = 0;  // Stack home offset of the implicit this parameter in member functions
	int32_t current_function_varargs_reg_save_offset_ = 0;  // Offset of varargs register save area (Linux only)
	bool skip_previous_function_finalization_ = false;  // Set when a function is skipped due to codegen error

	// CFI instruction tracking for exception handling
	std::vector<ElfFileWriter::CFIInstruction> current_function_cfi_;

	// Pending function info for exception handling
	struct PendingFunctionInfo {
		StringHandle name;
		uint32_t offset;
		uint32_t length;
	};
	std::vector<PendingFunctionInfo> pending_functions_;
	std::vector<StackVariableScope> variable_scopes;

	// Control flow tracking - Phase 5: Use StringHandle for efficient label tracking
	struct PendingBranch {
		StringHandle target_label;
		uint32_t patch_position; // Position in textSectionData where the offset needs to be written
	};
	std::unordered_map<StringHandle, uint32_t> label_positions_;
	std::vector<PendingBranch> pending_branches_;

	// Loop context tracking for break/continue - Phase 5: Use StringHandle
	struct LoopContext {
		StringHandle loop_end_label;       // Label to jump to for break
		StringHandle loop_increment_label; // Label to jump to for continue
	};
	std::vector<LoopContext> loop_context_stack_;

	// Global variable tracking
	struct GlobalVariableInfo {
		StringHandle name;
		Type type;
		size_t size_in_bytes;
		bool is_initialized;
		std::vector<char> init_data;  // Raw bytes for initialized data
		StringHandle reloc_target;    // If valid, data relocation (R_X86_64_64) for this symbol
	};
	std::vector<GlobalVariableInfo> global_variables_;

	// Helper function to check if a variable is a global/static local variable
	bool isGlobalVariable(StringHandle name) const;

	// VTable tracking
	struct VTableInfo {
		StringHandle vtable_symbol;  // e.g., "??_7Base@@6B@" or "_ZTV4Base"
		StringHandle class_name;
		std::vector<std::string> function_symbols;  // Mangled function names in vtable order
		std::vector<std::string> base_class_names;  // Base class names for RTTI (legacy)
		std::vector<ObjectFileWriter::BaseClassDescriptorInfo> base_class_info; // Detailed base class info for RTTI
		const RTTITypeInfo* rtti_info;  // Pointer to RTTI information for this class (nullptr if not polymorphic)
	};
	std::vector<VTableInfo> vtables_;

	// Pending global variable relocations (added after symbols are created)
	struct PendingGlobalRelocation {
		uint64_t offset;
		StringHandle symbol_name;
		uint32_t type;
		int64_t addend = -4;  // Default addend for PC-relative relocations
	};
	std::vector<PendingGlobalRelocation> pending_global_relocations_;

	// Track which stack offsets hold indirect storage (references or address-only pointers)
	std::unordered_map<int32_t, IndirectStorageInfo> indirect_stack_info_;
	// Track TempVar var_numbers that were given reference/address-only metadata by setIndirectStorageInfo.
	// These must be cleared at function boundaries alongside indirect_stack_info_ to prevent
	// stale metadata from polluting later functions (TempVar var_numbers are reused across functions).
	std::vector<size_t> reference_temp_var_numbers_;
	// Map from variable names to their offsets (for reference lookup by name)
	std::unordered_map<std::string, int32_t, TransparentStringHash, std::equal_to<>> variable_name_to_offset_;
	// Track TempVar sizes from instructions that produce them (for correct loads in conditionals)
	std::unordered_map<StringHandle, int> temp_var_sizes_;

	// Track if dynamic_cast runtime helpers need to be emitted
	bool needs_dynamic_cast_runtime_ = false;

	// Track most recently allocated named variable for TempVar linking
	StringHandle last_allocated_variable_name_;
	int32_t last_allocated_variable_offset_ = 0;

	// Prologue patching for stack allocation
	uint32_t current_function_prologue_offset_ = 0;  // Offset of SUB RSP instruction for patching
	size_t max_temp_var_index_ = 0;  // Highest TempVar number used (for stack size calculation)
	int next_temp_var_offset_ = 8;  // Next available offset for TempVar allocation (starts at 8, increments by 8)
	uint32_t current_function_named_vars_size_ = 0;  // Size of named vars + shadow space for current function
	uint32_t current_function_reserved_catch_ref_temp_size_ = 0;  // Windows FH3 reference-catch slots kept near named vars
	std::vector<StringHandle> current_function_reserved_catch_ref_temps_;
	uint32_t current_function_reserved_catch_obj_padding_size_ = 0;  // Padding to avoid FH3 dispCatchObj == 0 for by-value catch temps
	uint32_t current_function_reserved_catch_return_slot_size_ = 0;  // Dedicated FH3 catch-funclet return spill/flag slots kept above temp vars

	// Exception handling tracking
	struct CatchHandler {
		TypeIndex type_index;  // Type index for user-defined types
		Type exception_type;   // Type enum for built-in types (Int, Double, etc.)
		uint32_t handler_offset;  // Code offset of catch handler
		uint32_t handler_end_offset;  // Code offset where catch handler ends
		uint32_t funclet_entry_offset;  // Code offset of catch funclet entry
		uint32_t funclet_end_offset;  // Code offset where catch funclet ends
		int32_t catch_obj_stack_offset;  // Pre-computed stack offset for exception object
		bool is_catch_all;  // True for catch(...)
		bool is_const;  // True if caught by const
		CVReferenceQualifier ref_qualifier = CVReferenceQualifier::None;  // Catch binding reference qualifier

		bool is_reference() const;
		bool is_rvalue_reference() const;
	};

	struct TryBlock {
		uint32_t try_start_offset;  // Code offset where try block starts
		uint32_t try_end_offset;  // Code offset where try block ends
		std::vector<CatchHandler> catch_handlers;  // Associated catch clauses
		std::vector<std::pair<StringHandle, StringHandle>> cleanup_vars;  // {struct_name, var_name} destroyed when unwinding this try state
	};

	// Destructor unwinding support
	struct LocalObject {
		TempVar temp_var;  // Stack location of the object
		TypeIndex type_index;  // Type of the object (for finding destructor)
		int state_when_constructed;  // State number when object was constructed
		StringHandle destructor_name;  // Mangled name of the destructor (if known)
	};

	struct UnwindMapEntry {
		int to_state = -1;  // State to transition to after unwinding
		StringHandle action;  // Name of destructor to call (or empty for no action)
	};

	std::vector<TryBlock> current_function_try_blocks_;  // Try blocks in current function
	TryBlock* current_try_block_ = nullptr;  // Currently active try block being processed
	std::vector<size_t> try_block_nesting_stack_;  // Stack of try block indices for nested try tracking
	size_t pending_catch_try_index_ = SIZE_MAX;  // Try block index awaiting catch handlers
		struct ActiveCatchHandlerRef {
			size_t try_block_index = SIZE_MAX;
			size_t handler_index = SIZE_MAX;
		};
		struct CatchCodegenContext {
			ActiveCatchHandlerRef handler_ref{};
			bool inside_catch_handler = false;
			bool in_catch_funclet = false;
			bool catch_has_pending_parent_return = false;
			StringHandle continuation_label{};
		};
	CatchHandler* current_catch_handler_ = nullptr;  // Currently active catch handler being processed
		ActiveCatchHandlerRef current_catch_handler_ref_{};  // Ref-based active catch handler tracking for nested catch codegen state.
		std::vector<CatchCodegenContext> catch_codegen_context_stack_;  // Saved nested catch codegen contexts.
	bool inside_catch_handler_ = false;  // Tracks whether we're emitting code inside a catch handler (ELF).
	bool in_catch_funclet_ = false;  // Tracks whether codegen is currently inside a Windows catch funclet.
	bool current_function_has_cpp_eh_ = false;  // Pre-scanned: function has C++ try/catch blocks (needs FH3 state variable).
	int32_t catch_funclet_return_slot_offset_ = 0;  // Parent-frame spill slot used to preserve return value across catch funclet continuation setup.
	int32_t catch_funclet_return_flag_slot_offset_ = 0;  // Parent-frame flag slot indicating continuation should return using saved catch return value.
	uint32_t catch_funclet_return_label_counter_ = 0;  // Monotonic counter for synthetic catch return trampoline labels.
	bool catch_has_pending_parent_return_ = false;  // True after a catch return emits the parent-return continuation handoff.
	StringHandle current_catch_continuation_label_;  // Current catch continuation label in parent function.
	struct CatchReturnBridge {
		int32_t return_slot_offset;
		int32_t flag_slot_offset;
	};
	std::unordered_map<StringHandle, CatchReturnBridge> catch_return_bridges_;
	std::unordered_map<StringHandle, StringHandle> catch_continuation_fixup_map_;  // continuation_label → fixup_label for catch path stack restoration
	std::vector<uint32_t> catch_continuation_sub_rsp_patches_;  // Offsets of SUB RSP IMM32 in fixup code, patched with the extra post-frame stack allocation
	std::vector<uint32_t> catch_continuation_lea_rbp_patches_;  // Offsets of LEA RBP,[RSP+N] IMM32 in fixup code, patched with total_stack at function end
	uint32_t eh_prologue_lea_rbp_offset_ = 0;  // Offset of LEA RBP,[RSP+N] in C++ EH prologue, patched with the effective frame size
	uint32_t eh_prologue_extra_sub_rsp_offset_ = 0;  // Offset of the post-SEH-frame SUB RSP imm32 in C++ EH prologue, patched with any extra stack allocation
	std::vector<uint32_t> catch_funclet_lea_rbp_patches_;  // Offsets of LEA RBP,[RDX+N] in catch funclets, patched with the effective frame size
	std::vector<uint32_t> cleanup_funclet_lea_rbp_patches_;  // Offsets of LEA RBP,[RDX+N] in cleanup funclets, patched with the effective frame size
	std::vector<std::pair<StringHandle, StringHandle>> pending_windows_function_cleanup_vars_;  // Function-scope cleanup vars for Windows FH3 unwind funclets
	std::vector<LocalObject> current_function_local_objects_;  // Objects with destructors
	std::vector<UnwindMapEntry> current_function_unwind_map_;  // Unwind map for destructors
	int current_exception_state_ = -1;  // Current exception handling state number

	// ELF catch handler selector dispatch tracking
	// For multi-handler try blocks on Linux, the landing pad needs selector-based dispatch.
	// We emit CMP instructions with placeholder filter values that get patched at function finalization.
	struct ElfCatchFilterPatch {
		uint32_t patch_offset;      // Offset of the IMM32 placeholder in textSectionData
		size_t try_block_index;     // Index of the originating try block (0-based)
		size_t handler_index;       // Handler index within its try block (0-based)
	};
	std::vector<ElfCatchFilterPatch> elf_catch_filter_patches_;
	int32_t elf_exc_ptr_offset_ = 0;   // Stack offset for saved exception pointer
	int32_t elf_selector_offset_ = 0;  // Stack offset for saved selector value
	uint32_t current_function_cleanup_lp_offset_ = 0;  // Offset of function-level cleanup LP (ELF Phase 2)
	bool current_function_is_noexcept_ = false;         // True if current function is declared noexcept
		StringHandle elf_no_match_lp_label_;  // Label for the "no catch matched" entry point into the cleanup LP

	// Windows SEH (Structured Exception Handling) tracking
	struct SehExceptHandler {
		uint32_t handler_offset;  // Code offset of __except handler
		uint32_t filter_result;   // Filter expression evaluation result (temp var number)
		bool is_constant_filter;  // True if filter is a compile-time constant
		int32_t constant_filter_value; // Constant filter value (EXCEPTION_EXECUTE_HANDLER=1, EXCEPTION_CONTINUE_SEARCH=0, etc.)
		uint32_t filter_funclet_offset = 0; // Code offset of filter funclet (for non-constant filters)
	};

	struct SehFinallyHandler {
		uint32_t handler_offset;  // Code offset of __finally handler
	};

	struct SehTryBlock {
		uint32_t try_start_offset;  // Code offset where __try block starts
		uint32_t try_end_offset;    // Code offset where __try block ends
		std::optional<SehExceptHandler> except_handler;   // __except handler (if present)
		std::optional<SehFinallyHandler> finally_handler; // __finally handler (if present)
	};

	std::vector<SehTryBlock> current_function_seh_try_blocks_;  // SEH try blocks in current function
	std::vector<size_t> seh_try_block_stack_;  // Stack of indices into current_function_seh_try_blocks_ for nesting
	uint32_t current_seh_filter_funclet_offset_ = 0;  // Offset of the most recently emitted filter funclet

}; // End of IrToObjConverter class

