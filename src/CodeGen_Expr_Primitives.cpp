#include "CodeGen.h"

	std::vector<IrOperand> AstToIr::visitExpressionNode(const ExpressionNode& exprNode, 
	ExpressionContext context) {
		return std::visit([this, context](const auto& expr) -> std::vector<IrOperand> {
			using T = std::decay_t<decltype(expr)>;
			if constexpr (std::is_same_v<T, IdentifierNode>) {
				return generateIdentifierIr(expr, context);
			} else if constexpr (std::is_same_v<T, QualifiedIdentifierNode>) {
				return generateQualifiedIdentifierIr(expr);
			} else if constexpr (std::is_same_v<T, BoolLiteralNode>) {
				return { Type::Bool, 8, expr.value() ? 1ULL : 0ULL, 0ULL };
			} else if constexpr (std::is_same_v<T, NumericLiteralNode>) {
				return generateNumericLiteralIr(expr);
			} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
				return generateStringLiteralIr(expr);
			} else if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
				return generateBinaryOperatorIr(expr);
			} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
				return generateUnaryOperatorIr(expr, context);
			} else if constexpr (std::is_same_v<T, TernaryOperatorNode>) {
				return generateTernaryOperatorIr(expr);
			} else if constexpr (std::is_same_v<T, FunctionCallNode>) {
				return generateFunctionCallIr(expr);
			} else if constexpr (std::is_same_v<T, MemberFunctionCallNode>) {
				return generateMemberFunctionCallIr(expr);
			} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
				return generateArraySubscriptIr(expr, context);
			} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
				return generateMemberAccessIr(expr, context);
			} else if constexpr (std::is_same_v<T, SizeofExprNode>) {
				auto const_result = tryEvaluateAsConstExpr(expr);
				return const_result.empty() ? generateSizeofIr(expr) : const_result;
			} else if constexpr (std::is_same_v<T, SizeofPackNode>) {
				FLASH_LOG(Codegen, Error, "sizeof... operator found during code generation - should have been substituted during template instantiation");
				return {};
			} else if constexpr (std::is_same_v<T, AlignofExprNode>) {
				auto const_result = tryEvaluateAsConstExpr(expr);
				return const_result.empty() ? generateAlignofIr(expr) : const_result;
			} else if constexpr (std::is_same_v<T, NoexceptExprNode>) {
				return generateNoexceptExprIr(expr);
			} else if constexpr (std::is_same_v<T, OffsetofExprNode>) {
				return generateOffsetofIr(expr);
			} else if constexpr (std::is_same_v<T, TypeTraitExprNode>) {
				return generateTypeTraitIr(expr);
			} else if constexpr (std::is_same_v<T, NewExpressionNode>) {
				return generateNewExpressionIr(expr);
			} else if constexpr (std::is_same_v<T, DeleteExpressionNode>) {
				return generateDeleteExpressionIr(expr);
			} else if constexpr (std::is_same_v<T, StaticCastNode>) {
				return generateStaticCastIr(expr);
			} else if constexpr (std::is_same_v<T, DynamicCastNode>) {
				return generateDynamicCastIr(expr);
			} else if constexpr (std::is_same_v<T, ConstCastNode>) {
				return generateConstCastIr(expr);
			} else if constexpr (std::is_same_v<T, ReinterpretCastNode>) {
				return generateReinterpretCastIr(expr);
			} else if constexpr (std::is_same_v<T, TypeidNode>) {
				return generateTypeidIr(expr);
			} else if constexpr (std::is_same_v<T, LambdaExpressionNode>) {
				return generateLambdaExpressionIr(expr);
			} else if constexpr (std::is_same_v<T, ConstructorCallNode>) {
				return generateConstructorCallIr(expr);
			} else if constexpr (std::is_same_v<T, TemplateParameterReferenceNode>) {
				return generateTemplateParameterReferenceIr(expr);
			} else if constexpr (std::is_same_v<T, FoldExpressionNode>) {
				FLASH_LOG(Codegen, Error, "Fold expression found during code generation - should have been expanded during template instantiation");
				throw InternalError("Unexpanded fold expression reached codegen - complex pack pattern not yet supported");
			} else if constexpr (std::is_same_v<T, PseudoDestructorCallNode>) {
				return generatePseudoDestructorCallIr(expr);
			} else if constexpr (std::is_same_v<T, PointerToMemberAccessNode>) {
				return generatePointerToMemberAccessIr(expr);
			} else if constexpr (std::is_same_v<T, PackExpansionExprNode>) {
				FLASH_LOG(Codegen, Error, "PackExpansionExprNode found during code generation - should have been expanded during template instantiation");
				throw InternalError("Unexpanded pack expansion reached codegen - pack expansion in function call contexts not yet implemented");
			} else if constexpr (std::is_same_v<T, InitializerListConstructionNode>) {
				return generateInitializerListConstructionIr(expr);
			} else if constexpr (std::is_same_v<T, ThrowExpressionNode>) {
				FLASH_LOG(Codegen, Debug, "ThrowExpressionNode encountered in expression context - skipping codegen");
				return {};
			} else {
				static_assert(!std::is_same_v<T, T>, "Unhandled ExpressionNode variant");
			}
		}, exprNode);
	}

	std::vector<IrOperand> AstToIr::generateNoexceptExprIr(const NoexceptExprNode& noexcept_node) {
		bool is_noexcept = true;
		if (noexcept_node.expr().is<ExpressionNode>()) {
			is_noexcept = isExpressionNoexcept(noexcept_node.expr().as<ExpressionNode>());
		}
		return { Type::Bool, 8, is_noexcept ? 1ULL : 0ULL, 0ULL };
	}

	std::vector<IrOperand> AstToIr::generatePseudoDestructorCallIr(const PseudoDestructorCallNode& dtor) {
		std::string_view type_name = dtor.has_qualified_name() 
			? dtor.qualified_type_name().view()
			: dtor.type_name();
		FLASH_LOG(Codegen, Debug, "Generating explicit destructor call for type: ", type_name);
		
		ASTNode object_node = dtor.object();
		std::string_view object_name;
		const DeclarationNode* object_decl = nullptr;
		TypeSpecifierNode object_type(Type::Void, TypeQualifier::None, 0);
		
		if (object_node.is<ExpressionNode>()) {
			const ExpressionNode& object_expr = object_node.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(object_expr)) {
				const IdentifierNode& object_ident = std::get<IdentifierNode>(object_expr);
				object_name = object_ident.name();
				const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
				if (symbol.has_value()) {
					object_decl = get_decl_from_symbol(*symbol);
					if (object_decl) {
						object_type = object_decl->type_node().as<TypeSpecifierNode>();
						if (dtor.is_arrow_access() && object_type.pointer_levels().size() > 0) {
							object_type.remove_pointer_level();
						}
					}
				}
			}
		}
		
		if (is_struct_type(object_type.type())) {
			size_t struct_type_index = object_type.type_index();
			if (struct_type_index > 0 && struct_type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[struct_type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info && struct_info->hasDestructor()) {
					FLASH_LOG(Codegen, Debug, "Generating IR for destructor call on struct: ", 
					StringTable::getStringView(struct_info->getName()));
					DestructorCallOp dtor_op;
					dtor_op.struct_name = struct_info->getName();
					dtor_op.object = StringTable::getOrInternStringHandle(object_name);
					ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), dtor.type_name_token()));
				} else {
					FLASH_LOG(Codegen, Debug, "Struct ", type_name, " has no destructor, skipping call");
				}
			}
		} else {
			FLASH_LOG(Codegen, Debug, "Non-class type ", type_name, " - destructor call is no-op");
		}
		return {};
	}

	std::vector<IrOperand> AstToIr::generatePointerToMemberAccessIr(const PointerToMemberAccessNode& ptmNode) {
		auto object_operands = visitExpressionNode(ptmNode.object().as<ExpressionNode>(), ExpressionContext::LValueAddress);
		if (object_operands.empty()) {
			FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: object expression returned empty operands");
			return {};
		}
		
		auto ptr_operands = visitExpressionNode(ptmNode.member_pointer().as<ExpressionNode>());
		if (ptr_operands.empty()) {
			FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: member pointer expression returned empty operands");
			return {};
		}
		
		TempVar object_addr = var_counter.next();
		if (ptmNode.is_arrow()) {
			if (std::holds_alternative<StringHandle>(object_operands[2])) {
				StringHandle obj_ptr_name = std::get<StringHandle>(object_operands[2]);
				AssignmentOp assign_op;
				assign_op.result = object_addr;
				assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, object_addr};
				assign_op.rhs = TypedValue{Type::UnsignedLongLong, 64, obj_ptr_name};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
			} else if (std::holds_alternative<TempVar>(object_operands[2])) {
				object_addr = std::get<TempVar>(object_operands[2]);
			} else {
				FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: unexpected object operand type for ->*");
				return {};
			}
		} else {
			if (std::holds_alternative<StringHandle>(object_operands[2])) {
				StringHandle obj_name = std::get<StringHandle>(object_operands[2]);
				AddressOfOp addr_op;
				addr_op.result = object_addr;
				addr_op.operand = TypedValue{
					.type = std::get<Type>(object_operands[0]),
					.size_in_bits = std::get<int>(object_operands[1]),
					.value = obj_name,
					.pointer_depth = 0
				};
				ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
			} else if (std::holds_alternative<TempVar>(object_operands[2])) {
				object_addr = std::get<TempVar>(object_operands[2]);
			} else {
				FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: unexpected object operand type for .*");
				return {};
			}
		}
		
		if (ptr_operands.size() < 2) {
			FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: member pointer operands incomplete (size=", ptr_operands.size(), ")");
			return {};
		}
		
		TempVar member_addr = var_counter.next();
		BinaryOp add_op;
		add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, object_addr};
		add_op.rhs = toTypedValue(ptr_operands);
		add_op.result = member_addr;
		ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), ptmNode.operator_token()));
		
		Type member_type = std::get<Type>(ptr_operands[0]);
		int member_size = std::get<int>(ptr_operands[1]);
		TypeIndex member_type_index = 0;
		if (ptr_operands.size() >= 4 && std::holds_alternative<unsigned long long>(ptr_operands[3])) {
			member_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(ptr_operands[3]));
		}
		
		TempVar result_var = emitDereference(member_type, member_size, 1, member_addr, ptmNode.operator_token());
		return { member_type, member_size, result_var, static_cast<unsigned long long>(member_type_index) };
	}

	int AstToIr::calculateIdentifierSizeBits(const TypeSpecifierNode& type_node, bool is_array, std::string_view identifier_name) {
		bool is_array_type = is_array || type_node.is_array();
		int size_bits;
		
		if (is_array_type || type_node.pointer_depth() > 0) {
			// For arrays and pointers, the identifier itself is a pointer (64 bits on x64)
			// The element/pointee size is stored separately and used for pointer arithmetic
			size_bits = 64;  // Pointer size on x64 architecture
		} else {
			// For regular variables, return the variable size
			size_bits = static_cast<int>(type_node.size_in_bits());
			// Fallback: if size_bits is 0, calculate from type (parser bug workaround)
			if (size_bits == 0) {
				FLASH_LOG(Codegen, Warning, "Parser returned size_bits=0 for identifier '", identifier_name, 
				"' (type=", static_cast<int>(type_node.type()), ") - using fallback calculation");
				size_bits = get_type_size_bits(type_node.type());
			}
		}
		
		return size_bits;
	}

	std::vector<IrOperand> AstToIr::generateIdentifierIr(const IdentifierNode& identifierNode, 
	ExpressionContext context) {
		// Check if this is a captured variable in a lambda
		StringHandle var_name_str = StringTable::getOrInternStringHandle(identifierNode.name());
		if (current_lambda_context_.isActive() &&
		current_lambda_context_.captures.find(var_name_str) != current_lambda_context_.captures.end()) {
			// This is a captured variable - generate member access (this->x)
			// Look up the closure struct type
			auto type_it = gTypesByName.find(current_lambda_context_.closure_type);
			if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
				TypeIndex closure_type_index = type_it->second->type_index_;
				// Find the member
				auto result = FlashCpp::gLazyMemberResolver.resolve(closure_type_index, var_name_str);
				if (result) {
					const StructMember* member = result.member;
					// Check if this is a by-reference capture
					auto kind_it = current_lambda_context_.capture_kinds.find(var_name_str);
					bool is_reference = (kind_it != current_lambda_context_.capture_kinds.end() &&
					kind_it->second == LambdaCaptureNode::CaptureKind::ByReference);

					if (is_reference) {
						// By-reference capture: member is a pointer, need to dereference
						// First, load the pointer from the closure
						TempVar ptr_temp = var_counter.next();
						MemberLoadOp member_load;
						member_load.result.value = ptr_temp;
						member_load.result.type = member->type;  // Base type (e.g., Int)
						member_load.result.size_in_bits = 64;  // pointer size in bits
						member_load.object = StringTable::getOrInternStringHandle("this");
						member_load.member_name = member->getName();
						member_load.offset = static_cast<int>(result.adjusted_offset);
						member_load.is_reference = member->is_reference();
						member_load.is_rvalue_reference = member->is_rvalue_reference();
						member_load.struct_type_info = nullptr;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

						// The ptr_temp now contains the address of the captured variable
						// We need to dereference it using PointerDereference
						auto capture_type_it = current_lambda_context_.capture_types.find(var_name_str);
						if (capture_type_it != current_lambda_context_.capture_types.end()) {
							const TypeSpecifierNode& orig_type = capture_type_it->second;

							// Generate Dereference to load the value
							TempVar result_temp = emitDereference(orig_type.type(), 64, 0, ptr_temp);
							
							// Mark as lvalue with Indirect metadata for unified assignment handler
							// This represents dereferencing a pointer: *ptr
							LValueInfo lvalue_info(
								LValueInfo::Kind::Indirect,
								ptr_temp,  // The pointer temp var
								0  // offset is 0 for dereference
							);
							setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));

							TypeIndex type_index = (orig_type.type() == Type::Struct) ? orig_type.type_index() : 0;
							return { orig_type.type(), static_cast<int>(orig_type.size_in_bits()), result_temp, static_cast<unsigned long long>(type_index) };
						}

						// Fallback: return the pointer temp
						return { member->type, 64, ptr_temp, 0ULL };
					} else {
						// By-value capture: direct member access
						TempVar result_temp = var_counter.next();
						MemberLoadOp member_load;
						member_load.result.value = result_temp;
						member_load.result.type = member->type;
						member_load.result.size_in_bits = static_cast<int>(member->size * 8);
						member_load.object = StringTable::getOrInternStringHandle("this");  // implicit this pointer
						member_load.member_name = member->getName();
						member_load.offset = static_cast<int>(result.adjusted_offset);
						member_load.is_reference = member->is_reference();
						member_load.is_rvalue_reference = member->is_rvalue_reference();
						member_load.struct_type_info = nullptr;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
						
						// For mutable lambdas, set LValue metadata so assignments write back to the member
						if (current_lambda_context_.is_mutable) {
							// Use 'this' as the base object (StringHandle version)
							// The assignment handler will emit MemberStore using this info
							LValueInfo lvalue_info(
								LValueInfo::Kind::Member,
								StringTable::getOrInternStringHandle("this"),  // object name (this pointer)
								static_cast<int>(result.adjusted_offset)
							);
							lvalue_info.member_name = member->getName();
							lvalue_info.is_pointer_to_member = true;  // 'this' is a pointer, need to dereference
							setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
						}
						
						TypeIndex type_index = (member->type == Type::Struct) ? member->type_index : 0;
						return { member->type, static_cast<int>(member->size * 8), result_temp, static_cast<unsigned long long>(type_index) };
					}
				}
			}
		}

		// If we're inside a [*this] lambda, prefer resolving to members of the copied object
		if (isInCopyThisLambda() && current_lambda_context_.enclosing_struct_type_index > 0) {
			auto result = FlashCpp::gLazyMemberResolver.resolve(current_lambda_context_.enclosing_struct_type_index, var_name_str);
			if (result) {
				const StructMember* member = result.member;
				if (auto copy_this_temp = emitLoadCopyThis(Token())) {
					TempVar result_temp = var_counter.next();
					MemberLoadOp member_load;
					member_load.result.value = result_temp;
					member_load.result.type = member->type;
					member_load.result.size_in_bits = static_cast<int>(member->size * 8);
					member_load.object = *copy_this_temp;
					member_load.member_name = member->getName();
					member_load.offset = static_cast<int>(result.adjusted_offset);
					member_load.is_reference = member->is_reference();
					member_load.is_rvalue_reference = member->is_rvalue_reference();
					member_load.struct_type_info = nullptr;
					ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
					
					LValueInfo lvalue_info(
						LValueInfo::Kind::Member,
						*copy_this_temp,
						static_cast<int>(result.adjusted_offset)
					);
					lvalue_info.member_name = member->getName();
					setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
					
					TypeIndex type_index = (member->type == Type::Struct) ? member->type_index : 0;
					return { member->type, static_cast<int>(member->size * 8), result_temp, static_cast<unsigned long long>(type_index) };
				}
			}
		}

		// Check if this is a static local variable FIRST (before any other lookups)
		// Phase 4: Using StringHandle for lookup
		StringHandle identifier_handle = StringTable::getOrInternStringHandle(identifierNode.name());
		auto static_local_it = static_local_names_.find(identifier_handle);
		if (static_local_it != static_local_names_.end()) {
			// This is a static local - generate GlobalLoad with mangled name
			const StaticLocalInfo& info = static_local_it->second;

			// For LValueAddress context (assignment LHS), return the mangled name directly
			// This allows the assignment instruction to store to the global variable
			if (context == ExpressionContext::LValueAddress) {
				return { info.type, info.size_in_bits, info.mangled_name, 0ULL };
			}

			// For Load context (normal read), generate GlobalLoad with mangled name
			TempVar result_temp = var_counter.next();
			GlobalLoadOp op;
			op.result.type = info.type;
			op.result.size_in_bits = info.size_in_bits;
			op.result.value = result_temp;
			op.global_name = info.mangled_name;  // Use mangled name
			ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

			// Return the temp variable that will hold the loaded value
			return { info.type, info.size_in_bits, result_temp, 0ULL };
		}

		// Check using declarations from local scope FIRST, before local symbol table lookup
		// This handles cases like: using ::globalValue; return globalValue;
		// where globalValue should resolve to the global namespace version even if there's
		// a namespace-scoped version with the same name
		std::optional<ASTNode> symbol;
		bool is_global = false;
		std::optional<StringHandle> resolved_qualified_name;  // Track the qualified name from using declaration
		
		if (global_symbol_table_) {
			auto using_declarations = symbol_table.get_current_using_declaration_handles();
			for (const auto& [local_name, target_info] : using_declarations) {
				if (local_name == identifierNode.name()) {
					const auto& [namespace_handle, original_name] = target_info;
					StringHandle original_handle = StringTable::getOrInternStringHandle(original_name);
					resolved_qualified_name = namespace_handle.isGlobal()
						? original_handle
						: gNamespaceRegistry.buildQualifiedIdentifier(namespace_handle, original_handle);
					
					// Resolve using the global symbol table
					symbol = global_symbol_table_->lookup_qualified(namespace_handle, original_handle);
					if (symbol.has_value()) {
						is_global = true;
						break;
					}
				}
			}
		}
		
		// If not resolved via using declaration, try local symbol table (for local variables, parameters, etc.)
		// This ensures constructor parameters shadow member variables in initializer expressions
		if (!symbol.has_value()) {
			symbol = symbol_table.lookup(identifierNode.name());
		}

		// If not found locally, try global symbol table (for enum values, global variables, namespace-scoped variables, etc.)
		if (!symbol.has_value() && global_symbol_table_) {
			symbol = global_symbol_table_->lookup(identifierNode.name());
			is_global = symbol.has_value();  // If found in global table, it's a global
			
			// If still not found, check using directives from local scope in the global symbol table
			// This handles cases like: using namespace X; int y = X_var;
			// where X_var is defined in namespace X
			if (!symbol.has_value()) {
				auto using_directives = symbol_table.get_current_using_directive_handles();
				for (NamespaceHandle ns_handle : using_directives) {
					symbol = global_symbol_table_->lookup_qualified(ns_handle, identifierNode.name());
					if (symbol.has_value()) {
						is_global = true;
						break;
					}
				}
			}

			// If still unresolved, try unqualified lookup through the current namespace chain.
			// This handles unscoped enum enumerators in namespace scope (e.g., memory_order_relaxed in std).
			if (!symbol.has_value() && !current_namespace_stack_.empty()) {
				NamespaceHandle current_ns = NamespaceRegistry::GLOBAL_NAMESPACE;
				bool namespace_path_valid = true;
				for (const auto& ns_name : current_namespace_stack_) {
					NamespaceHandle next_ns = gNamespaceRegistry.lookupNamespace(
						current_ns, StringTable::getOrInternStringHandle(ns_name));
					if (!next_ns.isValid()) {
						namespace_path_valid = false;
						break;
					}
					current_ns = next_ns;
				}

				if (namespace_path_valid) {
					NamespaceHandle search_ns = current_ns;
					while (search_ns.isValid()) {
						symbol = global_symbol_table_->lookup_qualified(search_ns, identifier_handle);
						if (symbol.has_value()) {
							is_global = true;
							resolved_qualified_name = search_ns.isGlobal()
								? identifier_handle
								: gNamespaceRegistry.buildQualifiedIdentifier(search_ns, identifier_handle);
							break;
						}
						if (search_ns.isGlobal()) {
							break;
						}
						search_ns = gNamespaceRegistry.getParent(search_ns);
					}
				}
			}

			// If still unresolved, consult namespace-scope using declarations/directives
			// recorded in the global symbol table (e.g. using std::memory_order_relaxed;).
			if (!symbol.has_value()) {
				auto global_using_declarations = global_symbol_table_->get_current_using_declaration_handles();
				for (const auto& [local_name, target_info] : global_using_declarations) {
					if (local_name == identifierNode.name()) {
						const auto& [namespace_handle, original_name] = target_info;
						symbol = global_symbol_table_->lookup_qualified(namespace_handle, original_name);
						if (symbol.has_value()) {
							is_global = true;
							StringHandle original_handle = StringTable::getOrInternStringHandle(original_name);
							resolved_qualified_name = namespace_handle.isGlobal()
								? original_handle
								: gNamespaceRegistry.buildQualifiedIdentifier(namespace_handle, original_handle);
							break;
						}
					}
				}
			}
			if (!symbol.has_value()) {
				auto global_using_directives = global_symbol_table_->get_current_using_directive_handles();
				for (NamespaceHandle ns_handle : global_using_directives) {
					symbol = global_symbol_table_->lookup_qualified(ns_handle, identifierNode.name());
					if (symbol.has_value()) {
						is_global = true;
						resolved_qualified_name = ns_handle.isGlobal()
							? identifier_handle
							: gNamespaceRegistry.buildQualifiedIdentifier(ns_handle, identifier_handle);
						break;
					}
				}
			}

		}

		// Only check if it's a member variable if NOT found in symbol tables
		// This gives priority to parameters and local variables over member variables
		// Skip this for [*this] lambdas - they need to access through __copy_this instead
		// Also check that we're not in a lambda context where this would be an enclosing struct member
		if (!symbol.has_value() && current_struct_name_.isValid() && 
		!isInCopyThisLambda() && !current_lambda_context_.isActive()) {
			// Look up the struct type
			auto type_it = gTypesByName.find(current_struct_name_);
			if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
				TypeIndex struct_type_index = type_it->second->type_index_;
				const StructTypeInfo* struct_info = type_it->second->getStructInfo();
				if (struct_info) {
					// Check if this identifier is a member of the struct
					auto result = FlashCpp::gLazyMemberResolver.resolve(struct_type_index, var_name_str);
					if (result) {
						const StructMember* member = result.member;
						// This is a member variable access - generate MemberAccess IR with implicit 'this'
						TempVar result_temp = var_counter.next();
						MemberLoadOp member_load;
						member_load.result.value = result_temp;
						member_load.result.type = member->type;
						member_load.result.size_in_bits = static_cast<int>(member->size * 8);
						member_load.object = StringTable::getOrInternStringHandle("this");  // implicit this pointer
						member_load.member_name = member->getName();
						member_load.offset = static_cast<int>(result.adjusted_offset);
						member_load.is_reference = member->is_reference();
						member_load.is_rvalue_reference = member->is_rvalue_reference();
						member_load.struct_type_info = nullptr;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
						
						// Mark as lvalue with member metadata for unified assignment handler
						LValueInfo lvalue_info(
							LValueInfo::Kind::Member,
							StringTable::getOrInternStringHandle("this"),
							static_cast<int>(result.adjusted_offset)
						);
						lvalue_info.member_name = member->getName();
						setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
						
						TypeIndex type_index = (member->type == Type::Struct) ? member->type_index : 0;
						return { member->type, static_cast<int>(member->size * 8), result_temp, static_cast<unsigned long long>(type_index) };
					}
					
					// Check if this identifier is a static member
					const StructStaticMember* static_member = struct_info->findStaticMember(var_name_str);
					if (static_member) {
						// This is a static member access - generate GlobalLoad IR
						// Static members are stored as globals with qualified names
						// Note: Namespaces are already included in current_struct_name_ via mangling
						auto qualified_name = StringTable::getOrInternStringHandle(StringBuilder().append(current_struct_name_).append("::"sv).append(var_name_str));
						
						int member_size_bits = static_cast<int>(static_member->size * 8);
						// If size is 0 for struct types, look up from type info
						if (member_size_bits == 0 && static_member->type_index > 0 && static_member->type_index < gTypeInfo.size()) {
							const StructTypeInfo* member_si = gTypeInfo[static_member->type_index].getStructInfo();
							if (member_si) {
								member_size_bits = static_cast<int>(member_si->total_size * 8);
							}
						}
						
						TempVar result_temp = var_counter.next();
						GlobalLoadOp op;
						op.result.type = static_member->type;
						op.result.size_in_bits = member_size_bits;
						op.result.value = result_temp;
						op.global_name = qualified_name;
						ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));
						
						TypeIndex type_index = (static_member->type == Type::Struct) ? static_member->type_index : 0;
						return { static_member->type, member_size_bits, result_temp, static_cast<unsigned long long>(type_index) };
					}
				}
			}
		}
		// If still not found and we're in a struct, check nested enum enumerators
		// Unscoped enums declared inside a class make their enumerators accessible in the class scope
		// Only search enums tracked as nested within the current struct to avoid
		// incorrectly resolving enumerators from unrelated structs.
		if (!symbol.has_value() && current_struct_name_.isValid()) {
			auto type_it = gTypesByName.find(current_struct_name_);
			if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
				const StructTypeInfo* struct_info = type_it->second->getStructInfo();
				if (struct_info) {
					StringHandle id_handle = StringTable::getOrInternStringHandle(identifierNode.name());
					for (TypeIndex enum_idx : struct_info->getNestedEnumIndices()) {
						if (enum_idx < gTypeInfo.size()) {
							const EnumTypeInfo* enum_info = gTypeInfo[enum_idx].getEnumInfo();
							if (enum_info && !enum_info->is_scoped) {
								const Enumerator* enumerator = enum_info->findEnumerator(id_handle);
								if (enumerator) {
									return { enum_info->underlying_type, static_cast<int>(enum_info->underlying_size),
									static_cast<unsigned long long>(enumerator->value) };
								}
							}
						}
					}
				}
			}
		}
		if (!symbol.has_value()) {
			FLASH_LOG(Codegen, Error, "Symbol '", identifierNode.name(), "' not found in symbol table during code generation");
			FLASH_LOG(Codegen, Error, "  Current function: ", current_function_name_);
			FLASH_LOG(Codegen, Error, "  Current struct: ", current_struct_name_);
			throw InternalError("Expected symbol '" + std::string(identifierNode.name()) + "' to exist in code generation");
			return {};
		}

		if (symbol->is<DeclarationNode>()) {
			const auto& decl_node = symbol->as<DeclarationNode>();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// Check if this is an enum value (enumerator constant)
			// IMPORTANT: References and pointers to enum are VARIABLES, not enumerator constants
			// Only non-reference, non-pointer enum-typed identifiers CAN BE enumerators
			// We must verify the identifier actually exists as an enumerator before treating it as a constant
			if (type_node.type() == Type::Enum && !type_node.is_reference() && type_node.pointer_depth() == 0) {
				// Check if this identifier is actually an enumerator (not just a variable of enum type)
				size_t enum_type_index = type_node.type_index();
				if (enum_type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[enum_type_index];
					const EnumTypeInfo* enum_info = type_info.getEnumInfo();
					if (enum_info) {
						// Use findEnumerator to check if this identifier is actually an enumerator
						const Enumerator* enumerator = enum_info->findEnumerator(StringTable::getOrInternStringHandle(identifierNode.name()));
						if (enumerator) {
							// This IS an enumerator constant - return its value using the underlying type
							return { enum_info->underlying_type, static_cast<int>(enum_info->underlying_size),
							static_cast<unsigned long long>(enumerator->value) };
						}
						// If not found as an enumerator, it's a variable of enum type - fall through to variable handling
					}
				}
			}

			// Check if this is a global variable
			if (is_global) {
				// Generate GlobalLoad IR instruction
				TempVar result_temp = var_counter.next();
				// For arrays, result is a pointer (64-bit address)
				bool is_array_type = decl_node.is_array() || type_node.is_array();
				int size_bits = (type_node.pointer_depth() > 0 || is_array_type) ? 64 : static_cast<int>(type_node.size_in_bits());
				GlobalLoadOp op;
				op.result.type = type_node.type();
				op.result.size_in_bits = size_bits;
				op.result.value = result_temp;
				
				// If we resolved this via a using declaration, use the resolved qualified name
				// Otherwise, check if this global has a mangled name (e.g., anonymous namespace variable)
				if (resolved_qualified_name.has_value()) {
					op.global_name = *resolved_qualified_name;
				} else {
					// Phase 4: Using StringHandle for lookup
					StringHandle simple_name_handle = StringTable::getOrInternStringHandle(identifierNode.name());
					auto it = global_variable_names_.find(simple_name_handle);
					if (it != global_variable_names_.end()) {
						op.global_name = it->second;  // Use mangled StringHandle
					} else {
						op.global_name = StringTable::getOrInternStringHandle(identifierNode.name());  // Use simple name as StringHandle
					}
				}
				
				op.is_array = is_array_type;  // Arrays need LEA to get address
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

				// Return the temp variable that will hold the loaded value
				// For pointers and arrays, return 64 bits (pointer size)
				// Include type_index for struct types
				TypeIndex type_index = (type_node.type() == Type::Struct) ? type_node.type_index() : 0;
				return { type_node.type(), size_bits, result_temp, static_cast<unsigned long long>(type_index) };
			}

			// Check if this is a reference parameter - if so, we need to dereference it
			// Reference parameters (both lvalue & and rvalue &&) hold an address, and we need to load the value from that address
			// EXCEPT for array references, where the reference IS the array pointer
			// IMPORTANT: When context is LValueAddress (e.g., LHS of assignment), DON'T dereference - return the parameter name directly
			//
			// NOTE: This handles both reference PARAMETERS and local reference VARIABLES (like structured binding references)
			// The distinction is:
			// - Reference parameters: stored in VariableDeclOp with is_reference=true during code generation
			// - Local reference variables: created with DeclarationNode that has reference TypeSpecifierNode
			if (type_node.is_reference()) {
				// Check if this is actually a local reference variable (not a parameter)
				// Local reference variables are stored on the stack and hold a pointer value
				// We can detect this by checking if a VariableDeclOp was generated with is_reference=true
				// For now, we'll treat all references as parameters unless they fail the parameter test
				
				// For references to arrays (e.g., int (&arr)[3]), the reference parameter
				// already holds the array address directly. We don't dereference it.
				// Just return it as a pointer (64 bits on x64 architecture).
				if (type_node.is_array()) {
					// Return the array reference as a 64-bit pointer
					return { type_node.type(), POINTER_SIZE_BITS, StringTable::getOrInternStringHandle(identifierNode.name()), 0ULL };
				}
				
				// For LValueAddress context (e.g., LHS of assignment, function call with reference parameter)
				// For compound assignments, we need to return a TempVar with lvalue metadata
				// For simple assignments and function calls, we can return the reference directly
				if (context == ExpressionContext::LValueAddress) {
					// For auto types, default to int (32 bits)
					Type pointee_type = type_node.type();
					int pointee_size = static_cast<int>(type_node.size_in_bits());
					if (pointee_type == Type::Auto || pointee_size == 0) {
						pointee_type = Type::Int;
						pointee_size = 32;
					}
					
					TypeIndex type_index = (pointee_type == Type::Struct) ? type_node.type_index() : 0;
					
					// Create a TempVar with Indirect lvalue metadata for compound assignments
					// This allows handleLValueCompoundAssignment to work with reference variables
					TempVar lvalue_temp = var_counter.next();
					FLASH_LOG_FORMAT(Codegen, Debug, "Reference LValueAddress: Creating TempVar {} for reference '{}'", 
						lvalue_temp.var_number, identifierNode.name());
					
					// Generate Assignment to copy the pointer value from the reference parameter to the temp
					StringHandle var_handle = StringTable::getOrInternStringHandle(identifierNode.name());
					AssignmentOp assign_op;
					assign_op.result = lvalue_temp;
					assign_op.lhs = TypedValue{pointee_type, 64, lvalue_temp};  // 64-bit pointer dest
					assign_op.rhs = TypedValue{pointee_type, 64, var_handle};  // 64-bit pointer source
					assign_op.is_pointer_store = false;
					assign_op.dereference_rhs_references = false;  // Don't dereference - just copy the pointer!
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
					
					LValueInfo lvalue_info(
						LValueInfo::Kind::Indirect,
						lvalue_temp,  // Use the temp var holding the address, not the parameter name
						0  // offset is 0 for simple dereference
					);
					setTempVarMetadata(lvalue_temp, TempVarMetadata::makeLValue(lvalue_info));
					FLASH_LOG_FORMAT(Codegen, Debug, "Reference LValueAddress: Set metadata on TempVar {}", lvalue_temp.var_number);
					
					// Return with TempVar that has lvalue metadata
					// The type/size are for the pointee (what the reference refers to)
					return { pointee_type, pointee_size, lvalue_temp, static_cast<unsigned long long>(type_index) };
				}
				
				// For non-array references in Load context, we need to dereference to get the value
				
				// For auto types, default to int (32 bits) since the mangling also defaults to int
				// This matches the behavior in NameMangling.h which falls through to 'H' (int)
				Type pointee_type = type_node.type();
				int pointee_size = static_cast<int>(type_node.size_in_bits());
				if (pointee_type == Type::Auto || pointee_size == 0) {
					pointee_type = Type::Int;
					pointee_size = 32;
				}
				
				// For enum references, treat dereferenced value as underlying type
				// This allows enum variables to work in arithmetic/bitwise operations
				if (pointee_type == Type::Enum && type_node.type_index() < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_node.type_index()];
					const EnumTypeInfo* enum_info = type_info.getEnumInfo();
					if (enum_info) {
						pointee_type = enum_info->underlying_type;
						pointee_size = static_cast<int>(enum_info->underlying_size);
					}
				}
				
				int ptr_depth = type_node.pointer_depth() > 0 ? type_node.pointer_depth() : 1;
				TempVar result_temp = emitDereference(pointee_type, 64, ptr_depth,
					StringTable::getOrInternStringHandle(identifierNode.name()));
				
				// Mark as lvalue with Indirect metadata for unified assignment handler
				// This allows compound assignments (like x *= 2) to work on dereferenced references
				LValueInfo lvalue_info(
					LValueInfo::Kind::Indirect,
					StringTable::getOrInternStringHandle(identifierNode.name()),  // The reference variable name
					0  // offset is 0 for simple dereference
				);
				setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
				
				TypeIndex type_index = (pointee_type == Type::Struct || type_node.type() == Type::Enum) ? type_node.type_index() : 0;
				return { pointee_type, pointee_size, result_temp, static_cast<unsigned long long>(type_index) };
			}
			
			// Regular local variable
			// Use helper function to calculate size_bits with proper fallback handling
			int size_bits = calculateIdentifierSizeBits(type_node, decl_node.is_array(), identifierNode.name());
			
			// For enum variables (not enumerators), return the underlying integer type
			// This allows enum variables to work in arithmetic/bitwise operations
			Type return_type = type_node.type();
			if (type_node.type() == Type::Enum && type_node.type_index() < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_node.type_index()];
				const EnumTypeInfo* enum_info = type_info.getEnumInfo();
				if (enum_info) {
					return_type = enum_info->underlying_type;
					size_bits = static_cast<int>(enum_info->underlying_size);
				}
			}
			
			// For the 4th element: 
			// - For struct types, ALWAYS return type_index (even if it's a pointer to struct)
			// - For enum types, return type_index to preserve type information
			// - For non-struct/enum pointer types, return pointer_depth
			// - Otherwise return 0
			unsigned long long fourth_element = 0ULL;
			if (type_node.type() == Type::Struct || type_node.type() == Type::Enum) {
				fourth_element = static_cast<unsigned long long>(type_node.type_index());
			} else if (type_node.pointer_depth() > 0) {
				fourth_element = static_cast<unsigned long long>(type_node.pointer_depth());
			}
			return { return_type, size_bits, StringTable::getOrInternStringHandle(identifierNode.name()), fourth_element };
		}

		// Check if it's a VariableDeclarationNode
		if (symbol->is<VariableDeclarationNode>()) {
			const auto& var_decl_node = symbol->as<VariableDeclarationNode>();
			const auto& decl_node = var_decl_node.declaration();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// Check if this is actually a global variable
			if (is_global) {
				// This is a global variable - generate GlobalLoad
				TempVar result_temp = var_counter.next();
				// For arrays, pointers, and references, result is a pointer (64-bit address)
				bool is_array_type = decl_node.is_array() || type_node.is_array();
				bool is_ptr_or_ref = type_node.is_pointer() || type_node.is_reference() || type_node.is_function_pointer();
				int size_bits = (is_array_type || is_ptr_or_ref) ? 64 : static_cast<int>(type_node.size_in_bits());
				GlobalLoadOp op;
				op.result.type = type_node.type();
				op.result.size_in_bits = size_bits;
				op.result.value = result_temp;
				
				// If we resolved this via a using declaration, use the resolved qualified name
				// Otherwise, check if this global has a mangled name (e.g., anonymous namespace variable)
				if (resolved_qualified_name.has_value()) {
					op.global_name = *resolved_qualified_name;
				} else {
					// Phase 4: Using StringHandle for lookup
					StringHandle simple_name_handle = StringTable::getOrInternStringHandle(identifierNode.name());
					auto it = global_variable_names_.find(simple_name_handle);
					if (it != global_variable_names_.end()) {
						op.global_name = it->second;  // Use mangled StringHandle
					} else {
						op.global_name = StringTable::getOrInternStringHandle(identifierNode.name());  // Use simple name as StringHandle
					}
				}
				
				op.is_array = is_array_type;  // Arrays need LEA to get address
				StringHandle saved_global_name = op.global_name;  // save before move
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

				// Register Global lvalue metadata so compound assignments (+=, -=, etc.) can write back
				if (!is_array_type) {
					setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(
						LValueInfo(LValueInfo::Kind::Global, saved_global_name),
						type_node.type(), size_bits));
				}

				// Return the temp variable that will hold the loaded value
				// Include type_index for struct types
				TypeIndex type_index = (type_node.type() == Type::Struct) ? type_node.type_index() : 0;
				return { type_node.type(), size_bits, result_temp, static_cast<unsigned long long>(type_index) };
			} else {
				// This is a local variable
				
				// Check if this is a reference variable - if so, we need to dereference it
				// Reference variables (both lvalue & and rvalue &&) hold an address, and we need to load the value from that address
				// EXCEPT for array references, where the reference IS the array pointer
				if (type_node.is_reference()) {
					FLASH_LOG_FORMAT(Codegen, Debug, "VariableDecl reference '{}': context={}", 
						identifierNode.name(), context == ExpressionContext::LValueAddress ? "LValueAddress" : "Load");
					
					// For references to arrays (e.g., int (&arr)[3]), the reference variable
					// already holds the array address directly. We don't dereference it.
					// Just return it as a pointer (64 bits on x64 architecture).
					if (type_node.is_array()) {
						// Return the array reference as a 64-bit pointer
						return { type_node.type(), POINTER_SIZE_BITS, StringTable::getOrInternStringHandle(identifierNode.name()), 0ULL };
					}
					
					// For LValueAddress context (assignment LHS), we need to treat the reference variable
					// as an indirect lvalue (pointer that needs dereferencing for stores)
					if (context == ExpressionContext::LValueAddress) {
						FLASH_LOG_FORMAT(Codegen, Debug, "VariableDecl reference '{}': Creating addr_temp for LValueAddress", identifierNode.name());
						// For auto types, default to int (32 bits)
						Type pointee_type = type_node.type();
						int pointee_size = static_cast<int>(type_node.size_in_bits());
						if (pointee_type == Type::Auto || pointee_size == 0) {
							pointee_type = Type::Int;
							pointee_size = 32;
						}
						
						// The reference variable holds a pointer address
						// We need to load it into a temp and mark it with Indirect LValue metadata
						TempVar addr_temp = var_counter.next();
						StringHandle var_handle = StringTable::getOrInternStringHandle(identifierNode.name());
						
						// Use AssignmentOp to copy the pointer value to a temp
						AssignmentOp assign_op;
						assign_op.result = addr_temp;
						assign_op.lhs = TypedValue{pointee_type, 64, addr_temp};  // 64-bit pointer dest
						assign_op.rhs = TypedValue{pointee_type, 64, var_handle};  // 64-bit pointer source
						assign_op.is_pointer_store = false;
						assign_op.dereference_rhs_references = false;  // Don't dereference - just copy the pointer!
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
						
						// Mark the temp with Indirect LValue metadata
						// This tells the assignment handler to use DereferenceStore
						LValueInfo lvalue_info(
							LValueInfo::Kind::Indirect,
							addr_temp,  // The temp holding the pointer address
							0  // offset is 0 for dereference
						);
						setTempVarMetadata(addr_temp, TempVarMetadata::makeLValue(lvalue_info));
						
						TypeIndex type_index = (pointee_type == Type::Struct) ? type_node.type_index() : 0;
						return { pointee_type, pointee_size, addr_temp, static_cast<unsigned long long>(type_index) };
					}
					
					// For Load context (reading the value), dereference to get the value
					
					// For auto types, default to int (32 bits) since the mangling also defaults to int
					// This matches the behavior in NameMangling.h which falls through to 'H' (int)
					Type pointee_type = type_node.type();
					int pointee_size = static_cast<int>(type_node.size_in_bits());
					if (pointee_type == Type::Auto || pointee_size == 0) {
						pointee_type = Type::Int;
						pointee_size = 32;
					}
					
					int ptr_depth = type_node.pointer_depth() > 0 ? type_node.pointer_depth() : 1;
					TempVar result_temp = emitDereference(pointee_type, 64, ptr_depth,
						StringTable::getOrInternStringHandle(identifierNode.name()));
					
					// Mark as lvalue with Indirect metadata for unified assignment handler
					// This allows compound assignments (like x *= 2) to work on dereferenced references
					LValueInfo lvalue_info(
						LValueInfo::Kind::Indirect,
						StringTable::getOrInternStringHandle(identifierNode.name()),  // The reference variable name
						0  // offset is 0 for simple dereference
					);
					setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
					
					TypeIndex type_index = (pointee_type == Type::Struct) ? type_node.type_index() : 0;
					return { pointee_type, pointee_size, result_temp, static_cast<unsigned long long>(type_index) };
				}
				
				// Regular local variable (not a reference) - return variable name
				// Use helper function to calculate size_bits with proper fallback handling
				int size_bits = calculateIdentifierSizeBits(type_node, decl_node.is_array(), identifierNode.name());
				
				// For the 4th element: 
				// - For struct types, ALWAYS return type_index (even if it's a pointer to struct)
				// - For non-struct pointer types, return pointer_depth
				// - Otherwise return 0
				unsigned long long fourth_element = (type_node.type() == Type::Struct)
					? static_cast<unsigned long long>(type_node.type_index())
					: ((type_node.pointer_depth() > 0) ? static_cast<unsigned long long>(type_node.pointer_depth()) : 0ULL);
				return { type_node.type(), size_bits, StringTable::getOrInternStringHandle(identifierNode.name()), fourth_element };
			}
		}
		
		// Check if it's a FunctionDeclarationNode (function name used as value)
		if (symbol->is<FunctionDeclarationNode>()) {
			// This is a function name being used as a value (e.g., fp = add)
			// Generate FunctionAddress IR instruction
			const auto& func_decl = symbol->as<FunctionDeclarationNode>();
			
			// Compute mangled name from the function declaration
			const auto& return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
			std::vector<TypeSpecifierNode> param_types;
			for (const auto& param : func_decl.parameter_nodes()) {
				if (param.is<DeclarationNode>()) {
					param_types.push_back(param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>());
				}
			}
			std::string_view mangled = generateMangledNameForCall(
				identifierNode.name(), return_type, param_types, func_decl.is_variadic(), "");
			
			TempVar func_addr_var = var_counter.next();
			FunctionAddressOp op;
			op.result.type = Type::FunctionPointer;
			op.result.size_in_bits = 64;
			op.result.value = func_addr_var;
			op.function_name = StringTable::getOrInternStringHandle(identifierNode.name());
			op.mangled_name = StringTable::getOrInternStringHandle(mangled);
			ir_.addInstruction(IrInstruction(IrOpcode::FunctionAddress, std::move(op), Token()));

			// Return the function address as a pointer (64 bits)
			return { Type::FunctionPointer, 64, func_addr_var, 0ULL };
		}

		// Check if it's a TemplateVariableDeclarationNode (variable template)
		if (symbol->is<TemplateVariableDeclarationNode>()) {
			// Variable template without instantiation - should not reach codegen
			// The parser should have instantiated it already
			throw InternalError("Uninstantiated variable template in codegen");
			return {};
		}

		// If we get here, the symbol is not a known type
		FLASH_LOG(Codegen, Error, "Unknown symbol type for identifier '", identifierNode.name(), "'");
		throw InternalError("Identifier is not a DeclarationNode");
		return {};
	}

	std::vector<IrOperand> AstToIr::generateQualifiedIdentifierIr(const QualifiedIdentifierNode& qualifiedIdNode) {
		// Check if this is a scoped enum value (e.g., Direction::North)
		NamespaceHandle ns_handle = qualifiedIdNode.namespace_handle();
		if (!ns_handle.isGlobal()) {
			// The struct/enum name is the last namespace component (the name of the namespace handle)
			std::string_view struct_or_enum_name = gNamespaceRegistry.getName(ns_handle);
			
			// Could be EnumName::EnumeratorName
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_or_enum_name));
			if (type_it != gTypesByName.end() && type_it->second->isEnum()) {
				const EnumTypeInfo* enum_info = type_it->second->getEnumInfo();
				if (enum_info && enum_info->is_scoped) {
					// This is a scoped enum - look up the enumerator value
					long long enum_value = enum_info->getEnumeratorValue(StringTable::getOrInternStringHandle(qualifiedIdNode.name()));
					// Return the enum value as a constant
					return { enum_info->underlying_type, static_cast<int>(enum_info->underlying_size),
					static_cast<unsigned long long>(enum_value) };
				}
			}

			// Check if this is a static member access (e.g., StructName::static_member or ns::StructName::static_member)
			// For nested types (depth > 1), try fully qualified name FIRST to avoid ambiguity
			// This handles member template specializations like MakeUnsigned::List_int_char
			auto struct_type_it = gTypesByName.end();
			
			if (gNamespaceRegistry.getDepth(ns_handle) > 1) {
				StringHandle ns_qualified_handle = gNamespaceRegistry.getQualifiedNameHandle(ns_handle);
				std::string_view full_qualified_name = StringTable::getStringView(ns_qualified_handle);
				
				// First try with the namespace handle directly
				struct_type_it = gTypesByName.find(ns_qualified_handle);
				if (struct_type_it != gTypesByName.end()) {
					struct_or_enum_name = full_qualified_name;
					FLASH_LOG(Codegen, Debug, "Found struct with full qualified name: ", full_qualified_name);
				} else {
					// Fallback: search by string content
					// This handles cases where the type was registered with a different StringHandle
					// but has the same string content (e.g., type aliases in templates)
					for (const auto& [key, val] : gTypesByName) {
						std::string_view key_str = StringTable::getStringView(key);
						if (key_str == full_qualified_name) {
							struct_type_it = gTypesByName.find(key);
							if (struct_type_it != gTypesByName.end()) {
								struct_or_enum_name = key_str;
								FLASH_LOG(Codegen, Debug, "Found struct by string content: ", full_qualified_name);
							}
							break;
						}
					}
				}
			}
			
			// If not found with fully qualified name, try simple name
			if (struct_type_it == gTypesByName.end()) {
				struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_or_enum_name));
				FLASH_LOG(Codegen, Debug, "generateQualifiedIdentifierIr: struct_or_enum_name='", struct_or_enum_name, "', found=", (struct_type_it != gTypesByName.end()));
			}
			
			// If not found directly, search for template instantiation using TypeInfo metadata
			// This handles cases like has_type<T>::value where T has a default = void argument
			// Uses TypeInfo::baseTemplateName() for deterministic lookup instead of prefix scanning
			// Selection is deterministic by choosing the instantiation with the smallest type_index_
			if (struct_type_it == gTypesByName.end()) {
				// Use TypeInfo metadata to find instantiation with matching base template name
				// We select deterministically by choosing the smallest type_index_ among matches
				StringHandle base_name_handle = StringTable::getOrInternStringHandle(struct_or_enum_name);
				TypeIndex best_type_index = std::numeric_limits<TypeIndex>::max();
				for (auto it = gTypesByName.begin(); it != gTypesByName.end(); ++it) {
					if (it->second->isStruct() && it->second->isTemplateInstantiation()) {
						// Use TypeInfo metadata for matching
						if (it->second->baseTemplateName() == base_name_handle) {
							// Deterministic selection: prefer smallest type_index_
							if (it->second->type_index_ < best_type_index) {
								best_type_index = it->second->type_index_;
								struct_type_it = it;
								FLASH_LOG(Codegen, Debug, "Found struct via TypeInfo metadata: baseTemplate=", 
									struct_or_enum_name, " -> ", StringTable::getStringView(it->first),
									" (type_index=", it->second->type_index_, ")");
							}
						}
					}
				}
			}
			
			// Fallback: try old-style _void suffix for backward compatibility with legacy code
			if (struct_type_it == gTypesByName.end()) {
				std::string_view struct_name_with_void = StringBuilder().append(struct_or_enum_name).append("_void"sv).commit();
				struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name_with_void));
				if (struct_type_it != gTypesByName.end()) {
					FLASH_LOG(Codegen, Debug, "Found struct with _void suffix: ", struct_name_with_void);
				}
			}
			
			if (struct_type_it != gTypesByName.end() && struct_type_it->second->isStruct()) {
				const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
				// If struct_info is null, this might be a type alias - resolve it via type_index
				if (!struct_info && struct_type_it->second->type_index_ < gTypeInfo.size()) {
					const TypeInfo* resolved_type = &gTypeInfo[struct_type_it->second->type_index_];
					if (resolved_type && resolved_type->isStruct()) {
						struct_info = resolved_type->getStructInfo();
					}
				}
				if (struct_info) {
					FLASH_LOG(Codegen, Debug, "Looking for static member '", qualifiedIdNode.name(), "' in struct '", struct_or_enum_name, "'");
					// Look for static member recursively (checks base classes too)
					auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(StringTable::getOrInternStringHandle(qualifiedIdNode.name()));
					FLASH_LOG(Codegen, Debug, "findStaticMemberRecursive result: static_member=", (static_member != nullptr), ", owner_struct=", (owner_struct != nullptr));
					if (static_member && owner_struct) {
						// Check if the owner struct is an incomplete template instantiation
						auto owner_type_it = gTypesByName.find(owner_struct->getName());
						if (owner_type_it != gTypesByName.end() && owner_type_it->second->is_incomplete_instantiation_) {
							std::string_view owner_name = StringTable::getStringView(owner_struct->getName());
							FLASH_LOG(Codegen, Error, "Cannot access static member '", qualifiedIdNode.name(), 
							"' from incomplete template instantiation '", owner_name, "'");
							// Return a placeholder value instead of generating GlobalLoad
							// This prevents linker errors from undefined references to incomplete instantiations
							return { Type::Bool, 8, 0ULL, 0ULL };
						}
						
						// Determine the correct qualified name to use
						// If we accessed through a type alias (struct_type_it->second) that resolves to
						// a different struct than the owner, we should use the resolved struct name
						StringHandle qualified_struct_name = owner_struct->getName();
						
						// Check if we're accessing through a type alias by comparing names
						if (struct_type_it->second->name() != owner_struct->getName()) {
							// Accessing through type alias or derived class
							// First, check if this is inheritance (owner_struct is a base class of accessed struct)
							// In that case, we should use owner_struct's name directly, not do type alias resolution
							bool is_inheritance = false;
							const StructTypeInfo* accessed_struct = struct_type_it->second->getStructInfo();
							if (accessed_struct) {
								for (const auto& base : accessed_struct->base_classes) {
									if (base.type_index < gTypeInfo.size()) {
										const TypeInfo& base_type = gTypeInfo[base.type_index];
										const StructTypeInfo* base_struct = base_type.getStructInfo();
										if (base_struct && base_struct->getName() == owner_struct->getName()) {
											is_inheritance = true;
											FLASH_LOG(Codegen, Debug, "Static member found via inheritance from base class: ", owner_struct->getName());
											break;
										}
									}
								}
							}
							
							// Skip type alias resolution for inheritance - use owner_struct's name directly
							if (!is_inheritance) {
								// Try to resolve to the actual instantiated type
								const TypeInfo* resolved_type = struct_type_it->second;
								
								// Special handling for true_type and false_type
								// These should resolve to integral_constant<bool, 1> and integral_constant<bool, 0>
								// but the template system doesn't instantiate them properly
								std::string_view alias_name = StringTable::getStringView(resolved_type->name());
								if (alias_name == "true_type" || alias_name == "false_type") {
									// Generate the value directly without needing a static member
									// true_type -> 1, false_type -> 0
									bool value = (alias_name == "true_type") ? true : false;
									FLASH_LOG(Codegen, Debug, "Special handling for ", alias_name, " -> value=", value);
									return { Type::Bool, 8, static_cast<unsigned long long>(value), 0ULL };
								}
								
								// Follow the full type alias chain (e.g., true_type -> bool_constant -> integral_constant)
								std::unordered_set<TypeIndex> visited;
								while (resolved_type && 
								resolved_type->type_index_ < gTypeInfo.size() && 
								resolved_type->type_index_ != 0 &&
								!visited.contains(resolved_type->type_index_)) {
									visited.insert(resolved_type->type_index_);
									const TypeInfo* target_type = &gTypeInfo[resolved_type->type_index_];
									
									if (target_type && target_type->isStruct() && target_type->getStructInfo()) {
										// Use the target struct's name
										qualified_struct_name = target_type->name();
										FLASH_LOG(Codegen, Debug, "Resolved type alias to: ", qualified_struct_name);
										
										// If target is also an alias, continue following
										if (target_type->type_index_ != 0 && target_type->type_index_ != resolved_type->type_index_) {
											resolved_type = target_type;
										} else {
											break;
										}
									} else {
										break;
									}
								}
								
								// If still resolving to a primary template (no template args in name),
								// try to find a properly instantiated version by checking emitted static members
								std::string_view owner_name_str = StringTable::getStringView(qualified_struct_name);
								bool looks_like_primary_template = 
									(owner_name_str.find('_') == std::string_view::npos || 
									owner_name_str == StringTable::getStringView(owner_struct->getName()));
								
								if (looks_like_primary_template) {
									// Search for an instantiated version that has this static member
									std::string search_suffix = std::string("::") + std::string(StringTable::getStringView(StringTable::getOrInternStringHandle(qualifiedIdNode.name())));
									for (const auto& emitted_handle : emitted_static_members_) {
										std::string_view emitted = StringTable::getStringView(emitted_handle);
										if (emitted.find(search_suffix) != std::string::npos &&
										emitted.find(std::string(owner_name_str) + "_") == 0) {
											// Found an instantiated version - extract the struct name
											size_t colon_pos = emitted.find("::");
											if (colon_pos != std::string::npos) {
												std::string inst_name = std::string(emitted.substr(0, colon_pos));
												qualified_struct_name = StringTable::getOrInternStringHandle(inst_name);
												FLASH_LOG(Codegen, Debug, "Using instantiated version: ", inst_name, " instead of primary template");
												break;
											}
										}
									}
								}
							}
						}
						
						// This is a static member access - generate GlobalLoad
						FLASH_LOG(Codegen, Debug, "Found static member in owner struct: ", owner_struct->getName(), ", using qualified name with: ", qualified_struct_name);
						int qsm_size_bits = static_cast<int>(static_member->size * 8);
						// If size is 0 for struct types, look up from type info
						if (qsm_size_bits == 0 && static_member->type_index > 0 && static_member->type_index < gTypeInfo.size()) {
							const StructTypeInfo* qsm_si = gTypeInfo[static_member->type_index].getStructInfo();
							if (qsm_si) {
								qsm_size_bits = static_cast<int>(qsm_si->total_size * 8);
							}
						}
						TempVar result_temp = var_counter.next();
						GlobalLoadOp op;
						op.result.type = static_member->type;
						op.result.size_in_bits = qsm_size_bits;
						op.result.value = result_temp;
						// Use qualified name as the global symbol name: StructName::static_member
						op.global_name = StringTable::getOrInternStringHandle(StringBuilder().append(qualified_struct_name).append("::"sv).append(qualifiedIdNode.name()));
						ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

						// For reference members, the global holds a pointer — dereference it
						if (static_member->is_reference()) {
							TempVar deref_temp = var_counter.next();
							DereferenceOp deref_op;
							deref_op.result = deref_temp;
							deref_op.pointer.type = static_member->type;
							deref_op.pointer.size_in_bits = get_type_size_bits(static_member->type);
							deref_op.pointer.pointer_depth = 1;
							deref_op.pointer.value = result_temp;
							ir_.addInstruction(IrInstruction(IrOpcode::Dereference, deref_op, Token()));
							TypeIndex type_index = (static_member->type == Type::Struct) ? static_member->type_index : 0;
							return { static_member->type, get_type_size_bits(static_member->type), deref_temp, static_cast<unsigned long long>(type_index) };
						}

						// Return the temp variable that will hold the loaded value
						TypeIndex type_index = (static_member->type == Type::Struct) ? static_member->type_index : 0;
						return { static_member->type, qsm_size_bits, result_temp, static_cast<unsigned long long>(type_index) };
					}
				}
			}
		}

		// Look up the qualified identifier in the symbol table
		const std::optional<ASTNode> symbol = symbol_table.lookup_qualified(qualifiedIdNode.qualifiedIdentifier());

		// Also try global symbol table for namespace-qualified globals
		std::optional<ASTNode> global_symbol;
		if (!symbol.has_value() && global_symbol_table_) {
			global_symbol = global_symbol_table_->lookup_qualified(qualifiedIdNode.qualifiedIdentifier());
		}

		const std::optional<ASTNode>& found_symbol = symbol.has_value() ? symbol : global_symbol;

		if (!found_symbol.has_value()) {
			// For external functions (like std::print), we might not have them in our symbol table
			// Return a placeholder - the actual linking will happen later
			return { Type::Int, 32,  StringTable::getOrInternStringHandle(qualifiedIdNode.name()), 0ULL };
		}

		if (found_symbol->is<DeclarationNode>()) {
			const auto& decl_node = found_symbol->as<DeclarationNode>();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// Check if this is a global variable (namespace-scoped)
			// If found in global symbol table, it's a global variable
			bool is_global = global_symbol.has_value();

			if (is_global) {
				// Generate GlobalLoad for namespace-qualified global variable
				TempVar result_temp = var_counter.next();
				GlobalLoadOp op;
				op.result.type = type_node.type();
				op.result.size_in_bits = static_cast<int>(type_node.size_in_bits());
				op.result.value = result_temp;
				// Use fully qualified name (ns::value) to match the global variable symbol
				op.global_name = gNamespaceRegistry.buildQualifiedIdentifier(
					qualifiedIdNode.namespace_handle(),
					StringTable::getOrInternStringHandle(qualifiedIdNode.name()));
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

				// Return the temp variable that will hold the loaded value
				TypeIndex type_index = (type_node.type() == Type::Struct) ? type_node.type_index() : 0;
				return { type_node.type(), static_cast<int>(type_node.size_in_bits()), result_temp, static_cast<unsigned long long>(type_index) };
			} else {
				// Local variable - just return the name
				TypeIndex type_index = (type_node.type() == Type::Struct) ? type_node.type_index() : 0;
				return { type_node.type(), static_cast<int>(type_node.size_in_bits()),  StringTable::getOrInternStringHandle(qualifiedIdNode.name()), static_cast<unsigned long long>(type_index) };
			}
		}

		if (found_symbol->is<VariableDeclarationNode>()) {
			const auto& var_decl_node = found_symbol->as<VariableDeclarationNode>();
			const auto& decl_node = var_decl_node.declaration_node().as<DeclarationNode>();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// Namespace-scoped variables are always global
			// Generate GlobalLoad for namespace-qualified global variable
			TempVar result_temp = var_counter.next();
			int size_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
			GlobalLoadOp op;
			op.result.type = type_node.type();
			op.result.size_in_bits = size_bits;
			op.result.value = result_temp;
			// Use fully qualified name (ns::value) to match the global variable symbol
			op.global_name = gNamespaceRegistry.buildQualifiedIdentifier(
				qualifiedIdNode.namespace_handle(),
				StringTable::getOrInternStringHandle(qualifiedIdNode.name()));
			ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

			// Return the temp variable that will hold the loaded value
			// For pointers, return 64 bits (pointer size)
			TypeIndex type_index = (type_node.type() == Type::Struct) ? type_node.type_index() : 0;
			return { type_node.type(), size_bits, result_temp, static_cast<unsigned long long>(type_index) };
		}

		if (found_symbol->is<FunctionDeclarationNode>()) {
			// This is a function - just return the name for function calls
			// The actual function call handling is done elsewhere
			return { Type::Function, 64, StringTable::getOrInternStringHandle(qualifiedIdNode.name()), 0ULL };
		}

		// If we get here, the symbol is not a supported type
		throw InternalError("Qualified identifier is not a supported type");
		return {};
	}

	std::vector<IrOperand>
		AstToIr::generateNumericLiteralIr(const NumericLiteralNode& numericLiteralNode) {
		// Generate IR for numeric literal using the actual type from the literal
		// Check if it's a floating-point type
		if (is_floating_point_type(numericLiteralNode.type())) {
			// For floating-point literals, the value is stored as double
			return { numericLiteralNode.type(), static_cast<int>(numericLiteralNode.sizeInBits()), std::get<double>(numericLiteralNode.value()), 0ULL };
		} else {
			// For integer literals, the value is stored as unsigned long long
			return { numericLiteralNode.type(), static_cast<int>(numericLiteralNode.sizeInBits()), std::get<unsigned long long>(numericLiteralNode.value()), 0ULL };
		}
	}

