#include "Parser.h"
#include "IrGenerator.h"
#include "CallNodeHelpers.h"
#include "SemanticAnalysis.h"

// ── Shared consteval-materialization helpers ────────────────────────────────

const TypeInfo* AstToIr::resolveToConcreteStructTypeInfo(TypeIndex type_idx) const {
	const TypeInfo* ti = tryGetTypeInfo(type_idx);
	if (!ti)
		return nullptr;
	// Chase aliases up to 64 levels deep.
	for (int depth = 0; depth < 64 && ti && !ti->getStructInfo(); ++depth) {
		ti = tryGetTypeInfo(ti->type_index_);
		if (!ti)
			break;
	}
	return ti;
}

unsigned long long AstToIr::evalResultScalarToRaw(const ConstExpr::EvalResult& r) {
	if (const auto* ll = std::get_if<long long>(&r.value))
		return static_cast<unsigned long long>(*ll);
	if (const auto* ull = std::get_if<unsigned long long>(&r.value))
		return *ull;
	if (const auto* b = std::get_if<bool>(&r.value))
		return *b ? 1ULL : 0ULL;
	return static_cast<unsigned long long>(r.as_int());
}

void AstToIr::populateReferenceReturnInfo(CallOp& call_op, const TypeSpecifierNode& return_type) {
	call_op.returns_reference =
		(return_type.is_reference() || return_type.is_rvalue_reference()) &&
		!return_type.has_function_signature();
	call_op.returns_rvalue_reference = return_type.is_rvalue_reference();
	call_op.referenced_value_size_in_bits = call_op.returns_reference
												? SizeInBits{getTypeSpecSizeBits(return_type)}
												: call_op.return_size_in_bits;
}

void AstToIr::populateReferenceReturnInfo(VirtualCallOp& call_op, const TypeSpecifierNode& return_type) {
	call_op.returns_reference =
		(return_type.is_reference() || return_type.is_rvalue_reference()) &&
		!return_type.has_function_signature();
	call_op.returns_rvalue_reference = return_type.is_rvalue_reference();
	call_op.referenced_value_size_in_bits = call_op.returns_reference
												? SizeInBits{getTypeSpecSizeBits(return_type)}
												: call_op.result.size_in_bits;
}

TypeIndex AstToIr::getFunctionSignatureReturnTypeIndex(const FunctionSignature& signature) const {
	if (!needs_type_index(signature.returnType())) {
		TypeIndex native = nativeTypeIndex(signature.returnType());
		return native.is_valid()
			? native.withCategory(signature.returnType())
			: TypeIndex{}.withCategory(signature.returnType());
	}

	if (signature.return_type_index.is_valid()) {
		const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(signature.return_type_index);
		if (resolved_alias.type_index.is_valid()) {
			return resolved_alias.type_index.withCategory(resolved_alias.type_index.category());
		}
		return signature.return_type_index.withCategory(signature.returnType());
	}

	TypeIndex fallback = nativeTypeIndex(signature.returnType());
	return fallback.is_valid()
		? fallback.withCategory(signature.returnType())
		: TypeIndex{}.withCategory(signature.returnType());
}

int AstToIr::getFunctionSignatureReturnSizeBits(const FunctionSignature& signature) const {
	if (signature.returnType() == TypeCategory::Void) {
		return 0;
	}

	if (!needs_type_index(signature.returnType())) {
		return get_type_size_bits(signature.returnType());
	}

	if (signature.return_type_index.is_valid()) {
		const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(signature.return_type_index);
		if (resolved_alias.terminal_type_info && resolved_alias.terminal_type_info->hasStoredSize()) {
			return resolved_alias.terminal_type_info->sizeInBits().value;
		}

		const TypeIndex canonical_index = resolved_alias.type_index.is_valid()
			? resolved_alias.type_index.withCategory(resolved_alias.type_index.category())
			: signature.return_type_index.withCategory(signature.returnType());
		if (!needs_type_index(canonical_index.category())) {
			const int bits = get_type_size_bits(canonical_index.category());
			if (bits > 0) {
				return bits;
			}
		}
	}

	return get_type_size_bits(signature.returnType());
}

static TypeSpecifierNode normalizeCallReturnType(TypeSpecifierNode return_type);

void AstToIr::populateIndirectCallReturnInfo(IndirectCallOp& call_op, const FunctionSignature& signature) {
	call_op.return_type_index = getFunctionSignatureReturnTypeIndex(signature);
	call_op.return_size_in_bits = SizeInBits{getFunctionSignatureReturnSizeBits(signature)};
	call_op.use_return_slot = needsHiddenReturnParam(
		call_op.returnType(),
		0,
		false,
		call_op.return_size_in_bits.value,
		context_->isLLP64());
}

static TypeIndex getCallReturnTypeIndex(const TypeSpecifierNode& return_type) {
	if (return_type.type_index().is_valid()) {
		return return_type.type_index().withCategory(return_type.type());
	}

	TypeIndex native = nativeTypeIndex(return_type.type());
	return native.is_valid()
		? native.withCategory(return_type.type())
		: TypeIndex{}.withCategory(return_type.type());
}

void AstToIr::populateCallReturnInfo(CallOp& call_op, const TypeSpecifierNode& return_type) {
	const TypeSpecifierNode normalized_return_type = normalizeCallReturnType(return_type);
	call_op.return_type_index = getCallReturnTypeIndex(normalized_return_type);
	call_op.return_size_in_bits = SizeInBits{
		(normalized_return_type.pointer_depth() > 0 ||
		 normalized_return_type.is_reference() ||
		 normalized_return_type.is_rvalue_reference())
			? POINTER_SIZE_BITS
			: static_cast<int>(normalized_return_type.size_in_bits())};

	// When TypeSpecifierNode reports size 0 for a non-void type, resolve it:
	// structs via tryGetStructTypeInfo, primitives via get_type_size_bits.
	if (!call_op.return_size_in_bits.is_set() && normalized_return_type.category() != TypeCategory::Void) {
		const TypeCategory cat = normalized_return_type.category();
		if (cat == TypeCategory::Struct && normalized_return_type.type_index().is_valid()) {
			if (const StructTypeInfo* ret_struct = tryGetStructTypeInfo(normalized_return_type.type_index())) {
				call_op.return_size_in_bits = SizeInBits{static_cast<int>(ret_struct->sizeInBits().value)};
			}
		}
		if (!call_op.return_size_in_bits.is_set()) {
			const int fallback_bits = get_type_size_bits(cat);
			if (fallback_bits > 0) {
				call_op.return_size_in_bits = SizeInBits{fallback_bits};
			}
		}
	}

	populateReferenceReturnInfo(call_op, normalized_return_type);
}

CallOp AstToIr::createCallOp(
	TempVar result,
	StringHandle function_name,
	const TypeSpecifierNode& return_type,
	bool is_member_function,
	bool is_variadic) {
	CallOp call_op;
	call_op.result = result;
	call_op.function_name = function_name;
	call_op.is_member_function = is_member_function;
	call_op.is_variadic = is_variadic;
	populateCallReturnInfo(call_op, return_type);
	return call_op;
}

CallOp AstToIr::createCallOp(
	TempVar result,
	StringHandle function_name,
	TypeIndex return_type_index,
	SizeInBits return_size_in_bits,
	bool is_member_function,
	bool is_variadic) {
	CallOp call_op;
	call_op.result = result;
	call_op.function_name = function_name;
	call_op.return_type_index = return_type_index;
	call_op.return_size_in_bits = return_size_in_bits;
	call_op.is_member_function = is_member_function;
	call_op.is_variadic = is_variadic;
	return call_op;
}

ExprResult AstToIr::buildIndirectCallReturnResult(const FunctionSignature& signature, TempVar ret_var) const {
	return makeExprResult(
		getFunctionSignatureReturnTypeIndex(signature),
		SizeInBits{getFunctionSignatureReturnSizeBits(signature)},
		IrOperand{ret_var},
		PointerDepth{},
		ValueStorage::ContainsData);
}

static TypeSpecifierNode normalizeCallReturnType(TypeSpecifierNode return_type) {
	if (return_type.type() != TypeCategory::TypeAlias || !return_type.type_index().is_valid()) {
		return return_type;
	}

	const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(return_type.type_index());
	if (resolved_alias.terminal_type_info) {
		return_type.set_type_index(resolved_alias.terminal_type_info->type_index_);
		return_type.set_category(resolved_alias.terminal_type_info->category());
	}
	return_type.add_pointer_levels(static_cast<int>(resolved_alias.pointer_depth));
	if (return_type.reference_qualifier() == ReferenceQualifier::None &&
		resolved_alias.reference_qualifier != ReferenceQualifier::None) {
		return_type.set_reference_qualifier(resolved_alias.reference_qualifier);
	}
	if (!return_type.has_function_signature() && resolved_alias.function_signature.has_value()) {
		return_type.set_function_signature(*resolved_alias.function_signature);
	}
	if (!resolved_alias.array_dimensions.empty()) {
		std::vector<size_t> array_dimensions = return_type.array_dimensions();
		array_dimensions.insert(array_dimensions.end(),
								resolved_alias.array_dimensions.begin(),
								resolved_alias.array_dimensions.end());
		return_type.set_array_dimensions(array_dimensions);
	}
	return return_type;
}

ExprResult AstToIr::buildCallReturnResult(
	const TypeSpecifierNode& return_type,
	TempVar ret_var,
	ExpressionContext context,
	const Token& source_token) {
	const TypeSpecifierNode normalized_return_type = normalizeCallReturnType(return_type);
	// ── Reference-returning functions ──────────────────────────────────────
	if (normalized_return_type.is_reference() || normalized_return_type.is_rvalue_reference()) {
		LValueInfo lvalue_info(LValueInfo::Kind::Indirect, ret_var, 0);
		int referenced_size_bits = getTypeSpecSizeBits(normalized_return_type);
		if (normalized_return_type.is_rvalue_reference()) {
			setTempVarMetadata(ret_var, TempVarMetadata::makeXValue(lvalue_info, normalized_return_type.category(), referenced_size_bits));
		} else {
			setTempVarMetadata(ret_var, TempVarMetadata::makeLValue(lvalue_info, normalized_return_type.category(), referenced_size_bits));
		}

		if (context != ExpressionContext::LValueAddress) {
			const PointerDepth return_pointer_depth{static_cast<int>(normalized_return_type.pointer_depth())};
			if (isIrStructType(toIrType(normalized_return_type.type())) && normalized_return_type.type_index().is_valid()) {
				return makeExprResult(
					normalized_return_type.type_index().withCategory(normalized_return_type.type()),
					SizeInBits{referenced_size_bits},
					IrOperand{ret_var},
					return_pointer_depth,
					ValueStorage::ContainsAddress);
			}

			TypeCategory pointee_type = getRuntimeValueType(normalized_return_type.type_index().withCategory(normalized_return_type.type()), return_pointer_depth);
			int pointee_size_bits = getRuntimeValueSizeBits(normalized_return_type.type_index(), referenced_size_bits, return_pointer_depth);
			int dereference_pointer_depth = normalized_return_type.pointer_depth() > 0 ? static_cast<int>(normalized_return_type.pointer_depth()) : 1;
			TempVar loaded_value = emitDereference(pointee_type, pointee_size_bits, dereference_pointer_depth, IrValue(ret_var), source_token);
			LValueInfo deref_lvalue_info(LValueInfo::Kind::Indirect, ret_var, 0);
			auto metadata = normalized_return_type.is_rvalue_reference()
				? TempVarMetadata::makeXValue(deref_lvalue_info, pointee_type, pointee_size_bits)
				: TempVarMetadata::makeLValue(deref_lvalue_info, pointee_type, pointee_size_bits);
			setTempVarMetadata(loaded_value, std::move(metadata));
			return makeExprResult(
				normalized_return_type.type_index().withCategory(pointee_type),
				SizeInBits{pointee_size_bits},
				IrOperand{loaded_value},
				return_pointer_depth,
				ValueStorage::ContainsData);
		}
	}

	// ── Non-reference (or LValueAddress context for references) ───────────
	int result_size = (normalized_return_type.pointer_depth() > 0 || normalized_return_type.is_reference() || normalized_return_type.is_rvalue_reference())
						  ? 64
						  : static_cast<int>(normalized_return_type.size_in_bits());
	TypeIndex type_index_result = isIrStructType(toIrType(normalized_return_type.type()))
									  ? normalized_return_type.type_index()
									  : TypeIndex{};
	ValueStorage st = (normalized_return_type.is_reference() || normalized_return_type.is_rvalue_reference())
						  ? ValueStorage::ContainsAddress
						  : ValueStorage::ContainsData;
	return makeExprResult(
		type_index_result.withCategory(normalized_return_type.type()),
		SizeInBits{result_size},
		IrOperand{ret_var},
		PointerDepth{static_cast<int>(normalized_return_type.pointer_depth())},
		st);
}

// Convert a member EvalResult to its raw bit-pattern, preserving IEEE 754 for float/double.
static unsigned long long evalResultMemberToRaw(const ConstExpr::EvalResult& r, TypeCategory member_type) {
	if (member_type == TypeCategory::Float) {
		float fval = static_cast<float>(r.as_double());
		uint32_t fbits = 0;
		std::memcpy(&fbits, &fval, sizeof(float));
		return static_cast<unsigned long long>(fbits);
	}
	if (member_type == TypeCategory::Double || member_type == TypeCategory::LongDouble) {
		double dval = r.as_double();
		unsigned long long dbits = 0;
		std::memcpy(&dbits, &dval, sizeof(double));
		return dbits;
	}
	// Integer / bool / enum: extract as raw integer bits.
	if (const auto* ll = std::get_if<long long>(&r.value))
		return static_cast<unsigned long long>(*ll);
	if (const auto* ull = std::get_if<unsigned long long>(&r.value))
		return *ull;
	if (const auto* b = std::get_if<bool>(&r.value))
		return *b ? 1ULL : 0ULL;
	return static_cast<unsigned long long>(r.as_int());
}

ExprResult AstToIr::materializeConstevalAggregateResult(
	const ConstExpr::EvalResult& eval_result,
	const TypeSpecifierNode& ret_spec,
	TypeCategory ret_type,
	SizeInBits ret_size,
	const Token& call_token) {
	if (eval_result.object_member_bindings.empty())
		return {};

	const TypeInfo* struct_type_info = resolveToConcreteStructTypeInfo(ret_spec.type_index());
	if (!struct_type_info)
		return {};
	const StructTypeInfo* si = struct_type_info->getStructInfo();
	if (!si)
		return {};

	TempVar struct_tmp = var_counter.next();
	StringHandle struct_tmp_handle = StringTable::getOrInternStringHandle(struct_tmp.name());

	VariableDeclOp vdecl;
	vdecl.type_index = TypeIndex(ret_spec.type_index().index(), ret_type);
	vdecl.size_in_bits = ret_size;
	vdecl.var_name = struct_tmp_handle;
	ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(vdecl), call_token));

	for (const auto& member : si->members) {
		const std::string_view member_sv = StringTable::getStringView(member.getName());
		auto it = eval_result.object_member_bindings.find(member_sv);
		if (it == eval_result.object_member_bindings.end())
			continue;
		MemberStoreOp ms;
		ms.value.setType(member.type_index.category());
		ms.value.size_in_bits = SizeInBits{static_cast<int>(member.size * 8)};
		ms.value.value = IrValue{evalResultMemberToRaw(it->second, member.memberType())};
		ms.object = struct_tmp_handle;
		ms.member_name = member.getName();
		ms.offset = static_cast<int>(member.offset);
		ms.struct_type_info = struct_type_info;
		ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(ms), call_token));
	}
	return makeExprResult(ret_spec.type_index().withCategory(ret_type), ret_size, IrOperand{struct_tmp_handle}, PointerDepth{}, ValueStorage::ContainsData);
}

ExprResult AstToIr::generateCallExprIr(const CallExprNode& callExprNode, ExpressionContext context) {
	if (callExprNode.has_receiver()) {
		if (!callExprNode.callee().has_function_declaration()) {
			throw InternalError("CallExprNode with receiver is missing FunctionDeclarationNode");
		}
		const FunctionDeclarationNode& callee_function = *callExprNode.callee().function_declaration_or_null();
		if (callExprNode.callee().is_static_member() || callee_function.is_static()) {
			if (callExprNode.receiver().is<ExpressionNode>()) {
				const ExpressionNode& receiver_expr = callExprNode.receiver().as<ExpressionNode>();
				const bool trivial_receiver =
					std::holds_alternative<IdentifierNode>(receiver_expr) ||
					std::holds_alternative<QualifiedIdentifierNode>(receiver_expr);
				if (!trivial_receiver) {
					visitExpressionNode(receiver_expr);
				}
			}
			CallExprNode direct_static_call = makeResolvedCallExpr(
				callee_function,
				copyCallArguments(callExprNode.arguments()),
				callExprNode.called_from());
			CallMetadataCopyOptions copy_options;
			copy_options.copy_mangled_name = false;
			copyCallMetadata(direct_static_call, callExprNode, copy_options);
			return generateFunctionCallIr(direct_static_call, context, &callExprNode);
		}
		return generateMemberFunctionCallIr(callExprNode, context);
	}
	return generateFunctionCallIr(callExprNode, context);
}


ExprResult AstToIr::generateFunctionCallIr(const CallExprNode& callExprNode, ExpressionContext context) {
	return generateFunctionCallIr(callExprNode, context, &callExprNode);
}

ExprResult AstToIr::generateFunctionCallIr(const CallExprNode& callExprNode, ExpressionContext context, const void* sema_call_key) {
	std::vector<IrOperand> irOperands;
	irOperands.reserve(2 + callExprNode.arguments().size() * 4);	 // ret_var + name + ~4 operands per arg
	auto appendArgumentIrResult = [&](const ExprResult& result) {
		irOperands.reserve(irOperands.size() + 3);
		irOperands.emplace_back(result.typeEnum());
		irOperands.emplace_back(result.size_in_bits.value);
		irOperands.emplace_back(result.value);
	};

	const auto& decl_node = callExprNode.callee().declaration();
	std::string_view func_name_view = decl_node.identifier_token().value();
	std::string_view lookup_name_view = callExprNode.has_qualified_name()
											? callExprNode.qualified_name()
											: func_name_view;

	FLASH_LOG_FORMAT(Codegen, Debug, "=== generateFunctionCallIr: func_name={} ===", func_name_view);

		// Check for compiler intrinsics and handle them specially
	auto intrinsic_result = tryGenerateIntrinsicIr(func_name_view, callExprNode);
	if (intrinsic_result.has_value()) {
		return intrinsic_result.value();
	}

		// Check if this function is marked as inline_always (pure expression template instantiations)
		// These functions should always be inlined and never generate calls
		// Look up the function to check its inline_always flag
	extern SymbolTable gSymbolTable;
	auto all_overloads = gSymbolTable.lookup_all(func_name_view);

	for (const auto& overload : all_overloads) {
		if (overload.is<FunctionDeclarationNode>()) {
			const FunctionDeclarationNode* overload_func_decl = &overload.as<FunctionDeclarationNode>();
			const DeclarationNode* overload_decl = &overload_func_decl->decl_node();

				// Check if this is the matching overload
			if (overload_decl == &decl_node) {
					// Found the matching function - check if it should be inlined
				if (overload_func_decl->is_inline_always() && callExprNode.arguments().size() == 1) {
						// Check if function returns a reference - if so, we need special handling
					const TypeSpecifierNode& return_type_spec = overload_decl->type_node().as<TypeSpecifierNode>();
					bool returns_reference = return_type_spec.is_reference() || return_type_spec.is_rvalue_reference();

					auto arg_node = callExprNode.arguments()[0];
					if (arg_node.is<ExpressionNode>()) {
						FLASH_LOG(Codegen, Debug, "Inlining pure expression function (inline_always): ", func_name_view);

						if (returns_reference) {
								// For functions returning references (like std::move, std::forward),
								// we need to generate an addressof the argument, not just return it
							const ExpressionNode& arg_expr = arg_node.as<ExpressionNode>();

								// Check if the argument is an identifier (common case for move(x))
							if (std::holds_alternative<IdentifierNode>(arg_expr)) {
								const IdentifierNode& ident = std::get<IdentifierNode>(arg_expr);

									// Generate addressof for the identifier
								TempVar result_var = var_counter.next();
								AddressOfOp op;
								op.result = result_var;

									// Get type info from the identifier
								StringHandle id_handle = StringTable::getOrInternStringHandle(ident.name());
								TypeCategory operand_type = TypeCategory::Int;  // Default
								int operand_size = 32;
								if (const DeclarationNode* decl = lookupDeclaration(id_handle)) {
									const TypeSpecifierNode& type = decl->type_node().as<TypeSpecifierNode>();
									operand_type = type.type();
									operand_size = static_cast<int>(type.size_in_bits());
									if (operand_size == 0)
										operand_size = get_type_size_bits(operand_type);
								}

								op.operand.setType(operand_type);
								op.operand.size_in_bits = SizeInBits{static_cast<int>(operand_size)};
								op.operand.pointer_depth = PointerDepth{};
								op.operand.value = id_handle;

								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, op, Token()));

									// Return pointer type (64-bit address) with pointer depth 1
								return makeExprResult(nativeTypeIndex(operand_type), SizeInBits{64}, IrOperand{result_var}, PointerDepth{1}, ValueStorage::ContainsData);
							}
								// For non-identifier expressions, fall through to generate a regular call
								// (we can't inline complex expressions that need reference semantics)
						} else {
								// Non-reference return - can inline directly by returning argument
							auto arg_ir = visitExpressionNode(arg_node.as<ExpressionNode>());
							return arg_ir;
						}
					}
				}
				break;  // Found the matching function, stop searching
			}
		}
	}

		// Check if this is a function pointer call
		// Look up the identifier in both local and global symbol tables
	const std::optional<ASTNode> func_symbol = lookupSymbol(func_name_view);
	const DeclarationNode* func_ptr_decl = nullptr;

		// Check for DeclarationNode directly
	if (func_symbol.has_value() && func_symbol->is<DeclarationNode>()) {
		func_ptr_decl = &func_symbol->as<DeclarationNode>();
	}
		// Also check for VariableDeclarationNode (from comma-separated declarations)
	else if (func_symbol.has_value() && func_symbol->is<VariableDeclarationNode>()) {
		func_ptr_decl = &func_symbol->as<VariableDeclarationNode>().declaration();
	}

	if (func_ptr_decl) {
		const auto& func_type = func_ptr_decl->type_node().as<TypeSpecifierNode>();
		const FunctionDeclarationNode* resolved_operator_call =
			!sema_ ? nullptr : sema_->getResolvedOpCall(sema_call_key);

		if (resolved_operator_call) {
			ChunkedVector<ASTNode> member_args;
			callExprNode.arguments().visit([&](ASTNode argument) {
				member_args.push_back(argument);
			});

			ASTNode object_expr = ASTNode::emplace_node<ExpressionNode>(
				IdentifierNode(func_ptr_decl->identifier_token()));
			CallExprNode member_call = makeResolvedMemberCallExpr(
				object_expr,
				*resolved_operator_call,
				std::move(member_args),
				callExprNode.called_from());
			return generateMemberFunctionCallIr(member_call, context, sema_call_key);
		}

			// Check if this is a function pointer or a substituted function type carrying
			// a function signature. Free-function template parameters can instantiate into
			// the latter form before full canonicalization.
			// auto&& parameters in recursive lambdas need to be treated as callables
		if (func_type.category() != TypeCategory::Struct &&
			(func_type.is_function_pointer() || func_type.has_function_signature())) {
				// This is an indirect call through a function pointer
				// Generate IndirectCall IR: [result_var, func_ptr_var, arg1, arg2, ...]
			TempVar ret_var = var_counter.next();

				// Mark function return value as prvalue (Option 2: Value Category Tracking)
			setTempVarMetadata(ret_var, TempVarMetadata::makePRValue());

				// Generate IR for function arguments
			std::vector<TypedValue> arguments;
			callExprNode.arguments().visit([&](ASTNode argument) {
				ExprResult argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
					// Extract type, size, and value from the expression result
				TypeCategory arg_type = argumentIrOperands.typeEnum();
				int arg_size = argumentIrOperands.size_in_bits.value;
				IrValue arg_value = std::visit([](auto&& arg) -> IrValue {
					using T = std::decay_t<decltype(arg)>;
					if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
								  std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
						return arg;
					} else {
						return 0ULL;
					}
				},
											   argumentIrOperands.value);
				arguments.push_back(makeTypedValue(arg_type, SizeInBits{arg_size}, arg_value));
			});

				// Add the indirect call instruction
			IndirectCallOp op{
				.result = ret_var,
				.function_pointer = StringTable::getOrInternStringHandle(func_name_view),
				.arguments = std::move(arguments)};
			if (func_type.has_function_signature()) {
				populateIndirectCallReturnInfo(op, func_type.function_signature());
			}
			ir_.addInstruction(IrOpcode::IndirectCall, std::move(op), callExprNode.called_from());

				// Return the result variable with the return type from the function signature
			if (func_type.has_function_signature()) {
				return buildIndirectCallReturnResult(func_type.function_signature(), ret_var);
			} else {
					// For auto types or missing signature, default to int
				return makeExprResult(nativeTypeIndex(TypeCategory::Int), SizeInBits{32}, IrOperand{ret_var}, PointerDepth{}, ValueStorage::ContainsData);
			}
		}

		if (func_type.category() == TypeCategory::Struct) {
			const FunctionDeclarationNode* operator_call = nullptr;

				// Fallback: replicate the arity-based lookup for call sites that were
				// not reached by the semantic pass (e.g. template instantiation paths
				// that create CallExprNodes after sema has run).
			if (!operator_call) {
				const TypeInfo* func_type_info = tryGetTypeInfo(func_type.type_index());
				const StructTypeInfo* struct_info = func_type_info ? func_type_info->getStructInfo() : nullptr;
				if (struct_info) {
					const FunctionDeclarationNode* sole_operator_call = nullptr;
					size_t operator_call_count = 0;
					for (const auto& member_func : struct_info->member_functions) {
						if (member_func.operator_kind == OverloadableOperator::Call &&
							member_func.function_decl.is<FunctionDeclarationNode>()) {
							const auto& candidate = member_func.function_decl.as<FunctionDeclarationNode>();
							++operator_call_count;
							if (!sole_operator_call) {
								sole_operator_call = &candidate;
							}
							if (candidate.parameter_nodes().size() == callExprNode.arguments().size()) {
								operator_call = &candidate;
								break;
							}
						}
					}
					if (!operator_call && operator_call_count == 1) {
							// Only fall back when the callable object exposes a single
							// operator(); this avoids silently choosing an arbitrary overload.
						operator_call = sole_operator_call;
					}
				}
			}

			if (operator_call) {
				ChunkedVector<ASTNode> member_args;
				callExprNode.arguments().visit([&](ASTNode argument) {
					member_args.push_back(argument);
				});

				ASTNode object_expr = ASTNode::emplace_node<ExpressionNode>(
					IdentifierNode(func_ptr_decl->identifier_token()));
				CallExprNode member_call = makeResolvedMemberCallExpr(
					object_expr,
					*operator_call,
					std::move(member_args),
					callExprNode.called_from());
				return generateMemberFunctionCallIr(member_call, context, sema_call_key);
			}
		}

			// decltype(auto) is not a valid parameter type; reject it before
			// the recursive-lambda auto&& callable fallback can mishandle it.
		if (func_type.category() == TypeCategory::DeclTypeAuto) {
			throw CompileError("'decltype(auto)' is not allowed as a parameter type");
		}

		bool is_recursive_lambda_self = false;
		if (current_lambda_context_.isActive() && func_type.category() == TypeCategory::Struct &&
			func_type.is_rvalue_reference()) {
			auto closure_type_it = getTypesByNameMap().find(current_lambda_context_.closure_type);
			if (closure_type_it != getTypesByNameMap().end() &&
				func_type.type_index() == closure_type_it->second->type_index_) {
				is_recursive_lambda_self = true;
			}
		}

			// Handle auto-typed callables (e.g., recursive lambda pattern: self(self, n-1)).
			// Once generic lambda params are normalized, the recursive `self` parameter is
			// a concrete closure-type rvalue reference rather than plain `auto`, so accept
			// that form here as well.
		if (isPlaceholderAutoType(func_type.type()) || is_recursive_lambda_self) {
				// This is likely a recursive lambda call pattern where 'self' is a lambda passed as auto&&
				// We need to find the lambda's closure type and call its operator()

				// Look up the deduced type for this auto parameter
				// First, check if we're inside a lambda context
			if (current_lambda_context_.isActive()) {
					// We're inside a lambda - this could be a recursive call through an auto&& parameter
					// The pattern is: auto factorial = [](auto&& self, int n) { ... self(self, n-1); }

					// Get the current lambda's closure type name to construct the operator() call
				std::string_view closure_type_name = StringTable::getStringView(current_lambda_context_.closure_type);

					// Generate a member function call to operator()
				TempVar ret_var = var_counter.next();
				setTempVarMetadata(ret_var, TempVarMetadata::makePRValue());

					// Build the call operands
				CallOp call_op = createCallOp(
					ret_var,
					StringHandle{},
					nativeTypeIndex(TypeCategory::Int).withCategory(TypeCategory::Int),
					SizeInBits{32},
					false,
					false);

					// Add the object (self) as the first argument (this pointer)
				call_op.args.push_back(makeTypedValue(TypeCategory::Struct, SizeInBits{64}, IrValue(StringTable::getOrInternStringHandle(func_name_view))));

					// Generate IR for the remaining arguments and collect types for mangling
				std::vector<TypeSpecifierNode> arg_types;

					// Look up the closure type to get the proper type_index
				TypeIndex closure_type_index{};
				auto it = getTypesByNameMap().find(current_lambda_context_.closure_type);
				if (it != getTypesByNameMap().end()) {
					closure_type_index = it->second->type_index_;
				}

				callExprNode.arguments().visit([&](ASTNode argument) {
						// Check if this argument is the same as the callee (recursive lambda pattern)
						// In that case, we should pass the reference directly without dereferencing
					bool is_self_arg = false;
					const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
					if (const auto* id = std::get_if<IdentifierNode>(&arg_expr)) {
						if (id->name() == func_name_view) {
							is_self_arg = true;
						}
					}

					if (is_self_arg) {
							// For the self argument in recursive lambda calls, pass the reference directly
							// Don't call visitExpressionNode which would dereference it
						call_op.args.push_back(makeTypedValue(TypeCategory::Struct, SizeInBits{64}, IrValue(StringTable::getOrInternStringHandle(func_name_view))));

							// Type for mangling is rvalue reference to closure type
						TypeSpecifierNode self_type(closure_type_index.withCategory(TypeCategory::Struct), 8, Token(), CVQualifier::None, ReferenceQualifier::None);
						self_type.set_reference_qualifier(ReferenceQualifier::RValueReference);
						arg_types.push_back(self_type);
					} else {
							// Normal argument - visit the expression
						ExprResult argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
						TypeCategory arg_type = argumentIrOperands.typeEnum();
						int arg_size = argumentIrOperands.size_in_bits.value;
						IrValue arg_value = std::visit([](auto&& arg) -> IrValue {
							using T = std::decay_t<decltype(arg)>;
							if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
										  std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
								return arg;
							} else {
								return 0ULL;
							}
						},
													   argumentIrOperands.value);
						call_op.args.push_back(makeTypedValue(arg_type, SizeInBits{static_cast<int>(arg_size)}, arg_value));

							// Type for mangling
						TypeSpecifierNode type_node(TypeIndex{}.withCategory(arg_type), arg_size, Token(), CVQualifier::None, ReferenceQualifier::None);
						arg_types.push_back(type_node);
					}
				});

					// Generate mangled name for operator() call
				TypeSpecifierNode return_type_node(TypeIndex{}.withCategory(TypeCategory::Int), 32, Token(), CVQualifier::None, ReferenceQualifier::None);
				std::string_view mangled_name = generateMangledNameForCall(
					"operator()",
					return_type_node,
					arg_types,
					false,
					closure_type_name,
					{},		// namespace_path
					!current_lambda_context_.is_mutable	// const unless mutable lambda
				);
				call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);

				ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), callExprNode.called_from()));

				return makeExprResult(nativeTypeIndex(TypeCategory::Int), SizeInBits{32}, IrOperand{ret_var}, PointerDepth{}, ValueStorage::ContainsData);
			}

				// Not inside a lambda context — this is an unresolved placeholder that
				// should have been resolved by semantic analysis or parameter normalization.
			throw InternalError("Unresolved placeholder type reached direct-call lowering outside lambda context");
		}
	}

		// Get the function declaration to extract parameter types for mangling
	std::string_view function_name = func_name_view;

		// Remap compiler builtins to their libc equivalents
		// __builtin_strlen -> strlen (libc function)
	if (func_name_view == "__builtin_strlen") {
		function_name = "strlen";
	} else if (func_name_view == "__builtin_memcmp") {
		function_name = "memcmp";
	} else if (func_name_view == "__builtin_memcpy") {
		function_name = "memcpy";
	}

	bool has_precomputed_mangled = callExprNode.has_mangled_name();
	const FunctionDeclarationNode* matched_func_decl = nullptr;

		// Helper: resolve mangled name from a matched function declaration
	auto resolveMangledName = [&](const FunctionDeclarationNode* func_decl, std::string_view struct_name = "") {
		if (!has_precomputed_mangled) {
			if (func_decl->has_mangled_name()) {
				function_name = func_decl->mangled_name();
			} else if (func_decl->linkage() != Linkage::C) {
				if (struct_name.empty()) {
					function_name = generateMangledNameForCall(*func_decl, "", current_namespace_stack_);
				} else {
						// Build namespace path from the struct's NamespaceHandle
						// so calls to member functions include the correct namespace.
						// Always recover from NamespaceHandle (not current_namespace_stack_)
						// to handle template instantiations from a different namespace context.
					std::vector<std::string> ns_path;
					if (struct_name.find("::") == std::string_view::npos) {
						auto name_handle = StringTable::getOrInternStringHandle(struct_name);
						auto type_it = getTypesByNameMap().find(name_handle);
						if (type_it != getTypesByNameMap().end()) {
							auto ns_views = buildNamespacePathFromHandle(type_it->second->namespaceHandle());
							ns_path.reserve(ns_views.size());
							for (auto sv : ns_views)
								ns_path.emplace_back(sv);
						}
					}
					function_name = generateMangledNameForCall(*func_decl, struct_name, ns_path);
				}
			}
		}
	};

	auto resolveQualifiedCallStruct = [&](std::string_view struct_part) -> const TypeInfo* {
		auto resolve_type_info = [&](StringHandle handle) -> const TypeInfo* {
			auto it = getTypesByNameMap().find(handle);
			return it != getTypesByNameMap().end() ? it->second : nullptr;
		};

		const TypeInfo* type_info = nullptr;

			// When inside a struct context, try the struct-qualified name first.
			// Template instantiation registers type aliases with the instantiated
			// struct name (e.g., "Outer$hash::MidType"), which carries the correct
			// substituted type.  The bare name (e.g., "MidType") may still point to
			// the template pattern's placeholder (e.g., TTT$hash) from parse time.
		if (current_struct_name_.isValid() && struct_part.find("::") == std::string_view::npos) {
			std::string_view alias_qualified_name = StringBuilder()
														.append(StringTable::getStringView(current_struct_name_))
														.append("::")
														.append(struct_part)
														.commit();
			type_info = resolve_type_info(StringTable::getOrInternStringHandle(alias_qualified_name));
		}

			// Fall back to the bare name
		if (!type_info) {
			type_info = resolve_type_info(StringTable::getOrInternStringHandle(struct_part));
		}

		constexpr size_t kMaxAliasDepth = 100;
		size_t alias_depth = 0;
		while (type_info && alias_depth < kMaxAliasDepth) {
				// Accept any TypeInfo that carries StructTypeInfo, regardless of type_ tag.
				// A template placeholder may be stored as Type::UserDefined yet still have
				// struct_info_ populated once instantiated.
			if (type_info->getStructInfo() != nullptr) {
				return type_info;
			}
			const TypeInfo* underlying = tryGetTypeInfo(type_info->type_index_);
			if (!underlying) {
				break;
			}
			if (underlying == type_info) {
				break;
			}
			type_info = underlying;
			++alias_depth;
		}

		return nullptr;
	};

	auto buildNamespaceStack = [](std::string_view qualified_name, std::vector<std::string>& out) {
		size_t ns_end = qualified_name.rfind("::");
		if (ns_end == std::string_view::npos)
			return;
		std::string_view ns_part = qualified_name.substr(0, ns_end);
		size_t start = 0;
		while (start < ns_part.size()) {
			size_t pos = ns_part.find("::", start);
			if (pos == std::string_view::npos) {
				out.emplace_back(ns_part.substr(start));
				break;
			}
			out.emplace_back(ns_part.substr(start, pos - start));
			start = pos + 2;
		}
	};

	auto queueDeferredMemberFunctions = [&](StringHandle struct_name,
											 const StructTypeInfo* struct_info,
											 std::string_view preferred_qualified_name) {
		if (!struct_name.isValid() || !struct_info) {
			return;
		}
		std::vector<std::string> ns_stack;
		buildNamespaceStack(preferred_qualified_name, ns_stack);
		if (ns_stack.empty()) {
			buildNamespaceStack(StringTable::getStringView(struct_name), ns_stack);
		}
		for (const auto& member_func : struct_info->member_functions) {
			DeferredMemberFunctionInfo deferred_info;
			deferred_info.struct_name = struct_name;
			deferred_info.function_node = member_func.function_decl;
			deferred_info.namespace_stack = ns_stack;
			deferred_member_functions_.push_back(std::move(deferred_info));
		}
	};

	auto instantiateAndQueueLazyMember = [&](StringHandle struct_name_handle,
											   StringHandle member_handle,
											   std::string_view qualified_name_for_ns,
											   size_t expected_param_count) -> const FunctionDeclarationNode* {
		if (!parser_ || !struct_name_handle.isValid() || !member_handle.isValid()) {
			return nullptr;
		}
		if (!LazyMemberInstantiationRegistry::getInstance().needsInstantiationAny(struct_name_handle, member_handle)) {
			return nullptr;
		}
		auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfoAny(struct_name_handle, member_handle);
		if (!lazy_info_opt.has_value()) {
			return nullptr;
		}
		auto instantiated_func = parser_->instantiateLazyMemberFunction(*lazy_info_opt);
		LazyMemberInstantiationRegistry::getInstance().markInstantiated(struct_name_handle, member_handle, lazy_info_opt->identity.is_const_method);
		if (!instantiated_func.has_value() || !instantiated_func->is<FunctionDeclarationNode>()) {
			return nullptr;
		}
		const FunctionDeclarationNode& fd = instantiated_func->as<FunctionDeclarationNode>();
		if (fd.parameter_nodes().size() != expected_param_count) {
			return nullptr;
		}

		DeferredMemberFunctionInfo deferred_info;
		deferred_info.struct_name = struct_name_handle;
		deferred_info.function_node = *instantiated_func;
		buildNamespaceStack(qualified_name_for_ns, deferred_info.namespace_stack);
		if (deferred_info.namespace_stack.empty()) {
			buildNamespaceStack(StringTable::getStringView(struct_name_handle), deferred_info.namespace_stack);
		}
		deferred_member_functions_.push_back(std::move(deferred_info));
		return &fd;
	};

	auto consumeResolvedDirectCallTarget = [&](const FunctionDeclarationNode* resolved_target, std::string_view source_label) {
		if (!resolved_target || matched_func_decl) {
			return;
		}
		std::string_view parent = resolved_target->parent_struct_name();
		const bool is_current_struct_or_unowned = parent.empty() ||
			(current_struct_name_.isValid() && StringTable::getStringView(current_struct_name_) == parent);
		if (is_current_struct_or_unowned) {
			matched_func_decl = resolved_target;
			resolveMangledName(matched_func_decl, parent);
			FLASH_LOG_FORMAT(Codegen, Debug, "Using {} direct call target for: {}", source_label, func_name_view);
			return;
		}

		StringHandle owner_handle;
		std::string_view resolved_owner_name = parent;
		const StructTypeInfo* owner_struct_info = nullptr;

		{
			StringHandle parent_handle = StringTable::getOrInternStringHandle(parent);
			auto owner_it = getTypesByNameMap().find(parent_handle);
			if (owner_it != getTypesByNameMap().end() && owner_it->second->isStruct()) {
				owner_handle = owner_it->second->name();
				resolved_owner_name = StringTable::getStringView(owner_handle);
				owner_struct_info = owner_it->second->getStructInfo();
			}
		}

		if (!owner_handle.isValid()) {
			return;
		}

		matched_func_decl = resolved_target;
		std::string_view owner_for_mangling = parent;
		if (gTemplateRegistry.isPatternStructName(StringTable::getOrInternStringHandle(owner_for_mangling))) {
			owner_for_mangling = resolved_owner_name;
		}
		resolveMangledName(matched_func_decl, owner_for_mangling);
		if (owner_struct_info && !owner_struct_info->member_functions.empty()) {
			queueDeferredMemberFunctions(owner_handle, owner_struct_info, owner_for_mangling);
		} else if (const FunctionDeclarationNode* instantiated_func =
			instantiateAndQueueLazyMember(owner_handle,
				matched_func_decl->decl_node().identifier_token().handle(),
				resolved_owner_name,
				callExprNode.arguments().size())) {
			matched_func_decl = instantiated_func;
			resolveMangledName(matched_func_decl, resolved_owner_name);
		}
		FLASH_LOG_FORMAT(Codegen, Debug, "Using {} cross-struct direct call target for: {}", source_label, func_name_view);
	};

	// Phase 1 (sema-owned ordinary call resolution): consume the pre-resolved
	// direct-call target stored by semantic analysis before attempting any
	// duplicate symbol-table recovery work in codegen.
	if (!matched_func_decl && sema_ && !has_precomputed_mangled && !callExprNode.has_qualified_name()) {
		consumeResolvedDirectCallTarget(sema_->getResolvedDirectCall(sema_call_key), "sema-resolved");
	}

	// Check if the call expression has a pre-computed mangled name (for namespace-scoped functions)
	// If so, use it directly and skip the lookup logic
	if (has_precomputed_mangled) {
		function_name = callExprNode.mangled_name();
		FLASH_LOG_FORMAT(Codegen, Debug, "Using pre-computed mangled name from call expression: {}", function_name);
		// We don't need to find matched_func_decl since we already have the mangled name
		// The mangled name is sufficient for generating the call instruction
	}

	// Keep lookup recovery only for call shapes that the direct-call sema cache does not
	// fully own yet:
	// - no sema or no tracked normalized body,
	// - precomputed mangled / qualified calls that bypass the ordinary-call cache,
	// - static-member lowering that still rewrites through the direct-call path,
	// - and explicit sema unresolved-call escape hatches.
	const bool allow_lookup_recovery =
		!sema_ || // no semantic data wired into codegen
		!sema_normalized_current_function_ || // body not tracked by normalized_bodies_
		has_precomputed_mangled || // namespace/qualified/static cache path
		callExprNode.callee().is_static_member() || // member call rewritten through direct-call lowering
		(callExprNode.callee().has_function_declaration() &&
		 callExprNode.callee().function_declaration_or_null()->is_static()) || // static member metadata on full declaration
		callExprNode.has_qualified_name() || // qualified lookup is not owned by resolved_direct_call_table_
		sema_->hasUnresolvedCallArgs(sema_call_key); // sema recorded a known resolution gap

	// For sema-normalized ordinary direct calls, lowering must consume the sema-owned
	// callee selection instead of rescanning symbol tables and member hierarchies again.
	if (!matched_func_decl && allow_lookup_recovery) {
		// Look up the function in the global symbol table to get all overloads
		// Use global_symbol_table_ if available, otherwise fall back to local symbol_table
		auto scoped_overloads = global_symbol_table_
									? global_symbol_table_->lookup_all(decl_node.identifier_token().value())
									: symbol_table.lookup_all(decl_node.identifier_token().value());

		// Also try looking up in gSymbolTable directly for comparison
		extern SymbolTable gSymbolTable;
		auto gSymbolTable_overloads = gSymbolTable.lookup_all(decl_node.identifier_token().value());

		// Find the matching overload by comparing the DeclarationNode address
		// This works because the call expression holds a reference to the specific
		// DeclarationNode that was selected by overload resolution
		FLASH_LOG_FORMAT(Codegen, Debug, "Looking for function: {}, all_overloads size: {}, gSymbolTable_overloads size: {}",
						 lookup_name_view, scoped_overloads.size(), gSymbolTable_overloads.size());
		for (const auto& overload : scoped_overloads) {
			const FunctionDeclarationNode* overload_func_decl = nullptr;
			if (overload.is<FunctionDeclarationNode>()) {
				overload_func_decl = &overload.as<FunctionDeclarationNode>();
			} else if (overload.is<TemplateFunctionDeclarationNode>()) {
				overload_func_decl = &overload.as<TemplateFunctionDeclarationNode>().function_decl_node();
			}

			if (overload_func_decl) {
				const DeclarationNode* overload_decl = &overload_func_decl->decl_node();
				FLASH_LOG_FORMAT(Codegen, Debug, "  Checking overload at {}, looking for {}",
								 (void*)overload_decl, (void*)&decl_node);
				if (overload_decl == &decl_node ||
					(has_precomputed_mangled && overload_func_decl->has_mangled_name() &&
					 overload_func_decl->mangled_name() == callExprNode.mangled_name())) {
						// Found the matching overload
					matched_func_decl = overload_func_decl;
					resolveMangledName(matched_func_decl);
					FLASH_LOG_FORMAT(Codegen, Debug, "Matched overload, function_name: {}", function_name);
					break;
				}
			}
		}

		// For instantiated template calls, the concrete specialization is registered under its
		// mangled name in the symbol table. Prefer that over falling back to the template pattern.
	if (!matched_func_decl && has_precomputed_mangled) {
		auto mangled_symbol = lookupSymbol(callExprNode.mangled_name());
		if (mangled_symbol.has_value()) {
			if (mangled_symbol->is<FunctionDeclarationNode>()) {
				matched_func_decl = &mangled_symbol->as<FunctionDeclarationNode>();
				resolveMangledName(matched_func_decl);
				FLASH_LOG_FORMAT(Codegen, Debug, "Matched function by mangled symbol lookup: {}", function_name);
			} else if (mangled_symbol->is<TemplateFunctionDeclarationNode>()) {
				matched_func_decl = &mangled_symbol->as<TemplateFunctionDeclarationNode>().function_decl_node();
				resolveMangledName(matched_func_decl);
				FLASH_LOG_FORMAT(Codegen, Debug, "Matched template function by mangled symbol lookup: {}", function_name);
			}
		}
	}

	// Remap stale pattern-owner manglings when an unqualified member call is being lowered
	// inside an instantiated template class body.
	if (!matched_func_decl && has_precomputed_mangled && current_struct_name_.isValid() &&
		!callExprNode.has_qualified_name()) {
		auto current_struct_it = getTypesByNameMap().find(current_struct_name_);
		if (current_struct_it != getTypesByNameMap().end() && current_struct_it->second->isStruct()) {
			const StructTypeInfo* struct_info = current_struct_it->second->getStructInfo();
			const size_t expected_param_count = callExprNode.arguments().size();
			auto findMemberInHierarchy = [&](auto&& self, const StructTypeInfo* current_struct) -> const FunctionDeclarationNode* {
				if (!current_struct) {
					return nullptr;
				}

				for (const auto& member_func : current_struct->member_functions) {
					if (!member_func.function_decl.is<FunctionDeclarationNode>()) {
						continue;
					}

					const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
					if (func_decl.decl_node().identifier_token().value() == func_name_view &&
						func_decl.parameter_nodes().size() == expected_param_count) {
						return &func_decl;
					}
				}

				for (const auto& base_spec : current_struct->base_classes) {
					if (const TypeInfo* base_type_info = tryGetTypeInfo(base_spec.type_index)) {
						if (const StructTypeInfo* base_struct_info = base_type_info->getStructInfo()) {
							if (const FunctionDeclarationNode* base_match = self(self, base_struct_info)) {
								return base_match;
							}
						}
					}
				}

				return nullptr;
			};

			if (const FunctionDeclarationNode* instantiated_member = findMemberInHierarchy(findMemberInHierarchy, struct_info)) {
				matched_func_decl = instantiated_member;
				has_precomputed_mangled = false;
				resolveMangledName(matched_func_decl, StringTable::getStringView(current_struct_name_));
				FLASH_LOG_FORMAT(Codegen, Debug, "Remapped stale precomputed member call {} to {}", func_name_view, function_name);
			}
		}
	}

		// Fallback: if pointer comparison failed (e.g., for template instantiations),
		// try to find the function by checking if there's only one overload with this name
	if (!matched_func_decl && !has_precomputed_mangled && scoped_overloads.size() == 1 &&
		(scoped_overloads[0].is<FunctionDeclarationNode>() || scoped_overloads[0].is<TemplateFunctionDeclarationNode>())) {
		matched_func_decl = scoped_overloads[0].is<FunctionDeclarationNode>()
								? &scoped_overloads[0].as<FunctionDeclarationNode>()
								: &scoped_overloads[0].as<TemplateFunctionDeclarationNode>().function_decl_node();

		resolveMangledName(matched_func_decl);
	}

		// Additional fallback: check gSymbolTable directly (for member functions added during delayed parsing)
	if (!matched_func_decl && !has_precomputed_mangled && gSymbolTable_overloads.size() == 1 &&
		(gSymbolTable_overloads[0].is<FunctionDeclarationNode>() || gSymbolTable_overloads[0].is<TemplateFunctionDeclarationNode>())) {
		matched_func_decl = gSymbolTable_overloads[0].is<FunctionDeclarationNode>()
								? &gSymbolTable_overloads[0].as<FunctionDeclarationNode>()
								: &gSymbolTable_overloads[0].as<TemplateFunctionDeclarationNode>().function_decl_node();

		resolveMangledName(matched_func_decl);
	}

		// Defensive fallback for precomputed-mangled calls: if all lookups above failed to populate
		// matched_func_decl (e.g., address comparison failed among multiple overloads), scan
		// gSymbolTable by unqualified name and match on DeclarationNode pointer equality.
		// Without this, a consteval function with a precomputed mangled name could bypass the
		// consteval enforcement check below (C++20 [dcl.consteval]).
	if (!matched_func_decl && has_precomputed_mangled) {
		for (const auto& overload : gSymbolTable_overloads) {
			const FunctionDeclarationNode* candidate = nullptr;
			if (overload.is<FunctionDeclarationNode>())
				candidate = &overload.as<FunctionDeclarationNode>();
			else if (overload.is<TemplateFunctionDeclarationNode>())
				candidate = &overload.as<TemplateFunctionDeclarationNode>().function_decl_node();
			if (candidate && &candidate->decl_node() == &decl_node) {
				matched_func_decl = candidate;
				resolveMangledName(matched_func_decl);
				FLASH_LOG_FORMAT(Codegen, Debug, "Matched function via gSymbolTable pointer scan (precomputed mangled): {}", function_name);
				break;
			}
		}
	}

		// Final fallback: if we're in a member function, check the current struct's member functions
	if (!matched_func_decl && current_struct_name_.isValid() && !callExprNode.has_qualified_name()) {
		auto type_it = getTypesByNameMap().find(current_struct_name_);
		if (type_it != getTypesByNameMap().end() && type_it->second->isStruct()) {
			const StructTypeInfo* struct_info = type_it->second->getStructInfo();
			if (struct_info) {
				for (const auto& member_func : struct_info->member_functions) {
					if (member_func.function_decl.is<FunctionDeclarationNode>()) {
						const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
						if (func_decl.decl_node().identifier_token().value() == func_name_view) {
								// Found matching member function
							matched_func_decl = &func_decl;
							resolveMangledName(matched_func_decl, StringTable::getStringView(current_struct_name_));
							break;
						}
					}
				}
			}

				// If not found in current struct, check base classes
			if (!matched_func_decl && struct_info) {
					// Search through base classes recursively
				std::function<void(const StructTypeInfo*)> searchBaseClasses = [&](const StructTypeInfo* current_struct) {
					for (const auto& base_spec : current_struct->base_classes) {
							// Look up base class in gTypeInfo
						if (const TypeInfo* base_type_info = tryGetTypeInfo(base_spec.type_index)) {
							if (base_type_info->isStruct()) {
								const StructTypeInfo* base_struct_info = base_type_info->getStructInfo();
								if (base_struct_info) {
										// Check member functions in base class
									for (const auto& member_func : base_struct_info->member_functions) {
										if (member_func.function_decl.is<FunctionDeclarationNode>()) {
											const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
											if (func_decl.decl_node().identifier_token().value() == func_name_view) {
													// Found matching member function in base class
												matched_func_decl = &func_decl;
												resolveMangledName(matched_func_decl, StringTable::getStringView(base_struct_info->getName()));
												return; // Stop searching once found
											}
										}
									}
										// Recursively search base classes of this base class
									if (!matched_func_decl) {
										searchBaseClasses(base_struct_info);
									}
								}
							}
						}
					}
				};
				searchBaseClasses(struct_info);
			}
		}
	}

		// Fallback: if the function is a qualified static member call (ClassName::method),
		// look up the struct by iterating over known types and matching the function.
		// Note: We match by function name AND parameter count to avoid false positives
		// from identically named functions on different structs.
	if (!matched_func_decl && !has_precomputed_mangled && !callExprNode.has_qualified_name()) {
		size_t expected_param_count = 0;
		callExprNode.arguments().visit([&](ASTNode) { ++expected_param_count; });

		for (const auto& [name_handle, type_info_ptr] : getTypesByNameMap()) {
			if (!type_info_ptr->isStruct())
				continue;
			const StructTypeInfo* struct_info = type_info_ptr->getStructInfo();
			if (!struct_info)
				continue;
				// Skip pattern structs (templates) - they shouldn't be used for code generation
			if (gTemplateRegistry.isPatternStructName(name_handle))
				continue;
			if (type_info_ptr->is_incomplete_instantiation_)
				continue;
				// Skip uninstantiated class template patterns — if the struct was registered
				// as a class template but is NOT a template instantiation, it is an
				// uninstantiated pattern and must not be used for codegen.
				// Template instantiations (isTemplateInstantiation) are concrete types
				// and should NOT be skipped.
			if (!type_info_ptr->isTemplateInstantiation()) {
				if (gTemplateRegistry.isClassTemplate(name_handle)) {
					continue;
				}
			}

			std::string_view struct_type_name = StringTable::getStringView(name_handle);
			for (const auto& member_func : struct_info->member_functions) {
				if (member_func.function_decl.is<FunctionDeclarationNode>()) {
					const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
					if (func_decl.decl_node().identifier_token().value() == func_name_view && func_decl.parameter_nodes().size() == expected_param_count) {
						matched_func_decl = &func_decl;
							// Use the struct type name for mangling (not parent_struct_name which
							// may reference a template pattern)
						std::string_view parent_for_mangling = func_decl.parent_struct_name();
						if (gTemplateRegistry.isPatternStructName(StringTable::getOrInternStringHandle(parent_for_mangling))) {
							parent_for_mangling = struct_type_name;
						}
						resolveMangledName(matched_func_decl, parent_for_mangling);
						FLASH_LOG_FORMAT(Codegen, Debug, "Resolved static member function via struct search: {} -> {}", func_name_view, function_name);

							// Queue all member functions of this struct for deferred generation
							// since the matched function may call other members (e.g., lowest() calls min()).
							// Derive namespace from the matched function's parent struct first (authoritative),
							// then fall back to the resolved type name when needed.
						queueDeferredMemberFunctions(type_info_ptr->name(), struct_info, parent_for_mangling);

						break;
					}
				}
			}
			if (matched_func_decl)
				break;
		}
	}

		// Handle dependent qualified function names: Base$dependentHash::member
		// These occur when a template body contains Base<T>::member() and T is substituted
		// but the hash was computed with the dependent type, not the concrete type.
	if (!matched_func_decl) {
		size_t scope_pos = lookup_name_view.find("::");
		std::string_view base_template_name;
		if (scope_pos != std::string_view::npos) {
			base_template_name = extractBaseTemplateName(lookup_name_view.substr(0, scope_pos));
		}
			// Direct lookup: if the struct qualifier is directly in getTypesByNameMap() (e.g., "Mid$hash::get"),
			// find it immediately rather than only checking base classes.
		if (scope_pos != std::string_view::npos && !matched_func_decl) {
			std::string_view struct_part = lookup_name_view.substr(0, scope_pos);
			std::string_view member_name_direct = lookup_name_view.substr(scope_pos + 2);
			const TypeInfo* direct_type_info = resolveQualifiedCallStruct(struct_part);
			if (direct_type_info && direct_type_info->isStruct()) {
				const StructTypeInfo* si = direct_type_info->getStructInfo();
				if (si) {
					std::string_view resolved_struct_part = StringTable::getStringView(direct_type_info->name());
						// Count expected parameters for overload disambiguation
					size_t direct_expected_param_count = 0;
					callExprNode.arguments().visit([&](ASTNode) { ++direct_expected_param_count; });
					for (const auto& mf : si->member_functions) {
						if (mf.function_decl.is<FunctionDeclarationNode>()) {
							const auto& fd = mf.function_decl.as<FunctionDeclarationNode>();
							if (fd.decl_node().identifier_token().value() == member_name_direct && fd.parameter_nodes().size() == direct_expected_param_count) {
								matched_func_decl = &fd;
								resolveMangledName(matched_func_decl, resolved_struct_part);
									// Queue all member functions of this struct for deferred generation
								queueDeferredMemberFunctions(direct_type_info->name(), si, resolved_struct_part);
								break;
							}
						}
					}
						// If member_functions is empty (lazy-instantiated template), check
						// LazyMemberInstantiationRegistry and trigger instantiation now.
					if (!matched_func_decl) {
						StringHandle struct_name_handle = direct_type_info->name();
						StringHandle member_handle = StringTable::getOrInternStringHandle(member_name_direct);
						if (const FunctionDeclarationNode* instantiated_func =
								instantiateAndQueueLazyMember(struct_name_handle, member_handle, resolved_struct_part, direct_expected_param_count)) {
							matched_func_decl = instantiated_func;
							resolveMangledName(matched_func_decl, resolved_struct_part);
							FLASH_LOG_FORMAT(Codegen, Debug, "Resolved lazy member '{}::{}' via LazyMemberInstantiationRegistry -> {}", resolved_struct_part, member_name_direct, function_name);
						}
					}
				}
			}
		}
		if (!matched_func_decl && !base_template_name.empty() && scope_pos != std::string_view::npos) {
			std::string_view member_name = lookup_name_view.substr(scope_pos + 2);

			FLASH_LOG_FORMAT(Codegen, Debug, "Resolving dependent qualified call: base_template='{}', member='{}'", base_template_name, member_name);

				// Search current struct's base classes for a matching template instantiation
			if (current_struct_name_.isValid()) {
				auto type_it = getTypesByNameMap().find(current_struct_name_);
				if (type_it != getTypesByNameMap().end() && type_it->second->isStruct()) {
					const StructTypeInfo* curr_struct = type_it->second->getStructInfo();
					if (curr_struct) {
						for (const auto& base_spec : curr_struct->base_classes) {
							if (const TypeInfo* base_type_info = tryGetTypeInfo(base_spec.type_index)) {
								if (base_type_info->isTemplateInstantiation() &&
									StringTable::getStringView(base_type_info->baseTemplateName()) == base_template_name &&
									base_type_info->isStruct()) {
									const StructTypeInfo* base_struct_info = base_type_info->getStructInfo();
									if (base_struct_info) {
										for (const auto& member_func : base_struct_info->member_functions) {
											if (member_func.function_decl.is<FunctionDeclarationNode>()) {
												const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
												std::string_view func_id = func_decl.decl_node().identifier_token().value();
												if (func_id == member_name) {
													matched_func_decl = &func_decl;
													resolveMangledName(matched_func_decl, StringTable::getStringView(base_struct_info->getName()));
													break;
												}
											}
										}
										if (matched_func_decl)
											break;
									}
								}
							}
						}
					}
				}
			}
		}
		}
	}

	if (!matched_func_decl && !allow_lookup_recovery) {
		throw InternalError(std::string(
			StringBuilder()
				.append("Phase 1: sema-normalized direct call missing resolved target for '")
				.append(func_name_view)
				.append("'")
				.commit()));
	}

		// consteval enforcement: every call to a consteval function is an immediate invocation and
		// must be a constant expression (C++20 [dcl.consteval]).  generateFunctionCallIr is only
		// reached when emitting runtime IR, so we first try to fold the call at compile time.
		// If the fold succeeds we return the constant directly (no runtime call emitted).
		// If it fails the call is genuinely ill-formed — throw a CompileError.
		// Note: when has_precomputed_mangled is true, matched_func_decl may be null if all
		// symbol-table lookups failed.  In that case the call will be emitted as a runtime call,
		// which is safe for constexpr functions; consteval functions are checked below.
	if (matched_func_decl && matched_func_decl->is_consteval()) {
			// Use the global symbol table so free functions declared at namespace scope can be found.
		extern SymbolTable gSymbolTable;
		ConstExpr::EvaluationContext ctx(global_symbol_table_ ? *global_symbol_table_ : gSymbolTable);
		ctx.global_symbols = global_symbol_table_ ? global_symbol_table_ : &gSymbolTable;
		ctx.parser = parser_;
		auto eval_call_node = ASTNode::emplace_node<ExpressionNode>(callExprNode);
		auto eval_result = ConstExpr::Evaluator::evaluate(eval_call_node, ctx);
		if (!eval_result.success()) {
			throw CompileError("call to consteval function '" + std::string(func_name_view) +
							   "' cannot be used in a non-constant context: " + eval_result.error_message);
		}
			// Materialize the constant result into an ExprResult without emitting a call instruction.
		const TypeSpecifierNode& ret_spec =
			matched_func_decl->decl_node().type_node().as<TypeSpecifierNode>();
		const TypeCategory ret_type = ret_spec.type();
		const int ret_bits_raw = static_cast<int>(ret_spec.size_in_bits());
		const SizeInBits ret_size{ret_bits_raw != 0 ? ret_bits_raw : static_cast<int>(get_type_size_bits(ret_type))};

			// Float / double
		if (ret_type == TypeCategory::Float) {
			float fval = static_cast<float>(eval_result.as_double());
			uint32_t fbits;
			std::memcpy(&fbits, &fval, sizeof(float));
			return makeExprResult(nativeTypeIndex(ret_type), SizeInBits{32}, IrOperand{static_cast<unsigned long long>(fbits)}, PointerDepth{}, ValueStorage::ContainsData);
		}
		if (ret_type == TypeCategory::Double || ret_type == TypeCategory::LongDouble) {
			double dval = eval_result.as_double();
			unsigned long long dbits;
			std::memcpy(&dbits, &dval, sizeof(double));
			return makeExprResult(nativeTypeIndex(ret_type), SizeInBits{64}, IrOperand{dbits}, PointerDepth{}, ValueStorage::ContainsData);
		}

			// Aggregate / struct: emit VariableDecl + MemberStore sequence
		if (!eval_result.object_member_bindings.empty()) {
			auto agg = materializeConstevalAggregateResult(
				eval_result, ret_spec, ret_type, ret_size, callExprNode.called_from());
			if (agg.category() != TypeCategory::Void)
				return agg;
		}

			// Scalar integer / bool / enum
		return makeExprResult(nativeTypeIndex(ret_type), ret_size, IrOperand{evalResultScalarToRaw(eval_result)}, PointerDepth{}, ValueStorage::ContainsData);
	}

		// Always add the return variable and function name (mangled for overload resolution)
	FLASH_LOG_FORMAT(Codegen, Debug, "Final function_name for call: '{}'", function_name);
	TempVar ret_var = var_counter.next();

		// Mark function return value as prvalue (Option 2: Value Category Tracking)
		// Function returns (by value) produce temporaries with no persistent identity
	setTempVarMetadata(ret_var, TempVarMetadata::makePRValue());

	irOperands.emplace_back(ret_var);
	irOperands.emplace_back(StringTable::getOrInternStringHandle(function_name));

	const std::vector<CachedParamInfo>* cached_param_list = nullptr;
	{
		StringHandle cache_key = callExprNode.has_mangled_name()
									 ? callExprNode.mangled_name_handle()
									 : StringTable::getOrInternStringHandle(function_name);
		auto cache_it = function_param_cache_.find(cache_key);
		if (cache_it != function_param_cache_.end()) {
			cached_param_list = &cache_it->second;
		}
	}

		// Process arguments - match them with parameter types
	size_t arg_index = 0;
	const auto& func_decl_node = callExprNode.callee().declaration();

		// Get parameters from the function declaration
	std::vector<ASTNode> param_nodes;
	if (matched_func_decl) {
		param_nodes = matched_func_decl->parameter_nodes();
	} else if (!has_precomputed_mangled) {
			// Try to get it from the function declaration stored in the call expression
			// Look up the function in symbol table to get full declaration with parameters
		auto local_func_symbol = lookupSymbol(func_decl_node.identifier_token().value());
		if (local_func_symbol.has_value() && local_func_symbol->is<FunctionDeclarationNode>()) {
			const auto& resolved_func_decl = local_func_symbol->as<FunctionDeclarationNode>();
			param_nodes = resolved_func_decl.parameter_nodes();
		}
	}

	callExprNode.arguments().visit([&](ASTNode argument) {
			// Get the parameter type for this argument (if it exists)
		const TypeSpecifierNode* param_type = nullptr;
		const DeclarationNode* param_decl = nullptr;
		if (arg_index < param_nodes.size() && param_nodes[arg_index].is<DeclarationNode>()) {
			param_decl = &param_nodes[arg_index].as<DeclarationNode>();
		} else if (!param_nodes.empty() && param_nodes.back().is<DeclarationNode>()) {
			const auto& last_param = param_nodes.back().as<DeclarationNode>();
			if (last_param.is_parameter_pack()) {
				param_decl = &last_param;
			}
		}
		if (param_decl)
			param_type = &param_decl->type_node().as<TypeSpecifierNode>();

		const CachedParamInfo* cached_param = nullptr;
		if (cached_param_list && !cached_param_list->empty()) {
			if (arg_index < cached_param_list->size()) {
				cached_param = &(*cached_param_list)[arg_index];
			} else if (cached_param_list->back().is_parameter_pack) {
				cached_param = &cached_param_list->back();
			}
		}

		CVReferenceQualifier param_ref_qualifier = CVReferenceQualifier::None;
		[[maybe_unused]] bool param_is_pack = param_decl && param_decl->is_parameter_pack();
		if (param_type) {
			param_ref_qualifier = param_type->is_rvalue_reference()
									  ? CVReferenceQualifier::RValueReference
									  : (param_type->is_reference() ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None);
		} else if (cached_param) {
			param_ref_qualifier = cached_param->ref_qualifier;
			param_is_pack = cached_param->is_parameter_pack;
		}
		const CallArgReferenceBindingInfo* sema_ref_binding = nullptr;
		if (param_type && sema_) {
			sema_ref_binding = sema_->getCallRefBinding(
				sema_call_key,
				arg_index);
		}

			// Special case: if argument is a reference identifier being passed to a reference parameter,
			// handle it directly without visiting the expression. This prevents the Load context from
			// generating a Dereference operation (which would give us the value, not the address).
			// For reference-to-reference passing, we just want to pass the variable name directly,
			// and let the IRConverter use MOV to load the address stored in the reference.
		if ((!sema_ref_binding || !sema_ref_binding->is_valid()) &&
			param_ref_qualifier != CVReferenceQualifier::None &&
			std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
			const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
			const DeclarationNode* decl_ptr = lookupDeclaration(identifier.name());
			if (decl_ptr) {
				const auto& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
				if (type_node.is_reference() || type_node.is_rvalue_reference()) {
						// Argument is a reference variable being passed to a reference parameter
						// Pass the identifier name directly - the IRConverter will use MOV to
						// load the address stored in the reference variable
					irOperands.emplace_back(type_node.type());
					irOperands.emplace_back(64);	 // References are stored as 64-bit pointers
					irOperands.emplace_back(StringTable::getOrInternStringHandle(identifier.name()));
					arg_index++;
					return;	// Skip the rest of the processing
				}
			}
		}

			// Determine expression context for the argument
			// Default to Load context, which reads values
		ExpressionContext arg_context = ExpressionContext::Load;

			// If the parameter expects a reference, use LValueAddress context to avoid dereferencing
			// This is needed for non-reference arguments being passed to reference parameters
		if (param_ref_qualifier != CVReferenceQualifier::None &&
			(!sema_ref_binding || !sema_ref_binding->is_valid() || sema_ref_binding->binds_directly())) {
			arg_context = ExpressionContext::LValueAddress;
		}

		ExprResult argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>(), arg_context);
		arg_index++;

		if (param_type && sema_ref_binding && sema_ref_binding->is_valid()) {
			if (auto sema_bound_arg = tryApplySemaCallArgReferenceBinding(
					argumentIrOperands, argument, *param_type, sema_ref_binding, callExprNode.called_from())) {
				appendArgumentIrResult(*sema_bound_arg);
				return;
			}
		}

		bool materialized_selected_ctor = false;
		if (param_type) {
			if (auto materialized = tryMaterializeSemaSelectedConvertingConstructor(
					argumentIrOperands, argument, *param_type, callExprNode.called_from())) {
				argumentIrOperands = *materialized;
				materialized_selected_ctor = true;
			}
		}

			// Check if we need to call a conversion operator for this argument
			// This handles cases like: func(myStruct) where func expects int and myStruct has operator int()
		if (param_type && !materialized_selected_ctor) {
			TypeCategory arg_type = argumentIrOperands.typeEnum();
			TypeCategory param_base_type = param_type->type();

			TypeIndex arg_type_index = argumentIrOperands.type_index;

				// Check sema annotation first: if the semantic pass pre-computed a conversion, use it.
			bool sema_applied_arg_conversion = false;
			if (sema_ && argument.is<ExpressionNode>()) {
				const void* key = &argument.as<ExpressionNode>();
				const auto slot = sema_->getSlot(key);
				if (slot.has_value() && slot->has_cast()) {
					const ImplicitCastInfo& cast_info =
						sema_->castInfoTable()[slot->cast_info_index.value - 1];
					TypeCategory from_type = sema_->typeContext().get(cast_info.source_type_id).category();
					const TypeCategory to_type = sema_->typeContext().get(cast_info.target_type_id).category();
					if (cast_info.cast_kind == StandardConversionKind::UserDefined &&
						from_type == TypeCategory::Struct) {
							// Sema annotated a user-defined conversion operator call
						TypeIndex source_type_idx = sema_->typeContext().get(cast_info.source_type_id).type_index;
						if (const TypeInfo* src_type_info = tryGetTypeInfo(source_type_idx)) {
							const bool source_is_const = ((static_cast<uint8_t>(sema_->typeContext().get(cast_info.source_type_id).base_cv)) & (static_cast<uint8_t>(CVQualifier::Const))) != 0;
							const StructMemberFunction* conv_op = findConversionOperator(
								src_type_info->getStructInfo(), param_type->type_index(), source_is_const);
							if (conv_op) {
								FLASH_LOG(Codegen, Debug, "Sema-annotated user-defined conversion in function arg from ",
										  StringTable::getStringView(src_type_info->name()), " to parameter type");
								const int param_size = static_cast<int>(param_type->size_in_bits());
								if (auto result = emitConversionOperatorCall(argumentIrOperands, *src_type_info, *conv_op,
																			 param_type->type_index(), param_size,
																			 callExprNode.called_from())) {
									argumentIrOperands = *result;
									arg_type = argumentIrOperands.typeEnum();
									arg_type_index = argumentIrOperands.type_index;
									sema_applied_arg_conversion = true;
								}
							}
						}
					} else if (!is_struct_type(from_type) && !is_struct_type(to_type)) {
							// Sema may annotate as Type::Enum while codegen resolves enum
							// constants to their underlying type; use actual runtime type.
						if (from_type == TypeCategory::Enum && from_type != argumentIrOperands.typeEnum())
							from_type = argumentIrOperands.typeEnum();
						argumentIrOperands = generateTypeConversion(argumentIrOperands, from_type, to_type, callExprNode.called_from());
						arg_type = argumentIrOperands.typeEnum();
						arg_type_index = argumentIrOperands.type_index;
						sema_applied_arg_conversion = true;
					}
				}
			}

				// Phase 15: sema should annotate all standard primitive argument conversions.
				// Non-arithmetic types (struct, user_defined, enum, auto, function_pointer)
				// are outside sema's current scope — keep fallback unconditionally.
				// For arithmetic types, assert when sema missed the annotation.
				// Exception: hasUnresolvedCallArgs means sema tried but couldn't resolve the callee
				// (e.g. template specialization) — Phase 16+ work item.
			if (!sema_applied_arg_conversion &&
				param_ref_qualifier == CVReferenceQualifier::None &&
				param_type->pointer_depth() == 0 &&
				arg_type != param_base_type) {
				TypeConversionResult standard_conversion = can_convert_type(arg_type, param_base_type);
				if (standard_conversion.is_valid &&
					standard_conversion.rank != ConversionRank::UserDefined) {
					if (sema_normalized_current_function_ &&
						is_standard_arithmetic_type(arg_type) && is_standard_arithmetic_type(param_base_type) &&
						!(sema_ &&
						  sema_->hasUnresolvedCallArgs(sema_call_key))) {
						throw InternalError(std::string("Phase 15: sema missed function call argument conversion (") + std::string(getTypeName(arg_type)) + " -> " + std::string(getTypeName(param_base_type)) + ")");
					}
					argumentIrOperands = generateTypeConversion(argumentIrOperands, arg_type, param_base_type, callExprNode.called_from());
					arg_type = argumentIrOperands.typeEnum();
					arg_type_index = argumentIrOperands.type_index;
				}
			}

				// Check if argument type doesn't match parameter type and parameter expects struct
				// This handles implicit conversions via converting constructors
			if (arg_type != param_base_type && param_base_type == TypeCategory::Struct && param_type->pointer_depth() == 0) {
				TypeIndex param_type_index = param_type->type_index();

				if (const TypeInfo* target_type_info = tryGetTypeInfo(param_type_index)) {
					const StructTypeInfo* target_struct_info = target_type_info->getStructInfo();

						// Look for a converting constructor that takes the argument type
					if (target_struct_info) {
						bool found_matching_ctor = false;
						bool found_non_explicit_ctor = false;
						for (const auto& func : target_struct_info->member_functions) {
							if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
								const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
								const auto& params = ctor_node.parameter_nodes();

									// Check for single-parameter constructor (or multi-parameter with defaults)
								if (params.size() >= 1) {
									if (params[0].is<DeclarationNode>()) {
										const auto& ctor_param_decl = params[0].as<DeclarationNode>();
										const auto& ctor_param_type = ctor_param_decl.type_node().as<TypeSpecifierNode>();

											// Match if types are compatible
										bool param_matches = false;
										if (ctor_param_type.type() == arg_type) {
											param_matches = true;
												// For class types, require exact type match, not just Type::Struct kind.
											if (isIrStructType(toIrType(arg_type)) &&
												(!arg_type_index.is_valid() || !ctor_param_type.type_index().is_valid() ||
												 ctor_param_type.type_index() != arg_type_index)) {
												param_matches = false;
											}
										}

										if (param_matches) {
												// Check if remaining parameters have defaults
											bool all_have_defaults = true;
											for (size_t i = 1; i < params.size(); ++i) {
												if (!params[i].is<DeclarationNode>() ||
													!params[i].as<DeclarationNode>().has_default_value()) {
													all_have_defaults = false;
													break;
												}
											}

											if (all_have_defaults) {
												found_matching_ctor = true;
												if (!ctor_node.is_explicit()) {
													found_non_explicit_ctor = true; // Optimization: a valid non-explicit ctor is found.
													break;
												}
											}
										}
									}
								}
							}
						}

							// Emit error only when every matching converting constructor is explicit.
						if (found_matching_ctor && !found_non_explicit_ctor) {
							FLASH_LOG(General, Error, "Cannot use implicit conversion with explicit constructor for type '",
									  StringTable::getStringView(target_type_info->name()), "'");
							FLASH_LOG(General, Error, "  In function call at argument ", arg_index);
							FLASH_LOG(General, Error, "  Use explicit construction: ",
									  StringTable::getStringView(target_type_info->name()), "(value)");
							throw CompileError("Cannot use implicit conversion with explicit constructor in function argument");
						}
					}
				}
			}

				// Check if argument is struct type and parameter expects different type
			if (arg_type == TypeCategory::Struct && arg_type != param_base_type && param_type->pointer_depth() == 0) {
				if (const TypeInfo* source_type_info = tryGetTypeInfo(arg_type_index)) {
					const int param_size = static_cast<int>(param_type->size_in_bits());

						// Look for a conversion operator to the parameter type
					const bool source_is_const = isExprConstQualified(argument);
					const StructMemberFunction* conv_op = findConversionOperator(
						source_type_info->getStructInfo(), param_type->type_index(), source_is_const);

					if (conv_op) {
						FLASH_LOG(Codegen, Debug, "Found conversion operator for function argument from ",
								  StringTable::getStringView(source_type_info->name()),
								  " to parameter type");
						if (auto result = emitConversionOperatorCall(argumentIrOperands, *source_type_info, *conv_op,
																	 param_type->type_index(), param_size, Token()))
							argumentIrOperands = *result;
					}
				}
			}
		}

			// Check if visitExpressionNode returned a TempVar - this means the value was computed
			// (e.g., global load, expression result, etc.) and we should use the TempVar directly
		bool use_computed_result = std::holds_alternative<TempVar>(argumentIrOperands.value);

			// For identifiers that returned local variable references (string_view), handle specially
		if (!use_computed_result && std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
			const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
			std::optional<ASTNode> symbol = lookupSymbol(identifier.name());
			if (!symbol.has_value()) {
				FLASH_LOG(Codegen, Error, "Symbol '", identifier.name(), "' not found for function argument");
				FLASH_LOG(Codegen, Error, "  Current function: ", current_function_name_);
				throw InternalError("Missing symbol for function argument");
			}
			const DeclarationNode* decl_ptr = get_decl_from_symbol(*symbol);
			if (!decl_ptr) {
				FLASH_LOG(Codegen, Error, "Function argument '", identifier.name(), "' is not a DeclarationNode");
				throw InternalError("Unexpected symbol type for function argument");
			}

			const auto& decl_node = *decl_ptr;
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

				// Enumerator constants should be passed as immediate values, not variable references.
			if (std::optional<ExprResult> enumerator_constant = tryMakeEnumeratorConstantExpr(
					type_node,
					StringTable::getOrInternStringHandle(identifier.name()))) {
				appendArgumentIrResult(*enumerator_constant);
				return;
			}

				// Check if this is an array - arrays decay to pointers when passed to functions
			if (decl_node.is_array()) {
					// For arrays, we need to pass the address of the first element
					// Create a temporary for the address
					// Generate AddressOf IR instruction to get the address of the array
				TempVar addr_var = emitAddressOf(type_node.category(), static_cast<int>(type_node.size_in_bits()), IrValue(StringTable::getOrInternStringHandle(identifier.name())));

					// Add the pointer (address) to the function call operands
					// For now, we use the element type with 64-bit size to indicate it's a pointer
					// TODO: Add proper pointer type support to the Type enum
				irOperands.emplace_back(type_node.type());  // Element type (e.g., Char for char[])
				irOperands.emplace_back(64);	 // Pointer size is 64 bits on x64
				irOperands.emplace_back(addr_var);
			} else if (param_ref_qualifier != CVReferenceQualifier::None) {
					// Parameter expects a reference - pass the address of the argument
				if (type_node.is_reference() || type_node.is_rvalue_reference()) {
						// Argument is already a reference - just pass it through
						// References are stored as pointers (64 bits), not the pointee size
					irOperands.emplace_back(type_node.type());
					irOperands.emplace_back(64);	 // Pointer size, not pointee size
					irOperands.emplace_back(StringTable::getOrInternStringHandle(identifier.name()));
				} else {
						// Argument is a value - take its address
					TempVar addr_var = emitAddressOf(type_node.category(), static_cast<int>(type_node.size_in_bits()), IrValue(StringTable::getOrInternStringHandle(identifier.name())));

						// Pass the address
					irOperands.emplace_back(type_node.type());
					irOperands.emplace_back(64);	 // Pointer size
					irOperands.emplace_back(addr_var);
				}
			} else if (type_node.is_reference() || type_node.is_rvalue_reference()) {
					// Argument is a reference but parameter expects a value - dereference
				TempVar deref_var = emitDereference(type_node.category(), 64, 1,
													StringTable::getOrInternStringHandle(identifier.name()));

					// Pass the dereferenced value
				irOperands.emplace_back(type_node.type());
				irOperands.emplace_back(static_cast<int>(type_node.size_in_bits()));
				irOperands.emplace_back(deref_var);
			} else {
					// Regular variable - pass by value
					// For pointer types, size is always 64 bits regardless of pointee type
				int arg_size = (type_node.pointer_depth() > 0) ? 64 : static_cast<int>(type_node.size_in_bits());
				irOperands.emplace_back(type_node.type());
				irOperands.emplace_back(arg_size);
				irOperands.emplace_back(StringTable::getOrInternStringHandle(identifier.name()));
			}
		} else {
				// Not an identifier - could be a literal, expression result, etc.
				// Check if parameter expects a reference and argument is a literal
			if (param_ref_qualifier != CVReferenceQualifier::None) {
					// Parameter expects a reference, but argument is not an identifier
					// We need to materialize the value into a temporary and pass its address

					// Check if this is a literal value (has unsigned long long or double in value)
				bool is_literal = (std::holds_alternative<unsigned long long>(argumentIrOperands.value) ||
								   std::holds_alternative<double>(argumentIrOperands.value));

				if (is_literal) {
						// Materialize the literal into a temporary variable
					TypeCategory literal_type = argumentIrOperands.typeEnum();
					int literal_size = argumentIrOperands.size_in_bits.value;

						// Create a temporary variable to hold the literal value
					TempVar temp_var = var_counter.next();

						// Generate an assignment IR to store the literal using typed payload
					AssignmentOp assign_op;
					assign_op.result = temp_var;	 // unused but required

						// Convert IrOperand to IrValue for the literal
					IrValue rhs_value;
					if (const auto* ull_val = std::get_if<unsigned long long>(&argumentIrOperands.value)) {
						rhs_value = *ull_val;
					} else if (const auto* d_val = std::get_if<double>(&argumentIrOperands.value)) {
						rhs_value = *d_val;
					}

						// Create TypedValue for lhs and rhs
					assign_op.lhs = makeTypedValue(literal_type, SizeInBits{static_cast<int>(literal_size)}, temp_var);
					assign_op.rhs = makeTypedValue(literal_type, SizeInBits{static_cast<int>(literal_size)}, rhs_value);

					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));

						// Now take the address of the temporary
					TempVar addr_var = emitAddressOf(literal_type, literal_size, IrValue(temp_var));

						// Pass the address
					irOperands.emplace_back(literal_type);
					irOperands.emplace_back(64);	 // Pointer size
					irOperands.emplace_back(addr_var);
				} else {
						// Not a literal (expression result in a TempVar) - check if it needs address taken
					if (std::holds_alternative<TempVar>(argumentIrOperands.value)) {
						TypeCategory expr_type = argumentIrOperands.typeEnum();
						int expr_size = argumentIrOperands.size_in_bits.value;
						TempVar expr_var = std::get<TempVar>(argumentIrOperands.value);

							// Check if the TempVar already holds an address
							// This can happen when:
							// 1. It's the result of a cast to reference (xvalue/lvalue)
							// 2. It's a 64-bit struct (pointer to struct)
							// 3. It has lvalue/xvalue metadata indicating it's already an address
						bool is_already_address = false;

							// Check for xvalue/lvalue metadata (from reference casts)
						auto& metadata_storage = GlobalTempVarMetadataStorage::instance();
						if (metadata_storage.hasMetadata(expr_var)) {
							TempVarMetadata metadata = metadata_storage.getMetadata(expr_var);
							if (metadata.category == ValueCategory::LValue ||
								metadata.category == ValueCategory::XValue) {
								is_already_address = true;
							}
						}

							// Fallback heuristic: 64-bit struct type likely holds an address
						if (!is_already_address && expr_size == 64 && expr_type == TypeCategory::Struct) {
							is_already_address = true;
						}

						if (is_already_address) {
								// Already an address - pass through directly
							appendArgumentIrResult(argumentIrOperands);
						} else {
								// Need to take address of the value
							TempVar addr_var = emitAddressOf(expr_type, expr_size, IrValue(expr_var));

							irOperands.emplace_back(expr_type);
							irOperands.emplace_back(64);	 // Pointer size
							irOperands.emplace_back(addr_var);
						}
					} else {
							// Fallback - just pass through directly
						appendArgumentIrResult(argumentIrOperands);
					}
				}
			} else {
					// Parameter doesn't expect a reference - pass through as-is
				appendArgumentIrResult(argumentIrOperands);
			}
		}
	});

		// Get return type information
		// Prefer the matched function declaration's return type over the original call's,
		// since template instantiation may have resolved dependent types (e.g., Tp* → int*)
		// But DON'T use it if the return type is still unresolved (UserDefined = template param)
	const TypeSpecifierNode* best_return_type = nullptr;
	if (matched_func_decl) {
		const auto& mrt = matched_func_decl->decl_node().type_node().as<TypeSpecifierNode>();
		if (mrt.category() != TypeCategory::UserDefined) {
			best_return_type = &mrt;
		}
	}
	if (!best_return_type) {
		best_return_type = &decl_node.type_node().as<TypeSpecifierNode>();
	}
	const auto& return_type = *best_return_type;
	CallOp call_op = createCallOp(
		ret_var,
		StringTable::getOrInternStringHandle(function_name),
		return_type,
		false,
		false);

		// Check if this is an indirect call (function pointer/reference)
	call_op.is_indirect_call = callExprNode.callee().is_indirect();
	if (matched_func_decl && matched_func_decl->is_member_function() && !matched_func_decl->is_static()) {
		call_op.is_member_function = true;
		TypeCategory this_type = TypeCategory::Struct;
		TypeIndex this_type_index{};
		std::string_view parent_struct = matched_func_decl->parent_struct_name();
		if (!parent_struct.empty()) {
			StringHandle parent_struct_handle = StringTable::getOrInternStringHandle(parent_struct);
			auto parent_it = getTypesByNameMap().find(parent_struct_handle);
			if (parent_it != getTypesByNameMap().end() && parent_it->second != nullptr) {
				this_type = parent_it->second->typeEnum();
				this_type_index = parent_it->second->type_index_;
			}
		}
		call_op.args.push_back(makeTypedValue(this_type_index.withCategory(this_type), SizeInBits{64}, IrValue(StringTable::getOrInternStringHandle("this"))));
	}

		// Detect if calling a function that returns struct by value (needs hidden return parameter for RVO)
		// Exclude references - they return a pointer, not a struct by value
	bool returns_struct = returnsStructByValue(return_type.type(), return_type.pointer_depth(), return_type.is_reference());
	bool needs_hidden_ret = needsHiddenReturnParam(return_type.type(), return_type.pointer_depth(), return_type.is_reference(), return_type.size_in_bits(), context_->isLLP64());
	if (needs_hidden_ret) {
		call_op.return_slot = ret_var;  // The result temp var serves as the return slot

		FLASH_LOG_FORMAT(Codegen, Debug,
						 "Function call {} returns struct by value (size={} bits) - using return slot (temp_{})",
						 function_name, return_type.size_in_bits(), ret_var.var_number);
	} else if (returns_struct) {
		FLASH_LOG_FORMAT(Codegen, Debug,
						 "Function call {} returns small struct by value (size={} bits) - will return in RAX",
						 function_name, return_type.size_in_bits());
	}

		// Set is_variadic based on function declaration (if available)
	if (matched_func_decl) {
		call_op.is_variadic = matched_func_decl->is_variadic();
	}

		// Convert operands to TypedValue arguments (skip first 2: result and function_name)
		// Operands come in fixed groups of 3: [type, size, value].
	size_t arg_idx = 0;
	for (size_t i = 2; i + 2 < irOperands.size(); i += 3) {
		TypedValue arg = toTypedValue(std::span<const IrOperand>(&irOperands[i], 3));
		const TypeSpecifierNode* param_type_spec = nullptr;
		if (matched_func_decl && arg_idx < param_nodes.size() && param_nodes[arg_idx].is<DeclarationNode>()) {
			param_type_spec = &param_nodes[arg_idx].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
		} else if (cached_param_list && !cached_param_list->empty()) {
			if (arg_idx < cached_param_list->size()) {
				const auto& cached = (*cached_param_list)[arg_idx];
				if (cached.type_node.is<TypeSpecifierNode>()) {
					param_type_spec = &cached.type_node.as<TypeSpecifierNode>();
				}
			} else if (cached_param_list->back().is_parameter_pack) {
				const auto& cached = cached_param_list->back();
				if (cached.type_node.is<TypeSpecifierNode>()) {
					param_type_spec = &cached.type_node.as<TypeSpecifierNode>();
				}
			}
		}
		if (param_type_spec) {
				// For regular call arguments, only apply metadata that the argument
				// evaluation couldn't know (pointer_depth, cv_qualifier, ref_qualifier).
				// Do NOT overwrite type/size_in_bits — the argument's evaluated type
				// must be preserved so the backend emits the correct-width MOV.
				// (e.g., a short arg must stay 16-bit even if the param is int;
				// the backend handles the implicit promotion.)
				// applyTypeNodeMetadata is still used for default arguments where the
				// parameter type IS the correct type.
			arg.pointer_depth = PointerDepth{static_cast<int>(param_type_spec->pointer_depth())};
			if (param_type_spec->type_index().is_valid()) {
				arg.type_index = param_type_spec->type_index();
			}
			arg.cv_qualifier = param_type_spec->cv_qualifier();
			if (param_type_spec->is_rvalue_reference()) {
				arg.ref_qualifier = ReferenceQualifier::RValueReference;
			} else if (param_type_spec->is_reference()) {
				arg.ref_qualifier = ReferenceQualifier::LValueReference;
			} else {
				arg.ref_qualifier = ReferenceQualifier::None;
			}
		}

		call_op.args.push_back(arg);
		arg_idx++;
	}

		// Fill in default arguments for parameters that weren't explicitly provided
	if (matched_func_decl) {
		fillInDefaultArguments(call_op, param_nodes, arg_idx);
	} else if (cached_param_list) {
		fillInCachedDefaultArguments(call_op, *cached_param_list, arg_idx);
	}

		// Add the function call instruction with typed payload
	ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), callExprNode.called_from()));

	return buildCallReturnResult(return_type, ret_var, context, callExprNode.called_from());
}
