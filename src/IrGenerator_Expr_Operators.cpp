#include "Parser.h"
#include "IrGenerator.h"
#include "SemanticAnalysis.h"

namespace {
[[noreturn]] void throwDeletedSameTypeAssignmentCompileError(const StructTypeInfo& struct_info, bool prefer_move) {
	const char* assignment_kind = prefer_move ? "move" : "copy";
	std::string_view error_msg = StringBuilder()
	                                 .append("Call to deleted ")
	                                 .append(assignment_kind)
	                                 .append(" assignment operator of '")
	                                 .append(StringTable::getStringView(struct_info.name))
	                                 .append("'")
	                                 .commit();
	throw CompileError(std::string(error_msg));
}

void diagnoseDeletedSameTypeAssignmentUsage(const StructTypeInfo& struct_info, bool prefer_move) {
	if (prefer_move) {
		if (struct_info.isMoveAssignmentDeleted()) {
			throwDeletedSameTypeAssignmentCompileError(struct_info, true);
		}
		if (struct_info.findMoveAssignmentOperator(true)) {
			return;
		}
	}

	if (struct_info.isCopyAssignmentDeleted()) {
		throwDeletedSameTypeAssignmentCompileError(struct_info, false);
	}
}

bool shouldPreferMoveAssignment(const ExprResult& rhs_expr_result) {
	if (const auto* temp_var = std::get_if<TempVar>(&rhs_expr_result.value)) {
		return getTempVarMetadata(*temp_var).category != ValueCategory::LValue;
	}
	return false;
}

std::optional<bool> getSameTypeAssignmentKind(const StructTypeInfo& struct_info, const FunctionDeclarationNode& func_decl) {
	if (func_decl.parameter_nodes().empty() || !func_decl.parameter_nodes()[0].is<DeclarationNode>()) {
		return std::nullopt;
	}

	const auto& param_type = func_decl.parameter_nodes()[0].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
	if (param_type.category() != TypeCategory::Struct || !struct_info.isOwnTypeIndex(param_type.type_index())) {
		return std::nullopt;
	}

	if (param_type.is_rvalue_reference()) {
		return true;
	}
	if (param_type.is_lvalue_reference()) {
		return false;
	}
	return std::nullopt;
}
} // namespace

AstToIr::GlobalStaticBindingInfo AstToIr::resolveGlobalOrStaticBinding(const IdentifierNode& identifier) {
	GlobalStaticBindingInfo info;
	StringHandle identifier_handle = identifier.nameHandle();
	if (!identifier_handle.isValid()) {
		identifier_handle = StringTable::getOrInternStringHandle(identifier.name());
	}

	auto tryPopulateTypeFromDeclaration = [&](const ASTNode& symbol) {
		const DeclarationNode* decl_ptr = nullptr;
		if (symbol.is<VariableDeclarationNode>()) {
			decl_ptr = &symbol.as<VariableDeclarationNode>().declaration();
		} else if (symbol.is<DeclarationNode>()) {
			decl_ptr = &symbol.as<DeclarationNode>();
		}

		if (!decl_ptr || !decl_ptr->type_node().is<TypeSpecifierNode>()) {
			return;
		}

		const auto& ts = decl_ptr->type_node().as<TypeSpecifierNode>();
		info.type_index = nativeTypeIndex(ts.type());
		info.size_in_bits = SizeInBits{ts.size_in_bits()};
	};

	switch (identifier.binding()) {
	case IdentifierBinding::Global: {
		auto mangle_it = global_variable_names_.find(identifier_handle);
		info.store_name = (mangle_it != global_variable_names_.end()) ? mangle_it->second : identifier_handle;
		info.is_global_or_static = info.store_name.isValid();

		if (global_symbol_table_) {
			const auto symbol = global_symbol_table_->lookup(identifier.name());
			if (symbol.has_value()) {
				tryPopulateTypeFromDeclaration(*symbol);
			}
		}
		return info;
	}
	case IdentifierBinding::StaticLocal: {
		auto it = static_local_names_.find(identifier_handle);
		if (it == static_local_names_.end()) {
			return info;
		}

		info.is_global_or_static = true;
		info.store_name = it->second.mangled_name;
		info.type_index = nativeTypeIndex(it->second.type());
		info.size_in_bits = it->second.size_in_bits;
		return info;
	}
	case IdentifierBinding::StaticMember: {
		info.store_name = identifier.resolved_name();
		info.is_global_or_static = info.store_name.isValid();
		if (!info.store_name.isValid()) {
			return info;
		}

		const auto findStaticMemberInStruct = [&](StringHandle struct_name) -> const StructStaticMember* {
			if (!struct_name.isValid()) {
				return nullptr;
			}

			auto struct_it = getTypesByNameMap().find(struct_name);
			if (struct_it == getTypesByNameMap().end() || !struct_it->second || !struct_it->second->getStructInfo()) {
				return nullptr;
			}

			return struct_it->second->getStructInfo()->findStaticMember(identifier_handle);
		};

		const std::string_view resolved_name = info.store_name.view();
		const size_t last_scope_pos = resolved_name.rfind("::");
		const StringHandle owner_name = (last_scope_pos == std::string_view::npos)
		                                    ? StringHandle{}
		                                    : StringTable::getOrInternStringHandle(resolved_name.substr(0, last_scope_pos));

		const StructStaticMember* static_member = findStaticMemberInStruct(owner_name);
		if (!static_member && current_struct_name_.isValid() && current_struct_name_ != owner_name) {
			static_member = findStaticMemberInStruct(current_struct_name_);
		}
		if (static_member) {
			info.type_index = nativeTypeIndex(static_member->memberType());
			info.size_in_bits = SizeInBits{static_cast<int>(static_member->size * 8)};
		}
		return info;
	}
	default:
		break;
	}

	if (identifier.binding() != IdentifierBinding::Unresolved) {
		return info;
	}

	auto static_local_it = static_local_names_.find(identifier_handle);
	if (static_local_it != static_local_names_.end()) {
		info.is_global_or_static = true;
		info.store_name = static_local_it->second.mangled_name;
		info.type_index = nativeTypeIndex(static_local_it->second.type());
		info.size_in_bits = static_local_it->second.size_in_bits;
		return info;
	}

	if (global_symbol_table_) {
		const auto global_symbol = global_symbol_table_->lookup(identifier.name());
		if (global_symbol.has_value()) {
			auto mangle_it = global_variable_names_.find(identifier_handle);
			info.store_name = (mangle_it != global_variable_names_.end()) ? mangle_it->second : identifier_handle;
			info.is_global_or_static = info.store_name.isValid();
			tryPopulateTypeFromDeclaration(*global_symbol);
		}
	}

	return info;
}

std::optional<TypedValue> AstToIr::generateDefaultStructArg(const InitializerListNode& init_list, const TypeSpecifierNode& param_type) {
	// Look up the struct type info
	TypeIndex type_idx = param_type.type_index();
	if (!type_idx.is_valid() || type_idx.index() >= getTypeInfoCount())
		return std::nullopt;
	const TypeInfo& type_info = getTypeInfo(type_idx);
	const StructTypeInfo* struct_info = type_info.getStructInfo();
	if (!struct_info)
		return std::nullopt;

	// Create a temporary variable for the struct
	TempVar temp = var_counter.next();
	const auto& initializers = init_list.initializers();

	// Emit constructor call (default ctor to allocate the struct)
	ConstructorCallOp ctor_op;
	ctor_op.struct_name = type_info.name();
	ctor_op.object = temp;
	if (initializers.empty()) {
		fillInDefaultConstructorArguments(ctor_op, *struct_info);
	}
	ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), Token()));

	// Emit member stores for each initializer
	const auto& members = struct_info->members;
	for (size_t i = 0; i < initializers.size() && i < members.size(); ++i) {
		const ASTNode& init_expr = initializers[i];
		const auto& member = members[i];

		// Evaluate the initializer expression
		IrValue store_value;
		TypeCategory store_type = member.memberType();
		int store_size = static_cast<int>(member.size * 8);
		bool store_value_set = false;

		if (init_expr.is<ExpressionNode>()) {
			ExprResult init_result = visitExpressionNode(init_expr.as<ExpressionNode>());
			store_type = init_result.typeEnum();
			store_size = init_result.size_in_bits.value;
			store_value = toIrValue(init_result.value);
			store_value_set = true;
		} else if (init_expr.is<InitializerListNode>() && member.type_index.category() == TypeCategory::Struct &&
		           member.type_index.is_valid()) {
			// Nested struct aggregate init: recursively construct the sub-aggregate
			// Per C++20 [dcl.init.aggr]/4-5, nested brace-enclosed init lists
			// initialize sub-aggregate members recursively.
			if (const StructTypeInfo* nested_struct_info = tryGetStructTypeInfo(member.type_index)) {
				// Build a temporary TypeSpecifierNode for the nested struct type
				int nested_size_bits = static_cast<int>(nested_struct_info->total_size * 8);
				TypeSpecifierNode nested_type_spec(member.type_index.withCategory(member.memberType()), nested_size_bits, Token{}, CVQualifier::None, ReferenceQualifier::None);
				auto nested_result = generateDefaultStructArg(init_expr.as<InitializerListNode>(), nested_type_spec);
				if (nested_result.has_value()) {
					store_type = nested_result->category();
					store_size = nested_result->size_in_bits.value;
					store_value = nested_result->value;
					store_value_set = true;
				} else {
					FLASH_LOG(Codegen, Error, "generateDefaultStructArg: failed to recursively init nested struct member");
				}
			}
		} else if (!init_expr.is<ExpressionNode>()) {
			FLASH_LOG(Codegen, Error, "generateDefaultStructArg: unhandled initializer type for member");
		}

		// Skip MemberStore if the value was never set (prevents emitting IR with a
		// default-constructed IrValue that may carry garbage into the callee).
		if (!store_value_set) {
			FLASH_LOG(Codegen, Error, "generateDefaultStructArg: skipping member '", member.name, "' - store value not set");
			continue;
		}

		// Emit MemberStoreOp
		MemberStoreOp ms;
		ms.value = makeTypedValue(member.type_index.withCategory(store_type), SizeInBits{store_size}, store_value);
		ms.object = temp;
		ms.member_name = member.name;
		ms.offset = static_cast<int>(member.offset);
		ms.struct_type_info = &type_info;
		ms.ref_qualifier = CVReferenceQualifier::None;
		ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(ms), Token()));
	}

	int actual_size_bits = static_cast<int>(struct_info->total_size * 8);
	TypedValue result;
	result.setType(TypeCategory::Struct);
	result.ir_type = IrType::Struct;
	result.size_in_bits = SizeInBits{static_cast<int>(actual_size_bits)};
	result.value = IrValue(temp);
	result.type_index = type_idx;
	return result;
}

void AstToIr::applyTypeNodeMetadata(TypedValue& value, const TypeSpecifierNode& type_node) {
	value.setType(type_node.type());
	value.ir_type = toIrType(type_node.type());
	if (type_node.pointer_depth() > 0 || type_node.is_reference() || type_node.is_rvalue_reference() || type_node.is_function_pointer() || type_node.is_member_function_pointer() || type_node.is_member_object_pointer()) {
		value.size_in_bits = SizeInBits{POINTER_SIZE_BITS};
	} else if (type_node.category() == TypeCategory::Struct && type_node.type_index().is_valid()) {
		const StructTypeInfo* struct_info = tryGetStructTypeInfo(type_node.type_index());
		if (struct_info) {
			value.size_in_bits = SizeInBits{static_cast<int>(struct_info->total_size * 8)};
		} else {
			value.size_in_bits = SizeInBits{type_node.size_in_bits()};
		}
	} else {
		value.size_in_bits = SizeInBits{type_node.size_in_bits()};
		if (!value.size_in_bits.is_set()) {
			value.size_in_bits = SizeInBits{get_type_size_bits(type_node.category())};
		}
	}
	value.type_index = type_node.type_index();
	value.pointer_depth = PointerDepth{static_cast<int>(type_node.pointer_depth())};
	value.cv_qualifier = type_node.cv_qualifier();
	if (type_node.is_rvalue_reference()) {
		value.ref_qualifier = ReferenceQualifier::RValueReference;
	} else if (type_node.is_reference()) {
		value.ref_qualifier = ReferenceQualifier::LValueReference;
	}
}

TypedValue AstToIr::buildConstructorArgumentValue(
    const ExprResult& argument_result,
    const ASTNode& argument,
    const TypeSpecifierNode* param_type,
    const Token& token) {
	TypedValue value;
	bool param_is_ref = param_type && (param_type->is_reference() || param_type->is_rvalue_reference());

	auto makeReferenceAddressValue = [&](TempVar address_temp) {
		value.setType(argument_result.category());
		value.ir_type = argument_result.ir_type;
		value.size_in_bits = SizeInBits{POINTER_SIZE_BITS};
		value.value = address_temp;
		value.type_index = argument_result.type_index;
	};

	auto materializeReferenceTemporary = [&](const TypedValue& source_value) {
		TempVar temp_var = var_counter.next();
		AssignmentOp assign_op;
		assign_op.result = temp_var;
		assign_op.lhs = makeTypedValue(source_value.type_index, source_value.size_in_bits, temp_var);
		assign_op.rhs = source_value;
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), token));

		TempVar addr_var = emitAddressOf(source_value.category(), source_value.size_in_bits.value, IrValue(temp_var), token);
		makeReferenceAddressValue(addr_var);
	};

	// Returns true if argument_result already holds a 64-bit address
	// (reference binding must MOV, not take the address again).
	auto tempAlreadyHoldsAddress = [&]() {
		return argument_result.storage == ValueStorage::ContainsAddress;
	};

	if (param_is_ref &&
	    argument.is<ExpressionNode>() &&
	    std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
		const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
		std::optional<ASTNode> symbol = lookupSymbol(identifier.name());

		const DeclarationNode* arg_decl = nullptr;
		if (symbol.has_value() && symbol->is<DeclarationNode>()) {
			arg_decl = &symbol->as<DeclarationNode>();
		} else if (symbol.has_value() && symbol->is<VariableDeclarationNode>()) {
			arg_decl = &symbol->as<VariableDeclarationNode>().declaration();
		}

		if (arg_decl) {
			const auto& arg_type = arg_decl->type_node().as<TypeSpecifierNode>();
			if (arg_type.is_reference() || arg_type.is_rvalue_reference()) {
				value = toTypedValue(argument_result);
			} else {
				TempVar addr_var = var_counter.next();
				AddressOfOp addr_op;
				addr_op.result = addr_var;
				addr_op.operand.setType(arg_type.category());
				addr_op.operand.ir_type = toIrType(arg_type.type());
				addr_op.operand.size_in_bits = SizeInBits{arg_type.size_in_bits()};
				addr_op.operand.pointer_depth = PointerDepth{};
				addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
				ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), token));

				value.setType(arg_type.type());
				value.ir_type = toIrType(arg_type.type());
				value.size_in_bits = SizeInBits{POINTER_SIZE_BITS};
				value.value = addr_var;
				value.ref_qualifier = ReferenceQualifier::LValueReference;
				value.type_index = arg_type.type_index();
			}
		} else {
			value = toTypedValue(argument_result);
		}
	} else if (param_is_ref && std::holds_alternative<TempVar>(argument_result.value)) {
		const TempVar& arg_temp = std::get<TempVar>(argument_result.value);
		if (auto lvalue_info = getTempVarLValueInfo(arg_temp);
		    lvalue_info.has_value() &&
		    lvalue_info->kind == LValueInfo::Kind::Member &&
		    std::holds_alternative<StringHandle>(lvalue_info->base)) {
			TempVar address_temp = var_counter.next();
			AddressOfMemberOp addr_member_op;
			addr_member_op.result = address_temp;
			addr_member_op.base_object = std::get<StringHandle>(lvalue_info->base);
			addr_member_op.member_offset = lvalue_info->offset;
			addr_member_op.member_type_index = argument_result.type_index;
			addr_member_op.member_size_in_bits = argument_result.size_in_bits.value;
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOfMember, std::move(addr_member_op), token));

			ValueCategory category = isTempVarXValue(arg_temp) ? ValueCategory::XValue : ValueCategory::LValue;
			TempVarMetadata address_meta = TempVarMetadata::makeReference(
			    TypeIndex{0, argument_result.typeEnum()},
			    argument_result.size_in_bits,
			    category);
			address_meta.lvalue_info = LValueInfo(LValueInfo::Kind::Indirect, address_temp, 0);
			setTempVarMetadata(address_temp, std::move(address_meta));

			makeReferenceAddressValue(address_temp);
		} else if (tempAlreadyHoldsAddress()) {
			value = toTypedValue(argument_result);
		} else {
			TempVar addr_var = emitAddressOf(
			    argument_result.category(),
			    argument_result.size_in_bits.value,
			    IrValue(arg_temp),
			    token);
			makeReferenceAddressValue(addr_var);
		}
	} else if (param_is_ref && (std::holds_alternative<unsigned long long>(argument_result.value) ||
	                            std::holds_alternative<double>(argument_result.value))) {
		materializeReferenceTemporary(toTypedValue(argument_result));
	} else {
		value = toTypedValue(argument_result);
	}

	if (param_is_ref && std::holds_alternative<TempVar>(argument_result.value)) {
		const TempVar& arg_temp = std::get<TempVar>(argument_result.value);
		if (isTempVarXValue(arg_temp)) {
			value.ref_qualifier = ReferenceQualifier::RValueReference;
		} else if (isTempVarLValue(arg_temp)) {
			value.ref_qualifier = ReferenceQualifier::LValueReference;
		}
	}

	if (param_type) {
		ReferenceQualifier arg_ref_qualifier = value.ref_qualifier;
		applyTypeNodeMetadata(value, *param_type);
		if (arg_ref_qualifier != ReferenceQualifier::None) {
			value.ref_qualifier = arg_ref_qualifier;
		}
	}

	return value;
}

void AstToIr::fillInDefaultArguments(CallOp& call_op, const std::vector<ASTNode>& param_nodes, size_t arg_idx) {
	for (size_t i = arg_idx; i < param_nodes.size(); ++i) {
		if (!param_nodes[i].is<DeclarationNode>())
			continue;
		const auto& param_decl = param_nodes[i].as<DeclarationNode>();
		if (param_decl.is_parameter_pack()) {
			// A trailing function parameter pack may legally be omitted.
			break;
		}
		if (!param_decl.has_default_value()) {
			// Reaching a non-default parameter here means overload resolution
			// accepted a call with too few arguments — that's a compiler bug.
			throw InternalError("Missing default argument for parameter '" +
			                    std::string(param_decl.identifier_token().value()) +
			                    "' (overload resolution should have rejected this call)");
		}
		call_op.args.push_back(materializeDefaultArgument(
		    param_decl.default_value(),
		    param_decl.type_node().as<TypeSpecifierNode>(),
		    StringBuilder()
		        .append("parameter '")
		        .append(param_decl.identifier_token().value())
		        .append("'")
		        .commit()));
	}
}

TypedValue AstToIr::materializeDefaultArgument(
    const ASTNode& default_expr,
    const TypeSpecifierNode& param_type_spec,
    std::string_view error_context) {
	auto materializePlaceholderCtorDefault = [&](const ConstructorCallNode& ctor_call) -> std::optional<TypedValue> {
		if (ctor_call.arguments().size() == 0 &&
		    ctor_call.type_node().is<TypeSpecifierNode>() &&
		    (ctor_call.type_node().as<TypeSpecifierNode>().category() == TypeCategory::UserDefined ||
		     ctor_call.type_node().as<TypeSpecifierNode>().category() == TypeCategory::Struct ||
		     ctor_call.type_node().as<TypeSpecifierNode>().category() == TypeCategory::Template ||
		     isPlaceholderAutoType(ctor_call.type_node().as<TypeSpecifierNode>().type())) &&
		    !is_struct_type(param_type_spec.category()) &&
		    param_type_spec.category() != TypeCategory::UserDefined) {
			const int type_size_bits = get_type_size_bits(param_type_spec.category());
			TypedValue concrete_default = is_floating_point_type(param_type_spec.category())
			                                  ? makeTypedValue(param_type_spec.type(), SizeInBits{type_size_bits}, 0.0)
			                                  : makeTypedValue(param_type_spec.type(), SizeInBits{type_size_bits}, 0ULL);
			applyTypeNodeMetadata(concrete_default, param_type_spec);
			return concrete_default;
		}
		return std::nullopt;
	};

	if (default_expr.is<ConstructorCallNode>()) {
		const ConstructorCallNode& ctor_call = default_expr.as<ConstructorCallNode>();
		if (std::optional<TypedValue> concrete_default = materializePlaceholderCtorDefault(ctor_call)) {
			return *concrete_default;
		}
		ASTNode wrapped_expr = ASTNode::emplace_node<ExpressionNode>(ctor_call);
		auto default_operands = visitExpressionNode(wrapped_expr.as<ExpressionNode>());
		TypedValue tv = toTypedValue(default_operands);
		applyTypeNodeMetadata(tv, param_type_spec);
		return tv;
	}

	if (default_expr.is<ExpressionNode>()) {
		const ExpressionNode& default_expr_node = default_expr.as<ExpressionNode>();
		if (const auto* ctor_call = std::get_if<ConstructorCallNode>(&default_expr_node)) {
			if (std::optional<TypedValue> concrete_default = materializePlaceholderCtorDefault(*ctor_call)) {
				return *concrete_default;
			}
		}
		auto default_operands = visitExpressionNode(default_expr_node);
		TypedValue tv = toTypedValue(default_operands);
		applyTypeNodeMetadata(tv, param_type_spec);
		return tv;
	}

	if (default_expr.is<InitializerListNode>()) {
		auto result = generateDefaultStructArg(default_expr.as<InitializerListNode>(), param_type_spec);
		if (result.has_value()) {
			applyTypeNodeMetadata(*result, param_type_spec);
			return *result;
		}
		throw InternalError("Failed to generate struct default argument for " + std::string(error_context));
	}

	throw InternalError("Unhandled default argument AST node type for " + std::string(error_context));
}

void AstToIr::fillInConstructorDefaultArguments(
    ConstructorCallOp& ctor_op,
    const ConstructorDeclarationNode& ctor_node,
    size_t explicit_arg_count) {
	const auto& params = ctor_node.parameter_nodes();
	for (size_t i = explicit_arg_count; i < params.size(); ++i) {
		if (!params[i].is<DeclarationNode>()) {
			continue;
		}

		const auto& param_decl = params[i].as<DeclarationNode>();
		if (param_decl.is_parameter_pack()) {
			// A trailing constructor parameter pack may legally be omitted.
			break;
		}
		if (!param_decl.has_default_value()) {
			throw InternalError(std::string(StringBuilder()
			                                    .append("Missing default argument for constructor parameter '")
			                                    .append(param_decl.identifier_token().value())
			                                    .append("' (constructor resolution should have rejected this call)")
			                                    .commit()));
		}

		ctor_op.arguments.push_back(materializeDefaultArgument(
		    param_decl.default_value(),
		    param_decl.type_node().as<TypeSpecifierNode>(),
		    StringBuilder()
		        .append("constructor parameter '")
		        .append(param_decl.identifier_token().value())
		        .append("'")
		        .commit()));
	}
}

void AstToIr::fillInDefaultConstructorArguments(ConstructorCallOp& ctor_op, const StructTypeInfo& struct_info) {
	const StructMemberFunction* default_ctor = struct_info.findDefaultConstructor();
	if (!default_ctor || !default_ctor->function_decl.is<ConstructorDeclarationNode>()) {
		return;
	}

	fillInConstructorDefaultArguments(
	    ctor_op,
	    default_ctor->function_decl.as<ConstructorDeclarationNode>(),
	    ctor_op.arguments.size());
}

void AstToIr::fillInCachedDefaultArguments(CallOp& call_op, const std::vector<CachedParamInfo>& cached_params, size_t arg_idx) {
	for (size_t i = arg_idx; i < cached_params.size(); ++i) {
		const auto& param = cached_params[i];
		if (param.is_parameter_pack) {
			break;
		}
		if (!param.has_default_value) {
			throw InternalError("Missing default argument for parameter '" +
			                    std::string(StringTable::getStringView(param.name)) +
			                    "' (overload resolution should have rejected this call)");
		}

		const ASTNode& default_expr = param.default_value;
		if (!param.type_node.is<TypeSpecifierNode>()) {
			throw InternalError("Cached parameter type missing for default argument evaluation");
		}
		call_op.args.push_back(materializeDefaultArgument(
		    default_expr,
		    param.type_node.as<TypeSpecifierNode>(),
		    StringBuilder()
		        .append("cached parameter '")
		        .append(StringTable::getStringView(param.name))
		        .append("'")
		        .commit()));
	}
}

ExprResult AstToIr::generateTernaryOperatorIr(const TernaryOperatorNode& ternaryNode) {
	// Ternary operator: condition ? true_expr : false_expr
	// Generate IR:
	// 1. Evaluate condition
	// 2. Conditional branch to true or false label
	// 3. Label for true branch, evaluate true_expr, assign to result, jump to end
	// 4. Label for false branch, evaluate false_expr, assign to result
	// 5. Label for end (both branches merge here)

	// Generate unique labels for this ternary
	static size_t ternary_counter = 0;
	auto true_label = StringTable::createStringHandle(StringBuilder().append("ternary_true_").append(ternary_counter));
	auto false_label = StringTable::createStringHandle(StringBuilder().append("ternary_false_").append(ternary_counter));
	auto end_label = StringTable::createStringHandle(StringBuilder().append("ternary_end_").append(ternary_counter));
	ternary_counter++;

	// Evaluate the condition
	auto condition_operands = visitExpressionNode(ternaryNode.condition().as<ExpressionNode>());
	// C++20 [expr.cond]: contextual bool conversion.
	condition_operands = applyConditionBoolConversion(condition_operands, ternaryNode.condition(), ternaryNode.get_token());

	// Generate conditional branch: if condition true goto true_label, else goto false_label
	CondBranchOp cond_branch;
	cond_branch.label_true = true_label;
	cond_branch.label_false = false_label;
	cond_branch.condition = toTypedValue(condition_operands);
	ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), ternaryNode.get_token()));

	// True branch label
	ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = true_label}, ternaryNode.get_token()));

	// C++20 [expr.cond]/7: if the second and third operands have different
	// arithmetic types, the usual arithmetic conversions determine the common type.
	// The sema pass annotates each branch with a conversion to the common type.
	// We also try parser type inference as a fallback.
	TypeCategory common_cat = TypeCategory::Invalid;
	// Check sema annotations: if either branch has a conversion annotation, that
	// tells us the target (common) type.
	if (sema_) {
		TypeCategory true_cat = getSemaAnnotatedTargetType(ternaryNode.true_expr());
		TypeCategory false_cat = getSemaAnnotatedTargetType(ternaryNode.false_expr());
		if (true_cat != TypeCategory::Invalid)
			common_cat = true_cat;
		else if (false_cat != TypeCategory::Invalid)
			common_cat = false_cat;
	}
	// Fallback: try parser type inference
	if (common_cat == TypeCategory::Invalid && parser_) {
		auto true_ts = parser_->get_expression_type(ternaryNode.true_expr());
		auto false_ts = parser_->get_expression_type(ternaryNode.false_expr());
		if (true_ts.has_value() && false_ts.has_value())
			common_cat = get_common_type(true_ts->category(), false_ts->category());
	}

	// Evaluate true expression
	ExprResult true_result = visitExpressionNode(ternaryNode.true_expr().as<ExpressionNode>());

	// Finalize common_cat: if parser inference failed, fall back to true branch type
	if (common_cat == TypeCategory::Invalid)
		common_cat = true_result.category();
	TypeCategory common_type = common_cat;

	// Convert true result to common type if needed.
	// NOTE: sema annotations were already consumed above when determining common_type
	// via getSemaAnnotatedTargetType. The actual conversion uses common_type directly;
	// there is no need to re-query the annotation here since both paths would produce
	// the same generateTypeConversion call (sema_target == common_type by construction).
	if (true_result.typeEnum() != common_type)
		true_result = generateTypeConversion(true_result, true_result.category(), common_type, ternaryNode.get_token());

	int result_size = get_type_size_bits(common_type);
	if (result_size == 0)
		result_size = true_result.size_in_bits.value;

	// Create result variable to hold the final value
	TempVar result_var = var_counter.next();

	// Assign true_expr result to result variable
	AssignmentOp assign_true_op;
	assign_true_op.result = result_var;
	assign_true_op.lhs.setType(common_type);
	assign_true_op.lhs.size_in_bits = SizeInBits{result_size};
	assign_true_op.lhs.value = result_var;
	assign_true_op.rhs = toTypedValue(true_result);
	ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_true_op), ternaryNode.get_token()));

	// Unconditional branch to end
	ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = end_label}, ternaryNode.get_token()));

	// False branch label
	ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = false_label}, ternaryNode.get_token()));

	// Evaluate false expression
	ExprResult false_result = visitExpressionNode(ternaryNode.false_expr().as<ExpressionNode>());

	// Convert false result to common type if needed (same reasoning as true branch above).
	if (false_result.typeEnum() != common_type)
		false_result = generateTypeConversion(false_result, false_result.category(), common_type, ternaryNode.get_token());

	// Assign false_expr result to result variable
	AssignmentOp assign_false_op;
	assign_false_op.result = result_var;
	assign_false_op.lhs.setType(common_type);
	assign_false_op.lhs.size_in_bits = SizeInBits{result_size};
	assign_false_op.lhs.value = result_var;
	assign_false_op.rhs = toTypedValue(false_result);
	ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_false_op), ternaryNode.get_token()));

	// End label (merge point)
	ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = end_label}, ternaryNode.get_token()));

	// Return the result variable
	return makeExprResult(nativeTypeIndex(common_type), SizeInBits{result_size}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
}

ExprResult AstToIr::generateBinaryOperatorIr(const BinaryOperatorNode& binaryOperatorNode) {
	const auto& op = binaryOperatorNode.op();

	// Special handling for comma operator
	// The comma operator evaluates both operands left-to-right and returns the right operand
	if (op == ",") {
		// Generate IR for the left-hand side (evaluate for side effects, discard result)
		visitExpressionNode(binaryOperatorNode.get_lhs().as<ExpressionNode>());

		// Generate IR for the right-hand side (this is the result)
		ExprResult rhsExprResult = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

		// Return the right-hand side result
		return rhsExprResult;
	}

	// Special handling for assignment to array subscript or member access
	// Use LValueAddress context to avoid redundant Load instructions
	if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
		const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();

		// Check if LHS is an array subscript or member access (lvalue expressions)
		if (std::holds_alternative<ArraySubscriptNode>(lhs_expr) ||
		    std::holds_alternative<MemberAccessNode>(lhs_expr)) {

			// Evaluate LHS with LValueAddress context (no Load instruction)
			ExprResult lhsExprResult = visitExpressionNode(lhs_expr, ExpressionContext::LValueAddress);

			// Safety check: if LHS evaluation failed or returned invalid size, fall through to legacy code
			int lhs_size = lhsExprResult.size_in_bits.value;
			bool use_unified_handler = lhs_size > 0;
			if (use_unified_handler && lhs_size > 1024) {
				FLASH_LOG(Codegen, Info, "Unified handler skipped: invalid size (", lhs_size, ")");
				use_unified_handler = false; // Invalid size, use legacy code
			}

			if (use_unified_handler) {
				// Evaluate RHS normally (Load context)
				ExprResult rhsExprResult = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

				// Try to handle assignment using unified lvalue metadata handler
				if (handleLValueAssignment(lhsExprResult, rhsExprResult, binaryOperatorNode.get_token())) {
					// Assignment was handled successfully via metadata
					FLASH_LOG(Codegen, Info, "Unified handler SUCCESS for array/member assignment");
					return rhsExprResult;
				}

				// If metadata handler didn't work, fall through to legacy code
				// This shouldn't happen with proper metadata, but provides a safety net
				FLASH_LOG(Codegen, Info, "Unified handler returned false, falling through to legacy code");
			}
			// If use_unified_handler is false, fall through to legacy handlers below
		}
	}

	// Special handling for assignment to member variables in member functions
	// Now that implicit member access is marked with lvalue metadata, use unified handler
	if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>() && current_struct_name_.isValid()) {
		const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
			const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
			std::string_view lhs_name = lhs_ident.name();

			// Check if this is a member variable of the current struct
			auto type_it = getTypesByNameMap().find(current_struct_name_);
			if (type_it != getTypesByNameMap().end() && type_it->second->isStruct()) {
				TypeIndex struct_type_index = type_it->second->type_index_;
				auto member_result = FlashCpp::gLazyMemberResolver.resolve(struct_type_index, StringTable::getOrInternStringHandle(std::string(lhs_name)));
				if (member_result) {
					// This is an assignment to a member variable: member = value
					// Handle via unified handler (identifiers are now marked as lvalues)
					ExprResult lhsExprResult = visitExpressionNode(lhs_expr);
					ExprResult rhsExprResult = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

					// Handle assignment using unified lvalue metadata handler
					if (handleLValueAssignment(lhsExprResult, rhsExprResult, binaryOperatorNode.get_token())) {
						// Assignment was handled successfully via metadata
						FLASH_LOG(Codegen, Debug, "Unified handler SUCCESS for implicit member assignment (", lhs_name, ")");
						return rhsExprResult;
					}

					// This shouldn't happen with proper metadata, but log for debugging
					FLASH_LOG(Codegen, Error, "Unified handler unexpectedly failed for implicit member assignment: ", lhs_name);
					return makeExprResult(nativeTypeIndex(TypeCategory::Int), SizeInBits{32}, IrOperand{TempVar{0}}, PointerDepth{}, ValueStorage::ContainsData);
				}
			}
		}
	}

	// Captured-by-reference assignment.
	// Explicit [&x] captures have CapturedByRef binding set at parse time.
	// Capture-all [&] variables keep Local binding but appear in current_lambda_context_.capture_kinds.
	if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
		const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
			const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
			bool is_captured_by_ref = (lhs_ident.binding() == IdentifierBinding::CapturedByRef);
			if (!is_captured_by_ref && current_lambda_context_.isActive()) {
				StringHandle lhs_name_str = StringTable::getOrInternStringHandle(lhs_ident.name());
				auto capture_it = current_lambda_context_.captures.find(lhs_name_str);
				if (capture_it != current_lambda_context_.captures.end()) {
					auto kind_it = current_lambda_context_.capture_kinds.find(lhs_name_str);
					is_captured_by_ref = (kind_it != current_lambda_context_.capture_kinds.end() &&
					                      kind_it->second == LambdaCaptureNode::CaptureKind::ByReference);
				}
			}
			if (is_captured_by_ref) {
				std::string_view lhs_name = lhs_ident.name();
				ExprResult lhsExprResult = visitExpressionNode(lhs_expr);
				ExprResult rhsExprResult = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());
				if (handleLValueAssignment(lhsExprResult, rhsExprResult, binaryOperatorNode.get_token())) {
					FLASH_LOG(Codegen, Debug, "Unified handler SUCCESS for captured-by-reference assignment (", lhs_name, ")");
					return rhsExprResult;
				}
				FLASH_LOG(Codegen, Error, "Unified handler unexpectedly failed for captured-by-reference assignment: ", lhs_name);
				return makeExprResult(nativeTypeIndex(TypeCategory::Int), SizeInBits{32}, IrOperand{TempVar{0}}, PointerDepth{}, ValueStorage::ContainsData);
			}
		}
	}

	// Special handling for function pointer assignment
	if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
		const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
			const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
			std::string_view lhs_name = lhs_ident.name();

			// Look up the LHS in the symbol table
			const std::optional<ASTNode> lhs_symbol = symbol_table.lookup(lhs_name);
			if (lhs_symbol.has_value() && lhs_symbol->is<DeclarationNode>()) {
				const auto& lhs_decl = lhs_symbol->as<DeclarationNode>();
				const auto& lhs_type = lhs_decl.type_node().as<TypeSpecifierNode>();

				// Check if LHS is a function pointer
				if (lhs_type.is_function_pointer()) {
					// This is a function pointer assignment
					// Generate IR for the RHS (which should be a function address)
					ExprResult rhs_result = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

					// Generate Assignment IR using typed payload
					TempVar result_var = var_counter.next();
					AssignmentOp assign_op;
					assign_op.result = result_var;
					assign_op.lhs.setType(lhs_type.category());
					assign_op.lhs.size_in_bits = SizeInBits{lhs_type.size_in_bits()};
					assign_op.lhs.value = StringTable::getOrInternStringHandle(lhs_name);
					assign_op.rhs = toTypedValue(rhs_result);
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));

					// Return the result
					return makeExprResult(nativeTypeIndex(lhs_type.type()), SizeInBits{static_cast<int>(lhs_type.size_in_bits())}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
				}
			}
		}
	}

	// Helper: prefer sema annotation over local policy for operand conversions on global/static paths.
	// Returns true and applies the conversion when a non-struct sema slot exists for the node.
	// When expected_target is valid (not Invalid), the annotation's target type must match; otherwise
	// the helper returns false so the caller's fallback policy runs instead.
	auto tryGlobalSemaConv = [&](ExprResult& expr, const ASTNode& node, TypeCategory expected_cat = TypeCategory::Invalid) -> bool {
		if (!sema_ || !node.is<ExpressionNode>())
			return false;
		const void* key = &node.as<ExpressionNode>();
		const auto slot = sema_->getSlot(key);
		if (!slot.has_value() || !slot->has_cast())
			return false;
		const ImplicitCastInfo& ci = sema_->castInfoTable()[slot->cast_info_index.value - 1];
		TypeCategory from_t = sema_->typeContext().get(ci.source_type_id).category();
		const TypeCategory to_t = sema_->typeContext().get(ci.target_type_id).category();
		if (from_t == TypeCategory::Struct || to_t == TypeCategory::Struct)
			return false;
		if (expected_cat != TypeCategory::Invalid && to_t != expected_cat)
			return false;
		// Defensive: sema source type should match the expression's runtime type.
		// Exception: sema may annotate as Enum while codegen resolves enum
		// constants to their underlying type early (via tryMakeEnumeratorConstantExpr).
		if (from_t != expr.typeEnum()) {
			if (from_t == TypeCategory::Enum)
				from_t = expr.typeEnum();
			else
				throw InternalError("sema annotation source type does not match expr.type");
		}
		expr = generateTypeConversion(expr, from_t, to_t, binaryOperatorNode.get_token());
		return true;
	};

	auto makeGlobalAssignmentResultLValue = [&](const GlobalStaticBindingInfo& binding) -> ExprResult {
		TempVar result_temp = var_counter.next();
		GlobalLoadOp load_op;
		load_op.result.setType(binding.type_index.category());
		load_op.result.ir_type = toIrType(binding.bindingType());
		load_op.result.size_in_bits = binding.size_in_bits;
		load_op.result.value = result_temp;
		load_op.global_name = binding.store_name;
		ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(load_op), binaryOperatorNode.get_token()));

		setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(
		                                    LValueInfo(LValueInfo::Kind::Global, binding.store_name, 0),
		                                    binding.type_index.category(), binding.size_in_bits.value));

		return makeExprResult(nativeTypeIndex(binding.bindingType()), binding.size_in_bits, IrOperand{result_temp}, PointerDepth{}, ValueStorage::ContainsData);
	};

	auto tryGetGlobalLValueName = [&](const ExprResult& expr_result) -> std::optional<StringHandle> {
		const auto* temp_var = std::get_if<TempVar>(&expr_result.value);
		if (!temp_var) {
			return std::nullopt;
		}
		if (auto lvalue_info = getTempVarLValueInfo(*temp_var);
		    lvalue_info.has_value() &&
		    lvalue_info->kind == LValueInfo::Kind::Global &&
		    std::holds_alternative<StringHandle>(lvalue_info->base)) {
			return std::get<StringHandle>(lvalue_info->base);
		}
		return std::nullopt;
	};

	// Special handling for global variable and static local variable assignment
	if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
		const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
			const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
			const auto gsi = resolveGlobalOrStaticBinding(lhs_ident);

			if (gsi.is_global_or_static && !isIrStructType(toIrType(gsi.bindingType()))) {
				// This is a global variable or static local assignment - generate GlobalStore instruction
				// Generate IR for the RHS
				ExprResult rhsExprResult = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

				// C++20 [expr.ass]: convert RHS to LHS type if they differ.
				// Phase 15: sema should annotate global/static assignment conversions.
				if (!tryGlobalSemaConv(rhsExprResult, binaryOperatorNode.get_rhs(), gsi.type_index.category()) &&
				    rhsExprResult.typeEnum() != gsi.bindingType() && gsi.type_index.category() != TypeCategory::Void) {
					if (sema_normalized_current_function_ && is_standard_arithmetic_type(rhsExprResult.typeEnum()) && is_standard_arithmetic_type(gsi.type_index.category()))
						throw InternalError(std::string("Phase 15: sema missed global/static assignment (") + std::string(getTypeName(rhsExprResult.typeEnum())) + " -> " + std::string(getTypeName(gsi.bindingType())) + ")");
					rhsExprResult = generateTypeConversion(rhsExprResult, rhsExprResult.category(), gsi.type_index.category(), binaryOperatorNode.get_token());
				}

				// Materialize the final assigned value into a stack temp before GlobalStore.
				// This mirrors the compound-assignment path and avoids backend register-tracking
				// gaps when the RHS comes from a conversion result that only lives in a register.
				TempVar store_temp = var_counter.next();
				AssignmentOp assign_op;
				assign_op.result = store_temp;
				assign_op.lhs.setType(gsi.type_index.category());
				assign_op.lhs.size_in_bits = gsi.size_in_bits;
				assign_op.lhs.value = store_temp;
				assign_op.rhs = toTypedValue(rhsExprResult);
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));

				std::vector<IrOperand> store_operands;
				store_operands.emplace_back(gsi.store_name);
				store_operands.emplace_back(store_temp);

				ir_.addInstruction(IrOpcode::GlobalStore, std::move(store_operands), binaryOperatorNode.get_token());

				// C++20 [expr.ass]/3: the result is an lvalue referring to the left operand.
				return makeGlobalAssignmentResultLValue(gsi);
			}
		}
	}

	// Special handling for compound assignment to global/static local variables
	// (e.g., static int n = 0; n += 21;)
	if (isCompoundAssignmentOp(op) &&
	    binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
		const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
			const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
			const auto gsi = resolveGlobalOrStaticBinding(lhs_ident);

			if (gsi.is_global_or_static && gsi.type_index.category() != TypeCategory::Void && gsi.size_in_bits.is_set()) {
				// Load current value from global
				TempVar loaded = var_counter.next();
				GlobalLoadOp load_op;
				load_op.result.setType(gsi.type_index.category());
				load_op.result.ir_type = toIrType(gsi.bindingType());
				load_op.result.size_in_bits = gsi.size_in_bits;
				load_op.result.value = loaded;
				load_op.global_name = gsi.store_name;
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(load_op), binaryOperatorNode.get_token()));

				// Evaluate RHS
				ExprResult rhs_result = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

				// Map compound op to arithmetic opcode
				const auto base_opcode = compoundOpToBaseOpcode(op);
				if (!base_opcode.has_value()) {
					FLASH_LOG(Codegen, Error, "Unsupported compound assignment operator for global: ", op);
					return ExprResult{};
				}

				// C++20 [expr.shift]: shift operands undergo independent integral promotions,
				// NOT usual arithmetic conversions. The result type is the promoted LHS type.
				// All other operators use usual arithmetic conversions per [expr.ass]/7.
				const bool is_shift_op = (op == "<<=" || op == ">>=");
				const TypeCategory commonType = is_shift_op
				                                    ? promote_integer_type(gsi.type_index.category())
				                                    : get_common_type(gsi.type_index.category(), rhs_result.category());

				// Reject floating-point LHS early for shift ops (C++20 [expr.shift]/1).
				if (is_shift_op && is_floating_point_type(gsi.type_index.category()))
					throw CompileError("Shift compound assignment is not defined for floating-point operands (C++20 [expr.shift]/1)");

				ExprResult lhs_operand = makeExprResult(nativeTypeIndex(gsi.type_index.category()), gsi.size_in_bits, IrOperand{loaded}, PointerDepth{}, ValueStorage::ContainsData);
				if (gsi.type_index.category() != commonType) {
					if (!tryGlobalSemaConv(lhs_operand, binaryOperatorNode.get_lhs(), commonType)) {
						if (sema_normalized_current_function_ && is_standard_arithmetic_type(gsi.type_index.category()) && is_standard_arithmetic_type(commonType))
							throw InternalError(std::string("Phase 15: sema missed compound assign global LHS (") + std::string(getTypeName(gsi.type_index.category())) + " -> " + std::string(getTypeName(commonType)) + ")");
						lhs_operand = generateTypeConversion(lhs_operand, gsi.type_index.category(), commonType, binaryOperatorNode.get_token());
					}
				}
				// C++20 [expr.shift]: shift RHS undergoes independent integral promotion,
				// NOT conversion to the LHS/result type. Other operators convert RHS to commonType.
				// Phase 15: prefer sema annotation; log warning on fallback.
				if (is_shift_op) {
					// Reject float RHS before promotion to avoid unnecessary conversion work.
					if (is_floating_point_type(rhs_result.typeEnum()))
						throw CompileError("Shift compound assignment is not defined for floating-point operands (C++20 [expr.shift]/1)");
					const TypeCategory promoted_rhs = promote_integer_type(rhs_result.category());
					if (rhs_result.category() != promoted_rhs) {
						if (!tryGlobalSemaConv(rhs_result, binaryOperatorNode.get_rhs())) {
							if (sema_normalized_current_function_ && is_standard_arithmetic_type(rhs_result.typeEnum()))
								throw InternalError(std::string("Phase 15: sema missed shift RHS promotion (") + std::string(getTypeName(rhs_result.typeEnum())) + " -> " + std::string(getTypeName(promoted_rhs)) + ")");
							rhs_result = generateTypeConversion(rhs_result, rhs_result.category(), promoted_rhs, binaryOperatorNode.get_token());
						}
					}
				} else if (rhs_result.category() != commonType) {
					if (!tryGlobalSemaConv(rhs_result, binaryOperatorNode.get_rhs(), commonType)) {
						if (sema_normalized_current_function_ && is_standard_arithmetic_type(rhs_result.typeEnum()) && is_standard_arithmetic_type(commonType))
							throw InternalError(std::string("Phase 15: sema missed compound assign global RHS (") + std::string(getTypeName(rhs_result.typeEnum())) + " -> " + std::string(getTypeName(commonType)) + ")");
						rhs_result = generateTypeConversion(rhs_result, rhs_result.category(), commonType, binaryOperatorNode.get_token());
					}
				}

				// Select the correct opcode for the common type.
				IrOpcode arith_opcode = *base_opcode;
				if (is_floating_point_type(commonType)) {
					if (arith_opcode == IrOpcode::Modulo)
						throw CompileError("Operator %= is not defined for floating-point operands (C++20 [expr.mul]/4)");
					if (arith_opcode == IrOpcode::BitwiseAnd || arith_opcode == IrOpcode::BitwiseOr || arith_opcode == IrOpcode::BitwiseXor)
						throw CompileError("Bitwise compound assignment is not defined for floating-point operands");
					// Shifts on floating-point are ill-formed; the RHS check above catches the
					// float-RHS case; this catches float-LHS (e.g., float g; g <<= 1;).
					if (arith_opcode == IrOpcode::ShiftLeft || arith_opcode == IrOpcode::ShiftRight)
						throw CompileError("Shift compound assignment is not defined for floating-point operands (C++20 [expr.shift]/1)");
					if (arith_opcode == IrOpcode::Add)
						arith_opcode = IrOpcode::FloatAdd;
					else if (arith_opcode == IrOpcode::Subtract)
						arith_opcode = IrOpcode::FloatSubtract;
					else if (arith_opcode == IrOpcode::Multiply)
						arith_opcode = IrOpcode::FloatMultiply;
					else if (arith_opcode == IrOpcode::Divide)
						arith_opcode = IrOpcode::FloatDivide;
				} else if (is_unsigned_integer_type(commonType)) {
					if (arith_opcode == IrOpcode::Divide)
						arith_opcode = IrOpcode::UnsignedDivide;
					else if (arith_opcode == IrOpcode::Modulo)
						arith_opcode = IrOpcode::UnsignedModulo;
					else if (arith_opcode == IrOpcode::ShiftRight)
						arith_opcode = IrOpcode::UnsignedShiftRight;
				}

				// Perform the operation in common type
				TempVar result_var = var_counter.next();
				BinaryOp bin_op{
				    .lhs = toTypedValue(lhs_operand),
				    .rhs = toTypedValue(rhs_result),
				    .result = result_var,
				};
				ir_.addInstruction(IrInstruction(arith_opcode, std::move(bin_op), binaryOperatorNode.get_token()));

				// Convert result back to global's type if needed
				ExprResult op_result = makeExprResult(nativeTypeIndex(commonType), SizeInBits{get_type_size_bits(commonType)}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
				if (commonType != gsi.type_index.category()) {
					// Phase 17: verify sema annotated the back-conversion.
					if (sema_ && sema_normalized_current_function_ &&
					    is_standard_arithmetic_type(commonType) && is_standard_arithmetic_type(gsi.type_index.category())) {
						auto back_conv = sema_->getCompoundAssignBackConv(static_cast<const void*>(&binaryOperatorNode));
						if (!back_conv.has_value())
							throw InternalError(std::string("Phase 17: sema missed global compound assign back-conversion (") + std::string(getTypeName(commonType)) + " -> " + std::string(getTypeName(gsi.type_index.category())) + ")");
					}
					op_result = generateTypeConversion(op_result, commonType, gsi.type_index.category(), binaryOperatorNode.get_token());
				}

				// Materialize the conversion result into a stack-flushed temporary
				// so that GlobalStore can safely read it. This works around the
				// register-tracking gap where GlobalStore reads from the stack slot
				// but generateTypeConversion may leave the result only in a register.
				// The AssignmentOp pattern (result == lhs.value == store_temp) is the
				// standard IR idiom: "allocate store_temp, then store op_result into it."
				TempVar store_temp = var_counter.next();
				{
					AssignmentOp mat;
					mat.result = store_temp;
					mat.lhs = makeTypedValue(gsi.type_index.category(), gsi.size_in_bits, IrValue{store_temp});
					mat.rhs = toTypedValue(op_result);
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(mat), binaryOperatorNode.get_token()));
				}

				// Store result back to global
				std::vector<IrOperand> store_operands;
				store_operands.emplace_back(gsi.store_name);
				store_operands.emplace_back(store_temp);
				ir_.addInstruction(IrOpcode::GlobalStore, std::move(store_operands), binaryOperatorNode.get_token());

				return makeGlobalAssignmentResultLValue(gsi);
			}
		}
	}

	// Special handling for compound assignment to array subscript or member access
	// Use LValueAddress context for the LHS, similar to regular assignment
	// Helper lambda to check if operator is a compound assignment
	if (isCompoundAssignmentOp(op) &&
	    binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
		const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();

		// Check if LHS is an array subscript or member access (lvalue expressions)
		if (std::holds_alternative<ArraySubscriptNode>(lhs_expr) ||
		    std::holds_alternative<MemberAccessNode>(lhs_expr)) {

			// Evaluate LHS with LValueAddress context (no Load instruction)
			ExprResult lhsExprResult = visitExpressionNode(lhs_expr, ExpressionContext::LValueAddress);

			// Safety check
			int lhs_size = lhsExprResult.size_in_bits.value;
			bool use_unified_handler = lhs_size > 0;
			if (use_unified_handler && lhs_size > 1024) {
				FLASH_LOG(Codegen, Info, "Compound assignment unified handler skipped: invalid size (", lhs_size, ")");
				use_unified_handler = false;
			}

			if (use_unified_handler) {
				// Evaluate RHS normally (Load context)
				ExprResult rhsExprResult = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

				// For compound assignments, we need to:
				// 1. Load the current value from the lvalue
				// 2. Perform the operation (add, subtract, etc.)
				// 3. Store the result back to the lvalue

				// Try to handle compound assignment using lvalue metadata
				if (handleLValueCompoundAssignment(lhsExprResult, rhsExprResult, binaryOperatorNode.get_token(), op)) {
					// Compound assignment was handled successfully via metadata
					FLASH_LOG(Codegen, Info, "Unified handler SUCCESS for array/member compound assignment");
					// Return the LHS operands which contain the result type/size info
					// The actual result value is stored in the lvalue, so we return lvalue info
					return lhsExprResult;
				}

				// If metadata handler didn't work, fall through to legacy code
				FLASH_LOG(Codegen, Info, "Compound assignment unified handler returned false, falling through to legacy code");
			}
		}
	}

	// Generate IR for the left-hand side and right-hand side of the operation
	// For assignment (=), use LValueAddress context for LHS to avoid dereferencing reference parameters
	ExpressionContext lhs_context = (op == "=") ? ExpressionContext::LValueAddress : ExpressionContext::Load;
	ExprResult lhsExprResult = visitExpressionNode(binaryOperatorNode.get_lhs().as<ExpressionNode>(), lhs_context);
	ExprResult rhsExprResult = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

	// Try unified metadata-based handler for compound assignments on identifiers
	// This ensures implicit member accesses (including [*this] lambdas) use the correct base object
	if (isCompoundAssignmentOp(op) &&
	    handleLValueCompoundAssignment(lhsExprResult, rhsExprResult, binaryOperatorNode.get_token(), op)) {
		FLASH_LOG(Codegen, Info, "Unified handler SUCCESS for compound assignment");
		return lhsExprResult;
	}

	// Try unified lvalue-based assignment handler (uses value category metadata)
	// This handles assignments like *ptr = value using lvalue metadata
	if (op == "=" && handleLValueAssignment(lhsExprResult, rhsExprResult, binaryOperatorNode.get_token())) {
		// Assignment was handled via lvalue metadata, return RHS as result
		return rhsExprResult;
	}

	// Get the types and sizes of the operands
	int lhsSize = lhsExprResult.size_in_bits.value;
	int rhsSize = rhsExprResult.size_in_bits.value;
	TypeCategory lhsCat = lhsExprResult.category();
	TypeCategory rhsCat = rhsExprResult.category();

	auto tryGetBinaryOperatorTypeSpecs = [&]() -> std::optional<std::pair<TypeSpecifierNode, TypeSpecifierNode>> {
		if (!parser_) {
			return std::nullopt;
		}

		auto left_type_spec = parser_->get_expression_type(binaryOperatorNode.get_lhs());
		auto right_type_spec = parser_->get_expression_type(binaryOperatorNode.get_rhs());
		if (!left_type_spec.has_value() || !right_type_spec.has_value()) {
			return std::nullopt;
		}

		adjust_argument_type_for_overload_resolution(binaryOperatorNode.get_lhs(), *left_type_spec);
		adjust_argument_type_for_overload_resolution(binaryOperatorNode.get_rhs(), *right_type_spec);
		return std::make_pair(*left_type_spec, *right_type_spec);
	};

	auto requiresUserDefinedBinaryOperator = [](const TypeSpecifierNode& type_spec) {
		if (type_spec.pointer_depth() > 0 || type_spec.is_function_pointer() || type_spec.is_member_function_pointer() || type_spec.is_member_object_pointer()) {
			return false;
		}
		TypeCategory base_type = resolve_type_alias(type_spec.type_index());
		return isIrStructType(toIrType(base_type)) && type_spec.type_index().is_valid();
	};

	auto requiresUserDefinedBinaryOperatorByBase = [](TypeIndex type_index) {
		TypeCategory resolved = resolve_type_alias(type_index);
		return isIrStructType(toIrType(resolved)) && type_index.is_valid();
	};

	auto makeReferenceArgument = [&](const ExprResult& operand_result, TypeCategory operand_type, int operand_size) -> std::optional<TypedValue> {
		TypedValue arg;
		arg.setType(operand_type);
		arg.size_in_bits = SizeInBits{64};

		if (const auto* string = std::get_if<StringHandle>(&operand_result.value)) {
			arg.value = emitAddressOf(operand_type, operand_size, IrValue(*string));
			return arg;
		}

		if (std::holds_alternative<TempVar>(operand_result.value)) {
			TempVar temp_var = std::get<TempVar>(operand_result.value);
			if (auto global_name = tryGetGlobalLValueName(operand_result); global_name.has_value()) {
				arg.value = emitAddressOf(operand_type, operand_size, IrValue(*global_name));
				return arg;
			}
			bool is_already_address = false;

			auto& metadata_storage = GlobalTempVarMetadataStorage::instance();
			if (metadata_storage.hasMetadata(temp_var)) {
				TempVarMetadata metadata = metadata_storage.getMetadata(temp_var);
				if (metadata.category == ValueCategory::LValue || metadata.category == ValueCategory::XValue) {
					is_already_address = true;
				}
			}

			if (!is_already_address && operand_size == 64 && operand_type == TypeCategory::Struct) {
				is_already_address = true;
			}

			arg.value = is_already_address
			                ? IrValue(temp_var)
			                : emitAddressOf(operand_type, operand_size, IrValue(temp_var));
			return arg;
		}

		bool is_literal = std::holds_alternative<unsigned long long>(operand_result.value) || std::holds_alternative<double>(operand_result.value);
		if (!is_literal) {
			return std::nullopt;
		}

		TempVar temp_var = var_counter.next();
		AssignmentOp assign_op;
		assign_op.result = temp_var;
		assign_op.lhs = makeTypedValue(operand_type, SizeInBits{static_cast<int>(operand_size)}, temp_var);

		IrValue rhs_value;
		if (const auto* ull_val = std::get_if<unsigned long long>(&operand_result.value)) {
			rhs_value = *ull_val;
		} else {
			rhs_value = std::get<double>(operand_result.value);
		}
		assign_op.rhs = makeTypedValue(operand_type, SizeInBits{static_cast<int>(operand_size)}, rhs_value);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));

		arg.value = emitAddressOf(operand_type, operand_size, IrValue(temp_var));
		return arg;
	};

	// Special handling for struct assignment with user-defined operator=(non-struct)
	// This handles patterns like: struct_var = primitive_value
	// where struct has operator=(int), operator=(double), etc.
	if (op == "=" && lhsCat == TypeCategory::Struct &&
	    rhsCat != TypeCategory::Struct &&
	    !rhsExprResult.type_index.is_valid() &&
	    lhsExprResult.type_index.is_valid()) {
		// Get the type index of the struct
		TypeIndex lhs_type_index = lhsExprResult.type_index;
		TypeIndex rhs_type_index = carriesSemanticTypeIndex(rhsCat)
		                               ? rhsExprResult.type_index
		                               : TypeIndex{};

		if (lhs_type_index.is_valid()) {
			// Check for user-defined operator= that takes the RHS type
			OperatorOverloadResult overload_result;
			if (binaryOperatorNode.has_ambiguous_operator_overload()) {
				overload_result = OperatorOverloadResult::ambiguous();
			} else if (binaryOperatorNode.has_resolved_member_operator_overload()) {
				overload_result = OperatorOverloadResult(binaryOperatorNode.resolved_member_operator_overload());
			} else if (binaryOperatorNode.has_no_match_operator_overload()) {
				overload_result = OperatorOverloadResult::no_overload();
			} else {
				if (auto type_specs = tryGetBinaryOperatorTypeSpecs(); type_specs.has_value()) {
					overload_result = findBinaryOperatorOverload(type_specs->first, type_specs->second, OverloadableOperator::Assign);
				} else {
					overload_result = findBinaryOperatorOverload(lhs_type_index, rhs_type_index, OverloadableOperator::Assign, rhsCat);
				}
			}

			if (overload_result.is_ambiguous) {
				throw CompileError("Ambiguous overload for operator=");
			}

			if (overload_result.has_match) {
				const StructMemberFunction& member_func = *overload_result.member_overload;
				const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
				if (lhs_type_index.is_valid()) {
					if (const StructTypeInfo* struct_info = getTypeInfo(lhs_type_index).getStructInfo()) {
						if (auto same_type_assignment_kind = getSameTypeAssignmentKind(*struct_info, func_decl);
						    same_type_assignment_kind.has_value()) {
							diagnoseDeletedSameTypeAssignmentUsage(*struct_info, *same_type_assignment_kind);
						}
					}
				}
				if (func_decl.is_deleted()) {
					throw CompileError("Call to deleted function 'operator='");
				}

				// Check if the parameter type matches RHS type
				const auto& param_nodes = func_decl.parameter_nodes();
				if (!param_nodes.empty() && param_nodes[0].is<DeclarationNode>()) {
					const auto& param_decl = param_nodes[0].as<DeclarationNode>();
					const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();

					// Check if parameter is a primitive type matching RHS
					if (!isIrStructType(toIrType(param_type.type()))) {
						// Found matching operator=(primitive_type)! Generate function call
						FLASH_LOG_FORMAT(Codegen, Debug, "Found operator= with primitive param for struct type index {}", lhs_type_index);

						std::string_view struct_name = StringTable::getStringView(getTypeInfo(lhs_type_index).name());
						const TypeSpecifierNode& return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();

						// Get parameter types for mangling
						std::vector<TypeSpecifierNode> param_types;
						param_types.push_back(param_type);

						// Generate mangled name for operator=
						std::vector<std::string_view> empty_namespace;
						auto mangled_name = NameMangling::generateMangledName(
						    "operator=",
						    return_type,
						    param_types,
						    false, // not variadic
						    struct_name,
						    empty_namespace,
						    Linkage::CPlusPlus,
						    member_func.is_const());

						TempVar result_var = var_counter.next();

						// Take address of LHS to pass as 'this' pointer
						std::variant<StringHandle, TempVar> lhs_value;
						if (const auto* string = std::get_if<StringHandle>(&lhsExprResult.value)) {
							lhs_value = *string;
						} else if (const auto* temp_var = std::get_if<TempVar>(&lhsExprResult.value)) {
							if (auto global_name = tryGetGlobalLValueName(lhsExprResult); global_name.has_value()) {
								lhs_value = *global_name;
							} else {
								lhs_value = *temp_var;
							}
						} else {
							FLASH_LOG(Codegen, Error, "Cannot take address of operator= LHS - not an lvalue");
							return ExprResult{};
						}

						TempVar lhs_addr = var_counter.next();
						AddressOfOp addr_op;
						addr_op.result = lhs_addr;
						addr_op.operand.setType(lhsCat);
						addr_op.operand.ir_type = toIrType(lhsCat);
						addr_op.operand.size_in_bits = SizeInBits{lhsSize};
						addr_op.operand.pointer_depth = PointerDepth{};
						std::visit([&addr_op](auto&& val) { addr_op.operand.value = val; }, lhs_value);
						ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), binaryOperatorNode.get_token()));

						// Generate function call
						CallOp call_op;
						call_op.result = result_var;
						call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);

						// Pass 'this' pointer as first argument
						TypedValue this_arg;
						this_arg.setType(lhsCat);
						this_arg.ir_type = toIrType(lhsCat);
						this_arg.size_in_bits = SizeInBits{64}; // 'this' is always a pointer (64-bit)
						this_arg.value = lhs_addr;
						call_op.args.push_back(this_arg);

						// Pass RHS value as second argument
						call_op.args.push_back(toTypedValue(rhsExprResult));

						call_op.return_type_index = return_type.type_index();
						call_op.return_size_in_bits = SizeInBits{static_cast<int>(return_type.size_in_bits())};

						ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), binaryOperatorNode.get_token()));

						// Return result
						return makeExprResult(nativeTypeIndex(return_type.type()), SizeInBits{static_cast<int>(return_type.size_in_bits())}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
					}
				}
			}
		}
	}

	// Check for binary operator overloads when either operand carries a user-defined type identity
	// Binary operators like +, -, *, etc. can be overloaded as member or free functions
	// This should be checked before trying to generate built-in arithmetic operations
	TypeIndex lhs_type_index = lhsExprResult.type_index;
	TypeIndex rhs_type_index = rhsExprResult.type_index;

	auto tryGetConcreteBinaryOperatorTypeSpecs = [&]() -> std::optional<std::pair<TypeSpecifierNode, TypeSpecifierNode>> {
		auto type_specs = tryGetBinaryOperatorTypeSpecs();
		if (!type_specs.has_value()) {
			return std::nullopt;
		}

		auto patchTypeSpecFromIr = [](TypeSpecifierNode& type_spec, TypeIndex ir_type_index) {
			if (!ir_type_index.is_valid()) {
				return;
			}

			TypeCategory resolved_ir_type = resolve_type_alias(ir_type_index);
			if (ir_type_index.is_valid()) {
				resolved_ir_type = resolve_type_alias(ir_type_index);
			}
			if (!binaryOperatorUsesTypeIndexIdentity(resolved_ir_type)) {
				return;
			}

			TypeCategory effective_spec_type = effectiveBinaryOperatorTypeFromSpec(type_spec);
			if (!binaryOperatorUsesTypeIndexIdentity(effective_spec_type) || type_spec.type_index() != ir_type_index) {
				type_spec.set_category(resolved_ir_type);
				type_spec.set_type_index(ir_type_index);
			}
		};

		patchTypeSpecFromIr(type_specs->first, lhs_type_index);
		patchTypeSpecFromIr(type_specs->second, rhs_type_index);
		return type_specs;
	};

	auto normalizeSyntaxTypeSpec = [](const TypeSpecifierNode& type_spec) {
		if (const TypeInfo* owner_type_info = tryGetTypeInfo(type_spec.type_index())) {
			if (const StructTypeInfo* owner_struct = owner_type_info->getStructInfo()) {
				std::string_view token_name = type_spec.token().value();
				if (!token_name.empty() && token_name != StringTable::getStringView(owner_struct->name)) {
					StringHandle qualified_alias_handle = StringTable::getOrInternStringHandle(
					    StringBuilder().append(owner_struct->name).append("::").append(token_name).commit());
					auto alias_it = getTypesByNameMap().find(qualified_alias_handle);
					if (alias_it != getTypesByNameMap().end() && alias_it->second != nullptr) {
						const TypeInfo& alias_type_info = *alias_it->second;
						TypeSpecifierNode resolved(alias_type_info.typeEnum(), TypeQualifier::None, alias_type_info.type_size_, type_spec.token(), type_spec.cv_qualifier());
						resolved.set_type_index(alias_type_info.type_index_);
						resolved.copy_indirection_from(type_spec);
						resolved.set_reference_qualifier(type_spec.reference_qualifier());
						return resolved;
					}
				}
			}
		}
		return type_spec;
	};
	auto typeSpecRequiresUserDefinedOperator = [&](const TypeSpecifierNode& raw_type_spec) {
		const TypeSpecifierNode type_spec = normalizeSyntaxTypeSpec(raw_type_spec);
		if (type_spec.pointer_depth() > 0) {
			return false;
		}
		TypeCategory semantic_type = resolve_type_alias(type_spec.type_index());
		if (carriesSemanticTypeIndex(semantic_type)) {
			return true;
		}
		if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
			if (type_info->getStructInfo() || type_info->getEnumInfo()) {
				return true;
			}
			TypeCategory indexed_type = resolve_type_alias(type_spec.type_index());
			if (carriesSemanticTypeIndex(indexed_type)) {
				return true;
			}
		}
		return false;
	};
	auto syntaxOperandTypeSpec = [&](const ASTNode& operand) -> std::optional<TypeSpecifierNode> {
		if (!operand.is<ExpressionNode>()) {
			return std::nullopt;
		}

		const ExpressionNode& expr = operand.as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(expr)) {
			const auto& id = std::get<IdentifierNode>(expr);
			if (id.name() == "nullptr") {
				return TypeSpecifierNode(TypeCategory::Nullptr, TypeQualifier::None, 64, id.identifier_token(), CVQualifier::None);
			}
			if (auto symbol = symbol_table.lookup(id.name())) {
				const DeclarationNode* decl_ptr = nullptr;
				if (symbol->is<VariableDeclarationNode>()) {
					decl_ptr = &symbol->as<VariableDeclarationNode>().declaration();
				} else if (symbol->is<DeclarationNode>()) {
					decl_ptr = &symbol->as<DeclarationNode>();
				}
				if (decl_ptr && decl_ptr->type_node().is<TypeSpecifierNode>()) {
					return normalizeSyntaxTypeSpec(decl_ptr->type_node().as<TypeSpecifierNode>());
				}
			}
			return std::nullopt;
		}
		if (const auto* cast = std::get_if<StaticCastNode>(&expr)) {
			if (cast->target_type().is<TypeSpecifierNode>()) {
				return normalizeSyntaxTypeSpec(cast->target_type().as<TypeSpecifierNode>());
			}
			return std::nullopt;
		}
		if (const auto* literal = std::get_if<NumericLiteralNode>(&expr)) {
			return TypeSpecifierNode(literal->type(), literal->qualifier(), literal->sizeInBits(), Token{}, CVQualifier::None);
		}
		if (std::holds_alternative<BoolLiteralNode>(expr)) {
			return TypeSpecifierNode(TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::None);
		}
		if (std::holds_alternative<StringLiteralNode>(expr)) {
			TypeSpecifierNode str_type(TypeCategory::Char, TypeQualifier::None, 8, Token{}, CVQualifier::None);
			str_type.add_pointer_level(CVQualifier::Const);
			return str_type;
		}
		return std::nullopt;
	};

	auto concrete_type_specs = tryGetConcreteBinaryOperatorTypeSpecs();
	if (!concrete_type_specs.has_value()) {
		auto lhs_syntax_type = syntaxOperandTypeSpec(binaryOperatorNode.get_lhs());
		auto rhs_syntax_type = syntaxOperandTypeSpec(binaryOperatorNode.get_rhs());
		if (lhs_syntax_type.has_value() && rhs_syntax_type.has_value()) {
			concrete_type_specs = std::make_pair(*lhs_syntax_type, *rhs_syntax_type);
		}
	}
	bool concrete_operands_require_user_defined_operator =
	    concrete_type_specs.has_value() && (isUserDefinedBinaryOperatorOperandType(concrete_type_specs->first) || isUserDefinedBinaryOperatorOperandType(concrete_type_specs->second));
	auto syntaxOperandRequiresUserDefinedOperator = [&](const ASTNode& operand) -> std::optional<bool> {
		auto type_spec = syntaxOperandTypeSpec(operand);
		if (type_spec.has_value()) {
			return typeSpecRequiresUserDefinedOperator(*type_spec);
		}
		return std::nullopt;
	};

	bool lhs_has_user_defined_identity = false;
	bool rhs_has_user_defined_identity = false;
	std::optional<bool> lhs_syntax_requires_user_defined;
	std::optional<bool> rhs_syntax_requires_user_defined;
	if (concrete_type_specs.has_value()) {
		// Keep the lowered IR operand types/sizes for built-in arithmetic.
		// The parser's concrete syntax type specs are only for overload resolution;
		// overwriting lhsType/rhsType here can corrupt nested arithmetic chains by
		// reusing pre-conversion syntax types instead of the actual subexpression result types.
		lhs_has_user_defined_identity = concrete_operands_require_user_defined_operator && isUserDefinedBinaryOperatorOperandType(concrete_type_specs->first);
		rhs_has_user_defined_identity = concrete_operands_require_user_defined_operator && isUserDefinedBinaryOperatorOperandType(concrete_type_specs->second);
	} else {
		auto hasUserDefinedIdentityFromIr = [](TypeIndex type_index) {
			if (!type_index.is_valid() || type_index.index() >= getTypeInfoCount()) {
				return false;
			}
			const TypeInfo& type_info = getTypeInfo(type_index);
			TypeCategory semantic_type = resolve_type_alias(type_index);
			if (carriesSemanticTypeIndex(semantic_type)) {
				return true;
			}
			if (type_info.getStructInfo() || type_info.getEnumInfo()) {
				return true;
			}
			TypeCategory indexed_type = resolve_type_alias(type_index);
			return carriesSemanticTypeIndex(indexed_type);
		};

		lhs_has_user_defined_identity = hasUserDefinedIdentityFromIr(lhs_type_index);
		rhs_has_user_defined_identity = hasUserDefinedIdentityFromIr(rhs_type_index);
		lhs_syntax_requires_user_defined = syntaxOperandRequiresUserDefinedOperator(binaryOperatorNode.get_lhs());
		rhs_syntax_requires_user_defined = syntaxOperandRequiresUserDefinedOperator(binaryOperatorNode.get_rhs());
		if (lhs_syntax_requires_user_defined.has_value()) {
			lhs_has_user_defined_identity = *lhs_syntax_requires_user_defined;
		}
		if (rhs_syntax_requires_user_defined.has_value()) {
			rhs_has_user_defined_identity = *rhs_syntax_requires_user_defined;
		}
	}

	bool recorded_overload_still_relevant = binaryOperatorNode.has_recorded_operator_overload_resolution();
	if (concrete_type_specs.has_value() && !concrete_operands_require_user_defined_operator) {
		recorded_overload_still_relevant = false;
	} else if (!concrete_type_specs.has_value() && lhs_syntax_requires_user_defined.has_value() && rhs_syntax_requires_user_defined.has_value() && !lhs_has_user_defined_identity && !rhs_has_user_defined_identity) {
		recorded_overload_still_relevant = false;
	}
	OverloadableOperator op_kind = stringToOverloadableOperator(op);
	bool should_attempt_operator_overload = isOverloadableBinaryOperator(op_kind) && (concrete_type_specs.has_value()
	                                                                                      ? concrete_operands_require_user_defined_operator
	                                                                                      : (lhs_has_user_defined_identity || rhs_has_user_defined_identity || recorded_overload_still_relevant));
	bool can_try_spaceship_rewrite = false;

	if (should_attempt_operator_overload) {
		// Check for operator overload (member function or free function)
		OperatorOverloadResult overload_result;
		bool can_recompute_recorded_failure =
		    (binaryOperatorNode.has_ambiguous_operator_overload() || binaryOperatorNode.has_no_match_operator_overload()) && recorded_overload_still_relevant && (lhs_has_user_defined_identity || rhs_has_user_defined_identity);

		if (binaryOperatorNode.has_ambiguous_operator_overload() && !can_recompute_recorded_failure) {
			overload_result = OperatorOverloadResult::ambiguous();
		} else if (binaryOperatorNode.has_resolved_free_function_operator_overload()) {
			overload_result = OperatorOverloadResult(binaryOperatorNode.resolved_free_function_operator_overload());
		} else if (binaryOperatorNode.has_resolved_member_operator_overload()) {
			overload_result = OperatorOverloadResult(binaryOperatorNode.resolved_member_operator_overload());
		} else if (binaryOperatorNode.has_no_match_operator_overload() && !can_recompute_recorded_failure) {
			overload_result = OperatorOverloadResult::no_overload();
		} else {
			SymbolTable& sym_table = global_symbol_table_ ? *global_symbol_table_ : symbol_table;
			if (concrete_type_specs.has_value()) {
				overload_result = findBinaryOperatorOverloadWithFreeFunction(
				    concrete_type_specs->first,
				    concrete_type_specs->second,
				    op_kind,
				    op,
				    sym_table);
				if (!overload_result.has_match && !overload_result.is_ambiguous && (lhs_type_index.is_valid() || rhs_type_index.is_valid())) {
					overload_result = findBinaryOperatorOverloadWithFreeFunction(
					    lhs_type_index,
					    rhs_type_index,
					    op_kind,
					    op,
					    sym_table,
					    rhsCat);
				}
			} else {
				overload_result = findBinaryOperatorOverloadWithFreeFunction(
				    lhs_type_index,
				    rhs_type_index,
				    op_kind,
				    op,
				    sym_table,
				    rhsCat);
			}
		}
		if (overload_result.is_ambiguous) {
			throw CompileError("Ambiguous overload for operator" + std::string(op));
		}

		bool requires_user_defined_operator = false;
		if (concrete_type_specs.has_value()) {
			requires_user_defined_operator =
			    requiresUserDefinedBinaryOperator(concrete_type_specs->first) || requiresUserDefinedBinaryOperator(concrete_type_specs->second);
		} else {
			requires_user_defined_operator =
			    requiresUserDefinedBinaryOperatorByBase(lhs_type_index) || requiresUserDefinedBinaryOperatorByBase(rhs_type_index);
		}

		can_try_spaceship_rewrite =
		    !overload_result.has_match && requires_user_defined_operator && lhsCat == TypeCategory::Struct && (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" || op == "!=");

		if (!overload_result.has_match && requires_user_defined_operator && !can_try_spaceship_rewrite) {
			throw CompileError("Operator" + std::string(op) + " not defined for operand types");
		}

		if (overload_result.has_match && overload_result.is_free_function) {
			// Found a free-function operator overload: operator+(LHSType, RHSType)
			FLASH_LOG_FORMAT(Codegen, Debug, "Resolving free-function operator{} overload", op);

			const FunctionDeclarationNode& func_decl = *overload_result.free_function_overload;
			TypeSpecifierNode return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();

			// Get parameter types for mangling
			std::vector<TypeSpecifierNode> param_types;
			for (const auto& param_node : func_decl.parameter_nodes()) {
				if (param_node.is<DeclarationNode>()) {
					param_types.push_back(param_node.as<DeclarationNode>().type_node().as<TypeSpecifierNode>());
				}
			}

			// Get namespace path for mangling
			std::vector<std::string_view> namespace_path;
			if (global_symbol_table_) {
				auto ns_handle_opt = global_symbol_table_->find_namespace_of_function(func_decl);
				if (ns_handle_opt.has_value()) {
					NamespaceHandle nh = *ns_handle_opt;
					while (nh.isValid() && !nh.isGlobal()) {
						const NamespaceEntry& entry = gNamespaceRegistry.getEntry(nh);
						namespace_path.insert(namespace_path.begin(), StringTable::getStringView(entry.name));
						nh = gNamespaceRegistry.getParent(nh);
					}
				}
			}

			StringBuilder op_name_sb;
			op_name_sb.append("operator").append(op);
			std::string_view operator_func_name = op_name_sb.commit();
			auto mangled_name = NameMangling::generateMangledName(
			    operator_func_name,
			    return_type,
			    param_types,
			    false, // not variadic
			    "", // no struct (free function)
			    namespace_path,
			    Linkage::CPlusPlus,
			    false // free function, never const
			);

			TempVar result_var = var_counter.next();
			CallOp call_op;
			call_op.result = result_var;
			call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
			call_op.is_member_function = false;
			call_op.return_type_index = return_type.type_index();
			int actual_return_size = static_cast<int>(return_type.size_in_bits());
			if (actual_return_size == 0 && return_type.category() == TypeCategory::Struct && return_type.type_index().is_valid()) {
				if (const StructTypeInfo* ret_struct = tryGetStructTypeInfo(return_type.type_index())) {
					actual_return_size = static_cast<int>(ret_struct->total_size * 8);
				}
			}
			call_op.return_size_in_bits = SizeInBits{actual_return_size};

			bool needs_hidden_return = needsHiddenReturnParam(return_type.type(), return_type.pointer_depth(), return_type.is_reference(), call_op.return_size_in_bits.value, context_->isLLP64());
			if (needs_hidden_return) {
				call_op.return_slot = result_var;
			}

			// Helper: take address of operand for reference parameters
			auto passOperandArg = [&](const ExprResult& operand_result, TypeCategory opType, int opSize, std::string_view role, CallOp& cop) -> bool {
				if (auto ref_arg = makeReferenceArgument(operand_result, opType, opSize); ref_arg.has_value()) {
					cop.args.push_back(*ref_arg);
					return true;
				}
				FLASH_LOG_FORMAT(Codegen, Error, "Cannot materialize free-function operator {} reference argument", role);
				return false;
			};

			// Pass LHS as first argument
			if (!param_types.empty() && param_types[0].is_reference()) {
				if (!passOperandArg(lhsExprResult, lhsCat, lhsSize, "LHS", call_op))
					return ExprResult{};
			} else {
				call_op.args.push_back(toTypedValue(lhsExprResult));
			}

			// Pass RHS as second argument
			if (param_types.size() >= 2 && param_types[1].is_reference()) {
				if (!passOperandArg(rhsExprResult, rhsCat, rhsSize, "RHS", call_op))
					return ExprResult{};
			} else {
				call_op.args.push_back(toTypedValue(rhsExprResult));
			}

			ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), binaryOperatorNode.get_token()));
			return makeExprResult(return_type.type_index(), SizeInBits{actual_return_size}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
		}

		else if (overload_result.has_match) {
			// Found a member operator overload! Generate a member function call
			FLASH_LOG_FORMAT(Codegen, Debug, "Resolving binary operator{} overload for type index {}",
			                 op, lhs_type_index);

			const StructMemberFunction& member_func = *overload_result.member_overload;
			const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
			if (op == "=") {
				if (lhs_type_index.is_valid()) {
					if (const StructTypeInfo* struct_info = getTypeInfo(lhs_type_index).getStructInfo()) {
						if (auto same_type_assignment_kind = getSameTypeAssignmentKind(*struct_info, func_decl);
						    same_type_assignment_kind.has_value()) {
							diagnoseDeletedSameTypeAssignmentUsage(*struct_info, *same_type_assignment_kind);
						}
					}
				}
			}
			if (op == "=" && func_decl.is_deleted()) {
				throw CompileError("Call to deleted function 'operator='");
			}

			// Get struct name for mangling
			std::string_view struct_name = StringTable::getStringView(getTypeInfo(lhs_type_index).name());

			// Get the return type from the function declaration
			TypeSpecifierNode return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
			resolveSelfReferentialType(return_type, lhs_type_index);

			// Get the parameter types for mangling
			std::vector<TypeSpecifierNode> param_types;
			for (const auto& param_node : func_decl.parameter_nodes()) {
				if (param_node.is<DeclarationNode>()) {
					const auto& param_decl = param_node.as<DeclarationNode>();
					TypeSpecifierNode param_type = param_decl.type_node().as<TypeSpecifierNode>();
					resolveSelfReferentialType(param_type, lhs_type_index);
					param_types.push_back(param_type);
				}
			}

			// Generate mangled name for the operator
			std::string operator_func_name = "operator";
			operator_func_name += op;
			std::vector<std::string_view> empty_namespace;
			auto mangled_name = NameMangling::generateMangledName(
			    operator_func_name,
			    return_type,
			    param_types,
			    false, // not variadic
			    struct_name,
			    empty_namespace,
			    Linkage::CPlusPlus,
			    member_func.is_const());

			// Generate the call to the operator overload
			// For member function: a.operator+(b) where 'a' is 'this' and 'b' is the parameter
			TempVar result_var = var_counter.next();

			// Take address of LHS to pass as 'this' pointer
			// The LHS operand contains a struct value - extract it properly
			std::variant<StringHandle, TempVar> lhs_value;
			if (const auto* string_val = std::get_if<StringHandle>(&lhsExprResult.value)) {
				lhs_value = *string_val;
			} else if (const auto* temp_var = std::get_if<TempVar>(&lhsExprResult.value)) {
				if (auto global_name = tryGetGlobalLValueName(lhsExprResult); global_name.has_value()) {
					lhs_value = *global_name;
				} else {
					lhs_value = *temp_var;
				}
			} else {
				// Can't take address of non-lvalue
				FLASH_LOG(Codegen, Error, "Cannot take address of binary operator LHS - not an lvalue");
				return ExprResult{};
			}

			TempVar lhs_addr = var_counter.next();
			AddressOfOp addr_op;
			addr_op.result = lhs_addr;
			addr_op.operand.setType(lhsCat);
			addr_op.operand.ir_type = toIrType(lhsCat);
			addr_op.operand.size_in_bits = SizeInBits{lhsSize};
			addr_op.operand.pointer_depth = PointerDepth{}; // TODO: Verify pointer depth
			// Convert std::variant<StringHandle, TempVar> to IrValue
			if (const auto* string_ptr = std::get_if<StringHandle>(&lhs_value)) {
				addr_op.operand.value = *string_ptr;
			} else {
				addr_op.operand.value = std::get<TempVar>(lhs_value);
			}
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), binaryOperatorNode.get_token()));

			// Create the call operation
			CallOp call_op;
			call_op.result = result_var;
			call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);

			// Resolve actual return type - defaulted operator<=> has 'auto' return type
			// that is deduced to int (returning -1/0/1)
			TypeCategory resolved_return_type = return_type.type();
			TypeCategory resolved_cat = return_type.category();
			int actual_return_size = static_cast<int>(return_type.size_in_bits());
			if (isPlaceholderAutoType(resolved_cat) && op == "<=>") {
				resolved_return_type = TypeCategory::Int;
				resolved_cat = TypeCategory::Int;
				actual_return_size = 32;
			}
			if (actual_return_size == 0 && resolved_cat == TypeCategory::Struct && return_type.type_index().is_valid()) {
				// Look up struct size from type info
				if (const StructTypeInfo* ret_struct = tryGetStructTypeInfo(return_type.type_index())) {
					actual_return_size = static_cast<int>(ret_struct->total_size * 8);
				}
			}
			call_op.return_type_index = return_type.type_index().withCategory(resolved_return_type);
			call_op.return_size_in_bits = SizeInBits{actual_return_size};
			call_op.is_member_function = true; // This is a member function call

			// Detect if returning struct by value (needs hidden return parameter for RVO)
			bool returns_struct_by_value = returnsStructByValue(return_type.type(), return_type.pointer_depth(), return_type.is_reference());
			bool needs_hidden_return_param = needsHiddenReturnParam(return_type.type(), return_type.pointer_depth(), return_type.is_reference(), actual_return_size, context_->isLLP64());

			if (needs_hidden_return_param) {
				call_op.return_slot = result_var;

				FLASH_LOG_FORMAT(Codegen, Debug,
				                 "Binary operator overload returns large struct by value (size={} bits) - using return slot",
				                 actual_return_size);
			} else if (returns_struct_by_value) {
				// Small struct return - no return slot needed
				FLASH_LOG_FORMAT(Codegen, Debug,
				                 "Binary operator overload returns small struct by value (size={} bits) - will return in RAX",
				                 actual_return_size);
			}

			// Add 'this' pointer as first argument
			TypedValue this_arg;
			this_arg.setType(lhsCat);
			this_arg.ir_type = toIrType(lhsCat);
			this_arg.size_in_bits = SizeInBits{64}; // 'this' is always a pointer (64-bit)
			this_arg.value = lhs_addr;
			call_op.args.push_back(this_arg);

			// Add RHS as the second argument
			// Check if the parameter is a reference - if so, we need to pass the address
			if (!param_types.empty() && param_types[0].is_reference()) {
				if (auto rhs_arg = makeReferenceArgument(rhsExprResult, rhsCat, rhsSize); rhs_arg.has_value()) {
					call_op.args.push_back(*rhs_arg);
				} else {
					FLASH_LOG(Codegen, Error, "Cannot materialize binary operator RHS reference argument");
					return ExprResult{};
				}
			} else {
				// Parameter is not a reference - pass the value directly
				call_op.args.push_back(toTypedValue(rhsExprResult));
			}
			fillInDefaultArguments(call_op, func_decl.parameter_nodes(), 1);

			ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), binaryOperatorNode.get_token()));

			// Return the result with resolved types
			return makeExprResult(return_type.type_index().withCategory(resolved_return_type), SizeInBits{static_cast<int>(actual_return_size)}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
		}
	}

	// Special handling for spaceship-based comparisons on struct types.
	// Direct <=> returns the comparison result; relational/equality operators
	// are rewritten to compare that result against zero when no direct overload matched.
	FLASH_LOG_FORMAT(Codegen, Debug, "Binary operator check: op='{}', lhsType={}", op, static_cast<int>(lhsCat));

	if (op == "<=>" || op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" || op == "!=") {
		FLASH_LOG_FORMAT(Codegen, Debug, "Spaceship operator detected: lhsType={}, is_struct={}",
		                 static_cast<int>(lhsCat), lhsCat == TypeCategory::Struct);

		// Check if LHS is a struct type
		if (lhsCat == TypeCategory::Struct && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();

			// Get the LHS value - can be an identifier, member access, or other expression
			std::variant<StringHandle, TempVar> lhs_value;
			TypeIndex spaceship_lhs_type_index{};

			if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
				// Simple identifier case: p1 <=> p2
				const auto& lhs_id = std::get<IdentifierNode>(lhs_expr);
				std::string_view lhs_name = lhs_id.name();
				lhs_value = StringTable::getOrInternStringHandle(lhs_name);

				// Get the struct type info from symbol table
				auto symbol = symbol_table.lookup(lhs_name);
				if (symbol && symbol->is<VariableDeclarationNode>()) {
					const auto& var_decl = symbol->as<VariableDeclarationNode>();
					const auto& decl = var_decl.declaration();
					const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
					spaceship_lhs_type_index = type_node.type_index();
				} else if (symbol && symbol->is<DeclarationNode>()) {
					const auto& decl = symbol->as<DeclarationNode>();
					const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
					spaceship_lhs_type_index = type_node.type_index();
				} else {
					// Can't find the variable declaration
					return ExprResult{};
				}
			} else if (std::holds_alternative<MemberAccessNode>(lhs_expr)) {
				// Member access case: p.member <=> q.member
				const auto& member_access = std::get<MemberAccessNode>(lhs_expr);

				// Generate IR for the member access expression
				ExprResult member_ir = generateMemberAccessIr(member_access);
				if (member_ir.effectiveIrType() == IrType::Void && !member_ir.size_in_bits.is_set()) {
					return ExprResult{};
				}

				// Extract the result temp var and type index
				lhs_value = std::get<TempVar>(member_ir.value);
				spaceship_lhs_type_index = member_ir.type_index;
			} else {
				// Other expression types - use the already-generated ExprResult
				if (const auto* temp_var = std::get_if<TempVar>(&lhsExprResult.value)) {
					lhs_value = *temp_var;
				} else {
					// Complex expression that doesn't produce a temp var
					return ExprResult{};
				}

				// Try to get type index from the evaluated ExprResult if available
				if (lhsExprResult.type_index.is_valid()) {
					spaceship_lhs_type_index = lhsExprResult.type_index;
				} else {
					// Can't determine type index for complex expression
					return ExprResult{};
				}
			}

			// Look up the operator<=> function in the struct
			if (const StructTypeInfo* spaceship_struct = tryGetStructTypeInfo(spaceship_lhs_type_index)) {
				const StructTypeInfo& struct_info = *spaceship_struct;

				// Find operator<=> in member functions
				const StructMemberFunction* spaceship_op = nullptr;
				for (const auto& func : struct_info.member_functions) {
					if (func.operator_kind == OverloadableOperator::Spaceship) {
						spaceship_op = &func;
						break;
					}
				}

				if (spaceship_op && spaceship_op->function_decl.is<FunctionDeclarationNode>()) {
					const auto& func_decl = spaceship_op->function_decl.as<FunctionDeclarationNode>();

					// Generate a member function call: lhs.operator<=>(rhs)
					TempVar result_var = var_counter.next();

					// Get return type from the function declaration
					const auto& return_type_node = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
					TypeCategory return_type = return_type_node.type();
					int return_size = static_cast<int>(return_type_node.size_in_bits());

					// Defaulted operator<=> with auto return type actually returns int
					if (isPlaceholderAutoType(return_type)) {
						return_type = TypeCategory::Int;
						return_size = 32;
					}

					TypeSpecifierNode resolved_return_type_node = return_type_node;
					if (resolved_return_type_node.type() != return_type) {
						resolved_return_type_node = TypeSpecifierNode(return_type, TypeQualifier::None, return_size, return_type_node.token(), CVQualifier::None);
					}

					// Generate mangled name for the operator<=> call
					std::vector<TypeSpecifierNode> param_types;
					for (const auto& param_node : func_decl.parameter_nodes()) {
						if (param_node.is<DeclarationNode>()) {
							const auto& param_decl = param_node.as<DeclarationNode>();
							TypeSpecifierNode param_type = param_decl.type_node().as<TypeSpecifierNode>();
							resolveSelfReferentialType(param_type, spaceship_lhs_type_index);
							param_types.push_back(param_type);
						}
					}

					std::string_view mangled_name = generateMangledNameForCall(
					    "operator<=>",
					    resolved_return_type_node,
					    param_types,
					    false, // not variadic
					    StringTable::getStringView(spaceship_struct->name),
					    {}, // namespace_path
					    func_decl.is_const_member_function());

					// Create the call operation
					CallOp call_op;
					call_op.result = result_var;
					call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
					call_op.return_type_index = nativeTypeIndex(return_type);
					call_op.return_size_in_bits = SizeInBits{return_size};
					call_op.is_member_function = true;
					call_op.is_variadic = func_decl.is_variadic();

					// Determine if return slot is needed (same logic as generateFunctionCallIr)
					bool returns_struct_by_value = returnsStructByValue(return_type, return_type_node.pointer_depth(), return_type_node.is_reference());
					bool needs_hidden_return_param = needsHiddenReturnParam(return_type, return_type_node.pointer_depth(), return_type_node.is_reference(), return_size, context_->isLLP64());

					FLASH_LOG_FORMAT(Codegen, Debug,
					                 "Spaceship operator call: return_size={}, threshold={}, returns_struct={}, needs_hidden={}",
					                 return_size, getStructReturnThreshold(context_->isLLP64()), returns_struct_by_value, needs_hidden_return_param);

					if (needs_hidden_return_param) {
						call_op.return_slot = result_var;
						FLASH_LOG(Codegen, Debug, "Using return slot for spaceship operator");
					} else {
						FLASH_LOG(Codegen, Debug, "No return slot for spaceship operator (small struct return in RAX)");
					}

					// Add the LHS object as the first argument (this pointer)
					// For member functions, the this pointer is passed by name or temp var
					TypedValue lhs_arg;
					lhs_arg.setType(lhsCat);
					lhs_arg.ir_type = toIrType(lhsCat);
					lhs_arg.size_in_bits = SizeInBits{lhsSize};
					// Convert lhs_value (which can be string_view or TempVar) to IrValue
					if (const auto* string = std::get_if<StringHandle>(&lhs_value)) {
						lhs_arg.value = IrValue(*string);
					} else {
						lhs_arg.value = IrValue(std::get<TempVar>(lhs_value));
					}
					call_op.args.push_back(lhs_arg);

					// Add the RHS as the second argument
					// Check if parameter expects a reference
					TypedValue rhs_arg = toTypedValue(rhsExprResult);
					if (param_types.size() > 0) {
						// Check if first parameter is a reference
						const TypeSpecifierNode& param_type = param_types[0];
						if (param_type.is_rvalue_reference()) {
							rhs_arg.ref_qualifier = ReferenceQualifier::RValueReference;
						} else if (param_type.is_reference()) {
							rhs_arg.ref_qualifier = ReferenceQualifier::LValueReference;
						}
					}
					call_op.args.push_back(rhs_arg);

					ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), binaryOperatorNode.get_token()));

					if (op == "<=>") {
						return makeExprResult(nativeTypeIndex(return_type), SizeInBits{static_cast<int>(return_size)}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
					}

					TempVar cmp_result = var_counter.next();
					BinaryOp cmp_op{
					    .lhs = makeTypedValue(TypeCategory::Int, SizeInBits{32}, result_var),
					    .rhs = makeTypedValue(TypeCategory::Int, SizeInBits{32}, 0ULL),
					    .result = cmp_result,
					};

					IrOpcode cmp_opcode = IrOpcode::Equal;
					if (op == "<")
						cmp_opcode = IrOpcode::LessThan;
					else if (op == "<=")
						cmp_opcode = IrOpcode::LessEqual;
					else if (op == ">")
						cmp_opcode = IrOpcode::GreaterThan;
					else if (op == ">=")
						cmp_opcode = IrOpcode::GreaterEqual;
					else if (op == "==")
						cmp_opcode = IrOpcode::Equal;
					else if (op == "!=")
						cmp_opcode = IrOpcode::NotEqual;

					ir_.addInstruction(IrInstruction(cmp_opcode, std::move(cmp_op), binaryOperatorNode.get_token()));
					return makeExprResult(nativeTypeIndex(TypeCategory::Bool), SizeInBits{8}, IrOperand{cmp_result}, PointerDepth{}, ValueStorage::ContainsData);
				}
			}
		}

		// If we get here, operator<=> is not defined or not found
		if (can_try_spaceship_rewrite) {
			throw CompileError("Operator" + std::string(op) + " not defined for operand types");
		}
	}

	// Try to get pointer depth for pointer arithmetic
	int lhs_pointer_depth = 0;
	const TypeSpecifierNode* lhs_type_node = nullptr;
	if (binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
		const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
			const auto& lhs_id = std::get<IdentifierNode>(lhs_expr);
			auto symbol = lookupSymbol(lhs_id.name());
			if (symbol && symbol->is<VariableDeclarationNode>()) {
				const auto& var_decl = symbol->as<VariableDeclarationNode>();
				const auto& decl = var_decl.declaration();
				const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
				lhs_pointer_depth = static_cast<int>(type_node.pointer_depth());
				// Arrays decay to pointers in expressions - treat them as pointer_depth == 1
				if (decl.is_array() && lhs_pointer_depth == 0) {
					lhs_pointer_depth = 1;
				}
				lhs_type_node = &type_node;
			} else if (symbol && symbol->is<DeclarationNode>()) {
				const auto& decl = symbol->as<DeclarationNode>();
				const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
				lhs_pointer_depth = static_cast<int>(type_node.pointer_depth());
				// Arrays decay to pointers in expressions - treat them as pointer_depth == 1
				if (decl.is_array() && lhs_pointer_depth == 0) {
					lhs_pointer_depth = 1;
				}
				lhs_type_node = &type_node;
			}
		}
	}

	// Fallback: use the evaluated ExprResult pointer depth for expressions like
	// &member or function calls returning pointers.
	if (lhs_pointer_depth == 0) {
		lhs_pointer_depth = lhsExprResult.pointer_depth.value;
	}

	// Try to get pointer depth for RHS as well (for ptr - ptr case)
	int rhs_pointer_depth = 0;
	if (binaryOperatorNode.get_rhs().is<ExpressionNode>()) {
		const ExpressionNode& rhs_expr = binaryOperatorNode.get_rhs().as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(rhs_expr)) {
			const auto& rhs_id = std::get<IdentifierNode>(rhs_expr);
			auto symbol = lookupSymbol(rhs_id.name());
			if (symbol && symbol->is<VariableDeclarationNode>()) {
				const auto& var_decl = symbol->as<VariableDeclarationNode>();
				const auto& decl = var_decl.declaration();
				const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
				rhs_pointer_depth = static_cast<int>(type_node.pointer_depth());
			} else if (symbol && symbol->is<DeclarationNode>()) {
				const auto& decl = symbol->as<DeclarationNode>();
				const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
				rhs_pointer_depth = static_cast<int>(type_node.pointer_depth());
			}
		}
	}

	// Special handling for pointer subtraction (ptr - ptr)
	// Result is ptrdiff_t (number of elements between pointers)
	if (op == "-" && lhs_pointer_depth > 0 && rhs_pointer_depth > 0 && lhs_type_node) {
		// Both sides are pointers - this is pointer difference
		// C++ standard: (ptr1 - ptr2) / sizeof(*ptr1) gives element count
		// Result type is ptrdiff_t (signed long, 64-bit on x64)

		// Step 1: Subtract the pointers (gives byte difference)
		TempVar byte_diff = var_counter.next();
		BinaryOp sub_op{
		    .lhs = makeTypedValue(lhsCat, SizeInBits{64}, toIrValue(lhsExprResult.value)),
		    .rhs = makeTypedValue(rhsCat, SizeInBits{64}, toIrValue(rhsExprResult.value)),
		    .result = byte_diff,
		};
		ir_.addInstruction(IrInstruction(IrOpcode::Subtract, std::move(sub_op), binaryOperatorNode.get_token()));

		// Step 2: Determine element size using existing getSizeInBytes function
		size_t element_size;
		if (lhs_pointer_depth > 1) {
			element_size = 8; // Multi-level pointer: element is a pointer
		} else {
			// Single-level pointer: element size is sizeof(base_type)
			element_size = getSizeInBytes(lhs_type_node->type_index(), lhs_type_node->size_in_bits());
		}

		// Step 3: Divide byte difference by element size to get element count
		TempVar result_var = var_counter.next();
		BinaryOp div_op{
		    .lhs = makeTypedValue(TypeCategory::Long, SizeInBits{64}, byte_diff),
		    .rhs = makeTypedValue(TypeCategory::Int, SizeInBits{32}, static_cast<unsigned long long>(element_size)),
		    .result = result_var,
		};
		ir_.addInstruction(IrInstruction(IrOpcode::Divide, std::move(div_op), binaryOperatorNode.get_token()));

		// Return result as Long (ptrdiff_t) with 64-bit size
		return makeExprResult(nativeTypeIndex(TypeCategory::Long), SizeInBits{64}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
	}

	// Special handling for pointer arithmetic (ptr + int or ptr - int)
	// Only apply if LHS is actually a pointer (has pointer_depth > 0)
	// NOT for regular 64-bit integers like long, even though they are also 64 bits
	if ((op == "+" || op == "-") && lhsSize == 64 && lhs_pointer_depth > 0 && is_integer_type(rhsCat)) {
		// Left side is a pointer (64-bit with pointer_depth > 0), right side is integer
		// Result should be a pointer (64-bit)
		// Need to scale the offset by sizeof(pointed-to-type)

		// Determine element size
		size_t element_size;
		if (lhs_pointer_depth > 1) {
			// Multi-level pointer: element is a pointer, so 8 bytes
			element_size = 8;
		} else if (lhs_type_node) {
			// Single-level pointer: element size is sizeof(base_type)
			element_size = getSizeInBytes(lhs_type_node->type_index(), lhs_type_node->size_in_bits());
		} else {
			// Fallback: derive element size from operand's base type
			int base_size_bits = get_type_size_bits(lhsCat);
			element_size = base_size_bits / 8;
			if (element_size == 0)
				element_size = 1; // Safety: avoid zero-size elements
		}

		// Scale the offset: offset_scaled = offset * element_size
		TempVar scaled_offset = var_counter.next();

		// Use typed BinaryOp for the multiply operation
		BinaryOp scale_op{
		    .lhs = toTypedValue(rhsExprResult),
		    .rhs = makeTypedValue(TypeCategory::Int, SizeInBits{32}, static_cast<unsigned long long>(element_size)),
		    .result = scaled_offset,
		};
		ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(scale_op), binaryOperatorNode.get_token()));

		// Now add the scaled offset to the pointer
		TempVar result_var = var_counter.next();

		// Use typed BinaryOp for pointer addition/subtraction
		BinaryOp ptr_arith_op{
		    .lhs = makeTypedValue(lhsCat, SizeInBits{lhsSize}, toIrValue(lhsExprResult.value)),
		    .rhs = makeTypedValue(TypeCategory::Int, SizeInBits{32}, scaled_offset),
		    .result = result_var,
		};

		IrOpcode ptr_opcode = (op == "+") ? IrOpcode::Add : IrOpcode::Subtract;
		ir_.addInstruction(IrInstruction(ptr_opcode, std::move(ptr_arith_op), binaryOperatorNode.get_token()));

		// Return pointer type with 64-bit size
		return makeExprResult(nativeTypeIndex(lhsCat), SizeInBits{64}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
	}

	// Check for logical operations BEFORE type promotions
	// Logical operations should preserve boolean types without promotion
	// C++20 [expr.log.and], [expr.log.or]: each operand is contextually
	// converted to bool.
	if (op == "&&" || op == "||") {
		// Convert operands to bool when they are not already bool-compatible.
		// Reuse applyConditionBoolConversion which checks sema annotations and
		// falls back to local float→int conversion when needed.
		lhsExprResult = applyConditionBoolConversion(lhsExprResult, binaryOperatorNode.get_lhs(), binaryOperatorNode.get_token());
		rhsExprResult = applyConditionBoolConversion(rhsExprResult, binaryOperatorNode.get_rhs(), binaryOperatorNode.get_token());

		TempVar result_var = var_counter.next();
		BinaryOp bin_op{
		    .lhs = makeTypedValue(TypeCategory::Bool, SizeInBits{8}, toIrValue(lhsExprResult.value)),
		    .rhs = makeTypedValue(TypeCategory::Bool, SizeInBits{8}, toIrValue(rhsExprResult.value)),
		    .result = result_var,
		};
		IrOpcode opcode = (op == "&&") ? IrOpcode::LogicalAnd : IrOpcode::LogicalOr;
		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		return makeExprResult(nativeTypeIndex(TypeCategory::Bool), SizeInBits{8}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData); // Logical operations return bool8
	}

	// Special handling for pointer compound assignment (ptr += int or ptr -= int)
	// MUST be before type promotions to avoid truncating the pointer
	if ((op == "+=" || op == "-=") && lhsSize == 64 && lhs_pointer_depth > 0 && is_integer_type(rhsCat) && lhs_type_node) {
		// Left side is a pointer (64-bit), right side is integer
		// Need to scale the offset by sizeof(pointed-to-type)
		FLASH_LOG_FORMAT(Codegen, Debug, "[PTR_ARITH_DEBUG] Compound assignment: lhsSize={}, pointer_depth={}, rhsType={}", lhsSize, lhs_pointer_depth, static_cast<int>(rhsCat));

		// Determine element size using existing getSizeInBytes function
		size_t element_size;
		if (lhs_pointer_depth > 1) {
			element_size = 8; // Multi-level pointer
		} else {
			// Single-level pointer: element size is sizeof(base_type)
			element_size = getSizeInBytes(lhs_type_node->type_index(), lhs_type_node->size_in_bits());
		}

		// Scale the offset: offset_scaled = offset * element_size
		TempVar scaled_offset = var_counter.next();
		BinaryOp scale_op{
		    .lhs = toTypedValue(rhsExprResult),
		    .rhs = makeTypedValue(TypeCategory::Int, SizeInBits{32}, static_cast<unsigned long long>(element_size)),
		    .result = scaled_offset,
		};
		ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(scale_op), binaryOperatorNode.get_token()));

		// ptr = ptr + scaled_offset (or ptr - scaled_offset)
		TempVar result_var = var_counter.next();
		BinaryOp ptr_arith_op{
		    .lhs = makeTypedValue(lhsCat, SizeInBits{lhsSize}, toIrValue(lhsExprResult.value)),
		    .rhs = makeTypedValue(TypeCategory::Int, SizeInBits{32}, scaled_offset),
		    .result = result_var,
		};

		IrOpcode ptr_opcode = (op == "+=") ? IrOpcode::Add : IrOpcode::Subtract;
		ir_.addInstruction(IrInstruction(ptr_opcode, std::move(ptr_arith_op), binaryOperatorNode.get_token()));

		// Store result back to LHS (must be a variable)
		if (std::holds_alternative<StringHandle>(lhsExprResult.value)) {
			AssignmentOp assign_op;
			assign_op.result = std::get<StringHandle>(lhsExprResult.value);
			assign_op.lhs = makeTypedValue(lhsCat, SizeInBits{lhsSize}, std::get<StringHandle>(lhsExprResult.value));

			// Check if LHS is a reference variable
			StringHandle lhs_handle = std::get<StringHandle>(lhsExprResult.value);
			std::string_view lhs_name = StringTable::getStringView(lhs_handle);
			if (isVariableReference(lhs_name)) {
				assign_op.lhs.ref_qualifier = ReferenceQualifier::LValueReference;
			}

			assign_op.rhs = makeTypedValue(lhsCat, SizeInBits{lhsSize}, result_var);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
		} else if (std::holds_alternative<TempVar>(lhsExprResult.value)) {
			AssignmentOp assign_op;
			assign_op.result = std::get<TempVar>(lhsExprResult.value);
			assign_op.lhs = makeTypedValue(lhsCat, SizeInBits{lhsSize}, std::get<TempVar>(lhsExprResult.value));

			// Check if LHS TempVar corresponds to a reference variable
			TempVar lhs_temp = std::get<TempVar>(lhsExprResult.value);
			std::string_view temp_name = lhs_temp.name();
			// Remove '%' prefix if present
			if (!temp_name.empty() && temp_name[0] == '%') {
				temp_name = temp_name.substr(1);
			}
			if (isVariableReference(temp_name)) {
				assign_op.lhs.ref_qualifier = ReferenceQualifier::LValueReference;
			}

			assign_op.rhs = makeTypedValue(lhsCat, SizeInBits{lhsSize}, result_var);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
		}

		// Return the pointer result
		return makeExprResult(nativeTypeIndex(lhsCat), SizeInBits{lhsSize}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
	}

	// Apply integer promotions and find common type
	// BUT: Skip type promotion for pointer assignments (ptr = ptr_expr)
	// Pointers should not be converted to common types
	if (op == "=" && lhsSize == 64 && lhs_pointer_depth > 0) {
		// This is a pointer assignment - no type conversions needed
		// Just assign the RHS to the LHS directly
		FLASH_LOG_FORMAT(Codegen, Debug, "[PTR_ARITH_DEBUG] Pointer assignment: lhsSize={}, pointer_depth={}", lhsSize, lhs_pointer_depth);

		// Get the assignment target (must be a variable)
		if (std::holds_alternative<StringHandle>(lhsExprResult.value)) {
			AssignmentOp assign_op;
			assign_op.result = std::get<StringHandle>(lhsExprResult.value);
			assign_op.lhs = makeTypedValue(lhsCat, SizeInBits{lhsSize}, std::get<StringHandle>(lhsExprResult.value));

			// Check if LHS is a reference variable
			StringHandle lhs_handle = std::get<StringHandle>(lhsExprResult.value);
			std::string_view lhs_name = StringTable::getStringView(lhs_handle);
			if (isVariableReference(lhs_name)) {
				assign_op.lhs.ref_qualifier = ReferenceQualifier::LValueReference;
			}

			assign_op.rhs = toTypedValue(rhsExprResult);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
			// Return the assigned value
			return makeExprResult(nativeTypeIndex(lhsCat), SizeInBits{lhsSize}, IrOperand{std::get<StringHandle>(lhsExprResult.value)}, PointerDepth{}, ValueStorage::ContainsData);
		} else if (std::holds_alternative<TempVar>(lhsExprResult.value)) {
			[[maybe_unused]] TempVar result_var = var_counter.next();
			AssignmentOp assign_op;
			assign_op.result = std::get<TempVar>(lhsExprResult.value);
			assign_op.lhs = makeTypedValue(lhsCat, SizeInBits{lhsSize}, std::get<TempVar>(lhsExprResult.value));

			// Check if LHS TempVar corresponds to a reference variable
			TempVar lhs_temp = std::get<TempVar>(lhsExprResult.value);
			std::string_view temp_name = lhs_temp.name();
			// Remove '%' prefix if present
			if (!temp_name.empty() && temp_name[0] == '%') {
				temp_name = temp_name.substr(1);
			}
			if (isVariableReference(temp_name)) {
				assign_op.lhs.ref_qualifier = ReferenceQualifier::LValueReference;
			}

			assign_op.rhs = toTypedValue(rhsExprResult);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
			// Return the assigned value
			return makeExprResult(nativeTypeIndex(lhsCat), SizeInBits{lhsSize}, IrOperand{std::get<TempVar>(lhsExprResult.value)}, PointerDepth{}, ValueStorage::ContainsData);
		}
	}

	// Special handling for assignment: convert RHS to LHS type instead of finding common type
	// For assignment, we don't want to promote the LHS
	if (op == "=") {
		if (lhsCat == TypeCategory::Struct && rhsCat == TypeCategory::Struct && lhsExprResult.type_index.is_valid() && rhsExprResult.type_index.is_valid() && lhsExprResult.type_index == rhsExprResult.type_index) {
			if (const StructTypeInfo* struct_info = tryGetStructTypeInfo(lhsExprResult.type_index)) {
				diagnoseDeletedSameTypeAssignmentUsage(*struct_info, shouldPreferMoveAssignment(rhsExprResult));
			}
		}

		// Convert RHS to LHS type if they differ — lhsType is the ground truth for the destination.
		// Phase 15: prefer sema annotation; log warning on fallback for arithmetic types.
		if (rhsCat != lhsCat) {
			if (!tryGlobalSemaConv(rhsExprResult, binaryOperatorNode.get_rhs(), lhsCat)) {
				if (sema_normalized_current_function_ && is_standard_arithmetic_type(rhsCat) && is_standard_arithmetic_type(lhsCat))
					throw InternalError(std::string("Phase 15: sema missed local assignment (") + std::string(getTypeName(rhsCat)) + " -> " + std::string(getTypeName(lhsCat)) + ")");
				rhsExprResult = generateTypeConversion(rhsExprResult, rhsCat, lhsCat, binaryOperatorNode.get_token());
			}
		}
		// Now both are the same type, create assignment
		AssignmentOp assign_op;
		// Extract the LHS value directly (it's either StringHandle or TempVar)
		if (const auto* string = std::get_if<StringHandle>(&lhsExprResult.value)) {
			assign_op.result = *string;
		} else if (const auto* temp_var = std::get_if<TempVar>(&lhsExprResult.value)) {
			assign_op.result = *temp_var;
		} else {
			// LHS is an immediate value - this shouldn't happen for valid assignments
			throw InternalError("Assignment LHS cannot be an immediate value");
			return ExprResult{};
		}
		assign_op.lhs = toTypedValue(lhsExprResult);
		assign_op.rhs = toTypedValue(rhsExprResult);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
		// Assignment expression returns the LHS (the assigned-to value)
		if (auto global_name = tryGetGlobalLValueName(lhsExprResult); global_name.has_value()) {
			TempVar store_temp = var_counter.next();
			AssignmentOp materialize_store;
			materialize_store.result = store_temp;
			materialize_store.lhs = makeTypedValue(lhsCat, SizeInBits{lhsSize}, store_temp);
			materialize_store.rhs = toTypedValue(rhsExprResult);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_store), binaryOperatorNode.get_token()));

			std::vector<IrOperand> store_operands;
			store_operands.emplace_back(*global_name);
			store_operands.emplace_back(store_temp);
			ir_.addInstruction(IrOpcode::GlobalStore, std::move(store_operands), binaryOperatorNode.get_token());

			GlobalStaticBindingInfo binding;
			binding.is_global_or_static = true;
			binding.store_name = *global_name;
			binding.type_index = TypeIndex{0, lhsCat};
			binding.size_in_bits = SizeInBits{lhsSize};
			return makeGlobalAssignmentResultLValue(binding);
		}
		return lhsExprResult;
	}

	// C++20 [expr.shift]: shift operands undergo independent integral promotions,
	// NOT usual arithmetic conversions.  The result type is the promoted LHS type.
	const bool is_shift_op = (op == "<<" || op == ">>" || op == "<<=" || op == ">>=");
	TypeCategory commonType = is_shift_op
	                              ? promote_integer_type(lhsExprResult.category()) // shift: result type = promoted LHS
	                              : get_common_type(lhsExprResult.category(), rhsExprResult.category());

	// Save original LHS value binding before type conversion — only needed for compound assignment store-back.
	const IrOperand original_lhs_value = (isCompoundAssignmentOp(op)) ? lhsExprResult.value : IrOperand{};

	// Phase 15: generate conversions — prefer sema annotations; log warning on fallback.
	// Reuse tryGlobalSemaConv (defined above) which performs sema slot lookup, struct-type
	// guard, expected-target verification, enum type mismatch handling, and conversion.
	if (lhsCat != commonType) {
		if (!tryGlobalSemaConv(lhsExprResult, binaryOperatorNode.get_lhs(), commonType)) {
			if (sema_normalized_current_function_ && is_standard_arithmetic_type(lhsCat) && is_standard_arithmetic_type(commonType))
				throw InternalError(std::string("Phase 15: sema missed binary LHS (") + std::string(getTypeName(lhsCat)) + " -> " + std::string(getTypeName(commonType)) + ")");
			lhsExprResult = generateTypeConversion(lhsExprResult, lhsCat, commonType, binaryOperatorNode.get_token());
		}
	}
	// C++20 [expr.shift]: shift RHS undergoes independent integral promotion,
	// NOT conversion to the LHS/result type.  Only apply sema-annotated promotion
	// (e.g. short→int) — never widen to commonType (which is the promoted LHS type).
	// Phase 15: if sema missed the promotion and it's needed, assert.
	if (is_shift_op) {
		const TypeCategory promoted_rhs = promote_integer_type(rhsExprResult.category());
		if (rhsCat != promoted_rhs) {
			if (!tryGlobalSemaConv(rhsExprResult, binaryOperatorNode.get_rhs())) {
				if (sema_normalized_current_function_ && is_standard_arithmetic_type(rhsCat))
					throw InternalError(std::string("Phase 15: sema missed shift RHS promotion (") + std::string(getTypeName(rhsCat)) + " -> " + std::string(getTypeName(promoted_rhs)) + ")");
				rhsExprResult = generateTypeConversion(rhsExprResult, rhsCat, promoted_rhs, binaryOperatorNode.get_token());
			}
		}
	} else if (rhsCat != commonType) {
		if (!tryGlobalSemaConv(rhsExprResult, binaryOperatorNode.get_rhs(), commonType)) {
			if (sema_normalized_current_function_ && is_standard_arithmetic_type(rhsCat) && is_standard_arithmetic_type(commonType))
				throw InternalError(std::string("Phase 15: sema missed binary RHS (") + std::string(getTypeName(rhsCat)) + " -> " + std::string(getTypeName(commonType)) + ")");
			rhsExprResult = generateTypeConversion(rhsExprResult, rhsCat, commonType, binaryOperatorNode.get_token());
		}
	}

	// C++20 [expr.ass]/7: for compound assignment E1 op= E2, the behavior is equivalent
	// to E1 = static_cast<T1>(E1 op E2) where T1 is the type of E1. When the LHS was
	// promoted/converted for the operation, the compound-assign opcodes would store the
	// result into the temporary (losing the original variable binding). Fix: perform the
	// binary operation explicitly, convert the result back to LHS type, and store it.
	if (isCompoundAssignmentOp(op) && lhsCat != commonType) {
		if (const auto base_opcode = compoundOpToBaseOpcode(op); base_opcode.has_value()) {
			IrOpcode arith_opcode = *base_opcode;
			// Upgrade to the correct opcode for the common type.
			if (is_floating_point_type(commonType)) {
				// C++20 [expr.mul]/4: % requires integral operands; diagnose ill-formed code.
				if (arith_opcode == IrOpcode::Modulo)
					throw CompileError("Operator %= is not defined for floating-point operands (C++20 [expr.mul]/4)");
				// C++20 [expr.bit.and], [expr.bit.or], [expr.bit.xor]: bitwise ops require integral operands.
				if (arith_opcode == IrOpcode::BitwiseAnd || arith_opcode == IrOpcode::BitwiseOr || arith_opcode == IrOpcode::BitwiseXor)
					throw CompileError("Bitwise compound assignment is not defined for floating-point operands");
				if (arith_opcode == IrOpcode::Add)
					arith_opcode = IrOpcode::FloatAdd;
				else if (arith_opcode == IrOpcode::Subtract)
					arith_opcode = IrOpcode::FloatSubtract;
				else if (arith_opcode == IrOpcode::Multiply)
					arith_opcode = IrOpcode::FloatMultiply;
				else if (arith_opcode == IrOpcode::Divide)
					arith_opcode = IrOpcode::FloatDivide;
			} else if (is_unsigned_integer_type(commonType)) {
				if (arith_opcode == IrOpcode::Divide)
					arith_opcode = IrOpcode::UnsignedDivide;
				else if (arith_opcode == IrOpcode::Modulo)
					arith_opcode = IrOpcode::UnsignedModulo;
				else if (arith_opcode == IrOpcode::ShiftRight)
					arith_opcode = IrOpcode::UnsignedShiftRight;
			}

			// 1. Perform the arithmetic in common type
			TempVar op_result = var_counter.next();
			BinaryOp bin_op{
			    .lhs = toTypedValue(lhsExprResult),
			    .rhs = toTypedValue(rhsExprResult),
			    .result = op_result,
			};
			ir_.addInstruction(IrInstruction(arith_opcode, std::move(bin_op), binaryOperatorNode.get_token()));

			// 2. Convert result back to original LHS type
			// Phase 17: verify sema annotated the back-conversion (ownership transfer).
			if (sema_ && sema_normalized_current_function_ &&
			    is_standard_arithmetic_type(commonType) && is_standard_arithmetic_type(lhsCat)) {
				auto back_conv = sema_->getCompoundAssignBackConv(static_cast<const void*>(&binaryOperatorNode));
				if (!back_conv.has_value())
					throw InternalError(std::string("Phase 17: sema missed compound assign back-conversion (") + std::string(getTypeName(commonType)) + " -> " + std::string(getTypeName(lhsCat)) + ")");
			}
			ExprResult op_expr = makeExprResult(nativeTypeIndex(commonType), SizeInBits{get_type_size_bits(commonType)}, IrOperand{op_result}, PointerDepth{}, ValueStorage::ContainsData);
			ExprResult converted = generateTypeConversion(op_expr, commonType, lhsCat, binaryOperatorNode.get_token());

			// 3. Store back to original LHS variable
			// original_lhs_value was the value of lhsExprResult before type conversion
			// (which is a StringHandle for local variables or TempVar for other lvalues).
			AssignmentOp assign_op;
			if (const auto* sh = std::get_if<StringHandle>(&original_lhs_value)) {
				assign_op.result = *sh;
				assign_op.lhs = makeTypedValue(lhsCat, SizeInBits{lhsSize}, *sh);
			} else if (const auto* tv = std::get_if<TempVar>(&original_lhs_value)) {
				assign_op.result = *tv;
				assign_op.lhs = makeTypedValue(lhsCat, SizeInBits{lhsSize}, *tv);
			} else {
				throw InternalError("Compound assignment LHS must be a variable");
			}
			assign_op.rhs = toTypedValue(converted);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
			return makeExprResult(nativeTypeIndex(lhsCat), SizeInBits{lhsSize}, original_lhs_value, PointerDepth{}, ValueStorage::ContainsData);
		}
	}

	// Check if we're dealing with floating-point operations
	bool is_floating_point_op = is_floating_point_type(commonType);

	// Create a temporary variable for the result
	TempVar result_var = var_counter.next();

	// Mark arithmetic/comparison result as prvalue (Option 2: Value Category Tracking)
	// Binary operations produce temporary values (prvalues) with no persistent identity
	setTempVarMetadata(result_var, TempVarMetadata::makePRValue());

	// Generate the IR for the operation based on the operator and operand types
	// Use a lookup table approach for better performance and maintainability
	IrOpcode opcode;

	// New typed operand goes in here. Goal is that all operands live here
	static const std::unordered_map<std::string_view, IrOpcode> bin_ops = {
	    {"+", IrOpcode::Add}, {"-", IrOpcode::Subtract}, {"*", IrOpcode::Multiply}, {"<<", IrOpcode::ShiftLeft}, {"&", IrOpcode::BitwiseAnd}, {"|", IrOpcode::BitwiseOr}, {"^", IrOpcode::BitwiseXor}};

	auto bin_ops_it = !is_floating_point_op ? bin_ops.find(op) : bin_ops.end();
	if (bin_ops_it != bin_ops.end()) {
		opcode = bin_ops_it->second;

		// Use fully typed instruction (zero vector allocation!)
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = result_var,
		};

		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
	}
	// Division operations (typed)
	else if (op == "/" && !is_floating_point_op) {
		opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedDivide : IrOpcode::Divide;

		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = result_var,
		};

		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
	}
	// Modulo operations (typed): signed vs. unsigned; float is ill-formed (C++20 [expr.mul]/4)
	else if (op == "%") {
		if (is_floating_point_op)
			throw CompileError("Operator % is not defined for floating-point operands (C++20 [expr.mul]/4)");
		opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedModulo : IrOpcode::Modulo;

		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = result_var,
		};

		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
	}
	// Right shift operations (typed)
	else if (op == ">>") {
		opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedShiftRight : IrOpcode::ShiftRight;

		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = result_var,
		};

		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
	}
	// Comparison operations (typed)
	// For pointer comparisons, override types to use 64-bit unsigned integers
	// Helper lambda to apply pointer comparison type override
	auto applyPointerComparisonOverride = [&](BinaryOp& bin_op, IrOpcode& opcode) {
		if (lhs_pointer_depth > 0 && rhs_pointer_depth > 0) {
			bin_op.lhs.setType(TypeCategory::UnsignedLongLong);
			bin_op.lhs.ir_type = IrType::Integer;
			bin_op.lhs.size_in_bits = SizeInBits{64};
			bin_op.rhs.setType(TypeCategory::UnsignedLongLong);
			bin_op.rhs.ir_type = IrType::Integer;
			bin_op.rhs.size_in_bits = SizeInBits{64};

			// For ordered comparisons, ensure we use unsigned comparison for pointers
			if (opcode == IrOpcode::LessThan)
				opcode = IrOpcode::UnsignedLessThan;
			else if (opcode == IrOpcode::LessEqual)
				opcode = IrOpcode::UnsignedLessEqual;
			else if (opcode == IrOpcode::GreaterThan)
				opcode = IrOpcode::UnsignedGreaterThan;
			else if (opcode == IrOpcode::GreaterEqual)
				opcode = IrOpcode::UnsignedGreaterEqual;
		}
	};

	if (op == "==" && !is_floating_point_op) {
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = result_var,
		};
		opcode = IrOpcode::Equal;
		applyPointerComparisonOverride(bin_op, opcode);
		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
	} else if (op == "!=" && !is_floating_point_op) {
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = result_var,
		};
		opcode = IrOpcode::NotEqual;
		applyPointerComparisonOverride(bin_op, opcode);
		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
	} else if (op == "<" && !is_floating_point_op) {
		opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedLessThan : IrOpcode::LessThan;
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = result_var,
		};
		applyPointerComparisonOverride(bin_op, opcode);
		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
	} else if (op == "<=" && !is_floating_point_op) {
		opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedLessEqual : IrOpcode::LessEqual;
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = result_var,
		};
		applyPointerComparisonOverride(bin_op, opcode);
		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
	} else if (op == ">" && !is_floating_point_op) {
		opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedGreaterThan : IrOpcode::GreaterThan;
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = result_var,
		};
		applyPointerComparisonOverride(bin_op, opcode);
		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
	} else if (op == ">=" && !is_floating_point_op) {
		opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedGreaterEqual : IrOpcode::GreaterEqual;
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = result_var,
		};
		applyPointerComparisonOverride(bin_op, opcode);
		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
	}
	// Compound assignment operations (typed)
	// For compound assignments, result is stored back in LHS variable
	// NOTE: Pointer compound assignments (ptr += int, ptr -= int) are handled earlier,
	// before type promotions, to avoid truncating the pointer
	else if (op == "+=") {
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = toIrValue(lhsExprResult.value), // Store result in LHS variable
		};
		ir_.addInstruction(IrInstruction(IrOpcode::AddAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		return lhsExprResult;
	} else if (op == "-=") {
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = toIrValue(lhsExprResult.value),
		};
		ir_.addInstruction(IrInstruction(IrOpcode::SubAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		return lhsExprResult;
	} else if (op == "*=") {
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = toIrValue(lhsExprResult.value),
		};
		ir_.addInstruction(IrInstruction(IrOpcode::MulAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		return lhsExprResult;
	} else if (op == "/=") {
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = toIrValue(lhsExprResult.value),
		};
		ir_.addInstruction(IrInstruction(IrOpcode::DivAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		return lhsExprResult;
	} else if (op == "%=") {
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = toIrValue(lhsExprResult.value),
		};
		ir_.addInstruction(IrInstruction(IrOpcode::ModAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		return lhsExprResult;
	} else if (op == "&=") {
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = toIrValue(lhsExprResult.value),
		};
		ir_.addInstruction(IrInstruction(IrOpcode::AndAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		return lhsExprResult;
	} else if (op == "|=") {
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = toIrValue(lhsExprResult.value),
		};
		ir_.addInstruction(IrInstruction(IrOpcode::OrAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		return lhsExprResult;
	} else if (op == "^=") {
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = toIrValue(lhsExprResult.value),
		};
		ir_.addInstruction(IrInstruction(IrOpcode::XorAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		return lhsExprResult;
	} else if (op == "<<=") {
		BinaryOp bin_op{
		    .lhs = toTypedValue(lhsExprResult),
		    .rhs = toTypedValue(rhsExprResult),
		    .result = toIrValue(lhsExprResult.value),
		};
		ir_.addInstruction(IrInstruction(IrOpcode::ShlAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		return lhsExprResult;
	} else if (op == ">>=") {
		// For unsigned types, use logical shift right (SHR) instead of arithmetic (SAR).
		// ShrAssign always emits SAR which sign-extends the MSB — wrong for unsigned.
		if (is_unsigned_integer_type(commonType)) {
			// Decompose into: result = lhs >> rhs; store back to lhs
			TempVar shr_result = var_counter.next();
			BinaryOp shr_op{
			    .lhs = toTypedValue(lhsExprResult),
			    .rhs = toTypedValue(rhsExprResult),
			    .result = shr_result,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::UnsignedShiftRight, std::move(shr_op), binaryOperatorNode.get_token()));
			AssignmentOp assign_op;
			if (const auto* sh = std::get_if<StringHandle>(&lhsExprResult.value)) {
				assign_op.result = *sh;
				assign_op.lhs = toTypedValue(lhsExprResult);
			} else if (const auto* tv = std::get_if<TempVar>(&lhsExprResult.value)) {
				assign_op.result = *tv;
				assign_op.lhs = toTypedValue(lhsExprResult);
			} else {
				throw InternalError("Compound assignment LHS must be a variable");
			}
			assign_op.rhs = makeTypedValue(commonType, SizeInBits{get_type_size_bits(commonType)}, shr_result);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
		} else {
			BinaryOp bin_op{
			    .lhs = toTypedValue(lhsExprResult),
			    .rhs = toTypedValue(rhsExprResult),
			    .result = toIrValue(lhsExprResult.value),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::ShrAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		return lhsExprResult;
	} else if (is_floating_point_op) { // Floating-point operations
		// Float operations use typed BinaryOp
		if (op == "+" || op == "-" || op == "*" || op == "/") {
			// Determine float opcode
			IrOpcode float_opcode;
			if (op == "+")
				float_opcode = IrOpcode::FloatAdd;
			else if (op == "-")
				float_opcode = IrOpcode::FloatSubtract;
			else if (op == "*")
				float_opcode = IrOpcode::FloatMultiply;
			else if (op == "/")
				float_opcode = IrOpcode::FloatDivide;
			else {
				throw InternalError("Unsupported float operator");
				return ExprResult{};
			}

			// Create typed BinaryOp for float arithmetic
			BinaryOp bin_op{
			    .lhs = toTypedValue(lhsExprResult),
			    .rhs = toTypedValue(rhsExprResult),
			    .result = result_var,
			};

			ir_.addInstruction(IrInstruction(float_opcode, std::move(bin_op), binaryOperatorNode.get_token())); // Return the result variable with float type and size
			return makeExprResult(nativeTypeIndex(commonType), SizeInBits{get_type_size_bits(commonType)}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
		}

		// Float comparison operations use typed BinaryOp
		else if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
			// Determine float comparison opcode
			IrOpcode float_cmp_opcode;
			if (op == "==")
				float_cmp_opcode = IrOpcode::FloatEqual;
			else if (op == "!=")
				float_cmp_opcode = IrOpcode::FloatNotEqual;
			else if (op == "<")
				float_cmp_opcode = IrOpcode::FloatLessThan;
			else if (op == "<=")
				float_cmp_opcode = IrOpcode::FloatLessEqual;
			else if (op == ">")
				float_cmp_opcode = IrOpcode::FloatGreaterThan;
			else if (op == ">=")
				float_cmp_opcode = IrOpcode::FloatGreaterEqual;
			else {
				throw InternalError("Unsupported float comparison operator");
				return ExprResult{};
			}

			// Create typed BinaryOp for float comparison
			BinaryOp bin_op{
			    .lhs = toTypedValue(lhsExprResult),
			    .rhs = toTypedValue(rhsExprResult),
			    .result = result_var,
			};

			ir_.addInstruction(IrInstruction(float_cmp_opcode, std::move(bin_op), binaryOperatorNode.get_token()));

			// Float comparisons return boolean (bool8)
			return makeExprResult(nativeTypeIndex(TypeCategory::Bool), SizeInBits{8}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
		} else {
			// Unsupported floating-point operator
			throw InternalError("Unsupported floating-point binary operator");
			return ExprResult{};
		}
	}

	// For comparison operations, return boolean type (8 bits - bool size in C++)
	// For other operations, return the common type
	if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
		return makeExprResult(nativeTypeIndex(TypeCategory::Bool), SizeInBits{8}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
	} else {
		// Return the result variable with its type and size
		// Note: Assignment is handled earlier and returns before reaching this point
		return makeExprResult(nativeTypeIndex(commonType), SizeInBits{get_type_size_bits(commonType)}, IrOperand{result_var}, PointerDepth{}, ValueStorage::ContainsData);
	}
}

std::string_view AstToIr::generateMangledNameForCall(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& param_types, bool is_variadic, std::string_view struct_name, const std::vector<std::string>& namespace_path, bool is_const_method) {
	return NameMangling::generateMangledName(name, return_type, param_types, is_variadic, struct_name, namespace_path, Linkage::CPlusPlus, is_const_method).view();
}

std::string_view AstToIr::generateMangledNameForCall(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<ASTNode>& param_nodes, bool is_variadic, std::string_view struct_name, const std::vector<std::string>& namespace_path, bool is_const_method) {
	return NameMangling::generateMangledName(name, return_type, param_nodes, is_variadic, struct_name, namespace_path, Linkage::CPlusPlus, is_const_method).view();
}

std::string_view AstToIr::generateMangledNameForCall(const FunctionDeclarationNode& func_node, std::string_view struct_name_override, const std::vector<std::string>& namespace_path) {
	const DeclarationNode& decl_node = func_node.decl_node();
	const TypeSpecifierNode& return_type = decl_node.type_node().as<TypeSpecifierNode>();
	std::string_view func_name = decl_node.identifier_token().value();

	std::string_view struct_name = !struct_name_override.empty() ? struct_name_override
	                                                             : (func_node.is_member_function() ? func_node.parent_struct_name() : std::string_view{});

	// For member functions, resolve self-referential parameter types in template-instantiated
	// structs. When a template class has `operator+=(const W& other)`, the stored param type
	// still references the template base `W` (with total_size=0) instead of the instantiation
	// `W<int>`. Resolve by looking up the enclosing struct's type_index.
	if (!struct_name.empty()) {
		auto struct_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(struct_name));
		if (struct_it != getTypesByNameMap().end()) {
			TypeIndex struct_type_index = struct_it->second->type_index_;
			bool needs_resolution = false;
			// Check return type for self-referential struct
			if (return_type.category() == TypeCategory::Struct && return_type.type_index().is_valid()) {
				const TypeInfo* rti = tryGetTypeInfo(return_type.type_index());
				if (!rti || !rti->struct_info_ || rti->struct_info_->total_size == 0) {
					needs_resolution = true;
				}
			}
			if (!needs_resolution) {
				for (const auto& param : func_node.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const auto& pt = param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
						if (pt.category() == TypeCategory::Struct && pt.type_index().is_valid()) {
							const TypeInfo* ti = tryGetTypeInfo(pt.type_index());
							if (!ti || !ti->struct_info_ || ti->struct_info_->total_size == 0) {
								needs_resolution = true;
								break;
							}
						}
					}
				}
			}
			if (needs_resolution) {
				std::vector<TypeSpecifierNode> resolved_params;
				resolved_params.reserve(func_node.parameter_nodes().size());
				for (const auto& param : func_node.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						TypeSpecifierNode pt = param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
						resolveSelfReferentialType(pt, struct_type_index);
						resolved_params.push_back(pt);
					}
				}
				TypeSpecifierNode resolved_return_type_copy = return_type;
				resolveSelfReferentialType(resolved_return_type_copy, struct_type_index);
				return NameMangling::generateMangledName(func_name, resolved_return_type_copy, resolved_params,
				                                         func_node.is_variadic(), struct_name, namespace_path, func_node.linkage(),
				                                         func_node.is_const_member_function())
				    .view();
			}
		}
	}

	// Pass linkage from the function node to ensure extern "C" functions aren't mangled
	return NameMangling::generateMangledName(func_name, return_type, func_node.parameter_nodes(),
	                                         func_node.is_variadic(), struct_name, namespace_path, func_node.linkage(),
	                                         func_node.is_const_member_function())
	    .view();
}

std::optional<ExprResult> AstToIr::tryGenerateIntrinsicIr(std::string_view func_name, const FunctionCallNode& functionCallNode) {
	// Lookup table for intrinsic handlers using if-else chain
	// More maintainable than multiple nested if statements

	// Variadic argument intrinsics
	if (func_name == "__builtin_va_start" || func_name == "__va_start") {
		return generateVaStartIntrinsic(functionCallNode);
	}
	if (func_name == "__builtin_va_arg") {
		return generateVaArgIntrinsic(functionCallNode);
	}

	// Integer abs intrinsics
	if (func_name == "__builtin_labs" || func_name == "__builtin_llabs") {
		return generateBuiltinAbsIntIntrinsic(functionCallNode);
	}

	// Floating point abs intrinsics
	if (func_name == "__builtin_fabs" || func_name == "__builtin_fabsf" || func_name == "__builtin_fabsl") {
		return generateBuiltinAbsFloatIntrinsic(functionCallNode, func_name);
	}

	// Optimization hint intrinsics
	if (func_name == "__builtin_unreachable") {
		return generateBuiltinUnreachableIntrinsic(functionCallNode);
	}
	if (func_name == "__builtin_assume") {
		return generateBuiltinAssumeIntrinsic(functionCallNode);
	}
	if (func_name == "__builtin_expect") {
		return generateBuiltinExpectIntrinsic(functionCallNode);
	}
	if (func_name == "__builtin_launder") {
		return generateBuiltinLaunderIntrinsic(functionCallNode);
	}

	// __builtin_strlen - maps to libc strlen function, not an inline intrinsic
	// Return std::nullopt to fall through to regular function call handling,
	// but the function name will be remapped in generateFunctionCallIr

	// SEH exception intrinsics
	if (func_name == "GetExceptionCode" || func_name == "_exception_code") {
		return generateGetExceptionCodeIntrinsic(functionCallNode);
	}
	if (func_name == "GetExceptionInformation" || func_name == "_exception_info") {
		return generateGetExceptionInformationIntrinsic(functionCallNode);
	}
	if (func_name == "_abnormal_termination" || func_name == "AbnormalTermination") {
		return generateAbnormalTerminationIntrinsic(functionCallNode);
	}

	return std::nullopt; // Not an intrinsic
}

ExprResult AstToIr::generateBuiltinAbsIntIntrinsic(const FunctionCallNode& functionCallNode) {
	if (functionCallNode.arguments().size() != 1) {
		FLASH_LOG(Codegen, Error, "__builtin_labs/__builtin_llabs requires exactly 1 argument");
		return makeExprResult(nativeTypeIndex(TypeCategory::Long), SizeInBits{64}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
	}

	// Get the argument
	ASTNode arg = functionCallNode.arguments()[0];
	ExprResult arg_result = visitExpressionNode(arg.as<ExpressionNode>());

	// Extract argument details
	TypeCategory arg_type = arg_result.typeEnum();
	int arg_size = arg_result.size_in_bits.value;
	TypedValue arg_value = toTypedValue(arg_result);

	// Step 1: Arithmetic shift right by 63 to get sign mask (all 1s if negative, all 0s if positive)
	TempVar sign_mask = var_counter.next();
	BinaryOp shift_op{
	    .lhs = arg_value,
	    .rhs = makeTypedValue(TypeCategory::Int, SizeInBits{32}, 63ULL),
	    .result = sign_mask};
	ir_.addInstruction(IrInstruction(IrOpcode::ShiftRight, std::move(shift_op), functionCallNode.called_from()));

	// Step 2: XOR with sign mask
	TempVar xor_result = var_counter.next();
	BinaryOp xor_op{
	    .lhs = arg_value,
	    .rhs = makeTypedValue(arg_type, SizeInBits{static_cast<int>(arg_size)}, sign_mask),
	    .result = xor_result};
	ir_.addInstruction(IrInstruction(IrOpcode::BitwiseXor, std::move(xor_op), functionCallNode.called_from()));

	// Step 3: Subtract sign mask
	TempVar abs_result = var_counter.next();
	BinaryOp sub_op{
	    .lhs = makeTypedValue(arg_type, SizeInBits{static_cast<int>(arg_size)}, xor_result),
	    .rhs = makeTypedValue(arg_type, SizeInBits{static_cast<int>(arg_size)}, sign_mask),
	    .result = abs_result};
	ir_.addInstruction(IrInstruction(IrOpcode::Subtract, std::move(sub_op), functionCallNode.called_from()));

	return makeExprResult(nativeTypeIndex(arg_type), SizeInBits{static_cast<int>(arg_size)}, IrOperand{abs_result}, PointerDepth{}, ValueStorage::ContainsData);
}

ExprResult AstToIr::generateBuiltinAbsFloatIntrinsic(const FunctionCallNode& functionCallNode, std::string_view func_name) {
	if (functionCallNode.arguments().size() != 1) {
		FLASH_LOG(Codegen, Error, func_name, " requires exactly 1 argument");
		return makeExprResult(nativeTypeIndex(TypeCategory::Double), SizeInBits{64}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
	}

	// Get the argument
	ASTNode arg = functionCallNode.arguments()[0];
	ExprResult arg_result = visitExpressionNode(arg.as<ExpressionNode>());

	// Extract argument details
	TypeCategory arg_type = arg_result.typeEnum();
	int arg_size = arg_result.size_in_bits.value;
	TypedValue arg_value = toTypedValue(arg_result);

	// For floating point abs, clear the sign bit using bitwise AND
	// Float (32-bit): AND with 0x7FFFFFFF
	// Double (64-bit): AND with 0x7FFFFFFFFFFFFFFF
	unsigned long long mask = (arg_size == 32) ? 0x7FFFFFFFULL : 0x7FFFFFFFFFFFFFFFULL;

	TempVar abs_result = var_counter.next();
	BinaryOp and_op{
	    .lhs = arg_value,
	    .rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{static_cast<int>(arg_size)}, mask),
	    .result = abs_result};
	ir_.addInstruction(IrInstruction(IrOpcode::BitwiseAnd, std::move(and_op), functionCallNode.called_from()));

	return makeExprResult(nativeTypeIndex(arg_type), SizeInBits{static_cast<int>(arg_size)}, IrOperand{abs_result}, PointerDepth{}, ValueStorage::ContainsData);
}

bool AstToIr::isVaListPointerType(const ASTNode& arg, const ExprResult& ir_result) const {
	// Check if the argument is an identifier with pointer type
	if (arg.is<ExpressionNode>() && std::holds_alternative<IdentifierNode>(arg.as<ExpressionNode>())) {
		const auto& id = std::get<IdentifierNode>(arg.as<ExpressionNode>());
		if (auto sym = symbol_table.lookup(id.name())) {
			if (sym->is<DeclarationNode>()) {
				const auto& ty = sym->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
				if (ty.pointer_depth() > 0)
					return true;
			} else if (sym->is<VariableDeclarationNode>()) {
				const auto& ty = sym->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
				if (ty.pointer_depth() > 0)
					return true;
			}
		}
	}

	// Fallback: treat as pointer when operand size is pointer sized (common for typedef char*)
	if (ir_result.size_in_bits == SizeInBits{POINTER_SIZE_BITS}) {
		return true;
	}

	return false;
}

ExprResult AstToIr::generateVaArgIntrinsic(const FunctionCallNode& functionCallNode) {
	// __builtin_va_arg takes 2 arguments: va_list variable and type
	// After preprocessing: __builtin_va_arg(args, int) - parser sees this as function call with 2 args
	if (functionCallNode.arguments().size() != 2) {
		FLASH_LOG(Codegen, Error, "__builtin_va_arg requires exactly 2 arguments (va_list and type)");
		return makeExprResult(nativeTypeIndex(TypeCategory::Void), SizeInBits{0}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
	}

	// Get the first argument (va_list variable)
	ASTNode arg0 = functionCallNode.arguments()[0];
	ExprResult vaListExprResult = visitExpressionNode(arg0.as<ExpressionNode>());

	// Get the second argument (type identifier or type specifier)
	ASTNode arg1 = functionCallNode.arguments()[1];

	// Extract type information from the second argument
	TypeCategory requested_type = TypeCategory::Int;
	int requested_size = 32;
	bool is_float_type = false;

	// The second argument can be either an IdentifierNode (from old macro) or TypeSpecifierNode (from new parser)
	// TypeSpecifierNode is stored directly in ASTNode, not in ExpressionNode
	if (arg1.is<TypeSpecifierNode>()) {
		// New parser path: TypeSpecifierNode passed directly
		const auto& type_spec = arg1.as<TypeSpecifierNode>();
		requested_type = type_spec.type();
		requested_size = static_cast<int>(type_spec.size_in_bits());
		is_float_type = (requested_type == TypeCategory::Float || requested_type == TypeCategory::Double);
	} else if (arg1.is<ExpressionNode>() && std::holds_alternative<IdentifierNode>(arg1.as<ExpressionNode>())) {
		// Old path: IdentifierNode with type name
		std::string_view type_name = std::get<IdentifierNode>(arg1.as<ExpressionNode>()).name();

		// Map type names to Type enum
		if (type_name == "int") {
			requested_type = TypeCategory::Int;
			requested_size = 32;
		} else if (type_name == "double") {
			requested_type = TypeCategory::Double;
			requested_size = 64;
			is_float_type = true;
		} else if (type_name == "float") {
			requested_type = TypeCategory::Float;
			requested_size = 32;
			is_float_type = true;
		} else if (type_name == "long") {
			requested_type = TypeCategory::Long;
			requested_size = 64;
		} else if (type_name == "char") {
			requested_type = TypeCategory::Char;
			requested_size = 8;
		} else {
			// Default to int
			requested_type = TypeCategory::Int;
			requested_size = 32;
		}
	}

	// va_list_ir[2] contains the variable/temp identifier
	std::variant<StringHandle, TempVar> va_list_var;
	if (const auto* temp_var = std::get_if<TempVar>(&vaListExprResult.value)) {
		va_list_var = *temp_var;
	} else if (const auto* string = std::get_if<StringHandle>(&vaListExprResult.value)) {
		va_list_var = *string;
	} else {
		FLASH_LOG(Codegen, Error, "__builtin_va_arg first argument must be a variable");
		return makeExprResult(nativeTypeIndex(TypeCategory::Void), SizeInBits{0}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
	}

	// Detect if the user's va_list is a pointer type (e.g., typedef char* va_list;)
	// This must match the detection logic in generateVaStartIntrinsic
	bool va_list_is_pointer = isVaListPointerType(arg0, vaListExprResult);

	if (context_->isItaniumMangling() && !va_list_is_pointer) {
		// Linux/System V AMD64 ABI: Use va_list structure
		// va_list points to a structure with:
		//   unsigned int gp_offset;      (offset 0)
		//   unsigned int fp_offset;      (offset 4)
		//   void *overflow_arg_area;     (offset 8)
		//   void *reg_save_area;         (offset 16)

		// The va_list variable is a char* that points to the va_list structure.
		// We need to load this pointer value into a TempVar.
		TempVar va_list_struct_ptr;
		if (const auto* temp_var = std::get_if<TempVar>(&va_list_var)) {
			// va_list is already a TempVar - use it directly
			va_list_struct_ptr = *temp_var;
		} else {
			// va_list is a variable name - load its value (which is a pointer) into a TempVar
			va_list_struct_ptr = var_counter.next();
			StringHandle var_name_handle = std::get<StringHandle>(va_list_var);

			// Use Assignment to load the pointer value from the variable
			AssignmentOp load_pointer;
			load_pointer.result = va_list_struct_ptr;
			load_pointer.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_list_struct_ptr);
			load_pointer.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, var_name_handle);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(load_pointer), functionCallNode.called_from()));
		}

		// Step 2: Compute address of the appropriate offset field (gp_offset for ints, fp_offset for floats)
		// Step 3: Load current offset value (32-bit unsigned) from the offset field
		TempVar current_offset = var_counter.next();
		DereferenceOp load_offset;
		load_offset.result = current_offset;
		load_offset.pointer.setType(TypeCategory::UnsignedInt); // Reading a 32-bit unsigned offset
		load_offset.pointer.type_index = nativeTypeIndex(TypeCategory::UnsignedInt);
		load_offset.pointer.ir_type = IrType::Integer;
		load_offset.pointer.size_in_bits = SizeInBits{32}; // gp_offset/fp_offset is 32 bits
		load_offset.pointer.pointer_depth = PointerDepth{1};

		if (is_float_type) {
			// fp_offset is at offset 4 - compute va_list_struct_ptr + 4
			TempVar fp_offset_addr = var_counter.next();
			BinaryOp fp_offset_calc;
			fp_offset_calc.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_list_struct_ptr);
			fp_offset_calc.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, 4ULL);
			fp_offset_calc.result = fp_offset_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(fp_offset_calc), functionCallNode.called_from()));

			// Materialize the address before using it
			TempVar materialized_fp_addr = var_counter.next();
			AssignmentOp materialize;
			materialize.result = materialized_fp_addr;
			materialize.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, materialized_fp_addr);
			materialize.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, fp_offset_addr);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize), functionCallNode.called_from()));

			// Read 32-bit fp_offset value from [va_list_struct + 4]
			load_offset.pointer.value = materialized_fp_addr;
		} else {
			// gp_offset is at offset 0 - read directly from va_list_struct_ptr
			// Read 32-bit gp_offset value from [va_list_struct + 0]
			load_offset.pointer.value = va_list_struct_ptr;
		}

		ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(load_offset), functionCallNode.called_from()));

		// Phase 4: Overflow support - check if offset >= limit and use overflow_arg_area if so
		// For integers: gp_offset limit is 48 (6 registers * 8 bytes)
		// For floats: fp_offset limit is 176 (48 + 8 registers * 16 bytes)
		static size_t va_arg_counter = 0;
		size_t current_va_arg = va_arg_counter++;
		auto reg_path_label = StringTable::createStringHandle(StringBuilder().append("va_arg_reg_").append(current_va_arg));
		auto overflow_path_label = StringTable::createStringHandle(StringBuilder().append("va_arg_overflow_").append(current_va_arg));
		auto va_arg_end_label = StringTable::createStringHandle(StringBuilder().append("va_arg_end_").append(current_va_arg));

		// Allocate result variable that will be assigned in both paths
		TempVar value = var_counter.next();

		// Calculate the slot size for integer types based on the type size
		// For floats: 16 bytes (XMM register), for integers: round up to 8-byte boundary
		// System V AMD64 ABI: structs up to 16 bytes use 1-2 register slots
		unsigned long long slot_size = is_float_type ? 16ULL : ((requested_size + 63) / 64) * 8;

		// Compare current_offset < limit (48 for int, 176 for float)
		// For larger types, we need to check if there's enough space for the full type
		unsigned long long offset_limit = is_float_type ? 176ULL : 48ULL;
		TempVar cmp_result = var_counter.next();
		BinaryOp compare_op;
		compare_op.lhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, current_offset);
		// Adjust limit: need to have slot_size bytes remaining
		compare_op.rhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, offset_limit - slot_size + 8);
		compare_op.result = cmp_result;
		ir_.addInstruction(IrInstruction(IrOpcode::UnsignedLessThan, std::move(compare_op), functionCallNode.called_from()));

		// Conditional branch: if (current_offset < limit) goto reg_path else goto overflow_path
		CondBranchOp cond_branch;
		cond_branch.label_true = reg_path_label;
		cond_branch.label_false = overflow_path_label;
		cond_branch.condition = makeTypedValue(TypeCategory::Bool, SizeInBits{1}, cmp_result);
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), functionCallNode.called_from()));

		// ============ REGISTER PATH ============
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = reg_path_label}, functionCallNode.called_from()));

		// Step 4: Load reg_save_area pointer (at offset 16)
		TempVar reg_save_area_field_addr = var_counter.next();
		BinaryOp reg_save_addr;
		reg_save_addr.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_list_struct_ptr);
		reg_save_addr.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, 16ULL);
		reg_save_addr.result = reg_save_area_field_addr;
		ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(reg_save_addr), functionCallNode.called_from()));

		// Materialize the address before using it
		TempVar materialized_reg_save_addr = var_counter.next();
		AssignmentOp materialize_reg;
		materialize_reg.result = materialized_reg_save_addr;
		materialize_reg.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, materialized_reg_save_addr);
		materialize_reg.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, reg_save_area_field_addr);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_reg), functionCallNode.called_from()));

		TempVar reg_save_area_ptr = var_counter.next();
		DereferenceOp load_reg_save_ptr;
		load_reg_save_ptr.result = reg_save_area_ptr;
		load_reg_save_ptr.pointer.setType(TypeCategory::UnsignedLongLong);
		load_reg_save_ptr.pointer.type_index = nativeTypeIndex(TypeCategory::UnsignedLongLong);
		load_reg_save_ptr.pointer.ir_type = IrType::Integer;
		load_reg_save_ptr.pointer.size_in_bits = SizeInBits{64}; // Pointer is always 64 bits
		load_reg_save_ptr.pointer.pointer_depth = PointerDepth{1};
		load_reg_save_ptr.pointer.value = materialized_reg_save_addr;
		ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(load_reg_save_ptr), functionCallNode.called_from()));

		// Step 5: Compute address: reg_save_area + current_offset
		TempVar arg_addr = var_counter.next();
		BinaryOp compute_addr;
		compute_addr.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, reg_save_area_ptr);
		// Need to convert offset from uint32 to uint64 for addition
		TempVar offset_64 = var_counter.next();
		AssignmentOp convert_offset;
		convert_offset.result = offset_64;
		convert_offset.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, offset_64);
		convert_offset.rhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, current_offset);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(convert_offset), functionCallNode.called_from()));

		compute_addr.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, offset_64);
		compute_addr.result = arg_addr;
		ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(compute_addr), functionCallNode.called_from()));

		// Step 6: Read the value at arg_addr
		TempVar reg_value = var_counter.next();
		DereferenceOp read_reg_value;
		read_reg_value.result = reg_value;
		read_reg_value.pointer.setType(requested_type);
		read_reg_value.pointer.type_index = nativeTypeIndex(requested_type);
		read_reg_value.pointer.size_in_bits = SizeInBits{static_cast<int>(requested_size)};
		read_reg_value.pointer.pointer_depth = PointerDepth{1};
		read_reg_value.pointer.value = arg_addr;
		ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(read_reg_value), functionCallNode.called_from()));

		// Assign to result variable
		AssignmentOp assign_reg_result;
		assign_reg_result.result = value;
		assign_reg_result.lhs = makeTypedValue(requested_type, SizeInBits{static_cast<int>(requested_size)}, value);
		assign_reg_result.rhs = makeTypedValue(requested_type, SizeInBits{static_cast<int>(requested_size)}, reg_value);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_reg_result), functionCallNode.called_from()));

		// Step 7: Increment the offset by slot_size and store back
		// slot_size is 16 for floats (XMM regs), or rounded up to 8-byte boundary for integers/structs
		TempVar new_offset = var_counter.next();
		BinaryOp increment_offset;
		increment_offset.lhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, current_offset);
		increment_offset.rhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, slot_size);
		increment_offset.result = new_offset;
		ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(increment_offset), functionCallNode.called_from()));

		// Step 8: Store updated offset back to the appropriate field in the structure
		TempVar materialized_offset = var_counter.next();
		AssignmentOp materialize;
		materialize.result = materialized_offset;
		materialize.lhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, materialized_offset);
		materialize.rhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, new_offset);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize), functionCallNode.called_from()));

		DereferenceStoreOp store_offset;
		store_offset.pointer.setType(TypeCategory::UnsignedInt);
		store_offset.pointer.type_index = nativeTypeIndex(TypeCategory::UnsignedInt);
		store_offset.pointer.ir_type = IrType::Integer;
		store_offset.pointer.size_in_bits = SizeInBits{64}; // Pointer is always 64 bits
		store_offset.pointer.pointer_depth = PointerDepth{1};
		if (is_float_type) {
			// Store to fp_offset field at offset 4
			TempVar fp_offset_store_addr = var_counter.next();
			BinaryOp fp_store_addr_calc;
			fp_store_addr_calc.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_list_struct_ptr);
			fp_store_addr_calc.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, 4ULL);
			fp_store_addr_calc.result = fp_offset_store_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(fp_store_addr_calc), functionCallNode.called_from()));

			TempVar materialized_addr = var_counter.next();
			AssignmentOp materialize_addr;
			materialize_addr.result = materialized_addr;
			materialize_addr.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, materialized_addr);
			materialize_addr.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, fp_offset_store_addr);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_addr), functionCallNode.called_from()));

			store_offset.pointer.value = materialized_addr;
		} else {
			// Store to gp_offset field at offset 0
			store_offset.pointer.value = va_list_struct_ptr;
		}
		store_offset.value = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, materialized_offset);
		ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_offset), functionCallNode.called_from()));

		// Jump to end
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = va_arg_end_label}, functionCallNode.called_from()));

		// ============ OVERFLOW PATH ============
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = overflow_path_label}, functionCallNode.called_from()));

		// Load overflow_arg_area pointer (at offset 8)
		TempVar overflow_field_addr = var_counter.next();
		BinaryOp overflow_addr_calc;
		overflow_addr_calc.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_list_struct_ptr);
		overflow_addr_calc.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, 8ULL);
		overflow_addr_calc.result = overflow_field_addr;
		ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(overflow_addr_calc), functionCallNode.called_from()));

		// Materialize before dereferencing
		TempVar materialized_overflow_addr = var_counter.next();
		AssignmentOp materialize_overflow;
		materialize_overflow.result = materialized_overflow_addr;
		materialize_overflow.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, materialized_overflow_addr);
		materialize_overflow.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, overflow_field_addr);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_overflow), functionCallNode.called_from()));

		TempVar overflow_ptr = var_counter.next();
		DereferenceOp load_overflow_ptr;
		load_overflow_ptr.result = overflow_ptr;
		load_overflow_ptr.pointer.setType(TypeCategory::UnsignedLongLong);
		load_overflow_ptr.pointer.type_index = nativeTypeIndex(TypeCategory::UnsignedLongLong);
		load_overflow_ptr.pointer.ir_type = IrType::Integer;
		load_overflow_ptr.pointer.size_in_bits = SizeInBits{64};
		load_overflow_ptr.pointer.pointer_depth = PointerDepth{1};
		load_overflow_ptr.pointer.value = materialized_overflow_addr;
		ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(load_overflow_ptr), functionCallNode.called_from()));

		// Read value from overflow_arg_area
		TempVar overflow_value = var_counter.next();
		DereferenceOp read_overflow_value;
		read_overflow_value.result = overflow_value;
		read_overflow_value.pointer.setType(requested_type);
		read_overflow_value.pointer.type_index = nativeTypeIndex(requested_type);
		read_overflow_value.pointer.size_in_bits = SizeInBits{static_cast<int>(requested_size)};
		read_overflow_value.pointer.pointer_depth = PointerDepth{1};
		read_overflow_value.pointer.value = overflow_ptr;
		ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(read_overflow_value), functionCallNode.called_from()));

		// Assign to result variable
		AssignmentOp assign_overflow_result;
		assign_overflow_result.result = value;
		assign_overflow_result.lhs = makeTypedValue(requested_type, SizeInBits{static_cast<int>(requested_size)}, value);
		assign_overflow_result.rhs = makeTypedValue(requested_type, SizeInBits{static_cast<int>(requested_size)}, overflow_value);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_overflow_result), functionCallNode.called_from()));

		// Advance overflow_arg_area by the actual stack argument size (always 8 bytes on x64 stack)
		// Note: slot_size is for register save area; stack always uses 8-byte slots
		unsigned long long overflow_advance = (requested_size + 63) / 64 * 8; // Round up to 8-byte boundary
		TempVar new_overflow_ptr = var_counter.next();
		BinaryOp advance_overflow;
		advance_overflow.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, overflow_ptr);
		advance_overflow.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, overflow_advance);
		advance_overflow.result = new_overflow_ptr;
		ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(advance_overflow), functionCallNode.called_from()));

		// Store updated overflow_arg_area back to structure
		DereferenceStoreOp store_overflow;
		store_overflow.pointer.setType(TypeCategory::UnsignedLongLong);
		store_overflow.pointer.type_index = nativeTypeIndex(TypeCategory::UnsignedLongLong);
		store_overflow.pointer.ir_type = IrType::Integer;
		store_overflow.pointer.size_in_bits = SizeInBits{64};
		store_overflow.pointer.pointer_depth = PointerDepth{1};
		store_overflow.pointer.value = materialized_overflow_addr;
		store_overflow.value = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, new_overflow_ptr);
		ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_overflow), functionCallNode.called_from()));

		// ============ END LABEL ============
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = va_arg_end_label}, functionCallNode.called_from()));

		return makeExprResult(nativeTypeIndex(requested_type), SizeInBits{static_cast<int>(requested_size)}, IrOperand{value}, PointerDepth{}, ValueStorage::ContainsData);

	} else {
		// Windows/MSVC ABI or Linux with simple char* va_list
		// On Linux: va_start now points to the va_list structure, so use structure-based approach
		// On Windows: va_list is a simple pointer, use pointer-based approach

		if (context_->isItaniumMangling()) {
			// Linux/System V AMD64: char* va_list now points to va_list structure
			// Use the same structure-based approach with overflow support

			// Step 1: Load the va_list pointer (points to va_list structure)
			TempVar va_list_struct_ptr = var_counter.next();
			AssignmentOp load_ptr_op;
			load_ptr_op.result = va_list_struct_ptr;
			load_ptr_op.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_list_struct_ptr);
			if (const auto* string = std::get_if<StringHandle>(&va_list_var)) {
				load_ptr_op.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, *string);
			} else {
				load_ptr_op.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, std::get<TempVar>(va_list_var));
			}
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(load_ptr_op), functionCallNode.called_from()));

			// Load gp_offset (offset 0) for integers, or fp_offset (offset 4) for floats
			TempVar current_offset = var_counter.next();
			DereferenceOp load_offset;
			load_offset.result = current_offset;
			load_offset.pointer.setType(TypeCategory::UnsignedInt);
			load_offset.pointer.type_index = nativeTypeIndex(TypeCategory::UnsignedInt);
			load_offset.pointer.ir_type = IrType::Integer;
			load_offset.pointer.size_in_bits = SizeInBits{32};
			load_offset.pointer.pointer_depth = PointerDepth{1};

			if (is_float_type) {
				// fp_offset is at offset 4 - compute va_list_struct_ptr + 4
				TempVar fp_offset_addr = var_counter.next();
				BinaryOp fp_offset_calc;
				fp_offset_calc.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_list_struct_ptr);
				fp_offset_calc.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, 4ULL);
				fp_offset_calc.result = fp_offset_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(fp_offset_calc), functionCallNode.called_from()));

				// Materialize the address before using it
				TempVar materialized_fp_addr = var_counter.next();
				AssignmentOp materialize;
				materialize.result = materialized_fp_addr;
				materialize.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, materialized_fp_addr);
				materialize.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, fp_offset_addr);
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize), functionCallNode.called_from()));

				// Read 32-bit fp_offset value from [va_list_struct + 4]
				load_offset.pointer.value = materialized_fp_addr;
			} else {
				// gp_offset is at offset 0 - read directly from va_list_struct_ptr
				load_offset.pointer.value = va_list_struct_ptr;
			}
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(load_offset), functionCallNode.called_from()));

			// Phase 4: Overflow support with conditional branch
			static size_t va_arg_ptr_counter = 0;
			size_t current_va_arg = va_arg_ptr_counter++;
			auto reg_path_label = StringTable::createStringHandle(StringBuilder().append("va_arg_ptr_reg_").append(current_va_arg));
			auto overflow_path_label = StringTable::createStringHandle(StringBuilder().append("va_arg_ptr_overflow_").append(current_va_arg));
			auto va_arg_end_label = StringTable::createStringHandle(StringBuilder().append("va_arg_ptr_end_").append(current_va_arg));

			// Allocate result variable
			TempVar value = var_counter.next();

			// Calculate the slot size for integer types based on the type size
			// For floats: 16 bytes (XMM register), for integers: round up to 8-byte boundary
			unsigned long long slot_size = is_float_type ? 16ULL : ((requested_size + 63) / 64) * 8;

			// Compare current_offset < limit (48 for int, 176 for float)
			// For larger types, we need to check if there's enough space for the full type
			unsigned long long offset_limit = is_float_type ? 176ULL : 48ULL;
			TempVar cmp_result = var_counter.next();
			BinaryOp compare_op;
			compare_op.lhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, current_offset);
			// Adjust limit: need to have slot_size bytes remaining
			compare_op.rhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, offset_limit - slot_size + 8);
			compare_op.result = cmp_result;
			ir_.addInstruction(IrInstruction(IrOpcode::UnsignedLessThan, std::move(compare_op), functionCallNode.called_from()));

			// Conditional branch
			CondBranchOp cond_branch;
			cond_branch.label_true = reg_path_label;
			cond_branch.label_false = overflow_path_label;
			cond_branch.condition = makeTypedValue(TypeCategory::Bool, SizeInBits{1}, cmp_result);
			ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), functionCallNode.called_from()));

			// ============ REGISTER PATH ============
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = reg_path_label}, functionCallNode.called_from()));

			// Load reg_save_area pointer (at offset 16)
			TempVar reg_save_area_field_addr = var_counter.next();
			BinaryOp reg_save_addr;
			reg_save_addr.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_list_struct_ptr);
			reg_save_addr.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, 16ULL);
			reg_save_addr.result = reg_save_area_field_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(reg_save_addr), functionCallNode.called_from()));

			TempVar materialized_reg_save_addr = var_counter.next();
			AssignmentOp materialize_reg;
			materialize_reg.result = materialized_reg_save_addr;
			materialize_reg.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, materialized_reg_save_addr);
			materialize_reg.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, reg_save_area_field_addr);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_reg), functionCallNode.called_from()));

			TempVar reg_save_area_ptr = var_counter.next();
			DereferenceOp load_reg_save_ptr;
			load_reg_save_ptr.result = reg_save_area_ptr;
			load_reg_save_ptr.pointer.setType(TypeCategory::UnsignedLongLong);
			load_reg_save_ptr.pointer.type_index = nativeTypeIndex(TypeCategory::UnsignedLongLong);
			load_reg_save_ptr.pointer.ir_type = IrType::Integer;
			load_reg_save_ptr.pointer.size_in_bits = SizeInBits{64};
			load_reg_save_ptr.pointer.pointer_depth = PointerDepth{1};
			load_reg_save_ptr.pointer.value = materialized_reg_save_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(load_reg_save_ptr), functionCallNode.called_from()));

			// Compute address: reg_save_area + current_offset
			TempVar offset_64 = var_counter.next();
			AssignmentOp convert_offset;
			convert_offset.result = offset_64;
			convert_offset.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, offset_64);
			convert_offset.rhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, current_offset);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(convert_offset), functionCallNode.called_from()));

			TempVar arg_addr = var_counter.next();
			BinaryOp compute_addr;
			compute_addr.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, reg_save_area_ptr);
			compute_addr.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, offset_64);
			compute_addr.result = arg_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(compute_addr), functionCallNode.called_from()));

			// Read value
			TempVar reg_value = var_counter.next();
			DereferenceOp read_reg_value;
			read_reg_value.result = reg_value;
			read_reg_value.pointer.setType(requested_type);
			read_reg_value.pointer.type_index = nativeTypeIndex(requested_type);
			read_reg_value.pointer.size_in_bits = SizeInBits{static_cast<int>(requested_size)};
			read_reg_value.pointer.pointer_depth = PointerDepth{1};
			read_reg_value.pointer.value = arg_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(read_reg_value), functionCallNode.called_from()));

			// Assign to result
			AssignmentOp assign_reg_result;
			assign_reg_result.result = value;
			assign_reg_result.lhs = makeTypedValue(requested_type, SizeInBits{static_cast<int>(requested_size)}, value);
			assign_reg_result.rhs = makeTypedValue(requested_type, SizeInBits{static_cast<int>(requested_size)}, reg_value);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_reg_result), functionCallNode.called_from()));

			// Increment gp_offset by slot_size, or fp_offset by 16
			TempVar new_offset = var_counter.next();
			BinaryOp increment_offset;
			increment_offset.lhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, current_offset);
			increment_offset.rhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, slot_size);
			increment_offset.result = new_offset;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(increment_offset), functionCallNode.called_from()));

			TempVar materialized_offset = var_counter.next();
			AssignmentOp materialize_off;
			materialize_off.result = materialized_offset;
			materialize_off.lhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, materialized_offset);
			materialize_off.rhs = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, new_offset);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_off), functionCallNode.called_from()));

			DereferenceStoreOp store_offset;
			store_offset.pointer.setType(TypeCategory::UnsignedInt);
			store_offset.pointer.type_index = nativeTypeIndex(TypeCategory::UnsignedInt);
			store_offset.pointer.ir_type = IrType::Integer;
			store_offset.pointer.size_in_bits = SizeInBits{64};
			store_offset.pointer.pointer_depth = PointerDepth{1};
			if (is_float_type) {
				// Store to fp_offset field at offset 4
				TempVar fp_offset_store_addr = var_counter.next();
				BinaryOp fp_store_addr_calc;
				fp_store_addr_calc.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_list_struct_ptr);
				fp_store_addr_calc.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, 4ULL);
				fp_store_addr_calc.result = fp_offset_store_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(fp_store_addr_calc), functionCallNode.called_from()));

				TempVar materialized_addr = var_counter.next();
				AssignmentOp materialize_addr;
				materialize_addr.result = materialized_addr;
				materialize_addr.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, materialized_addr);
				materialize_addr.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, fp_offset_store_addr);
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_addr), functionCallNode.called_from()));

				store_offset.pointer.value = materialized_addr;
			} else {
				// Store to gp_offset field at offset 0
				store_offset.pointer.value = va_list_struct_ptr;
			}
			store_offset.value = makeTypedValue(TypeCategory::UnsignedInt, SizeInBits{32}, materialized_offset);
			ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_offset), functionCallNode.called_from()));

			// Jump to end
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = va_arg_end_label}, functionCallNode.called_from()));

			// ============ OVERFLOW PATH ============
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = overflow_path_label}, functionCallNode.called_from()));

			// Load overflow_arg_area (at offset 8)
			TempVar overflow_field_addr = var_counter.next();
			BinaryOp overflow_addr_calc;
			overflow_addr_calc.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_list_struct_ptr);
			overflow_addr_calc.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, 8ULL);
			overflow_addr_calc.result = overflow_field_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(overflow_addr_calc), functionCallNode.called_from()));

			TempVar materialized_overflow_addr = var_counter.next();
			AssignmentOp materialize_overflow;
			materialize_overflow.result = materialized_overflow_addr;
			materialize_overflow.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, materialized_overflow_addr);
			materialize_overflow.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, overflow_field_addr);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_overflow), functionCallNode.called_from()));

			TempVar overflow_ptr = var_counter.next();
			DereferenceOp load_overflow_ptr;
			load_overflow_ptr.result = overflow_ptr;
			load_overflow_ptr.pointer.setType(TypeCategory::UnsignedLongLong);
			load_overflow_ptr.pointer.type_index = nativeTypeIndex(TypeCategory::UnsignedLongLong);
			load_overflow_ptr.pointer.ir_type = IrType::Integer;
			load_overflow_ptr.pointer.size_in_bits = SizeInBits{64};
			load_overflow_ptr.pointer.pointer_depth = PointerDepth{1};
			load_overflow_ptr.pointer.value = materialized_overflow_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(load_overflow_ptr), functionCallNode.called_from()));

			// Read value from overflow area
			TempVar overflow_value = var_counter.next();
			DereferenceOp read_overflow_value;
			read_overflow_value.result = overflow_value;
			read_overflow_value.pointer.setType(requested_type);
			read_overflow_value.pointer.type_index = nativeTypeIndex(requested_type);
			read_overflow_value.pointer.size_in_bits = SizeInBits{static_cast<int>(requested_size)};
			read_overflow_value.pointer.pointer_depth = PointerDepth{1};
			read_overflow_value.pointer.value = overflow_ptr;
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(read_overflow_value), functionCallNode.called_from()));

			// Assign to result
			AssignmentOp assign_overflow_result;
			assign_overflow_result.result = value;
			assign_overflow_result.lhs = makeTypedValue(requested_type, SizeInBits{static_cast<int>(requested_size)}, value);
			assign_overflow_result.rhs = makeTypedValue(requested_type, SizeInBits{static_cast<int>(requested_size)}, overflow_value);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_overflow_result), functionCallNode.called_from()));

			// Advance overflow_arg_area by the actual stack argument size (always 8 bytes per slot on x64 stack)
			unsigned long long overflow_advance = (requested_size + 63) / 64 * 8; // Round up to 8-byte boundary
			TempVar new_overflow_ptr = var_counter.next();
			BinaryOp advance_overflow;
			advance_overflow.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, overflow_ptr);
			advance_overflow.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, overflow_advance);
			advance_overflow.result = new_overflow_ptr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(advance_overflow), functionCallNode.called_from()));

			DereferenceStoreOp store_overflow;
			store_overflow.pointer.setType(TypeCategory::UnsignedLongLong);
			store_overflow.pointer.type_index = nativeTypeIndex(TypeCategory::UnsignedLongLong);
			store_overflow.pointer.ir_type = IrType::Integer;
			store_overflow.pointer.size_in_bits = SizeInBits{64};
			store_overflow.pointer.pointer_depth = PointerDepth{1};
			store_overflow.pointer.value = materialized_overflow_addr;
			store_overflow.value = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, new_overflow_ptr);
			ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_overflow), functionCallNode.called_from()));

			// ============ END LABEL ============
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = va_arg_end_label}, functionCallNode.called_from()));

			return makeExprResult(nativeTypeIndex(requested_type), SizeInBits{static_cast<int>(requested_size)}, IrOperand{value}, PointerDepth{}, ValueStorage::ContainsData);

		} else {
			// Windows/MSVC ABI: Simple pointer-based approach
			// va_list is a char* that directly holds the address of the next variadic argument

			// Step 1: Load the current pointer value from va_list variable
			TempVar current_ptr = var_counter.next();
			AssignmentOp load_ptr_op;
			load_ptr_op.result = current_ptr;
			load_ptr_op.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, current_ptr);
			if (const auto* string = std::get_if<StringHandle>(&va_list_var)) {
				load_ptr_op.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, *string);
			} else {
				load_ptr_op.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, std::get<TempVar>(va_list_var));
			}
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(load_ptr_op), functionCallNode.called_from()));

			// Step 2: Read the value at the current pointer
			// Win64 ABI: structs > 8 bytes are passed by pointer in variadic calls,
			// so the stack slot holds a pointer to the struct, not the struct itself.
			// We need to read the pointer first, then dereference it.
			bool is_indirect_struct = (requested_type == TypeCategory::Struct && requested_size > 64);

			TempVar value = var_counter.next();
			if (is_indirect_struct) {
				// Large struct: stack slot contains a pointer to the struct
				// Step 2a: Read the pointer from the stack slot
				TempVar struct_ptr = var_counter.next();
				DereferenceOp deref_ptr_op;
				deref_ptr_op.result = struct_ptr;
				deref_ptr_op.pointer.setType(TypeCategory::UnsignedLongLong);
				deref_ptr_op.pointer.type_index = nativeTypeIndex(TypeCategory::UnsignedLongLong);
				deref_ptr_op.pointer.ir_type = IrType::Integer;
				deref_ptr_op.pointer.size_in_bits = SizeInBits{64};
				deref_ptr_op.pointer.pointer_depth = PointerDepth{1};
				deref_ptr_op.pointer.value = current_ptr;
				ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_ptr_op), functionCallNode.called_from()));

				// Step 2b: Dereference the struct pointer to get the actual struct
				DereferenceOp deref_struct_op;
				deref_struct_op.result = value;
				deref_struct_op.pointer.setType(requested_type);
				deref_struct_op.pointer.type_index = nativeTypeIndex(requested_type);
				deref_struct_op.pointer.size_in_bits = SizeInBits{static_cast<int>(requested_size)};
				deref_struct_op.pointer.pointer_depth = PointerDepth{1};
				deref_struct_op.pointer.value = struct_ptr;
				ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_struct_op), functionCallNode.called_from()));
			} else {
				// Small types (≤8 bytes): read value directly from stack slot
				DereferenceOp deref_value_op;
				deref_value_op.result = value;
				deref_value_op.pointer.setType(requested_type);
				deref_value_op.pointer.type_index = nativeTypeIndex(requested_type);
				deref_value_op.pointer.size_in_bits = SizeInBits{static_cast<int>(requested_size)};
				deref_value_op.pointer.pointer_depth = PointerDepth{1};
				deref_value_op.pointer.value = current_ptr;
				ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_value_op), functionCallNode.called_from()));
			}

			// Step 3: Advance va_list by 8 bytes (always 8 - even for large structs,
			// since the stack slot holds a pointer, not the struct itself)
			TempVar next_ptr = var_counter.next();
			BinaryOp add_op;
			add_op.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, current_ptr);
			add_op.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, 8ULL);
			add_op.result = next_ptr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), functionCallNode.called_from()));

			// Step 4: Store the updated pointer back to va_list
			AssignmentOp assign_op;
			assign_op.result = var_counter.next(); // unused but required
			if (const auto* temp_var = std::get_if<TempVar>(&va_list_var)) {
				assign_op.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, *temp_var);
			} else {
				assign_op.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, std::get<StringHandle>(va_list_var));
			}
			assign_op.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, next_ptr);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), functionCallNode.called_from()));

			return makeExprResult(nativeTypeIndex(requested_type), SizeInBits{static_cast<int>(requested_size)}, IrOperand{value}, PointerDepth{}, ValueStorage::ContainsData);
		}
	}
}

ExprResult AstToIr::generateVaStartIntrinsic(const FunctionCallNode& functionCallNode) {
	// __builtin_va_start takes 2 arguments: va_list (not pointer!), and last fixed parameter
	if (functionCallNode.arguments().size() != 2) {
		FLASH_LOG(Codegen, Error, "__builtin_va_start requires exactly 2 arguments");
		return makeExprResult(nativeTypeIndex(TypeCategory::Void), SizeInBits{0}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
	}

	// Get the first argument (va_list variable)
	ASTNode arg0 = functionCallNode.arguments()[0];
	ExprResult arg0ExprResult = visitExpressionNode(arg0.as<ExpressionNode>());

	// Get the va_list variable name (needed for assignment later)
	StringHandle va_list_name_handle;
	if (std::holds_alternative<IdentifierNode>(arg0.as<ExpressionNode>())) {
		const auto& id = std::get<IdentifierNode>(arg0.as<ExpressionNode>());
		va_list_name_handle = StringTable::getOrInternStringHandle(id.name());
	}

	// Detect if the user's va_list is a pointer type (e.g., typedef char* va_list;)
	bool va_list_is_pointer = isVaListPointerType(arg0, arg0ExprResult);

	// Get the second argument (last fixed parameter)
	ASTNode arg1 = functionCallNode.arguments()[1];
	[[maybe_unused]] auto arg1_ir = visitExpressionNode(arg1.as<ExpressionNode>());

	// The second argument should be an identifier (the parameter name)
	std::string_view last_param_name;
	if (std::holds_alternative<IdentifierNode>(arg1.as<ExpressionNode>())) {
		last_param_name = std::get<IdentifierNode>(arg1.as<ExpressionNode>()).name();
	} else {
		FLASH_LOG(Codegen, Error, "__builtin_va_start second argument must be a parameter name");
		return makeExprResult(nativeTypeIndex(TypeCategory::Void), SizeInBits{0}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
	}

	// Platform-specific varargs implementation:
	// - Windows (MSVC mangling): variadic args on stack, use &last_param + 8
	// - Linux (Itanium mangling): variadic args in registers, initialize va_list structure

	if (context_->isItaniumMangling() && !va_list_is_pointer) {
		// Linux/System V AMD64 ABI: Use va_list structure
		// The structure has already been initialized in the function prologue by IRConverter.
		// We just need to assign the address of the va_list structure to the user's va_list variable.

		// Get address of the va_list structure
		TempVar va_list_struct_addr = emitAddressOf(TypeCategory::Char, 8, IrValue(StringTable::getOrInternStringHandle("__varargs_va_list_struct__"sv)), functionCallNode.called_from());

		// Finally, assign the address of the va_list structure to the user's va_list variable (char* pointer)
		// Get the va_list variable from arg0ExprResult.value
		std::variant<StringHandle, TempVar> va_list_var;
		if (va_list_name_handle.isValid()) {
			va_list_var = va_list_name_handle;
		} else if (const auto* temp_var = std::get_if<TempVar>(&arg0ExprResult.value)) {
			va_list_var = *temp_var;
		} else if (const auto* string = std::get_if<StringHandle>(&arg0ExprResult.value)) {
			va_list_var = *string;
		} else {
			FLASH_LOG(Codegen, Error, "__builtin_va_start first argument must be a variable or temp");
			return makeExprResult(nativeTypeIndex(TypeCategory::Void), SizeInBits{0}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
		}

		AssignmentOp final_assign;
		if (const auto* string = std::get_if<StringHandle>(&va_list_var)) {
			final_assign.result = *string;
			final_assign.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, *string);
		} else {
			final_assign.result = std::get<TempVar>(va_list_var);
			final_assign.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, std::get<TempVar>(va_list_var));
		}
		final_assign.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_list_struct_addr);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(final_assign), functionCallNode.called_from()));

	} else {
		// va_list is a simple char* pointer type (typedef char* va_list;)
		// On Windows: variadic args are on the stack, so use &last_param + 8
		// On Linux: variadic args are in registers saved to reg_save_area, point there instead

		std::variant<StringHandle, TempVar> va_list_var;
		if (va_list_name_handle.isValid()) {
			va_list_var = va_list_name_handle;
		} else if (const auto* temp_var = std::get_if<TempVar>(&arg0ExprResult.value)) {
			va_list_var = *temp_var;
		} else if (const auto* string = std::get_if<StringHandle>(&arg0ExprResult.value)) {
			va_list_var = *string;
		} else {
			FLASH_LOG(Codegen, Error, "__builtin_va_start first argument must be a variable or temp");
			return makeExprResult(nativeTypeIndex(TypeCategory::Void), SizeInBits{0}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
		}

		if (context_->isItaniumMangling()) {
			// Linux/System V AMD64: Use va_list structure internally even for char* va_list
			// Phase 4: Point to the va_list structure so va_arg can access gp_offset and overflow_arg_area
			// This enables proper overflow support when >5 variadic int args are passed

			// Get address of va_list structure
			TempVar va_struct_addr = emitAddressOf(TypeCategory::Char, 8, IrValue(StringTable::getOrInternStringHandle("__varargs_va_list_struct__"sv)), functionCallNode.called_from());

			// Assign to va_list variable
			AssignmentOp assign_op;
			if (const auto* string = std::get_if<StringHandle>(&va_list_var)) {
				assign_op.result = *string;
				assign_op.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, *string);
			} else {
				assign_op.result = std::get<TempVar>(va_list_var);
				assign_op.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, std::get<TempVar>(va_list_var));
			}
			assign_op.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_struct_addr);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), functionCallNode.called_from()));
		} else {
			// Windows/MSVC ABI: Compute &last_param + 8 (variadic args are on stack)
			TempVar last_param_addr = var_counter.next();

			// Generate AddressOf IR for the last parameter
			AddressOfOp addr_op;
			addr_op.result = last_param_addr;
			// Get the type of the last parameter from the symbol table
			auto param_symbol = symbol_table.lookup(last_param_name);
			if (!param_symbol.has_value()) {
				FLASH_LOG(Codegen, Error, "Parameter '", last_param_name, "' not found in __builtin_va_start");
				return makeExprResult(nativeTypeIndex(TypeCategory::Void), SizeInBits{0}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
			}
			const DeclarationNode& param_decl = param_symbol->as<DeclarationNode>();
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			addr_op.operand.setType(param_type.category());
			addr_op.operand.ir_type = toIrType(param_type.type());
			addr_op.operand.size_in_bits = SizeInBits{param_type.size_in_bits()};
			addr_op.operand.pointer_depth = PointerDepth{static_cast<int>(param_type.pointer_depth())};
			addr_op.operand.value = StringTable::getOrInternStringHandle(last_param_name);
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), functionCallNode.called_from()));

			// Add 8 bytes (64 bits) to get to the next parameter slot
			TempVar va_start_addr = var_counter.next();
			BinaryOp add_op;
			add_op.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, last_param_addr);
			add_op.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, 8ULL);
			add_op.result = va_start_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), functionCallNode.called_from()));

			// Assign to va_list variable
			AssignmentOp assign_op;
			if (const auto* string = std::get_if<StringHandle>(&va_list_var)) {
				assign_op.result = *string;
				assign_op.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, *string);
			} else {
				assign_op.result = std::get<TempVar>(va_list_var);
				assign_op.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, std::get<TempVar>(va_list_var));
			}
			assign_op.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, va_start_addr);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), functionCallNode.called_from()));
		}
	}

	// __builtin_va_start returns void
	return makeExprResult(nativeTypeIndex(TypeCategory::Void), SizeInBits{0}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
}

ExprResult AstToIr::generateBuiltinUnreachableIntrinsic(const FunctionCallNode& functionCallNode) {
	// Verify no arguments (some compilers allow it, we'll be strict)
	if (functionCallNode.arguments().size() != 0) {
		FLASH_LOG(Codegen, Warning, "__builtin_unreachable should not have arguments (ignoring)");
	}

	// For now, we just return void and don't generate any IR
	// A more sophisticated implementation could:
	// 1. Mark the current basic block as unreachable for optimization
	// 2. Allow following code to be eliminated as dead code
	// 3. Use this information for branch prediction

	FLASH_LOG(Codegen, Debug, "__builtin_unreachable encountered - marking code path as unreachable");

	// Return void (this intrinsic doesn't produce a value)
	return makeExprResult(nativeTypeIndex(TypeCategory::Void), SizeInBits{0}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
}

ExprResult AstToIr::generateBuiltinAssumeIntrinsic(const FunctionCallNode& functionCallNode) {
	if (functionCallNode.arguments().size() != 1) {
		FLASH_LOG(Codegen, Error, "__builtin_assume requires exactly 1 argument (condition)");
		return makeExprResult(nativeTypeIndex(TypeCategory::Void), SizeInBits{0}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
	}

	// Evaluate the condition expression (but we don't use the result)
	// In a real implementation, we'd use this to inform the optimizer
	ASTNode condition = functionCallNode.arguments()[0];
	[[maybe_unused]] auto condition_ir = visitExpressionNode(condition.as<ExpressionNode>());

	// For now, we just evaluate the expression and ignore it
	// A more sophisticated implementation could:
	// 1. Track assumptions for later optimization passes
	// 2. Use assumptions for constant folding
	// 3. Enable more aggressive optimizations in conditional branches

	FLASH_LOG(Codegen, Debug, "__builtin_assume encountered - assumption recorded (not yet used for optimization)");

	// Return void (this intrinsic doesn't produce a value)
	return makeExprResult(nativeTypeIndex(TypeCategory::Void), SizeInBits{0}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
}

ExprResult AstToIr::generateBuiltinExpectIntrinsic(const FunctionCallNode& functionCallNode) {
	if (functionCallNode.arguments().size() != 2) {
		FLASH_LOG(Codegen, Error, "__builtin_expect requires exactly 2 arguments (expr, expected_value)");
		// Return a default value matching typical usage (long type)
		return makeExprResult(nativeTypeIndex(TypeCategory::LongLong), SizeInBits{64}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
	}

	// Evaluate the first argument (the expression)
	ASTNode expr = functionCallNode.arguments()[0];
	auto expr_ir = visitExpressionNode(expr.as<ExpressionNode>());

	// Evaluate the second argument (the expected value) but don't use it for now
	ASTNode expected = functionCallNode.arguments()[1];
	[[maybe_unused]] auto expected_ir = visitExpressionNode(expected.as<ExpressionNode>());

	// For now, we just return the expression value unchanged
	// A more sophisticated implementation could:
	// 1. Pass branch prediction hints to the code generator
	// 2. Reorder basic blocks to favor the expected path
	// 3. Use profile-guided optimization data

	FLASH_LOG(Codegen, Debug, "__builtin_expect encountered - branch prediction hint recorded (not yet used)");

	// Return the first argument (the expression value)
	return expr_ir;
}

ExprResult AstToIr::generateBuiltinLaunderIntrinsic(const FunctionCallNode& functionCallNode) {
	if (functionCallNode.arguments().size() != 1) {
		FLASH_LOG(Codegen, Error, "__builtin_launder requires exactly 1 argument (pointer)");
		return makeExprResult(nativeTypeIndex(TypeCategory::UnsignedLongLong), SizeInBits{64}, IrOperand{0ULL}, PointerDepth{}, ValueStorage::ContainsData);
	}

	// Evaluate the pointer argument
	ASTNode ptr_arg = functionCallNode.arguments()[0];
	ExprResult ptrExprResult = visitExpressionNode(ptr_arg.as<ExpressionNode>());

	// Extract pointer details
	[[maybe_unused]] TypeCategory ptr_type = ptrExprResult.typeEnum();
	[[maybe_unused]] int ptr_size = ptrExprResult.size_in_bits.value;

	// For now, we just return the pointer unchanged
	// In a real implementation, __builtin_launder would:
	// 1. Create an optimization barrier so compiler can't assume anything about pointee
	// 2. Prevent const/restrict/alias analysis from making invalid assumptions
	// 3. Essential after placement new to get a pointer to the new object
	//
	// Example use case:
	//   struct S { const int x; };
	//   alignas(S) char buffer[sizeof(S)];
	//   new (buffer) S{42};  // placement new
	//   S* ptr = std::launder(reinterpret_cast<S*>(buffer));  // safe access

	FLASH_LOG(Codegen, Debug, "__builtin_launder encountered - optimization barrier created");

	// Return the pointer unchanged (but optimization barrier is implied)
	return ptrExprResult;
}

ExprResult AstToIr::generateGetExceptionCodeIntrinsic(const FunctionCallNode& functionCallNode) {
	// The IR opcode produces a 32-bit exception code value.  The declared return type
	// may differ (e.g., unsigned long vs unsigned int).  Derive the expression type
	// from the function declaration so the intrinsic result matches what the caller
	// expects — same as regular function calls derive their type from the declaration.
	const auto& ret_type_spec = functionCallNode.function_declaration().type_node().as<TypeSpecifierNode>();
	const TypeCategory result_type = ret_type_spec.type();
	const int result_size = static_cast<int>(ret_type_spec.size_in_bits());

	TempVar result = var_counter.next();
	if (seh_in_filter_funclet_) {
		// Filter context: EXCEPTION_POINTERS* is in [rsp+8], read ExceptionCode from there
		SehExceptionIntrinsicOp op;
		op.result = result;
		ir_.addInstruction(IrInstruction(IrOpcode::SehGetExceptionCode, std::move(op), functionCallNode.called_from()));
	} else if (seh_has_saved_exception_code_) {
		// __except body context: read from parent-frame slot saved during filter evaluation
		SehGetExceptionCodeBodyOp op;
		op.saved_var = seh_saved_exception_code_var_;
		op.result = result;
		ir_.addInstruction(IrInstruction(IrOpcode::SehGetExceptionCodeBody, std::move(op), functionCallNode.called_from()));
	} else {
		// Fallback (e.g. filter without a saved slot): use the direct filter path
		SehExceptionIntrinsicOp op;
		op.result = result;
		ir_.addInstruction(IrInstruction(IrOpcode::SehGetExceptionCode, std::move(op), functionCallNode.called_from()));
	}
	return makeExprResult(nativeTypeIndex(result_type), SizeInBits{result_size}, IrOperand{result}, PointerDepth{}, ValueStorage::ContainsData);
}

ExprResult AstToIr::generateAbnormalTerminationIntrinsic(const FunctionCallNode& functionCallNode) {
	const auto& ret_type_spec = functionCallNode.function_declaration().type_node().as<TypeSpecifierNode>();
	const TypeCategory result_type = ret_type_spec.type();
	const int result_size = static_cast<int>(ret_type_spec.size_in_bits());

	TempVar result = var_counter.next();
	SehAbnormalTerminationOp op;
	op.result = result;
	ir_.addInstruction(IrInstruction(IrOpcode::SehAbnormalTermination, std::move(op), functionCallNode.called_from()));
	return makeExprResult(nativeTypeIndex(result_type), SizeInBits{result_size}, IrOperand{result}, PointerDepth{}, ValueStorage::ContainsData);
}

ExprResult AstToIr::generateGetExceptionInformationIntrinsic(const FunctionCallNode& functionCallNode) {
	const auto& ret_type_spec = functionCallNode.function_declaration().type_node().as<TypeSpecifierNode>();
	const TypeCategory result_type = ret_type_spec.type();
	const int result_size = static_cast<int>(ret_type_spec.size_in_bits());

	TempVar result = var_counter.next();
	SehExceptionIntrinsicOp op;
	op.result = result;
	ir_.addInstruction(IrInstruction(IrOpcode::SehGetExceptionInfo, std::move(op), functionCallNode.called_from()));
	return makeExprResult(nativeTypeIndex(result_type), SizeInBits{result_size}, IrOperand{result}, PointerDepth{}, ValueStorage::ContainsData);
}

// Helper function to handle assignment using lvalue metadata
// Queries LValueInfo::Kind and routes to appropriate store instruction
// Returns true if assignment was handled via lvalue metadata, false otherwise
//
// USAGE: Call this after evaluating both LHS and RHS expressions.
//        If it returns true, the assignment was handled and caller should skip normal assignment logic.
//        If it returns false, fall back to normal assignment or special-case handling.
//
// CURRENT LIMITATIONS:
// - ArrayElement and Member cases need additional metadata (index, member_name) not currently in LValueInfo
// - Only Indirect (dereference) case is fully implemented
// - Future work: Extend LValueInfo or pass additional context to handle all cases
bool AstToIr::handleLValueAssignment(const ExprResult& lhs_operands,
                                     const ExprResult& rhs_operands,
                                     const Token& token) {
	// Check if LHS has a TempVar with lvalue metadata
	if (!std::holds_alternative<TempVar>(lhs_operands.value)) {
		FLASH_LOG(Codegen, Info, "handleLValueAssignment: FAIL - has_tempvar=false");
		return false;
	}

	TempVar lhs_temp = std::get<TempVar>(lhs_operands.value);
	auto lvalue_info_opt = getTempVarLValueInfo(lhs_temp);
	TempVarMetadata lhs_meta = getTempVarMetadata(lhs_temp);

	if (!lvalue_info_opt.has_value()) {
		FLASH_LOG(Codegen, Info, "handleLValueAssignment: FAIL - no lvalue metadata for temp=", lhs_temp.var_number);
		return false;
	}

	const LValueInfo& lv_info = lvalue_info_opt.value();
	TypeCategory lvalue_cat = (lhs_meta.valueCategory() != TypeCategory::Invalid) ? lhs_meta.valueCategory() : lhs_operands.category();
	TypeCategory lvalue_type = lvalue_cat;
	auto inferLValueSizeBits = [&]() {
		int inferred_size_bits = 0;
		// Use IrType to catch both Type::Struct and Type::UserDefined, so
		// typedef-to-struct aliases also use the struct-layout path.
		if (isIrStructType(toIrType(lvalue_cat))) {
			if (const TypeInfo* type_info = tryGetTypeInfo(lhs_operands.type_index)) {
				if (const StructTypeInfo* struct_info = type_info->getStructInfo()) {
					inferred_size_bits = static_cast<int>(struct_info->total_size * 8);
				} else {
					inferred_size_bits = static_cast<int>(type_info->type_size_);
				}
			}
		} else {
			inferred_size_bits = get_type_size_bits(lvalue_type);
		}
		if (inferred_size_bits == 0 && lhs_meta.value_size_bits.value > 0) {
			inferred_size_bits = lhs_meta.value_size_bits.value;
		}
		if (inferred_size_bits == 0) {
			inferred_size_bits = lhs_operands.size_in_bits.value;
		}
		return inferred_size_bits;
	};

	FLASH_LOG(Codegen, Debug, "handleLValueAssignment: kind=", static_cast<int>(lv_info.kind));

	auto tryResolveExprTypeIndex = [&](const ExprResult& expr_result) -> TypeIndex {
		if (expr_result.type_index.is_valid()) {
			return expr_result.type_index;
		}

		auto resolveBaseTypeIndex = [&](const auto& self, const std::variant<StringHandle, TempVar>& base) -> TypeIndex {
			if (const auto* base_name = std::get_if<StringHandle>(&base)) {
				if (auto symbol = lookupSymbol(*base_name)) {
					if (const DeclarationNode* decl = get_decl_from_symbol(*symbol);
					    decl && decl->type_node().is<TypeSpecifierNode>()) {
						return decl->type_node().as<TypeSpecifierNode>().type_index();
					}
				}
				return {};
			}

			const TempVar& base_temp = std::get<TempVar>(base);
			TempVarMetadata base_meta = getTempVarMetadata(base_temp);
			if (!base_meta.lvalue_info.has_value()) {
				return {};
			}
			return self(self, base_meta.lvalue_info->base);
		};

		if (const auto* base_name = std::get_if<StringHandle>(&expr_result.value)) {
			return resolveBaseTypeIndex(resolveBaseTypeIndex, *base_name);
		}

		const auto* temp_var = std::get_if<TempVar>(&expr_result.value);
		if (!temp_var) {
			return {};
		}

		TempVarMetadata expr_meta = getTempVarMetadata(*temp_var);
		if (!expr_meta.lvalue_info.has_value()) {
			return {};
		}
		return resolveBaseTypeIndex(resolveBaseTypeIndex, expr_meta.lvalue_info->base);
	};

	auto diagnoseDeletedMetadataAssignment = [&]() {
		if (lv_info.kind != LValueInfo::Kind::ArrayElement && lv_info.kind != LValueInfo::Kind::Member && lv_info.kind != LValueInfo::Kind::Indirect) {
			return;
		}

		if (lhs_operands.category() != TypeCategory::Struct || rhs_operands.category() != TypeCategory::Struct) {
			return;
		}

		TypeIndex lhs_type_index = tryResolveExprTypeIndex(lhs_operands);
		TypeIndex rhs_type_index = tryResolveExprTypeIndex(rhs_operands);
		if (!lhs_type_index.is_valid() || !rhs_type_index.is_valid() || lhs_type_index != rhs_type_index || lhs_type_index.index() >= getTypeInfoCount()) {
			return;
		}

		if (const StructTypeInfo* struct_info = getTypeInfo(lhs_type_index).getStructInfo()) {
			diagnoseDeletedSameTypeAssignmentUsage(*struct_info, shouldPreferMoveAssignment(rhs_operands));
		}
	};

	// The metadata path can emit stores directly and skip the regular assignment logic,
	// so preserve deleted special-member diagnostics here for handled lvalue forms.
	diagnoseDeletedMetadataAssignment();

	// Route to appropriate store instruction based on LValueInfo::Kind
	switch (lv_info.kind) {
	case LValueInfo::Kind::ArrayElement: {
		// Array element assignment: arr[i] = value
		FLASH_LOG(Codegen, Debug, "  -> ArrayStore (handled via metadata)");

		// Check if we have the index stored in metadata
		if (!lv_info.array_index.has_value()) {
			FLASH_LOG(Codegen, Info, "     ArrayElement: No index in metadata, falling back");
			return false;
		}

		FLASH_LOG(Codegen, Info, "     ArrayElement: Has index in metadata, proceeding with unified handler");

		// Build TypedValue for index from metadata
		IrValue index_value = lv_info.array_index.value();
		TypedValue index_tv;
		index_tv.value = index_value;
		index_tv.setType(TypeCategory::Int); // Index type (typically int)
		index_tv.ir_type = IrType::Integer;
		index_tv.size_in_bits = SizeInBits{32}; // Standard index size

		// Build TypedValue for value with LHS type/size but RHS value
		// This is important: the size must match the array element type
		TypedValue value_tv;
		value_tv.setType(lhs_operands.category());
		value_tv.ir_type = lhs_operands.effectiveIrType();
		value_tv.size_in_bits = lhs_operands.size_in_bits;
		value_tv.value = toIrValue(rhs_operands.value);

		// Emit the store using helper
		emitArrayStore(
		    lhs_operands.category(), // element_type
		    lhs_operands.size_in_bits.value, // element_size_bits
		    lv_info.base, // array
		    index_tv, // index
		    value_tv, // value (with LHS type/size, RHS value)
		    lv_info.offset, // member_offset
		    lv_info.is_pointer_to_array, // is_pointer_to_array
		    token);
		return true;
	}

	case LValueInfo::Kind::Member: {
		// Member assignment: obj.member = value
		FLASH_LOG(Codegen, Debug, "  -> MemberStore (handled via metadata)");

		// Check if we have member_name stored in metadata
		if (!lv_info.member_name.has_value()) {
			FLASH_LOG(Codegen, Debug, "     No member_name in metadata, falling back");
			return false;
		}

		// Safety check: validate size is reasonable (not 0 or negative)
		int lhs_size = lhs_operands.size_in_bits.value;
		if (lhs_size <= 0 || lhs_size > 1024) {
			FLASH_LOG(Codegen, Debug, "     Invalid size in metadata (", lhs_size, "), falling back");
			return false;
		}

		// Build TypedValue with LHS type/size but RHS value
		// This is important: the size must match the member being stored to, not the RHS
		TypedValue value_tv;
		value_tv.setType(lhs_operands.category());
		value_tv.ir_type = lhs_operands.effectiveIrType();
		value_tv.size_in_bits = SizeInBits{static_cast<int>(lhs_size)};
		value_tv.value = toIrValue(rhs_operands.value);

		// Emit the store using helper
		emitMemberStore(
		    value_tv, // value (with LHS type/size, RHS value)
		    lv_info.base, // object
		    lv_info.member_name.value(), // member_name
		    lv_info.offset, // offset
		    CVReferenceQualifier::None,
		    lv_info.is_pointer_to_member, // is_pointer_to_member
		    token,
		    lv_info.bitfield_width, // bitfield_width
		    lv_info.bitfield_bit_offset // bitfield_bit_offset
		);
		return true;
	}

	case LValueInfo::Kind::Indirect: {
		// Dereference assignment: *ptr = value
		// This case works because we have all needed info in LValueInfo
		FLASH_LOG(Codegen, Debug, "  -> DereferenceStore (handled via metadata)");
		TypeCategory pointee_type = lvalue_type;
		int pointee_size_bits = inferLValueSizeBits();
		TypedValue value_tv;
		value_tv.setType(pointee_type);
		value_tv.ir_type = toIrType(pointee_type);
		value_tv.size_in_bits = SizeInBits{static_cast<int>(pointee_size_bits)};
		value_tv.value = toIrValue(rhs_operands.value);

		// Emit the store using helper
		emitDereferenceStore(
		    value_tv,
		    pointee_type,
		    pointee_size_bits,
		    lv_info.base, // pointer
		    token);
		return true;
	}

	case LValueInfo::Kind::Direct:
	case LValueInfo::Kind::Temporary:
		// Direct variable assignment - handled by regular assignment logic
		FLASH_LOG(Codegen, Debug, "  -> Regular assignment (Direct/Temporary)");
		return false;

	default:
		return false;
	}

	return false;
}

// Handle compound assignment to lvalues (e.g., v.x += 5, arr[i] += 5)
// Supports Member kind (struct member access), Indirect kind (dereferenced pointers - already supported), and ArrayElement kind (array subscripts - added in this function)
// This is similar to handleLValueAssignment but also performs the arithmetic operation
bool AstToIr::handleLValueCompoundAssignment(const ExprResult& lhs_operands,
                                             const ExprResult& rhs_operands,
                                             const Token& token,
                                             std::string_view op) {
	// Check if LHS has a TempVar with lvalue metadata
	if (!std::holds_alternative<TempVar>(lhs_operands.value)) {
		FLASH_LOG(Codegen, Info, "handleLValueCompoundAssignment: FAIL - has_tempvar=false");
		return false;
	}

	TempVar lhs_temp = std::get<TempVar>(lhs_operands.value);
	FLASH_LOG_FORMAT(Codegen, Debug, "handleLValueCompoundAssignment: Checking TempVar {} for metadata", lhs_temp.var_number);
	auto lvalue_info_opt = getTempVarLValueInfo(lhs_temp);
	TempVarMetadata lhs_meta = getTempVarMetadata(lhs_temp);

	if (!lvalue_info_opt.has_value()) {
		FLASH_LOG_FORMAT(Codegen, Debug, "handleLValueCompoundAssignment: FAIL - no lvalue metadata for TempVar {}", lhs_temp.var_number);
		return false;
	}

	const LValueInfo& lv_info = lvalue_info_opt.value();
	TypeCategory lvalue_cat = (lhs_meta.valueCategory() != TypeCategory::Invalid) ? lhs_meta.valueCategory() : lhs_operands.category();
	TypeCategory lvalue_type = lvalue_cat;
	auto inferLValueSizeBits = [&]() {
		int inferred_size_bits = 0;
		// Use IrType to catch both Type::Struct and Type::UserDefined, so
		// typedef-to-struct aliases also use the struct-layout path.
		if (isIrStructType(toIrType(lvalue_cat))) {
			if (const TypeInfo* type_info = tryGetTypeInfo(lhs_operands.type_index)) {
				if (const StructTypeInfo* struct_info = type_info->getStructInfo()) {
					inferred_size_bits = static_cast<int>(struct_info->total_size * 8);
				} else {
					inferred_size_bits = static_cast<int>(type_info->type_size_);
				}
			}
		} else {
			inferred_size_bits = get_type_size_bits(lvalue_type);
		}
		if (inferred_size_bits == 0 && lhs_meta.value_size_bits.value > 0) {
			inferred_size_bits = lhs_meta.value_size_bits.value;
		}
		if (inferred_size_bits == 0) {
			inferred_size_bits = lhs_operands.size_in_bits.value;
		}
		return inferred_size_bits;
	};
	int lvalue_size_bits = inferLValueSizeBits();

	FLASH_LOG(Codegen, Debug, "handleLValueCompoundAssignment: kind=", static_cast<int>(lv_info.kind), " op=", op);

	// For compound assignments, we need to:
	// 1. The lhs_temp already contains the ADDRESS (from LValueAddress context)
	// 2. We need to LOAD the current value from that address
	// 3. Perform the operation with RHS
	// 4. Store the result back to the address

	// First, load the current value from the lvalue
	// The lhs_temp should contain the address, but we need to generate a Load instruction
	// to get the current value into a temp var
	TempVar current_value_temp = var_counter.next();

	// Map compound assignment operator to the corresponding IR opcode (defined once, used by all branches)
	const auto base_opcode = compoundOpToBaseOpcode(op);
	if (!base_opcode.has_value()) {
		FLASH_LOG(Codegen, Debug, "     Unsupported compound assignment operator: ", op);
		return false;
	}
	IrOpcode operation_opcode = *base_opcode;

	// Generate a Load instruction based on the lvalue kind
	// Support both Member kind and Indirect kind (for dereferenced pointers like &y in lambda captures)
	if (lv_info.kind == LValueInfo::Kind::Indirect) {
		// For Indirect kind (dereferenced pointer), the base can be a TempVar or StringHandle
		// Generate a Dereference instruction to load the current value
		DereferenceOp deref_op;
		deref_op.result = current_value_temp;
		deref_op.pointer.setType(lvalue_type);
		deref_op.pointer.type_index = nativeTypeIndex(lvalue_type);
		deref_op.pointer.size_in_bits = SizeInBits{64}; // pointer size
		deref_op.pointer.pointer_depth = PointerDepth{1};

		// Extract the base (TempVar or StringHandle)
		std::variant<TempVar, StringHandle> base_value;
		if (const auto* temp_var = std::get_if<TempVar>(&lv_info.base)) {
			deref_op.pointer.value = *temp_var;
			base_value = *temp_var;
		} else if (const auto* string = std::get_if<StringHandle>(&lv_info.base)) {
			deref_op.pointer.value = *string;
			base_value = *string;
		} else {
			FLASH_LOG(Codegen, Debug, "     Indirect kind requires TempVar or StringHandle base");
			return false;
		}

		ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), token));

		// Now perform the operation (e.g., Add for +=, Subtract for -=, etc.)
		TempVar result_temp = var_counter.next();

		// Create the binary operation
		BinaryOp bin_op;
		bin_op.lhs.setType(lvalue_type);
		bin_op.lhs.ir_type = toIrType(lvalue_type);
		bin_op.lhs.size_in_bits = SizeInBits{static_cast<int>(lvalue_size_bits)};
		bin_op.lhs.value = current_value_temp;
		bin_op.rhs = toTypedValue(rhs_operands);
		bin_op.result = result_temp;

		ir_.addInstruction(IrInstruction(operation_opcode, std::move(bin_op), token));

		// Store result back through the pointer using DereferenceStore
		TypedValue result_tv;
		result_tv.setType(lvalue_type);
		result_tv.ir_type = toIrType(lvalue_type);
		result_tv.size_in_bits = SizeInBits{static_cast<int>(lvalue_size_bits)};
		result_tv.value = result_temp;

		// Handle both TempVar and StringHandle bases for DereferenceStore
		if (std::holds_alternative<TempVar>(base_value)) {
			emitDereferenceStore(
			    result_tv,
			    lvalue_type,
			    lvalue_size_bits,
			    std::get<TempVar>(base_value),
			    token);
		} else {
			// StringHandle base: emitDereferenceStore expects a TempVar, so we pass the StringHandle as the pointer
			// Generate DereferenceStore with StringHandle directly
			DereferenceStoreOp store_op;
			store_op.pointer.setType(lvalue_type);
			store_op.pointer.type_index = nativeTypeIndex(lvalue_type);
			store_op.pointer.size_in_bits = SizeInBits{64};
			store_op.pointer.pointer_depth = PointerDepth{1};
			store_op.pointer.value = std::get<StringHandle>(base_value);
			store_op.value = result_tv;
			ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_op), token));
		}

		return true;
	}

	// Handle ArrayElement kind for compound assignments (e.g., arr[i] += 5)
	if (lv_info.kind == LValueInfo::Kind::ArrayElement) {
		// Check if we have the index stored in metadata
		if (!lv_info.array_index.has_value()) {
			FLASH_LOG(Codegen, Debug, "     ArrayElement: No index in metadata for compound assignment");
			return false;
		}

		FLASH_LOG(Codegen, Debug, "     ArrayElement compound assignment: proceeding with unified handler");

		// Build TypedValue for index from metadata
		IrValue index_value = lv_info.array_index.value();
		TypedValue index_tv;
		index_tv.value = index_value;
		index_tv.setType(TypeCategory::Int); // Index type (typically int)
		index_tv.ir_type = IrType::Integer;
		index_tv.size_in_bits = SizeInBits{32}; // Standard index size

		// Create ArrayAccessOp to load current value
		ArrayAccessOp load_op;
		load_op.result = current_value_temp;
		load_op.element_type_index = lhs_operands.type_index;
		load_op.element_size_in_bits = lhs_operands.size_in_bits.value;
		load_op.array = lv_info.base;
		load_op.index = index_tv;
		load_op.member_offset = lv_info.offset;
		load_op.is_pointer_to_array = lv_info.is_pointer_to_array;

		ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(load_op), token));

		// Now perform the operation (e.g., Add for +=, Subtract for -=, etc.)
		TempVar result_temp = var_counter.next();

		// Create the binary operation
		BinaryOp bin_op;
		bin_op.lhs.setType(lhs_operands.category());
		bin_op.lhs.ir_type = lhs_operands.effectiveIrType();
		bin_op.lhs.size_in_bits = lhs_operands.size_in_bits;
		bin_op.lhs.value = current_value_temp;
		bin_op.rhs = toTypedValue(rhs_operands);
		bin_op.result = result_temp;

		ir_.addInstruction(IrInstruction(operation_opcode, std::move(bin_op), token));

		// Finally, store the result back to the array element
		TypedValue result_tv;
		result_tv.setType(lhs_operands.category());
		result_tv.ir_type = lhs_operands.effectiveIrType();
		result_tv.size_in_bits = lhs_operands.size_in_bits;
		result_tv.value = result_temp;

		// Emit the store using helper
		emitArrayStore(
		    lhs_operands.category(), // element_type
		    lhs_operands.size_in_bits.value, // element_size_bits
		    lv_info.base, // array
		    index_tv, // index
		    result_tv, // value (result of operation)
		    lv_info.offset, // member_offset
		    lv_info.is_pointer_to_array, // is_pointer_to_array
		    token);

		return true;
	}

	// Handle Global kind for compound assignments (e.g., g_score += 20)
	if (lv_info.kind == LValueInfo::Kind::Global) {
		if (!std::holds_alternative<StringHandle>(lv_info.base)) {
			FLASH_LOG(Codegen, Debug, "     Global compound assignment: base is not a StringHandle");
			return false;
		}
		StringHandle global_name = std::get<StringHandle>(lv_info.base);
		FLASH_LOG(Codegen, Debug, "     Global compound assignment op=", op);

		// lhs_temp already holds the loaded value (from GlobalLoad in LHS evaluation)
		TempVar result_temp = var_counter.next();
		BinaryOp bin_op;
		bin_op.lhs.setType(lhs_operands.category());
		bin_op.lhs.ir_type = lhs_operands.effectiveIrType();
		bin_op.lhs.size_in_bits = lhs_operands.size_in_bits;
		bin_op.lhs.value = lhs_temp;
		bin_op.rhs = toTypedValue(rhs_operands);
		bin_op.result = result_temp;
		ir_.addInstruction(IrInstruction(operation_opcode, std::move(bin_op), token));

		// Store result back to global
		std::vector<IrOperand> store_operands;
		store_operands.emplace_back(global_name);
		store_operands.emplace_back(result_temp);
		ir_.addInstruction(IrOpcode::GlobalStore, std::move(store_operands), token);

		return true;
	}

	if (lv_info.kind != LValueInfo::Kind::Member) {
		FLASH_LOG(Codegen, Debug, "     Compound assignment only supports Member, Indirect, ArrayElement, or Global kind, got: ", static_cast<int>(lv_info.kind));
		return false;
	}

	// For member access, generate MemberAccess (Load) instruction
	if (!lv_info.member_name.has_value()) {
		FLASH_LOG(Codegen, Debug, "     No member_name in metadata for compound assignment");
		return false;
	}

	// Lookup member info to get is_reference flags
	bool member_is_reference = false;
	bool member_is_rvalue_reference = false;

	// Try to get struct type info from the base object
	if (std::holds_alternative<StringHandle>(lv_info.base)) {
		StringHandle base_name_handle = std::get<StringHandle>(lv_info.base);
		std::string_view base_name = StringTable::getStringView(base_name_handle);

		// Look up the base object in symbol table
		std::optional<ASTNode> symbol = lookupSymbol(base_name);

		if (symbol.has_value()) {
			const DeclarationNode* decl = get_decl_from_symbol(*symbol);
			if (decl) {
				const TypeSpecifierNode& type_node = decl->type_node().as<TypeSpecifierNode>();
				if (is_struct_type(type_node.category())) {
					TypeIndex type_index = type_node.type_index();
					if (type_index.is_valid()) {
						auto result = FlashCpp::gLazyMemberResolver.resolve(type_index, lv_info.member_name.value());
						if (result) {
							member_is_reference = result.member->is_reference();
							member_is_rvalue_reference = result.member->is_rvalue_reference();
						}
					}
				}
			}
		}
	}
	// Note: For TempVar base, we don't have easy access to type info, so we default to false
	// This is acceptable since most compound assignments don't involve reference members

	MemberLoadOp load_op;
	load_op.result.value = current_value_temp;
	load_op.result.setType(lhs_operands.category());
	load_op.result.ir_type = lhs_operands.effectiveIrType();
	load_op.result.size_in_bits = lhs_operands.size_in_bits;
	load_op.object = lv_info.base;
	load_op.member_name = lv_info.member_name.value();
	load_op.offset = lv_info.offset;
	load_op.ref_qualifier = ((member_is_rvalue_reference) ? CVReferenceQualifier::RValueReference : ((member_is_reference) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None));
	load_op.struct_type_info = nullptr;
	load_op.is_pointer_to_member = lv_info.is_pointer_to_member;
	load_op.bitfield_width = lv_info.bitfield_width;
	load_op.bitfield_bit_offset = lv_info.bitfield_bit_offset;

	ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), token));

	// Now perform the operation (e.g., Add for +=, Subtract for -=, etc.)
	TempVar result_temp = var_counter.next();

	// Create the binary operation (size_in_bits is already SizeInBits — direct assignment)
	BinaryOp bin_op;
	bin_op.lhs.setType(lhs_operands.category());
	bin_op.lhs.ir_type = lhs_operands.effectiveIrType();
	bin_op.lhs.size_in_bits = lhs_operands.size_in_bits;
	bin_op.lhs.value = current_value_temp;
	bin_op.rhs = toTypedValue(rhs_operands);
	bin_op.result = result_temp;

	ir_.addInstruction(IrInstruction(operation_opcode, std::move(bin_op), token));

	// Finally, store the result back to the lvalue
	TypedValue result_tv;
	result_tv.setType(lhs_operands.category());
	result_tv.ir_type = lhs_operands.effectiveIrType();
	result_tv.size_in_bits = lhs_operands.size_in_bits;
	result_tv.value = result_temp;
	CVReferenceQualifier member_ref_qualifier = member_is_rvalue_reference
	                                                ? CVReferenceQualifier::RValueReference
	                                                : (member_is_reference ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None);

	emitMemberStore(
	    result_tv,
	    lv_info.base,
	    lv_info.member_name.value(),
	    lv_info.offset,
	    member_ref_qualifier,
	    lv_info.is_pointer_to_member, // is_pointer_to_member
	    token,
	    lv_info.bitfield_width,
	    lv_info.bitfield_bit_offset);

	return true;
}
