	// Helper struct for multidimensional array access
	struct MultiDimArrayAccess {
		std::string_view base_array_name;
		std::vector<ASTNode> indices;  // Indices from outermost to innermost
		const DeclarationNode* base_decl = nullptr;
		bool is_valid = false;
	};

	// Helper struct for multidimensional member array access  
	struct MultiDimMemberArrayAccess {
		std::string_view object_name;
		std::string_view member_name;
		std::vector<ASTNode> indices;  // Indices from outermost to innermost
		const StructMember* member_info = nullptr;
		bool is_valid = false;
	};

	// Helper function to collect all indices from a chain of ArraySubscriptNodes for member arrays
	// For obj.arr[i][j][k], returns {object="obj", member="arr", indices=[i, j, k]}
	MultiDimMemberArrayAccess collectMultiDimMemberArrayIndices(const ArraySubscriptNode& subscript) {
		MultiDimMemberArrayAccess result;
		std::vector<ASTNode> indices_reversed;
		const ExpressionNode* current = &subscript.array_expr().as<ExpressionNode>();
		
		// Collect the outermost index first
		indices_reversed.push_back(subscript.index_expr());
		
		// Walk down the chain of ArraySubscriptNodes
		while (std::holds_alternative<ArraySubscriptNode>(*current)) {
			const ArraySubscriptNode& inner = std::get<ArraySubscriptNode>(*current);
			indices_reversed.push_back(inner.index_expr());
			current = &inner.array_expr().as<ExpressionNode>();
		}
		
		FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: Collected {} indices", indices_reversed.size());
		
		// The base should be a member access (obj.member)
		if (std::holds_alternative<MemberAccessNode>(*current)) {
			const MemberAccessNode& base_member = std::get<MemberAccessNode>(*current);
			result.member_name = base_member.member_name();
			FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: Found MemberAccessNode, member_name={}", 
				std::string(result.member_name));
			
			// Get the object
			if (base_member.object().is<ExpressionNode>()) {
				const ExpressionNode& obj_expr = base_member.object().as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(obj_expr)) {
					const IdentifierNode& object_ident = std::get<IdentifierNode>(obj_expr);
					result.object_name = object_ident.name();
					FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: object_name={}", std::string(result.object_name));
					
					// Look up the object to get struct type
					std::optional<ASTNode> symbol = symbol_table.lookup(result.object_name);
					FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: symbol.has_value()={}", symbol.has_value());
					if (symbol.has_value()) {
						FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: symbol->is<DeclarationNode>()={}", symbol->is<DeclarationNode>());
						FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: symbol->is<VariableDeclarationNode>()={}", symbol->is<VariableDeclarationNode>());
					}
					// Try both DeclarationNode and VariableDeclarationNode
					const DeclarationNode* decl_node = nullptr;
					if (symbol.has_value()) {
						if (symbol->is<DeclarationNode>()) {
							decl_node = &symbol->as<DeclarationNode>();
						} else if (symbol->is<VariableDeclarationNode>()) {
							decl_node = &symbol->as<VariableDeclarationNode>().declaration();
						}
					}
					
					if (decl_node) {
						const auto& type_node = decl_node->type_node().as<TypeSpecifierNode>();
						
						FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: Found decl, is_struct={}, type_index={}", 
							is_struct_type(type_node.type()), type_node.type_index());
						
						if (is_struct_type(type_node.type()) && type_node.type_index() < gTypeInfo.size()) {
							TypeIndex type_index = type_node.type_index();
							auto member_result = FlashCpp::gLazyMemberResolver.resolve(
								type_index,
								StringTable::getOrInternStringHandle(std::string(result.member_name)));
							
							FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: gLazyMemberResolver.resolve returned {}", static_cast<bool>(member_result));
							
							if (member_result) {
								const StructMember* member = member_result.member;
								result.member_info = member;
								
								FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: member->is_array={}, array_dimensions.size()={}", 
									member->is_array, member->array_dimensions.size());
								
								// Reverse the indices so they're in order from outermost to innermost
								result.indices.reserve(indices_reversed.size());
								for (auto it = indices_reversed.rbegin(); it != indices_reversed.rend(); ++it) {
									result.indices.push_back(*it);
								}
								
								// Valid if member is a multidimensional array with matching indices
								result.is_valid = member->is_array && 
								                  !member->array_dimensions.empty() &&
								                  (member->array_dimensions.size() == result.indices.size()) &&
								                  (result.indices.size() > 1);
								
								FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: is_valid={} (is_array={}, dim_size={}, indices_size={}, indices>1={})",
									result.is_valid, member->is_array, member->array_dimensions.size(), 
									result.indices.size(), (result.indices.size() > 1));
							}
						}
					}
				}
			}
		}
		
		return result;
	}

	// Helper function to collect all indices from a chain of ArraySubscriptNodes
	// For arr[i][j][k], returns {base="arr", indices=[i, j, k]}
	MultiDimArrayAccess collectMultiDimArrayIndices(const ArraySubscriptNode& subscript) {
		MultiDimArrayAccess result;
		std::vector<ASTNode> indices_reversed;
		const ExpressionNode* current = &subscript.array_expr().as<ExpressionNode>();
		
		// Collect the outermost index first (the one in the current subscript)
		indices_reversed.push_back(subscript.index_expr());
		
		// Walk down the chain of ArraySubscriptNodes
		while (std::holds_alternative<ArraySubscriptNode>(*current)) {
			const ArraySubscriptNode& inner = std::get<ArraySubscriptNode>(*current);
			indices_reversed.push_back(inner.index_expr());
			current = &inner.array_expr().as<ExpressionNode>();
		}
		
		// The base should be an identifier
		if (std::holds_alternative<IdentifierNode>(*current)) {
			const IdentifierNode& base_ident = std::get<IdentifierNode>(*current);
			result.base_array_name = base_ident.name();
			
			// Look up the declaration
			result.base_decl = lookupDeclaration(result.base_array_name);
			
			// Reverse the indices so they're in order from outermost to innermost
			// For arr[i][j], we collected [j, i], now reverse to [i, j]
			result.indices.reserve(indices_reversed.size());
			for (auto it = indices_reversed.rbegin(); it != indices_reversed.rend(); ++it) {
				result.indices.push_back(*it);
			}
			
			result.is_valid = (result.base_decl != nullptr) && 
			                  (result.base_decl->array_dimension_count() == result.indices.size()) &&
			                  (result.indices.size() > 1);  // Only valid for multidimensional
		}
		
		return result;
	}

	std::vector<IrOperand> generateArraySubscriptIr(const ArraySubscriptNode& arraySubscriptNode,
	                                                 ExpressionContext context = ExpressionContext::Load) {
		// Generate IR for array[index] expression
		// This computes the address: base_address + (index * element_size)

		// Check for multidimensional array access pattern (arr[i][j])
		// If the array expression is itself an ArraySubscriptNode, we have a multidimensional access
		const ExpressionNode& array_expr = arraySubscriptNode.array_expr().as<ExpressionNode>();
		FLASH_LOG_FORMAT(Codegen, Debug, "generateArraySubscriptIr: array_expr is ArraySubscriptNode = {}",
			std::holds_alternative<ArraySubscriptNode>(array_expr));
		if (std::holds_alternative<ArraySubscriptNode>(array_expr)) {
			// First check if this is a multidimensional member array access (obj.arr[i][j])
			auto member_multi_dim = collectMultiDimMemberArrayIndices(arraySubscriptNode);
			FLASH_LOG_FORMAT(Codegen, Debug, "Member multidim check: is_valid={}", member_multi_dim.is_valid);
			
			if (member_multi_dim.is_valid && member_multi_dim.member_info) {
				FLASH_LOG(Codegen, Debug, "Flattening multidimensional member array access!");
				// We have a valid multidimensional member array access
				// For obj.arr[M][N] accessed as obj.arr[i][j], compute flat_index = i*N + j
				
				const StructMember* member = member_multi_dim.member_info;
				Type element_type = member->type;
				int base_element_size = get_type_size_bits(element_type);
				
				// Get all dimension sizes
				const std::vector<size_t>& dim_sizes = member->array_dimensions;
				
				// Compute strides: stride[k] = product of dimensions after k
				std::vector<size_t> strides(dim_sizes.size());
				strides.back() = 1;
				for (int k = static_cast<int>(dim_sizes.size()) - 2; k >= 0; --k) {
					strides[k] = strides[k + 1] * dim_sizes[k + 1];
				}
				
				// Generate code to compute flat index
				auto idx0_operands = visitExpressionNode(member_multi_dim.indices[0].as<ExpressionNode>());
				TempVar flat_index = var_counter.next();
				
				if (strides[0] == 1) {
					BinaryOp add_op;
					add_op.lhs = toTypedValue(idx0_operands);
					add_op.rhs = TypedValue{Type::Int, 32, 0ULL};
					add_op.result = IrValue{flat_index};
					ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
				} else {
					BinaryOp mul_op;
					mul_op.lhs = toTypedValue(idx0_operands);
					mul_op.rhs = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(strides[0])};
					mul_op.result = IrValue{flat_index};
					ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));
				}
				
				// Add remaining indices
				for (size_t k = 1; k < member_multi_dim.indices.size(); ++k) {
					auto idx_operands = visitExpressionNode(member_multi_dim.indices[k].as<ExpressionNode>());
					
					if (strides[k] == 1) {
						TempVar new_flat = var_counter.next();
						BinaryOp add_op;
						add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
						add_op.rhs = toTypedValue(idx_operands);
						add_op.result = IrValue{new_flat};
						ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
						flat_index = new_flat;
					} else {
						TempVar temp_prod = var_counter.next();
						BinaryOp mul_op;
						mul_op.lhs = toTypedValue(idx_operands);
						mul_op.rhs = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(strides[k])};
						mul_op.result = IrValue{temp_prod};
						ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));
						
						TempVar new_flat = var_counter.next();
						BinaryOp add_op;
						add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
						add_op.rhs = TypedValue{Type::UnsignedLongLong, 64, temp_prod};
						add_op.result = IrValue{new_flat};
						ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
						flat_index = new_flat;
					}
				}
				
				// Generate single array access with flat index
				TempVar result_var = var_counter.next();
				StringHandle qualified_name = StringTable::getOrInternStringHandle(
					StringBuilder().append(member_multi_dim.object_name).append(".").append(member_multi_dim.member_name));
				
				LValueInfo lvalue_info(
					LValueInfo::Kind::ArrayElement,
					qualified_name,
					static_cast<int64_t>(member->offset)
				);
				lvalue_info.array_index = IrValue{flat_index};
				lvalue_info.is_pointer_to_array = false;
				setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));
				
				ArrayAccessOp payload;
				payload.result = result_var;
				payload.element_type = element_type;
				payload.element_size_in_bits = base_element_size;
				payload.array = qualified_name;
				payload.member_offset = static_cast<int64_t>(member->offset);
				payload.is_pointer_to_array = false;
				payload.index.type = Type::UnsignedLongLong;
				payload.index.size_in_bits = 64;
				payload.index.value = flat_index;
				
				if (context == ExpressionContext::LValueAddress) {
					return { element_type, base_element_size, result_var, 0ULL };
				}
				
				ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));
				return { element_type, base_element_size, result_var, 0ULL };
			}
			
			// This could be a multidimensional array access
			auto multi_dim = collectMultiDimArrayIndices(arraySubscriptNode);
			
			if (multi_dim.is_valid && multi_dim.base_decl) {
				// We have a valid multidimensional array access
				// For arr[M][N][P] accessed as arr[i][j][k], compute flat_index = i*N*P + j*P + k
				
				const auto& type_node = multi_dim.base_decl->type_node().as<TypeSpecifierNode>();
				Type element_type = type_node.type();
				int element_size_bits = static_cast<int>(type_node.size_in_bits());
				size_t element_type_index = (element_type == Type::Struct) ? type_node.type_index() : 0;
				
				// Get element size for struct types
				if (element_size_bits == 0 && element_type == Type::Struct && element_type_index > 0) {
					const TypeInfo& type_info = gTypeInfo[element_type_index];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						element_size_bits = static_cast<int>(struct_info->total_size * 8);
					}
				}
				
				// Get all dimension sizes
				std::vector<size_t> dim_sizes;
				const auto& dims = multi_dim.base_decl->array_dimensions();
				for (const auto& dim_expr : dims) {
					ConstExpr::EvaluationContext ctx(symbol_table);
					auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
					if (eval_result.success() && eval_result.as_int() > 0) {
						dim_sizes.push_back(static_cast<size_t>(eval_result.as_int()));
					} else {
						// Can't evaluate dimension at compile time, fall back to regular handling
						break;
					}
				}
				
				if (dim_sizes.size() == multi_dim.indices.size()) {
					// All dimensions evaluated successfully, compute flat index
					// For arr[D0][D1][D2] accessed as arr[i0][i1][i2]:
					// flat_index = i0 * (D1*D2) + i1 * D2 + i2
					
					// First, compute strides: stride[k] = product of dimensions after k
					std::vector<size_t> strides(dim_sizes.size());
					strides.back() = 1;
					for (int k = static_cast<int>(dim_sizes.size()) - 2; k >= 0; --k) {
						strides[k] = strides[k + 1] * dim_sizes[k + 1];
					}
					
					// Generate code to compute flat index
					// Start with the first index times its stride
					auto idx0_operands = visitExpressionNode(multi_dim.indices[0].as<ExpressionNode>());
					TempVar flat_index = var_counter.next();
					
					if (strides[0] == 1) {
						// Simple case: stride is 1, just copy the index
						// Use Add with 0 to effectively copy
						BinaryOp add_op;
						add_op.lhs = toTypedValue(idx0_operands);
						add_op.rhs = TypedValue{Type::Int, 32, 0ULL};
						add_op.result = IrValue{flat_index};
						ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
					} else {
						// flat_index = indices[0] * strides[0]
						BinaryOp mul_op;
						mul_op.lhs = toTypedValue(idx0_operands);
						mul_op.rhs = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(strides[0])};
						mul_op.result = IrValue{flat_index};
						ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));
					}
					
					// Add remaining indices: flat_index += indices[k] * strides[k]
					for (size_t k = 1; k < multi_dim.indices.size(); ++k) {
						auto idx_operands = visitExpressionNode(multi_dim.indices[k].as<ExpressionNode>());
						
						if (strides[k] == 1) {
							// flat_index += indices[k]
							TempVar new_flat = var_counter.next();
							BinaryOp add_op;
							add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
							add_op.rhs = toTypedValue(idx_operands);
							add_op.result = IrValue{new_flat};
							ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
							flat_index = new_flat;
						} else {
							// temp = indices[k] * strides[k]
							TempVar temp_prod = var_counter.next();
							BinaryOp mul_op;
							mul_op.lhs = toTypedValue(idx_operands);
							mul_op.rhs = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(strides[k])};
							mul_op.result = IrValue{temp_prod};
							ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));
							
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
					
					// Now generate the array access using the flat index
					TempVar result_var = var_counter.next();
					
					// Mark array element access as lvalue using metadata system
					LValueInfo lvalue_info(
						LValueInfo::Kind::ArrayElement,
						StringTable::getOrInternStringHandle(multi_dim.base_array_name),
						0  // offset computed dynamically by index
					);
					lvalue_info.array_index = IrValue{flat_index};
					lvalue_info.is_pointer_to_array = false;  // This is a real array, not a pointer
					setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));
					
					// Create ArrayAccessOp with the flat index
					ArrayAccessOp payload;
					payload.result = result_var;
					payload.element_type = element_type;
					payload.element_size_in_bits = element_size_bits;
					payload.member_offset = 0;
					payload.is_pointer_to_array = false;
					payload.array = StringTable::getOrInternStringHandle(multi_dim.base_array_name);
					payload.index.type = Type::UnsignedLongLong;
					payload.index.size_in_bits = 64;
					payload.index.value = flat_index;
					
					if (context == ExpressionContext::LValueAddress) {
						// Don't emit ArrayAccess instruction (no load)
						return { element_type, element_size_bits, result_var, static_cast<unsigned long long>(element_type_index) };
					}
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));
					
					return { element_type, element_size_bits, result_var, static_cast<unsigned long long>(element_type_index) };
				}
			}
		}

		// Check if the array expression is a member access (e.g., obj.array[index])
		if (std::holds_alternative<MemberAccessNode>(array_expr)) {
			const MemberAccessNode& member_access = std::get<MemberAccessNode>(array_expr);
			const ASTNode& object_node = member_access.object();
			std::string_view member_name = member_access.member_name();

			// Handle simple case: obj.array[index]
			if (object_node.is<ExpressionNode>()) {
				const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(obj_expr)) {
					const IdentifierNode& object_ident = std::get<IdentifierNode>(obj_expr);
					std::string_view object_name = object_ident.name();

					// Look up the object to get struct type
					const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
					if (symbol.has_value() && symbol->is<DeclarationNode>()) {
						const auto& decl_node = symbol->as<DeclarationNode>();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

						if (is_struct_type(type_node.type())) {
							TypeIndex struct_type_index = type_node.type_index();
							if (struct_type_index < gTypeInfo.size()) {
								auto member_result = FlashCpp::gLazyMemberResolver.resolve(
									struct_type_index,
									StringTable::getOrInternStringHandle(std::string(member_name)));
								
								if (member_result) {
									const StructMember* member = member_result.member;
									// Get index expression
									auto index_operands = visitExpressionNode(arraySubscriptNode.index_expr().as<ExpressionNode>());

									// Get element type and size from the member
									Type element_type = member->type;
									int element_size_bits = static_cast<int>(member->size * 8);
									
									// For array members, member->size is the total size, we need element size
									// This is a simplified assumption - we need better array type info
									// For now, assume arrays of primitives and compute element size
									// TODO: Get actual array length from type info
									// For now, use a heuristic: if size is larger than element type, it's an array
									int base_element_size = get_type_size_bits(element_type);  // Use existing helper
									
									if (base_element_size > 0 && element_size_bits > base_element_size) {
										// It's an array
										element_size_bits = base_element_size;
									}

									// Create a temporary variable for the result
									TempVar result_var = var_counter.next();
									
									// Mark array element access as lvalue (Option 2: Value Category Tracking)
									StringHandle qualified_name = StringTable::getOrInternStringHandle(
										StringBuilder().append(object_name).append(".").append(member_name));
									LValueInfo lvalue_info(
										LValueInfo::Kind::ArrayElement,
										qualified_name,
										static_cast<int64_t>(member_result.adjusted_offset)  // member offset in struct
									);
									// Store index information for unified assignment handler
									lvalue_info.array_index = toIrValue(index_operands[2]);
									lvalue_info.is_pointer_to_array = false;  // Member arrays are actual arrays, not pointers
									setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));

									// Create typed payload for ArrayAccess with qualified member name
									ArrayAccessOp payload;
									payload.result = result_var;
									payload.element_type = element_type;
									payload.element_size_in_bits = element_size_bits;
									payload.array = StringTable::getOrInternStringHandle(StringBuilder().append(object_name).append(".").append(member_name));
									payload.member_offset = static_cast<int64_t>(member_result.adjusted_offset);
									payload.is_pointer_to_array = false;  // Member arrays are actual arrays, not pointers
									
									// Set index as TypedValue
									payload.index.type = std::get<Type>(index_operands[0]);
									payload.index.size_in_bits = std::get<int>(index_operands[1]);
									if (std::holds_alternative<unsigned long long>(index_operands[2])) {
										payload.index.value = std::get<unsigned long long>(index_operands[2]);
									} else if (std::holds_alternative<TempVar>(index_operands[2])) {
										payload.index.value = std::get<TempVar>(index_operands[2]);
									} else if (std::holds_alternative<StringHandle>(index_operands[2])) {
										payload.index.value = std::get<StringHandle>(index_operands[2]);
									}

									// When context is LValueAddress, skip the load and return address/metadata only
									if (context == ExpressionContext::LValueAddress) {
										// Don't emit ArrayAccess instruction (no load)
										// Just return the metadata with the result temp var
										return { element_type, element_size_bits, result_var, 0ULL };
									}

									// Create instruction with typed payload (Load context - default)
									ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));

									// Return the result with the element type
									return { element_type, element_size_bits, result_var, 0ULL };
								}
							}
						}
					}
				}
			}
		}

		// Fall back to default handling for regular arrays
		// Get the array expression (should be an identifier for now)
		auto array_operands = visitExpressionNode(arraySubscriptNode.array_expr().as<ExpressionNode>());

		// Get the index expression
		auto index_operands = visitExpressionNode(arraySubscriptNode.index_expr().as<ExpressionNode>());

		// Get array type information
		Type element_type = std::get<Type>(array_operands[0]);
		int element_size_bits = std::get<int>(array_operands[1]);

		// Check if this is a pointer type (e.g., int* arr)
		// If so, we need to get the base type size, not the pointer size (64)
		// Look up the identifier to get the actual type info
		bool is_pointer_to_array = false;
		size_t element_type_index = 0;  // Track type_index for struct elements
		int element_pointer_depth = 0;  // Track pointer depth for pointer array elements
		const ExpressionNode& arr_expr = arraySubscriptNode.array_expr().as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(arr_expr)) {
			const IdentifierNode& arr_ident = std::get<IdentifierNode>(arr_expr);
			const DeclarationNode* decl_ptr = lookupDeclaration(arr_ident.name());
			if (decl_ptr) {
				const auto& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
				
				// Capture type_index for struct types (important for member access on array elements)
				if (type_node.type() == Type::Struct) {
					element_type_index = type_node.type_index();
				}
				
				// For array types, ALWAYS get the element size from type_node, not from array_operands
				// array_operands[1] contains 64 (pointer size) for arrays, not the element size
				if (decl_ptr->is_array() || type_node.is_array()) {
					// Check if this is an array of pointers (e.g., int* ptrs[3])
					// In this case, the element size should be the pointer size (64 bits), not the base type size
					if (type_node.pointer_depth() > 0) {
						// Array of pointers: element size is always 64 bits (pointer size)
						element_size_bits = 64;
						// Track pointer depth for the array element (e.g., for int* arr[3], element has pointer_depth=1)
						element_pointer_depth = type_node.pointer_depth();
					} else {
						// Get the element size from type_node
						element_size_bits = static_cast<int>(type_node.size_in_bits());
						// If still 0, compute from type info for struct types
						if (element_size_bits == 0 && type_node.type() == Type::Struct && element_type_index > 0) {
							const TypeInfo& type_info = gTypeInfo[element_type_index];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info) {
								element_size_bits = static_cast<int>(struct_info->total_size * 8);
							}
						}
					}
				}
				// For array parameters with explicit size (e.g., reference-to-array params),
				// we need pointer indirection
				// NOTE: Local arrays with explicit size (e.g., int arr[3]) are NOT pointers
				// EXCEPTION: Reference-to-array parameters (e.g., int (&arr)[3]) ARE pointers
				if (type_node.is_array() && decl_ptr->array_size().has_value()) {
					// Check if this is a reference to an array (parameter)
					// References to arrays need pointer indirection
					if (type_node.is_reference() || type_node.is_rvalue_reference()) {
						is_pointer_to_array = true;
					}
					// Local arrays with explicit size are NOT pointers (they're actual arrays on stack)
					// We don't set is_pointer_to_array for non-reference arrays
				}
				// For pointer types or reference types (not arrays), get the pointee size
				// BUT: Skip this if we already handled an array of pointers above (decl_ptr->is_array() case)
				else if (!decl_ptr->is_array() && (type_node.pointer_depth() > 0 || type_node.is_reference() || type_node.is_rvalue_reference())) {
					// Get the base type size (what the pointer points to)
					element_size_bits = static_cast<int>(type_node.size_in_bits());
					is_pointer_to_array = true;  // This is a pointer or reference, not an actual array
				}
			}
		}
		
		// Fix element size for array members accessed through TempVar (e.g., vls.values[i])
		// When array comes from member_access, element_size_bits is the TOTAL array size (e.g., 640 bits for int[20])
		// We need to derive the actual element size from the element type
		if (std::holds_alternative<TempVar>(array_operands[2]) && !is_pointer_to_array) {
			// Check if element_size_bits is much larger than expected for element_type
			int base_element_size = get_type_size_bits(element_type);
			if (base_element_size > 0 && element_size_bits > base_element_size) {
				// This is likely an array where we got the total size instead of element size
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Array subscript on TempVar: fixing element_size from {} bits (total) to {} bits (element)",
					element_size_bits, base_element_size);
				element_size_bits = base_element_size;
			}
		}

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();
		
		// If the array expression resolved to a TempVar that actually refers to a member,
		// recover the qualified name and offset from its lvalue metadata so we don't lose
		// struct/offset information (important for member arrays).
		std::variant<StringHandle, TempVar> base_variant;
		int base_member_offset = 0;
		bool base_is_pointer_to_member = false;
		// Fast-path: if the array expression is a member access, rebuild qualified name directly
		if (std::holds_alternative<MemberAccessNode>(array_expr)) {
			const auto& member_access = std::get<MemberAccessNode>(array_expr);
			if (member_access.object().is<ExpressionNode>()) {
				const ExpressionNode& obj_expr = member_access.object().as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(obj_expr)) {
					const auto& object_ident = std::get<IdentifierNode>(obj_expr);
					std::string_view object_name = object_ident.name();
					auto symbol = symbol_table.lookup(object_name);
					if (symbol.has_value() && symbol->is<DeclarationNode>()) {
						const auto& decl_node = symbol->as<DeclarationNode>();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
						if (is_struct_type(type_node.type()) && type_node.type_index() < gTypeInfo.size()) {
							auto member_result = FlashCpp::gLazyMemberResolver.resolve(
								type_node.type_index(),
								StringTable::getOrInternStringHandle(std::string(member_access.member_name())));
							if (member_result) {
								base_variant = StringTable::getOrInternStringHandle(
									StringBuilder().append(object_name).append(".").append(member_access.member_name()));
								base_member_offset = static_cast<int>(member_result.adjusted_offset);
								// Member access via '.' is not a pointer access for locals
							}
						}
					}
				}
			}
			// If object isn't a simple identifier (e.g., arr[i].member), fall back to using the
			// computed operands to keep a valid base (TempVar or StringHandle) instead of
			// leaving an empty StringHandle that leads to invalid offsets.
			if (base_variant.valueless_by_exception()) {
				if (std::holds_alternative<TempVar>(array_operands[2])) {
					base_variant = std::get<TempVar>(array_operands[2]);
				} else if (std::holds_alternative<StringHandle>(array_operands[2])) {
					base_variant = std::get<StringHandle>(array_operands[2]);
				}
			}
		}
		// Simple identifier array (non-member)
		else if (std::holds_alternative<IdentifierNode>(array_expr)) {
			const auto& ident = std::get<IdentifierNode>(array_expr);
			base_variant = StringTable::getOrInternStringHandle(ident.name());
		}
		if (std::holds_alternative<TempVar>(array_operands[2])) {
			TempVar base_temp = std::get<TempVar>(array_operands[2]);
			if (auto base_lv = getTempVarLValueInfo(base_temp)) {
				if (base_lv->kind == LValueInfo::Kind::Member && base_lv->member_name.has_value()) {
					// Build qualified name: object.member
					if (std::holds_alternative<StringHandle>(base_lv->base)) {
						auto obj_name = std::get<StringHandle>(base_lv->base);
						base_variant = StringTable::getOrInternStringHandle(
							StringBuilder().append(StringTable::getStringView(obj_name))
							                .append(".")
							                .append(StringTable::getStringView(base_lv->member_name.value())));
						base_member_offset = base_lv->offset;
						base_is_pointer_to_member = base_lv->is_pointer_to_member;
					}
				}
			}
		}
		if (!std::holds_alternative<StringHandle>(base_variant)) {
			if (std::holds_alternative<StringHandle>(array_operands[2])) {
				base_variant = std::get<StringHandle>(array_operands[2]);
			}
		}
		// Prefer keeping TempVar base when available to preserve stack offsets for nested accesses
		if (!std::holds_alternative<TempVar>(base_variant) && std::holds_alternative<TempVar>(array_operands[2])) {
			base_variant = std::get<TempVar>(array_operands[2]);
		}
		
		// Mark array element access as lvalue (Option 2: Value Category Tracking)
		// arr[i] is an lvalue - it designates an object with a stable address
		LValueInfo lvalue_info(
			LValueInfo::Kind::ArrayElement,
			base_variant,
			base_member_offset  // offset for member arrays (otherwise 0)
		);
		// Store index information for unified assignment handler
		// Support both constant and variable indices
		lvalue_info.array_index = toIrValue(index_operands[2]);
		FLASH_LOG(Codegen, Debug, "Array index stored in metadata (supports constants and variables)");
		lvalue_info.is_pointer_to_array = is_pointer_to_array || base_is_pointer_to_member;
		setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));

		// Create typed payload for ArrayAccess
		ArrayAccessOp payload;
		payload.result = result_var;
		payload.element_type = element_type;
		payload.element_size_in_bits = element_size_bits;
		payload.member_offset = 0;  // Not a member array
		payload.is_pointer_to_array = is_pointer_to_array;
		
		// Set array (either variable name or temp)
		if (std::holds_alternative<StringHandle>(array_operands[2])) {
			payload.array = std::get<StringHandle>(array_operands[2]);
		} else if (std::holds_alternative<TempVar>(array_operands[2])) {
			payload.array = std::get<TempVar>(array_operands[2]);
		}
		
		// Set index as TypedValue
		Type index_type = std::get<Type>(index_operands[0]);
		int index_size = std::get<int>(index_operands[1]);
		payload.index.type = index_type;
		payload.index.size_in_bits = index_size;
		
		if (std::holds_alternative<unsigned long long>(index_operands[2])) {
			payload.index.value = std::get<unsigned long long>(index_operands[2]);
		} else if (std::holds_alternative<TempVar>(index_operands[2])) {
			payload.index.value = std::get<TempVar>(index_operands[2]);
		} else if (std::holds_alternative<StringHandle>(index_operands[2])) {
			payload.index.value = std::get<StringHandle>(index_operands[2]);
		}

		// When context is LValueAddress, skip the load and return address/metadata only
		if (context == ExpressionContext::LValueAddress) {
			// Don't emit ArrayAccess instruction (no load)
			// Just return the metadata with the result temp var
			// The metadata contains all information needed for store operations
			// For the 4th element: 
			// - For struct types, return type_index
			// - For pointer array elements, return pointer_depth
			// - Otherwise return 0
			unsigned long long fourth_element = (element_type == Type::Struct)
				? static_cast<unsigned long long>(element_type_index)
				: ((element_pointer_depth > 0) ? static_cast<unsigned long long>(element_pointer_depth) : 0ULL);
			return { element_type, element_size_bits, result_var, fourth_element };
		}

		// Create instruction with typed payload (Load context - default)
		ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));

		// Return [element_type, element_size_bits, result_var, struct_type_index or pointer_depth]
		// For the 4th element: 
		// - For struct types, return type_index
		// - For pointer array elements, return pointer_depth
		// - Otherwise return 0
		unsigned long long fourth_element = (element_type == Type::Struct)
			? static_cast<unsigned long long>(element_type_index)
			: ((element_pointer_depth > 0) ? static_cast<unsigned long long>(element_pointer_depth) : 0ULL);
		return { element_type, element_size_bits, result_var, fourth_element };
	}

	// Helper function to validate and setup identifier-based member access
	// Returns true on success, false on error
	bool validateAndSetupIdentifierMemberAccess(
		std::string_view object_name,
		std::variant<StringHandle, TempVar>& base_object,
		Type& base_type,
		size_t& base_type_index,
		bool& is_pointer_dereference) {
		
		// Look up the object in the symbol table (local first, then global)
		std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
		
		// If not found locally, try global symbol table (for global struct variables)
		if (!symbol.has_value() && global_symbol_table_) {
			symbol = global_symbol_table_->lookup(object_name);
		}
		
		// If not found in symbol tables, check if it's a type name (for static member access like ClassName::member)
		if (!symbol.has_value()) {
			FLASH_LOG(Codegen, Debug, "validateAndSetupIdentifierMemberAccess: object_name='", object_name, "' not in symbol table, checking gTypesByName");
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(object_name));
			if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
				// This is a type name - set up for static member access
				FLASH_LOG(Codegen, Debug, "Found type '", object_name, "' in gTypesByName with type_index=", type_it->second->type_index_);
				base_object = StringTable::getOrInternStringHandle(object_name);
				base_type = Type::Struct;
				base_type_index = type_it->second->type_index_;
				is_pointer_dereference = false;  // Type names don't need dereferencing
				return true;
			}
			
			FLASH_LOG(Codegen, Error, "object '", object_name, "' not found in symbol table or type registry");
			return false;
		}

		// Use helper to get DeclarationNode from either DeclarationNode or VariableDeclarationNode
		const DeclarationNode* object_decl_ptr = get_decl_from_symbol(*symbol);
		if (!object_decl_ptr) {
			FLASH_LOG(Codegen, Error, "object '", object_name, "' is not a declaration");
			return false;
		}
		const DeclarationNode& object_decl = *object_decl_ptr;
		const TypeSpecifierNode& object_type = object_decl.type_node().as<TypeSpecifierNode>();

		// Verify this is a struct type (or a pointer/reference to a struct type)
		// References and pointers are automatically dereferenced for member access
		// Note: Type can be either Struct or UserDefined (for user-defined types like Point)
		// For pointers, the type might be Void with pointer_depth > 0 and type_index pointing to struct
		bool is_valid_for_member_access = is_struct_type(object_type.type()) || 
		                                  (object_type.pointer_depth() > 0 && object_type.type_index() > 0);
		if (!is_valid_for_member_access) {
			FLASH_LOG(Codegen, Error, "member access '.' on non-struct type '", object_name, "'");
			return false;
		}

		base_object = StringTable::getOrInternStringHandle(object_name);
		base_type = object_type.type();
		base_type_index = object_type.type_index();
		
		// Check if this is a pointer to struct (e.g., P* pp) or a reference to struct (e.g., P& pr)
		// In this case, member access like pp->member or pr.member should be treated as pointer dereference
		// References are implemented as pointers internally, so they need the same treatment
		if (object_type.pointer_depth() > 0 || object_type.is_reference() || object_type.is_rvalue_reference()) {
			is_pointer_dereference = true;
		}
		
		return true;
	}

	// Helper: extract base_type, base_object, and base_type_index from IR operands [type, size_bits, value, type_index?]
	bool extractBaseFromOperands(
		const std::vector<IrOperand>& operands,
		std::variant<StringHandle, TempVar>& base_object,
		Type& base_type,
		size_t& base_type_index,
		std::string_view error_context) {

		if (operands.size() < 3) {
			FLASH_LOG(Codegen, Error, "Failed to evaluate ", error_context, " for member access");
			return false;
		}
		base_type = std::get<Type>(operands[0]);
		if (std::holds_alternative<TempVar>(operands[2])) {
			base_object = std::get<TempVar>(operands[2]);
		} else if (std::holds_alternative<StringHandle>(operands[2])) {
			base_object = std::get<StringHandle>(operands[2]);
		} else {
			FLASH_LOG(Codegen, Error, error_context, " result has unsupported value type");
			return false;
		}
		if (operands.size() >= 4 && std::holds_alternative<unsigned long long>(operands[3])) {
			base_type_index = static_cast<size_t>(std::get<unsigned long long>(operands[3]));
		}
		return true;
	}

	// Helper: build return vector for member access results — [type, size_bits, temp_var] or [type, size_bits, temp_var, type_index]
	// type_index is only included in the result when type == Type::Struct (ignored otherwise)
	static std::vector<IrOperand> makeMemberResult(Type type, int size_bits, TempVar result_var, size_t type_index = 0) {
		if (type == Type::Struct) {
			return { type, size_bits, result_var, static_cast<unsigned long long>(type_index) };
		}
		return { type, size_bits, result_var };
	}

	// Helper: set up base object from an identifier, handling 'this' in lambdas and normal identifiers
	bool setupBaseFromIdentifier(
		std::string_view object_name,
		const Token& member_token,
		std::variant<StringHandle, TempVar>& base_object,
		Type& base_type,
		size_t& base_type_index,
		bool& is_pointer_dereference) {

		if (object_name == "this") {
			// First try [*this] capture - returns copy of the object
			if (auto copy_this_temp = emitLoadCopyThis(member_token)) {
				base_object = *copy_this_temp;
				base_type = Type::Struct;
				base_type_index = current_lambda_context_.enclosing_struct_type_index;
				return true;
			}
			// Then try [this] capture - returns pointer to the object
			if (auto this_ptr_temp = emitLoadThisPointer(member_token)) {
				base_object = *this_ptr_temp;
				base_type = Type::Struct;
				base_type_index = current_lambda_context_.enclosing_struct_type_index;
				is_pointer_dereference = true;
				return true;
			}
		}
		return validateAndSetupIdentifierMemberAccess(object_name, base_object, base_type, base_type_index, is_pointer_dereference);
	}

	std::vector<IrOperand> generateMemberAccessIr(const MemberAccessNode& memberAccessNode,
	                                               ExpressionContext context = ExpressionContext::Load) {
		std::vector<IrOperand> irOperands;

		// Get the object being accessed
		ASTNode object_node = memberAccessNode.object();
		std::string_view member_name = memberAccessNode.member_name();
		bool is_arrow = memberAccessNode.is_arrow();

		// Variables to hold the base object info
		std::variant<StringHandle, TempVar> base_object;
		Type base_type = Type::Void;
		size_t base_type_index = 0;
		bool is_pointer_dereference = false;  // Track if we're accessing through pointer (ptr->member)
		bool base_setup_complete = false;

		// Normalize: unwrap ExpressionNode to get the concrete variant pointer for unified dispatch
		const ExpressionNode* expr = object_node.is<ExpressionNode>() ? &object_node.as<ExpressionNode>() : nullptr;

		// Helper lambdas to check node types across both ExpressionNode variant and top-level ASTNode
		auto get_identifier = [&]() -> const IdentifierNode* {
			if (expr && std::holds_alternative<IdentifierNode>(*expr)) return &std::get<IdentifierNode>(*expr);
			if (object_node.is<IdentifierNode>()) return &object_node.as<IdentifierNode>();
			return nullptr;
		};
		auto get_member_func_call = [&]() -> const MemberFunctionCallNode* {
			if (expr && std::holds_alternative<MemberFunctionCallNode>(*expr)) return &std::get<MemberFunctionCallNode>(*expr);
			if (object_node.is<MemberFunctionCallNode>()) return &object_node.as<MemberFunctionCallNode>();
			return nullptr;
		};

		// OPERATOR-> OVERLOAD RESOLUTION
		// If this is arrow access (obj->member), check if the object has operator->() overload
		if (const IdentifierNode* ident = is_arrow ? get_identifier() : nullptr) {
			StringHandle identifier_handle = StringTable::getOrInternStringHandle(ident->name());
			
			const TypeSpecifierNode* type_node = nullptr;
			if (const DeclarationNode* decl = lookupDeclaration(identifier_handle)) {
				type_node = &decl->type_node().as<TypeSpecifierNode>();
			}
			
			// Check if it's a struct with operator-> overload
			if (type_node && type_node->type() == Type::Struct && type_node->pointer_depth() == 0) {
				auto overload_result = findUnaryOperatorOverload(type_node->type_index(), "->");
				
				if (overload_result.has_overload) {
					// Found an overload! Call operator->() to get pointer, then access member
					FLASH_LOG_FORMAT(Codegen, Debug, "Resolving operator-> overload for type index {}", 
					         type_node->type_index());
					
					const StructMemberFunction& member_func = *overload_result.member_overload;
					const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
					
					// Get struct name for mangling
					std::string_view struct_name = StringTable::getStringView(gTypeInfo[type_node->type_index()].name());
					
					// Get the return type from the function declaration (should be a pointer)
					const TypeSpecifierNode& return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
					
					// Generate mangled name for operator->
					std::string_view operator_func_name = "operator->";
					std::vector<TypeSpecifierNode> empty_params;
					std::vector<std::string_view> empty_namespace;
					auto mangled_name = NameMangling::generateMangledName(
						operator_func_name,
						return_type,
						empty_params,
						false,
						struct_name,
						empty_namespace,
						Linkage::CPlusPlus
					);
					
					// Generate the call to operator->()
					TempVar ptr_result = var_counter.next();
					
					CallOp call_op;
					call_op.result = ptr_result;
					call_op.return_type = return_type.type();
					call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
					if (call_op.return_size_in_bits == 0) {
						call_op.return_size_in_bits = get_type_size_bits(return_type.type());
					}
					call_op.function_name = mangled_name;
					call_op.is_variadic = false;
					call_op.is_member_function = true;
					
					// Add 'this' pointer as first argument
					call_op.args.push_back(TypedValue{
						.type = type_node->type(),
						.size_in_bits = 64,  // Pointer size
						.value = IrValue(identifier_handle)
					});
					
					// Add the function call instruction
					ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), memberAccessNode.member_token()));
					
					// operator-> should return a pointer, so we treat ptr_result as pointing to the actual object
					if (return_type.pointer_depth() > 0) {
						base_object = ptr_result;
						base_type = return_type.type();
						base_type_index = return_type.type_index();
						is_pointer_dereference = true;
						base_setup_complete = true;
					}
				}
			}
		}

		// Resolve the base object — single dispatch chain regardless of ExpressionNode wrapping
		if (!base_setup_complete) {
			if (const IdentifierNode* ident = get_identifier()) {
				if (!setupBaseFromIdentifier(ident->name(), memberAccessNode.member_token(),
				                             base_object, base_type, base_type_index, is_pointer_dereference)) {
					return {};
				}
			}
			else if (const MemberFunctionCallNode* call = get_member_func_call()) {
				auto call_result = generateMemberFunctionCallIr(*call);
				if (!extractBaseFromOperands(call_result, base_object, base_type, base_type_index, "member function call")) {
					return {};
				}
				if (is_arrow) {
					is_pointer_dereference = true;
				}
			}
			else if (expr && std::holds_alternative<MemberAccessNode>(*expr)) {
				auto nested_result = generateMemberAccessIr(std::get<MemberAccessNode>(*expr), context);
				if (!extractBaseFromOperands(nested_result, base_object, base_type, base_type_index, "nested member access")) {
					return {};
				}
				if (base_type != Type::Struct) {
					FLASH_LOG(Codegen, Error, "nested member access on non-struct type");
					return {};
				}
				if (is_arrow) {
					is_pointer_dereference = true;
				}
			}
			else if (expr && std::holds_alternative<UnaryOperatorNode>(*expr)) {
				const UnaryOperatorNode& unary_op = std::get<UnaryOperatorNode>(*expr);

				if (unary_op.op() != "*") {
					FLASH_LOG(Codegen, Error, "member access on non-dereference unary operator");
					return {};
				}

				const ASTNode& operand_node = unary_op.get_operand();
				if (!operand_node.is<ExpressionNode>()) {
					FLASH_LOG(Codegen, Error, "dereference operand is not an expression");
					return {};
				}
				const ExpressionNode& operand_expr = operand_node.as<ExpressionNode>();
				
				// Special handling for 'this' in lambdas with [this] or [*this] capture
				bool is_lambda_this = false;
				if (std::holds_alternative<IdentifierNode>(operand_expr)) {
					const IdentifierNode& ptr_ident = std::get<IdentifierNode>(operand_expr);
					std::string_view ptr_name = ptr_ident.name();
					
					if (ptr_name == "this" && current_lambda_context_.isActive() && 
					    current_lambda_context_.captures.find(StringTable::getOrInternStringHandle("this"sv)) != current_lambda_context_.captures.end()) {
						is_lambda_this = true;
						auto capture_kind_it = current_lambda_context_.capture_kinds.find(StringTable::getOrInternStringHandle("this"sv));
						if (capture_kind_it != current_lambda_context_.capture_kinds.end() && 
						    capture_kind_it->second == LambdaCaptureNode::CaptureKind::CopyThis) {
							// [*this] capture: load from the copied object in __copy_this
							const StructTypeInfo* closure_struct = getCurrentClosureStruct();
							const StructMember* copy_this_member = closure_struct ? closure_struct->findMember("__copy_this") : nullptr;
							int copy_this_offset = copy_this_member ? static_cast<int>(copy_this_member->offset) : 0;
							int copy_this_size_bits = copy_this_member ? static_cast<int>(copy_this_member->size * 8) : 64;
							
							TempVar copy_this_ref = var_counter.next();
							MemberLoadOp load_copy_this;
							load_copy_this.result.value = copy_this_ref;
							load_copy_this.result.type = Type::Struct;
							load_copy_this.result.size_in_bits = copy_this_size_bits;
							load_copy_this.object = StringTable::getOrInternStringHandle("this"sv);
							load_copy_this.member_name = StringTable::getOrInternStringHandle("__copy_this");
							load_copy_this.offset = copy_this_offset;
							load_copy_this.is_reference = false;
							load_copy_this.is_rvalue_reference = false;
							load_copy_this.struct_type_info = nullptr;
							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_copy_this), memberAccessNode.member_token()));
							
							LValueInfo lvalue_info(
								LValueInfo::Kind::Member,
								StringTable::getOrInternStringHandle("this"sv),
								copy_this_offset
							);
							lvalue_info.member_name = StringTable::getOrInternStringHandle("__copy_this");
							lvalue_info.is_pointer_to_member = true;
							setTempVarMetadata(copy_this_ref, TempVarMetadata::makeLValue(lvalue_info));
							
							base_object = copy_this_ref;
							base_type = Type::Struct;
							base_type_index = current_lambda_context_.enclosing_struct_type_index;
						} else {
							// [this] capture: load the pointer from __this
							int this_member_offset = getClosureMemberOffset("__this");
							
							TempVar this_ptr = var_counter.next();
							MemberLoadOp load_this;
							load_this.result.value = this_ptr;
							load_this.result.type = Type::Void;
							load_this.result.size_in_bits = 64;
							load_this.object = StringTable::getOrInternStringHandle("this"sv);
							load_this.member_name = StringTable::getOrInternStringHandle("__this");
							load_this.offset = this_member_offset;
							load_this.is_reference = false;
							load_this.is_rvalue_reference = false;
							load_this.struct_type_info = nullptr;
							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_this), memberAccessNode.member_token()));
							
							base_object = this_ptr;
							base_type = Type::Struct;
							base_type_index = current_lambda_context_.enclosing_struct_type_index;
						}
					}
				}
				
				if (!is_lambda_this) {
					auto pointer_operands = visitExpressionNode(operand_expr);
					if (!extractBaseFromOperands(pointer_operands, base_object, base_type, base_type_index, "pointer expression")) {
						return {};
					}
					is_pointer_dereference = true;
				}
			}
			else if (expr && std::holds_alternative<ArraySubscriptNode>(*expr)) {
				auto array_operands = generateArraySubscriptIr(std::get<ArraySubscriptNode>(*expr));
				if (!extractBaseFromOperands(array_operands, base_object, base_type, base_type_index, "array subscript")) {
					return {};
				}
			}
			else if (expr && std::holds_alternative<FunctionCallNode>(*expr)) {
				auto call_result = generateFunctionCallIr(std::get<FunctionCallNode>(*expr));
				if (!extractBaseFromOperands(call_result, base_object, base_type, base_type_index, "function call")) {
					return {};
				}
				if (is_arrow) {
					is_pointer_dereference = true;
				}
			}
			else {
				FLASH_LOG(Codegen, Error, "member access on unsupported object type");
				return {};
			}
		}

		// Now we have the base object (either a name or a temp var) and its type
		// Get the struct type info
		const TypeInfo* type_info = nullptr;

		// Try to find by direct index lookup
		if (base_type_index < gTypeInfo.size()) {
			const TypeInfo& ti = gTypeInfo[base_type_index];
			if (ti.type_ == Type::Struct && ti.getStructInfo()) {
				type_info = &ti;
			}
		}

		// If not found by index, search through all type info entries
		// This handles cases where type_index might not be set correctly
		if (!type_info) {
			for (const auto& ti : gTypeInfo) {
				if (ti.type_index_ == base_type_index && ti.type_ == Type::Struct && ti.getStructInfo()) {
					type_info = &ti;
					break;
				}
			}
		}

		if (!type_info || !type_info->getStructInfo()) {
			std::cerr << "Error: Struct type info not found for type_index=" << base_type_index << "\n";
			if (std::holds_alternative<StringHandle>(base_object)) {
				std::cerr << "  Object name: " << std::get<StringHandle>(base_object) << "\n";
			}
			std::cerr << "  Available struct types in gTypeInfo:\n";
			for (const auto& ti : gTypeInfo) {
				if (ti.type_ == Type::Struct && ti.getStructInfo()) {
					std::cerr << "    - " << ti.name() << " (type_index=" << ti.type_index_ << ")\n";
				}
			}
			std::cerr << "  Available types in gTypesByName:\n";
			for (const auto& [name, ti] : gTypesByName) {
				if (ti->type_ == Type::Struct) {
					std::cerr << "    - " << name << " (type_index=" << ti->type_index_ << ")\n";
				}
			}
			std::cerr << "error: struct type info not found\n";
			return {};
		}

		const StructTypeInfo* struct_info = type_info->getStructInfo();
		
		// FIRST check if this is a static member (can be accessed via instance in C++)
		auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(StringTable::getOrInternStringHandle(member_name));
		if (static_member) {
			// This is a static member! Access it via GlobalLoad instead of MemberLoad
			// Static members are accessed using qualified names (OwnerClassName::memberName)
			// Use the owner_struct name, not the current struct, to get the correct qualified name
			StringBuilder qualified_name_sb;
			qualified_name_sb.append(StringTable::getStringView(owner_struct->getName()));
			qualified_name_sb.append("::"sv);
			qualified_name_sb.append(member_name);
			std::string_view qualified_name = qualified_name_sb.commit();
			
			FLASH_LOG(Codegen, Debug, "Static member access: ", member_name, " in struct ", type_info->name(), " owned by ", owner_struct->getName(), " -> qualified_name: ", qualified_name);
			
			// Create a temporary variable for the result
			TempVar result_var = var_counter.next();
			
			int sm_size_bits = static_cast<int>(static_member->size * 8);
			// If size is 0 for struct types, look up from type info
			if (sm_size_bits == 0 && static_member->type_index > 0 && static_member->type_index < gTypeInfo.size()) {
				const StructTypeInfo* sm_si = gTypeInfo[static_member->type_index].getStructInfo();
				if (sm_si) {
					sm_size_bits = static_cast<int>(sm_si->total_size * 8);
				}
			}
			
			// Build GlobalLoadOp for the static member
			GlobalLoadOp global_load;
			global_load.result.value = result_var;
			global_load.result.type = static_member->type;
			global_load.result.size_in_bits = sm_size_bits;
			global_load.global_name = StringTable::getOrInternStringHandle(qualified_name);
			
			ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(global_load), Token()));
			
			return makeMemberResult(static_member->type, sm_size_bits, result_var, static_member->type_index);
		}
		
		// Use recursive lookup to find instance members in base classes as well
		auto member_result = FlashCpp::gLazyMemberResolver.resolve(base_type_index, StringTable::getOrInternStringHandle(member_name));

		if (!member_result) {
			std::cerr << "error: member '" << member_name << "' not found in struct '" << type_info->name() << "'\n";
			std::cerr << "  available members:\n";
			for (const auto& m : struct_info->members) {
				std::cerr << "    - " << StringTable::getStringView(m.getName()) << "\n";
			}
			throw std::runtime_error("Member not found in struct");
		}
		
		const StructMember* member = member_result.member;

		// Check access control
		const StructTypeInfo* current_context = getCurrentStructContext();
		std::string_view current_function = getCurrentFunctionName();
		if (!checkMemberAccess(member, struct_info, current_context, nullptr, current_function)) {
			std::cerr << "Error: Cannot access ";
			if (member->access == AccessSpecifier::Private) {
				std::cerr << "private";
			} else if (member->access == AccessSpecifier::Protected) {
				std::cerr << "protected";
			}
			std::cerr << " member '" << member_name << "' of '" << StringTable::getStringView(struct_info->getName()) << "'";
			if (current_context) {
				std::cerr << " from '" << StringTable::getStringView(current_context->getName()) << "'";
			}
			std::cerr << "\n";
			throw std::runtime_error("Access control violation");
		}

		// Check if base_object is a TempVar with lvalue metadata
		// If so, we can unwrap it to get the ultimate base and combine offsets
		// This optimization is ONLY applied in LValueAddress context (for stores)
		// In Load context, we keep the chain of member_access instructions
		int accumulated_offset = static_cast<int>(member_result.adjusted_offset);
		std::variant<StringHandle, TempVar> ultimate_base = base_object;
		StringHandle ultimate_member_name = StringTable::getOrInternStringHandle(member_name);
		bool did_unwrap = false;
		
		if (context == ExpressionContext::LValueAddress && std::holds_alternative<TempVar>(base_object)) {
			TempVar base_temp = std::get<TempVar>(base_object);
			auto base_lvalue_info = getTempVarLValueInfo(base_temp);
			
			if (base_lvalue_info.has_value() && base_lvalue_info->kind == LValueInfo::Kind::Member) {
				// The base is itself a member access
				// Combine the offsets and use the ultimate base (LValueAddress context only)
				accumulated_offset += base_lvalue_info->offset;
				ultimate_base = base_lvalue_info->base;
				is_pointer_dereference = base_lvalue_info->is_pointer_to_member;
				// When unwrapping nested member access, use the first-level member name
				// For example: obj.inner.value -> use "inner" (member of obj), not "value"
				if (base_lvalue_info->member_name.has_value()) {
					ultimate_member_name = base_lvalue_info->member_name.value();
				}
				did_unwrap = true;
			}
		}

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();
		
		// Mark member access as lvalue (Option 2: Value Category Tracking)
		// obj.member is an lvalue - it designates a specific object member
		// Use adjusted_offset from member_result to handle inheritance correctly
		LValueInfo lvalue_info(
			LValueInfo::Kind::Member,
			did_unwrap ? ultimate_base : base_object,
			did_unwrap ? accumulated_offset : static_cast<int>(member_result.adjusted_offset)
		);
		// Store member name for unified assignment handler
		lvalue_info.member_name = ultimate_member_name;
		lvalue_info.is_pointer_to_member = is_pointer_dereference;  // Mark if accessing through pointer
		lvalue_info.bitfield_width = member->bitfield_width;
		lvalue_info.bitfield_bit_offset = member->bitfield_bit_offset;
		setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));

		// Build MemberLoadOp
		MemberLoadOp member_load;
		member_load.result.value = result_var;
		member_load.result.type = member->type;
		member_load.result.size_in_bits = static_cast<int>(member->size * 8);  // Convert bytes to bits

		// Set base object, member name, and offset — using unwrapped values when applicable
		auto& effective_base = did_unwrap ? ultimate_base : base_object;
		std::visit([&](auto& base_value) { member_load.object = base_value; }, effective_base);
		member_load.member_name = did_unwrap ? ultimate_member_name : StringTable::getOrInternStringHandle(member_name);
		member_load.offset = did_unwrap ? accumulated_offset : static_cast<int>(member_result.adjusted_offset);
	
		// Add reference metadata (required for proper handling of reference members)
		member_load.is_reference = member->is_reference();
		member_load.is_rvalue_reference = member->is_rvalue_reference();
		member_load.struct_type_info = nullptr;
		member_load.is_pointer_to_member = is_pointer_dereference;  // Mark if accessing through pointer
		member_load.bitfield_width = member->bitfield_width;
		member_load.bitfield_bit_offset = member->bitfield_bit_offset;

		int member_size_bits = static_cast<int>(member->size * 8);

		// When context is LValueAddress, skip the load and return address/metadata only
		// EXCEPTION: For reference members, we must emit MemberAccess to load the stored address
		// because references store a pointer value that needs to be returned
		if (context == ExpressionContext::LValueAddress && !member->is_reference()) {
			return makeMemberResult(member->type, member_size_bits, result_var, member->type_index);
		}

		// Add the member access instruction (Load context - default)
		ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

		// For reference members in LValueAddress context, the result_var now holds the
		// pointer value loaded from the member slot. Update the LValueInfo to be Kind::Indirect
		// so that assignment goes THROUGH the pointer (dereference store), not to the member slot.
		if (context == ExpressionContext::LValueAddress && member->is_reference()) {
			LValueInfo ref_lvalue_info(
				LValueInfo::Kind::Indirect,
				result_var,  // The TempVar holding the loaded pointer
				0            // No offset - the pointer points directly to the target
			);
			setTempVarMetadata(result_var, TempVarMetadata::makeLValue(ref_lvalue_info));
		}

		return makeMemberResult(member->type, member_size_bits, result_var, member->type_index);
	}

	// Helper function to calculate array size from a DeclarationNode
	// Returns the total size in bytes, or 0 if the array size cannot be determined
	std::optional<size_t> calculateArraySize(const DeclarationNode& decl) {
		if (!decl.is_array()) {
			return std::nullopt;
		}

		const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
		size_t element_size = type_spec.size_in_bits() / 8;
		
		// For struct types, get size from gTypeInfo instead of size_in_bits()
		if (element_size == 0 && type_spec.type() == Type::Struct) {
			size_t type_index = type_spec.type_index();
			if (type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info) {
					element_size = struct_info->total_size;
				}
			}
		}
		
		if (element_size == 0) {
			return std::nullopt;
		}
		
		// Get array size - support multidimensional arrays
		const auto& dims = decl.array_dimensions();
		if (dims.empty()) {
			return std::nullopt;
		}

		// Evaluate all dimension size expressions and compute total element count
		size_t array_count = 1;
		ConstExpr::EvaluationContext ctx(symbol_table);
		
		for (const auto& dim_expr : dims) {
			auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
			if (!eval_result.success()) {
				return std::nullopt;
			}
			
			long long dim_size = eval_result.as_int();
			if (dim_size <= 0) {
				return std::nullopt;
			}
			
			// Check for potential overflow in multiplication
			size_t dim_size_u = static_cast<size_t>(dim_size);
			if (array_count > SIZE_MAX / dim_size_u) {
				FLASH_LOG(Codegen, Warning, "Array dimension count calculation would overflow");
				return std::nullopt;
			}
			array_count *= dim_size_u;
		}
		
		// Check for potential overflow in multiplication with element size
		if (array_count > SIZE_MAX / element_size) {
			FLASH_LOG(Codegen, Warning, "Array size calculation would overflow: ", array_count, " * ", element_size);
			return std::nullopt;
		}
		
		return element_size * array_count;
	}

	std::vector<IrOperand> generateSizeofIr(const SizeofExprNode& sizeofNode) {
		size_t size_in_bytes = 0;

		// Helper: look up sizeof a struct member (static or non-static) by qualified name.
		// Returns the member size in bytes, or 0 if not found.
		auto lookupStructMemberSize = [](std::string_view struct_name, std::string_view member_name) -> size_t {
			StringHandle struct_name_handle = StringTable::getOrInternStringHandle(struct_name);
			auto struct_type_it = gTypesByName.find(struct_name_handle);
			if (struct_type_it != gTypesByName.end()) {
				const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
				if (struct_info) {
					// Search static members
					StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
					auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_name_handle);
					if (static_member) {
						// sizeof on a reference yields the size of the referenced type
						if (static_member->is_reference()) {
							size_t ref_size = get_type_size_bits(static_member->type) / 8;
							if (ref_size == 0 && static_member->type == Type::Struct && static_member->type_index > 0 && static_member->type_index < gTypeInfo.size()) {
								const StructTypeInfo* si = gTypeInfo[static_member->type_index].getStructInfo();
								if (si) ref_size = si->total_size;
							}
							FLASH_LOG(Codegen, Debug, "sizeof(struct_member): found static ref member, referenced type size=", ref_size);
							return ref_size;
						}
						FLASH_LOG(Codegen, Debug, "sizeof(struct_member): found static member, size=", static_member->size);
						return static_member->size;
					}
					// Search non-static members
					for (const auto& member : struct_info->members) {
						if (StringTable::getStringView(member.getName()) == member_name) {
							// sizeof on a reference yields the size of the referenced type
							if (member.is_reference()) {
								size_t ref_size = member.referenced_size_bits / 8;
								FLASH_LOG(Codegen, Debug, "sizeof(struct_member): found ref member, referenced type size=", ref_size);
								return ref_size;
							}
							FLASH_LOG(Codegen, Debug, "sizeof(struct_member): found member, size=", member.size);
							return member.size;
						}
					}
				}
			}
			return 0;
		};

		if (sizeofNode.is_type()) {
			// sizeof(type)
			const ASTNode& type_node = sizeofNode.type_or_expr();
			if (!type_node.is<TypeSpecifierNode>()) {
				throw std::runtime_error("sizeof type argument must be TypeSpecifierNode");
				return {};
			}

			const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
			Type type = type_spec.type();

			// Workaround for parser limitation: when sizeof(arr) is parsed where arr is an
			// array variable, the parser may incorrectly parse it as a type.
			// Also handles sizeof(Foo::val) where the parser treats Foo::val as a qualified type name.
			// If size_in_bits is 0, try looking up the identifier in the symbol table.
			if (type_spec.size_in_bits() == 0 && type_spec.token().type() == Token::Type::Identifier) {
				StringHandle identifier = StringTable::getOrInternStringHandle(type_spec.token().value());
				
				// Check if this is a qualified name (e.g., Foo::val) parsed as a type placeholder.
				// The type name in gTypeInfo will contain "::" for qualified names.
				if (type_spec.type_index() < gTypeInfo.size()) {
					std::string_view type_name = StringTable::getStringView(gTypeInfo[type_spec.type_index()].name());
				auto sep_pos = type_name.rfind("::");
					if (sep_pos != std::string_view::npos) {
						std::string_view struct_name = type_name.substr(0, sep_pos);
						std::string_view member_name = type_name.substr(sep_pos + 2);
						FLASH_LOG(Codegen, Debug, "sizeof(qualified_type): struct=", struct_name, " member=", member_name);
						size_t member_size = lookupStructMemberSize(struct_name, member_name);
						if (member_size > 0) {
							return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(member_size) };
						}
					}
				}
				
				// Look up the identifier in the symbol table
				const DeclarationNode* decl = lookupDeclaration(identifier);
				if (decl) {
					auto array_size = calculateArraySize(*decl);
					if (array_size.has_value()) {
						// Return sizeof result for array
						return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(*array_size) };
					}
				}
				
				// Handle template parameters in member functions with trailing requires clauses
				// When sizeof(T) is used in a template class member function, T is a template parameter
				// that should be resolved from the instantiated class's template arguments
				if (!decl && !lookupSymbol(identifier).has_value() && current_struct_name_.isValid()) {
					// We're in a member function - try to resolve the template parameter
					std::string_view struct_name = StringTable::getStringView(current_struct_name_);
					size_t param_size_bytes = resolveTemplateSizeFromStructName(struct_name);
					
					if (param_size_bytes > 0) {
						return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(param_size_bytes) };
					}
				}
			}

			// Handle array types: sizeof(int[10]) 
			if (type_spec.is_array()) {
				size_t element_size = type_spec.size_in_bits() / 8;
				size_t array_count = 0;
				
				if (type_spec.array_size().has_value()) {
					array_count = *type_spec.array_size();
				}
				
				if (array_count > 0) {
					size_in_bytes = element_size * array_count;
				} else {
					size_in_bytes = element_size; // Fallback: just element size
				}
			}
			// Handle struct types
			else if (type == Type::Struct) {
				size_t type_index = type_spec.type_index();
				if (type_index >= gTypeInfo.size()) {
					throw std::runtime_error("Invalid type index for struct");
					return {};
				}

				const TypeInfo& type_info = gTypeInfo[type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (!struct_info) {
					throw std::runtime_error("Struct type info not found");
					return {};
				}

				size_in_bytes = struct_info->total_size;
			}
			else {
				// For primitive types, convert bits to bytes
				size_in_bytes = type_spec.size_in_bits() / 8;
			}
		}
		else {
			// sizeof(expression) - evaluate the type of the expression
			const ASTNode& expr_node = sizeofNode.type_or_expr();
			if (!expr_node.is<ExpressionNode>()) {
				throw std::runtime_error("sizeof expression argument must be ExpressionNode");
				return {};
			}

			// Special handling for identifiers: sizeof(x) where x is a variable
			// This path handles cases where the parser correctly identifies x as an expression
			const ExpressionNode& expr = expr_node.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr)) {
				const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
				
				// Look up the identifier in the symbol table
				const DeclarationNode* decl = lookupDeclaration(id_node.name());
				if (decl) {
					// Check if it's an array
					auto array_size = calculateArraySize(*decl);
					if (array_size.has_value()) {
						// Return sizeof result for array
						return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(*array_size) };
					}
					
					// For regular variables, get the type size from the declaration
					const TypeSpecifierNode& var_type = decl->type_node().as<TypeSpecifierNode>();
					if (var_type.type() == Type::Struct) {
						size_t type_index = var_type.type_index();
						if (type_index < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_index];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info && struct_info->total_size > 0) {
								return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(struct_info->total_size) };
							}
							// Fallback: use type_size_ from TypeInfo (works for template instantiations at global scope)
							if (type_info.type_size_ > 0) {
								return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(type_info.type_size_) };
							}
						}
						// Fallback: use size_in_bits from the type specifier node
						if (var_type.size_in_bits() > 0) {
							return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(var_type.size_in_bits() / 8) };
						}
					} else {
						// Primitive type - use get_type_size_bits to handle cases where size_in_bits wasn't set
						int size_bits = var_type.size_in_bits();
						if (size_bits == 0) {
							size_bits = get_type_size_bits(var_type.type());
						}
						size_in_bytes = size_bits / 8;
						return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(size_in_bytes) };
					}
				}
			}
			// Special handling for member access: sizeof(s.member) where member is an array
			else if (std::holds_alternative<MemberAccessNode>(expr)) {
				const MemberAccessNode& member_access = std::get<MemberAccessNode>(expr);
				std::string_view member_name = member_access.member_name();
				FLASH_LOG(Codegen, Debug, "sizeof(member_access): member_name=", member_name);
				
				// Get the object's type to find the struct info
				const ASTNode& object_node = member_access.object();
				if (object_node.is<ExpressionNode>()) {
					const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(obj_expr)) {
						const IdentifierNode& id_node = std::get<IdentifierNode>(obj_expr);
						FLASH_LOG(Codegen, Debug, "sizeof(member_access): object_name=", id_node.name());
						
						// Look up the identifier to get its type
						const DeclarationNode* decl = lookupDeclaration(id_node.name());
						if (decl) {
							const TypeSpecifierNode& obj_type = decl->type_node().as<TypeSpecifierNode>();
							FLASH_LOG(Codegen, Debug, "sizeof(member_access): obj_type=", (int)obj_type.type(), " type_index=", obj_type.type_index());
							if (obj_type.type() == Type::Struct) {
								size_t type_index = obj_type.type_index();
								if (type_index < gTypeInfo.size()) {
									const TypeInfo& type_info = gTypeInfo[type_index];
									std::string_view base_type_name = StringTable::getStringView(type_info.name());
									FLASH_LOG(Codegen, Debug, "sizeof(member_access): type_info name=", base_type_name);
									const StructTypeInfo* struct_info = type_info.getStructInfo();
									
									// First try the direct struct_info
									size_t direct_member_size = 0;
									if (struct_info && !struct_info->members.empty()) {
										FLASH_LOG(Codegen, Debug, "sizeof(member_access): struct found, members=", struct_info->members.size());
										// Find the member in the struct
										for (const auto& member : struct_info->members) {
											FLASH_LOG(Codegen, Debug, "  checking member: ", StringTable::getStringView(member.getName()), " size=", member.size);
											if (StringTable::getStringView(member.getName()) == member_name) {
												direct_member_size = member.size;
												break;
											}
										}
									}
									
									// If direct lookup found a member with size > 1, use it
									// Otherwise, search for instantiated types (template vs instantiation mismatch)
									if (direct_member_size > 1) {
										FLASH_LOG(Codegen, Debug, "sizeof(member_access): FOUND member size=", direct_member_size);
										return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(direct_member_size) };
									}
									
									// Fallback: If direct lookup failed or found size <= 1 (could be unsubstituted template),
									// search for instantiated types that match this base template name
									// This handles cases like test<int> where type_index points to 'test' 
									// but we need 'test$hash' for the correct member size
									for (const auto& ti : gTypeInfo) {
										std::string_view ti_name = StringTable::getStringView(ti.name());
										// Check if this is an instantiation of the base template
										// Instantiated names start with base_name followed by '_' or '$'
										if (ti_name.size() > base_type_name.size() && 
										    ti_name.substr(0, base_type_name.size()) == base_type_name &&
										    (ti_name[base_type_name.size()] == '_' || ti_name[base_type_name.size()] == '$')) {
											const StructTypeInfo* inst_struct_info = ti.getStructInfo();
											if (inst_struct_info && !inst_struct_info->members.empty()) {
												for (const auto& member : inst_struct_info->members) {
													if (StringTable::getStringView(member.getName()) == member_name) {
														FLASH_LOG(Codegen, Debug, "sizeof(member_access): Found in instantiated type '", ti_name, "' member size=", member.size);
														return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(member.size) };
													}
												}
											}
										}
									}
									
									// If no instantiation found but direct lookup had a result, use that
									if (direct_member_size > 0) {
										FLASH_LOG(Codegen, Debug, "sizeof(member_access): Using direct lookup member size=", direct_member_size);
										return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(direct_member_size) };
									}
								}
							}
						}
					}
				}
			}
			// Special handling for array subscript: sizeof(arr[0]) 
			// This should not generate runtime code - just get the element type
			else if (std::holds_alternative<ArraySubscriptNode>(expr)) {
				const ArraySubscriptNode& array_subscript = std::get<ArraySubscriptNode>(expr);
				const ASTNode& array_expr_node = array_subscript.array_expr();
				
				// Check if the array expression is an identifier
				if (array_expr_node.is<ExpressionNode>()) {
					const ExpressionNode& array_expr = array_expr_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(array_expr)) {
						const IdentifierNode& id_node = std::get<IdentifierNode>(array_expr);
						
						// Look up the array identifier in the symbol table
						const DeclarationNode* decl = lookupDeclaration(id_node.name());
						if (decl) {
							const TypeSpecifierNode& var_type = decl->type_node().as<TypeSpecifierNode>();
							
							// Get the base element type size
							size_t element_size = var_type.size_in_bits() / 8;
							if (element_size == 0) {
								element_size = get_type_size_bits(var_type.type()) / 8;
							}
							
							// Handle struct element types
							if (element_size == 0 && var_type.type() == Type::Struct) {
								size_t type_index = var_type.type_index();
								if (type_index < gTypeInfo.size()) {
									const TypeInfo& type_info = gTypeInfo[type_index];
									const StructTypeInfo* struct_info = type_info.getStructInfo();
									if (struct_info) {
										element_size = struct_info->total_size;
									}
								}
							}
							
							// For multidimensional arrays, arr[0] should return size of the sub-array
							// e.g., for int arr[3][4], sizeof(arr[0]) = sizeof(int[4]) = 16
							const auto& dims = decl->array_dimensions();
							if (dims.size() > 1) {
								// Calculate sub-array size: element_size * product of all dims except first
								size_t sub_array_count = 1;
								ConstExpr::EvaluationContext ctx(symbol_table);
								
								for (size_t i = 1; i < dims.size(); ++i) {
									auto eval_result = ConstExpr::Evaluator::evaluate(dims[i], ctx);
									if (!eval_result.success()) {
										// Can't evaluate dimension at compile time, fall through to IR generation
										FLASH_LOG(Codegen, Debug, "sizeof(arr[index]): Could not evaluate dimension ", i, 
										          " for '", id_node.name(), "', falling back to IR generation");
										goto fallback_to_ir;
									}
									
									long long dim_size = eval_result.as_int();
									if (dim_size <= 0) {
										FLASH_LOG(Codegen, Debug, "sizeof(arr[index]): Invalid dimension size ", dim_size, 
										          " for '", id_node.name(), "'");
										goto fallback_to_ir;
									}
									
									sub_array_count *= static_cast<size_t>(dim_size);
								}
								
								size_in_bytes = element_size * sub_array_count;
								FLASH_LOG(Codegen, Debug, "sizeof(arr[index]): multidim array=", id_node.name(), 
								          " element_size=", element_size, " sub_array_count=", sub_array_count,
								          " total=", size_in_bytes);
							} else {
								// Single dimension or non-array, just return element size
								size_in_bytes = element_size;
								FLASH_LOG(Codegen, Debug, "sizeof(arr[index]): array=", id_node.name(), 
								          " element_size=", size_in_bytes);
							}
							
							// Return the size without generating runtime IR
							return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(size_in_bytes) };
						}
						
						fallback_to_ir:
						
						// If we couldn't resolve compile-time, log and fall through
						FLASH_LOG(Codegen, Debug, "sizeof(arr[index]): Could not resolve '", id_node.name(), 
						          "' at compile-time, falling back to IR generation");
					}
				}
			}
			// Special handling for qualified identifiers: sizeof(Foo::val) where val is a static member
			else if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
				const QualifiedIdentifierNode& qual_id = std::get<QualifiedIdentifierNode>(expr);
				std::string_view struct_name = gNamespaceRegistry.getQualifiedName(qual_id.namespace_handle());
				std::string_view member_name = qual_id.name();
				FLASH_LOG(Codegen, Debug, "sizeof(qualified_id): struct=", struct_name, " member=", member_name);
				
				size_t member_size = lookupStructMemberSize(struct_name, member_name);
				if (member_size > 0) {
					return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(member_size) };
				}
			}

			// Fall back to default expression handling
			// Generate IR for the expression to get its type
			auto expr_operands = visitExpressionNode(expr_node.as<ExpressionNode>());
			if (expr_operands.empty()) {
				return {};
			}

			// Extract type and size from the expression result
			Type expr_type = std::get<Type>(expr_operands[0]);
			int size_in_bits = std::get<int>(expr_operands[1]);

			// Handle struct types
			if (expr_type == Type::Struct) {
				// For struct expressions, we need to look up the type index
				// This is a simplification - in a full implementation we'd track type_index through expressions
				throw std::runtime_error("sizeof(struct_expression) not fully implemented yet");
				return {};
			}
			else {
				size_in_bytes = size_in_bits / 8;
			}
		}

		// Safety check: if size_in_bytes is still 0, something went wrong
		// This shouldn't happen, but add a fallback just in case
		if (size_in_bytes == 0) {
			FLASH_LOG(Codegen, Warning, "sizeof returned 0, this indicates a bug in type size tracking");
		}

		// Return sizeof result as a constant unsigned long long (size_t equivalent)
		// Format: [type, size_bits, value]
		return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(size_in_bytes) };
	}

	std::vector<IrOperand> generateAlignofIr(const AlignofExprNode& alignofNode) {
		size_t alignment = 0;

		if (alignofNode.is_type()) {
			// alignof(type)
			const ASTNode& type_node = alignofNode.type_or_expr();
			if (!type_node.is<TypeSpecifierNode>()) {
				throw std::runtime_error("alignof type argument must be TypeSpecifierNode");
				return {};
			}

			const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
			Type type = type_spec.type();

			// Handle struct types
			if (type == Type::Struct) {
				size_t type_index = type_spec.type_index();
				if (type_index >= gTypeInfo.size()) {
					throw std::runtime_error("Invalid type index for struct");
					return {};
				}

				const TypeInfo& type_info = gTypeInfo[type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (!struct_info) {
					throw std::runtime_error("Struct type info not found");
					return {};
				}

				alignment = struct_info->alignment;
			}
			else {
				// For primitive types, use standard alignment calculation
				size_t size_in_bytes = type_spec.size_in_bits() / 8;
				alignment = calculate_alignment_from_size(size_in_bytes, type);
			}
		}
		else {
			// alignof(expression) - determine the alignment of the expression's type
			const ASTNode& expr_node = alignofNode.type_or_expr();
			if (!expr_node.is<ExpressionNode>()) {
				throw std::runtime_error("alignof expression argument must be ExpressionNode");
				return {};
			}

			// Special handling for identifiers: alignof(x) where x is a variable
			const ExpressionNode& expr = expr_node.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr)) {
				const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
				
				// Look up the identifier in the symbol table
				std::optional<ASTNode> symbol = lookupSymbol(id_node.name());
				
				if (symbol.has_value()) {
					const DeclarationNode* decl = get_decl_from_symbol(*symbol);
					if (decl) {
						// Get the type alignment from the declaration
						const TypeSpecifierNode& var_type = decl->type_node().as<TypeSpecifierNode>();
						if (var_type.type() == Type::Struct) {
							size_t type_index = var_type.type_index();
							if (type_index < gTypeInfo.size()) {
								const TypeInfo& type_info = gTypeInfo[type_index];
								const StructTypeInfo* struct_info = type_info.getStructInfo();
								if (struct_info) {
									return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(struct_info->alignment) };
								}
							}
						} else {
							// Primitive type - use get_type_size_bits to handle cases where size_in_bits wasn't set
							int size_bits = var_type.size_in_bits();
							if (size_bits == 0) {
								size_bits = get_type_size_bits(var_type.type());
							}
							size_t size_in_bytes = size_bits / 8;
							alignment = calculate_alignment_from_size(size_in_bytes, var_type.type());
							return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(alignment) };
						}
					}
				}
			}

			// Fall back to default expression handling
			// Generate IR for the expression to get its type
			auto expr_operands = visitExpressionNode(expr_node.as<ExpressionNode>());
			if (expr_operands.empty()) {
				return {};
			}

			// Extract type and size from the expression result
			Type expr_type = std::get<Type>(expr_operands[0]);
			int size_in_bits = std::get<int>(expr_operands[1]);

			// Handle struct types
			if (expr_type == Type::Struct) {
				// For struct expressions, we need to look up the type index
				// This is a simplification - in a full implementation we'd track type_index through expressions
				throw std::runtime_error("alignof(struct_expression) not fully implemented yet");
				return {};
			}
			else {
				// For primitive types
				size_t size_in_bytes = size_in_bits / 8;
				alignment = calculate_alignment_from_size(size_in_bytes, expr_type);
			}
		}

		// Safety check: alignment should never be 0 for valid types
		assert(alignment != 0 && "alignof returned 0, this indicates a bug in type alignment tracking");

		// Return alignof result as a constant unsigned long long (size_t equivalent)
		// Format: [type, size_bits, value]
		return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(alignment) };
	}

	std::vector<IrOperand> generateOffsetofIr(const OffsetofExprNode& offsetofNode) {
		// offsetof(struct_type, member)
		const ASTNode& type_node = offsetofNode.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			throw std::runtime_error("offsetof type argument must be TypeSpecifierNode");
			return {};
		}

		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		if (type_spec.type() != Type::Struct) {
			throw std::runtime_error("offsetof requires a struct type");
			return {};
		}

		// Get the struct type info
		size_t type_index = type_spec.type_index();
		if (type_index >= gTypeInfo.size()) {
			throw std::runtime_error("Invalid type index for struct");
			return {};
		}

		// Find the member
		std::string_view member_name = offsetofNode.member_name();
		auto member_result = FlashCpp::gLazyMemberResolver.resolve(
			static_cast<TypeIndex>(type_index),
			StringTable::getOrInternStringHandle(std::string(member_name)));
		if (!member_result) {
			throw std::runtime_error("Member not found in struct");
			return {};
		}

		// Return offset as a constant unsigned long long (size_t equivalent)
		// Format: [type, size_bits, value]
		return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(member_result.adjusted_offset) };
	}

	// Helper function to check if a type is a scalar type (arithmetic, enum, pointer, member pointer, nullptr_t)
	bool isScalarType(Type type, bool is_reference, size_t pointer_depth) const {
		if (is_reference) return false;
		if (pointer_depth > 0) return true;  // Pointers are scalar
		return (type == Type::Bool || type == Type::Char || type == Type::Short ||
		        type == Type::Int || type == Type::Long || type == Type::LongLong ||
		        type == Type::UnsignedChar || type == Type::UnsignedShort ||
		        type == Type::UnsignedInt || type == Type::UnsignedLong ||
		        type == Type::UnsignedLongLong || type == Type::Float ||
		        type == Type::Double || type == Type::LongDouble || type == Type::Enum ||
		        type == Type::Nullptr || type == Type::MemberObjectPointer ||
		        type == Type::MemberFunctionPointer);
	}

	bool isArithmeticType(Type type) const {
		// Branchless: arithmetic types are Bool(1) through LongDouble(14)
		// Using range check instead of multiple comparisons
		return (static_cast<int_fast16_t>(type) >= static_cast<int_fast16_t>(Type::Bool)) &
		       (static_cast<int_fast16_t>(type) <= static_cast<int_fast16_t>(Type::LongDouble));
	}

	bool isFundamentalType(Type type) const {
		// Branchless: fundamental types are Void(0), Nullptr(28), or arithmetic types Bool(1) through LongDouble(14)
		// Using bitwise OR of conditions for branchless evaluation
		return (type == Type::Void) | (type == Type::Nullptr) | isArithmeticType(type);
	}

	std::vector<IrOperand> generateTypeTraitIr(const TypeTraitExprNode& traitNode) {
		// Type traits evaluate to a compile-time boolean constant
		bool result = false;

		// Handle no-argument traits first (like __is_constant_evaluated)
		if (traitNode.is_no_arg_trait()) {
			switch (traitNode.kind()) {
				case TypeTraitKind::IsConstantEvaluated:
					// __is_constant_evaluated() - returns true if being evaluated at compile time
					// In runtime code, this always returns false
					// In constexpr context, this would return true
					// For now, return false (runtime context)
					result = false;
					break;
				default:
					result = false;
					break;
			}
			// Return result as a bool constant
			return { Type::Bool, 8, static_cast<unsigned long long>(result ? 1 : 0) };
		}

		// For traits that require type arguments, extract the type information
		const ASTNode& type_node = traitNode.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			throw std::runtime_error("Type trait argument must be TypeSpecifierNode");
			return {};
		}

		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		Type type = type_spec.type();
		bool is_reference = type_spec.is_reference();
		bool is_rvalue_reference = type_spec.is_rvalue_reference();
		size_t pointer_depth = type_spec.pointer_depth();
		
		// Get TypeInfo and StructTypeInfo for use by shared evaluator and binary traits
		[[maybe_unused]] const TypeInfo* outer_type_info = (type_spec.type_index() < gTypeInfo.size()) ? &gTypeInfo[type_spec.type_index()] : nullptr;
		[[maybe_unused]] const StructTypeInfo* outer_struct_info = outer_type_info ? outer_type_info->getStructInfo() : nullptr;

		// Handle binary traits that require a second type argument
		switch (traitNode.kind()) {
			case TypeTraitKind::IsBaseOf:
				// __is_base_of(Base, Derived) - Check if Base is a base class of Derived
				if (traitNode.has_second_type()) {
					const ASTNode& second_type_node = traitNode.second_type_node();
					if (second_type_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& derived_spec = second_type_node.as<TypeSpecifierNode>();
						
						// Both types must be class types (not references, not pointers)
						if (type == Type::Struct && derived_spec.type() == Type::Struct &&
						    !is_reference && pointer_depth == 0 &&
						    !derived_spec.is_reference() && derived_spec.pointer_depth() == 0 &&
						    type_spec.type_index() < gTypeInfo.size() &&
						    derived_spec.type_index() < gTypeInfo.size()) {
							
							const TypeInfo& base_info = gTypeInfo[type_spec.type_index()];
							const TypeInfo& derived_info = gTypeInfo[derived_spec.type_index()];
							const StructTypeInfo* base_struct = base_info.getStructInfo();
							const StructTypeInfo* derived_struct = derived_info.getStructInfo();
							
							if (base_struct && derived_struct) {
								// Same type is considered base of itself
								if (type_spec.type_index() == derived_spec.type_index()) {
									result = true;
								} else {
									// Check if base_struct is in derived_struct's base classes
									for (const auto& base_class : derived_struct->base_classes) {
										if (base_class.type_index == type_spec.type_index()) {
											result = true;
											break;
										}
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsSame:
				// __is_same(T, U) - Check if T and U are the same type (exactly the same)
				if (traitNode.has_second_type()) {
					const ASTNode& second_type_node = traitNode.second_type_node();
					if (second_type_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& second_spec = second_type_node.as<TypeSpecifierNode>();
						
						// Check if all properties match exactly
						result = (type == second_spec.type() &&
						         is_reference == second_spec.is_reference() &&
						         is_rvalue_reference == second_spec.is_rvalue_reference() &&
						         pointer_depth == second_spec.pointer_depth() &&
						         type_spec.type_index() == second_spec.type_index() &&
						         type_spec.is_array() == second_spec.is_array() &&
						         type_spec.is_const() == second_spec.is_const() &&
						         type_spec.is_volatile() == second_spec.is_volatile());
					}
				}
				break;

			case TypeTraitKind::IsConvertible:
				// __is_convertible(From, To) - Check if From can be converted to To
				if (traitNode.has_second_type()) {
					const ASTNode& second_type_node = traitNode.second_type_node();
					if (second_type_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& to_spec = second_type_node.as<TypeSpecifierNode>();
						const TypeSpecifierNode& from_spec = type_spec;
						
						Type from_type = from_spec.type();
						Type to_type = to_spec.type();
						bool from_is_ref = from_spec.is_reference();
						bool to_is_ref = to_spec.is_reference();
						size_t from_ptr_depth = from_spec.pointer_depth();
						size_t to_ptr_depth = to_spec.pointer_depth();
						
						// Same type is always convertible
						if (from_type == to_type && from_is_ref == to_is_ref && 
						    from_ptr_depth == to_ptr_depth && 
						    from_spec.type_index() == to_spec.type_index()) {
							result = true;
						}
						// Arithmetic types are generally convertible to each other
						else if (isArithmeticType(from_type) && isArithmeticType(to_type) &&
						         !from_is_ref && !to_is_ref && 
						         from_ptr_depth == 0 && to_ptr_depth == 0) {
							result = true;
						}
						// Pointers with same depth and compatible types
						else if (from_ptr_depth > 0 && to_ptr_depth > 0 && 
						         from_ptr_depth == to_ptr_depth && !from_is_ref && !to_is_ref) {
							// Pointer convertibility (same type or derived-to-base)
							result = (from_type == to_type || from_spec.type_index() == to_spec.type_index());
						}
						// nullptr_t is convertible to any pointer type
						else if (from_type == Type::Nullptr && to_ptr_depth > 0 && !to_is_ref) {
							result = true;
						}
						// Derived to base conversion for class types
						else if (from_type == Type::Struct && to_type == Type::Struct &&
						         !from_is_ref && !to_is_ref && 
						         from_ptr_depth == 0 && to_ptr_depth == 0 &&
						         from_spec.type_index() < gTypeInfo.size() &&
						         to_spec.type_index() < gTypeInfo.size()) {
							// Check if from_type is derived from to_type
							const TypeInfo& from_info = gTypeInfo[from_spec.type_index()];
							const StructTypeInfo* from_struct = from_info.getStructInfo();
							if (from_struct) {
								for (const auto& base_class : from_struct->base_classes) {
									if (base_class.type_index == to_spec.type_index()) {
										result = true;
										break;
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsNothrowConvertible:
				// __is_nothrow_convertible(From, To) - Same as IsConvertible but for nothrow conversions
				// For now, use the same logic as IsConvertible (conservative approximation)
				if (traitNode.has_second_type()) {
					const ASTNode& second_type_node = traitNode.second_type_node();
					if (second_type_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& to_spec = second_type_node.as<TypeSpecifierNode>();
						const TypeSpecifierNode& from_spec = type_spec;
						
						Type from_type = from_spec.type();
						Type to_type = to_spec.type();
						bool from_is_ref = from_spec.is_reference();
						bool to_is_ref = to_spec.is_reference();
						size_t from_ptr_depth = from_spec.pointer_depth();
						size_t to_ptr_depth = to_spec.pointer_depth();
						
						// Same type is always nothrow convertible
						if (from_type == to_type && from_is_ref == to_is_ref && 
						    from_ptr_depth == to_ptr_depth && 
						    from_spec.type_index() == to_spec.type_index()) {
							result = true;
						}
						// Arithmetic types are nothrow convertible to each other
						else if (isArithmeticType(from_type) && isArithmeticType(to_type) &&
						         !from_is_ref && !to_is_ref && 
						         from_ptr_depth == 0 && to_ptr_depth == 0) {
							result = true;
						}
						// Pointers with same depth and compatible types
						else if (from_ptr_depth > 0 && to_ptr_depth > 0 && 
						         from_ptr_depth == to_ptr_depth && !from_is_ref && !to_is_ref) {
							result = (from_type == to_type || from_spec.type_index() == to_spec.type_index());
						}
						// nullptr_t is nothrow convertible to any pointer type
						else if (from_type == Type::Nullptr && to_ptr_depth > 0 && !to_is_ref) {
							result = true;
						}
						// Derived to base conversion for class types (nothrow if no virtual base)
						else if (from_type == Type::Struct && to_type == Type::Struct &&
						         !from_is_ref && !to_is_ref && 
						         from_ptr_depth == 0 && to_ptr_depth == 0 &&
						         from_spec.type_index() < gTypeInfo.size() &&
						         to_spec.type_index() < gTypeInfo.size()) {
							// Check if from_type is derived from to_type
							const TypeInfo& from_info = gTypeInfo[from_spec.type_index()];
							const StructTypeInfo* from_struct = from_info.getStructInfo();
							if (from_struct) {
								for (const auto& base_class : from_struct->base_classes) {
									if (base_class.type_index == to_spec.type_index()) {
										// Base class found - nothrow if not virtual
										result = !base_class.is_virtual;
										break;
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsPolymorphic:
				// A polymorphic class has at least one virtual function
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					result = struct_info && struct_info->has_vtable;
				}
				break;

			case TypeTraitKind::IsFinal:
				// A final class cannot be derived from
				// Note: This requires tracking 'final' keyword on classes
				// For now, check if any member function is marked final
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Check if any virtual function is marked final
						for (const auto& func : struct_info->member_functions) {
							if (func.is_final) {
								result = true;
								break;
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsAbstract:
				// An abstract class has at least one pure virtual function
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					result = struct_info && struct_info->is_abstract;
				}
				break;

			case TypeTraitKind::IsEmpty:
				// An empty class has no non-static data members (excluding empty base classes)
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Check if there are no non-static data members
						// and no virtual functions (vtable pointer would be a member)
						result = struct_info->members.empty() && !struct_info->has_vtable;
					}
				}
				break;

			case TypeTraitKind::IsAggregate:
				// An aggregate is:
				// - An array type, or
				// - A class type (struct/class/union) with:
				//   - No user-declared or inherited constructors
				//   - No private or protected non-static data members
				//   - No virtual functions
				//   - No virtual, private, or protected base classes
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Check aggregate conditions:
						// 1. No user-declared constructors (check member_functions for non-implicit constructors)
						// 2. No private or protected members (all members are public)
						// 3. No virtual functions (has_vtable flag)
						bool has_user_constructors = false;
						for (const auto& func : struct_info->member_functions) {
							if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
								const ConstructorDeclarationNode& ctor = func.function_decl.as<ConstructorDeclarationNode>();
								if (!ctor.is_implicit()) {
									has_user_constructors = true;
									break;
								}
							}
						}
						
						bool no_virtual = !struct_info->has_vtable;
						bool all_public = true;
						
						for (const auto& member : struct_info->members) {
							if (member.access == AccessSpecifier::Private || 
							    member.access == AccessSpecifier::Protected) {
								all_public = false;
								break;
							}
						}
						
						result = !has_user_constructors && no_virtual && all_public;
					}
				}
				// Arrays are aggregates
				else if (pointer_depth == 0 && !is_reference && type_spec.is_array()) {
					result = true;
				}
				break;

			case TypeTraitKind::IsStandardLayout:
				// A standard-layout class has specific requirements:
				// - No virtual functions or virtual base classes
				// - All non-static data members have same access control
				// - No base classes with non-static data members
				// - No base classes of the same type as first non-static data member
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Basic check: no virtual functions
						result = !struct_info->has_vtable;
						// If all members have the same access specifier, it's a simple standard layout
						if (result && struct_info->members.size() > 1) {
							AccessSpecifier first_access = struct_info->members[0].access;
							for (const auto& member : struct_info->members) {
								if (member.access != first_access) {
									result = false;
									break;
								}
							}
						}
					}
				}
				// Scalar types are standard layout
				else if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				break;

			case TypeTraitKind::HasUniqueObjectRepresentations:
				// Types with no padding bits have unique object representations
				// Integral types (except bool), and trivially copyable types without padding
				if ((type == Type::Char || type == Type::Short || type == Type::Int ||
				     type == Type::Long || type == Type::LongLong || type == Type::UnsignedChar ||
				     type == Type::UnsignedShort || type == Type::UnsignedInt ||
				     type == Type::UnsignedLong || type == Type::UnsignedLongLong)
				    && !is_reference && pointer_depth == 0) {
					result = true;
				}
				// Note: float/double may have padding or non-unique representations
				break;

			case TypeTraitKind::IsTriviallyCopyable:
				// A trivially copyable type can be copied with memcpy
				// - Scalar types (arithmetic, pointers, enums)
				// - Classes with trivial copy/move constructors and destructors, no virtual
				// TODO: Implement proper checking of copy/move constructors and assignment operators
				//       for full C++ standard compliance
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Classes: need to check for trivial special members and no virtual
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Simple heuristic: no virtual functions means likely trivially copyable
						// TODO: A more complete check would verify copy/move ctors are trivial
						result = !struct_info->has_vtable;
					}
				}
				break;

			case TypeTraitKind::IsTrivial:
				// A trivial type is trivially copyable and has a trivial default constructor
				// TODO: Full compliance requires checking that:
				//       - Has trivial default constructor
				//       - Has trivial copy constructor
				//       - Has trivial move constructor
				//       - Has trivial copy assignment operator
				//       - Has trivial move assignment operator
				//       - Has trivial destructor
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Simple heuristic: no virtual functions and no user-defined constructors
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
					}
				}
				break;

			case TypeTraitKind::IsPod:
				// POD (Plain Old Data) = trivial + standard layout (C++03 compatible)
				// In C++11+, this is deprecated but still useful
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// POD: no virtual functions, no user-defined ctors, all members same access
						bool is_pod = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
						if (is_pod && struct_info->members.size() > 1) {
							AccessSpecifier first_access = struct_info->members[0].access;
							for (const auto& member : struct_info->members) {
								if (member.access != first_access) {
									is_pod = false;
									break;
								}
							}
						}
						result = is_pod;
					}
				}
				break;

			case TypeTraitKind::IsLiteralType:
				// __is_literal_type - deprecated in C++17, removed in C++20
				FLASH_LOG(Codegen, Warning, "__is_literal_type is deprecated in C++17 and removed in C++20. "
				          "This trait is likely being invoked from a standard library header (e.g., <type_traits>) "
				          "that hasn't been fully updated for C++20. In modern C++, use std::is_constant_evaluated() "
				          "to check for compile-time contexts, or use other appropriate type traits.");
				// A literal type is one that can be used in constexpr context:
				// - Scalar types
				// - References
				// - Arrays of literal types
				// - Class types that have all of:
				//   - Trivial destructor
				//   - Aggregate type OR has at least one constexpr constructor
				//   - All non-static data members are literal types
				if (isScalarType(type, is_reference, pointer_depth) || is_reference) {
					result = true;
				}
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Simplified check: assume literal if trivially copyable
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
					}
				}
				break;

			case TypeTraitKind::IsConst:
				// __is_const - checks if type has const qualifier
				result = type_spec.is_const();
				break;

			case TypeTraitKind::IsVolatile:
				// __is_volatile - checks if type has volatile qualifier
				result = type_spec.is_volatile();
				break;

			case TypeTraitKind::IsSigned:
				// __is_signed - checks if integral type is signed
				result = ((type == Type::Char) |  // char is signed on most platforms
			          (type == Type::Short) | (type == Type::Int) |
			          (type == Type::Long) | (type == Type::LongLong))
			          & !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsUnsigned:
				// __is_unsigned - checks if integral type is unsigned
				result = ((type == Type::Bool) |  // bool is considered unsigned
			          (type == Type::UnsignedChar) | (type == Type::UnsignedShort) |
			          (type == Type::UnsignedInt) | (type == Type::UnsignedLong) |
			          (type == Type::UnsignedLongLong))
			          & !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsBoundedArray:
				// __is_bounded_array - array with known bound (e.g., int[10])
				// Check if it's an array and the size is known
				result = type_spec.is_array() & int(type_spec.array_size() > 0) &
			         !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsUnboundedArray:
				// __is_unbounded_array - array with unknown bound (e.g., int[])
				// Check if it's an array and the size is unknown (0 or negative)
				result = type_spec.is_array() & int(type_spec.array_size() <= 0) &
			         !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsConstructible:
				// __is_constructible(T, Args...) - Check if T can be constructed with Args...
				// For scalar types, default constructible (no args) or copy constructible (same type arg)
				if (isScalarType(type, is_reference, pointer_depth)) {
					const auto& arg_types = traitNode.additional_type_nodes();
					if (arg_types.empty()) {
						// Default constructible - all scalars are default constructible
						result = true;
					} else if (arg_types.size() == 1 && arg_types[0].is<TypeSpecifierNode>()) {
						// Copy/conversion construction - check if types are compatible
						const TypeSpecifierNode& arg_spec = arg_types[0].as<TypeSpecifierNode>();
						// Same type or convertible arithmetic types
						result = (arg_spec.type() == type) || 
						         (isScalarType(arg_spec.type(), arg_spec.is_reference(), arg_spec.pointer_depth()) &&
						          !arg_spec.is_reference() && arg_spec.pointer_depth() == 0);
					}
				}
				// Class types: check for appropriate constructor
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						const auto& arg_types = traitNode.additional_type_nodes();
						if (arg_types.empty()) {
							// Default constructible - has default constructor or no user-defined ctors
							result = !struct_info->hasUserDefinedConstructor() || struct_info->hasConstructor();
						} else {
							// Check for matching constructor
							// Simple heuristic: if it has any user-defined constructor, assume constructible
							result = struct_info->hasUserDefinedConstructor();
						}
					}
				}
				break;

			case TypeTraitKind::IsTriviallyConstructible:
				// __is_trivially_constructible(T, Args...) - Check if T can be trivially constructed
				// Scalar types are trivially constructible
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: no virtual, no user-defined ctors
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
					}
				}
				break;

			case TypeTraitKind::IsNothrowConstructible:
				// __is_nothrow_constructible(T, Args...) - Check if T can be constructed without throwing
				// Scalar types don't throw
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: similar to trivially constructible for now
				// TODO: Check for noexcept constructors
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
					}
				}
				break;

			case TypeTraitKind::IsAssignable:
				// __is_assignable(To, From) - Check if From can be assigned to To
				if (traitNode.has_second_type()) {
					const ASTNode& from_node = traitNode.second_type_node();
					if (from_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& from_spec = from_node.as<TypeSpecifierNode>();
						
						// For scalar types, check type compatibility
						if (isScalarType(type, is_reference, pointer_depth)) {
							// Scalars are assignable from compatible types
							result = isScalarType(from_spec.type(), from_spec.is_reference(), from_spec.pointer_depth());
						}
						// Class types: check for assignment operator
						else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info && !struct_info->is_union) {
								// If has copy/move assignment or no user-defined, assume assignable
								result = struct_info->hasCopyAssignmentOperator() || 
								         struct_info->hasMoveAssignmentOperator() ||
								         !struct_info->hasUserDefinedConstructor();
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsTriviallyAssignable:
				// __is_trivially_assignable(To, From) - Check if From can be trivially assigned to To
				if (traitNode.has_second_type()) {
					const ASTNode& from_node = traitNode.second_type_node();
					if (from_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& from_spec = from_node.as<TypeSpecifierNode>();
						
						// Scalar types are trivially assignable
						if (isScalarType(type, is_reference, pointer_depth) &&
						    isScalarType(from_spec.type(), from_spec.is_reference(), from_spec.pointer_depth())) {
							result = true;
						}
						// Class types: no virtual, no user-defined assignment
						else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
						         !is_reference && pointer_depth == 0) {
							const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info && !struct_info->is_union) {
								result = !struct_info->has_vtable && 
								         !struct_info->hasCopyAssignmentOperator() && 
								         !struct_info->hasMoveAssignmentOperator();
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsNothrowAssignable:
				// __is_nothrow_assignable(To, From) - Check if From can be assigned without throwing
				if (traitNode.has_second_type()) {
					const ASTNode& from_node = traitNode.second_type_node();
					if (from_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& from_spec = from_node.as<TypeSpecifierNode>();
						
						// Scalar types don't throw on assignment
						if (isScalarType(type, is_reference, pointer_depth) &&
						    isScalarType(from_spec.type(), from_spec.is_reference(), from_spec.pointer_depth())) {
							result = true;
						}
						// Class types: similar to trivially assignable for now
						// TODO: Check for noexcept assignment operators
						else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
						         !is_reference && pointer_depth == 0) {
							const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info && !struct_info->is_union) {
								result = !struct_info->has_vtable;
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsDestructible:
				// __is_destructible(T) - Check if T can be destroyed
				// All scalar types are destructible
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: check for accessible destructor
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Assume destructible unless we can prove otherwise
						// (no deleted destructor check available yet)
						result = true;
					}
				}
				break;

			case TypeTraitKind::IsTriviallyDestructible:
				// __is_trivially_destructible(T) - Check if T can be trivially destroyed
				// Scalar types are trivially destructible
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: no virtual, no user-defined destructor
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Trivially destructible if no vtable and no user-defined destructor
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedDestructor();
					} else if (struct_info && struct_info->is_union) {
						// Unions are trivially destructible if all members are
						result = true;
					}
				}
				break;

			case TypeTraitKind::IsNothrowDestructible:
				// __is_nothrow_destructible(T) - Check if T can be destroyed without throwing
				// Scalar types don't throw on destruction
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: assume noexcept destructor (most are in practice)
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Most destructors are noexcept by default since C++11
						result = true;
					}
				}
				break;

			case TypeTraitKind::HasTrivialDestructor:
				// __has_trivial_destructor(T) - GCC/Clang intrinsic, equivalent to IsTriviallyDestructible
				// Scalar types are trivially destructible
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: no virtual, no user-defined destructor
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Trivially destructible if no vtable and no user-defined destructor
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedDestructor();
					} else if (struct_info && struct_info->is_union) {
						// Unions are trivially destructible if all members are
						result = true;
					}
				}
				break;

			case TypeTraitKind::HasVirtualDestructor:
				// __has_virtual_destructor(T) - Check if T has a virtual destructor
				// Only class types can have virtual destructors
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Check if the destructor is explicitly marked as virtual
						// A class has a virtual destructor if:
						// 1. Its destructor is declared virtual, or
						// 2. It inherits from a base class with a virtual destructor
						// For now, we check if the class has a vtable (which implies virtual methods)
						// and if it has a user-defined destructor
						result = struct_info->has_vtable && struct_info->hasUserDefinedDestructor();
						
						// If the class has a vtable but no explicit destructor, check base classes
						if (!result && struct_info->has_vtable && !struct_info->base_classes.empty()) {
							// Check if any base class has a virtual destructor
							for (const auto& base : struct_info->base_classes) {
								if (base.type_index < gTypeInfo.size()) {
									const TypeInfo& base_type_info = gTypeInfo[base.type_index];
									const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
									if (base_struct_info && base_struct_info->has_vtable) {
										// If base has vtable, it might have virtual destructor
										// For simplicity, we assume presence of vtable indicates virtual destructor
										result = true;
										break;
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsLayoutCompatible:
				// __is_layout_compatible(T, U) - Check if T and U have the same layout
				if (traitNode.has_second_type()) {
					const ASTNode& second_node = traitNode.second_type_node();
					if (second_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& second_spec = second_node.as<TypeSpecifierNode>();
						
						// Same type is always layout compatible with itself
						if (type == second_spec.type() && 
						    pointer_depth == second_spec.pointer_depth() &&
						    is_reference == second_spec.is_reference()) {
							if (type == Type::Struct) {
								result = (type_spec.type_index() == second_spec.type_index());
							} else {
								result = true;
							}
						}
						// Different standard layout types with same size
						else if (isScalarType(type, is_reference, pointer_depth) &&
						         isScalarType(second_spec.type(), second_spec.is_reference(), second_spec.pointer_depth())) {
							result = (type_spec.size_in_bits() == second_spec.size_in_bits());
						}
					}
				}
				break;

			case TypeTraitKind::IsPointerInterconvertibleBaseOf:
				// __is_pointer_interconvertible_base_of(Base, Derived)
				// Check if Base is pointer-interconvertible with Derived
				// According to C++20: requires both to be standard-layout types and
				// Base is either the first base class or shares address with Derived
				if (traitNode.has_second_type()) {
					const ASTNode& derived_node = traitNode.second_type_node();
					if (derived_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& derived_spec = derived_node.as<TypeSpecifierNode>();
						
						// Both must be class types (not references, not pointers)
						if (type == Type::Struct && derived_spec.type() == Type::Struct &&
						    !is_reference && pointer_depth == 0 &&
						    !derived_spec.is_reference() && derived_spec.pointer_depth() == 0 &&
						    type_spec.type_index() < gTypeInfo.size() &&
						    derived_spec.type_index() < gTypeInfo.size()) {
							
							const TypeInfo& base_info = gTypeInfo[type_spec.type_index()];
							const TypeInfo& derived_info = gTypeInfo[derived_spec.type_index()];
							const StructTypeInfo* base_struct = base_info.getStructInfo();
							const StructTypeInfo* derived_struct = derived_info.getStructInfo();
							
							if (base_struct && derived_struct) {
								// Same type is pointer interconvertible with itself
								if (type_spec.type_index() == derived_spec.type_index()) {
									result = true;
								} else {
									// Both types must be standard-layout for pointer interconvertibility
									bool base_is_standard_layout = base_struct->isStandardLayout();
									bool derived_is_standard_layout = derived_struct->isStandardLayout();
									
									if (base_is_standard_layout && derived_is_standard_layout) {
										// Check if Base is the first base class at offset 0
										for (size_t i = 0; i < derived_struct->base_classes.size(); ++i) {
											if (derived_struct->base_classes[i].type_index == type_spec.type_index()) {
												// First base class at offset 0 is pointer interconvertible
												result = (i == 0);
												break;
											}
										}
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::UnderlyingType:
				// __underlying_type(T) returns the underlying type of an enum
				// This is a type query, not a bool result - handle specially
				if (type == Type::Enum && !is_reference && pointer_depth == 0 &&
				    type_spec.type_index() < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const EnumTypeInfo* enum_info = type_info.getEnumInfo();
					if (enum_info) {
						// Return the enum's declared underlying type
						return { enum_info->underlying_type, enum_info->underlying_size, 0ULL };
					}
					// Fallback to int if no enum info
					return { Type::Int, 32, 0ULL };
				}
				// For non-enums, this is an error - return false/0
				result = false;
				break;

			default:
				// For all other unary type traits, use the shared evaluator from TypeTraitEvaluator.h
				{
					TypeTraitResult eval_result = evaluateTypeTrait(traitNode.kind(), type_spec, outer_type_info, outer_struct_info);
					if (eval_result.success) {
						result = eval_result.value;
					} else {
						result = false;
					}
				}
				break;
		}

		// Return result as a bool constant
		// Format: [type, size_bits, value]
		return { Type::Bool, 8, static_cast<unsigned long long>(result ? 1 : 0) };
	}

