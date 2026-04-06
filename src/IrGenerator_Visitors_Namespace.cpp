#include "Parser.h"
#include "IrGenerator.h"
#include "SemanticAnalysis.h"

void AstToIr::visitNamespaceDeclarationNode(const NamespaceDeclarationNode& node) {
		// Namespace declarations themselves don't generate IR - they just provide scope
		// Track the current namespace for proper name mangling
		// For anonymous namespaces, push empty string which will be handled specially by mangling
	current_namespace_stack_.push_back(std::string(node.name()));

		// Visit all declarations within the namespace
	for (const auto& decl : node.declarations()) {
		visit(decl);
	}

		// Pop the namespace from the stack
	current_namespace_stack_.pop_back();
}

void AstToIr::visitUsingDirectiveNode(const UsingDirectiveNode& node) {
		// Using directives don't generate IR - they affect name lookup in the symbol table
		// Add the namespace to the current scope's using directives in the local symbol table
		// (not gSymbolTable, which is the parser's symbol table and has different scope management)
	symbol_table.add_using_directive(node.namespace_handle());
}

void AstToIr::visitUsingDeclarationNode(const UsingDeclarationNode& node) {
		// Using declarations don't generate IR - they import a specific name into the current scope
		// Add the using declaration to the local symbol table (not gSymbolTable)
	FLASH_LOG(Codegen, Debug, "Adding using declaration: ", node.identifier_name(), " from namespace handle=", node.namespace_handle().index);
	symbol_table.add_using_declaration(
		node.identifier_name(),
		node.namespace_handle(),
		node.identifier_name());
}

void AstToIr::visitUsingEnumNode(const UsingEnumNode& node) {
		// C++20 using enum - brings all enumerators of a scoped enum into the current scope
		// Look up the enum type and add all enumerators to the local symbol table
	StringHandle enum_name = node.enum_type_name();

	auto type_it = getTypesByNameMap().find(enum_name);
	if (type_it != getTypesByNameMap().end() && type_it->second->getEnumInfo()) {
		const EnumTypeInfo* enum_info = type_it->second->getEnumInfo();
		TypeIndex enum_type_index = type_it->second->type_index_;

			// Add each enumerator to the local symbol table
		for (const auto& enumerator : enum_info->enumerators) {
				// Create a type node for the enum type
			Token enum_type_token(Token::Type::Identifier,
								  StringTable::getStringView(enum_name), 0, 0, 0);
			auto enum_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
				enum_type_index.withCategory(TypeCategory::Enum), enum_info->sizeInBits().value, enum_type_token,
				CVQualifier::None, ReferenceQualifier::None);

				// Create a declaration node for the enumerator
			Token enumerator_token(Token::Type::Identifier,
								   StringTable::getStringView(enumerator.getName()), 0, 0, 0);
			auto enumerator_decl = ASTNode::emplace_node<DeclarationNode>(enum_type_node, enumerator_token);

				// Insert into local symbol table
			symbol_table.insert(StringTable::getStringView(enumerator.getName()), enumerator_decl);
		}

		FLASH_LOG(Codegen, Debug, "Using enum '", StringTable::getStringView(enum_name),
				  "' - added ", enum_info->enumerators.size(), " enumerators to local scope");
	} else {
		FLASH_LOG(General, Error, "Enum type '", StringTable::getStringView(enum_name),
				  "' not found for 'using enum' declaration");
	}
}

void AstToIr::visitNamespaceAliasNode(const NamespaceAliasNode& node) {
		// Namespace aliases don't generate IR - they create an alias for a namespace
		// Add the alias to the local symbol table (not gSymbolTable)
	symbol_table.add_namespace_alias(node.alias_name(), node.target_namespace());
}

void AstToIr::visitReturnStatementNode(const ReturnStatementNode& node) {
	if (node.expression()) {
		const auto& expr_opt = node.expression();

			// Handle InitializerListNode for braced initializers in return statements
		if (expr_opt->is<InitializerListNode>()) {
				// Create a temporary variable to hold the initialized struct
			TempVar temp_var = var_counter.next();

				// Generate initialization code similar to variable declarations
			const InitializerListNode& init_list = expr_opt->as<InitializerListNode>();

				// Get struct type information
			TypeCategory return_category = current_function_return_type_index_.category();
			int return_size = current_function_return_size_;

			if (return_category != TypeCategory::Struct) {
				FLASH_LOG(Codegen, Error, "InitializerListNode in return statement for non-struct type");
				return;
			}

				// Find the struct info
			const StructTypeInfo* struct_info = nullptr;

				// Look up the struct by return type index or name
			for (size_t i = 0; i < getTypeInfoCount(); ++i) {
				if (getTypeInfo(TypeIndex{i}).struct_info_ &&
					static_cast<int>(getTypeInfo(TypeIndex{i}).struct_info_->sizeInBits().value) == return_size) {
					struct_info = getTypeInfo(TypeIndex{i}).struct_info_.get();
					break;
				}
			}

			if (!struct_info) {
				FLASH_LOG(Codegen, Error, "Could not find struct type info for return type");
				return;
			}

				// Process initializer list to generate member stores
			const auto& initializers = init_list.initializers();
			std::unordered_map<StringHandle, const ASTNode*> member_values;
			size_t positional_index = 0;

			for (size_t i = 0; i < initializers.size(); ++i) {
				if (init_list.is_designated(i)) {
						// Designated initializer - use member name
					StringHandle member_name = init_list.member_name(i);
					member_values[member_name] = &initializers[i];
				} else {
						// Positional initializer - map to member by index
					if (positional_index < struct_info->members.size()) {
						StringHandle member_name = struct_info->members[positional_index].getName();
						member_values[member_name] = &initializers[i];
						positional_index++;
					}
				}
			}

				// Generate member stores for each initialized member
			for (const StructMember& member : struct_info->members) {
				StringHandle member_name_handle = member.getName();
				auto it = member_values.find(member_name_handle);

				if (it != member_values.end()) {
						// Evaluate the initializer expression
					const ASTNode* init_expr = it->second;
					if (init_expr->is<ExpressionNode>()) {
						ExprResult init_result = visitExpressionNode(init_expr->as<ExpressionNode>());
							// Generate member store
						MemberStoreOp store_op;
						store_op.object = temp_var;
						store_op.member_name = member.getName();
						store_op.offset = static_cast<int>(member.offset);
							// Create TypedValue from result
						store_op.value = toTypedValue(init_result);
						store_op.ref_qualifier = CVReferenceQualifier::None;
						ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_op), node.return_token()));
					}
				}
			}

				// Call any enclosing __finally funclets before returning
			emitSehFinallyCallsBeforeReturn(node.return_token());
			emitAndClearFullExpressionTempDestructors();

				// Emit destructor calls for all local variables before returning
			emitDestructorsForNonLocalExit(0);

				// Now return the temporary variable
			emitReturn(temp_var, return_category, return_size, node.return_token());
			return;
		}

			// Original handling for ExpressionNode
		assert(expr_opt->is<ExpressionNode>());

			// Set flag if we should use RVO (returning struct by value with hidden return param)
		if (current_function_has_hidden_return_param_) {
			in_return_statement_with_rvo_ = true;
		}

			// Fast path: reference return of '*this' can directly return the this pointer
		if (current_function_returns_reference_ && expr_opt->is<ExpressionNode>()) {
			const auto& ret_expr = expr_opt->as<ExpressionNode>();
			if (std::holds_alternative<UnaryOperatorNode>(ret_expr)) {
				const auto& unary = std::get<UnaryOperatorNode>(ret_expr);
				if (unary.op() == "*" && unary.get_operand().is<ExpressionNode>()) {
					const auto& operand_expr = unary.get_operand().as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(operand_expr)) {
						const auto& ident = std::get<IdentifierNode>(operand_expr);
						if (ident.name() == "this") {
							emitSehFinallyCallsBeforeReturn(node.return_token());
							emitAndClearFullExpressionTempDestructors();
							emitDestructorsForNonLocalExit(0);
							emitReturn(StringTable::getOrInternStringHandle("this"),
									   currentFunctionReturnType(), current_function_return_size_,
									   node.return_token());
							return;
						}
					}
				}
			}
		}

			// For reference return types, use LValueAddress context to get the address instead of the value
			// This ensures "return *this" returns the address (this pointer), not the dereferenced value
		ExpressionContext return_context = current_function_returns_reference_
											   ? ExpressionContext::LValueAddress
											   : ExpressionContext::Load;
		ExprResult operands = visitExpressionNode(expr_opt->as<ExpressionNode>(), return_context);

			// Clear the RVO flag after evaluation
		in_return_statement_with_rvo_ = false;

			// Check if this is a void return with a void expression (e.g., return void_func();)
		{
			TypeCategory expr_category = operands.category();

				// If returning a void expression in a void function, just emit void return
				// (the expression was already evaluated for its side effects)
			if (expr_category == TypeCategory::Void && current_function_return_type_index_.category() == TypeCategory::Void) {
				emitSehFinallyCallsBeforeReturn(node.return_token());
				emitAndClearFullExpressionTempDestructors();
				emitDestructorsForNonLocalExit(0);
				emitVoidReturn(node.return_token());
				return;
			}
		}

		if (isPlaceholderAutoType(current_function_return_type_index_.category())) {
			throw InternalError("Unresolved placeholder return type reached IR return lowering");
		}

			// Convert to the function's return type if necessary
			// Skip type conversion for reference returns - the expression already has the correct representation
		if (!current_function_returns_reference_) {
			TypeCategory expr_type = operands.typeEnum();
			TypeCategory expr_category = operands.category();
			int expr_size = operands.size_in_bits.value;

				// Get the current function's return type
			TypeCategory return_category = current_function_return_type_index_.category();
			TypeCategory return_type = currentFunctionReturnType();
			int return_size = current_function_return_size_;
			const TypeIndex return_type_index = is_struct_type(return_category)
				? current_function_return_type_index_
				: nativeTypeIndex(return_type);
			TypeSpecifierNode return_type_spec(
				return_type_index,
				return_size,
				node.return_token(),
				CVQualifier::None,
				ReferenceQualifier::None);
			const bool use_return_slot_for_ctor = current_function_has_hidden_return_param_;

				// Check whether the semantic pass has already computed a cast annotation.
				// When present for a non-struct conversion, apply it and skip the local policy.
				// When present as UserDefined, the source is a struct with a conversion operator.
			bool sema_applied_conversion = false;
			if (auto materialized = tryMaterializeSemaSelectedConvertingConstructor(
					operands, *expr_opt, return_type_spec, node.return_token(), use_return_slot_for_ctor)) {
				operands = *materialized;
				expr_type = operands.typeEnum();
				expr_category = operands.category();
				expr_size = operands.size_in_bits.value;
				sema_applied_conversion = true;
			}
			if (!sema_applied_conversion &&
				is_struct_type(return_category) &&
				return_type_spec.type_index().is_valid() &&
				(!is_struct_type(expr_category) || operands.type_index != return_type_spec.type_index())) {
				if (const TypeInfo* target_type_info = tryGetTypeInfo(return_type_spec.type_index())) {
					if (const StructTypeInfo* target_struct_info = target_type_info->getStructInfo()) {
						auto tryMaterializeReturnCtor = [&](const ConstructorDeclarationNode* ctor) {
							if (!ctor) {
								return false;
							}
							if (auto materialized = materializeSelectedConvertingConstructor(
									operands, *expr_opt, return_type_spec, *ctor,
									node.return_token(), use_return_slot_for_ctor)) {
								operands = *materialized;
								expr_type = operands.typeEnum();
								expr_category = operands.category();
								expr_size = operands.size_in_bits.value;
								sema_applied_conversion = true;
								return true;
							}
							return false;
						};
						if (auto arg_type_opt = buildCodegenOverloadResolutionArgType(*expr_opt)) {
							std::vector<TypeSpecifierNode> arg_types;
							arg_types.push_back(*arg_type_opt);
							auto resolution = resolve_constructor_overload(*target_struct_info, arg_types, true);
							if (resolution.is_ambiguous) {
								throw CompileError("Ambiguous constructor call");
							}
							if (resolution.has_match && resolution.selected_overload) {
								tryMaterializeReturnCtor(resolution.selected_overload);
							}
						}
					}
				}
			}
			if (sema_ && expr_opt->is<ExpressionNode>()) {
				const void* key = static_cast<const void*>(&expr_opt->as<ExpressionNode>());
				auto slot = sema_->getSlot(key);
				if (!sema_applied_conversion && slot.has_value() && slot->has_cast()) {
					const ImplicitCastInfo& cast_info =
						sema_->castInfoTable()[slot->cast_info_index.value - 1];
					const TypeCategory annotated_source_type = sema_->typeContext().get(cast_info.source_type_id).category();
					const TypeCategory to_type = sema_->typeContext().get(cast_info.target_type_id).category();
					if (cast_info.cast_kind == StandardConversionKind::UserDefined &&
						annotated_source_type == TypeCategory::Struct) {
							// Sema annotated a user-defined conversion operator call
						TypeIndex source_type_idx = sema_->typeContext().get(cast_info.source_type_id).type_index;
						if (const TypeInfo* src_type_info = tryGetTypeInfo(source_type_idx)) {
							const StructTypeInfo* src_struct_info = src_type_info->getStructInfo();
							const TypeIndex ret_type_idx = is_struct_type(return_category) ? current_function_return_type_index_ : nativeTypeIndex(return_category);
							const bool source_is_const = ((static_cast<uint8_t>(sema_->typeContext().get(cast_info.source_type_id).base_cv)) & (static_cast<uint8_t>(CVQualifier::Const))) != 0;
							const StructMemberFunction* conv_op = findConversionOperator(
								src_struct_info, ret_type_idx, source_is_const);
							if (conv_op) {
								FLASH_LOG(Codegen, Debug, "Sema-annotated user-defined conversion in return from ",
										  StringTable::getStringView(src_type_info->name()), " to return type");
								if (auto result = emitConversionOperatorCall(operands, *src_type_info, *conv_op,
																			 ret_type_idx, return_size, node.return_token())) {
									operands = *result;
									sema_applied_conversion = true;
								}
							}
						}
					} else if (!is_struct_type(annotated_source_type) && !is_struct_type(to_type)) {
						TypeCategory from_type = annotated_source_type;
							// Sema may annotate as Type::Enum while codegen resolves enum
							// constants to their underlying type; use actual runtime type.
						if (from_type == TypeCategory::Enum && from_type != operands.typeEnum())
							from_type = operands.typeEnum();
						operands = generateTypeConversion(operands, from_type, to_type, node.return_token());
						sema_applied_conversion = true;
					}
				}
			}

				// For reference returns we already evaluated the expression in LValueAddress
				// context, so the operand now represents the address-producing glvalue.
				// Do not run ordinary value conversion against the ABI-sized return slot.
			if (!sema_applied_conversion && (expr_type != return_type || expr_size != return_size)) {
					// Check for user-defined conversion operator (fallback when sema did not run)
					// If expr is a struct type with a conversion operator to return_type, call it
				if (is_struct_type(expr_category)) {
					TypeIndex expr_type_index = operands.type_index;

					if (const TypeInfo* source_type_info = tryGetTypeInfo(expr_type_index)) {
						const StructTypeInfo* source_struct_info = source_type_info->getStructInfo();
						const TypeIndex ret_type_idx = is_struct_type(return_category) ? current_function_return_type_index_ : nativeTypeIndex(return_category);

							// Look for a conversion operator to the return type
						const StructMemberFunction* conv_op = findConversionOperator(
							source_struct_info, ret_type_idx, isExprConstQualified(*expr_opt));

						if (conv_op) {
							FLASH_LOG(Codegen, Debug, "Found conversion operator in return statement from ",
									  StringTable::getStringView(source_type_info->name()),
									  " to return type");
							if (auto result = emitConversionOperatorCall(operands, *source_type_info, *conv_op,
																		 ret_type_idx, return_size, node.return_token()))
								operands = *result;
						} else {
								// No conversion operator found - fall back to generateTypeConversion
							operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
						}
					} else {
							// No valid type_index - fall back to generateTypeConversion
						operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
					}
				} else {
						// Phase 15: sema should annotate standard arithmetic return conversions.
						// Log a warning when sema missed it (inferExpressionType may fail for
						// some expression contexts). Same-type size mismatches are not type
						// conversions — no annotation expected.
					if (sema_normalized_current_function_ && expr_type != return_type &&
						is_standard_arithmetic_type(expr_type) && is_standard_arithmetic_type(return_type)) {
						throw InternalError(std::string("Phase 15: sema missed return conversion (") + std::string(getTypeName(expr_type)) + " -> " + std::string(getTypeName(return_type)) + ")");
					}
						// Fallback for non-arithmetic types (enum, user_defined, etc.)
					operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
				}
			}
		}

			// For reference returns, prefer materializing the referred-to address in IR when we
			// have direct member lvalue metadata. This avoids relying on backend reconstruction
			// from a loaded member temp.
		if (current_function_returns_reference_ &&
			std::holds_alternative<TempVar>(operands.value)) {
			TempVar return_temp = std::get<TempVar>(operands.value);
			auto lv_info_opt = getTempVarLValueInfo(return_temp);
			if (lv_info_opt.has_value()) {
				const LValueInfo& lv_info = *lv_info_opt;
				if (lv_info.kind == LValueInfo::Kind::Member &&
					std::holds_alternative<StringHandle>(lv_info.base)) {
					TempVar address_temp = var_counter.next();
					AddressOfMemberOp addr_member_op;
					addr_member_op.result = address_temp;
					addr_member_op.base_object = std::get<StringHandle>(lv_info.base);
					addr_member_op.member_offset = lv_info.offset;
					addr_member_op.member_type_index = current_function_return_type_index_;
					addr_member_op.member_size_in_bits = current_function_return_size_;
					ir_.addInstruction(IrInstruction(IrOpcode::AddressOfMember, std::move(addr_member_op), node.return_token()));
					TempVarMetadata address_meta = TempVarMetadata::makeReference(currentFunctionReturnTypeIndex(), SizeInBits{current_function_return_size_}, ValueCategory::LValue);
					address_meta.lvalue_info = LValueInfo(LValueInfo::Kind::Indirect, address_temp, 0);
					setTempVarMetadata(address_temp, std::move(address_meta));
					operands.value = address_temp;
				} else if (lv_info.kind == LValueInfo::Kind::ArrayElement &&
						   lv_info.array_index.has_value()) {
					// Materialize an ArrayElementAddress instruction for returning a reference
					// to an array element (e.g., return values[index]).
					// This converts the metadata-only representation into an explicit IR instruction
					// that the code generator can handle directly.
					auto getReferencedReturnSizeBits = [&]() {
						if (needs_type_index(current_function_return_type_index_.category())) {
							if (const TypeInfo* type_info = tryGetTypeInfo(current_function_return_type_index_)) {
								if (type_info->hasStoredSize()) {
									return type_info->sizeInBits().value;
								}
							}
						}
						int fallback_bits = get_type_size_bits(resolve_type_alias(current_function_return_type_index_));
						return fallback_bits > 0 ? fallback_bits : current_function_return_size_;
					};
					auto getArrayIndexTypedValue = [&](const IrValue& index_value) {
						TypedValue typed_index;
						if (const auto* temp_index = std::get_if<TempVar>(&index_value)) {
							const TempVarMetadata index_meta = getTempVarMetadata(*temp_index);
							TypeCategory index_category = index_meta.valueType();
							if (index_category == TypeCategory::Invalid) {
								index_category = TypeCategory::LongLong;
							}
							typed_index.type_index = index_meta.value_type_index.category() != TypeCategory::Invalid
													 ? index_meta.value_type_index
													 : nativeTypeIndex(index_category);
							typed_index.ir_type = index_meta.ir_type;
							typed_index.size_in_bits = index_meta.value_size_bits.value > 0
													 ? index_meta.value_size_bits
													 : SizeInBits{64};
							typed_index.is_signed = isSignedType(index_category);
						} else if (std::holds_alternative<unsigned long long>(index_value)) {
							typed_index.setType(TypeCategory::UnsignedLongLong);
							typed_index.ir_type = IrType::Integer;
							typed_index.size_in_bits = SizeInBits{64};
						} else {
							typed_index.setType(TypeCategory::LongLong);
							typed_index.ir_type = IrType::Integer;
							typed_index.size_in_bits = SizeInBits{64};
						}
						typed_index.value = index_value;
						return typed_index;
					};
					ArrayElementAddressOp elem_addr;
					TempVar address_temp = var_counter.next();
					elem_addr.result = address_temp;
					elem_addr.element_type_index = current_function_return_type_index_;
					elem_addr.element_size_in_bits = getReferencedReturnSizeBits();

					// When the base is a StringHandle like "this.values", the code generator's
					// handleArrayElementAddress can't handle qualified names. Emit an AddressOfMember
					// first to compute the base pointer, then use the TempVar result.
					bool base_is_pointer = lv_info.is_pointer_to_array;
					if (std::holds_alternative<StringHandle>(lv_info.base)) {
						StringHandle base_sh = std::get<StringHandle>(lv_info.base);
						std::string_view base_sv = StringTable::getStringView(base_sh);
						size_t dot_pos = base_sv.find('.');
						if (dot_pos != std::string_view::npos) {
							// Qualified name like "this.values" - emit AddressOfMember to resolve
							StringHandle obj_name = StringTable::getOrInternStringHandle(base_sv.substr(0, dot_pos));
							TempVar base_addr_temp = var_counter.next();
							AddressOfMemberOp addr_op;
							addr_op.result = base_addr_temp;
							addr_op.base_object = obj_name;
							addr_op.member_offset = lv_info.offset;
							addr_op.member_type_index = current_function_return_type_index_;
							addr_op.member_size_in_bits = elem_addr.element_size_in_bits;
							ir_.addInstruction(IrInstruction(IrOpcode::AddressOfMember, std::move(addr_op), node.return_token()));
							elem_addr.array = base_addr_temp;
							base_is_pointer = true;
						} else {
							elem_addr.array = base_sh;
						}
					} else {
						// TempVar base (e.g., from AddressOfMember/emitArrayMemberDecay)
						// holds a computed pointer, so the code generator must load it first.
						elem_addr.array = std::get<TempVar>(lv_info.base);
						base_is_pointer = true;
					}
					elem_addr.is_pointer_to_array = base_is_pointer;

					// Convert IrValue index to TypedValue
					const IrValue& idx_val = *lv_info.array_index;
					elem_addr.index = getArrayIndexTypedValue(idx_val);
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(elem_addr), node.return_token()));
					TempVarMetadata address_meta = TempVarMetadata::makeReference(currentFunctionReturnTypeIndex(), SizeInBits{current_function_return_size_}, ValueCategory::LValue);
					address_meta.lvalue_info = LValueInfo(LValueInfo::Kind::Indirect, address_temp, 0);
					setTempVarMetadata(address_temp, std::move(address_meta));
					operands.value = address_temp;
				} else if (lv_info.kind == LValueInfo::Kind::Global &&
						   std::holds_alternative<StringHandle>(lv_info.base)) {
					TempVar address_temp = emitAddressOf(
						currentFunctionReturnType(),
						current_function_return_size_,
						IrValue(std::get<StringHandle>(lv_info.base)),
						node.return_token());
					setTempVarMetadata(address_temp, TempVarMetadata::makeAddressOnly(
						currentFunctionReturnTypeIndex(),
						SizeInBits{current_function_return_size_},
						ValueCategory::LValue));
					operands.value = address_temp;
				}
			}
		}

			// Call any enclosing __finally funclets before returning
		emitSehFinallyCallsBeforeReturn(node.return_token());
		emitAndClearFullExpressionTempDestructors();

			// Emit destructor calls for all local variables before returning.
			// The return value expression has already been evaluated above, so destroying
			// locals here is correct: the return value is computed first, then locals are destroyed.
		emitDestructorsForNonLocalExit(0);

			// Extract IrValue from operands.value
		IrValue return_value;
		if (const auto* ull_val = std::get_if<unsigned long long>(&operands.value)) {
			return_value = *ull_val;
		} else if (std::holds_alternative<TempVar>(operands.value)) {
			TempVar return_temp = std::get<TempVar>(operands.value);
			return_value = return_temp;

				// C++17 mandatory copy elision: Check if this is a prvalue (e.g., constructor call result)
				// being returned - prvalues used to initialize objects of the same type must have copies elided
			if (isTempVarRVOEligible(return_temp)) {
				FLASH_LOG_FORMAT(Codegen, Debug,
								 "RVO opportunity detected: returning prvalue {} (constructor call result)",
								 return_temp.name());
			}

				// Mark the temp as a return value for potential NRVO analysis
			markTempVarAsReturnValue(return_temp);
		} else if (const auto* string = std::get_if<StringHandle>(&operands.value)) {
			return_value = *string;
		} else if (const auto* d_val = std::get_if<double>(&operands.value)) {
			return_value = *d_val;
		}
			// Use the function's return type, not the expression type
		emitReturn(return_value, currentFunctionReturnType(), current_function_return_size_,
				   node.return_token());
	} else {
			// Call any enclosing __finally funclets before returning
		emitSehFinallyCallsBeforeReturn(node.return_token());
		emitAndClearFullExpressionTempDestructors();
			// Emit destructor calls for all local variables before returning
		emitDestructorsForNonLocalExit(0);
		emitVoidReturn(node.return_token());
	}
}
