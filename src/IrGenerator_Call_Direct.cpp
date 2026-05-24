#include "Parser.h"
#include "IrGenerator.h"
#include "CallNodeHelpers.h"
#include "SemanticAnalysis.h"
#include "TypeSizeQuery.h"

static TypeSpecifierNode normalizeCallReturnType(TypeSpecifierNode return_type);

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
												? SizeInBits{requireConcreteAliasResolvedTypeSizeBits(return_type, "direct call reference return")}
												: call_op.return_size_in_bits;
}

void AstToIr::populateReferenceReturnInfo(VirtualCallOp& call_op, const TypeSpecifierNode& return_type) {
	call_op.returns_reference =
		(return_type.is_reference() || return_type.is_rvalue_reference()) &&
		!return_type.has_function_signature();
	call_op.returns_rvalue_reference = return_type.is_rvalue_reference();
	call_op.referenced_value_size_in_bits = call_op.returns_reference
												? SizeInBits{requireConcreteAliasResolvedTypeSizeBits(return_type, "virtual call reference return")}
												: call_op.result.size_in_bits;
}

TypeSpecifierNode AstToIr::buildFunctionSignatureReturnType(const FunctionSignature& signature) const {
	TypeSpecifierNode return_type(
		signature.return_type_index.withCategory(signature.returnType()),
		SizeInBits{get_type_size_bits(signature.returnType())},
		Token{},
		CVQualifier::None,
		signature.return_reference_qualifier);
	return_type.add_pointer_levels(signature.return_pointer_depth);
	return_type.set_size_in_bits(getTypeSpecSizeBits(return_type));
	return normalizeCallReturnType(return_type);
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
	const TypeSpecifierNode return_type = buildFunctionSignatureReturnType(signature);
	if (return_type.type() == TypeCategory::Void) {
		return 0;
	}
	return (return_type.pointer_depth() > 0 || return_type.is_reference() || return_type.is_rvalue_reference())
		? 64
		: requireConcreteAliasResolvedTypeSizeBits(return_type, "indirect call signature return size");
}

void AstToIr::populateIndirectCallReturnInfo(IndirectCallOp& call_op, const FunctionSignature& signature) {
	const TypeSpecifierNode return_type = buildFunctionSignatureReturnType(signature);
	call_op.return_type_index = getFunctionSignatureReturnTypeIndex(signature);
	call_op.return_size_in_bits = SizeInBits{getFunctionSignatureReturnSizeBits(signature)};
	call_op.return_pointer_depth = PointerDepth{signature.return_pointer_depth};
	call_op.is_variadic = signature.is_variadic;
	call_op.fixed_argument_count = signature.parameter_type_indices.size();
	call_op.returns_reference = signature.returns_reference() && !return_type.has_function_signature();
	call_op.returns_rvalue_reference = signature.returns_rvalue_reference();
	call_op.referenced_value_size_in_bits = call_op.returns_reference
												? SizeInBits{requireConcreteAliasResolvedTypeSizeBits(return_type, "indirect call reference return")}
												: call_op.return_size_in_bits;
	call_op.use_return_slot = needsHiddenReturnParam(
		call_op.returnType(),
		signature.return_pointer_depth,
		signature.returns_reference(),
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

	// When TypeSpecifierNode reports size 0 for a non-void type, resolve it
	// through the shared alias-aware concrete-size service.
	if (!call_op.return_size_in_bits.is_set() && normalized_return_type.category() != TypeCategory::Void) {
		call_op.return_size_in_bits = SizeInBits{
			requireConcreteAliasResolvedTypeSizeBits(normalized_return_type, "direct call return size")};
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

ExprResult AstToIr::buildIndirectCallReturnResult(const FunctionSignature& signature, TempVar ret_var) {
	return buildCallReturnResult(buildFunctionSignatureReturnType(signature), ret_var, ExpressionContext::Load, Token{});
}

static TypeSpecifierNode normalizeCallReturnType(TypeSpecifierNode return_type) {
	if (isPlaceholderAutoType(return_type.type()) && return_type.type_index().is_valid()) {
		if (const TypeInfo* return_type_info = tryGetTypeInfo(return_type.type_index())) {
			TypeCategory resolved_type = return_type_info->typeEnum();
			if (!isPlaceholderAutoType(resolved_type) && resolved_type != TypeCategory::Invalid) {
				return_type.set_category(resolved_type);
				if (return_type.size_in_bits() == 0) {
					return_type.set_size_in_bits(return_type_info->sizeInBits());
				}
			}
		}
	}
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
		const std::span<const size_t> return_dimensions = return_type.array_dimensions();
		std::vector<size_t> array_dimensions(return_dimensions.begin(), return_dimensions.end());
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
		int referenced_size_bits = requireConcreteAliasResolvedTypeSizeBits(normalized_return_type, "call return result reference size");
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
			copy_options.copy_qualified_name = false;
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
	std::vector<TypedValue> call_arguments;
	call_arguments.reserve(callExprNode.arguments().size());
	auto appendArgumentIrResult = [&](const ExprResult& result) {
		call_arguments.push_back(toTypedValue(result));
	};

	const auto& decl_node = callExprNode.callee().declaration();
	StringHandle func_name = decl_node.identifier_token().handle();
	std::string_view func_name_view = func_name.view();
	std::string_view lookup_name_view = callExprNode.has_qualified_name()
											? callExprNode.qualified_name()
											: func_name_view;
	const bool has_synthesized_template_suffix =
		func_name_view.find('$') != std::string_view::npos;
	auto sema_services = sema_.parserSemanticServices();
	const FunctionDeclarationNode* const parser_resolved_direct_target =
		getParserStoredDirectCallTarget(callExprNode);
	const ResolvedFunctionQueryResult sema_resolved_direct_query =
		sema_services.getResolvedDirectCallQuery(sema_call_key);
	const FunctionDeclarationNode* const sema_resolved_direct_target =
		sema_resolved_direct_query.state == ResolvedFunctionQueryResult::State::Available
			? sema_resolved_direct_query.function
			: nullptr;
	const FunctionDeclarationNode* const pre_resolved_direct_target = [&]() -> const FunctionDeclarationNode* {
		const FunctionDeclarationNode* target =
			parser_resolved_direct_target;
		if (!target) {
			target = sema_resolved_direct_target;
		}
		return target;
	}();

	FLASH_LOG_FORMAT(Codegen, Debug, "=== generateFunctionCallIr: func_name={} ===", func_name_view);

	// Check for compiler intrinsics and handle them specially
	auto intrinsic_result = tryGenerateIntrinsicIr(func_name_view, callExprNode);
	if (intrinsic_result.has_value()) {
		return intrinsic_result.value();
	}

	// Check if this function is marked as inline_always (pure expression template instantiations)
	// These functions should always be inlined and never generate calls
	// Prefer the sema/call-node resolved target before falling back to a lookup.
	extern SymbolTable gSymbolTable;
	const FunctionDeclarationNode* inline_always_target = pre_resolved_direct_target;
	if (!inline_always_target && !sema_normalized_current_function_) {
		auto all_overloads = gSymbolTable.lookup_all(func_name_view);
		for (const auto& overload : all_overloads) {
			if (!overload.is<FunctionDeclarationNode>()) {
				continue;
			}
			const FunctionDeclarationNode* overload_func_decl = &overload.as<FunctionDeclarationNode>();
			if (&overload_func_decl->decl_node() == &decl_node) {
				inline_always_target = overload_func_decl;
				break;
			}
		}
	}

	if (inline_always_target &&
		inline_always_target->is_inline_always() &&
		callExprNode.arguments().size() == 1) {
		const TypeSpecifierNode& return_type_spec = inline_always_target->decl_node().type_specifier_node();
		bool returns_reference = return_type_spec.is_reference() || return_type_spec.is_rvalue_reference();

		auto arg_node = callExprNode.arguments()[0];
		if (arg_node.is<ExpressionNode>()) {
			auto getInlineAlwaysArgType = [&]() -> std::optional<TypeSpecifierNode> {
				TypeSpecifierQueryResult sema_arg_type_query =
					sema_.parserSemanticServices().getOverloadResolutionArgTypeQuery(arg_node);
				if (sema_normalized_current_function_ &&
					sema_arg_type_query.state == TypeSpecifierQueryResult::State::NotYetAnalyzed) {
					throw InternalError("Normalized inline_always argument type query remained NotYetAnalyzed");
				}

				std::optional<TypeSpecifierNode> sema_arg_type;
				if (sema_arg_type_query.state == TypeSpecifierQueryResult::State::Available) {
					sema_arg_type = sema_arg_type_query.type;
				} else {
					sema_arg_type = sema_.parserSemanticServices().getOverloadResolutionArgType(arg_node);
				}

				const bool requires_recovery_fallback =
					!sema_arg_type.has_value() ||
					sema_arg_type->type() == TypeCategory::Invalid ||
					isPlaceholderAutoType(sema_arg_type->type());
				if (requires_recovery_fallback) {
					if (sema_normalized_current_function_) {
						throw InternalError(
							"Sema-missing inline_always argument type in sema-normalized body for '" +
							std::string(func_name_view) + "'");
					}
					throw InternalError(
						"Sema-missing inline_always argument type in non-normalized body for '" +
						std::string(func_name_view) + "'");
				}
				return sema_arg_type;
			};
			auto inlineAlwaysTypeMatches = [&](const TypeSpecifierNode& arg_type) {
				if (!returns_reference) {
					return return_type_spec.matches_signature(arg_type);
				}

				TypeSpecifierNode return_base = return_type_spec;
				return_base.set_reference_qualifier(ReferenceQualifier::None);
				TypeSpecifierNode arg_base = arg_type;
				arg_base.set_reference_qualifier(ReferenceQualifier::None);
				return return_base.matches_signature(arg_base);
			};
			bool can_inline = true;
			if (auto arg_type = getInlineAlwaysArgType(); arg_type.has_value()) {
				can_inline = inlineAlwaysTypeMatches(*arg_type);
			} else {
				can_inline = false;
			}
			if (can_inline) {
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
						StringHandle id_handle = ident.nameHandle();
						TypeCategory operand_type = TypeCategory::Int;  // Default
						static constexpr int DefaultInlineAlwaysOperandSizeBits = 32;
						int operand_size = DefaultInlineAlwaysOperandSizeBits;
						if (const DeclarationNode* decl = lookupDeclaration(id_handle)) {
							const TypeSpecifierNode& type = decl->type_specifier_node();
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
		const auto& func_type = func_ptr_decl->type_specifier_node();
		const ResolvedFunctionQueryResult resolved_operator_call_query =
			sema_services.getResolvedOpCallQuery(sema_call_key);
		const FunctionDeclarationNode* resolved_operator_call =
			resolved_operator_call_query.state == ResolvedFunctionQueryResult::State::Available
				? resolved_operator_call_query.function
				: nullptr;

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
		if (sema_normalized_current_function_ &&
			resolved_operator_call_query.state == ResolvedFunctionQueryResult::State::NotYetAnalyzed) {
			throw InternalError("Normalized direct operator() query remained NotYetAnalyzed");
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
				.function_pointer = func_name,
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

		// Invariant (audit 2026-04-27): sema reaches struct callable invocations
		// as member calls; codegen no longer recovers `operator()` targets by
		// traversing the struct/base-class hierarchy.

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

		// Handle the normalized recursive lambda self parameter
		// (e.g., self(self, n - 1)). Generic lambda callable parameters must have
		// been semantically normalized before codegen; do not treat unresolved
		// placeholder `auto` as the current closure or invent an `int` return type.
		if (is_recursive_lambda_self) {
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
				call_op.args.push_back(makeTypedValue(TypeCategory::Struct, SizeInBits{64}, IrValue(func_name)));

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
						call_op.args.push_back(makeTypedValue(TypeCategory::Struct, SizeInBits{64}, IrValue(func_name)));

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
					StringTable::getOrInternStringHandle(closure_type_name),
					{},		// namespace_path
					!current_lambda_context_.is_mutable	// const unless mutable lambda
				);
				call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);

				ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), callExprNode.called_from()));

				return makeExprResult(nativeTypeIndex(TypeCategory::Int), SizeInBits{32}, IrOperand{ret_var}, PointerDepth{}, ValueStorage::ContainsData);
			}
		}

		if (isPlaceholderAutoType(func_type.type())) {
				// Unresolved placeholders here used to be guessed as recursive lambda calls
				// with an `int` result. That is unsafe for callable generic-lambda
				// parameters; sema/parameter normalization owns this deduction.
			throw InternalError("Unresolved placeholder callable reached direct-call lowering");
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
	auto resolveMangledName = [&](const FunctionDeclarationNode* func_decl, StringHandle struct_name = StringHandle{}) {
		// Template-instantiation call nodes can carry the exact symbol name even when
		// the resolved declaration still points at pattern-owned metadata.
		if (has_precomputed_mangled) {
			function_name = callExprNode.mangled_name();
			return;
		}
		if (func_decl->has_mangled_name()) {
			function_name = func_decl->mangled_name();
		} else if (func_decl->linkage() != Linkage::C) {
			if (!struct_name.isValid()) {
				function_name = generateMangledNameForCall(*func_decl, StringHandle{}, func_decl->namespace_handle());
			} else {
				const OwnerManglingInfo owner_info =
					resolveOwnerManglingInfoForMangling(StringTable::getStringView(struct_name), func_decl->namespace_handle());
				function_name = generateMangledNameForCall(
					*func_decl,
					owner_info.owner_name_for_mangling,
					owner_info.owner_namespace_handle);
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

	auto extractQualifiedOwner = [](std::string_view qualified_name) -> std::string_view {
		const size_t scope_pos = qualified_name.rfind("::");
		return scope_pos == std::string_view::npos
			? std::string_view{}
			: qualified_name.substr(0, scope_pos);
	};

	auto queueDeferredMemberFunctions = [&](StringHandle struct_name,
											const StructTypeInfo* struct_info,
											std::string_view preferred_qualified_name) {
		if (!struct_name.isValid() || !struct_info) {
			return;
		}
		NamespaceHandle namespace_handle = buildNamespaceHandleFromQualifiedIdentifier(preferred_qualified_name);
		if (!namespace_handle.isValid() || namespace_handle.isGlobal()) {
			namespace_handle = buildNamespaceHandleForStructName(struct_name);
		}
		for (const auto& member_func : struct_info->member_functions) {
			ASTNode function_node = member_func.function_decl;
			if (function_node.is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode& func_decl = function_node.as<FunctionDeclarationNode>();
				if (!func_decl.is_materialized() && !func_decl.is_implicit() && member_func.getName().isValid()) {
					if (std::optional<ASTNode> materialized =
							sema_.ensureMemberFunctionMaterialized(
								struct_name,
								member_func.getName(),
								member_func.is_const())) {
						function_node = *materialized;
					}
				}
			}
			DeferredMemberFunctionInfo deferred_info;
			deferred_info.struct_name = struct_name;
			deferred_info.function_node = function_node;
			deferred_info.namespace_handle = namespace_handle;
			deferred_member_functions_.push_back(std::move(deferred_info));
		}
	};

	auto findCurrentStructMemberInHierarchy = [&]() -> const FunctionDeclarationNode* {
		if (!current_struct_name_.isValid()) {
			return nullptr;
		}
		auto current_struct_it = getTypesByNameMap().find(current_struct_name_);
		if (current_struct_it == getTypesByNameMap().end() || !current_struct_it->second->isStruct()) {
			return nullptr;
		}
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

		return findMemberInHierarchy(findMemberInHierarchy, struct_info);
	};

	auto consumeResolvedDirectCallTarget = [&](const FunctionDeclarationNode* resolved_target, std::string_view source_label) {
		if (!resolved_target || matched_func_decl) {
			return;
		}
		std::string_view parent = resolved_target->parent_struct_name();
		// Sema now owns the direct-call target even for decl-only and precomputed-mangled
		// member-like calls; codegen should only keep legacy lookup recovery for bodies
		// sema never normalized or explicit sema-recorded gaps.
		if (!parent.empty() && callExprNode.has_qualified_name()) {
			const StringHandle parent_handle = StringTable::getOrInternStringHandle(parent);
			if (gTemplateRegistry.isPatternStructName(parent_handle)) {
				return;
			}
		}
		if (!parent.empty() && current_struct_name_.isValid()) {
			const std::string_view current_struct_name_view = StringTable::getStringView(current_struct_name_);
			const StringHandle parent_handle = StringTable::getOrInternStringHandle(parent);
			const bool current_struct_matches_parent =
				current_struct_name_view == parent ||
				extractBaseTemplateName(current_struct_name_view) == parent ||
				([&] { auto pat = gTemplateRegistry.get_instantiation_pattern(current_struct_name_); return pat.has_value() && pat.value() == parent_handle; }());
			if (current_struct_matches_parent) {
				if (const FunctionDeclarationNode* instantiated_member = findCurrentStructMemberInHierarchy()) {
					matched_func_decl = instantiated_member;
					has_precomputed_mangled = false;
					resolveMangledName(matched_func_decl, current_struct_name_);
					FLASH_LOG_FORMAT(Codegen, Debug, "Remapped {} direct call target to current struct for: {}", source_label, func_name_view);
					return;
				}
			}
		}
		const bool is_current_struct_or_unowned = parent.empty() ||
			(current_struct_name_.isValid() && StringTable::getStringView(current_struct_name_) == parent);
		if (is_current_struct_or_unowned) {
			matched_func_decl = resolved_target;
			has_precomputed_mangled = false;
			resolveMangledName(matched_func_decl, StringTable::getOrInternStringHandle(parent));
			FLASH_LOG_FORMAT(Codegen, Debug, "Using {} direct call target for: {}", source_label, func_name_view);
			return;
		}

		StringHandle owner_handle;
		std::string_view resolved_owner_name = parent;
		const StructTypeInfo* owner_struct_info = nullptr;

		{
			const TypeInfo* owner_type_info = nullptr;
			if (callExprNode.has_qualified_name()) {
				owner_type_info = resolveQualifiedCallStruct(extractQualifiedOwner(callExprNode.qualified_name()));
			}
			if (!owner_type_info) {
				owner_type_info = resolveQualifiedCallStruct(parent);
			}
			if (owner_type_info && owner_type_info->isStruct()) {
				owner_handle = owner_type_info->name();
				resolved_owner_name = StringTable::getStringView(owner_handle);
				owner_struct_info = owner_type_info->getStructInfo();
			}
		}

		if (!owner_handle.isValid()) {
			return;
		}

		matched_func_decl = resolved_target;
		has_precomputed_mangled = false;
		std::string_view owner_for_mangling = parent;
		if (gTemplateRegistry.isPatternStructName(StringTable::getOrInternStringHandle(owner_for_mangling))) {
			owner_for_mangling = resolved_owner_name;
		}
		resolveMangledName(matched_func_decl, StringTable::getOrInternStringHandle(owner_for_mangling));
		if (owner_struct_info && !owner_struct_info->member_functions.empty()) {
			queueDeferredMemberFunctions(owner_handle, owner_struct_info, owner_for_mangling);
		}
		// Phase 5 Slice K invariant: sema's drain has already materialized every
		// reachable struct's members by this point, so the lazy registry is empty
		// for them.
		FLASH_LOG_FORMAT(Codegen, Debug, "Using {} cross-struct direct call target for: {}", source_label, func_name_view);
	};

	// Template substitution can preserve stale member-call descriptors from the
	// template pattern. Still trust pre-resolved free/static callees and concrete
	// instantiated members; only gate pattern-owned member descriptors.
	if (!matched_func_decl) {
		const FunctionDeclarationNode* callee_resolved_target =
			parser_resolved_direct_target;
		if (callee_resolved_target) {
			const bool is_pattern_member_target =
				!callee_resolved_target->parent_struct_name().empty() &&
				gTemplateRegistry.isPatternStructName(
					StringTable::getOrInternStringHandle(callee_resolved_target->parent_struct_name()));
			if (!has_synthesized_template_suffix || !is_pattern_member_target) {
				consumeResolvedDirectCallTarget(callee_resolved_target, "callee-resolved");
			}
		}
	}

	// Phase 1 (sema-owned ordinary call resolution): consume the pre-resolved
	// direct-call target stored by semantic analysis before attempting any
	// duplicate symbol-table recovery work in codegen.
	if (!matched_func_decl) {
		consumeResolvedDirectCallTarget(sema_resolved_direct_target, "sema-resolved");
	}

	// Check if the call expression has a pre-computed mangled name (for namespace-scoped functions)
	// If so, use it directly and skip the lookup logic
	if (has_precomputed_mangled && !matched_func_decl) {
		function_name = callExprNode.mangled_name();
		FLASH_LOG_FORMAT(Codegen, Debug, "Using pre-computed mangled name from call expression: {}", function_name);
		// We don't need to find matched_func_decl since we already have the mangled name
		// The mangled name is sufficient for generating the call instruction
	}

	// Keep lookup recovery only for bodies sema never normalized, synthesized
	// call wrappers that bypass the original call key, and explicit sema escape
	// hatches. Semantically normalized ordinary direct calls should now either
	// provide a sema-owned target or mark the call as unresolved.
	const bool sema_recorded_unresolved_call =
		sema_.hasUnresolvedCallArgs(sema_call_key);
	if (!matched_func_decl &&
		sema_normalized_current_function_ &&
		!sema_recorded_unresolved_call &&
		!parser_resolved_direct_target &&
		sema_resolved_direct_query.state == ResolvedFunctionQueryResult::State::NotYetAnalyzed) {
		throw InternalError("Normalized direct-call query remained NotYetAnalyzed");
	}
	const bool allow_lookup_recovery =
		!sema_normalized_current_function_ || // body not tracked by normalized_bodies_
		sema_recorded_unresolved_call; // sema recorded a known resolution gap

	// For sema-normalized ordinary direct calls, lowering must consume the sema-owned
	// callee selection instead of rescanning symbol tables and member hierarchies again.
	if (!matched_func_decl && allow_lookup_recovery) {
		// Look up the function in the global symbol table to get all overloads
		// Use global_symbol_table_ if available, otherwise fall back to local symbol_table
		auto scoped_overloads = global_symbol_table_
									? global_symbol_table_->lookup_all(decl_node.identifier_token().value())
									: symbol_table.lookup_all(decl_node.identifier_token().value());

		// Find the matching overload by comparing the DeclarationNode address
		// This works because the call expression holds a reference to the specific
		// DeclarationNode that was selected by overload resolution
		FLASH_LOG_FORMAT(Codegen, Debug, "Looking for function: {}, all_overloads size: {}",
						 lookup_name_view, scoped_overloads.size());
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
		if (const FunctionDeclarationNode* instantiated_member = findCurrentStructMemberInHierarchy()) {
			matched_func_decl = instantiated_member;
			has_precomputed_mangled = false;
			resolveMangledName(matched_func_decl, current_struct_name_);
			FLASH_LOG_FORMAT(Codegen, Debug, "Remapped stale precomputed member call {} to {}", func_name_view, function_name);
		}
	}

	// Audit 2026-04-27: the prior `scoped_overloads` and `gSymbolTable_overloads`
	// single-overload codegen recovery branches here were probed across the
	// full 2239-test corpus with hard-fail guards and never hit. Sema's
	// resolved direct-call target plus the precomputed-mangled path above
	// already cover these cases.

	// Invariant (audit 2026-04-27): the resolved-target paths above already
	// populate `matched_func_decl` for precomputed-mangled call sites
	// (including consteval enforcement, C++20 [dcl.consteval]). The earlier
	// `gSymbolTable` and `gSymbolTable_overloads` defensive scans, the
	// "current-struct + base-class member by name" recovery, and the
	// "qualified static member by struct iteration" recovery were all
	// removed after probing showed zero hits.

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
								resolveMangledName(matched_func_decl, StringTable::getOrInternStringHandle(resolved_struct_part));
								// Queue all member functions of this struct for deferred generation
								queueDeferredMemberFunctions(direct_type_info->name(), si, resolved_struct_part);
								break;
							}
						}
					}
					// Invariant: empty `member_functions` (lazy-instantiated template)
					// is impossible here — sema's drain materializes members
					// before this point.
				}
			}
		}
		if (scope_pos != std::string_view::npos && !matched_func_decl) {
			std::string_view struct_part = lookup_name_view.substr(0, scope_pos);
			std::string_view member_name_direct = lookup_name_view.substr(scope_pos + 2);
			const TypeInfo* alias_type_info = nullptr;
			if (current_struct_name_.isValid() && struct_part.find("::") == std::string_view::npos) {
				alias_type_info = resolveQualifiedCallStruct(struct_part);
				if (alias_type_info == nullptr) {
					StringHandle alias_qualified_handle = StringTable::getOrInternStringHandle(
						StringBuilder()
							.append(StringTable::getStringView(current_struct_name_))
							.append("::")
							.append(struct_part)
							.commit());
					auto alias_it = getTypesByNameMap().find(alias_qualified_handle);
					if (alias_it != getTypesByNameMap().end()) {
						alias_type_info = alias_it->second;
					}
				}
			}
			size_t direct_expected_param_count = 0;
			callExprNode.arguments().visit([&](ASTNode) { ++direct_expected_param_count; });
			const FunctionDeclarationNode* unique_static_member = nullptr;
			StringHandle unique_owner;
			for (const auto& [candidate_name, candidate_type_info] : getTypesByNameMap()) {
				if (candidate_type_info == nullptr || !candidate_type_info->isStruct()) {
					continue;
				}
				const StructTypeInfo* candidate_struct = candidate_type_info->getStructInfo();
				if (candidate_struct == nullptr) {
					continue;
				}
				for (const auto& mf : candidate_struct->member_functions) {
					if (!mf.function_decl.is<FunctionDeclarationNode>()) {
						continue;
					}
					const auto& fd = mf.function_decl.as<FunctionDeclarationNode>();
					if (!fd.is_static() ||
						fd.decl_node().identifier_token().value() != member_name_direct ||
						fd.parameter_nodes().size() != direct_expected_param_count) {
						continue;
					}
					if (unique_static_member != nullptr && unique_static_member != &fd) {
						unique_static_member = nullptr;
						unique_owner = {};
						goto ambiguous_qualified_static_member;
					}
					unique_static_member = &fd;
					unique_owner = candidate_type_info->name();
				}
			}
ambiguous_qualified_static_member:
			if (unique_static_member != nullptr && unique_owner.isValid()) {
				matched_func_decl = unique_static_member;
				StringHandle owner_for_call = unique_owner;
				if (alias_type_info != nullptr && !alias_type_info->isTemplateInstantiation() &&
					alias_type_info->type_index_.is_valid()) {
					if (const TypeInfo* underlying_alias_type =
							tryGetTypeInfo(alias_type_info->type_index_)) {
						alias_type_info = underlying_alias_type;
					}
				}
				if (alias_type_info != nullptr && alias_type_info->isTemplateInstantiation()) {
					InlineVector<TemplateTypeArg, 4> alias_args;
					for (const auto& stored_arg : alias_type_info->templateArgs()) {
						alias_args.push_back(toTemplateTypeArg(stored_arg));
					}
					if (!alias_args.empty()) {
						std::string_view alias_instantiation_name =
							StringTable::getStringView(alias_type_info->name());
						std::string_view instantiated_owner =
							alias_instantiation_name;
						if (size_t dollar_pos = alias_instantiation_name.find('$');
							dollar_pos != std::string_view::npos) {
							instantiated_owner =
								StringBuilder()
									.append(StringTable::getStringView(unique_owner))
									.append(alias_instantiation_name.substr(dollar_pos))
									.commit();
						}
						owner_for_call = StringTable::getOrInternStringHandle(instantiated_owner);
						DeferredMemberFunctionInfo deferred_info;
						deferred_info.struct_name = owner_for_call;
						deferred_info.function_node = ASTNode(unique_static_member);
						deferred_info.namespace_handle = unique_static_member->namespace_handle();
						deferred_member_functions_.push_back(std::move(deferred_info));
					}
				}
				resolveMangledName(matched_func_decl, owner_for_call);
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
													resolveMangledName(matched_func_decl, base_struct_info->getName());
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

		// Resolve P::method(args) where P is a template type parameter in the current struct.
		// e.g., template<typename P> struct UsePolicy { int process(int x) { return P::scale(x); } };
		if (!matched_func_decl && current_struct_name_.isValid() &&
			callExprNode.has_qualified_name() && scope_pos != std::string_view::npos) {
			std::string_view qualifier = lookup_name_view.substr(0, scope_pos);
			std::string_view member_name_local = lookup_name_view.substr(scope_pos + 2);

			const auto curr_type_it = getTypesByNameMap().find(current_struct_name_);
			if (curr_type_it != getTypesByNameMap().end() && curr_type_it->second) {
				const TypeInfo* curr_type = curr_type_it->second;
				// Walk instantiation context to find a type-parameter binding matching the qualifier
				if (const TypeInfo::TemplateArgInfo* bound_arg = curr_type->findLegacyInstantiationArgByName(qualifier)) {
					const TypeInfo* concrete_type = tryGetTypeInfo(bound_arg->type_index);
					if (concrete_type && concrete_type->isStruct()) {
						const StructTypeInfo* concrete_struct = concrete_type->getStructInfo();
						if (concrete_struct) {
							for (const auto& member_func : concrete_struct->member_functions) {
								if (!member_func.function_decl.is<FunctionDeclarationNode>()) {
									continue;
								}
								const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
								if (func_decl.decl_node().identifier_token().value() == member_name_local) {
									matched_func_decl = &func_decl;
									resolveMangledName(matched_func_decl, concrete_type->name());
									queueDeferredMemberFunctions(concrete_type->name(), concrete_struct, lookup_name_view);
									FLASH_LOG_FORMAT(Codegen, Debug,
										"Resolved template-param qualified call '{}' -> '{}::{}'",
										lookup_name_view,
										StringTable::getStringView(concrete_type->name()),
										member_name_local);
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

	if (!matched_func_decl && !allow_lookup_recovery) {
		throw InternalError(std::string(
			StringBuilder()
				.append("Phase 1: sema-normalized direct call missing resolved target for '")
				.append(func_name_view)
				.append("'")
				.commit()));
	}

	if (matched_func_decl &&
		matched_func_decl->is_member_function() &&
		matched_func_decl->decl_node().identifier_token().value() == "operator=") {
		StringHandle owner_struct_name = StringTable::getOrInternStringHandle(matched_func_decl->parent_struct_name());
		auto owner_type_it = getTypesByNameMap().find(owner_struct_name);
		const StructTypeInfo* owner_struct_info =
			(owner_type_it != getTypesByNameMap().end() && owner_type_it->second)
				? owner_type_it->second->getStructInfo()
				: nullptr;
		if (owner_struct_info &&
			shouldDeferImplicitAssignmentCodegen(*owner_struct_info, *matched_func_decl)) {
			for (const auto& member_func : owner_struct_info->member_functions) {
				if (!member_func.function_decl.is<FunctionDeclarationNode>()) {
					continue;
				}
				const auto& candidate = member_func.function_decl.as<FunctionDeclarationNode>();
				if (&candidate == matched_func_decl) {
					queueDeferredMemberFunctionFromNode(
						owner_struct_name,
						member_func.function_decl,
						buildNamespaceHandleForStructName(owner_struct_name));
					break;
				}
			}
		}
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
		ConstExpr::EvaluationContext ctx = makeEvalContext(global_symbol_table_ ? *global_symbol_table_ : gSymbolTable);
		ctx.global_symbols = global_symbol_table_ ? global_symbol_table_ : &gSymbolTable;
		auto eval_call_node = ASTNode::emplace_node<ExpressionNode>(callExprNode);
		auto eval_result = ConstExpr::Evaluator::evaluate(eval_call_node, ctx);
		if (!eval_result.success()) {
			throw CompileError("call to consteval function '" + std::string(func_name_view) +
							   "' cannot be used in a non-constant context: " + eval_result.error_message);
		}
		// Materialize the constant result into an ExprResult without emitting a call instruction.
		const TypeSpecifierNode& ret_spec =
			matched_func_decl->decl_node().type_specifier_node();
		const TypeCategory ret_type = ret_spec.type();
		const int ret_bits_raw = static_cast<int>(ret_spec.size_in_bits());
		const SizeInBits ret_size{
			ret_bits_raw != 0
				? ret_bits_raw
				: (ret_type == TypeCategory::Void
					   ? 0
					   : requireConcreteAliasResolvedTypeSizeBits(ret_spec, "consteval call return size"))};

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
		const std::span<const ASTNode> matched_params = matched_func_decl->parameter_nodes();
		param_nodes.assign(matched_params.begin(), matched_params.end());
	} else if (!has_precomputed_mangled) {
		// Try to get it from the function declaration stored in the call expression
		// Look up the function in symbol table to get full declaration with parameters
		auto local_func_symbol = lookupSymbol(func_decl_node.identifier_token().value());
		if (local_func_symbol.has_value() && local_func_symbol->is<FunctionDeclarationNode>()) {
			const auto& resolved_func_decl = local_func_symbol->as<FunctionDeclarationNode>();
			const std::span<const ASTNode> resolved_params = resolved_func_decl.parameter_nodes();
			param_nodes.assign(resolved_params.begin(), resolved_params.end());
		}
	}

	callExprNode.arguments().visit([&](ASTNode argument) {
		CallParamView param_view = resolveCallParamView(param_nodes, arg_index, nullptr, cached_param_list);
		const TypeSpecifierNode* param_type = param_view.type();
		CVReferenceQualifier param_ref_qualifier = param_view.ref_qualifier;
		const CallArgReferenceBindingInfo* sema_ref_binding = nullptr;
		if (param_type) {
			sema_ref_binding = sema_.getCallRefBinding(
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
			StringHandle identifier_name = identifier.nameHandle();
			const DeclarationNode* decl_ptr = lookupDeclaration(identifier_name);
			if (decl_ptr) {
				const auto& type_node = decl_ptr->type_specifier_node();
				if (type_node.is_reference() || type_node.is_rvalue_reference()) {
					// Argument is a reference variable being passed to a reference parameter
					// Pass the identifier name directly - the IRConverter will use MOV to
					// load the address stored in the reference variable
					call_arguments.push_back(buildReferenceCallArgumentFromDeclaration(*decl_ptr, identifier_name));
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

		if (param_type) {
			if (auto sema_bound_arg = tryBuildSemaBoundCallArgument(
					argumentIrOperands, argument, *param_type, sema_ref_binding, callExprNode.called_from())) {
				call_arguments.push_back(std::move(*sema_bound_arg));
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
			if (argument.is<ExpressionNode>()) {
				const void* key = &argument.as<ExpressionNode>();
				const auto slot = sema_.getSlot(key);
				if (slot.has_value() && slot->has_cast()) {
					const ImplicitCastInfo& cast_info =
						sema_.castInfoTable()[slot->cast_info_index.value - 1];
					TypeCategory from_type = sema_.typeContext().get(cast_info.source_type_id).category();
					const TypeCategory to_type = sema_.typeContext().get(cast_info.target_type_id).category();
					if (cast_info.cast_kind == StandardConversionKind::UserDefined &&
						from_type == TypeCategory::Struct) {
							// Sema annotated a user-defined conversion operator call
						TypeIndex source_type_idx = sema_.typeContext().get(cast_info.source_type_id).type_index;
						if (const TypeInfo* src_type_info = tryGetTypeInfo(source_type_idx)) {
							const bool source_is_const = ((static_cast<uint8_t>(sema_.typeContext().get(cast_info.source_type_id).base_cv)) & (static_cast<uint8_t>(CVQualifier::Const))) != 0;
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
					} else if (!is_struct_type(from_type) && !is_struct_type(to_type) &&
								   cast_info.cast_kind != StandardConversionKind::ArrayToPointer) {
						// Sema may annotate as Type::Enum while codegen resolves enum
						// constants to their underlying type; use actual runtime type.
						// NOTE: ArrayToPointer is excluded from generateTypeConversion because that
						//   function converts between scalar types (e.g. int->long), whereas
						//   array-to-pointer decay requires emitting an AddressOf IR node to
						//   materialise the base pointer from a stack-allocated array object.
						//   Calling generateTypeConversion on an array operand would instead
						//   integer-widen or reinterpret the value, producing garbage.
						//   String-literal args already carry a 64-bit TempVar pointer from
						//   generateStringLiteralIr, so decay is already complete.
						//   Identifier-array args are handled in the needs_array_decay block
						//   further below, which calls emitAddressOf to get the base pointer.
						if (from_type == TypeCategory::Enum && from_type != argumentIrOperands.typeEnum())
							from_type = argumentIrOperands.typeEnum();
						argumentIrOperands = generateTypeConversion(argumentIrOperands, from_type, to_type, callExprNode.called_from());
						arg_type = argumentIrOperands.typeEnum();
						arg_type_index = argumentIrOperands.type_index;
						sema_applied_arg_conversion = true;
					}
				}
			}

			// sema should annotate all resolved standard argument conversions.
			// Exception: hasUnresolvedCallArgs means sema tried but couldn't resolve the callee
			// (e.g. template specialization), so keep conversion recovery for that path.
			if (!sema_applied_arg_conversion &&
				param_ref_qualifier == CVReferenceQualifier::None &&
				param_type->pointer_depth() == 0 &&
				arg_type != param_base_type) {
				TypeConversionResult standard_conversion = can_convert_type(arg_type, param_base_type);
				if (standard_conversion.is_valid &&
					standard_conversion.rank != ConversionRank::UserDefined) {
					if (sema_normalized_current_function_ && !sema_.hasUnresolvedCallArgs(sema_call_key)) {
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
										const auto& ctor_param_type = ctor_param_decl.type_specifier_node();

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
			StringHandle identifier_name = identifier.nameHandle();
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

			const auto& arg_decl_node = *decl_ptr;
			call_arguments.push_back(buildDirectIdentifierCallArgument(
				arg_decl_node,
				identifier_name,
				param_ref_qualifier,
				argument,
				callExprNode.called_from()));
			return;
		} else {
			// Not an identifier - could be a literal, expression result, etc.
			// Check if parameter expects a reference and argument is a literal
			if (param_ref_qualifier != CVReferenceQualifier::None) {
				// Parameter expects a reference, but argument is not an identifier
				// We need to materialize the value into a temporary and pass its address
				call_arguments.push_back(buildReferenceCallArgumentFromResult(
					argumentIrOperands,
					callExprNode.called_from(),
					true));
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
		const auto& mrt = matched_func_decl->decl_node().type_specifier_node();
		if (mrt.category() != TypeCategory::UserDefined &&
			!isPlaceholderAutoType(mrt.type())) {
			best_return_type = &mrt;
		}
	}
	if (!best_return_type) {
		best_return_type = &decl_node.type_specifier_node();
	}
	const auto& return_type = *best_return_type;

	// Compute effective return type early to ensure CallOp creation and ABI metadata
	// use the same resolved type that buildCallReturnResult will eventually use.
	std::optional<TypeSpecifierNode> expression_return_type = getCallExpressionReturnType(ASTNode(&callExprNode));
	const TypeSpecifierNode& effective_return_type =
		expression_return_type.has_value() && shouldPreferExpressionReturnType(*expression_return_type, return_type)
			? *expression_return_type
			: return_type;

	CallOp call_op = createCallOp(
		ret_var,
		StringTable::getOrInternStringHandle(function_name),
		effective_return_type,
		false,
		false);

	// Check if this is an indirect call (function pointer/reference)
	call_op.is_indirect_call = callExprNode.callee().is_indirect();
	if (matched_func_decl &&
		matched_func_decl->is_member_function() &&
		!callExprNode.callee().is_static_member() &&
		!matched_func_decl->is_static()) {
		// Static/template rebinding can leave member metadata conservative; only add
		// an implicit this argument when the current lowering scope actually exposes one.
		if (lookupDeclaration("this")) {
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
	}

	// Detect if calling a function that returns struct by value (needs hidden return parameter for RVO)
	// Exclude references - they return a pointer, not a struct by value
	bool returns_struct = returnsStructByValue(effective_return_type.type(), effective_return_type.pointer_depth(), effective_return_type.is_reference());
	bool needs_hidden_ret = needsHiddenReturnParam(effective_return_type.type(), effective_return_type.pointer_depth(), effective_return_type.is_reference(), effective_return_type.size_in_bits(), context_->isLLP64());
	if (needs_hidden_ret) {
		call_op.return_slot = ret_var;  // The result temp var serves as the return slot

		FLASH_LOG_FORMAT(Codegen, Debug,
						 "Function call {} returns struct by value (size={} bits) - using return slot (temp_{})",
						 function_name, effective_return_type.size_in_bits(), ret_var.var_number);
	} else if (returns_struct) {
		FLASH_LOG_FORMAT(Codegen, Debug,
						 "Function call {} returns small struct by value (size={} bits) - will return in RAX",
						 function_name, effective_return_type.size_in_bits());
	}

	// Set is_variadic based on function declaration (if available)
	if (matched_func_decl) {
		call_op.is_variadic = matched_func_decl->is_variadic();
	}
	call_op.fixed_argument_count = param_nodes.size() + (call_op.is_member_function ? 1u : 0u);

	size_t arg_idx = 0;
	for (TypedValue arg : call_arguments) {
		CallParamView param_view = resolveCallParamView(param_nodes, arg_idx, nullptr, cached_param_list);
		const TypeSpecifierNode* param_type_spec = param_view.type();
		if (param_type_spec) {
			// Preserve the lowered argument's type identity and pointer depth.
			// They describe the runtime value being passed; copying them from the
			// parameter recreates the old flattened-operand ambiguity in a typed payload.
			applyCallParameterBindingMetadata(arg, *param_type_spec);
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

	return buildCallReturnResult(effective_return_type, ret_var, context, callExprNode.called_from());
}
