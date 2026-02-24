	std::vector<IrOperand> visitExpressionNode(const ExpressionNode& exprNode, 
	                                            ExpressionContext context = ExpressionContext::Load) {
		if (std::holds_alternative<IdentifierNode>(exprNode)) {
			const auto& expr = std::get<IdentifierNode>(exprNode);
			return generateIdentifierIr(expr, context);
		}
		else if (std::holds_alternative<QualifiedIdentifierNode>(exprNode)) {
			const auto& expr = std::get<QualifiedIdentifierNode>(exprNode);
			return generateQualifiedIdentifierIr(expr);
		}
		else if (std::holds_alternative<BoolLiteralNode>(exprNode)) {
			const auto& expr = std::get<BoolLiteralNode>(exprNode);
			// Convert boolean to integer for IR (true=1, false=0)
			// Return format: [type, size_in_bits, value, 0ULL]
			return { Type::Bool, 8, expr.value() ? 1ULL : 0ULL, 0ULL };
		}
		else if (std::holds_alternative<NumericLiteralNode>(exprNode)) {
			const auto& expr = std::get<NumericLiteralNode>(exprNode);
			return generateNumericLiteralIr(expr);
		}
		else if (std::holds_alternative<StringLiteralNode>(exprNode)) {
			const auto& expr = std::get<StringLiteralNode>(exprNode);
			return generateStringLiteralIr(expr);
		}
		else if (std::holds_alternative<BinaryOperatorNode>(exprNode)) {
			const auto& expr = std::get<BinaryOperatorNode>(exprNode);
			return generateBinaryOperatorIr(expr);
		}
		else if (std::holds_alternative<UnaryOperatorNode>(exprNode)) {
			const auto& expr = std::get<UnaryOperatorNode>(exprNode);
			return generateUnaryOperatorIr(expr, context);
		}
		else if (std::holds_alternative<TernaryOperatorNode>(exprNode)) {
			const auto& expr = std::get<TernaryOperatorNode>(exprNode);
			return generateTernaryOperatorIr(expr);
		}
		else if (std::holds_alternative<FunctionCallNode>(exprNode)) {
			const auto& expr = std::get<FunctionCallNode>(exprNode);
			return generateFunctionCallIr(expr);
		}
		else if (std::holds_alternative<MemberFunctionCallNode>(exprNode)) {
			const auto& expr = std::get<MemberFunctionCallNode>(exprNode);
			return generateMemberFunctionCallIr(expr);
		}
		else if (std::holds_alternative<ArraySubscriptNode>(exprNode)) {
			const auto& expr = std::get<ArraySubscriptNode>(exprNode);
			return generateArraySubscriptIr(expr, context);
		}
		else if (std::holds_alternative<MemberAccessNode>(exprNode)) {
			const auto& expr = std::get<MemberAccessNode>(exprNode);
			return generateMemberAccessIr(expr, context);
		}
		else if (std::holds_alternative<SizeofExprNode>(exprNode)) {
			const auto& sizeof_node = std::get<SizeofExprNode>(exprNode);
			
			// Try to evaluate as a constant expression first
			auto const_result = tryEvaluateAsConstExpr(sizeof_node);
			if (!const_result.empty()) {
				return const_result;
			}
			
			// Fall back to IR generation if constant evaluation failed
			return generateSizeofIr(sizeof_node);
		}
		else if (std::holds_alternative<SizeofPackNode>(exprNode)) {
			[[maybe_unused]] const auto& sizeof_pack_expr = std::get<SizeofPackNode>(exprNode);
			// sizeof... should have been replaced with a constant during template instantiation
			// If we reach here, it means sizeof... wasn't properly substituted
			// This is an error - sizeof... can only appear in template contexts
			FLASH_LOG(Codegen, Error, "sizeof... operator found during code generation - should have been substituted during template instantiation");
			return {};
		}
		else if (std::holds_alternative<AlignofExprNode>(exprNode)) {
			const auto& alignof_node = std::get<AlignofExprNode>(exprNode);
			
			// Try to evaluate as a constant expression first
			auto const_result = tryEvaluateAsConstExpr(alignof_node);
			if (!const_result.empty()) {
				return const_result;
			}
			
			// Fall back to IR generation if constant evaluation failed
			return generateAlignofIr(alignof_node);
		}
		else if (std::holds_alternative<NoexceptExprNode>(exprNode)) {
			const auto& noexcept_node = std::get<NoexceptExprNode>(exprNode);
			// noexcept(expr) returns true if expr doesn't throw, false otherwise
			// Analyze the expression to determine if it can throw
			bool is_noexcept = true;  // Default assumption
			
			if (noexcept_node.expr().is<ExpressionNode>()) {
				is_noexcept = isExpressionNoexcept(noexcept_node.expr().as<ExpressionNode>());
			}
			
			// Return a compile-time constant boolean
			return { Type::Bool, 8, is_noexcept ? 1ULL : 0ULL, 0ULL };
		}
		else if (std::holds_alternative<OffsetofExprNode>(exprNode)) {
			const auto& expr = std::get<OffsetofExprNode>(exprNode);
			return generateOffsetofIr(expr);
		}
		else if (std::holds_alternative<TypeTraitExprNode>(exprNode)) {
			const auto& expr = std::get<TypeTraitExprNode>(exprNode);
			return generateTypeTraitIr(expr);
		}
		else if (std::holds_alternative<NewExpressionNode>(exprNode)) {
			const auto& expr = std::get<NewExpressionNode>(exprNode);
			return generateNewExpressionIr(expr);
		}
		else if (std::holds_alternative<DeleteExpressionNode>(exprNode)) {
			const auto& expr = std::get<DeleteExpressionNode>(exprNode);
			return generateDeleteExpressionIr(expr);
		}
		else if (std::holds_alternative<StaticCastNode>(exprNode)) {
			const auto& expr = std::get<StaticCastNode>(exprNode);
			return generateStaticCastIr(expr);
		}
		else if (std::holds_alternative<DynamicCastNode>(exprNode)) {
			const auto& expr = std::get<DynamicCastNode>(exprNode);
			return generateDynamicCastIr(expr);
		}
		else if (std::holds_alternative<ConstCastNode>(exprNode)) {
			const auto& expr = std::get<ConstCastNode>(exprNode);
			return generateConstCastIr(expr);
		}
		else if (std::holds_alternative<ReinterpretCastNode>(exprNode)) {
			const auto& expr = std::get<ReinterpretCastNode>(exprNode);
			return generateReinterpretCastIr(expr);
		}
		else if (std::holds_alternative<TypeidNode>(exprNode)) {
			const auto& expr = std::get<TypeidNode>(exprNode);
			return generateTypeidIr(expr);
		}
		else if (std::holds_alternative<LambdaExpressionNode>(exprNode)) {
			const auto& expr = std::get<LambdaExpressionNode>(exprNode);
			return generateLambdaExpressionIr(expr);
		}
		else if (std::holds_alternative<ConstructorCallNode>(exprNode)) {
			const auto& expr = std::get<ConstructorCallNode>(exprNode);
			return generateConstructorCallIr(expr);
		}
		else if (std::holds_alternative<TemplateParameterReferenceNode>(exprNode)) {
			const auto& expr = std::get<TemplateParameterReferenceNode>(exprNode);
			return generateTemplateParameterReferenceIr(expr);
		}
		else if (std::holds_alternative<FoldExpressionNode>(exprNode)) {
			// Fold expressions should have been expanded during template instantiation
			// If we reach here, it means the fold wasn't properly substituted
			FLASH_LOG(Codegen, Error, "Fold expression found during code generation - should have been expanded during template instantiation");
			return {};
		}
		else if (std::holds_alternative<PseudoDestructorCallNode>(exprNode)) {
			// Explicit destructor call: obj.~Type() or ptr->~Type()
			// For class types, this calls the destructor
			// For non-class types (like int), this is a no-op
			const auto& dtor = std::get<PseudoDestructorCallNode>(exprNode);
			std::string_view type_name = dtor.has_qualified_name() 
				? std::string_view(dtor.qualified_type_name()) 
				: dtor.type_name();
			FLASH_LOG(Codegen, Debug, "Generating explicit destructor call for type: ", type_name);
			
			// Get the object expression
			ASTNode object_node = dtor.object();
			
			// Try to determine if this is a struct type that needs destructor call
			std::string_view object_name;
			const DeclarationNode* object_decl = nullptr;
			TypeSpecifierNode object_type(Type::Void, TypeQualifier::None, 0);
			
			if (object_node.is<ExpressionNode>()) {
				const ExpressionNode& object_expr = object_node.as<ExpressionNode>();
				
				if (std::holds_alternative<IdentifierNode>(object_expr)) {
					const IdentifierNode& object_ident = std::get<IdentifierNode>(object_expr);
					object_name = object_ident.name();
					
					// Look up the object in symbol table
					const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
					if (symbol.has_value()) {
						object_decl = get_decl_from_symbol(*symbol);
						if (object_decl) {
							object_type = object_decl->type_node().as<TypeSpecifierNode>();
							
							// Handle arrow access (ptr->~Type)
							if (dtor.is_arrow_access() && object_type.pointer_levels().size() > 0) {
								object_type.remove_pointer_level();
							}
						}
					}
				}
			}
			
			// Only generate destructor call for struct types
			if (is_struct_type(object_type.type())) {
				size_t struct_type_index = object_type.type_index();
				if (struct_type_index > 0 && struct_type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[struct_type_index];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					
					// Check if struct has a destructor
					if (struct_info && struct_info->hasDestructor()) {
						FLASH_LOG(Codegen, Debug, "Generating IR for destructor call on struct: ", 
						         StringTable::getStringView(struct_info->getName()));
						
						// Generate destructor call IR
						DestructorCallOp dtor_op;
						dtor_op.struct_name = struct_info->getName();
						dtor_op.object = StringTable::getOrInternStringHandle(object_name);
						ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), dtor.type_name_token()));
					} else {
						FLASH_LOG(Codegen, Debug, "Struct ", type_name, " has no destructor, skipping call");
					}
				}
			} else {
				// For non-class types (int, etc.), explicit destructor call is a no-op
				FLASH_LOG(Codegen, Debug, "Non-class type ", type_name, " - destructor call is no-op");
			}
			
			// Destructor calls return void
			return {};
		}
		else if (std::holds_alternative<PointerToMemberAccessNode>(exprNode)) {
			// Pointer-to-member operator: obj.*ptr or obj->*ptr
			// This accesses a member through a pointer-to-member offset
			const PointerToMemberAccessNode& ptmNode = std::get<PointerToMemberAccessNode>(exprNode);
			
			// Visit the object expression (LHS)
			auto object_operands = visitExpressionNode(ptmNode.object().as<ExpressionNode>(), ExpressionContext::LValueAddress);
			if (object_operands.empty()) {
				FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: object expression returned empty operands");
				return {};
			}
			
			// Visit the member pointer expression (RHS) - this should be the offset
			auto ptr_operands = visitExpressionNode(ptmNode.member_pointer().as<ExpressionNode>());
			if (ptr_operands.empty()) {
				FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: member pointer expression returned empty operands");
				return {};
			}
			
			// Get object base address
			TempVar object_addr = var_counter.next();
			if (ptmNode.is_arrow()) {
				// For ->*, object is a pointer - use it as the address
				if (std::holds_alternative<StringHandle>(object_operands[2])) {
					// Object is a named pointer variable - its value is the address we need
					// Use Assignment to load it into a temp var
					StringHandle obj_ptr_name = std::get<StringHandle>(object_operands[2]);
					AssignmentOp assign_op;
					assign_op.result = object_addr;
					assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, object_addr};
					assign_op.rhs = TypedValue{Type::UnsignedLongLong, 64, obj_ptr_name};
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
				} else if (std::holds_alternative<TempVar>(object_operands[2])) {
					// Object is already a temp var containing the address
					object_addr = std::get<TempVar>(object_operands[2]);
				} else {
					FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: unexpected object operand type for ->*");
					return {};
				}
			} else {
				// For .*, object is a value - take its address
				// If object is in LValueAddress context, object_operands[2] might be the address
				if (std::holds_alternative<StringHandle>(object_operands[2])) {
					// Object is a named variable - compute its address
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
					// Object is a temp var - might already be an address or need address-of
					object_addr = std::get<TempVar>(object_operands[2]);
				} else {
					FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: unexpected object operand type for .*");
					return {};
				}
			}
			
			// Validate ptr_operands before using
			if (ptr_operands.size() < 2) {
				FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: member pointer operands incomplete (size=", ptr_operands.size(), ")");
				return {};
			}
			
			// Add the offset to the object address
			TempVar member_addr = var_counter.next();
			BinaryOp add_op;
			add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, object_addr};
			add_op.rhs = toTypedValue(ptr_operands); // The offset value
			add_op.result = member_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), ptmNode.operator_token()));
			
			// Dereference to get the member value
			// The member type should be in ptr_operands[0]
			Type member_type = std::get<Type>(ptr_operands[0]);
			int member_size = std::get<int>(ptr_operands[1]);
			TypeIndex member_type_index = 0;
			if (ptr_operands.size() >= 4 && std::holds_alternative<unsigned long long>(ptr_operands[3])) {
				member_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(ptr_operands[3]));
			}
			
			TempVar result_var = var_counter.next();
			DereferenceOp deref_op;
			deref_op.result = result_var;
			deref_op.pointer = TypedValue{
				.type = member_type,
				.size_in_bits = member_size,
				.value = member_addr,
				.pointer_depth = 1  // We're dereferencing a pointer
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), ptmNode.operator_token()));
			
			// Return the dereferenced member value
			return { member_type, member_size, result_var, static_cast<unsigned long long>(member_type_index) };
		}
		else if (std::holds_alternative<PackExpansionExprNode>(exprNode)) {
			// Pack expansion: expr...
			// Should have been expanded during template instantiation
			// If we reach here, it means the pack wasn't properly substituted
			FLASH_LOG(Codegen, Error, "PackExpansionExprNode found during code generation - should have been expanded during template instantiation");
			return {};
		}
		else if (std::holds_alternative<InitializerListConstructionNode>(exprNode)) {
			// Compiler-generated initializer_list construction
			// This is the "compiler magic" for std::initializer_list
			const auto& init_list = std::get<InitializerListConstructionNode>(exprNode);
			return generateInitializerListConstructionIr(init_list);
		}
		else if (std::holds_alternative<ThrowExpressionNode>(exprNode)) {
			// Throw expression - like ThrowStatementNode but appears in expression context
			// For now, just skip code generation since throw expressions have type void
			// and their main effect is control flow which isn't compiled yet
			FLASH_LOG(Codegen, Debug, "ThrowExpressionNode encountered in expression context - skipping codegen");
			return {};
		}
		else {
			throw std::runtime_error("Not implemented yet");
		}
		return {};
	}

	// Helper function to calculate size_bits for local variables with proper fallback handling
	// Consolidates logic for handling arrays, pointers, and regular variables
	int calculateIdentifierSizeBits(const TypeSpecifierNode& type_node, bool is_array, std::string_view identifier_name) {
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

	std::vector<IrOperand> generateIdentifierIr(const IdentifierNode& identifierNode, 
	                                             ExpressionContext context = ExpressionContext::Load) {
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
						member_load.is_reference = member->is_reference;
						member_load.is_rvalue_reference = member->is_rvalue_reference;
						member_load.struct_type_info = nullptr;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

						// The ptr_temp now contains the address of the captured variable
						// We need to dereference it using PointerDereference
						auto capture_type_it = current_lambda_context_.capture_types.find(var_name_str);
						if (capture_type_it != current_lambda_context_.capture_types.end()) {
							const TypeSpecifierNode& orig_type = capture_type_it->second;

							// Generate Dereference to load the value
							TempVar result_temp = var_counter.next();
							std::vector<IrOperand> deref_operands;
							DereferenceOp deref_op;
							deref_op.result = result_temp;
							deref_op.pointer.type = orig_type.type();
							deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
							deref_op.pointer.value = ptr_temp;
							ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), Token()));
							
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
						member_load.is_reference = member->is_reference;
						member_load.is_rvalue_reference = member->is_rvalue_reference;
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
					member_load.is_reference = member->is_reference;
					member_load.is_rvalue_reference = member->is_rvalue_reference;
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
						member_load.is_reference = member->is_reference;
						member_load.is_rvalue_reference = member->is_rvalue_reference;
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
			throw std::runtime_error("Expected symbol '" + std::string(identifierNode.name()) + "' to exist in code generation");
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
				TempVar result_temp = var_counter.next();
				DereferenceOp deref_op;
				deref_op.result = result_temp;
				
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
				
				deref_op.pointer.type = pointee_type;
				deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
				deref_op.pointer.pointer_depth = type_node.pointer_depth() > 0 ? type_node.pointer_depth() : 1;  // References are like pointers
				deref_op.pointer.value = StringTable::getOrInternStringHandle(identifierNode.name());  // The reference parameter holds the address
				ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), Token()));
				
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
				// For arrays, result is a pointer (64-bit address)
				bool is_array_type = decl_node.is_array() || type_node.is_array();
				int size_bits = is_array_type ? 64 : static_cast<int>(type_node.size_in_bits());
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
					TempVar result_temp = var_counter.next();
					DereferenceOp deref_op;
					deref_op.result = result_temp;
					
					// For auto types, default to int (32 bits) since the mangling also defaults to int
					// This matches the behavior in NameMangling.h which falls through to 'H' (int)
					Type pointee_type = type_node.type();
					int pointee_size = static_cast<int>(type_node.size_in_bits());
					if (pointee_type == Type::Auto || pointee_size == 0) {
						pointee_type = Type::Int;
						pointee_size = 32;
					}
					
					deref_op.pointer.type = pointee_type;
					deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
					deref_op.pointer.pointer_depth = type_node.pointer_depth() > 0 ? type_node.pointer_depth() : 1;  // References are like pointers
					deref_op.pointer.value = StringTable::getOrInternStringHandle(identifierNode.name());  // The reference variable holds the address
					ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), Token()));
					
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
			throw std::runtime_error("Uninstantiated variable template in codegen");
			return {};
		}

		// If we get here, the symbol is not a known type
		FLASH_LOG(Codegen, Error, "Unknown symbol type for identifier '", identifierNode.name(), "'");
		throw std::runtime_error("Identifier is not a DeclarationNode");
		return {};
	}

	std::vector<IrOperand> generateQualifiedIdentifierIr(const QualifiedIdentifierNode& qualifiedIdNode) {
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
		throw std::runtime_error("Qualified identifier is not a supported type");
		return {};
	}

	std::vector<IrOperand>
		generateNumericLiteralIr(const NumericLiteralNode& numericLiteralNode) {
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

	std::vector<IrOperand> generateTypeConversion(const std::vector<IrOperand>& operands, Type fromType, Type toType, const Token& source_token) {
		// Get the actual size from the operands (they already contain the correct size)
		// operands format: [type, size, value]
		int fromSize = (operands.size() >= 2) ? std::get<int>(operands[1]) : get_type_size_bits(fromType);
		
		// For struct types (Struct or UserDefined), use the size from operands, not get_type_size_bits
		int toSize;
		if (is_struct_type(toType)) {
			// Preserve the original size for struct types
			toSize = fromSize;
		} else {
			toSize = get_type_size_bits(toType);
		}
		
		if (fromType == toType && fromSize == toSize) {
			return operands; // No conversion needed
		}

		// Check if the value is a compile-time constant (literal)
		// operands format: [type, size, value]
		bool is_literal = (operands.size() == 3) &&
		                  (std::holds_alternative<unsigned long long>(operands[2]) ||
		                   std::holds_alternative<int>(operands[2]) ||
		                   std::holds_alternative<double>(operands[2]));

		if (is_literal) {
			// For literal values, just convert the value directly without creating a TempVar
			// This allows the literal to be used as an immediate value in instructions
			if (std::holds_alternative<unsigned long long>(operands[2])) {
				unsigned long long value = std::get<unsigned long long>(operands[2]);
				// For integer literals, the value remains the same (truncation/extension happens at runtime)
				return { toType, toSize, value, 0ULL };
			} else if (std::holds_alternative<int>(operands[2])) {
				int value = std::get<int>(operands[2]);
				// Convert to unsigned long long for consistency
				return { toType, toSize, static_cast<unsigned long long>(value) };
			} else if (std::holds_alternative<double>(operands[2])) {
				double value = std::get<double>(operands[2]);
				return { toType, toSize, value, 0ULL };
			}
		}

		// For non-literal values (variables, TempVars), check if conversion is needed

		// Check for int-to-float or float-to-int conversions
		bool from_is_float = is_floating_point_type(fromType);
		bool to_is_float = is_floating_point_type(toType);

		if (from_is_float != to_is_float) {
			// Converting between int and float (or vice versa)
			TempVar resultVar = var_counter.next();
			TypeConversionOp conv_op{
				.result = resultVar,
				.from = toTypedValue(std::span<const IrOperand>(operands.data(), operands.size())),
				.to_type = toType,
				.to_size_in_bits = toSize
			};

			if (from_is_float && !to_is_float) {
				// Float to int conversion
				ir_.addInstruction(IrInstruction(IrOpcode::FloatToInt, std::move(conv_op), source_token));
			} else if (!from_is_float && to_is_float) {
				// Int to float conversion
				ir_.addInstruction(IrInstruction(IrOpcode::IntToFloat, std::move(conv_op), source_token));
			}

			return { toType, toSize, resultVar, 0ULL };
		}

		// If both are floats but different sizes, use FloatToFloat conversion
		if (from_is_float && to_is_float && fromSize != toSize) {
			TempVar resultVar = var_counter.next();
			TypeConversionOp conv_op{
				.result = resultVar,
				.from = toTypedValue(std::span<const IrOperand>(operands.data(), operands.size())),
				.to_type = toType,
				.to_size_in_bits = toSize
			};
			ir_.addInstruction(IrInstruction(IrOpcode::FloatToFloat, std::move(conv_op), source_token));
			return { toType, toSize, resultVar, 0ULL };
		}

		// If sizes are equal and only signedness differs, no actual conversion instruction is needed
		// The value can be reinterpreted as the new type
		if (fromSize == toSize) {
			// Same size, different signedness - just change the type metadata
			// Return the same value with the new type
			std::vector<IrOperand> result;
			result.push_back(toType);
			result.push_back(toSize);
			// Copy the value (TempVar or identifier)
			result.insert(result.end(), operands.begin() + 2, operands.end());
			return result;
		}

		// For non-literal values (variables, TempVars), create a conversion instruction
		TempVar resultVar = var_counter.next();

		if (fromSize < toSize) {
			// Extension needed
			ConversionOp conv_op{
				.from = toTypedValue(std::span<const IrOperand>(operands.data(), operands.size())),
				.to_type = toType,
				.to_size = toSize,
				.result = resultVar
			};
		
			// Determine whether to use sign extension or zero extension
			bool use_sign_extend = false;
			
			// For literals, check if the value fits in the signed range
			if (operands.size() >= 3 && std::holds_alternative<unsigned long long>(operands[2])) {
				unsigned long long lit_value = std::get<unsigned long long>(operands[2]);
				
				// Determine the signed max value for the source size
				unsigned long long signed_max = 0;
				if (fromSize == 8) {
					signed_max = std::numeric_limits<int8_t>::max();
				} else if (fromSize == 16) {
					signed_max = std::numeric_limits<int16_t>::max();
				} else if (fromSize == 32) {
					signed_max = std::numeric_limits<int32_t>::max();
				} else if (fromSize == 64) {
					signed_max = std::numeric_limits<int64_t>::max();
				}
				
				// If the value exceeds the signed max, it should be treated as unsigned (zero-extend)
				// Otherwise, use the type's signedness
				if (lit_value <= signed_max) {
					// Value fits in signed range, use type's signedness
					use_sign_extend = is_signed_integer_type(fromType);
				} else {
					// Value doesn't fit in signed range - zero extend
					use_sign_extend = false;
				}
			} else {
				// For non-literal values (variables, TempVars), use the type's signedness
				use_sign_extend = is_signed_integer_type(fromType);
			}
			
			if (use_sign_extend) {
				ir_.addInstruction(IrInstruction(IrOpcode::SignExtend, std::move(conv_op), source_token));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::ZeroExtend, std::move(conv_op), source_token));
			}
		} else if (fromSize > toSize) {
			// Truncation needed
			ConversionOp conv_op{
				.from = toTypedValue(std::span<const IrOperand>(operands.data(), operands.size())),
				.to_type = toType,
				.to_size = toSize,
				.result = resultVar
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Truncate, std::move(conv_op), source_token));
		}
		// Return the converted operands
		return { toType, toSize, resultVar, 0ULL };
	}

	std::vector<IrOperand>
		generateStringLiteralIr(const StringLiteralNode& stringLiteralNode) {
		// Generate IR for string literal
		// Create a temporary variable to hold the address of the string
		TempVar result_var = var_counter.next();

		// Add StringLiteral IR instruction using StringLiteralOp
		StringLiteralOp op;
		op.result = result_var;
		op.content = stringLiteralNode.value();

		ir_.addInstruction(IrInstruction(IrOpcode::StringLiteral, std::move(op), Token()));

		// Return the result as a char pointer (const char*)
		// We use Type::Char with 64-bit size to indicate it's a pointer
		return { Type::Char, 64, result_var, 0ULL };
	}

	// ============================================================================
	// Address Expression Analysis for One-Pass Address Calculation
	// ============================================================================
	
	// Helper function to extract DeclarationNode from a symbol (handles both DeclarationNode and VariableDeclarationNode)
	static const DeclarationNode* getDeclarationFromSymbol(const std::optional<ASTNode>& symbol) {
		if (!symbol.has_value()) {
			return nullptr;
		}
		if (symbol->is<DeclarationNode>()) {
			return &symbol->as<DeclarationNode>();
		} else if (symbol->is<VariableDeclarationNode>()) {
			return &symbol->as<VariableDeclarationNode>().declaration();
		}
		return nullptr;
	}
	
	// Structure to hold the components of an address expression
	struct AddressComponents {
		std::variant<StringHandle, TempVar> base;           // Base variable or temp
		std::vector<ComputeAddressOp::ArrayIndex> array_indices;  // Array indices
		int total_member_offset = 0;                        // Accumulated member offsets
		Type final_type = Type::Void;                       // Type of final result
		int final_size_bits = 0;                            // Size in bits
		int pointer_depth = 0;                              // Pointer depth of final result
	};

	// Analyze an expression for address calculation components
	// Returns std::nullopt if the expression is not suitable for one-pass address calculation
	std::optional<AddressComponents> analyzeAddressExpression(
		const ExpressionNode& expr, 
		int accumulated_offset = 0) 
	{
		// Handle Identifier (base case)
		if (std::holds_alternative<IdentifierNode>(expr)) {
			const IdentifierNode& identifier = std::get<IdentifierNode>(expr);
			StringHandle identifier_handle = StringTable::getOrInternStringHandle(identifier.name());
			
			// Look up the identifier
			std::optional<ASTNode> symbol = symbol_table.lookup(identifier_handle);
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(identifier_handle);
			}
			if (!symbol.has_value()) {
				return std::nullopt;  // Can't find identifier
			}
			
			// Get type info
			const TypeSpecifierNode* type_node = nullptr;
			if (symbol->is<DeclarationNode>()) {
				type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
			} else if (symbol->is<VariableDeclarationNode>()) {
				type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
			} else {
				return std::nullopt;
			}
			
			AddressComponents result;
			result.base = identifier_handle;
			result.total_member_offset = accumulated_offset;
			result.final_type = type_node->type();
			result.final_size_bits = static_cast<int>(type_node->size_in_bits());
			return result;
		}
		
		// Handle MemberAccess (obj.member)
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			const MemberAccessNode& memberAccess = std::get<MemberAccessNode>(expr);
			const ASTNode& object_node = memberAccess.object();
			
			if (!object_node.is<ExpressionNode>()) {
				return std::nullopt;
			}
			
			const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
			
			// Get object type to lookup member
			auto object_operands = visitExpressionNode(obj_expr, ExpressionContext::LValueAddress);
			if (object_operands.size() < 4) {
				return std::nullopt;
			}
			
			Type object_type = std::get<Type>(object_operands[0]);
			TypeIndex type_index = 0;
			if (std::holds_alternative<unsigned long long>(object_operands[3])) {
				type_index = static_cast<TypeIndex>(std::get<unsigned long long>(object_operands[3]));
			}
			
			// Look up member information
			if (type_index == 0 || type_index >= gTypeInfo.size() || object_type != Type::Struct) {
				return std::nullopt;
			}
			
			std::string_view member_name = memberAccess.member_name();
			StringHandle member_handle = StringTable::getOrInternStringHandle(std::string(member_name));
			auto result = FlashCpp::gLazyMemberResolver.resolve(type_index, member_handle);
			
			if (!result) {
				return std::nullopt;
			}
			
			// Recurse with accumulated offset
			int new_offset = accumulated_offset + static_cast<int>(result.adjusted_offset);
			auto base_components = analyzeAddressExpression(obj_expr, new_offset);
			
			if (!base_components.has_value()) {
				return std::nullopt;
			}
			
			// Update type to member type
			base_components->final_type = result.member->type;
			base_components->final_size_bits = static_cast<int>(result.member->size * 8);
			// Use explicit pointer depth from struct member layout
			base_components->pointer_depth = result.member->pointer_depth;
			
			return base_components;
		}
		
		// Handle ArraySubscript (arr[index])
		if (std::holds_alternative<ArraySubscriptNode>(expr)) {
			const ArraySubscriptNode& arraySubscript = std::get<ArraySubscriptNode>(expr);
			
			// For multidimensional arrays (nested ArraySubscriptNode), return nullopt
			// to let the specialized handling in generateUnaryOperatorIr compute the flat index correctly
			const ExpressionNode& array_expr_inner = arraySubscript.array_expr().as<ExpressionNode>();
			if (std::holds_alternative<ArraySubscriptNode>(array_expr_inner)) {
				return std::nullopt;  // Fall through to multidimensional array handling
			}
			
			// Get the array and index operands
			auto array_operands = visitExpressionNode(arraySubscript.array_expr().as<ExpressionNode>());
			auto index_operands = visitExpressionNode(arraySubscript.index_expr().as<ExpressionNode>());
			
			if (array_operands.size() < 3 || index_operands.size() < 3) {
				return std::nullopt;
			}
			
			Type element_type = std::get<Type>(array_operands[0]);
			int element_size_bits = std::get<int>(array_operands[1]);
			int element_pointer_depth = 0;  // Track pointer depth for pointer array elements
			
			// Calculate actual element size from array declaration
			if (std::holds_alternative<StringHandle>(array_operands[2])) {
				StringHandle array_name = std::get<StringHandle>(array_operands[2]);
				std::optional<ASTNode> symbol = symbol_table.lookup(array_name);
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(array_name);
				}
				
				const DeclarationNode* decl_ptr = getDeclarationFromSymbol(symbol);
				if (decl_ptr && (decl_ptr->is_array() || decl_ptr->type_node().as<TypeSpecifierNode>().is_array())) {
					const TypeSpecifierNode& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
					if (type_node.pointer_depth() > 0) {
						element_size_bits = 64;
						element_pointer_depth = type_node.pointer_depth();  // Track pointer depth
					} else if (type_node.type() == Type::Struct) {
						TypeIndex type_index_from_decl = type_node.type_index();
						if (type_index_from_decl > 0 && type_index_from_decl < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_index_from_decl];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info) {
								element_size_bits = static_cast<int>(struct_info->total_size * 8);
							}
						}
					} else {
						element_size_bits = static_cast<int>(type_node.size_in_bits());
						if (element_size_bits == 0) {
							element_size_bits = get_type_size_bits(type_node.type());
						}
					}
				}
			} else if (std::holds_alternative<TempVar>(array_operands[2])) {
				// Array from expression (e.g., member access: obj.arr_member[idx])
				// array_operands[1] contains total array size, we need element size
				// For primitive types, use the type's size directly
				if (element_type == Type::Struct) {
					// For struct arrays, element_size_bits is already correct from member info
					// (it contains the struct size, not the total array size)
				} else {
					// For primitive type arrays, get the element size from the type
					element_size_bits = get_type_size_bits(element_type);
				}
				// Try to get pointer depth from array_operands[3] if available
				if (array_operands.size() >= 4 && std::holds_alternative<unsigned long long>(array_operands[3])) {
					element_pointer_depth = static_cast<int>(std::get<unsigned long long>(array_operands[3]));
				}
			}
			
			// Recurse on the array expression (could be nested: arr[i][j])
			auto base_components = analyzeAddressExpression(arraySubscript.array_expr().as<ExpressionNode>(), accumulated_offset);
			
			if (!base_components.has_value()) {
				return std::nullopt;
			}
			
			// Add this array index
			ComputeAddressOp::ArrayIndex arr_idx;
			arr_idx.element_size_bits = element_size_bits;
			
			// Capture index type information for proper sign extension
			arr_idx.index_type = std::get<Type>(index_operands[0]);
			arr_idx.index_size_bits = std::get<int>(index_operands[1]);
			
			// Set index value
			if (std::holds_alternative<unsigned long long>(index_operands[2])) {
				arr_idx.index = std::get<unsigned long long>(index_operands[2]);
			} else if (std::holds_alternative<TempVar>(index_operands[2])) {
				arr_idx.index = std::get<TempVar>(index_operands[2]);
			} else if (std::holds_alternative<StringHandle>(index_operands[2])) {
				arr_idx.index = std::get<StringHandle>(index_operands[2]);
			} else {
				return std::nullopt;
			}
			
			base_components->array_indices.push_back(arr_idx);
			base_components->final_type = element_type;
			base_components->final_size_bits = element_size_bits;
			base_components->pointer_depth = element_pointer_depth;  // Set pointer depth for the element
			
			return base_components;
		}
		
		// Unsupported expression type
		return std::nullopt;
	}

	std::vector<IrOperand> generateUnaryOperatorIr(const UnaryOperatorNode& unaryOperatorNode, 
	                                                 ExpressionContext context = ExpressionContext::Load) {
		std::vector<IrOperand> irOperands;

		// OPERATOR OVERLOAD RESOLUTION
		// For full standard compliance, operator& should call overloaded operator& if it exists.
		// __builtin_addressof (marked with is_builtin_addressof flag) always bypasses overloads.
		if (!unaryOperatorNode.is_builtin_addressof() && unaryOperatorNode.op() == "&" && 
		    unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			
			// For now, only handle simple identifiers
			if (std::holds_alternative<IdentifierNode>(operandExpr)) {
				const IdentifierNode& ident = std::get<IdentifierNode>(operandExpr);
				StringHandle identifier_handle = StringTable::getOrInternStringHandle(ident.name());
				
				std::optional<ASTNode> symbol = symbol_table.lookup(identifier_handle);
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(identifier_handle);
				}
				
				if (symbol.has_value()) {
					const TypeSpecifierNode* type_node = nullptr;
					if (symbol->is<DeclarationNode>()) {
						type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
					} else if (symbol->is<VariableDeclarationNode>()) {
						type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
					}
					
					if (type_node && type_node->type() == Type::Struct && type_node->pointer_depth() == 0) {
						// Check for operator& overload
						auto overload_result = findUnaryOperatorOverload(type_node->type_index(), "&");
						
						if (overload_result.has_overload) {
							// Found an overload! Generate a member function call instead of built-in address-of
							FLASH_LOG_FORMAT(Codegen, Debug, "Resolving operator& overload for type index {}", 
							         type_node->type_index());
							
							const StructMemberFunction& member_func = *overload_result.member_overload;
							const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
							
							// Get struct name for mangling
							std::string_view struct_name = StringTable::getStringView(gTypeInfo[type_node->type_index()].name());
							
							// Get the return type from the function declaration
							const TypeSpecifierNode& return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
							
							// Generate mangled name using the proper mangling infrastructure
							// This handles both Itanium (Linux) and MSVC (Windows) name mangling
							std::string_view operator_func_name = "operator&";
							std::vector<TypeSpecifierNode> empty_params; // No explicit parameters (only implicit 'this')
							std::vector<std::string_view> empty_namespace;
							auto mangled_name = NameMangling::generateMangledName(
								operator_func_name,
								return_type,
								empty_params,
								false, // not variadic
								struct_name,
								empty_namespace,
								Linkage::CPlusPlus
							);
							
							// Generate the call
							TempVar ret_var = var_counter.next();
							
							// Create CallOp
							CallOp call_op;
							call_op.result = ret_var;
							call_op.return_type = return_type.type();
							// For pointer return types, use 64-bit size (pointer size on x64)
							if (return_type.pointer_depth() > 0) {
								call_op.return_size_in_bits = 64;
							} else {
								call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
								if (call_op.return_size_in_bits == 0) {
									call_op.return_size_in_bits = get_type_size_bits(return_type.type());
								}
							}
							call_op.function_name = mangled_name;  // MangledName implicitly converts to StringHandle
							call_op.is_variadic = false;
							call_op.is_member_function = true;  // This is a member function call
							
							// Add 'this' pointer as first argument
							call_op.args.push_back(TypedValue{
								.type = type_node->type(),
								.size_in_bits = 64,  // Pointer size
								.value = IrValue(identifier_handle)
							});
							
							// Add the function call instruction
							ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), unaryOperatorNode.get_token()));
							
							// Return the result
							unsigned long long fourth_element = static_cast<unsigned long long>(return_type.pointer_depth());
							if (fourth_element == 0 && return_type.type() == Type::Struct) {
								fourth_element = static_cast<unsigned long long>(return_type.type_index());
							}
							
							return {return_type.type(), call_op.return_size_in_bits, ret_var, fourth_element};
						}
					}
				}
			}
		}

		auto tryBuildIdentifierOperand = [&](const IdentifierNode& identifier, std::vector<IrOperand>& out) -> bool {
			// Phase 4: Using StringHandle for lookup
			StringHandle identifier_handle = StringTable::getOrInternStringHandle(identifier.name());

			// Static local variables are stored as globals with mangled names
			auto static_local_it = static_local_names_.find(identifier_handle);
			if (static_local_it != static_local_names_.end()) {
				out.clear();
				out.emplace_back(static_local_it->second.type);
				out.emplace_back(static_cast<int>(static_local_it->second.size_in_bits));
				out.emplace_back(static_local_it->second.mangled_name);
				out.emplace_back(0ULL); // pointer depth - assume 0 for static locals for now
				return true;
			}

			std::optional<ASTNode> symbol = symbol_table.lookup(identifier_handle);
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(identifier_handle);
			}
			if (!symbol.has_value()) {
				return false;
			}

			const TypeSpecifierNode* type_node = nullptr;
			if (symbol->is<DeclarationNode>()) {
				type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
			} else if (symbol->is<VariableDeclarationNode>()) {
				type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
			} else {
				return false;
			}

			out.clear();
			out.emplace_back(type_node->type());
			out.emplace_back(static_cast<int>(type_node->size_in_bits()));
			out.emplace_back(identifier_handle);
			// For the 4th element: 
			// - For struct types, ALWAYS return type_index (even if it's a pointer to struct)
			// - For non-struct pointer types, return pointer_depth
			// - Otherwise return 0
			unsigned long long fourth_element = (type_node->type() == Type::Struct)
				? static_cast<unsigned long long>(type_node->type_index())
				: ((type_node->pointer_depth() > 0) ? static_cast<unsigned long long>(type_node->pointer_depth()) : 0ULL);
			out.emplace_back(fourth_element);
			return true;
		};

		// Special handling for &arr[index] - generate address directly without loading value
		if (unaryOperatorNode.op() == "&" && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			
			// Try new one-pass address analysis first
			auto addr_components = analyzeAddressExpression(operandExpr);
			if (addr_components.has_value()) {
				// Successfully analyzed - generate ComputeAddress IR
				TempVar result_var = var_counter.next();
				
				ComputeAddressOp compute_addr_op;
				compute_addr_op.result = result_var;
				compute_addr_op.base = addr_components->base;
				compute_addr_op.array_indices = std::move(addr_components->array_indices);
				compute_addr_op.total_member_offset = addr_components->total_member_offset;
				compute_addr_op.result_type = addr_components->final_type;
				compute_addr_op.result_size_bits = addr_components->final_size_bits;
				
				ir_.addInstruction(IrInstruction(IrOpcode::ComputeAddress, std::move(compute_addr_op), unaryOperatorNode.get_token()));
				
				// Return pointer to result (64-bit pointer)
				// The 4th element is pointer_depth + 1 (we're taking address, so depth increases)
				return { addr_components->final_type, 64, result_var, static_cast<unsigned long long>(addr_components->pointer_depth + 1) };
			}
			
			// Fall back to legacy implementation if analysis failed
			
			// Handle &arr[index].member (member access on array element)
			if (std::holds_alternative<MemberAccessNode>(operandExpr)) {
				const MemberAccessNode& memberAccess = std::get<MemberAccessNode>(operandExpr);
				const ASTNode& object_node = memberAccess.object();
				
				// Check if the object is an array subscript
				if (object_node.is<ExpressionNode>()) {
					const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
					if (std::holds_alternative<ArraySubscriptNode>(obj_expr)) {
						const ArraySubscriptNode& arraySubscript = std::get<ArraySubscriptNode>(obj_expr);
						
						// Get the array and index operands
						auto array_operands = visitExpressionNode(arraySubscript.array_expr().as<ExpressionNode>());
						auto index_operands = visitExpressionNode(arraySubscript.index_expr().as<ExpressionNode>());
						
						// Check that we have valid operands
						if (array_operands.size() >= 3 && index_operands.size() >= 3) {
							Type element_type = std::get<Type>(array_operands[0]);
							int element_size_bits = std::get<int>(array_operands[1]);
							
							// For arrays, array_operands[1] is the pointer size (64), not element size
							// We need to calculate the actual element size from the array declaration
							if (std::holds_alternative<StringHandle>(array_operands[2])) {
								StringHandle array_name = std::get<StringHandle>(array_operands[2]);
								std::optional<ASTNode> symbol = symbol_table.lookup(array_name);
								if (!symbol.has_value() && global_symbol_table_) {
									symbol = global_symbol_table_->lookup(array_name);
								}
								if (symbol.has_value()) {
									const DeclarationNode* decl_ptr = nullptr;
									if (symbol->is<DeclarationNode>()) {
										decl_ptr = &symbol->as<DeclarationNode>();
									} else if (symbol->is<VariableDeclarationNode>()) {
										decl_ptr = &symbol->as<VariableDeclarationNode>().declaration();
									}
									
									if (decl_ptr && (decl_ptr->is_array() || decl_ptr->type_node().as<TypeSpecifierNode>().is_array())) {
										// This is an array - calculate element size
										const TypeSpecifierNode& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
										if (type_node.pointer_depth() > 0) {
											// Array of pointers
											element_size_bits = 64;
										} else if (type_node.type() == Type::Struct) {
											// Array of structs
											TypeIndex type_index_from_decl = type_node.type_index();
											if (type_index_from_decl > 0 && type_index_from_decl < gTypeInfo.size()) {
												const TypeInfo& type_info = gTypeInfo[type_index_from_decl];
												const StructTypeInfo* struct_info = type_info.getStructInfo();
												if (struct_info) {
													element_size_bits = static_cast<int>(struct_info->total_size * 8);
												}
											}
										} else {
											// Regular array - use type size
											element_size_bits = static_cast<int>(type_node.size_in_bits());
											if (element_size_bits == 0) {
												element_size_bits = get_type_size_bits(type_node.type());
											}
										}
									}
								}
							}
							
							// Get the struct type index (4th element of array_operands contains type_index for struct types)
							TypeIndex type_index = 0;
							if (array_operands.size() >= 4 && std::holds_alternative<unsigned long long>(array_operands[3])) {
								type_index = static_cast<TypeIndex>(std::get<unsigned long long>(array_operands[3]));
							}
							
							// Look up member information
							if (type_index > 0 && type_index < gTypeInfo.size() && element_type == Type::Struct) {
								std::string_view member_name = memberAccess.member_name();
								StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
								auto member_result = FlashCpp::gLazyMemberResolver.resolve(type_index, member_handle);
								
								if (member_result) {
									// First, get the address of the array element
									TempVar elem_addr_var = var_counter.next();
									ArrayElementAddressOp elem_addr_payload;
									elem_addr_payload.result = elem_addr_var;
									elem_addr_payload.element_type = element_type;
									elem_addr_payload.element_size_in_bits = element_size_bits;
									
									// Set array (either variable name or temp)
									if (std::holds_alternative<StringHandle>(array_operands[2])) {
										elem_addr_payload.array = std::get<StringHandle>(array_operands[2]);
									} else if (std::holds_alternative<TempVar>(array_operands[2])) {
										elem_addr_payload.array = std::get<TempVar>(array_operands[2]);
									}
									
									// Set index as TypedValue
									elem_addr_payload.index = toTypedValue(std::span<const IrOperand>(&index_operands[0], 3));
									
									ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(elem_addr_payload), arraySubscript.bracket_token()));
									
									// Now compute the member address by adding the member offset
									// We need to add the offset to the pointer value
									// Treat the pointer as a 64-bit integer for arithmetic purposes
									TempVar member_addr_var = var_counter.next();
									BinaryOp add_offset;
									add_offset.lhs = { Type::UnsignedLongLong, POINTER_SIZE_BITS, elem_addr_var };  // pointer treated as integer
									add_offset.rhs = { Type::UnsignedLongLong, POINTER_SIZE_BITS, static_cast<unsigned long long>(member_result.adjusted_offset) };
									add_offset.result = member_addr_var;
									
									ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_offset), memberAccess.member_token()));
									
									// Return pointer to member (64-bit pointer, 0 for no additional type info)
									return { member_result.member->type, POINTER_SIZE_BITS, member_addr_var, 0ULL };
								}
							}
						}
					}
				}
				
				// Handle general case: &obj.member (where obj is NOT an array subscript)
				// This generates the member address directly without loading the value
				if (!object_node.is<ExpressionNode>() || 
				    (object_node.is<ExpressionNode>() && !std::holds_alternative<ArraySubscriptNode>(object_node.as<ExpressionNode>()))) {
					
					// Get the object expression (identifier, pointer dereference, etc.)
					auto object_operands = visitExpressionNode(object_node.as<ExpressionNode>(), ExpressionContext::LValueAddress);
					
					if (object_operands.size() >= 3) {
						Type object_type = std::get<Type>(object_operands[0]);
						
						// Get the struct type index
						TypeIndex type_index = 0;
						if (object_operands.size() >= 4 && std::holds_alternative<unsigned long long>(object_operands[3])) {
							type_index = static_cast<TypeIndex>(std::get<unsigned long long>(object_operands[3]));
						}
						
						// Look up member information
						if (type_index > 0 && type_index < gTypeInfo.size() && object_type == Type::Struct) {
							std::string_view member_name = memberAccess.member_name();
							StringHandle member_handle = StringTable::getOrInternStringHandle(std::string(member_name));
							auto member_result = FlashCpp::gLazyMemberResolver.resolve(type_index, member_handle);
							
							if (member_result) {
								TempVar result_var = var_counter.next();
								
								// For simple identifiers, generate a MemberAddressOp or use AddressOf with member context
								// For now, use a simpler approach: emit AddressOf, then Add offset in generated code
								// But mark the intermediate as NOT a reference to avoid dereferencing
								
								if (std::holds_alternative<StringHandle>(object_operands[2])) {
									StringHandle obj_name = std::get<StringHandle>(object_operands[2]);
									
									// Create a custom AddressOf-like operation that computes obj_addr + member_offset directly
									// We'll use ArrayElementAddress with index 0 and treat it as a base address calc
									// Actually, let's just emit the calculation inline without using intermediate temps
									
									// Generate IR to compute the member address
									// We need a MemberAddressOp or similar
									// For now, let's use the existing approach but avoid marking as reference
									
									// Option: Generate AddressOfMemberOp
									AddressOfMemberOp addr_member_op;
									addr_member_op.result = result_var;
									addr_member_op.base_object = obj_name;
									addr_member_op.member_offset = static_cast<int>(member_result.adjusted_offset);
									addr_member_op.member_type = member_result.member->type;
									addr_member_op.member_size_in_bits = static_cast<int>(member_result.member->size * 8);
									
									ir_.addInstruction(IrInstruction(IrOpcode::AddressOfMember, std::move(addr_member_op), memberAccess.member_token()));
									
									// Return pointer to member
									return { member_result.member->type, POINTER_SIZE_BITS, result_var, 0ULL };
								}
							}
						}
					}
				}
			}
			
			// Handle &arr[index] (without member access) - includes multidimensional arrays
			if (std::holds_alternative<ArraySubscriptNode>(operandExpr)) {
				const ArraySubscriptNode& arraySubscript = std::get<ArraySubscriptNode>(operandExpr);
				
				// Check if this is a multidimensional array access (nested ArraySubscriptNode)
				const ExpressionNode& array_expr = arraySubscript.array_expr().as<ExpressionNode>();
				if (std::holds_alternative<ArraySubscriptNode>(array_expr)) {
					// This is a multidimensional array access like &arr[i][j]
					auto multi_dim = collectMultiDimArrayIndices(arraySubscript);
					
					if (multi_dim.is_valid && multi_dim.base_decl) {
						// Compute flat index using the same logic as generateArraySubscriptIr
						const auto& dims = multi_dim.base_decl->array_dimensions();
						std::vector<size_t> strides;
						strides.reserve(dims.size());
						
						// Calculate strides (same as in generateArraySubscriptIr)
						bool valid_dimensions = true;
						for (size_t i = 0; i < dims.size(); ++i) {
							size_t stride = 1;
							for (size_t j = i + 1; j < dims.size(); ++j) {
								ConstExpr::EvaluationContext ctx(symbol_table);
								auto eval_result = ConstExpr::Evaluator::evaluate(dims[j], ctx);
								if (eval_result.success() && eval_result.as_int() > 0) {
									stride *= static_cast<size_t>(eval_result.as_int());
								} else {
									// Invalid dimension - fall through to single-dimension handling
									valid_dimensions = false;
									break;
								}
							}
							if (!valid_dimensions) break;
							strides.push_back(stride);
						}
						
						if (!valid_dimensions) {
							// Fall through to single-dimensional array handling
							goto single_dim_handling;
						}
						
						// Get element type and size
						const TypeSpecifierNode& type_node = multi_dim.base_decl->type_node().as<TypeSpecifierNode>();
						Type element_type = type_node.type();
						int element_size_bits = static_cast<int>(type_node.size_in_bits());
						if (element_size_bits == 0) {
							element_size_bits = get_type_size_bits(element_type);
						}
						TypeIndex element_type_index = type_node.type_index();
						
						// Compute flat index: for arr[i][j] on arr[M][N], index = i*N + j
						TempVar flat_index = var_counter.next();
						bool first_term = true;
						
						for (size_t k = 0; k < multi_dim.indices.size(); ++k) {
							auto idx_operands = visitExpressionNode(multi_dim.indices[k].as<ExpressionNode>());
							
							if (strides[k] == 1) {
								if (first_term) {
									// flat_index = indices[k]
									AssignmentOp assign_op;
									assign_op.result = flat_index;
									assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
									assign_op.rhs = toTypedValue(idx_operands);
									ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
									first_term = false;
								} else {
									// flat_index += indices[k]
									TempVar new_flat = var_counter.next();
									BinaryOp add_op;
									add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
									add_op.rhs = toTypedValue(idx_operands);
									add_op.result = IrValue{new_flat};
									ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
									flat_index = new_flat;
								}
							} else {
								// temp = indices[k] * strides[k]
								TempVar temp_prod = var_counter.next();
								BinaryOp mul_op;
								mul_op.lhs = toTypedValue(idx_operands);
								mul_op.rhs = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(strides[k])};
								mul_op.result = IrValue{temp_prod};
								ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));
								
								if (first_term) {
									flat_index = temp_prod;
									first_term = false;
								} else {
									// flat_index += temp
									TempVar new_flat = var_counter.next();
									BinaryOp add_op;
									add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
									add_op.rhs = TypedValue{Type::UnsignedLongLong, 64, temp_prod};
									add_op.result = IrValue{new_flat};
									ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
									flat_index = new_flat;
								}
							}
						}
						
						// Now generate ArrayElementAddress with the flat index
						TempVar addr_var = var_counter.next();
						ArrayElementAddressOp payload;
						payload.result = addr_var;
						payload.element_type = element_type;
						payload.element_size_in_bits = element_size_bits;
						payload.array = StringTable::getOrInternStringHandle(multi_dim.base_array_name);
						payload.index.type = Type::UnsignedLongLong;
						payload.index.size_in_bits = 64;
						payload.index.value = flat_index;
						payload.is_pointer_to_array = false;  // Multidimensional arrays are actual arrays, not pointers
						
						ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(payload), arraySubscript.bracket_token()));
						
						return { element_type, 64, addr_var, static_cast<unsigned long long>(element_type_index) };
					}
				}
				
				// Fall through to single-dimensional array handling
				single_dim_handling:
				// Get the array and index operands
				auto array_operands = visitExpressionNode(arraySubscript.array_expr().as<ExpressionNode>());
				auto index_operands = visitExpressionNode(arraySubscript.index_expr().as<ExpressionNode>());
				
				Type element_type = std::get<Type>(array_operands[0]);
				int element_size_bits = std::get<int>(array_operands[1]);
				
				// For arrays, array_operands[1] is the pointer size (64), not element size
				// We need to calculate the actual element size from the array declaration
				if (std::holds_alternative<StringHandle>(array_operands[2])) {
					StringHandle array_name = std::get<StringHandle>(array_operands[2]);
					std::optional<ASTNode> symbol = symbol_table.lookup(array_name);
					if (!symbol.has_value() && global_symbol_table_) {
						symbol = global_symbol_table_->lookup(array_name);
					}
					if (symbol.has_value()) {
						const DeclarationNode* decl_ptr = nullptr;
						if (symbol->is<DeclarationNode>()) {
							decl_ptr = &symbol->as<DeclarationNode>();
						} else if (symbol->is<VariableDeclarationNode>()) {
							decl_ptr = &symbol->as<VariableDeclarationNode>().declaration();
						}
						
						if (decl_ptr && (decl_ptr->is_array() || decl_ptr->type_node().as<TypeSpecifierNode>().is_array())) {
							// This is an array - calculate element size
							const TypeSpecifierNode& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
							if (type_node.pointer_depth() > 0) {
								// Array of pointers
								element_size_bits = 64;
							} else if (type_node.type() == Type::Struct) {
								// Array of structs
								TypeIndex type_index = type_node.type_index();
								if (type_index > 0 && type_index < gTypeInfo.size()) {
									const TypeInfo& type_info = gTypeInfo[type_index];
									const StructTypeInfo* struct_info = type_info.getStructInfo();
									if (struct_info) {
										element_size_bits = static_cast<int>(struct_info->total_size * 8);
									}
								}
							} else {
								// Regular array - use type size
								element_size_bits = static_cast<int>(type_node.size_in_bits());
								if (element_size_bits == 0) {
									element_size_bits = get_type_size_bits(type_node.type());
								}
							}
						}
					}
				}
				
				// Create temporary for the address
				TempVar addr_var = var_counter.next();
				
				// Create typed payload for ArrayElementAddress
				ArrayElementAddressOp payload;
				payload.result = addr_var;
				payload.element_type = element_type;
				payload.element_size_in_bits = element_size_bits;
				
				// Set array (either variable name or temp)
				if (std::holds_alternative<StringHandle>(array_operands[2])) {
					payload.array = std::get<StringHandle>(array_operands[2]);
				} else if (std::holds_alternative<TempVar>(array_operands[2])) {
					payload.array = std::get<TempVar>(array_operands[2]);
				}
				
				// Set index as TypedValue
				payload.index = toTypedValue(std::span<const IrOperand>(&index_operands[0], 3)); 
				
				ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(payload), arraySubscript.bracket_token()));
				
				// Return pointer to element (64-bit pointer)
				return { element_type, 64, addr_var, 0ULL };
			}
		}
		
		// Helper lambda to generate member increment/decrement IR
		// Returns the result operands, or empty if not applicable
		// adjusted_offset must be provided (from LazyMemberResolver result)
		auto generateMemberIncDec = [&](StringHandle object_name, 
		                                 const StructMember* member, bool is_reference_capture,
		                                 const Token& token, size_t adjusted_offset) -> std::vector<IrOperand> {
			int member_size_bits = static_cast<int>(member->size * 8);
			TempVar result_var = var_counter.next();
			StringHandle member_name = member->getName();
			
			if (is_reference_capture) {
				// By-reference: load pointer, dereference, inc/dec, store back through pointer
				TempVar ptr_temp = var_counter.next();
				MemberLoadOp member_load;
				member_load.result.value = ptr_temp;
				member_load.result.type = member->type;
				member_load.result.size_in_bits = 64;  // pointer
				member_load.object = object_name;
				member_load.member_name = member_name;
				member_load.offset = static_cast<int>(adjusted_offset);
				member_load.is_reference = true;
				member_load.is_rvalue_reference = false;
				member_load.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), token));
				
				// Load current value through pointer
				TempVar current_val = var_counter.next();
				DereferenceOp deref_op;
				deref_op.result = current_val;
				deref_op.pointer.type = member->type;
				deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
				deref_op.pointer.pointer_depth = 1;  // TODO: Verify pointer depth
				deref_op.pointer.value = ptr_temp;
				ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), token));
				
				bool is_prefix = unaryOperatorNode.is_prefix();
				BinaryOp add_op{
					.lhs = { member->type, member_size_bits, current_val },
					.rhs = { Type::Int, 32, 1ULL },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(
					unaryOperatorNode.op() == "++" ? IrOpcode::Add : IrOpcode::Subtract, 
					std::move(add_op), token));
				
				// Store back through pointer
				DereferenceStoreOp store_op;
				store_op.pointer.type = member->type;
				store_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
				store_op.pointer.pointer_depth = 1;  // Single pointer dereference
				store_op.pointer.value = ptr_temp;
				store_op.value = { member->type, member_size_bits, result_var };
				ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_op), token));
				
				TempVar return_val = is_prefix ? result_var : current_val;
				return { member->type, member_size_bits, return_val, 0ULL };
			} else {
				// By-value: load member, inc/dec, store back to member
				TempVar current_val = var_counter.next();
				MemberLoadOp member_load;
				member_load.result.value = current_val;
				member_load.result.type = member->type;
				member_load.result.size_in_bits = member_size_bits;
				member_load.object = object_name;
				member_load.member_name = member_name;
				member_load.offset = static_cast<int>(adjusted_offset);
				member_load.is_reference = false;
				member_load.is_rvalue_reference = false;
				member_load.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), token));
				
				bool is_prefix = unaryOperatorNode.is_prefix();
				BinaryOp add_op{
					.lhs = { member->type, member_size_bits, current_val },
					.rhs = { Type::Int, 32, 1ULL },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(
					unaryOperatorNode.op() == "++" ? IrOpcode::Add : IrOpcode::Subtract, 
					std::move(add_op), token));
				
				// Store back to member
				MemberStoreOp store_op;
				store_op.object = object_name;
				store_op.member_name = member_name;
				store_op.offset = static_cast<int>(adjusted_offset);
				store_op.value = { member->type, member_size_bits, result_var };
				store_op.is_reference = false;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_op), token));
				
				TempVar return_val = is_prefix ? result_var : current_val;
				return { member->type, member_size_bits, return_val, 0ULL };
			}
		};
		
		// Check if this is an increment/decrement on a captured variable in a lambda
		if ((unaryOperatorNode.op() == "++" || unaryOperatorNode.op() == "--") && 
		    current_lambda_context_.isActive() && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(operandExpr)) {
				const IdentifierNode& identifier = std::get<IdentifierNode>(operandExpr);
				StringHandle var_name_str = StringTable::getOrInternStringHandle(identifier.name());
				
				// Check if this is a captured variable
				if (current_lambda_context_.captures.find(var_name_str) != current_lambda_context_.captures.end()) {
					// Look up the closure struct type
					auto type_it = gTypesByName.find(current_lambda_context_.closure_type);
					if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
						TypeIndex closure_type_index = type_it->second->type_index_;
						auto member_result = FlashCpp::gLazyMemberResolver.resolve(closure_type_index, var_name_str);
						if (member_result) {
							auto kind_it = current_lambda_context_.capture_kinds.find(var_name_str);
							bool is_reference = (kind_it != current_lambda_context_.capture_kinds.end() &&
							                     kind_it->second == LambdaCaptureNode::CaptureKind::ByReference);
							return generateMemberIncDec(StringTable::getOrInternStringHandle("this"sv), member_result.member, is_reference, 
							                            unaryOperatorNode.get_token(), member_result.adjusted_offset);
						}
					}
				}
			}
		}
		
		// Check if this is an increment/decrement on a struct member (e.g., ++inst.v)
		if ((unaryOperatorNode.op() == "++" || unaryOperatorNode.op() == "--") && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<MemberAccessNode>(operandExpr)) {
				const MemberAccessNode& member_access = std::get<MemberAccessNode>(operandExpr);
				auto member_name = StringTable::getOrInternStringHandle(member_access.member_name());
				
				// Get the object being accessed
				ASTNode object_node = member_access.object();
				if (object_node.is<ExpressionNode>()) {
					const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(obj_expr)) {
						const IdentifierNode& object_ident = std::get<IdentifierNode>(obj_expr);
						auto object_name = StringTable::getOrInternStringHandle(object_ident.name());
						
						// Look up the struct in symbol table
						std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
						if (!symbol.has_value() && global_symbol_table_) {
							symbol = global_symbol_table_->lookup(object_name);
						}
						
						if (symbol.has_value()) {
							const DeclarationNode* object_decl = get_decl_from_symbol(*symbol);
							if (object_decl) {
								const TypeSpecifierNode& object_type = object_decl->type_node().as<TypeSpecifierNode>();
								if (is_struct_type(object_type.type())) {
									TypeIndex type_index = object_type.type_index();
									if (type_index < gTypeInfo.size()) {
										auto member_result = FlashCpp::gLazyMemberResolver.resolve(type_index, member_name);
										if (member_result) {
											return generateMemberIncDec(object_name, member_result.member, false,
											                            member_access.member_token(), member_result.adjusted_offset);
										}
									}
								}
							}
						}
					}
				}
			}
		}
		
		std::vector<IrOperand> operandIrOperands;
		bool operandHandledAsIdentifier = false;
		// For ++, --, and & operators on identifiers, use tryBuildIdentifierOperand
		// This ensures we get the variable name (or static local's mangled name) directly
		// rather than generating a load that would lose the variable identity
		if ((unaryOperatorNode.op() == "++" || unaryOperatorNode.op() == "--" || unaryOperatorNode.op() == "&") && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(operandExpr)) {
				const IdentifierNode& identifier = std::get<IdentifierNode>(operandExpr);
				operandHandledAsIdentifier = tryBuildIdentifierOperand(identifier, operandIrOperands);
			}
		}

		// Special case: unary plus on lambda triggers decay to function pointer
		// Check if operand is a lambda expression before visiting it
		if (unaryOperatorNode.op() == "+" && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<LambdaExpressionNode>(operandExpr)) {
				const LambdaExpressionNode& lambda = std::get<LambdaExpressionNode>(operandExpr);
				
				// For non-capturing lambdas, unary plus triggers conversion to function pointer
				// This returns the address of the lambda's __invoke static function
				if (lambda.captures().empty()) {
					// Generate the lambda functions (operator(), __invoke, etc.)
					generateLambdaExpressionIr(lambda);
					
					// Return the address of the __invoke function
					TempVar func_addr_var = generateLambdaInvokeFunctionAddress(lambda);
					return { Type::FunctionPointer, 64, func_addr_var, 0ULL };
				}
				// For capturing lambdas, fall through to normal handling
				// (they cannot decay to function pointers)
			}
		}

		// Special handling for address-of non-static member: &Class::member
		// This should produce a pointer-to-member constant (member offset)
		if (!operandHandledAsIdentifier && unaryOperatorNode.op() == "&" && 
		    unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operand_expr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<QualifiedIdentifierNode>(operand_expr)) {
				const QualifiedIdentifierNode& qualIdNode = std::get<QualifiedIdentifierNode>(operand_expr);
				NamespaceHandle ns_handle = qualIdNode.namespace_handle();
				
				// Check if this is Class::member pattern (non-global namespace)
				if (!ns_handle.isGlobal()) {
					std::string_view class_name = gNamespaceRegistry.getName(ns_handle);
					std::string_view member_name = qualIdNode.name();
					
					// Look up the class type
					auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(class_name));
					if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
						TypeIndex struct_type_index = type_it->second->type_index_;
						// Try to find the member (non-static)
						auto member_result = FlashCpp::gLazyMemberResolver.resolve(
							struct_type_index,
							StringTable::getOrInternStringHandle(member_name));
						
						if (member_result) {
							// This is a pointer-to-member: return the member offset as a constant
							FLASH_LOG(Codegen, Debug, "Address-of non-static member '", class_name, "::", member_name, 
							          "' - returning offset ", member_result.adjusted_offset, " as pointer-to-member constant");
							
							// Return the offset directly as a constant value (no IR instruction needed)
							// This is a pointer-to-member constant - use 64-bit size and the member's type
							return { member_result.member->type, 64, static_cast<unsigned long long>(member_result.adjusted_offset), 
							         static_cast<unsigned long long>(member_result.member->type_index) };
						}
					}
				}
			}
		}

		if (!operandHandledAsIdentifier) {
			operandIrOperands = visitExpressionNode(unaryOperatorNode.get_operand().as<ExpressionNode>());
		}

		// Get the type of the operand
		Type operandType = std::get<Type>(operandIrOperands[0]);
		[[maybe_unused]] int operandSize = std::get<int>(operandIrOperands[1]);

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();

		// Generate the IR for the operation based on the operator
		if (unaryOperatorNode.op() == "!") {
			// Logical NOT - use UnaryOp struct
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};
			ir_.addInstruction(IrInstruction(IrOpcode::LogicalNot, unary_op, Token()));
			// Logical NOT always returns bool8
			return { Type::Bool, 8, result_var, 0ULL };
		}
		else if (unaryOperatorNode.op() == "~") {
			// Bitwise NOT - use UnaryOp struct
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};
			ir_.addInstruction(IrInstruction(IrOpcode::BitwiseNot, unary_op, Token()));
		}
		else if (unaryOperatorNode.op() == "-") {
			// Unary minus (negation) - use UnaryOp struct
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Negate, unary_op, Token()));
		}
		else if (unaryOperatorNode.op() == "+") {
			// Unary plus (no-op, just return the operand)
			return operandIrOperands;
		}
		else if (unaryOperatorNode.op() == "++") {
			// Increment operator (prefix or postfix)
			
			// Check if this is a pointer increment (requires pointer arithmetic)
			bool is_pointer = false;
			int element_size = 1;
			if (operandHandledAsIdentifier && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
				const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(operandExpr)) {
					const IdentifierNode& identifier = std::get<IdentifierNode>(operandExpr);
					auto symbol = symbol_table.lookup(identifier.name());
					if (symbol.has_value()) {
						const TypeSpecifierNode* type_node = nullptr;
						if (symbol->is<DeclarationNode>()) {
							type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
						} else if (symbol->is<VariableDeclarationNode>()) {
							type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
						} else {
							FLASH_LOG(Codegen, Error, "Could not type for identifier ", identifier.name());
							throw std::runtime_error("Invalid type node");
						}
						
						if (type_node->pointer_depth() > 0) {
							is_pointer = true;
							// Calculate element size for pointer arithmetic
							if (type_node->pointer_depth() > 1) {
								element_size = 8;  // Multi-level pointer: element is a pointer
							} else {
								// Single-level pointer: element size is sizeof(base_type)
								element_size = getSizeInBytes(type_node->type(), type_node->type_index(), type_node->size_in_bits());
							}
						}
					}
				}
			}
			
			// Use pointer-aware increment/decrement opcode
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};
			
			// Store element size for pointer arithmetic in the IR
			if (is_pointer) {
				// For pointers, we use a BinaryOp to add element_size instead
				// Use UnsignedLongLong for pointer arithmetic (pointers are 64-bit addresses)
			if (unaryOperatorNode.is_prefix()) {
				// ++ptr becomes: ptr = ptr + element_size
				BinaryOp add_op{
					.lhs = { Type::UnsignedLongLong, 64, std::holds_alternative<StringHandle>(operandIrOperands[2]) ? std::get<StringHandle>(operandIrOperands[2]) : IrValue{} },
					.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));					// Store back to the pointer variable
					if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
						AssignmentOp assign_op;
						assign_op.result = std::get<StringHandle>(operandIrOperands[2]);
						assign_op.lhs = { Type::UnsignedLongLong, 64, std::get<StringHandle>(operandIrOperands[2]) };
						assign_op.rhs = { Type::UnsignedLongLong, 64, result_var };
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
					}
					// Return pointer value (64-bit)
					return { operandType, 64, result_var, 0ULL };
				} else {
					// ptr++ (postfix): save old value, increment, return old value
					TempVar old_value = var_counter.next();
					
					// Save old value
					AssignmentOp save_op;
					if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
						save_op.result = old_value;
						save_op.lhs = { Type::UnsignedLongLong, 64, old_value };
						save_op.rhs = toTypedValue(operandIrOperands);
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(save_op), Token()));
					}
					
				// ptr = ptr + element_size
				BinaryOp add_op{
					.lhs = { Type::UnsignedLongLong, 64, std::holds_alternative<StringHandle>(operandIrOperands[2]) ? std::get<StringHandle>(operandIrOperands[2]) : IrValue{} },
					.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));					// Store back to the pointer variable
					if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
						AssignmentOp assign_op;
						assign_op.result = std::get<StringHandle>(operandIrOperands[2]);
						assign_op.lhs = { Type::UnsignedLongLong, 64, std::get<StringHandle>(operandIrOperands[2]) };
						assign_op.rhs = { Type::UnsignedLongLong, 64, result_var };
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
					}
					// Return old pointer value
					return { operandType, 64, old_value, 0ULL };
				}
			} else {
				// Regular integer increment
				if (unaryOperatorNode.is_prefix()) {
					// Prefix increment: ++x
					ir_.addInstruction(IrInstruction(IrOpcode::PreIncrement, unary_op, Token()));
				} else {
					// Postfix increment: x++
					ir_.addInstruction(IrInstruction(IrOpcode::PostIncrement, unary_op, Token()));
				}
			}
		}
		else if (unaryOperatorNode.op() == "--") {
			// Decrement operator (prefix or postfix)
			
			// Check if this is a pointer decrement (requires pointer arithmetic)
			bool is_pointer = false;
			int element_size = 1;
			if (operandHandledAsIdentifier && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
				const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(operandExpr)) {
					const IdentifierNode& identifier = std::get<IdentifierNode>(operandExpr);
					auto symbol = symbol_table.lookup(identifier.name());
					if (symbol.has_value()) {
						const TypeSpecifierNode* type_node = nullptr;
						if (symbol->is<DeclarationNode>()) {
							type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
						} else if (symbol->is<VariableDeclarationNode>()) {
							type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
						}
						
						if (type_node && type_node->pointer_depth() > 0) {
							is_pointer = true;
							// Calculate element size for pointer arithmetic
							if (type_node->pointer_depth() > 1) {
								element_size = 8;  // Multi-level pointer: element is a pointer
							} else {
								// Single-level pointer: element size is sizeof(base_type)
								element_size = getSizeInBytes(type_node->type(), type_node->type_index(), type_node->size_in_bits());
							}
						}
					}
				}
			}
			
			// Use pointer-aware decrement opcode
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};
			
			// Store element size for pointer arithmetic in the IR
			if (is_pointer) {
				// For pointers, we use a BinaryOp to subtract element_size instead
				// Use UnsignedLongLong for pointer arithmetic (pointers are 64-bit addresses)
			if (unaryOperatorNode.is_prefix()) {
				// --ptr becomes: ptr = ptr - element_size
				BinaryOp sub_op{
					.lhs = { Type::UnsignedLongLong, 64, std::holds_alternative<StringHandle>(operandIrOperands[2]) ? std::get<StringHandle>(operandIrOperands[2]) : IrValue{} },
					.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(IrOpcode::Subtract, std::move(sub_op), Token()));					// Store back to the pointer variable
					if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
						AssignmentOp assign_op;
						assign_op.result = std::get<StringHandle>(operandIrOperands[2]);
						assign_op.lhs = { Type::UnsignedLongLong, 64, std::get<StringHandle>(operandIrOperands[2]) };
						assign_op.rhs = { Type::UnsignedLongLong, 64, result_var };
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
					}
					// Return pointer value (64-bit)
					return { operandType, 64, result_var, 0ULL };
				} else {
					// ptr-- (postfix): save old value, decrement, return old value
					TempVar old_value = var_counter.next();
					
					// Save old value
					AssignmentOp save_op;
					if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
						save_op.result = old_value;
						save_op.lhs = { Type::UnsignedLongLong, 64, old_value };
						save_op.rhs = toTypedValue(operandIrOperands);
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(save_op), Token()));
					}
					
				// ptr = ptr - element_size
				BinaryOp sub_op{
					.lhs = { Type::UnsignedLongLong, 64, std::holds_alternative<StringHandle>(operandIrOperands[2]) ? std::get<StringHandle>(operandIrOperands[2]) : IrValue{} },
					.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(IrOpcode::Subtract, std::move(sub_op), Token()));					// Store back to the pointer variable
					if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
						AssignmentOp assign_op;
						assign_op.result = std::get<StringHandle>(operandIrOperands[2]);
						assign_op.lhs = { Type::UnsignedLongLong, 64, std::get<StringHandle>(operandIrOperands[2]) };
						assign_op.rhs = { Type::UnsignedLongLong, 64, result_var };
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
					}
					// Return old pointer value
					return { operandType, 64, old_value, 0ULL };
				}
			} else {
				// Regular integer decrement
				if (unaryOperatorNode.is_prefix()) {
					// Prefix decrement: --x
					ir_.addInstruction(IrInstruction(IrOpcode::PreDecrement, unary_op, Token()));
				} else {
					// Postfix decrement: x--
					ir_.addInstruction(IrInstruction(IrOpcode::PostDecrement, unary_op, Token()));
				}
			}
		}
		else if (unaryOperatorNode.op() == "&") {
			// Address-of operator: &x
			// Get the current pointer depth from operandIrOperands
			unsigned long long operand_ptr_depth = 0;
			if (operandIrOperands.size() >= 4 && std::holds_alternative<unsigned long long>(operandIrOperands[3])) {
				operand_ptr_depth = std::get<unsigned long long>(operandIrOperands[3]);
			}
			
			// Create typed payload with TypedValue
			AddressOfOp op;
			op.result = result_var;
			
			// Populate TypedValue with full type information
			op.operand.type = operandType;
			op.operand.size_in_bits = std::get<int>(operandIrOperands[1]);
			op.operand.pointer_depth = static_cast<int>(operand_ptr_depth);
			
			// Get the operand value - it's at index 2 in operandIrOperands
			if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
				op.operand.value = std::get<StringHandle>(operandIrOperands[2]);
			} else if (std::holds_alternative<TempVar>(operandIrOperands[2])) {
				op.operand.value = std::get<TempVar>(operandIrOperands[2]);
			} else {
				throw std::runtime_error("AddressOf operand must be StringHandle or TempVar");
			}
			
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, op, Token()));
			// Return 64-bit pointer with incremented pointer depth
			return { operandType, 64, result_var, operand_ptr_depth + 1 };
		}
		else if (unaryOperatorNode.op() == "*") {
			// Dereference operator: *x
			// When dereferencing a pointer, the result size depends on the pointer depth:
			// - For single pointer (int*), result is the base type size (e.g., 32 for int)
			// - For multi-level pointer (int**), result is still a pointer (64 bits)
			
			// For LValueAddress context (e.g., assignment LHS like *ptr = value),
			// we need to return operands with lvalue metadata so handleLValueAssignment
			// can detect this is a dereference store.
			if (context == ExpressionContext::LValueAddress) {
				// Get the element size (what we're storing to)
				int element_size = 64; // Default to pointer size
				int pointer_depth = 0;
				
				// Get pointer depth
				if (operandIrOperands.size() >= 4 && std::holds_alternative<unsigned long long>(operandIrOperands[3])) {
					pointer_depth = static_cast<int>(std::get<unsigned long long>(operandIrOperands[3]));
				} else if (unaryOperatorNode.get_operand().is<ExpressionNode>()) {
					const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(operandExpr)) {
						const IdentifierNode& identifier = std::get<IdentifierNode>(operandExpr);
						auto symbol = symbol_table.lookup(identifier.name());
						const DeclarationNode* decl = getDeclarationFromSymbol(symbol);
						if (decl) {
							pointer_depth = decl->type_node().as<TypeSpecifierNode>().pointer_depth();
						}
					}
				}
				
				// Calculate element size after dereference
				if (pointer_depth <= 1) {
					element_size = get_type_size_bits(operandType);
					if (element_size == 0) {
						element_size = 64;  // Default to pointer size for unknown types
					}
				}
				
				// Create a TempVar with Indirect lvalue metadata
				// This allows handleLValueAssignment to recognize this as a dereference store
				TempVar lvalue_temp = var_counter.next();
				
				// Extract the pointer base (StringHandle or TempVar)
				std::variant<StringHandle, TempVar> base;
				if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
					base = std::get<StringHandle>(operandIrOperands[2]);
				} else if (std::holds_alternative<TempVar>(operandIrOperands[2])) {
					base = std::get<TempVar>(operandIrOperands[2]);
				} else {
					// Fall back to old behavior if we can't extract base
					// This can happen with complex expressions that don't have a simple base
					FLASH_LOG(Codegen, Debug, "Dereference LValueAddress fallback: operand is not StringHandle or TempVar");
					return operandIrOperands;
				}
				
				// Emit assignment to copy the pointer value into lvalue_temp.
				// This is needed for reference initialization from *ptr (e.g., int& x = *__begin;).
				// The reference init code reads the TempVar's stack value; without this
				// assignment the slot would be uninitialized.
				IrValue rhs_value;
				if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
					rhs_value = std::get<StringHandle>(operandIrOperands[2]);
				} else if (std::holds_alternative<TempVar>(operandIrOperands[2])) {
					rhs_value = std::get<TempVar>(operandIrOperands[2]);
				} else if (std::holds_alternative<unsigned long long>(operandIrOperands[2])) {
					rhs_value = std::get<unsigned long long>(operandIrOperands[2]);
				} else {
					rhs_value = 0ULL;
				}
				AssignmentOp copy_op;
				copy_op.result = lvalue_temp;
				copy_op.lhs = TypedValue{operandType, 64, lvalue_temp};
				copy_op.rhs = TypedValue{operandType, 64, rhs_value};
				copy_op.is_pointer_store = false;
				copy_op.dereference_rhs_references = false;
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(copy_op), Token()));

				// Set lvalue metadata with Indirect kind (dereference)
				LValueInfo lvalue_info(
					LValueInfo::Kind::Indirect,
					base,
					0  // offset is 0 for simple dereference
				);
				setTempVarMetadata(lvalue_temp, TempVarMetadata::makeLValue(lvalue_info));
				
				// Return with TempVar that has the lvalue metadata.
				// The TempVar holds a 64-bit pointer (the address this lvalue refers to).
				unsigned long long result_ptr_depth = (pointer_depth > 0) ? (pointer_depth - 1) : 0;
				return { operandType, 64, lvalue_temp, result_ptr_depth };
			}
			
			int element_size = 64; // Default to pointer size
			int pointer_depth = 0;
			
			// First, try to get pointer depth from operandIrOperands (for TempVar results from previous operations)
			if (operandIrOperands.size() >= 4 && std::holds_alternative<unsigned long long>(operandIrOperands[3])) {
				pointer_depth = static_cast<int>(std::get<unsigned long long>(operandIrOperands[3]));
			}
			// Otherwise, look up the pointer operand to determine its pointer depth from symbol table
			else if (unaryOperatorNode.get_operand().is<ExpressionNode>()) {
				const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(operandExpr)) {
					const IdentifierNode& identifier = std::get<IdentifierNode>(operandExpr);
					auto symbol = symbol_table.lookup(identifier.name());
					if (symbol.has_value()) {
						const TypeSpecifierNode* type_node = nullptr;
						if (symbol->is<DeclarationNode>()) {
							type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
						} else if (symbol->is<VariableDeclarationNode>()) {
							type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
						}
						if (type_node) {
							pointer_depth = type_node->pointer_depth();
						}
					}
				}
			}
			
			// After dereferencing, pointer_depth decreases by 1
			// If still > 0, result is a pointer (64 bits)
			// If == 0, result is the base type
			if (pointer_depth <= 1) {
				// Single-level pointer or less: result is base type size
				switch (operandType) {
					case Type::Bool: element_size = 8; break;
					case Type::Char: element_size = 8; break;
					case Type::Short: element_size = 16; break;
					case Type::Int: element_size = 32; break;
					case Type::Long: element_size = 64; break;
					case Type::Float: element_size = 32; break;
					case Type::Double: element_size = 64; break;
					default: element_size = 64; break;  // Fallback for unknown types
				}
			}
			// else: multi-level pointer, element_size stays 64 (pointer)
		
			// Create typed payload with TypedValue
			DereferenceOp op;
			op.result = result_var;
			
			// Populate TypedValue with full type information
			op.pointer.type = operandType;
			// Use element_size as pointee size so IRConverter can load correct width
			op.pointer.size_in_bits = element_size;
			op.pointer.pointer_depth = pointer_depth;
			
			// Get the pointer value - it's at index 2 in operandIrOperands
			if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
				op.pointer.value = std::get<StringHandle>(operandIrOperands[2]);
			} else if (std::holds_alternative<TempVar>(operandIrOperands[2])) {
				op.pointer.value = std::get<TempVar>(operandIrOperands[2]);
			} else {
				throw std::runtime_error("Dereference pointer must be StringHandle or TempVar");
			}
		
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, op, Token()));
			
			// Mark dereference result as lvalue (Option 2: Value Category Tracking)
			// *ptr is an lvalue - it designates the dereferenced object
			// Extract StringHandle or TempVar from pointer.value (IrValue)
			std::variant<StringHandle, TempVar> base;
			if (std::holds_alternative<StringHandle>(op.pointer.value)) {
				base = std::get<StringHandle>(op.pointer.value);
			} else if (std::holds_alternative<TempVar>(op.pointer.value)) {
				base = std::get<TempVar>(op.pointer.value);
			}
			LValueInfo lvalue_info(
				LValueInfo::Kind::Indirect,
				base,
				0  // offset is 0 for simple dereference
			);
			setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));
		
			// Return the dereferenced value with the decremented pointer depth
			unsigned long long result_ptr_depth = (pointer_depth > 0) ? (pointer_depth - 1) : 0;
			return { operandType, element_size, result_var, result_ptr_depth };
		}
		else {
			throw std::runtime_error("Unary operator not implemented yet");
		}

		// Return the result
		return { operandType, std::get<int>(operandIrOperands[1]), result_var, 0ULL };
	}

	std::vector<IrOperand> generateTernaryOperatorIr(const TernaryOperatorNode& ternaryNode) {
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
	
		// Generate conditional branch: if condition true goto true_label, else goto false_label
		CondBranchOp cond_branch;
		cond_branch.label_true = true_label;
		cond_branch.label_false = false_label;
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), ternaryNode.get_token()));

		// True branch label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = true_label}, ternaryNode.get_token()));
		
		// Evaluate true expression
		auto true_operands = visitExpressionNode(ternaryNode.true_expr().as<ExpressionNode>());
		
		// Create result variable to hold the final value
		TempVar result_var = var_counter.next();
		Type result_type = std::get<Type>(true_operands[0]);
		int result_size = std::get<int>(true_operands[1]);
		
		// Assign true_expr result to result variable
		AssignmentOp assign_true_op;
		assign_true_op.result = result_var;
		assign_true_op.lhs.type = result_type;
		assign_true_op.lhs.size_in_bits = result_size;
		assign_true_op.lhs.value = result_var;
		assign_true_op.rhs = toTypedValue(true_operands);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_true_op), ternaryNode.get_token()));
		
		// Unconditional branch to end
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = end_label}, ternaryNode.get_token()));

		// False branch label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = false_label}, ternaryNode.get_token()));
	
		// Evaluate false expression
		auto false_operands = visitExpressionNode(ternaryNode.false_expr().as<ExpressionNode>());

		// Assign false_expr result to result variable
		AssignmentOp assign_false_op;
		assign_false_op.result = result_var;
		assign_false_op.lhs.type = result_type;
		assign_false_op.lhs.size_in_bits = result_size;
		assign_false_op.lhs.value = result_var;
		assign_false_op.rhs = toTypedValue(false_operands);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_false_op), ternaryNode.get_token()));
		
		// End label (merge point)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = end_label}, ternaryNode.get_token()));
		
		// Return the result variable
		return { result_type, result_size, result_var, 0ULL };
	}

	std::vector<IrOperand> generateBinaryOperatorIr(const BinaryOperatorNode& binaryOperatorNode) {
		std::vector<IrOperand> irOperands;

		const auto& op = binaryOperatorNode.op();
		static const std::unordered_set<std::string_view> compound_assignment_ops = {
			"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
		};

		// Special handling for comma operator
		// The comma operator evaluates both operands left-to-right and returns the right operand
		if (op == ",") {
			// Generate IR for the left-hand side (evaluate for side effects, discard result)
			auto lhsIrOperands = visitExpressionNode(binaryOperatorNode.get_lhs().as<ExpressionNode>());

			// Generate IR for the right-hand side (this is the result)
			auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

			// Return the right-hand side result
			return rhsIrOperands;
		}

		// Special handling for assignment to array subscript or member access
		// Use LValueAddress context to avoid redundant Load instructions
		if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			
			// Check if LHS is an array subscript or member access (lvalue expressions)
			if (std::holds_alternative<ArraySubscriptNode>(lhs_expr) || 
			    std::holds_alternative<MemberAccessNode>(lhs_expr)) {
				
				// Evaluate LHS with LValueAddress context (no Load instruction)
				auto lhsIrOperands = visitExpressionNode(lhs_expr, ExpressionContext::LValueAddress);
				
				// Safety check: if LHS evaluation failed or returned invalid size, fall through to legacy code
				bool use_unified_handler = !lhsIrOperands.empty();
				if (use_unified_handler && lhsIrOperands.size() >= 2) {
					int lhs_size = std::get<int>(lhsIrOperands[1]);
					if (lhs_size <= 0 || lhs_size > 1024) {
						FLASH_LOG(Codegen, Info, "Unified handler skipped: invalid size (", lhs_size, ")");
						use_unified_handler = false;  // Invalid size, use legacy code
					}
				} else {
					FLASH_LOG(Codegen, Info, "Unified handler skipped: empty or insufficient operands");
					use_unified_handler = false;
				}
				
				if (use_unified_handler) {
					// Evaluate RHS normally (Load context)
					auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());
					
					// Try to handle assignment using unified lvalue metadata handler
					if (handleLValueAssignment(lhsIrOperands, rhsIrOperands, binaryOperatorNode.get_token())) {
						// Assignment was handled successfully via metadata
						FLASH_LOG(Codegen, Info, "Unified handler SUCCESS for array/member assignment");
						return rhsIrOperands;
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
				auto type_it = gTypesByName.find(current_struct_name_);
				if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
					TypeIndex struct_type_index = type_it->second->type_index_;
					auto member_result = FlashCpp::gLazyMemberResolver.resolve(struct_type_index, StringTable::getOrInternStringHandle(std::string(lhs_name)));
					if (member_result) {
						// This is an assignment to a member variable: member = value
						// Handle via unified handler (identifiers are now marked as lvalues)
						auto lhsIrOperands = visitExpressionNode(lhs_expr);
						auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());
						
						// Handle assignment using unified lvalue metadata handler
						if (handleLValueAssignment(lhsIrOperands, rhsIrOperands, binaryOperatorNode.get_token())) {
							// Assignment was handled successfully via metadata
							FLASH_LOG(Codegen, Debug, "Unified handler SUCCESS for implicit member assignment (", lhs_name, ")");
							return rhsIrOperands;
						}
						
						// This shouldn't happen with proper metadata, but log for debugging
						FLASH_LOG(Codegen, Error, "Unified handler unexpectedly failed for implicit member assignment: ", lhs_name);
						return { Type::Int, 32, TempVar{0} };
					}
				}
			}
		}

		// Special handling for assignment to captured-by-reference variable inside lambda
		// Now that captured-by-reference identifiers are marked with lvalue metadata, use unified handler
		if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>() && current_lambda_context_.isActive()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
				const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
				std::string_view lhs_name = lhs_ident.name();
				StringHandle lhs_name_str = StringTable::getOrInternStringHandle(lhs_name);

				// Check if this is a captured-by-reference variable
				auto capture_it = current_lambda_context_.captures.find(lhs_name_str);
				if (capture_it != current_lambda_context_.captures.end()) {
					auto kind_it = current_lambda_context_.capture_kinds.find(lhs_name_str);
					if (kind_it != current_lambda_context_.capture_kinds.end() &&
					    kind_it->second == LambdaCaptureNode::CaptureKind::ByReference) {
						// This is assignment to a captured-by-reference variable
						// Handle via unified handler (identifiers are now marked as lvalues)
						auto lhsIrOperands = visitExpressionNode(lhs_expr);
						auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());
						
						// Handle assignment using unified lvalue metadata handler
						if (handleLValueAssignment(lhsIrOperands, rhsIrOperands, binaryOperatorNode.get_token())) {
							// Assignment was handled successfully via metadata
							FLASH_LOG(Codegen, Debug, "Unified handler SUCCESS for captured-by-reference assignment (", lhs_name, ")");
							return rhsIrOperands;
						}
						
						// This shouldn't happen with proper metadata, but log for debugging
						FLASH_LOG(Codegen, Error, "Unified handler unexpectedly failed for captured-by-reference assignment: ", lhs_name);
						return { Type::Int, 32, TempVar{0} };
					}
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
						auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

						// Generate Assignment IR using typed payload
						TempVar result_var = var_counter.next();
						AssignmentOp assign_op;
						assign_op.result = result_var;
						assign_op.lhs.type = lhs_type.type();
						assign_op.lhs.size_in_bits = static_cast<int>(lhs_type.size_in_bits());
						assign_op.lhs.value = StringTable::getOrInternStringHandle(lhs_name);
						assign_op.rhs = toTypedValue(rhsIrOperands);
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
						
						// Return the result
						return { lhs_type.type(), static_cast<int>(lhs_type.size_in_bits()), result_var, 0ULL };
					}
				}
			}
		}

		// Special handling for global variable and static local variable assignment
		if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
				const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
				std::string_view lhs_name = lhs_ident.name();

				// Check if this is a static local variable
				StringHandle lhs_handle = StringTable::getOrInternStringHandle(lhs_name);
				auto static_local_it = static_local_names_.find(lhs_handle);
				bool is_static_local = (static_local_it != static_local_names_.end());
				
				// Check if this is a global variable (not found in local symbol table, but found in global)
				const std::optional<ASTNode> local_symbol = symbol_table.lookup(lhs_name);
				bool is_global = false;
				
				if (!local_symbol.has_value() && global_symbol_table_) {
					// Not found locally - check global symbol table
					const std::optional<ASTNode> global_symbol = global_symbol_table_->lookup(lhs_name);
					if (global_symbol.has_value() && global_symbol->is<VariableDeclarationNode>()) {
						is_global = true;
					}
				}
				
				if (is_global || is_static_local) {
					// This is a global variable or static local assignment - generate GlobalStore instruction
					// Generate IR for the RHS
					auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

					// Generate GlobalStore IR: global_store @global_name, %value
					std::vector<IrOperand> store_operands;
					// For static locals, use the mangled name; for globals, use the simple name
					if (is_static_local) {
						store_operands.emplace_back(static_local_it->second.mangled_name);  // mangled name for static local
					} else {
						store_operands.emplace_back(StringTable::getOrInternStringHandle(lhs_name));  // simple name for global
					}
					
					// Extract the value from RHS (rhsIrOperands[2])
					if (std::holds_alternative<TempVar>(rhsIrOperands[2])) {
						store_operands.emplace_back(std::get<TempVar>(rhsIrOperands[2]));
					} else if (std::holds_alternative<StringHandle>(rhsIrOperands[2])
					        || std::holds_alternative<unsigned long long>(rhsIrOperands[2])
					        || std::holds_alternative<double>(rhsIrOperands[2])) {
						// Local variable (StringHandle) or constant: load into a temp first
						TempVar temp = var_counter.next();
						AssignmentOp assign_op;
						assign_op.result = temp;
						assign_op.lhs.type = std::get<Type>(rhsIrOperands[0]);
						assign_op.lhs.size_in_bits = std::get<int>(rhsIrOperands[1]);
						assign_op.lhs.value = temp;
						assign_op.rhs = toTypedValue(rhsIrOperands);
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
						store_operands.emplace_back(temp);
					} else {
						FLASH_LOG(Codegen, Error, "GlobalStore: unsupported RHS IrOperand type");
						return {};
					}

					ir_.addInstruction(IrOpcode::GlobalStore, std::move(store_operands), binaryOperatorNode.get_token());

					// Return the RHS value as the result (assignment expression returns the assigned value)
					return rhsIrOperands;
				}
			}
		}

		// Special handling for compound assignment to array subscript or member access
		// Use LValueAddress context for the LHS, similar to regular assignment
		// Helper lambda to check if operator is a compound assignment
		if (compound_assignment_ops.count(op) > 0 &&
		    binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			
			// Check if LHS is an array subscript or member access (lvalue expressions)
			if (std::holds_alternative<ArraySubscriptNode>(lhs_expr) || 
			    std::holds_alternative<MemberAccessNode>(lhs_expr)) {
				
				// Evaluate LHS with LValueAddress context (no Load instruction)
				auto lhsIrOperands = visitExpressionNode(lhs_expr, ExpressionContext::LValueAddress);
				
				// Safety check
				bool use_unified_handler = !lhsIrOperands.empty();
				if (use_unified_handler && lhsIrOperands.size() >= 2) {
					int lhs_size = std::get<int>(lhsIrOperands[1]);
					if (lhs_size <= 0 || lhs_size > 1024) {
						FLASH_LOG(Codegen, Info, "Compound assignment unified handler skipped: invalid size (", lhs_size, ")");
						use_unified_handler = false;
					}
				} else {
					FLASH_LOG(Codegen, Info, "Compound assignment unified handler skipped: empty or insufficient operands");
					use_unified_handler = false;
				}
				
				if (use_unified_handler) {
					// Evaluate RHS normally (Load context)
					auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());
					
					// For compound assignments, we need to:
					// 1. Load the current value from the lvalue
					// 2. Perform the operation (add, subtract, etc.)
					// 3. Store the result back to the lvalue
					
					// Try to handle compound assignment using lvalue metadata
					if (handleLValueCompoundAssignment(lhsIrOperands, rhsIrOperands, binaryOperatorNode.get_token(), op)) {
						// Compound assignment was handled successfully via metadata
						FLASH_LOG(Codegen, Info, "Unified handler SUCCESS for array/member compound assignment");
						// Return the LHS operands which contain the result type/size info
						// The actual result value is stored in the lvalue, so we return lvalue info
						return lhsIrOperands;
					}
					
					// If metadata handler didn't work, fall through to legacy code
					FLASH_LOG(Codegen, Info, "Compound assignment unified handler returned false, falling through to legacy code");
				}
			}
		}

		// Generate IR for the left-hand side and right-hand side of the operation
		// For assignment (=), use LValueAddress context for LHS to avoid dereferencing reference parameters
		ExpressionContext lhs_context = (op == "=") ? ExpressionContext::LValueAddress : ExpressionContext::Load;
		auto lhsIrOperands = visitExpressionNode(binaryOperatorNode.get_lhs().as<ExpressionNode>(), lhs_context);
		auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

		// Try unified metadata-based handler for compound assignments on identifiers
		// This ensures implicit member accesses (including [*this] lambdas) use the correct base object
		if (compound_assignment_ops.count(op) > 0 &&
		    handleLValueCompoundAssignment(lhsIrOperands, rhsIrOperands, binaryOperatorNode.get_token(), op)) {
			FLASH_LOG(Codegen, Info, "Unified handler SUCCESS for compound assignment");
			return lhsIrOperands;
		}

		// Try unified lvalue-based assignment handler (uses value category metadata)
		// This handles assignments like *ptr = value using lvalue metadata
		if (op == "=" && handleLValueAssignment(lhsIrOperands, rhsIrOperands, binaryOperatorNode.get_token())) {
			// Assignment was handled via lvalue metadata, return RHS as result
			return rhsIrOperands;
		}

		// Get the types and sizes of the operands
		Type lhsType = std::get<Type>(lhsIrOperands[0]);
		Type rhsType = std::get<Type>(rhsIrOperands[0]);
		int lhsSize = std::get<int>(lhsIrOperands[1]);
		int rhsSize = std::get<int>(rhsIrOperands[1]);

		// Special handling for struct assignment with user-defined operator=(non-struct) 
		// This handles patterns like: struct_var = primitive_value
		// where struct has operator=(int), operator=(double), etc.
		if (op == "=" && lhsType == Type::Struct && rhsType != Type::Struct && lhsIrOperands.size() >= 4) {
			// Get the type index of the struct
			TypeIndex lhs_type_index = 0;
			if (std::holds_alternative<unsigned long long>(lhsIrOperands[3])) {
				lhs_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(lhsIrOperands[3]));
			}
			
			if (lhs_type_index > 0 && lhs_type_index < gTypeInfo.size()) {
				// Check for user-defined operator= that takes the RHS type
				auto overload_result = findBinaryOperatorOverload(lhs_type_index, 0, "=");
				
				if (overload_result.has_overload) {
					const StructMemberFunction& member_func = *overload_result.member_overload;
					const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
					
					// Check if the parameter type matches RHS type
					const auto& param_nodes = func_decl.parameter_nodes();
					if (!param_nodes.empty() && param_nodes[0].is<DeclarationNode>()) {
						const auto& param_decl = param_nodes[0].as<DeclarationNode>();
						const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
						
						// Check if parameter is a primitive type matching RHS
						if (param_type.type() != Type::Struct && param_type.type() != Type::UserDefined) {
							// Found matching operator=(primitive_type)! Generate function call
							FLASH_LOG_FORMAT(Codegen, Debug, "Found operator= with primitive param for struct type index {}", lhs_type_index);
							
							std::string_view struct_name = StringTable::getStringView(gTypeInfo[lhs_type_index].name());
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
								Linkage::CPlusPlus
							);
							
							TempVar result_var = var_counter.next();
							
							// Take address of LHS to pass as 'this' pointer
							std::variant<StringHandle, TempVar> lhs_value;
							if (std::holds_alternative<StringHandle>(lhsIrOperands[2])) {
								lhs_value = std::get<StringHandle>(lhsIrOperands[2]);
							} else if (std::holds_alternative<TempVar>(lhsIrOperands[2])) {
								lhs_value = std::get<TempVar>(lhsIrOperands[2]);
							} else {
								FLASH_LOG(Codegen, Error, "Cannot take address of operator= LHS - not an lvalue");
								return {};
							}
							
							TempVar lhs_addr = var_counter.next();
							AddressOfOp addr_op;
							addr_op.result = lhs_addr;
							addr_op.operand.type = lhsType;
							addr_op.operand.size_in_bits = lhsSize;
							addr_op.operand.pointer_depth = 0;
							std::visit([&addr_op](auto&& val) { addr_op.operand.value = val; }, lhs_value);
							ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), binaryOperatorNode.get_token()));
							
							// Generate function call
							CallOp call_op;
							call_op.result = result_var;
							call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
							
							// Pass 'this' pointer as first argument
							TypedValue this_arg;
							this_arg.type = lhsType;
							this_arg.size_in_bits = 64;  // 'this' is always a pointer (64-bit)
							this_arg.value = lhs_addr;
							call_op.args.push_back(this_arg);
							
							// Pass RHS value as second argument
							call_op.args.push_back(toTypedValue(rhsIrOperands));
							
							call_op.return_type = return_type.type();
							call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
							
							ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), binaryOperatorNode.get_token()));
							
							// Return result
							return { return_type.type(), static_cast<int>(return_type.size_in_bits()), result_var, 0ULL };
						}
					}
				}
			}
		}

		// Check for binary operator overloads on struct types
		// Binary operators like +, -, *, etc. can be overloaded as member functions
		// This should be checked before trying to generate built-in arithmetic operations
		if (lhsType == Type::Struct && lhsIrOperands.size() >= 4) {
			// Get the type index of the left operand
			TypeIndex lhs_type_index = 0;
			if (std::holds_alternative<unsigned long long>(lhsIrOperands[3])) {
				lhs_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(lhsIrOperands[3]));
			}
			
			// Get the type index of the right operand (if it's a struct)
			TypeIndex rhs_type_index = 0;
			if (rhsType == Type::Struct && rhsIrOperands.size() >= 4) {
				if (std::holds_alternative<unsigned long long>(rhsIrOperands[3])) {
					rhs_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(rhsIrOperands[3]));
				}
			}
			
			// List of binary operators that can be overloaded
			// Skip assignment operators (=, +=, -=, etc.) as they are handled separately
			static const std::unordered_set<std::string_view> overloadable_binary_ops = {
				"+", "-", "*", "/", "%",           // Arithmetic
				"==", "!=", "<", ">", "<=", ">=",  // Comparison
				"&&", "||",                        // Logical
				"&", "|", "^",                     // Bitwise
				"<<", ">>",                        // Shift
				",",                               // Comma (already handled above)
				"<=>",                             // Spaceship (handled below)
			};
			
			if (overloadable_binary_ops.count(op) > 0 && lhs_type_index > 0) {
				// Check for operator overload
				auto overload_result = findBinaryOperatorOverload(lhs_type_index, rhs_type_index, op);
				
				if (overload_result.has_overload) {
					// Found an overload! Generate a member function call instead of built-in operation
					FLASH_LOG_FORMAT(Codegen, Debug, "Resolving binary operator{} overload for type index {}", 
					         op, lhs_type_index);
					
					const StructMemberFunction& member_func = *overload_result.member_overload;
					const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
					
					// Get struct name for mangling
					std::string_view struct_name = StringTable::getStringView(gTypeInfo[lhs_type_index].name());
					
					// Get the return type from the function declaration
					const TypeSpecifierNode& return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
					
					// Get the parameter types for mangling
					std::vector<TypeSpecifierNode> param_types;
					for (const auto& param_node : func_decl.parameter_nodes()) {
						if (param_node.is<DeclarationNode>()) {
							const auto& param_decl = param_node.as<DeclarationNode>();
							const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
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
						Linkage::CPlusPlus
					);
					
					// Generate the call to the operator overload
					// For member function: a.operator+(b) where 'a' is 'this' and 'b' is the parameter
					TempVar result_var = var_counter.next();
					
					// Take address of LHS to pass as 'this' pointer
					// The LHS operand contains a struct value - extract it properly
					std::variant<StringHandle, TempVar> lhs_value;
					if (std::holds_alternative<StringHandle>(lhsIrOperands[2])) {
						lhs_value = std::get<StringHandle>(lhsIrOperands[2]);
					} else if (std::holds_alternative<TempVar>(lhsIrOperands[2])) {
						lhs_value = std::get<TempVar>(lhsIrOperands[2]);
					} else {
						// Can't take address of non-lvalue
						FLASH_LOG(Codegen, Error, "Cannot take address of binary operator LHS - not an lvalue");
						return {};
					}
					
					TempVar lhs_addr = var_counter.next();
					AddressOfOp addr_op;
					addr_op.result = lhs_addr;
					addr_op.operand.type = lhsType;
					addr_op.operand.size_in_bits = lhsSize;
					addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
					// Convert std::variant<StringHandle, TempVar> to IrValue
					if (std::holds_alternative<StringHandle>(lhs_value)) {
						addr_op.operand.value = std::get<StringHandle>(lhs_value);
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
					Type resolved_return_type = return_type.type();
					int actual_return_size = static_cast<int>(return_type.size_in_bits());
					if (resolved_return_type == Type::Auto && op == "<=>") {
						resolved_return_type = Type::Int;
						actual_return_size = 32;
					}
					if (actual_return_size == 0 && resolved_return_type == Type::Struct && return_type.type_index() > 0) {
						// Look up struct size from type info
						if (return_type.type_index() < gTypeInfo.size() && gTypeInfo[return_type.type_index()].struct_info_) {
							actual_return_size = static_cast<int>(gTypeInfo[return_type.type_index()].struct_info_->total_size * 8);
						}
					}
					call_op.return_type = resolved_return_type;
					call_op.return_type_index = return_type.type_index();
					call_op.return_size_in_bits = actual_return_size;
					call_op.is_member_function = true;  // This is a member function call
					
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
					this_arg.type = lhsType;
					this_arg.size_in_bits = 64;  // 'this' is always a pointer (64-bit)
					this_arg.value = lhs_addr;
					call_op.args.push_back(this_arg);
					
					// Add RHS as the second argument
					// Check if the parameter is a reference - if so, we need to pass the address
					if (!param_types.empty() && param_types[0].is_reference()) {
						// Parameter is a reference - we need to pass the address of RHS
						std::variant<StringHandle, TempVar> rhs_value;
						if (std::holds_alternative<StringHandle>(rhsIrOperands[2])) {
							rhs_value = std::get<StringHandle>(rhsIrOperands[2]);
						} else if (std::holds_alternative<TempVar>(rhsIrOperands[2])) {
							rhs_value = std::get<TempVar>(rhsIrOperands[2]);
						} else {
							// Can't take address of non-lvalue
							FLASH_LOG(Codegen, Error, "Cannot take address of binary operator RHS - not an lvalue");
							return {};
						}
						
						TempVar rhs_addr = var_counter.next();
						AddressOfOp rhs_addr_op;
						rhs_addr_op.result = rhs_addr;
						rhs_addr_op.operand.type = rhsType;
						rhs_addr_op.operand.size_in_bits = rhsSize;
						rhs_addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
						// Convert std::variant<StringHandle, TempVar> to IrValue
						if (std::holds_alternative<StringHandle>(rhs_value)) {
							rhs_addr_op.operand.value = std::get<StringHandle>(rhs_value);
						} else {
							rhs_addr_op.operand.value = std::get<TempVar>(rhs_value);
						}
						ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(rhs_addr_op), binaryOperatorNode.get_token()));
						
						// Create TypedValue with the address
						TypedValue rhs_arg;
						rhs_arg.type = rhsType;
						rhs_arg.size_in_bits = 64;  // Reference is a pointer (64-bit)
						rhs_arg.value = rhs_addr;
						call_op.args.push_back(rhs_arg);
					} else {
						// Parameter is not a reference - pass the value directly
						call_op.args.push_back(toTypedValue(rhsIrOperands));
					}
					
					ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), binaryOperatorNode.get_token()));
					
					// Return the result with resolved types
					return {resolved_return_type, actual_return_size, result_var, 
					        return_type.type_index()};
				}
			}
		}

		// Special handling for spaceship operator <=> on struct types
		// This should be converted to a member function call: lhs.operator<=>(rhs)
		FLASH_LOG_FORMAT(Codegen, Debug, "Binary operator check: op='{}', lhsType={}", op, static_cast<int>(lhsType));
		
		if (op == "<=>") {
			FLASH_LOG_FORMAT(Codegen, Debug, "Spaceship operator detected: lhsType={}, is_struct={}", 
				static_cast<int>(lhsType), lhsType == Type::Struct);
			
			// Check if LHS is a struct type
			if (lhsType == Type::Struct && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
				const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
				
				// Get the LHS value - can be an identifier, member access, or other expression
				std::variant<StringHandle, TempVar> lhs_value;
				TypeIndex lhs_type_index = 0;
				
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
						lhs_type_index = type_node.type_index();
					} else if (symbol && symbol->is<DeclarationNode>()) {
						const auto& decl = symbol->as<DeclarationNode>();
						const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
						lhs_type_index = type_node.type_index();
					} else {
						// Can't find the variable declaration
						return {};
					}
				} else if (std::holds_alternative<MemberAccessNode>(lhs_expr)) {
					// Member access case: p.member <=> q.member
					const auto& member_access = std::get<MemberAccessNode>(lhs_expr);
					
					// Generate IR for the member access expression
					std::vector<IrOperand> member_ir = generateMemberAccessIr(member_access);
					if (member_ir.empty() || member_ir.size() < 4) {
						return {};
					}
					
					// Extract the result temp var and type index
					lhs_value = std::get<TempVar>(member_ir[2]);
					lhs_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(member_ir[3]));
				} else {
					// Other expression types - use already-generated lhsIrOperands
					// The lhsIrOperands were already generated earlier in this function
					if (lhsIrOperands.size() >= 3 && std::holds_alternative<TempVar>(lhsIrOperands[2])) {
						lhs_value = std::get<TempVar>(lhsIrOperands[2]);
					} else {
						// Complex expression that doesn't produce a temp var
						return {};
					}
					
					// Try to get type index from lhsIrOperands if available
					if (lhsIrOperands.size() >= 4 && std::holds_alternative<unsigned long long>(lhsIrOperands[3])) {
						lhs_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(lhsIrOperands[3]));
					} else {
						// Can't determine type index for complex expression
						return {};
					}
				}
				
				// Look up the operator<=> function in the struct
				if (lhs_type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[lhs_type_index];
					if (type_info.struct_info_) {
						const StructTypeInfo& struct_info = *type_info.struct_info_;
						
						// Find operator<=> in member functions
						const StructMemberFunction* spaceship_op = nullptr;
						for (const auto& func : struct_info.member_functions) {
							if (func.is_operator_overload && func.operator_symbol == "<=>") {
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
							Type return_type = return_type_node.type();
							int return_size = static_cast<int>(return_type_node.size_in_bits());
							
							// Defaulted operator<=> with auto return type actually returns int
							if (return_type == Type::Auto) {
								return_type = Type::Int;
								return_size = 32;
							}
							
							// Generate mangled name for the operator<=> call
							std::vector<TypeSpecifierNode> param_types;
							for (const auto& param_node : func_decl.parameter_nodes()) {
								if (param_node.is<DeclarationNode>()) {
									const auto& param_decl = param_node.as<DeclarationNode>();
									const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
									param_types.push_back(param_type);
								}
							}
							
							std::string_view mangled_name = generateMangledNameForCall(
								"operator<=>",
								return_type_node,
								param_types,
								false, // not variadic
								StringTable::getStringView(type_info.name())
							);
							
							// Create the call operation
							CallOp call_op;
							call_op.result = result_var;
							call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
							call_op.return_type = return_type;
							call_op.return_size_in_bits = return_size;
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
							lhs_arg.type = lhsType;
							lhs_arg.size_in_bits = lhsSize;
							// Convert lhs_value (which can be string_view or TempVar) to IrValue
							if (std::holds_alternative<StringHandle>(lhs_value)) {
								lhs_arg.value = IrValue(std::get<StringHandle>(lhs_value));
							} else {
								lhs_arg.value = IrValue(std::get<TempVar>(lhs_value));
							}
							call_op.args.push_back(lhs_arg);
						
							// Add the RHS as the second argument
							// Check if parameter expects a reference
							TypedValue rhs_arg = toTypedValue(rhsIrOperands);
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
							
							// Return the result
							return { return_type, return_size, result_var, 0ULL };
						}
					}
				}
			}
			
			// If we get here, operator<=> is not defined or not found
			// Fall through to error handling
		}

		// Try to get pointer depth for pointer arithmetic
		int lhs_pointer_depth = 0;
		const TypeSpecifierNode* lhs_type_node = nullptr;
		if (binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
				const auto& lhs_id = std::get<IdentifierNode>(lhs_expr);
				auto symbol = symbol_table.lookup(lhs_id.name());
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
		
		// Fallback: extract pointer depth from the LHS operands (4th element)
		// This handles expressions like &member, function calls returning pointers, etc.
		if (lhs_pointer_depth == 0 && lhsIrOperands.size() >= 4 && std::holds_alternative<unsigned long long>(lhsIrOperands[3])) {
			lhs_pointer_depth = static_cast<int>(std::get<unsigned long long>(lhsIrOperands[3]));
		}

		// Try to get pointer depth for RHS as well (for ptr - ptr case)
		int rhs_pointer_depth = 0;
		if (binaryOperatorNode.get_rhs().is<ExpressionNode>()) {
			const ExpressionNode& rhs_expr = binaryOperatorNode.get_rhs().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(rhs_expr)) {
				const auto& rhs_id = std::get<IdentifierNode>(rhs_expr);
				auto symbol = symbol_table.lookup(rhs_id.name());
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
				.lhs = { lhsType, 64, toIrValue(lhsIrOperands[2]) },
				.rhs = { rhsType, 64, toIrValue(rhsIrOperands[2]) },
				.result = byte_diff,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Subtract, std::move(sub_op), binaryOperatorNode.get_token()));
			
			// Step 2: Determine element size using existing getSizeInBytes function
			size_t element_size;
			if (lhs_pointer_depth > 1) {
				element_size = 8;  // Multi-level pointer: element is a pointer
			} else {
				// Single-level pointer: element size is sizeof(base_type)
				element_size = getSizeInBytes(lhs_type_node->type(), lhs_type_node->type_index(), lhs_type_node->size_in_bits());
			}
			
			// Step 3: Divide byte difference by element size to get element count
			TempVar result_var = var_counter.next();
			BinaryOp div_op{
				.lhs = { Type::Long, 64, byte_diff },
				.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
				.result = result_var,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Divide, std::move(div_op), binaryOperatorNode.get_token()));
			
			// Return result as Long (ptrdiff_t) with 64-bit size
			return { Type::Long, 64, result_var, 0ULL };
		}

		// Special handling for pointer arithmetic (ptr + int or ptr - int)
		// Only apply if LHS is actually a pointer (has pointer_depth > 0)
		// NOT for regular 64-bit integers like long, even though they are also 64 bits
		if ((op == "+" || op == "-") && lhsSize == 64 && lhs_pointer_depth > 0 && is_integer_type(rhsType)) {
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
				element_size = getSizeInBytes(lhs_type_node->type(), lhs_type_node->type_index(), lhs_type_node->size_in_bits());
			} else {
				// Fallback: derive element size from operand's base type
				int base_size_bits = get_type_size_bits(lhsType);
				element_size = base_size_bits / 8;
				if (element_size == 0) element_size = 1;  // Safety: avoid zero-size elements
			}
		
			// Scale the offset: offset_scaled = offset * element_size
			TempVar scaled_offset = var_counter.next();
			
		// Use typed BinaryOp for the multiply operation
		BinaryOp scale_op{
			.lhs = toTypedValue(rhsIrOperands),
			.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
			.result = scaled_offset,
		};
		ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(scale_op), binaryOperatorNode.get_token()));
	
		// Now add the scaled offset to the pointer
		TempVar result_var = var_counter.next();
		
		// Use typed BinaryOp for pointer addition/subtraction
		BinaryOp ptr_arith_op{
			.lhs = { lhsType, lhsSize, toIrValue(lhsIrOperands[2]) },
			.rhs = { Type::Int, 32, scaled_offset },
			.result = result_var,
		};
		
		IrOpcode ptr_opcode = (op == "+") ? IrOpcode::Add : IrOpcode::Subtract;
		ir_.addInstruction(IrInstruction(ptr_opcode, std::move(ptr_arith_op), binaryOperatorNode.get_token()));

			// Return pointer type with 64-bit size
			return { lhsType, 64, result_var, 0ULL };
		}
	
		// Check for logical operations BEFORE type promotions
		// Logical operations should preserve boolean types without promotion
		if (op == "&&" || op == "||") {
			TempVar result_var = var_counter.next();
			BinaryOp bin_op{
				.lhs = { Type::Bool, 8, toIrValue(lhsIrOperands[2]) },
				.rhs = { Type::Bool, 8, toIrValue(rhsIrOperands[2]) },
				.result = result_var,
			};
			IrOpcode opcode = (op == "&&") ? IrOpcode::LogicalAnd : IrOpcode::LogicalOr;
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
			return { Type::Bool, 8, result_var, 0ULL };  // Logical operations return bool8
		}

		// Special handling for pointer compound assignment (ptr += int or ptr -= int)
		// MUST be before type promotions to avoid truncating the pointer
		if ((op == "+=" || op == "-=") && lhsSize == 64 && lhs_pointer_depth > 0 && is_integer_type(rhsType) && lhs_type_node) {
			// Left side is a pointer (64-bit), right side is integer
			// Need to scale the offset by sizeof(pointed-to-type)
			FLASH_LOG_FORMAT(Codegen, Debug, "[PTR_ARITH_DEBUG] Compound assignment: lhsSize={}, pointer_depth={}, rhsType={}", lhsSize, lhs_pointer_depth, static_cast<int>(rhsType));
			
			// Determine element size using existing getSizeInBytes function
			size_t element_size;
			if (lhs_pointer_depth > 1) {
				element_size = 8;  // Multi-level pointer
			} else {
				// Single-level pointer: element size is sizeof(base_type)
				element_size = getSizeInBytes(lhs_type_node->type(), lhs_type_node->type_index(), lhs_type_node->size_in_bits());
			}
			
			// Scale the offset: offset_scaled = offset * element_size
			TempVar scaled_offset = var_counter.next();
			BinaryOp scale_op{
				.lhs = toTypedValue(rhsIrOperands),
				.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
				.result = scaled_offset,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(scale_op), binaryOperatorNode.get_token()));
			
			// ptr = ptr + scaled_offset (or ptr - scaled_offset)
			TempVar result_var = var_counter.next();
			BinaryOp ptr_arith_op{
				.lhs = { lhsType, lhsSize, toIrValue(lhsIrOperands[2]) },
				.rhs = { Type::Int, 32, scaled_offset },
				.result = result_var,
			};
			
			IrOpcode ptr_opcode = (op == "+=") ? IrOpcode::Add : IrOpcode::Subtract;
			ir_.addInstruction(IrInstruction(ptr_opcode, std::move(ptr_arith_op), binaryOperatorNode.get_token()));
			
			// Store result back to LHS (must be a variable)
			if (std::holds_alternative<StringHandle>(lhsIrOperands[2])) {
				AssignmentOp assign_op;
				assign_op.result = std::get<StringHandle>(lhsIrOperands[2]);
				assign_op.lhs = { lhsType, lhsSize, std::get<StringHandle>(lhsIrOperands[2]) };
				
				// Check if LHS is a reference variable
				StringHandle lhs_handle = std::get<StringHandle>(lhsIrOperands[2]);
				std::string_view lhs_name = StringTable::getStringView(lhs_handle);
				if (isVariableReference(lhs_name)) {
					assign_op.lhs.ref_qualifier = ReferenceQualifier::LValueReference;
				}
				
				assign_op.rhs = { lhsType, lhsSize, result_var };
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
			} else if (std::holds_alternative<TempVar>(lhsIrOperands[2])) {
				AssignmentOp assign_op;
				assign_op.result = std::get<TempVar>(lhsIrOperands[2]);
				assign_op.lhs = { lhsType, lhsSize, std::get<TempVar>(lhsIrOperands[2]) };
				
				// Check if LHS TempVar corresponds to a reference variable
				TempVar lhs_temp = std::get<TempVar>(lhsIrOperands[2]);
				std::string_view temp_name = lhs_temp.name();
				// Remove '%' prefix if present
				if (!temp_name.empty() && temp_name[0] == '%') {
					temp_name = temp_name.substr(1);
				}
				if (isVariableReference(temp_name)) {
					assign_op.lhs.ref_qualifier = ReferenceQualifier::LValueReference;
				}
				
				assign_op.rhs = { lhsType, lhsSize, result_var };
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
			}
			
			// Return the pointer result
			return { lhsType, lhsSize, result_var, 0ULL };
		}

		// Apply integer promotions and find common type
		// BUT: Skip type promotion for pointer assignments (ptr = ptr_expr)
		// Pointers should not be converted to common types
		if (op == "=" && lhsSize == 64 && lhs_pointer_depth > 0) {
			// This is a pointer assignment - no type conversions needed
			// Just assign the RHS to the LHS directly
			FLASH_LOG_FORMAT(Codegen, Debug, "[PTR_ARITH_DEBUG] Pointer assignment: lhsSize={}, pointer_depth={}", lhsSize, lhs_pointer_depth);
			
			// Get the assignment target (must be a variable)
			if (std::holds_alternative<StringHandle>(lhsIrOperands[2])) {
				AssignmentOp assign_op;
				assign_op.result = std::get<StringHandle>(lhsIrOperands[2]);
				assign_op.lhs = { lhsType, lhsSize, std::get<StringHandle>(lhsIrOperands[2]) };
				
				// Check if LHS is a reference variable
				StringHandle lhs_handle = std::get<StringHandle>(lhsIrOperands[2]);
				std::string_view lhs_name = StringTable::getStringView(lhs_handle);
				if (isVariableReference(lhs_name)) {
					assign_op.lhs.ref_qualifier = ReferenceQualifier::LValueReference;
				}
				
				assign_op.rhs = toTypedValue(rhsIrOperands);
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
				// Return the assigned value
				return { lhsType, lhsSize, std::get<StringHandle>(lhsIrOperands[2]), 0ULL };
			} else if (std::holds_alternative<TempVar>(lhsIrOperands[2])) {
				[[maybe_unused]] TempVar result_var = var_counter.next();
				AssignmentOp assign_op;
				assign_op.result = std::get<TempVar>(lhsIrOperands[2]);
				assign_op.lhs = { lhsType, lhsSize, std::get<TempVar>(lhsIrOperands[2]) };
				
				// Check if LHS TempVar corresponds to a reference variable
				TempVar lhs_temp = std::get<TempVar>(lhsIrOperands[2]);
				std::string_view temp_name = lhs_temp.name();
				// Remove '%' prefix if present
				if (!temp_name.empty() && temp_name[0] == '%') {
					temp_name = temp_name.substr(1);
				}
				if (isVariableReference(temp_name)) {
					assign_op.lhs.ref_qualifier = ReferenceQualifier::LValueReference;
				}
				
				assign_op.rhs = toTypedValue(rhsIrOperands);
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
				// Return the assigned value
				return { lhsType, lhsSize, std::get<TempVar>(lhsIrOperands[2]), 0ULL };
			}
		}
		
		// Special handling for assignment: convert RHS to LHS type instead of finding common type
		// For assignment, we don't want to promote the LHS
		if (op == "=") {
			// Convert RHS to LHS type if they differ
			if (rhsType != lhsType) {
				rhsIrOperands = generateTypeConversion(rhsIrOperands, rhsType, lhsType, binaryOperatorNode.get_token());
			}
			// Now both are the same type, create assignment
			AssignmentOp assign_op;
			// Extract the LHS value directly (it's either StringHandle or TempVar)
			if (std::holds_alternative<StringHandle>(lhsIrOperands[2])) {
				assign_op.result = std::get<StringHandle>(lhsIrOperands[2]);
			} else if (std::holds_alternative<TempVar>(lhsIrOperands[2])) {
				assign_op.result = std::get<TempVar>(lhsIrOperands[2]);
			} else {
				// LHS is an immediate value - this shouldn't happen for valid assignments
				throw std::runtime_error("Assignment LHS cannot be an immediate value");
				return {};
			}
			assign_op.lhs = toTypedValue(lhsIrOperands);
			assign_op.rhs = toTypedValue(rhsIrOperands);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
			// Assignment expression returns the LHS (the assigned-to value)
			return lhsIrOperands;
		}
		
		Type commonType = get_common_type(lhsType, rhsType);

		// Generate conversions if needed
		if (lhsType != commonType) {
			lhsIrOperands = generateTypeConversion(lhsIrOperands, lhsType, commonType, binaryOperatorNode.get_token());
		}
		if (rhsType != commonType) {
			rhsIrOperands = generateTypeConversion(rhsIrOperands, rhsType, commonType, binaryOperatorNode.get_token());
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
			{"+", IrOpcode::Add}, {"-", IrOpcode::Subtract}, {"*", IrOpcode::Multiply},
			{"<<", IrOpcode::ShiftLeft}, {"%", IrOpcode::Modulo},
			{"&", IrOpcode::BitwiseAnd}, {"|", IrOpcode::BitwiseOr}, {"^", IrOpcode::BitwiseXor}
		};

		auto bin_ops_it = !is_floating_point_op ? bin_ops.find(op) : bin_ops.end();
		if (bin_ops_it != bin_ops.end()) {
			opcode = bin_ops_it->second;

			// Use fully typed instruction (zero vector allocation!)
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		// Division operations (typed)
		else if (op == "/" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedDivide : IrOpcode::Divide;
			
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		// Right shift operations (typed)
		else if (op == ">>") {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedShiftRight : IrOpcode::ShiftRight;
			
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		// Comparison operations (typed)
		// For pointer comparisons, override types to use 64-bit unsigned integers
		// Helper lambda to apply pointer comparison type override
		auto applyPointerComparisonOverride = [&](BinaryOp& bin_op, IrOpcode& opcode) {
			if (lhs_pointer_depth > 0 && rhs_pointer_depth > 0) {
				bin_op.lhs.type = Type::UnsignedLongLong;
				bin_op.lhs.size_in_bits = 64;
				bin_op.rhs.type = Type::UnsignedLongLong;
				bin_op.rhs.size_in_bits = 64;
				
				// For ordered comparisons, ensure we use unsigned comparison for pointers
				if (opcode == IrOpcode::LessThan) opcode = IrOpcode::UnsignedLessThan;
				else if (opcode == IrOpcode::LessEqual) opcode = IrOpcode::UnsignedLessEqual;
				else if (opcode == IrOpcode::GreaterThan) opcode = IrOpcode::UnsignedGreaterThan;
				else if (opcode == IrOpcode::GreaterEqual) opcode = IrOpcode::UnsignedGreaterEqual;
			}
		};
		
		if (op == "==" && !is_floating_point_op) {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			opcode = IrOpcode::Equal;
			applyPointerComparisonOverride(bin_op, opcode);
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "!=" && !is_floating_point_op) {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			opcode = IrOpcode::NotEqual;
			applyPointerComparisonOverride(bin_op, opcode);
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "<" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedLessThan : IrOpcode::LessThan;
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			applyPointerComparisonOverride(bin_op, opcode);
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "<=" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedLessEqual : IrOpcode::LessEqual;
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			applyPointerComparisonOverride(bin_op, opcode);
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == ">" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedGreaterThan : IrOpcode::GreaterThan;
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			applyPointerComparisonOverride(bin_op, opcode);
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == ">=" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedGreaterEqual : IrOpcode::GreaterEqual;
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
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
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),  // Store result in LHS variable
			};
			ir_.addInstruction(IrInstruction(IrOpcode::AddAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "-=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::SubAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "*=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::MulAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "/=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::DivAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "%=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::ModAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "&=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::AndAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "|=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::OrAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "^=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::XorAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "<<=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::ShlAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == ">>=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::ShrAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (is_floating_point_op) { // Floating-point operations
			// Float operations use typed BinaryOp
			if (op == "+" || op == "-" || op == "*" || op == "/") {
				// Determine float opcode
				IrOpcode float_opcode;
				if (op == "+") float_opcode = IrOpcode::FloatAdd;
				else if (op == "-") float_opcode = IrOpcode::FloatSubtract;
				else if (op == "*") float_opcode = IrOpcode::FloatMultiply;
				else if (op == "/") float_opcode = IrOpcode::FloatDivide;
				else {
					throw std::runtime_error("Unsupported float operator");
					return {};
				}

			// Create typed BinaryOp for float arithmetic
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};

			ir_.addInstruction(IrInstruction(float_opcode, std::move(bin_op), binaryOperatorNode.get_token()));			// Return the result variable with float type and size
				return { commonType, get_type_size_bits(commonType), result_var, 0ULL };
			}

			// Float comparison operations use typed BinaryOp
			else if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
				// Determine float comparison opcode
				IrOpcode float_cmp_opcode;
				if (op == "==") float_cmp_opcode = IrOpcode::FloatEqual;
				else if (op == "!=") float_cmp_opcode = IrOpcode::FloatNotEqual;
				else if (op == "<") float_cmp_opcode = IrOpcode::FloatLessThan;
				else if (op == "<=") float_cmp_opcode = IrOpcode::FloatLessEqual;
				else if (op == ">") float_cmp_opcode = IrOpcode::FloatGreaterThan;
				else if (op == ">=") float_cmp_opcode = IrOpcode::FloatGreaterEqual;
				else {
					throw std::runtime_error("Unsupported float comparison operator");
					return {};
				}

				// Create typed BinaryOp for float comparison
				BinaryOp bin_op{
					.lhs = toTypedValue(lhsIrOperands),
					.rhs = toTypedValue(rhsIrOperands),
					.result = result_var,
				};

				ir_.addInstruction(IrInstruction(float_cmp_opcode, std::move(bin_op), binaryOperatorNode.get_token()));

				// Float comparisons return boolean (bool8)
				return { Type::Bool, 8, result_var, 0ULL };
			}
			else {
				// Unsupported floating-point operator
				throw std::runtime_error("Unsupported floating-point binary operator");
				return {};
			}
		}
	
		// For comparison operations, return boolean type (8 bits - bool size in C++)
		// For other operations, return the common type
		if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
			return { Type::Bool, 8, result_var, 0ULL };
		} else {
			// Return the result variable with its type and size
			// Note: Assignment is handled earlier and returns before reaching this point
			return { commonType, get_type_size_bits(commonType), result_var, 0ULL };
		}
	}

	// Helper function to generate Microsoft Visual C++ mangled name for function calls
	// Delegates to NameMangling::generateMangledName to keep all mangling logic in one place
	std::string_view generateMangledNameForCall(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& param_types, bool is_variadic = false, std::string_view struct_name = "", const std::vector<std::string>& namespace_path = {}) {
		return NameMangling::generateMangledName(name, return_type, param_types, is_variadic, struct_name, namespace_path).view();
	}

	// Overload that accepts parameter nodes directly to avoid creating a temporary vector
	std::string_view generateMangledNameForCall(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<ASTNode>& param_nodes, bool is_variadic = false, std::string_view struct_name = "", const std::vector<std::string>& namespace_path = {}) {
		return NameMangling::generateMangledName(name, return_type, param_nodes, is_variadic, struct_name, namespace_path).view();
	}

	// Overload that accepts a FunctionDeclarationNode directly
	// This extracts the function name, return type, parameters, and other info from the node
	// If struct_name_override is provided, it takes precedence over node.parent_struct_name()
	std::string_view generateMangledNameForCall(const FunctionDeclarationNode& func_node, std::string_view struct_name_override = "", const std::vector<std::string>& namespace_path = {}) {
		const DeclarationNode& decl_node = func_node.decl_node();
		const TypeSpecifierNode& return_type = decl_node.type_node().as<TypeSpecifierNode>();
		std::string_view func_name = decl_node.identifier_token().value();
		
		std::string_view struct_name = !struct_name_override.empty() ? struct_name_override
			: (func_node.is_member_function() ? func_node.parent_struct_name() : std::string_view{});
		
		// Pass linkage from the function node to ensure extern "C" functions aren't mangled
		return NameMangling::generateMangledName(func_name, return_type, func_node.parameter_nodes(),
			func_node.is_variadic(), struct_name, namespace_path, func_node.linkage()).view();
	}
	
	// Helper function to handle compiler intrinsics
	// Returns true if the function is an intrinsic and has been handled, false otherwise
	std::optional<std::vector<IrOperand>> tryGenerateIntrinsicIr(std::string_view func_name, const FunctionCallNode& functionCallNode) {
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

		return std::nullopt;  // Not an intrinsic
	}
	
	// Generate inline IR for __builtin_labs / __builtin_llabs
	// Uses branchless abs: abs(x) = (x XOR sign_mask) - sign_mask where sign_mask = x >> 63
	std::vector<IrOperand> generateBuiltinAbsIntIntrinsic(const FunctionCallNode& functionCallNode) {
		if (functionCallNode.arguments().size() != 1) {
			FLASH_LOG(Codegen, Error, "__builtin_labs/__builtin_llabs requires exactly 1 argument");
			return {Type::Long, 64, 0ULL, 0ULL};
		}
		
		// Get the argument
		ASTNode arg = functionCallNode.arguments()[0];
		auto arg_ir = visitExpressionNode(arg.as<ExpressionNode>());
		
		// Extract argument details
		Type arg_type = std::get<Type>(arg_ir[0]);
		int arg_size = std::get<int>(arg_ir[1]);
		TypedValue arg_value = toTypedValue(arg_ir);
		
		// Step 1: Arithmetic shift right by 63 to get sign mask (all 1s if negative, all 0s if positive)
		TempVar sign_mask = var_counter.next();
		BinaryOp shift_op{
			.lhs = arg_value,
			.rhs = TypedValue{Type::Int, 32, 63ULL},
			.result = sign_mask
		};
		ir_.addInstruction(IrInstruction(IrOpcode::ShiftRight, std::move(shift_op), functionCallNode.called_from()));
		
		// Step 2: XOR with sign mask
		TempVar xor_result = var_counter.next();
		BinaryOp xor_op{
			.lhs = arg_value,
			.rhs = TypedValue{arg_type, arg_size, sign_mask},
			.result = xor_result
		};
		ir_.addInstruction(IrInstruction(IrOpcode::BitwiseXor, std::move(xor_op), functionCallNode.called_from()));
		
		// Step 3: Subtract sign mask
		TempVar abs_result = var_counter.next();
		BinaryOp sub_op{
			.lhs = TypedValue{arg_type, arg_size, xor_result},
			.rhs = TypedValue{arg_type, arg_size, sign_mask},
			.result = abs_result
		};
		ir_.addInstruction(IrInstruction(IrOpcode::Subtract, std::move(sub_op), functionCallNode.called_from()));
		
		return {arg_type, arg_size, abs_result, 0ULL};
	}
	
	// Generate inline IR for __builtin_fabs / __builtin_fabsf / __builtin_fabsl
	// Uses bitwise AND to clear the sign bit
	std::vector<IrOperand> generateBuiltinAbsFloatIntrinsic(const FunctionCallNode& functionCallNode, std::string_view func_name) {
		if (functionCallNode.arguments().size() != 1) {
			FLASH_LOG(Codegen, Error, func_name, " requires exactly 1 argument");
			return {Type::Double, 64, 0ULL, 0ULL};
		}
		
		// Get the argument
		ASTNode arg = functionCallNode.arguments()[0];
		auto arg_ir = visitExpressionNode(arg.as<ExpressionNode>());
		
		// Extract argument details
		Type arg_type = std::get<Type>(arg_ir[0]);
		int arg_size = std::get<int>(arg_ir[1]);
		TypedValue arg_value = toTypedValue(arg_ir);
		
		// For floating point abs, clear the sign bit using bitwise AND
		// Float (32-bit): AND with 0x7FFFFFFF
		// Double (64-bit): AND with 0x7FFFFFFFFFFFFFFF
		unsigned long long mask = (arg_size == 32) ? 0x7FFFFFFFULL : 0x7FFFFFFFFFFFFFFFULL;
		
		TempVar abs_result = var_counter.next();
		BinaryOp and_op{
			.lhs = arg_value,
			.rhs = TypedValue{Type::UnsignedLongLong, arg_size, mask},
			.result = abs_result
		};
		ir_.addInstruction(IrInstruction(IrOpcode::BitwiseAnd, std::move(and_op), functionCallNode.called_from()));
		
		return {arg_type, arg_size, abs_result, 0ULL};
	}
	
	// Helper function to detect if a va_list argument is a simple pointer type
	// (e.g., typedef char* va_list;) vs the proper System V AMD64 va_list structure
	// Returns true if va_list is a pointer type, false otherwise
	bool isVaListPointerType(const ASTNode& arg, const std::vector<IrOperand>& ir_result) const {
		// Check if the argument is an identifier with pointer type
		if (arg.is<ExpressionNode>() && std::holds_alternative<IdentifierNode>(arg.as<ExpressionNode>())) {
			const auto& id = std::get<IdentifierNode>(arg.as<ExpressionNode>());
			if (auto sym = symbol_table.lookup(id.name())) {
				if (sym->is<DeclarationNode>()) {
					const auto& ty = sym->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
					if (ty.pointer_depth() > 0) return true;
				} else if (sym->is<VariableDeclarationNode>()) {
					const auto& ty = sym->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
					if (ty.pointer_depth() > 0) return true;
				}
			}
		}
		
		// Fallback: treat as pointer when operand size is pointer sized (common for typedef char*)
		if (ir_result.size() >= 2 && std::holds_alternative<int>(ir_result[1])) {
			if (std::get<int>(ir_result[1]) == POINTER_SIZE_BITS) {
				return true;
			}
		}
		
		return false;
	}
	
	// Generate IR for __builtin_va_arg intrinsic
	// __builtin_va_arg(va_list, type) - reads the current value and advances the appropriate offset
	std::vector<IrOperand> generateVaArgIntrinsic(const FunctionCallNode& functionCallNode) {
		// __builtin_va_arg takes 2 arguments: va_list variable and type
		// After preprocessing: __builtin_va_arg(args, int) - parser sees this as function call with 2 args
		if (functionCallNode.arguments().size() != 2) {
			FLASH_LOG(Codegen, Error, "__builtin_va_arg requires exactly 2 arguments (va_list and type)");
			return {Type::Void, 0, 0ULL, 0ULL};
		}
		
		// Get the first argument (va_list variable)
		ASTNode arg0 = functionCallNode.arguments()[0];
		auto va_list_ir = visitExpressionNode(arg0.as<ExpressionNode>());
		
		// Get the second argument (type identifier or type specifier) 
		ASTNode arg1 = functionCallNode.arguments()[1];
		
		// Extract type information from the second argument
		Type requested_type = Type::Int;
		int requested_size = 32;
		bool is_float_type = false;
		
		// The second argument can be either an IdentifierNode (from old macro) or TypeSpecifierNode (from new parser)
		// TypeSpecifierNode is stored directly in ASTNode, not in ExpressionNode
		if (arg1.is<TypeSpecifierNode>()) {
			// New parser path: TypeSpecifierNode passed directly
			const auto& type_spec = arg1.as<TypeSpecifierNode>();
			requested_type = type_spec.type();
			requested_size = static_cast<int>(type_spec.size_in_bits());
			is_float_type = (requested_type == Type::Float || requested_type == Type::Double);
		} else if (arg1.is<ExpressionNode>() && std::holds_alternative<IdentifierNode>(arg1.as<ExpressionNode>())) {
			// Old path: IdentifierNode with type name
			std::string_view type_name = std::get<IdentifierNode>(arg1.as<ExpressionNode>()).name();
			
			// Map type names to Type enum
			if (type_name == "int") {
				requested_type = Type::Int;
				requested_size = 32;
			} else if (type_name == "double") {
				requested_type = Type::Double;
				requested_size = 64;
				is_float_type = true;
			} else if (type_name == "float") {
				requested_type = Type::Float;
				requested_size = 32;
				is_float_type = true;
			} else if (type_name == "long") {
				requested_type = Type::Long;
				requested_size = 64;
			} else if (type_name == "char") {
				requested_type = Type::Char;
				requested_size = 8;
			} else {
				// Default to int
				requested_type = Type::Int;
				requested_size = 32;
			}
		}
		
		// va_list_ir[2] contains the variable/temp identifier
		std::variant<StringHandle, TempVar> va_list_var;
		if (std::holds_alternative<TempVar>(va_list_ir[2])) {
			va_list_var = std::get<TempVar>(va_list_ir[2]);
		} else if (std::holds_alternative<StringHandle>(va_list_ir[2])) {
			va_list_var = std::get<StringHandle>(va_list_ir[2]);
		} else {
			FLASH_LOG(Codegen, Error, "__builtin_va_arg first argument must be a variable");
			return {Type::Void, 0, 0ULL, 0ULL};
		}
		
		// Detect if the user's va_list is a pointer type (e.g., typedef char* va_list;)
		// This must match the detection logic in generateVaStartIntrinsic
		bool va_list_is_pointer = isVaListPointerType(arg0, va_list_ir);
		
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
			if (std::holds_alternative<TempVar>(va_list_var)) {
				// va_list is already a TempVar - use it directly
				va_list_struct_ptr = std::get<TempVar>(va_list_var);
			} else {
				// va_list is a variable name - load its value (which is a pointer) into a TempVar
				va_list_struct_ptr = var_counter.next();
				StringHandle var_name_handle = std::get<StringHandle>(va_list_var);
				
				// Use Assignment to load the pointer value from the variable
				AssignmentOp load_pointer;
				load_pointer.result = va_list_struct_ptr;
				load_pointer.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
				load_pointer.rhs = TypedValue{Type::UnsignedLongLong, 64, var_name_handle};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(load_pointer), functionCallNode.called_from()));
			}
			
			// Step 2: Compute address of the appropriate offset field (gp_offset for ints, fp_offset for floats)
			// Step 3: Load current offset value (32-bit unsigned) from the offset field
			TempVar current_offset = var_counter.next();
			DereferenceOp load_offset;
			load_offset.result = current_offset;
			load_offset.pointer.type = Type::UnsignedInt;  // Reading a 32-bit unsigned offset
			load_offset.pointer.size_in_bits = 32;  // gp_offset/fp_offset is 32 bits
			load_offset.pointer.pointer_depth = 1;
			
			if (is_float_type) {
				// fp_offset is at offset 4 - compute va_list_struct_ptr + 4
				TempVar fp_offset_addr = var_counter.next();
				BinaryOp fp_offset_calc;
				fp_offset_calc.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
				fp_offset_calc.rhs = TypedValue{Type::UnsignedLongLong, 64, 4ULL};
				fp_offset_calc.result = fp_offset_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(fp_offset_calc), functionCallNode.called_from()));
				
				// Materialize the address before using it
				TempVar materialized_fp_addr = var_counter.next();
				AssignmentOp materialize;
				materialize.result = materialized_fp_addr;
				materialize.lhs = TypedValue{Type::UnsignedLongLong, 64, materialized_fp_addr};
				materialize.rhs = TypedValue{Type::UnsignedLongLong, 64, fp_offset_addr};
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
			compare_op.lhs = TypedValue{Type::UnsignedInt, 32, current_offset};
			// Adjust limit: need to have slot_size bytes remaining
			compare_op.rhs = TypedValue{Type::UnsignedInt, 32, offset_limit - slot_size + 8};
			compare_op.result = cmp_result;
			ir_.addInstruction(IrInstruction(IrOpcode::UnsignedLessThan, std::move(compare_op), functionCallNode.called_from()));
			
			// Conditional branch: if (current_offset < limit) goto reg_path else goto overflow_path
			CondBranchOp cond_branch;
			cond_branch.label_true = reg_path_label;
			cond_branch.label_false = overflow_path_label;
			cond_branch.condition = TypedValue{Type::Bool, 1, cmp_result};
			ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), functionCallNode.called_from()));
			
			// ============ REGISTER PATH ============
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = reg_path_label}, functionCallNode.called_from()));
			
			// Step 4: Load reg_save_area pointer (at offset 16)
			TempVar reg_save_area_field_addr = var_counter.next();
			BinaryOp reg_save_addr;
			reg_save_addr.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
			reg_save_addr.rhs = TypedValue{Type::UnsignedLongLong, 64, 16ULL};
			reg_save_addr.result = reg_save_area_field_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(reg_save_addr), functionCallNode.called_from()));
			
			// Materialize the address before using it
			TempVar materialized_reg_save_addr = var_counter.next();
			AssignmentOp materialize_reg;
			materialize_reg.result = materialized_reg_save_addr;
			materialize_reg.lhs = TypedValue{Type::UnsignedLongLong, 64, materialized_reg_save_addr};
			materialize_reg.rhs = TypedValue{Type::UnsignedLongLong, 64, reg_save_area_field_addr};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_reg), functionCallNode.called_from()));
			
			TempVar reg_save_area_ptr = var_counter.next();
			DereferenceOp load_reg_save_ptr;
			load_reg_save_ptr.result = reg_save_area_ptr;
			load_reg_save_ptr.pointer.type = Type::UnsignedLongLong;
			load_reg_save_ptr.pointer.size_in_bits = 64;  // Pointer is always 64 bits
			load_reg_save_ptr.pointer.pointer_depth = 1;
			load_reg_save_ptr.pointer.value = materialized_reg_save_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(load_reg_save_ptr), functionCallNode.called_from()));
			
			// Step 5: Compute address: reg_save_area + current_offset
			TempVar arg_addr = var_counter.next();
			BinaryOp compute_addr;
			compute_addr.lhs = TypedValue{Type::UnsignedLongLong, 64, reg_save_area_ptr};
			// Need to convert offset from uint32 to uint64 for addition
			TempVar offset_64 = var_counter.next();
			AssignmentOp convert_offset;
			convert_offset.result = offset_64;
			convert_offset.lhs = TypedValue{Type::UnsignedLongLong, 64, offset_64};
			convert_offset.rhs = TypedValue{Type::UnsignedInt, 32, current_offset};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(convert_offset), functionCallNode.called_from()));
			
			compute_addr.rhs = TypedValue{Type::UnsignedLongLong, 64, offset_64};
			compute_addr.result = arg_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(compute_addr), functionCallNode.called_from()));
			
			// Step 6: Read the value at arg_addr
			TempVar reg_value = var_counter.next();
			DereferenceOp read_reg_value;
			read_reg_value.result = reg_value;
			read_reg_value.pointer.type = requested_type;
			read_reg_value.pointer.size_in_bits = requested_size;
			read_reg_value.pointer.pointer_depth = 1;
			read_reg_value.pointer.value = arg_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(read_reg_value), functionCallNode.called_from()));
			
			// Assign to result variable
			AssignmentOp assign_reg_result;
			assign_reg_result.result = value;
			assign_reg_result.lhs = TypedValue{requested_type, requested_size, value};
			assign_reg_result.rhs = TypedValue{requested_type, requested_size, reg_value};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_reg_result), functionCallNode.called_from()));
			
			// Step 7: Increment the offset by slot_size and store back
			// slot_size is 16 for floats (XMM regs), or rounded up to 8-byte boundary for integers/structs
			TempVar new_offset = var_counter.next();
			BinaryOp increment_offset;
			increment_offset.lhs = TypedValue{Type::UnsignedInt, 32, current_offset};
			increment_offset.rhs = TypedValue{Type::UnsignedInt, 32, slot_size};
			increment_offset.result = new_offset;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(increment_offset), functionCallNode.called_from()));
			
			// Step 8: Store updated offset back to the appropriate field in the structure
			TempVar materialized_offset = var_counter.next();
			AssignmentOp materialize;
			materialize.result = materialized_offset;
			materialize.lhs = TypedValue{Type::UnsignedInt, 32, materialized_offset};
			materialize.rhs = TypedValue{Type::UnsignedInt, 32, new_offset};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize), functionCallNode.called_from()));
			
			DereferenceStoreOp store_offset;
			store_offset.pointer.type = Type::UnsignedInt;
			store_offset.pointer.size_in_bits = 64;  // Pointer is always 64 bits
			store_offset.pointer.pointer_depth = 1;
			if (is_float_type) {
				// Store to fp_offset field at offset 4
				TempVar fp_offset_store_addr = var_counter.next();
				BinaryOp fp_store_addr_calc;
				fp_store_addr_calc.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
				fp_store_addr_calc.rhs = TypedValue{Type::UnsignedLongLong, 64, 4ULL};
				fp_store_addr_calc.result = fp_offset_store_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(fp_store_addr_calc), functionCallNode.called_from()));
				
				TempVar materialized_addr = var_counter.next();
				AssignmentOp materialize_addr;
				materialize_addr.result = materialized_addr;
				materialize_addr.lhs = TypedValue{Type::UnsignedLongLong, 64, materialized_addr};
				materialize_addr.rhs = TypedValue{Type::UnsignedLongLong, 64, fp_offset_store_addr};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_addr), functionCallNode.called_from()));
				
				store_offset.pointer.value = materialized_addr;
			} else {
				// Store to gp_offset field at offset 0
				store_offset.pointer.value = va_list_struct_ptr;
			}
			store_offset.value = TypedValue{Type::UnsignedInt, 32, materialized_offset};
			ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_offset), functionCallNode.called_from()));
			
			// Jump to end
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = va_arg_end_label}, functionCallNode.called_from()));
			
			// ============ OVERFLOW PATH ============
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = overflow_path_label}, functionCallNode.called_from()));
			
			// Load overflow_arg_area pointer (at offset 8)
			TempVar overflow_field_addr = var_counter.next();
			BinaryOp overflow_addr_calc;
			overflow_addr_calc.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
			overflow_addr_calc.rhs = TypedValue{Type::UnsignedLongLong, 64, 8ULL};
			overflow_addr_calc.result = overflow_field_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(overflow_addr_calc), functionCallNode.called_from()));
			
			// Materialize before dereferencing
			TempVar materialized_overflow_addr = var_counter.next();
			AssignmentOp materialize_overflow;
			materialize_overflow.result = materialized_overflow_addr;
			materialize_overflow.lhs = TypedValue{Type::UnsignedLongLong, 64, materialized_overflow_addr};
			materialize_overflow.rhs = TypedValue{Type::UnsignedLongLong, 64, overflow_field_addr};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_overflow), functionCallNode.called_from()));
			
			TempVar overflow_ptr = var_counter.next();
			DereferenceOp load_overflow_ptr;
			load_overflow_ptr.result = overflow_ptr;
			load_overflow_ptr.pointer.type = Type::UnsignedLongLong;
			load_overflow_ptr.pointer.size_in_bits = 64;
			load_overflow_ptr.pointer.pointer_depth = 1;
			load_overflow_ptr.pointer.value = materialized_overflow_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(load_overflow_ptr), functionCallNode.called_from()));
			
			// Read value from overflow_arg_area
			TempVar overflow_value = var_counter.next();
			DereferenceOp read_overflow_value;
			read_overflow_value.result = overflow_value;
			read_overflow_value.pointer.type = requested_type;
			read_overflow_value.pointer.size_in_bits = requested_size;
			read_overflow_value.pointer.pointer_depth = 1;
			read_overflow_value.pointer.value = overflow_ptr;
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(read_overflow_value), functionCallNode.called_from()));
			
			// Assign to result variable
			AssignmentOp assign_overflow_result;
			assign_overflow_result.result = value;
			assign_overflow_result.lhs = TypedValue{requested_type, requested_size, value};
			assign_overflow_result.rhs = TypedValue{requested_type, requested_size, overflow_value};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_overflow_result), functionCallNode.called_from()));
			
			// Advance overflow_arg_area by the actual stack argument size (always 8 bytes on x64 stack)
			// Note: slot_size is for register save area; stack always uses 8-byte slots
			unsigned long long overflow_advance = (requested_size + 63) / 64 * 8;  // Round up to 8-byte boundary
			TempVar new_overflow_ptr = var_counter.next();
			BinaryOp advance_overflow;
			advance_overflow.lhs = TypedValue{Type::UnsignedLongLong, 64, overflow_ptr};
			advance_overflow.rhs = TypedValue{Type::UnsignedLongLong, 64, overflow_advance};
			advance_overflow.result = new_overflow_ptr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(advance_overflow), functionCallNode.called_from()));
			
			// Store updated overflow_arg_area back to structure
			DereferenceStoreOp store_overflow;
			store_overflow.pointer.type = Type::UnsignedLongLong;
			store_overflow.pointer.size_in_bits = 64;
			store_overflow.pointer.pointer_depth = 1;
			store_overflow.pointer.value = materialized_overflow_addr;
			store_overflow.value = TypedValue{Type::UnsignedLongLong, 64, new_overflow_ptr};
			ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_overflow), functionCallNode.called_from()));
			
			// ============ END LABEL ============
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = va_arg_end_label}, functionCallNode.called_from()));
			
			return {requested_type, requested_size, value};
			
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
				load_ptr_op.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
				if (std::holds_alternative<StringHandle>(va_list_var)) {
					load_ptr_op.rhs = TypedValue{Type::UnsignedLongLong, 64, std::get<StringHandle>(va_list_var)};
				} else {
					load_ptr_op.rhs = TypedValue{Type::UnsignedLongLong, 64, std::get<TempVar>(va_list_var)};
				}
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(load_ptr_op), functionCallNode.called_from()));
				
				// Load gp_offset (offset 0) for integers, or fp_offset (offset 4) for floats
				TempVar current_offset = var_counter.next();
				DereferenceOp load_offset;
				load_offset.result = current_offset;
				load_offset.pointer.type = Type::UnsignedInt;
				load_offset.pointer.size_in_bits = 32;
				load_offset.pointer.pointer_depth = 1;
				
				if (is_float_type) {
					// fp_offset is at offset 4 - compute va_list_struct_ptr + 4
					TempVar fp_offset_addr = var_counter.next();
					BinaryOp fp_offset_calc;
					fp_offset_calc.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
					fp_offset_calc.rhs = TypedValue{Type::UnsignedLongLong, 64, 4ULL};
					fp_offset_calc.result = fp_offset_addr;
					ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(fp_offset_calc), functionCallNode.called_from()));
					
					// Materialize the address before using it
					TempVar materialized_fp_addr = var_counter.next();
					AssignmentOp materialize;
					materialize.result = materialized_fp_addr;
					materialize.lhs = TypedValue{Type::UnsignedLongLong, 64, materialized_fp_addr};
					materialize.rhs = TypedValue{Type::UnsignedLongLong, 64, fp_offset_addr};
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
				compare_op.lhs = TypedValue{Type::UnsignedInt, 32, current_offset};
				// Adjust limit: need to have slot_size bytes remaining
				compare_op.rhs = TypedValue{Type::UnsignedInt, 32, offset_limit - slot_size + 8};
				compare_op.result = cmp_result;
				ir_.addInstruction(IrInstruction(IrOpcode::UnsignedLessThan, std::move(compare_op), functionCallNode.called_from()));
				
				// Conditional branch
				CondBranchOp cond_branch;
				cond_branch.label_true = reg_path_label;
				cond_branch.label_false = overflow_path_label;
				cond_branch.condition = TypedValue{Type::Bool, 1, cmp_result};
				ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), functionCallNode.called_from()));
				
				// ============ REGISTER PATH ============
				ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = reg_path_label}, functionCallNode.called_from()));
				
				// Load reg_save_area pointer (at offset 16)
				TempVar reg_save_area_field_addr = var_counter.next();
				BinaryOp reg_save_addr;
				reg_save_addr.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
				reg_save_addr.rhs = TypedValue{Type::UnsignedLongLong, 64, 16ULL};
				reg_save_addr.result = reg_save_area_field_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(reg_save_addr), functionCallNode.called_from()));
				
				TempVar materialized_reg_save_addr = var_counter.next();
				AssignmentOp materialize_reg;
				materialize_reg.result = materialized_reg_save_addr;
				materialize_reg.lhs = TypedValue{Type::UnsignedLongLong, 64, materialized_reg_save_addr};
				materialize_reg.rhs = TypedValue{Type::UnsignedLongLong, 64, reg_save_area_field_addr};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_reg), functionCallNode.called_from()));
				
				TempVar reg_save_area_ptr = var_counter.next();
				DereferenceOp load_reg_save_ptr;
				load_reg_save_ptr.result = reg_save_area_ptr;
				load_reg_save_ptr.pointer.type = Type::UnsignedLongLong;
				load_reg_save_ptr.pointer.size_in_bits = 64;
				load_reg_save_ptr.pointer.pointer_depth = 1;
				load_reg_save_ptr.pointer.value = materialized_reg_save_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(load_reg_save_ptr), functionCallNode.called_from()));
				
				// Compute address: reg_save_area + current_offset
				TempVar offset_64 = var_counter.next();
				AssignmentOp convert_offset;
				convert_offset.result = offset_64;
				convert_offset.lhs = TypedValue{Type::UnsignedLongLong, 64, offset_64};
				convert_offset.rhs = TypedValue{Type::UnsignedInt, 32, current_offset};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(convert_offset), functionCallNode.called_from()));
				
				TempVar arg_addr = var_counter.next();
				BinaryOp compute_addr;
				compute_addr.lhs = TypedValue{Type::UnsignedLongLong, 64, reg_save_area_ptr};
				compute_addr.rhs = TypedValue{Type::UnsignedLongLong, 64, offset_64};
				compute_addr.result = arg_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(compute_addr), functionCallNode.called_from()));
				
				// Read value
				TempVar reg_value = var_counter.next();
				DereferenceOp read_reg_value;
				read_reg_value.result = reg_value;
				read_reg_value.pointer.type = requested_type;
				read_reg_value.pointer.size_in_bits = requested_size;
				read_reg_value.pointer.pointer_depth = 1;
				read_reg_value.pointer.value = arg_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(read_reg_value), functionCallNode.called_from()));
				
				// Assign to result
				AssignmentOp assign_reg_result;
				assign_reg_result.result = value;
				assign_reg_result.lhs = TypedValue{requested_type, requested_size, value};
				assign_reg_result.rhs = TypedValue{requested_type, requested_size, reg_value};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_reg_result), functionCallNode.called_from()));
				
				// Increment gp_offset by slot_size, or fp_offset by 16
				TempVar new_offset = var_counter.next();
				BinaryOp increment_offset;
				increment_offset.lhs = TypedValue{Type::UnsignedInt, 32, current_offset};
				increment_offset.rhs = TypedValue{Type::UnsignedInt, 32, slot_size};
				increment_offset.result = new_offset;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(increment_offset), functionCallNode.called_from()));
				
				TempVar materialized_offset = var_counter.next();
				AssignmentOp materialize_off;
				materialize_off.result = materialized_offset;
				materialize_off.lhs = TypedValue{Type::UnsignedInt, 32, materialized_offset};
				materialize_off.rhs = TypedValue{Type::UnsignedInt, 32, new_offset};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_off), functionCallNode.called_from()));
				
				DereferenceStoreOp store_offset;
				store_offset.pointer.type = Type::UnsignedInt;
				store_offset.pointer.size_in_bits = 64;
				store_offset.pointer.pointer_depth = 1;
				if (is_float_type) {
					// Store to fp_offset field at offset 4
					TempVar fp_offset_store_addr = var_counter.next();
					BinaryOp fp_store_addr_calc;
					fp_store_addr_calc.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
					fp_store_addr_calc.rhs = TypedValue{Type::UnsignedLongLong, 64, 4ULL};
					fp_store_addr_calc.result = fp_offset_store_addr;
					ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(fp_store_addr_calc), functionCallNode.called_from()));
					
					TempVar materialized_addr = var_counter.next();
					AssignmentOp materialize_addr;
					materialize_addr.result = materialized_addr;
					materialize_addr.lhs = TypedValue{Type::UnsignedLongLong, 64, materialized_addr};
					materialize_addr.rhs = TypedValue{Type::UnsignedLongLong, 64, fp_offset_store_addr};
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_addr), functionCallNode.called_from()));
					
					store_offset.pointer.value = materialized_addr;
				} else {
					// Store to gp_offset field at offset 0
					store_offset.pointer.value = va_list_struct_ptr;
				}
				store_offset.value = TypedValue{Type::UnsignedInt, 32, materialized_offset};
				ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_offset), functionCallNode.called_from()));
				
				// Jump to end
				ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = va_arg_end_label}, functionCallNode.called_from()));
				
				// ============ OVERFLOW PATH ============
				ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = overflow_path_label}, functionCallNode.called_from()));
				
				// Load overflow_arg_area (at offset 8)
				TempVar overflow_field_addr = var_counter.next();
				BinaryOp overflow_addr_calc;
				overflow_addr_calc.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
				overflow_addr_calc.rhs = TypedValue{Type::UnsignedLongLong, 64, 8ULL};
				overflow_addr_calc.result = overflow_field_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(overflow_addr_calc), functionCallNode.called_from()));
				
				TempVar materialized_overflow_addr = var_counter.next();
				AssignmentOp materialize_overflow;
				materialize_overflow.result = materialized_overflow_addr;
				materialize_overflow.lhs = TypedValue{Type::UnsignedLongLong, 64, materialized_overflow_addr};
				materialize_overflow.rhs = TypedValue{Type::UnsignedLongLong, 64, overflow_field_addr};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_overflow), functionCallNode.called_from()));
				
				TempVar overflow_ptr = var_counter.next();
				DereferenceOp load_overflow_ptr;
				load_overflow_ptr.result = overflow_ptr;
				load_overflow_ptr.pointer.type = Type::UnsignedLongLong;
				load_overflow_ptr.pointer.size_in_bits = 64;
				load_overflow_ptr.pointer.pointer_depth = 1;
				load_overflow_ptr.pointer.value = materialized_overflow_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(load_overflow_ptr), functionCallNode.called_from()));
				
				// Read value from overflow area
				TempVar overflow_value = var_counter.next();
				DereferenceOp read_overflow_value;
				read_overflow_value.result = overflow_value;
				read_overflow_value.pointer.type = requested_type;
				read_overflow_value.pointer.size_in_bits = requested_size;
				read_overflow_value.pointer.pointer_depth = 1;
				read_overflow_value.pointer.value = overflow_ptr;
				ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(read_overflow_value), functionCallNode.called_from()));
				
				// Assign to result
				AssignmentOp assign_overflow_result;
				assign_overflow_result.result = value;
				assign_overflow_result.lhs = TypedValue{requested_type, requested_size, value};
				assign_overflow_result.rhs = TypedValue{requested_type, requested_size, overflow_value};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_overflow_result), functionCallNode.called_from()));
				
				// Advance overflow_arg_area by the actual stack argument size (always 8 bytes per slot on x64 stack)
				unsigned long long overflow_advance = (requested_size + 63) / 64 * 8;  // Round up to 8-byte boundary
				TempVar new_overflow_ptr = var_counter.next();
				BinaryOp advance_overflow;
				advance_overflow.lhs = TypedValue{Type::UnsignedLongLong, 64, overflow_ptr};
				advance_overflow.rhs = TypedValue{Type::UnsignedLongLong, 64, overflow_advance};
				advance_overflow.result = new_overflow_ptr;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(advance_overflow), functionCallNode.called_from()));
				
				DereferenceStoreOp store_overflow;
				store_overflow.pointer.type = Type::UnsignedLongLong;
				store_overflow.pointer.size_in_bits = 64;
				store_overflow.pointer.pointer_depth = 1;
				store_overflow.pointer.value = materialized_overflow_addr;
				store_overflow.value = TypedValue{Type::UnsignedLongLong, 64, new_overflow_ptr};
				ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_overflow), functionCallNode.called_from()));
				
				// ============ END LABEL ============
				ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = va_arg_end_label}, functionCallNode.called_from()));
				
				return {requested_type, requested_size, value};
				
			} else {
				// Windows/MSVC ABI: Simple pointer-based approach
				// va_list is a char* that directly holds the address of the next variadic argument

				// Step 1: Load the current pointer value from va_list variable
				TempVar current_ptr = var_counter.next();
				AssignmentOp load_ptr_op;
				load_ptr_op.result = current_ptr;
				load_ptr_op.lhs = TypedValue{Type::UnsignedLongLong, 64, current_ptr};
				if (std::holds_alternative<StringHandle>(va_list_var)) {
					load_ptr_op.rhs = TypedValue{Type::UnsignedLongLong, 64, std::get<StringHandle>(va_list_var)};
				} else {
					load_ptr_op.rhs = TypedValue{Type::UnsignedLongLong, 64, std::get<TempVar>(va_list_var)};
				}
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(load_ptr_op), functionCallNode.called_from()));

				// Step 2: Read the value at the current pointer
				// Win64 ABI: structs > 8 bytes are passed by pointer in variadic calls,
				// so the stack slot holds a pointer to the struct, not the struct itself.
				// We need to read the pointer first, then dereference it.
				bool is_indirect_struct = (requested_type == Type::Struct && requested_size > 64);

				TempVar value = var_counter.next();
				if (is_indirect_struct) {
					// Large struct: stack slot contains a pointer to the struct
					// Step 2a: Read the pointer from the stack slot
					TempVar struct_ptr = var_counter.next();
					DereferenceOp deref_ptr_op;
					deref_ptr_op.result = struct_ptr;
					deref_ptr_op.pointer.type = Type::UnsignedLongLong;
					deref_ptr_op.pointer.size_in_bits = 64;
					deref_ptr_op.pointer.pointer_depth = 1;
					deref_ptr_op.pointer.value = current_ptr;
					ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_ptr_op), functionCallNode.called_from()));

					// Step 2b: Dereference the struct pointer to get the actual struct
					DereferenceOp deref_struct_op;
					deref_struct_op.result = value;
					deref_struct_op.pointer.type = requested_type;
					deref_struct_op.pointer.size_in_bits = requested_size;
					deref_struct_op.pointer.pointer_depth = 1;
					deref_struct_op.pointer.value = struct_ptr;
					ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_struct_op), functionCallNode.called_from()));
				} else {
					// Small types (≤8 bytes): read value directly from stack slot
					DereferenceOp deref_value_op;
					deref_value_op.result = value;
					deref_value_op.pointer.type = requested_type;
					deref_value_op.pointer.size_in_bits = requested_size;
					deref_value_op.pointer.pointer_depth = 1;
					deref_value_op.pointer.value = current_ptr;
					ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_value_op), functionCallNode.called_from()));
				}

				// Step 3: Advance va_list by 8 bytes (always 8 - even for large structs,
				// since the stack slot holds a pointer, not the struct itself)
				TempVar next_ptr = var_counter.next();
				BinaryOp add_op;
				add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, current_ptr};
				add_op.rhs = TypedValue{Type::UnsignedLongLong, 64, 8ULL};
				add_op.result = next_ptr;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), functionCallNode.called_from()));

				// Step 4: Store the updated pointer back to va_list
				AssignmentOp assign_op;
				assign_op.result = var_counter.next();  // unused but required
				if (std::holds_alternative<TempVar>(va_list_var)) {
					assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<TempVar>(va_list_var)};
				} else {
					assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<StringHandle>(va_list_var)};
				}
				assign_op.rhs = TypedValue{Type::UnsignedLongLong, 64, next_ptr};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), functionCallNode.called_from()));

				return {requested_type, requested_size, value};
			}
		}
	}
	
	// Generate IR for __builtin_va_start intrinsic
	std::vector<IrOperand> generateVaStartIntrinsic(const FunctionCallNode& functionCallNode) {
		// __builtin_va_start takes 2 arguments: va_list (not pointer!), and last fixed parameter
		if (functionCallNode.arguments().size() != 2) {
			FLASH_LOG(Codegen, Error, "__builtin_va_start requires exactly 2 arguments");
			return {Type::Void, 0, 0ULL, 0ULL};
		}
		
		// Get the first argument (va_list variable)
		ASTNode arg0 = functionCallNode.arguments()[0];
		auto arg0_ir = visitExpressionNode(arg0.as<ExpressionNode>());
		
		// Get the va_list variable name (needed for assignment later)
		StringHandle va_list_name_handle;
		if (std::holds_alternative<IdentifierNode>(arg0.as<ExpressionNode>())) {
			const auto& id = std::get<IdentifierNode>(arg0.as<ExpressionNode>());
			va_list_name_handle = StringTable::getOrInternStringHandle(id.name());
		}
		
		// Detect if the user's va_list is a pointer type (e.g., typedef char* va_list;)
		bool va_list_is_pointer = isVaListPointerType(arg0, arg0_ir);
		
		// Get the second argument (last fixed parameter)
		ASTNode arg1 = functionCallNode.arguments()[1];
		auto arg1_ir = visitExpressionNode(arg1.as<ExpressionNode>());
		
		// The second argument should be an identifier (the parameter name)
		std::string_view last_param_name;
		if (std::holds_alternative<IdentifierNode>(arg1.as<ExpressionNode>())) {
			last_param_name = std::get<IdentifierNode>(arg1.as<ExpressionNode>()).name();
		} else {
			FLASH_LOG(Codegen, Error, "__builtin_va_start second argument must be a parameter name");
			return {Type::Void, 0, 0ULL, 0ULL};
		}
		
		// Platform-specific varargs implementation:
		// - Windows (MSVC mangling): variadic args on stack, use &last_param + 8
		// - Linux (Itanium mangling): variadic args in registers, initialize va_list structure
		
		if (context_->isItaniumMangling() && !va_list_is_pointer) {
			// Linux/System V AMD64 ABI: Use va_list structure
			// The structure has already been initialized in the function prologue by IRConverter.
			// We just need to assign the address of the va_list structure to the user's va_list variable.
			
			// Get address of the va_list structure
			TempVar va_list_struct_addr = var_counter.next();
			AddressOfOp struct_addr_op;
			struct_addr_op.result = va_list_struct_addr;
			struct_addr_op.operand.type = Type::Char;
			struct_addr_op.operand.size_in_bits = 8;
			struct_addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
			struct_addr_op.operand.value = StringTable::getOrInternStringHandle("__varargs_va_list_struct__"sv);
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(struct_addr_op), functionCallNode.called_from()));
			
			// Finally, assign the address of the va_list structure to the user's va_list variable (char* pointer)
			// Get the va_list variable from arg0_ir[2]
			std::variant<StringHandle, TempVar> va_list_var;
			if (va_list_name_handle.isValid()) {
				va_list_var = va_list_name_handle;
			} else if (std::holds_alternative<TempVar>(arg0_ir[2])) {
				va_list_var = std::get<TempVar>(arg0_ir[2]);
			} else if (std::holds_alternative<StringHandle>(arg0_ir[2])) {
				va_list_var = std::get<StringHandle>(arg0_ir[2]);
			} else {
				FLASH_LOG(Codegen, Error, "__builtin_va_start first argument must be a variable or temp");
				return {Type::Void, 0, 0ULL, 0ULL};
			}
			
			AssignmentOp final_assign;
			if (std::holds_alternative<StringHandle>(va_list_var)) {
				final_assign.result = std::get<StringHandle>(va_list_var);
				final_assign.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<StringHandle>(va_list_var)};
			} else {
				final_assign.result = std::get<TempVar>(va_list_var);
				final_assign.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<TempVar>(va_list_var)};
			}
			final_assign.rhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_addr};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(final_assign), functionCallNode.called_from()));
			
		} else {
			// va_list is a simple char* pointer type (typedef char* va_list;)
			// On Windows: variadic args are on the stack, so use &last_param + 8
			// On Linux: variadic args are in registers saved to reg_save_area, point there instead
			
			std::variant<StringHandle, TempVar> va_list_var;
			if (va_list_name_handle.isValid()) {
				va_list_var = va_list_name_handle;
			} else if (std::holds_alternative<TempVar>(arg0_ir[2])) {
				va_list_var = std::get<TempVar>(arg0_ir[2]);
			} else if (std::holds_alternative<StringHandle>(arg0_ir[2])) {
				va_list_var = std::get<StringHandle>(arg0_ir[2]);
			} else {
				FLASH_LOG(Codegen, Error, "__builtin_va_start first argument must be a variable or temp");
				return {Type::Void, 0, 0ULL, 0ULL};
			}
			
			if (context_->isItaniumMangling()) {
				// Linux/System V AMD64: Use va_list structure internally even for char* va_list
				// Phase 4: Point to the va_list structure so va_arg can access gp_offset and overflow_arg_area
				// This enables proper overflow support when >5 variadic int args are passed
				
				// Get address of va_list structure
				TempVar va_struct_addr = var_counter.next();
				AddressOfOp va_struct_op;
				va_struct_op.result = va_struct_addr;
				va_struct_op.operand.type = Type::Char;
				va_struct_op.operand.size_in_bits = 8;
				va_struct_op.operand.pointer_depth = 0;
				va_struct_op.operand.value = StringTable::getOrInternStringHandle("__varargs_va_list_struct__"sv);
				ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(va_struct_op), functionCallNode.called_from()));
				
				// Assign to va_list variable
				AssignmentOp assign_op;
				if (std::holds_alternative<StringHandle>(va_list_var)) {
					assign_op.result = std::get<StringHandle>(va_list_var);
					assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<StringHandle>(va_list_var)};
				} else {
					assign_op.result = std::get<TempVar>(va_list_var);
					assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<TempVar>(va_list_var)};
				}
				assign_op.rhs = TypedValue{Type::UnsignedLongLong, 64, va_struct_addr};
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
					return {Type::Void, 0, 0ULL, 0ULL};
				}
				const DeclarationNode& param_decl = param_symbol->as<DeclarationNode>();
				const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				
				addr_op.operand.type = param_type.type();
				addr_op.operand.size_in_bits = static_cast<int>(param_type.size_in_bits());
				addr_op.operand.pointer_depth = param_type.pointer_depth();
				addr_op.operand.value = StringTable::getOrInternStringHandle(last_param_name);
				ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), functionCallNode.called_from()));
				
				// Add 8 bytes (64 bits) to get to the next parameter slot
				TempVar va_start_addr = var_counter.next();
				BinaryOp add_op;
				add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, last_param_addr};
				add_op.rhs = TypedValue{Type::UnsignedLongLong, 64, 8ULL};
				add_op.result = va_start_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), functionCallNode.called_from()));
				
				// Assign to va_list variable
				AssignmentOp assign_op;
				if (std::holds_alternative<StringHandle>(va_list_var)) {
					assign_op.result = std::get<StringHandle>(va_list_var);
					assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<StringHandle>(va_list_var)};
				} else {
					assign_op.result = std::get<TempVar>(va_list_var);
					assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<TempVar>(va_list_var)};
				}
				assign_op.rhs = TypedValue{Type::UnsignedLongLong, 64, va_start_addr};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), functionCallNode.called_from()));
			}
		}
		
		// __builtin_va_start returns void
		return {Type::Void, 0, 0ULL, 0ULL};
	}
	
	// Generate IR for __builtin_unreachable intrinsic
	// This is an optimization hint that tells the compiler a code path is unreachable
	// Standard usage: after switch default: cases, or after functions that don't return
	// Implementation: We generate no actual code - this is purely an optimization hint
	// In a more advanced compiler, this would enable dead code elimination and assumptions
	std::vector<IrOperand> generateBuiltinUnreachableIntrinsic(const FunctionCallNode& functionCallNode) {
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
		return {Type::Void, 0, 0ULL, 0ULL};
	}
	
	// Generate IR for __builtin_assume intrinsic
	// This is an optimization hint that tells the compiler to assume a condition is true
	// Syntax: __builtin_assume(condition)
	// Implementation: We evaluate the condition but don't use the result
	// In a more advanced compiler, this would enable optimizations based on the assumption
	std::vector<IrOperand> generateBuiltinAssumeIntrinsic(const FunctionCallNode& functionCallNode) {
		if (functionCallNode.arguments().size() != 1) {
			FLASH_LOG(Codegen, Error, "__builtin_assume requires exactly 1 argument (condition)");
			return {Type::Void, 0, 0ULL, 0ULL};
		}
		
		// Evaluate the condition expression (but we don't use the result)
		// In a real implementation, we'd use this to inform the optimizer
		ASTNode condition = functionCallNode.arguments()[0];
		auto condition_ir = visitExpressionNode(condition.as<ExpressionNode>());
		
		// For now, we just evaluate the expression and ignore it
		// A more sophisticated implementation could:
		// 1. Track assumptions for later optimization passes
		// 2. Use assumptions for constant folding
		// 3. Enable more aggressive optimizations in conditional branches
		
		FLASH_LOG(Codegen, Debug, "__builtin_assume encountered - assumption recorded (not yet used for optimization)");
		
		// Return void (this intrinsic doesn't produce a value)
		return {Type::Void, 0, 0ULL, 0ULL};
	}
	
	// Generate IR for __builtin_expect intrinsic
	// This is a branch prediction hint: __builtin_expect(expr, expected_value)
	// Returns expr, but hints that expr will likely equal expected_value
	// Common usage: if (__builtin_expect(rare_condition, 0)) { /* unlikely path */ }
	std::vector<IrOperand> generateBuiltinExpectIntrinsic(const FunctionCallNode& functionCallNode) {
		if (functionCallNode.arguments().size() != 2) {
			FLASH_LOG(Codegen, Error, "__builtin_expect requires exactly 2 arguments (expr, expected_value)");
			// Return a default value matching typical usage (long type)
			return {Type::LongLong, 64, 0ULL, 0ULL};
		}
		
		// Evaluate the first argument (the expression)
		ASTNode expr = functionCallNode.arguments()[0];
		auto expr_ir = visitExpressionNode(expr.as<ExpressionNode>());
		
		// Evaluate the second argument (the expected value) but don't use it for now
		ASTNode expected = functionCallNode.arguments()[1];
		auto expected_ir = visitExpressionNode(expected.as<ExpressionNode>());
		
		// For now, we just return the expression value unchanged
		// A more sophisticated implementation could:
		// 1. Pass branch prediction hints to the code generator
		// 2. Reorder basic blocks to favor the expected path
		// 3. Use profile-guided optimization data
		
		FLASH_LOG(Codegen, Debug, "__builtin_expect encountered - branch prediction hint recorded (not yet used)");
		
		// Return the first argument (the expression value)
		return expr_ir;
	}
	
	// Generate IR for __builtin_launder intrinsic
	// This prevents the compiler from assuming anything about what a pointer points to
	// Syntax: __builtin_launder(ptr)
	// Essential for implementing std::launder and placement new operations
	// Returns the pointer unchanged, but creates an optimization barrier
	std::vector<IrOperand> generateBuiltinLaunderIntrinsic(const FunctionCallNode& functionCallNode) {
		if (functionCallNode.arguments().size() != 1) {
			FLASH_LOG(Codegen, Error, "__builtin_launder requires exactly 1 argument (pointer)");
			return {Type::UnsignedLongLong, 64, 0ULL, 0ULL};
		}
		
		// Evaluate the pointer argument
		ASTNode ptr_arg = functionCallNode.arguments()[0];
		auto ptr_ir = visitExpressionNode(ptr_arg.as<ExpressionNode>());
		
		// Extract pointer details
		[[maybe_unused]] Type ptr_type = std::get<Type>(ptr_ir[0]);
		[[maybe_unused]] int ptr_size = std::get<int>(ptr_ir[1]);
		
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
		return ptr_ir;
	}

	// GetExceptionCode() - SEH intrinsic
	// In a filter funclet: RCX = EXCEPTION_POINTERS*, reads ExceptionRecord->ExceptionCode directly
	// In __except body: reads from a parent-frame slot that was saved during filter evaluation
	// Returns unsigned int (DWORD)
	std::vector<IrOperand> generateGetExceptionCodeIntrinsic(const FunctionCallNode& functionCallNode) {
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
		return {Type::UnsignedInt, 32, result, 0ULL};
	}

	// _abnormal_termination() / AbnormalTermination() - SEH intrinsic
	// Only valid inside a __finally block.
	// ECX is saved to [rsp+8] in the finally funclet prologue; reads from there.
	// Returns int (0 = normal termination, non-zero = exception unwind)
	std::vector<IrOperand> generateAbnormalTerminationIntrinsic(const FunctionCallNode& functionCallNode) {
		TempVar result = var_counter.next();
		SehAbnormalTerminationOp op;
		op.result = result;
		ir_.addInstruction(IrInstruction(IrOpcode::SehAbnormalTermination, std::move(op), functionCallNode.called_from()));
		return {Type::Int, 32, result, 0ULL};
	}

	// GetExceptionInformation() - SEH intrinsic
	// In a filter funclet: RCX = EXCEPTION_POINTERS*, returns the pointer directly
	// Returns EXCEPTION_POINTERS* (pointer)
	std::vector<IrOperand> generateGetExceptionInformationIntrinsic(const FunctionCallNode& functionCallNode) {
		TempVar result = var_counter.next();
		SehExceptionIntrinsicOp op;
		op.result = result;
		ir_.addInstruction(IrInstruction(IrOpcode::SehGetExceptionInfo, std::move(op), functionCallNode.called_from()));
		return {Type::UnsignedLongLong, 64, result, 0ULL};
	}

