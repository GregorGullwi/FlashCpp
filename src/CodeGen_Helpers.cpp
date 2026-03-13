#include "CodeGen.h"



void AstToIr::exitScope() {
	if (!scope_stack_.empty()) {
		// If try-cleanup capture is active and this is the target scope depth,
		// record vars in LIFO order before emitting their destructors.
		if (capture_try_cleanup_ && scope_stack_.size() == capture_try_cleanup_depth_) {
			for (auto it = scope_stack_.back().rbegin(); it != scope_stack_.back().rend(); ++it) {
				captured_try_cleanup_vars_.push_back(*it);
			}
		}
		// Generate destructor calls for all variables in this scope (in reverse order)
		const auto& scope_vars = scope_stack_.back();
		for (auto it = scope_vars.rbegin(); it != scope_vars.rend(); ++it) {
			// Generate destructor call
			DestructorCallOp dtor_op;
			dtor_op.struct_name = StringTable::getOrInternStringHandle(it->struct_name);
			dtor_op.object = StringTable::getOrInternStringHandle(it->variable_name);
			ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), Token()));
		}
		scope_stack_.pop_back();
	}
}



void AstToIr::emitActiveCatchScopeDestructors() {
	if (catch_scope_stack_.empty()) {
		return;
	}

	size_t catch_scope_index = catch_scope_stack_.size();
	for (size_t i = catch_scope_stack_.size(); i > 0; --i) {
		size_t candidate_index = i - 1;
		if (catch_scope_stack_[candidate_index].try_depth == active_try_statement_depth_) {
			catch_scope_index = candidate_index;
			break;
		}
	}

	if (catch_scope_index == catch_scope_stack_.size()) {
		return;
	}

	// The try-depth match answers whether this throw/rethrow is leaving an active
	// catch context at all. Once it is, frontend-emitted cleanup must cover the
	// full active catch chain because Windows catch-body locals are not yet
	// modeled as first-class unwind actions in the FH3 unwind map.
	size_t catch_scope_base_depth = catch_scope_stack_.front().base_depth;

	if (scope_stack_.size() <= catch_scope_base_depth) {
		return;
	}

	for (size_t scope_index = scope_stack_.size(); scope_index > catch_scope_base_depth; --scope_index) {
		const auto& scope_vars = scope_stack_[scope_index - 1];
		for (auto it = scope_vars.rbegin(); it != scope_vars.rend(); ++it) {
			DestructorCallOp dtor_op;
			dtor_op.struct_name = StringTable::getOrInternStringHandle(it->struct_name);
			dtor_op.object = StringTable::getOrInternStringHandle(it->variable_name);
			ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), Token()));
		}
	}
}



void AstToIr::exitFunctionScope() {
	if (scope_stack_.empty()) return;

	// Capture vars in LIFO order for the cleanup LP, but only those with destructors
	pending_function_cleanup_vars_.clear();
	for (auto it = scope_stack_.back().rbegin(); it != scope_stack_.back().rend(); ++it) {
		pending_function_cleanup_vars_.push_back({
			StringTable::getOrInternStringHandle(it->struct_name),
			StringTable::getOrInternStringHandle(it->variable_name)
		});
	}

	// Call normal exitScope to emit destructor IR instructions
	exitScope();
}



void AstToIr::emitPendingFunctionCleanupLP(const Token& token) {
	// Always emit FunctionCleanupLP when either:
	// (a) There are function-scope local vars with destructors (the common case), OR
	// (b) The function has typed catch handlers (function_has_typed_catch_ flag).
	//     On ELF, ElfCatchNoMatch emits a forward reference to __elf_no_match_lp_<n>
	//     that must be resolved by handleFunctionCleanupLP().  On Windows, the
	//     ElfCatchNoMatch handler is a no-op, but emitting an extra FunctionCleanupLP
	//     with empty cleanup_vars is also a no-op there (early return in the handler).
	if (pending_function_cleanup_vars_.empty() && !function_has_typed_catch_) return;

	FunctionCleanupLPOp cleanup_op;
	cleanup_op.cleanup_vars = std::move(pending_function_cleanup_vars_);
	ir_.addInstruction(IrInstruction(IrOpcode::FunctionCleanupLP, std::move(cleanup_op), token));
	function_has_typed_catch_ = false;
}



void AstToIr::registerVariableWithDestructor(const std::string& var_name, const std::string& struct_name) {
	if (!scope_stack_.empty()) {
		scope_stack_.back().push_back({var_name, struct_name});
	}
}



// Helper functions to emit store instructions
// These can be used by both the unified handler and special-case code

// Emit ArrayStore instruction
void AstToIr::emitArrayStore(Type element_type, int element_size_bits,
std::variant<StringHandle, TempVar> array,
const TypedValue& index, const TypedValue& value,
int64_t member_offset, bool is_pointer_to_array,
const Token& token) {
	ArrayStoreOp payload;
	payload.element_type = element_type;
	payload.element_size_in_bits = SizeInBits{element_size_bits};
	payload.array = array;
	payload.index = index;
	payload.value = value;
	payload.member_offset = member_offset;
	payload.is_pointer_to_array = is_pointer_to_array;
	
	ir_.addInstruction(IrInstruction(IrOpcode::ArrayStore, std::move(payload), token));
}



// Emit MemberStore instruction
void AstToIr::emitMemberStore(const TypedValue& value,
std::variant<StringHandle, TempVar> object,
StringHandle member_name, int offset,
CVReferenceQualifier ref_qualifier,
bool is_pointer_to_member,
const Token& token,
std::optional<size_t> bitfield_width,
size_t bitfield_bit_offset) {
	MemberStoreOp member_store;
	member_store.value = value;
	member_store.object = object;
	member_store.member_name = member_name;
	member_store.offset = offset;
	member_store.struct_type_info = nullptr;
	member_store.ref_qualifier = ref_qualifier;
	member_store.vtable_symbol = StringHandle();
	member_store.is_pointer_to_member = is_pointer_to_member;
	member_store.bitfield_width = bitfield_width;
	member_store.bitfield_bit_offset = bitfield_bit_offset;
	
	ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
}



// Emit DereferenceStore instruction
void AstToIr::emitDereferenceStore(const TypedValue& value, Type pointee_type, [[maybe_unused]] int pointee_size_bits,
std::variant<StringHandle, TempVar> pointer,
const Token& token) {
	DereferenceStoreOp store_op;
	store_op.value = value;
	
	// Populate pointer TypedValue
	store_op.pointer.type = pointee_type;
	store_op.pointer.size_in_bits = SizeInBits{64};  // Pointer is always 64 bits
	store_op.pointer.pointer_depth = PointerDepth{1};  // Single pointer dereference
	// Convert std::variant<StringHandle, TempVar> to IrValue
	if (std::holds_alternative<StringHandle>(pointer)) {
		store_op.pointer.value = std::get<StringHandle>(pointer);
	} else {
		store_op.pointer.value = std::get<TempVar>(pointer);
	}
	
	ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_op), token));
}



const DeclarationNode& AstToIr::requireDeclarationNode(const ASTNode& node, std::string_view context) const {
	try {
		return node.as<DeclarationNode>();
	} catch (...) {
		FLASH_LOG(Codegen, Error, "BAD DeclarationNode cast in ", context,
		": type_name=", node.type_name(),
		" has_value=", node.has_value());
		throw;
	}
}



// Helper to get the size of a type in bytes
// Reuses the same logic as sizeof() operator
// Used for pointer arithmetic (++/-- operators need sizeof(pointee_type))
size_t AstToIr::getSizeInBytes(Type type, TypeIndex type_index, int size_in_bits) const {
	if (type == Type::Struct) {
		assert(type_index < gTypeInfo.size() && "Invalid type_index for struct");
		const TypeInfo& type_info = gTypeInfo[type_index];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		assert(struct_info && "Struct type info not found");
		return struct_info->total_size;
	}
	// For Enum types with a valid type_index, look up the size from EnumTypeInfo.
	// Note: TypeInfo::type_size_ is NOT set for fully-defined enums (only for
	// forward-declared enums and typedef aliases), so we must read underlying_size
	// from the EnumTypeInfo directly.  underlying_size is in bits.
	if (type == Type::Enum && type_index > 0 && type_index < gTypeInfo.size()) {
		const TypeInfo& type_info = gTypeInfo[type_index];
		if (const EnumTypeInfo* enum_info = type_info.getEnumInfo()) {
			return enum_info->underlying_size / 8;
		}
		// Fallback for forward-declared enums that only have type_size_ set
		if (type_info.type_size_ > 0) {
			return type_info.type_size_ / 8;
		}
	}
	// For UserDefined (typedef) types with a valid type_index, type_size_ is
	// set from size_in_bits() at the typedef site, so it is in bits.
	if (type == Type::UserDefined && type_index > 0 && type_index < gTypeInfo.size()) {
		if (gTypeInfo[type_index].type_size_ > 0) {
			return gTypeInfo[type_index].type_size_ / 8;
		}
	}
	// For primitive types, convert bits to bytes
	return size_in_bits / 8;
}




// ── inline private helpers (CodeGen_Lambdas.cpp) ──
/// Unified symbol lookup: searches local scope first, then falls back to global scope
std::optional<ASTNode> AstToIr::lookupSymbol(StringHandle handle) const {
	auto symbol = symbol_table.lookup(handle);
	if (!symbol.has_value() && global_symbol_table_) {
		symbol = global_symbol_table_->lookup(handle);
	}
	return symbol;
}



std::optional<ASTNode> AstToIr::lookupSymbol(std::string_view name) const {
	auto symbol = symbol_table.lookup(name);
	if (!symbol.has_value() && global_symbol_table_) {
		symbol = global_symbol_table_->lookup(name);
	}
	return symbol;
}


/// Emit an AddressOf IR instruction and return the result TempVar holding the address.
TempVar AstToIr::emitAddressOf(Type type, int size_in_bits, IrValue source, Token token) {
	TempVar addr_var = var_counter.next();
	AddressOfOp addr_op;
	addr_op.result = addr_var;
	addr_op.operand.type = type;
	addr_op.operand.size_in_bits = SizeInBits{size_in_bits};
	addr_op.operand.pointer_depth = PointerDepth{};
	addr_op.operand.value = source;
	ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), token));
	return addr_var;
}


/// Emit a Dereference IR instruction and return the result TempVar holding the loaded value.
TempVar AstToIr::emitDereference(Type pointee_type, int pointer_size_bits, int pointer_depth, IrValue pointer_value, Token token) {
	TempVar result_var = var_counter.next();
	DereferenceOp deref_op;
	deref_op.result = result_var;
	deref_op.pointer.type = pointee_type;
	deref_op.pointer.size_in_bits = SizeInBits{static_cast<int>(pointer_size_bits)};
	deref_op.pointer.pointer_depth = PointerDepth{pointer_depth};
	deref_op.pointer.value = pointer_value;
	ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), token));
	return result_var;
}



// ============================================================================
// Return IR helper
// ============================================================================
void AstToIr::emitReturn(IrValue return_value, Type return_type, int return_size, const Token& token) {
	ReturnOp ret_op;
	ret_op.return_value = return_value;
	ret_op.return_type = return_type;
	ret_op.return_size = return_size;
	ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), token));
}
