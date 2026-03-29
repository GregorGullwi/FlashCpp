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
			node.identifier_name()
		);
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
					Type::Enum, enum_type_index, enum_info->underlying_size, enum_type_token);

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
				TypeIndex return_type_index = current_function_return_type_index_;
				int return_size = current_function_return_size_;

				if (!return_type_index.isStruct()) {
					FLASH_LOG(Codegen, Error, "InitializerListNode in return statement for non-struct type");
					return;
				}

				// Find the struct info
				const StructTypeInfo* struct_info = nullptr;

				// Look up the struct by return type index or name
				for (size_t i = 0; i < getTypeInfoCount(); ++i) {
					if (getTypeInfo(TypeIndex{i}).struct_info_ &&
					static_cast<int>(getTypeInfo(TypeIndex{i}).struct_info_->total_size * 8) == return_size) {
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
				emitReturn(temp_var, return_type_index, return_size, node.return_token());
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
								current_function_return_type_index_, current_function_return_size_,
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
				TypeCategory expr_type = operands.type_index.category();

				// If returning a void expression in a void function, just emit void return
				// (the expression was already evaluated for its side effects)
				if (expr_type == TypeCategory::Void && current_function_return_type_ == TypeCategory::Void) {
					emitSehFinallyCallsBeforeReturn(node.return_token());
					emitAndClearFullExpressionTempDestructors();
					emitDestructorsForNonLocalExit(0);
					emitVoidReturn(node.return_token());
					return;
				}
			}

			if (isPlaceholderAutoType(current_function_return_type_)) {
				throw InternalError("Unresolved placeholder return type reached IR return lowering");
			}

			// Convert to the function's return type if necessary
			// Skip type conversion for reference returns - the expression already has the correct representation
			if (!current_function_returns_reference_) {
				TypeCategory expr_type = operands.type_index.category();
				int expr_size = operands.size_in_bits.value;

				// Get the current function's return type
				TypeCategory return_type = current_function_return_type_;
				int return_size = current_function_return_size_;
				TypeSpecifierNode return_type_spec(
					current_function_return_type_index_.is_valid()
						? current_function_return_type_index_
						: TypeIndex{0, return_type},
					return_size,
					node.return_token());
				const bool use_return_slot_for_ctor = current_function_has_hidden_return_param_;

				// Check whether the semantic pass has already computed a cast annotation.
				// When present for a non-struct conversion, apply it and skip the local policy.
				// When present as UserDefined, the source is a struct with a conversion operator.
				bool sema_applied_conversion = false;
				if (auto materialized = tryMaterializeSemaSelectedConvertingConstructor(
						operands, *expr_opt, return_type_spec, node.return_token(), use_return_slot_for_ctor)) {
					operands = *materialized;
					expr_type = operands.type_index.category();
					expr_size = operands.size_in_bits.value;
					sema_applied_conversion = true;
				}
				if (sema_ && expr_opt->is<ExpressionNode>()) {
					const void* key = static_cast<const void*>(&expr_opt->as<ExpressionNode>());
					auto slot = sema_->getSlot(key);
					if (!sema_applied_conversion && slot.has_value() && slot->has_cast()) {
						const ImplicitCastInfo& cast_info =
							sema_->castInfoTable()[slot->cast_info_index.value - 1];
						const Type annotated_source_type = sema_->typeContext().get(cast_info.source_type_id).base_type;
						const Type to_type   = sema_->typeContext().get(cast_info.target_type_id).base_type;
						if (cast_info.cast_kind == StandardConversionKind::UserDefined &&
							annotated_source_type == Type::Struct) {
							// Sema annotated a user-defined conversion operator call
							TypeIndex source_type_idx = sema_->typeContext().get(cast_info.source_type_id).type_index;
							if (source_type_idx.is_valid() && source_type_idx.index() < getTypeInfoCount()) {
								const TypeInfo& src_type_info = getTypeInfo(source_type_idx);
								const StructTypeInfo* src_struct_info = src_type_info.getStructInfo();
								const TypeIndex ret_type_idx = (return_type == TypeCategory::Struct) ? current_function_return_type_index_ : TypeIndex{};
								const bool source_is_const = ((static_cast<uint8_t>(sema_->typeContext().get(cast_info.source_type_id).base_cv))
									& (static_cast<uint8_t>(CVQualifier::Const))) != 0;
								const StructMemberFunction* conv_op = findConversionOperator(
									src_struct_info, categoryToType(return_type), ret_type_idx, source_is_const);
								if (conv_op) {
									FLASH_LOG(Codegen, Debug, "Sema-annotated user-defined conversion in return from ",
										StringTable::getStringView(src_type_info.name()), " to return type");
									if (auto result = emitConversionOperatorCall(operands, src_type_info, *conv_op,
											categoryToType(return_type), ret_type_idx, return_size, node.return_token())) {
										operands = *result;
										sema_applied_conversion = true;
									}
								}
							}
						} else if (annotated_source_type != Type::Struct && to_type != Type::Struct) {
							Type from_type = annotated_source_type;
							// Sema may annotate as Type::Enum while codegen resolves enum
							// constants to their underlying type; use actual runtime type.
							if (from_type == Type::Enum && from_type != categoryToType(operands.type_index.category()))
								from_type = categoryToType(operands.type_index.category());
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
					if (expr_type == TypeCategory::Struct) {
						TypeIndex expr_type_index = operands.type_index;

						if (expr_type_index.is_valid() && expr_type_index.index() < getTypeInfoCount()) {
							const TypeInfo& source_type_info = getTypeInfo(expr_type_index);
							const StructTypeInfo* source_struct_info = source_type_info.getStructInfo();
							const TypeIndex ret_type_idx = (return_type == TypeCategory::Struct) ? current_function_return_type_index_ : TypeIndex{};

							// Look for a conversion operator to the return type
							const StructMemberFunction* conv_op = findConversionOperator(
								source_struct_info, categoryToType(return_type), ret_type_idx, isExprConstQualified(*expr_opt));

							if (conv_op) {
								FLASH_LOG(Codegen, Debug, "Found conversion operator in return statement from ",
									StringTable::getStringView(source_type_info.name()),
									" to return type");
								if (auto result = emitConversionOperatorCall(operands, source_type_info, *conv_op,
										categoryToType(return_type), ret_type_idx, return_size, node.return_token()))
									operands = *result;
							} else {
								// No conversion operator found - fall back to generateTypeConversion
								operands = generateTypeConversion(operands, categoryToType(expr_type), categoryToType(return_type), node.return_token());
							}
						} else {
							// No valid type_index - fall back to generateTypeConversion
							operands = generateTypeConversion(operands, categoryToType(expr_type), categoryToType(return_type), node.return_token());
						}
					} else {
						// Phase 15: sema should annotate standard arithmetic return conversions.
						// Log a warning when sema missed it (inferExpressionType may fail for
						// some expression contexts). Same-type size mismatches are not type
						// conversions — no annotation expected.
						if (sema_normalized_current_function_ && expr_type != return_type &&
							is_standard_arithmetic_type(categoryToType(expr_type)) && is_standard_arithmetic_type(categoryToType(return_type))) {
							throw InternalError(std::string("Phase 15: sema missed return conversion (") + std::string(getTypeName(categoryToType(expr_type))) + " -> " + std::string(getTypeName(categoryToType(return_type))) + ")");
						}
						// Fallback for non-arithmetic types (enum, user_defined, etc.)
						operands = generateTypeConversion(operands, categoryToType(expr_type), categoryToType(return_type), node.return_token());
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
						addr_member_op.member_type_index = TypeIndex{0, current_function_return_type_};
						addr_member_op.member_size_in_bits = current_function_return_size_;
						ir_.addInstruction(IrInstruction(IrOpcode::AddressOfMember, std::move(addr_member_op), node.return_token()));
						TempVarMetadata address_meta = TempVarMetadata::makeReference(TypeIndex{0, current_function_return_type_}, SizeInBits{current_function_return_size_}, ValueCategory::LValue);
						address_meta.lvalue_info = LValueInfo(LValueInfo::Kind::Indirect, address_temp, 0);
						setTempVarMetadata(address_temp, std::move(address_meta));
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
			emitReturn(return_value, current_function_return_type_index_, current_function_return_size_,
			node.return_token());
		}
		else {
			// Call any enclosing __finally funclets before returning
			emitSehFinallyCallsBeforeReturn(node.return_token());
			emitAndClearFullExpressionTempDestructors();
			// Emit destructor calls for all local variables before returning
			emitDestructorsForNonLocalExit(0);
			emitVoidReturn(node.return_token());
		}
	}
