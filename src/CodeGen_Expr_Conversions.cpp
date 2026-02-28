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
			const DeclarationNode* decl = lookupDeclaration(identifier_handle);
			if (!decl) {
				return std::nullopt;  // Can't find identifier
			}
			
			// Get type info
			const TypeSpecifierNode* type_node = &decl->type_node().as<TypeSpecifierNode>();
			
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
				const DeclarationNode* decl_ptr = lookupDeclaration(array_name);
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
				
				const DeclarationNode* decl = lookupDeclaration(identifier_handle);
				
				if (decl) {
					const TypeSpecifierNode* type_node = &decl->type_node().as<TypeSpecifierNode>();
					
					if (type_node->type() == Type::Struct && type_node->pointer_depth() == 0) {
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

			const DeclarationNode* decl = lookupDeclaration(identifier_handle);
			if (!decl) {
				return false;
			}

			const TypeSpecifierNode* type_node = &decl->type_node().as<TypeSpecifierNode>();

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
								const DeclarationNode* decl_ptr = lookupDeclaration(array_name);
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
					const DeclarationNode* decl_ptr = lookupDeclaration(array_name);
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
				TempVar current_val = emitDereference(member->type, 64, 1, ptr_temp, token);
				
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
						std::optional<ASTNode> symbol = lookupSymbol(object_name);
						
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

			// Check for user-defined operator++ overload on struct types
			if (operandType == Type::Struct && operandIrOperands.size() >= 4) {
				TypeIndex operand_type_index = 0;
				if (std::holds_alternative<unsigned long long>(operandIrOperands[3])) {
					operand_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(operandIrOperands[3]));
				}
				if (operand_type_index > 0) {
					auto overload_result = findUnaryOperatorOverload(operand_type_index, "++");
					if (overload_result.has_overload) {
						const StructMemberFunction& member_func = *overload_result.member_overload;
						const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
						std::string_view struct_name = StringTable::getStringView(gTypeInfo[operand_type_index].name());
						TypeSpecifierNode return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
						// Resolve self-referential return type for template structs
						if (return_type.type() == Type::Struct && return_type.type_index() > 0 && return_type.type_index() < gTypeInfo.size()) {
							auto& ret_ti = gTypeInfo[return_type.type_index()];
							if (!ret_ti.struct_info_ || ret_ti.struct_info_->total_size == 0) {
								return_type.set_type_index(operand_type_index);
							}
						}
						std::vector<TypeSpecifierNode> param_types;
						if (!unaryOperatorNode.is_prefix()) {
							// Postfix: add dummy int parameter for mangling
							TypeSpecifierNode int_type(Type::Int, TypeQualifier::None, 32, Token());
							param_types.push_back(int_type);
						}
						std::vector<std::string_view> empty_namespace;
						auto mangled_name = NameMangling::generateMangledName(
							"operator++", return_type, param_types, false,
							struct_name, empty_namespace, Linkage::CPlusPlus
						);

						TempVar ret_var = var_counter.next();
						CallOp call_op;
						call_op.result = ret_var;
						call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
						call_op.return_type = return_type.type();
						call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
						if (call_op.return_size_in_bits == 0 && return_type.type_index() > 0 && return_type.type_index() < gTypeInfo.size() && gTypeInfo[return_type.type_index()].struct_info_) {
							call_op.return_size_in_bits = static_cast<int>(gTypeInfo[return_type.type_index()].struct_info_->total_size * 8);
						}
						call_op.return_type_index = return_type.type_index();
						call_op.is_member_function = true;

						// Take address of operand for 'this' pointer
						TempVar this_addr = var_counter.next();
						AddressOfOp addr_op;
						addr_op.result = this_addr;
						addr_op.operand = toTypedValue(operandIrOperands);
						addr_op.operand.pointer_depth = 0;
						ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));

						TypedValue this_arg;
						this_arg.type = operandType;
						this_arg.size_in_bits = 64;
						this_arg.value = this_addr;
						call_op.args.push_back(this_arg);

						ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), Token()));
						return { return_type.type(), call_op.return_size_in_bits, ret_var, static_cast<unsigned long long>(return_type.type_index()) };
					}
				}
			}
			
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

			// Check for user-defined operator-- overload on struct types
			if (operandType == Type::Struct && operandIrOperands.size() >= 4) {
				TypeIndex operand_type_index = 0;
				if (std::holds_alternative<unsigned long long>(operandIrOperands[3])) {
					operand_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(operandIrOperands[3]));
				}
				if (operand_type_index > 0) {
					auto overload_result = findUnaryOperatorOverload(operand_type_index, "--");
					if (overload_result.has_overload) {
						const StructMemberFunction& member_func = *overload_result.member_overload;
						const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
						std::string_view struct_name = StringTable::getStringView(gTypeInfo[operand_type_index].name());
						TypeSpecifierNode return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
						if (return_type.type() == Type::Struct && return_type.type_index() > 0 && return_type.type_index() < gTypeInfo.size()) {
							auto& ret_ti = gTypeInfo[return_type.type_index()];
							if (!ret_ti.struct_info_ || ret_ti.struct_info_->total_size == 0) {
								return_type.set_type_index(operand_type_index);
							}
						}
						std::vector<TypeSpecifierNode> param_types;
						if (!unaryOperatorNode.is_prefix()) {
							TypeSpecifierNode int_type(Type::Int, TypeQualifier::None, 32, Token());
							param_types.push_back(int_type);
						}
						std::vector<std::string_view> empty_namespace;
						auto mangled_name = NameMangling::generateMangledName(
							"operator--", return_type, param_types, false,
							struct_name, empty_namespace, Linkage::CPlusPlus
						);

						TempVar ret_var = var_counter.next();
						CallOp call_op;
						call_op.result = ret_var;
						call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
						call_op.return_type = return_type.type();
						call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
						if (call_op.return_size_in_bits == 0 && return_type.type_index() > 0 && return_type.type_index() < gTypeInfo.size() && gTypeInfo[return_type.type_index()].struct_info_) {
							call_op.return_size_in_bits = static_cast<int>(gTypeInfo[return_type.type_index()].struct_info_->total_size * 8);
						}
						call_op.return_type_index = return_type.type_index();
						call_op.is_member_function = true;

						TempVar this_addr = var_counter.next();
						AddressOfOp addr_op;
						addr_op.result = this_addr;
						addr_op.operand = toTypedValue(operandIrOperands);
						addr_op.operand.pointer_depth = 0;
						ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));

						TypedValue this_arg;
						this_arg.type = operandType;
						this_arg.size_in_bits = 64;
						this_arg.value = this_addr;
						call_op.args.push_back(this_arg);

						ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), Token()));
						return { return_type.type(), call_op.return_size_in_bits, ret_var, static_cast<unsigned long long>(return_type.type_index()) };
					}
				}
			}
			
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
						const DeclarationNode* decl = symbol.has_value() ? get_decl_from_symbol(*symbol) : nullptr;
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

