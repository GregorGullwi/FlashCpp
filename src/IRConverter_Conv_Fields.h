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
	bool current_function_has_hidden_return_param_ = false;  // True if function uses hidden return parameter (RVO)
	bool current_function_returns_reference_ = false;  // True if function returns a reference (lvalue or rvalue)
	int32_t current_function_varargs_reg_save_offset_ = 0;  // Offset of varargs register save area (Linux only)
	
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
	};
	std::vector<GlobalVariableInfo> global_variables_;
	
	// Helper function to check if a variable is a global/static local variable
	bool isGlobalVariable(StringHandle name) const {
		for (const auto& global : global_variables_) {
			if (global.name == name) {
				return true;
			}
		}
		return false;
	}

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

	// Track which stack offsets hold references (parameters or locals)
	std::unordered_map<int32_t, ReferenceInfo> reference_stack_info_;
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
		bool is_reference;  // True if caught by lvalue reference
		bool is_rvalue_reference;  // True if caught by rvalue reference
	};

	struct TryBlock {
		uint32_t try_start_offset;  // Code offset where try block starts
		uint32_t try_end_offset;  // Code offset where try block ends
		std::vector<CatchHandler> catch_handlers;  // Associated catch clauses
	};

	// Destructor unwinding support
	struct LocalObject {
		TempVar temp_var;  // Stack location of the object
		TypeIndex type_index;  // Type of the object (for finding destructor)
		int state_when_constructed;  // State number when object was constructed
		StringHandle destructor_name;  // Mangled name of the destructor (if known)
	};

	struct UnwindMapEntry {
		int to_state;  // State to transition to after unwinding
		StringHandle action;  // Name of destructor to call (or empty for no action)
	};

	std::vector<TryBlock> current_function_try_blocks_;  // Try blocks in current function
	TryBlock* current_try_block_ = nullptr;  // Currently active try block being processed
	std::vector<size_t> try_block_nesting_stack_;  // Stack of try block indices for nested try tracking
	size_t pending_catch_try_index_ = SIZE_MAX;  // Try block index awaiting catch handlers
	CatchHandler* current_catch_handler_ = nullptr;  // Currently active catch handler being processed
	bool inside_catch_handler_ = false;  // Tracks whether we're emitting code inside a catch handler (ELF).
	bool in_catch_funclet_ = false;  // Tracks whether codegen is currently inside a Windows catch funclet.
	bool current_function_has_cpp_eh_ = false;  // Pre-scanned: function has C++ try/catch blocks (needs FH3 state variable).
	int32_t catch_funclet_return_slot_offset_ = 0;  // Parent-frame spill slot used to preserve return value across catch funclet continuation setup.
	int32_t catch_funclet_return_flag_slot_offset_ = 0;  // Parent-frame flag slot indicating continuation should return using saved catch return value.
	uint32_t catch_funclet_return_label_counter_ = 0;  // Monotonic counter for synthetic catch return trampoline labels.
	bool catch_funclet_terminated_by_return_ = false;  // True after a return statement emits a terminating catch-funclet return path.
	StringHandle current_catch_continuation_label_;  // Current catch continuation label in parent function.
	struct CatchReturnBridge {
		int32_t return_slot_offset;
		int32_t flag_slot_offset;
		int return_size_bits;
		bool is_float;
	};
	std::unordered_map<StringHandle, CatchReturnBridge> catch_return_bridges_;
	std::unordered_map<StringHandle, StringHandle> catch_continuation_fixup_map_;  // continuation_label → fixup_label for catch path stack restoration
	std::vector<uint32_t> catch_continuation_sub_rsp_patches_;  // Offsets of SUB RSP IMM32 in fixup code, patched with total_stack at function end
	uint32_t eh_prologue_lea_rbp_offset_ = 0;  // Offset of LEA RBP,[RSP+N] in C++ EH prologue, patched with total_stack
	std::vector<uint32_t> catch_funclet_lea_rbp_patches_;  // Offsets of LEA RBP,[RDX+N] in catch funclets, patched with total_stack
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
