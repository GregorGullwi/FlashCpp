#include "Parser.h"
#include "IrGenerator.h"



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

void AstToIr::registerFullExpressionTempDestructor(StringHandle struct_name,
	std::variant<StringHandle, TempVar> object,
	bool object_is_pointer) {
	pending_full_expression_temp_dtors_.push_back(FullExpressionTempDestructorInfo{
		struct_name,
		std::move(object),
		object_is_pointer
	});
}

void AstToIr::emitAndClearFullExpressionTempDestructors() {
	for (auto it = pending_full_expression_temp_dtors_.rbegin();
		it != pending_full_expression_temp_dtors_.rend();
		++it) {
		DestructorCallOp dtor_op;
		dtor_op.struct_name = it->struct_name;
		dtor_op.object = it->object;
		dtor_op.object_is_pointer = it->object_is_pointer;
		ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), Token()));
	}
	pending_full_expression_temp_dtors_.clear();
}



void AstToIr::emitDestructorsForNonLocalExit(size_t target_depth) {
	// Emit destructor calls for all variables in scopes from the current innermost scope
	// down to (but not including) target_depth, in LIFO order (innermost scope first).
	// Does NOT modify scope_stack_ — the normal exitScope() calls will still run later
	// (emitting dead code after the jump), which is harmless.
	for (size_t scope_index = scope_stack_.size(); scope_index > target_depth; --scope_index) {
		const auto& scope_vars = scope_stack_[scope_index - 1];
		for (auto it = scope_vars.rbegin(); it != scope_vars.rend(); ++it) {
			DestructorCallOp dtor_op;
			dtor_op.struct_name = StringTable::getOrInternStringHandle(it->struct_name);
			dtor_op.object = StringTable::getOrInternStringHandle(it->variable_name);
			ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), Token()));
		}
	}
}



// Pre-scan a statement subtree to populate label_scope_depth_map_ before the main IR
// generation pass.  This mirrors the scope-creation rules in the main visitor so that
// every LabelStatementNode is recorded with the scope_stack_ depth it would have during
// the real visit.  Both forward and backward goto statements can then look up the
// target depth from the map.
void AstToIr::prescanLabels(const ASTNode& node, size_t depth) {
	if (node.is<LabelStatementNode>()) {
		const auto& label = node.as<LabelStatementNode>();
		// getOrInternStringHandle accepts string_view directly — no extra allocation needed.
		label_scope_depth_map_[StringTable::getOrInternStringHandle(label.label_name())] = depth;
	} else if (node.is<BlockNode>()) {
		const auto& block = node.as<BlockNode>();
		// Mirror the scope-creation rule from visitBlockNode: a block whose statements
		// are ALL VariableDeclarationNodes (two or more) does NOT push a new scope.
		bool only_var_decls = true;
		size_t var_decl_count = 0;
		// Single pass: gather the scope-creation predicate AND recurse into children.
		block.get_statements().visit([&](const ASTNode& stmt) {
			if (stmt.is<VariableDeclarationNode>()) var_decl_count++;
			else only_var_decls = false;
		});
		bool enter_scope = !(only_var_decls && var_decl_count > 1);
		size_t new_depth = enter_scope ? depth + 1 : depth;
		block.get_statements().visit([&](const ASTNode& stmt) {
			prescanLabels(stmt, new_depth);
		});
	} else if (node.is<IfStatementNode>()) {
		const auto& if_stmt = node.as<IfStatementNode>();
		// if / if constexpr: then/else bodies are BlockNodes which manage their own scope.
		prescanLabels(if_stmt.get_then_statement(), depth);
		if (if_stmt.has_else()) {
			prescanLabels(*if_stmt.get_else_statement(), depth);
		}
	} else if (node.is<ForStatementNode>()) {
		// visitForStatementNode (IrGenerator_Stmt_Control.cpp) calls enterScope() explicitly
		// before visiting the body, adding one scope level for the for-init variables.
		// The body is then a BlockNode which adds another level of its own.
		// Passing depth + 1 here lets the BlockNode branch above handle that second increment.
		prescanLabels(node.as<ForStatementNode>().get_body_statement(), depth + 1);
	} else if (node.is<WhileStatementNode>()) {
		// visitWhileStatementNode does NOT call enterScope(); the body BlockNode does.
		prescanLabels(node.as<WhileStatementNode>().get_body_statement(), depth);
	} else if (node.is<DoWhileStatementNode>()) {
		prescanLabels(node.as<DoWhileStatementNode>().get_body_statement(), depth);
	} else if (node.is<RangedForStatementNode>()) {
		// Ranged-for does NOT call enterScope() — the body BlockNode handles scope.
		prescanLabels(node.as<RangedForStatementNode>().get_body_statement(), depth);
	} else if (node.is<SwitchStatementNode>()) {
		// The switch visitor iterates the body block's children inline without calling
		// enterScope(), so we must NOT let the body go through the BlockNode handler
		// (which would increment depth).  Instead, iterate children at the current depth.
		const auto& body = node.as<SwitchStatementNode>().get_body();
		if (body.is<BlockNode>()) {
			body.as<BlockNode>().get_statements().visit([&](const ASTNode& stmt) {
				prescanLabels(stmt, depth);
			});
		}
	} else if (node.is<CaseLabelNode>()) {
		const auto& case_node = node.as<CaseLabelNode>();
		if (case_node.has_statement()) {
			ASTNode case_stmt = *case_node.get_statement();
			// The switch visitor unwraps case-body BlockNodes inline (no enterScope),
			// so mirror that here: iterate the block's children at the same depth.
			if (case_stmt.is<BlockNode>()) {
				case_stmt.as<BlockNode>().get_statements().visit([&](const ASTNode& s) {
					prescanLabels(s, depth);
				});
			} else {
				prescanLabels(case_stmt, depth);
			}
		}
	} else if (node.is<DefaultLabelNode>()) {
		const auto& def_node = node.as<DefaultLabelNode>();
		if (def_node.has_statement()) {
			ASTNode def_stmt = *def_node.get_statement();
			if (def_stmt.is<BlockNode>()) {
				def_stmt.as<BlockNode>().get_statements().visit([&](const ASTNode& s) {
					prescanLabels(s, depth);
				});
			} else {
				prescanLabels(def_stmt, depth);
			}
		}
	} else if (node.is<TryStatementNode>()) {
		const auto& try_stmt = node.as<TryStatementNode>();
		// Both the try block and each catch body are BlockNodes that manage their own scopes.
		prescanLabels(try_stmt.try_block(), depth);
		for (const auto& catch_node : try_stmt.catch_clauses()) {
			prescanLabels(catch_node.as<CatchClauseNode>().body(), depth);
		}
	}
	// All other node types (expressions, plain declarations, break, continue, return,
	// goto, case values, etc.) cannot contain LabelStatementNodes — no recursion needed.
}



// Helper functions to emit store instructions
// These can be used by both the unified handler and special-case code

// Emit ArrayStore instruction
void AstToIr::emitArrayStore(TypeCategory element_type, int element_size_bits,
std::variant<StringHandle, TempVar> array,
const TypedValue& index, const TypedValue& value,
int64_t member_offset, bool is_pointer_to_array,
const Token& token) {
	ArrayStoreOp payload;
	payload.element_type_index = TypeIndex{0, element_type};
	payload.element_size_in_bits = element_size_bits;
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
void AstToIr::emitDereferenceStore(const TypedValue& value, TypeCategory pointee_type, [[maybe_unused]] int pointee_size_bits,
std::variant<StringHandle, TempVar> pointer,
const Token& token) {
	DereferenceStoreOp store_op;
	store_op.value = value;

	// Populate pointer TypedValue
	store_op.pointer.setType(pointee_type);
	store_op.pointer.type_index = TypeIndex{0, pointee_type};
	store_op.pointer.size_in_bits = SizeInBits{64};  // Pointer is always 64 bits
	store_op.pointer.pointer_depth = PointerDepth{1};  // Single pointer dereference
	// Convert std::variant<StringHandle, TempVar> to IrValue
	if (const auto* string = std::get_if<StringHandle>(&pointer)) {
		store_op.pointer.value = *string;
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
size_t AstToIr::getSizeInBytes(TypeIndex type_index, int size_in_bits) const {
	const TypeCategory type = type_index.category();
	// Use IrType to catch both TypeCategory::Struct and TypeCategory::UserDefined (which maps
	// to IrType::Struct) so that typedef-to-struct aliases use the struct-layout
	// path and get total_size instead of falling through to the scalar path.
	if (isIrStructType(toIrType(type))) {
		if (type_index.is_valid() && type_index.index() < getTypeInfoCount()) {
			const TypeInfo& type_info = getTypeInfo(type_index);
			if (const StructTypeInfo* struct_info = type_info.getStructInfo()) {
				return struct_info->total_size;
			}
		}
		// TypeCategory::Struct must always have a valid StructInfo; reaching here for
		// a genuine Struct is a compiler bug.  TypeCategory::UserDefined may be a
		// typedef to a primitive, so fall through to the generic path.
		assert(type != TypeCategory::Struct && "TypeCategory::Struct without valid StructInfo is a compiler bug");
	}
	// Non-struct path: size the runtime value representation for a non-pointer.
	return static_cast<size_t>(getRuntimeValueSizeBits(type_index, size_in_bits, PointerDepth{}) / 8);
}

int AstToIr::getPointerElementSize(TypeIndex type_index, int pointer_depth) const {
	if (pointer_depth > 1) {
		return 8;
	}
	int size = static_cast<int>(getSizeInBytes(type_index, get_type_size_bits(type_index.category())));
	return size != 0 ? size : 1;
}

TypeCategory AstToIr::getRuntimeValueType(TypeIndex semantic_type_index, PointerDepth pointer_depth) const {
	const TypeCategory semantic_type = semantic_type_index.category();
	if (pointer_depth.is_pointer()) {
		return semantic_type;
	}

	TypeCategory lowered_cat = resolve_type_alias(semantic_type_index);

	if (lowered_cat == TypeCategory::Enum && semantic_type_index.is_valid() && semantic_type_index.index() < getTypeInfoCount()) {
		if (const EnumTypeInfo* enum_info = getTypeInfo(semantic_type_index).getEnumInfo()) {
			return enum_info->underlying_type;
		}
	}

	return lowered_cat;
}

int AstToIr::getRuntimeValueSizeBits(TypeIndex type_index, int semantic_size_bits, PointerDepth pointer_depth) const {
	if (pointer_depth.is_pointer()) {
		return semantic_size_bits;
	}

	TypeCategory lowered_cat = resolve_type_alias(type_index);
	const TypeCategory semantic_cat = type_index.category();

	if (lowered_cat == TypeCategory::Enum && type_index.is_valid() && type_index.index() < getTypeInfoCount()) {
		const TypeInfo& type_info = getTypeInfo(type_index);
		if (const EnumTypeInfo* enum_info = type_info.getEnumInfo()) {
			return static_cast<int>(enum_info->underlying_size);
		}
		// Forward-declared enums / aliases may only have type_size_.
		if (type_info.type_size_ > 0) {
			return type_info.type_size_;
		}
	}

	if (semantic_cat == TypeCategory::UserDefined && type_index.is_valid() && type_index.index() < getTypeInfoCount()) {
		if (getTypeInfo(type_index).type_size_ > 0) {
			return getTypeInfo(type_index).type_size_;
		}
	}

	return semantic_size_bits;
}

std::optional<ExprResult> AstToIr::tryMakeEnumeratorConstantExpr(const TypeSpecifierNode& type_node, StringHandle identifier_handle) const {
	if (type_node.category() != TypeCategory::Enum || type_node.is_reference() || type_node.pointer_depth() > 0) {
		return std::nullopt;
	}

	if (!type_node.type_index().is_valid() || type_node.type_index().index() >= getTypeInfoCount()) {
		return std::nullopt;
	}

	const EnumTypeInfo* enum_info = getTypeInfo(type_node.type_index()).getEnumInfo();
	if (!enum_info) {
		return std::nullopt;
	}

	return tryMakeEnumeratorConstantExpr(*enum_info, identifier_handle);
}

std::optional<ExprResult> AstToIr::tryMakeEnumeratorConstantExpr(const EnumTypeInfo& enum_info, StringHandle identifier_handle) const {
	const Enumerator* enumerator = enum_info.findEnumerator(identifier_handle);
	if (!enumerator) {
		return std::nullopt;
	}

	return makeExprResult(nativeTypeIndex(enum_info.underlying_type), SizeInBits{static_cast<int>(enum_info.underlying_size)}, static_cast<unsigned long long>(enumerator->value), PointerDepth{}, ValueStorage::ContainsData);
}




// ── inline private helpers (IrGenerator_Lambdas.cpp) ──
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
TempVar AstToIr::emitAddressOf(TypeCategory type, int size_in_bits, IrValue source, Token token) {
	TempVar addr_var = var_counter.next();
	AddressOfOp addr_op;
	addr_op.result = addr_var;
	addr_op.operand.setType(type);
	addr_op.operand.size_in_bits = SizeInBits{size_in_bits};
	addr_op.operand.pointer_depth = PointerDepth{};
	addr_op.operand.value = source;
	ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), token));
	return addr_var;
}


/// Emit a Dereference IR instruction and return the result TempVar holding the loaded value.
TempVar AstToIr::emitDereference(TypeCategory pointee_type, int pointer_size_bits, int pointer_depth, IrValue pointer_value, Token token) {
	TempVar result_var = var_counter.next();
	DereferenceOp deref_op;
	deref_op.result = result_var;
	deref_op.pointer.setType(pointee_type);
	deref_op.pointer.type_index = TypeIndex{0, pointee_type};
	deref_op.pointer.size_in_bits = SizeInBits{static_cast<int>(pointer_size_bits)};
	deref_op.pointer.pointer_depth = PointerDepth{pointer_depth};
	deref_op.pointer.value = pointer_value;
	ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), token));
	return result_var;
}



// ============================================================================
// Return IR helper
// ============================================================================
void AstToIr::emitReturn(IrValue return_value, TypeCategory return_type, int return_size, const Token& token) {
	ReturnOp ret_op;
	ret_op.return_value = return_value;
	ret_op.return_type_index = TypeIndex{0, return_type};
	ret_op.return_size = return_size;
	ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), token));
}
