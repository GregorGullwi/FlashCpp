#include "Parser.h"
#include "IrGenerator.h"
#include "LambdaHelpers.h"
#include "SemanticAnalysis.h"

	ExprResult AstToIr::generateTypeConversion(const ExprResult& operands, Type fromType, Type toType, const Token& source_token) {
		// Get the actual size from the operands (they already contain the correct size)
		int fromSize = operands.size_in_bits.is_set() ? operands.size_in_bits.value : get_type_size_bits(fromType);

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

		// Check for int-to-float or float-to-int conversions
		bool from_is_float = is_floating_point_type(fromType);
		bool to_is_float = is_floating_point_type(toType);

		// Check if the value is a compile-time constant (literal)
		bool is_literal =
			std::holds_alternative<unsigned long long>(operands.value) ||
			std::holds_alternative<int>(operands.value) ||
			std::holds_alternative<double>(operands.value);

		if (is_literal) {
			if (from_is_float != to_is_float) {
				TempVar resultVar = var_counter.next();
				TypeConversionOp conv_op{
					.result = resultVar,
					.from = toTypedValue(operands),
					.to_type = toType,
					.to_size_in_bits = SizeInBits{toSize
				}};

				if (from_is_float) {
					ir_.addInstruction(IrInstruction(IrOpcode::FloatToInt, std::move(conv_op), source_token));
				} else {
					ir_.addInstruction(IrInstruction(IrOpcode::IntToFloat, std::move(conv_op), source_token));
				}

				return makeExprResult(toType, SizeInBits{toSize}, IrOperand{resultVar});
			}

			// For same-domain literal conversions, keep the value immediate.
			if (const auto* ull_val = std::get_if<unsigned long long>(&operands.value)) {
				unsigned long long value = *ull_val;
				return makeExprResult(toType, SizeInBits{toSize}, IrOperand{value});
			} else if (const auto* int_val = std::get_if<int>(&operands.value)) {
				int value = *int_val;
				return makeExprResult(toType, SizeInBits{toSize}, IrOperand{static_cast<unsigned long long>(value)});
			} else if (const auto* d_val = std::get_if<double>(&operands.value)) {
				double value = *d_val;
				return makeExprResult(toType, SizeInBits{toSize}, IrOperand{value});
			}
		}

		// For non-literal values (variables, TempVars), check if conversion is needed

		if (from_is_float != to_is_float) {
			// Converting between int and float (or vice versa)
			TempVar resultVar = var_counter.next();
			TypeConversionOp conv_op{
				.result = resultVar,
				.from = toTypedValue(operands),
				.to_type = toType,
				.to_size_in_bits = SizeInBits{toSize
			}};

			if (from_is_float && !to_is_float) {
				// Float to int conversion
				ir_.addInstruction(IrInstruction(IrOpcode::FloatToInt, std::move(conv_op), source_token));
			} else if (!from_is_float && to_is_float) {
				// Int to float conversion
				ir_.addInstruction(IrInstruction(IrOpcode::IntToFloat, std::move(conv_op), source_token));
			}

			return makeExprResult(toType, SizeInBits{toSize}, IrOperand{resultVar});
		}

		// If both are floats but different sizes, use FloatToFloat conversion
		if (from_is_float && to_is_float && fromSize != toSize) {
			TempVar resultVar = var_counter.next();
			TypeConversionOp conv_op{
				.result = resultVar,
				.from = toTypedValue(operands),
				.to_type = toType,
				.to_size_in_bits = SizeInBits{toSize
			}};
			ir_.addInstruction(IrInstruction(IrOpcode::FloatToFloat, std::move(conv_op), source_token));
			return makeExprResult(toType, SizeInBits{toSize}, IrOperand{resultVar});
		}

		// If sizes are equal and only signedness differs, no actual conversion instruction is needed
		// The value can be reinterpreted as the new type
		if (fromSize == toSize) {
			// Same size, different signedness - just change the type metadata
			return makeExprResult(
				toType,
				SizeInBits{toSize},
				operands.value,
				operands.type_index,
				operands.pointer_depth
			);
		}

		// For non-literal values (variables, TempVars), create a conversion instruction
		TempVar resultVar = var_counter.next();

		if (fromSize < toSize) {
			// Extension needed
			ConversionOp conv_op{
				.from = toTypedValue(operands),
				.to_type = toType,
				.to_size = toSize,
				.result = resultVar
			};

			// Determine whether to use sign extension or zero extension
			bool use_sign_extend = false;

			// For literals, check if the value fits in the signed range
			if (std::holds_alternative<unsigned long long>(operands.value)) {
				unsigned long long lit_value = std::get<unsigned long long>(operands.value);

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
				.from = toTypedValue(operands),
				.to_type = toType,
				.to_size = toSize,
				.result = resultVar
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Truncate, std::move(conv_op), source_token));
		}
		// Return the converted operands
		return makeExprResult(toType, SizeInBits{toSize}, IrOperand{resultVar});
	}

	ExprResult
		AstToIr::generateStringLiteralIr(const StringLiteralNode& stringLiteralNode) {
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
		return makeExprResult(Type::Char, SizeInBits{64}, IrOperand{result_var});
	}

	std::optional<AstToIr::AddressComponents> AstToIr::analyzeAddressExpression(
		const ExpressionNode& expr,
		int accumulated_offset)
	{
		// Handle Identifier (base case)
		if (std::holds_alternative<IdentifierNode>(expr)) {
			const IdentifierNode& identifier = std::get<IdentifierNode>(expr);
			StringHandle identifier_handle = StringTable::getOrInternStringHandle(identifier.name());

			// Static locals, static members, and globals live in static storage, so
			// preserve the actual storage symbol for downstream ComputeAddress handling.
			{
				const auto binding_info = resolveGlobalOrStaticBinding(identifier);
				if (binding_info.is_global_or_static &&
					binding_info.type != Type::Void &&
					binding_info.size_in_bits.is_set()) {
					AddressComponents result;
					result.base = binding_info.store_name;
					result.total_member_offset = accumulated_offset;
					result.final_type = binding_info.type;
					result.final_size_bits = binding_info.size_in_bits;
					return result;
				}
			}

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
			result.final_size_bits = SizeInBits{static_cast<int>(type_node->size_in_bits())};
			if (result.final_type == Type::Struct && !result.final_size_bits.is_set() &&
				type_node->type_index().is_valid() && type_node->type_index().value < gTypeInfo.size()) {
				if (const StructTypeInfo* struct_info = gTypeInfo[type_node->type_index().value].getStructInfo()) {
					result.final_size_bits = SizeInBits{static_cast<int>(struct_info->total_size * 8)};
				}
			}
			result.pointer_depth = PointerDepth{static_cast<int>(type_node->pointer_depth())};
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
			ExprResult object_operands = visitExpressionNode(obj_expr, ExpressionContext::LValueAddress);

			Type object_type = object_operands.type;
			TypeIndex type_index {};
			if (object_operands.type_index.is_valid()) {
				type_index = object_operands.type_index;
			}

			// Look up member information
			if (!type_index.is_valid() || type_index.value >= gTypeInfo.size() || object_type != Type::Struct) {
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
			base_components->final_size_bits = SizeInBits{static_cast<int>(result.member->size * 8)};
			// Use explicit pointer depth from struct member layout
			base_components->pointer_depth = PointerDepth{result.member->pointer_depth};

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
			ExprResult array_operands = visitExpressionNode(arraySubscript.array_expr().as<ExpressionNode>());
			ExprResult index_operands = visitExpressionNode(arraySubscript.index_expr().as<ExpressionNode>());


			Type element_type = array_operands.type;
			int element_size_bits = array_operands.size_in_bits.value;
			int element_pointer_depth = 0;  // Track pointer depth for pointer array elements

			// Calculate actual element size from array declaration
			if (std::holds_alternative<StringHandle>(array_operands.value)) {
				StringHandle array_name = std::get<StringHandle>(array_operands.value);
				const DeclarationNode* decl_ptr = lookupDeclaration(array_name);
				if (decl_ptr && (decl_ptr->is_array() || decl_ptr->type_node().as<TypeSpecifierNode>().is_array())) {
					const TypeSpecifierNode& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
					if (type_node.pointer_depth() > 0) {
						element_size_bits = 64;
						element_pointer_depth = type_node.pointer_depth();  // Track pointer depth
					} else if (type_node.type() == Type::Struct) {
						TypeIndex type_index_from_decl = type_node.type_index();
						if (type_index_from_decl.is_valid() && type_index_from_decl.value < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_index_from_decl.value];
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
			} else if (std::holds_alternative<TempVar>(array_operands.value)) {
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
				if (array_operands.pointer_depth.is_pointer()) {
					element_pointer_depth = array_operands.pointer_depth.value;
				}
			}

			// Recurse on the array expression (could be nested: arr[i][j])
			auto base_components = analyzeAddressExpression(arraySubscript.array_expr().as<ExpressionNode>(), accumulated_offset);

			if (!base_components.has_value()) {
				return std::nullopt;
			}

			// Add this array index
			ComputeAddressOp::ArrayIndex arr_idx;
			arr_idx.element_size_bits = SizeInBits{element_size_bits};

			// Capture index type information for proper sign extension
			arr_idx.index_type = index_operands.type;
			arr_idx.index_size_bits = index_operands.size_in_bits;

			// Set index value
			if (const auto* ull_val = std::get_if<unsigned long long>(&index_operands.value)) {
				arr_idx.index = *ull_val;
			} else if (const auto* temp_var = std::get_if<TempVar>(&index_operands.value)) {
				arr_idx.index = *temp_var;
			} else if (const auto* string = std::get_if<StringHandle>(&index_operands.value)) {
				arr_idx.index = *string;
			} else {
				return std::nullopt;
			}

			base_components->array_indices.push_back(arr_idx);
			base_components->final_type = element_type;
			base_components->final_size_bits = SizeInBits{element_size_bits};
			base_components->pointer_depth = PointerDepth{element_pointer_depth};  // Set pointer depth for the element

			return base_components;
		}

		// Unsupported expression type
		return std::nullopt;
	}

	std::optional<ExprResult> AstToIr::decayLambdaStructToFunctionPointer(const StructTypeInfo& struct_info, const Token& source_token) {
		auto sig_opt = getFunctionSignatureFromLambdaStruct(struct_info);
		if (!sig_opt.has_value()) {
			return std::nullopt;
		}

		const auto& sig = *sig_opt;

		std::string_view invoke_name = StringBuilder()
			.append(StringTable::getStringView(struct_info.getName()))
			.append("_invoke")
			.commit();

		std::string_view mangled = generateMangledNameForCall(
			invoke_name,
			sig.return_type,
			sig.param_types,
			false,
			""
		);

		TempVar func_addr_var = var_counter.next();
		FunctionAddressOp op;
		op.result.type = Type::FunctionPointer;
		op.result.ir_type = IrType::FunctionPointer;
		op.result.size_in_bits = SizeInBits{64};
		op.result.value = func_addr_var;
		op.function_name = StringTable::getOrInternStringHandle(invoke_name);
		op.mangled_name = StringTable::getOrInternStringHandle(mangled);
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionAddress, std::move(op), source_token));
		return makeExprResult(Type::FunctionPointer, SizeInBits{64}, IrOperand{func_addr_var});
	}

	ExprResult AstToIr::generateUnaryOperatorIr(const UnaryOperatorNode& unaryOperatorNode,
	ExpressionContext context) {
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
						auto overload_result = findUnaryOperatorOverload(type_node->type_index(), OverloadableOperator::BitwiseAnd);

						if (overload_result.has_match) {
							// Found an overload! Generate a member function call instead of built-in address-of
							FLASH_LOG_FORMAT(Codegen, Debug, "Resolving operator& overload for type index {}",
							type_node->type_index());

							const StructMemberFunction& member_func = *overload_result.member_overload;
							const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();

							// Get struct name for mangling
							std::string_view struct_name = StringTable::getStringView(gTypeInfo[type_node->type_index().value].name());

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
								call_op.return_size_in_bits = SizeInBits{64};
							} else {
								call_op.return_size_in_bits = SizeInBits{static_cast<int>(return_type.size_in_bits())};
								if (!call_op.return_size_in_bits.is_set()) {
									call_op.return_size_in_bits = SizeInBits{get_type_size_bits(return_type.type())};
								}
							}
							call_op.function_name = mangled_name;  // MangledName implicitly converts to StringHandle
							call_op.is_variadic = false;
							call_op.is_member_function = true;  // This is a member function call

							// Add 'this' pointer as first argument
							call_op.args.push_back(makeTypedValue(type_node->type(), SizeInBits{64}, IrValue(identifier_handle)));

							// Capture return metadata before the move invalidates call_op.
							SizeInBits ret_size_in_bits = call_op.return_size_in_bits;
							// Add the function call instruction
							ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), unaryOperatorNode.get_token()));

							// Return the result
							return makeExprResult(
								return_type.type(),
								ret_size_in_bits,
								IrOperand{ret_var},
								return_type.type_index(),
								PointerDepth{static_cast<int>(return_type.pointer_depth())}
							);
						}
					}
				}
			}
		}

		auto tryBuildIdentifierOperand = [&](const IdentifierNode& identifier, ExprResult& out) -> bool {
			// Phase 4: Using StringHandle for lookup
			StringHandle identifier_handle = StringTable::getOrInternStringHandle(identifier.name());

			const DeclarationNode* decl = lookupDeclaration(identifier_handle);
			const TypeSpecifierNode* type_node = decl ? &decl->type_node().as<TypeSpecifierNode>() : nullptr;

			if ((unaryOperatorNode.op() == "++" || unaryOperatorNode.op() == "--") && type_node) {
				const auto binding_info = resolveGlobalOrStaticBinding(identifier);
				if (binding_info.is_global_or_static && binding_info.type != Type::Void && binding_info.size_in_bits.is_set()) {
					int size_bits = (type_node->pointer_depth() > 0 || type_node->is_reference() || type_node->is_function_pointer())
						? 64
						: static_cast<int>(type_node->size_in_bits());

					TempVar result_temp = var_counter.next();
					GlobalLoadOp load_op;
					load_op.result.type = type_node->type();
					load_op.result.ir_type = toIrType(type_node->type());
					load_op.result.size_in_bits = SizeInBits{static_cast<int>(size_bits)};
					load_op.result.value = result_temp;
					load_op.global_name = binding_info.store_name;
					ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(load_op), unaryOperatorNode.get_token()));

					setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(
						LValueInfo(LValueInfo::Kind::Global, binding_info.store_name),
						type_node->type(), size_bits));

					out = makeExprResult(
						type_node->type(),
						SizeInBits{size_bits},
						IrOperand{result_temp},
						type_node->type_index(),
						PointerDepth{static_cast<int>(type_node->pointer_depth())}
					);
					return true;
				}
			}

			// Static local variables are stored as globals with mangled names
			auto static_local_it = static_local_names_.find(identifier_handle);
			if (static_local_it != static_local_names_.end()) {
				constexpr TypeIndex kStaticLocalTypeIndex {};
				out = makeExprResult(
					static_local_it->second.type,
					static_local_it->second.size_in_bits,
					IrOperand{static_local_it->second.mangled_name},
					kStaticLocalTypeIndex,
					PointerDepth{}
				); // pointer depth is always 0 for static locals here
				return true;
			}

			if (!decl) {
				return false;
			}

			// For the 4th element:
			// - For struct types, ALWAYS return type_index (even if it's a pointer to struct)
			// - For non-struct pointer types, return pointer_depth
			// - Otherwise return 0
			out = makeExprResult(
				type_node->type(),
				SizeInBits{static_cast<int>(type_node->size_in_bits())},
				IrOperand{identifier_handle},
				type_node->type_index(),
				PointerDepth{static_cast<int>(type_node->pointer_depth())}
			);
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
				ExprResult result = makeExprResult(
					addr_components->final_type,
					SizeInBits{64},
					result_var,
					TypeIndex{},
					PointerDepth{addr_components->pointer_depth.value + 1});
				return result;
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
						ExprResult array_operands = visitExpressionNode(arraySubscript.array_expr().as<ExpressionNode>());
						ExprResult index_operands = visitExpressionNode(arraySubscript.index_expr().as<ExpressionNode>());

						// Check that we have valid operands
						{
							Type element_type = array_operands.type;
							int element_size_bits = array_operands.size_in_bits.value;

							// For arrays, array_operands[1] is the pointer size (64), not element size
							// We need to calculate the actual element size from the array declaration
							if (std::holds_alternative<StringHandle>(array_operands.value)) {
								StringHandle array_name = std::get<StringHandle>(array_operands.value);
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
										if (type_index_from_decl.is_valid() && type_index_from_decl.value < gTypeInfo.size()) {
											const TypeInfo& type_info = gTypeInfo[type_index_from_decl.value];
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
							TypeIndex type_index {};
							if (array_operands.type_index.is_valid()) {
								type_index = array_operands.type_index;
							}

							// Look up member information
							if (type_index.is_valid() && type_index.value < gTypeInfo.size() && element_type == Type::Struct) {
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
									if (const auto* string = std::get_if<StringHandle>(&array_operands.value)) {
										elem_addr_payload.array = *string;
									} else if (const auto* temp_var = std::get_if<TempVar>(&array_operands.value)) {
										elem_addr_payload.array = *temp_var;
									}

									// Set index as TypedValue
									elem_addr_payload.index = toTypedValue(index_operands);

									ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(elem_addr_payload), arraySubscript.bracket_token()));

									// Now compute the member address by adding the member offset
									// We need to add the offset to the pointer value
									// Treat the pointer as a 64-bit integer for arithmetic purposes
									TempVar member_addr_var = var_counter.next();
									BinaryOp add_offset;
									add_offset.lhs = { Type::UnsignedLongLong, SizeInBits{POINTER_SIZE_BITS}, elem_addr_var };  // pointer treated as integer
									add_offset.rhs = { Type::UnsignedLongLong, SizeInBits{POINTER_SIZE_BITS}, static_cast<unsigned long long>(member_result.adjusted_offset) };
									add_offset.result = member_addr_var;

									ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_offset), memberAccess.member_token()));

									// Return pointer to member (64-bit pointer, 0 for no additional type info)
									return makeExprResult(member_result.member->type, SizeInBits{POINTER_SIZE_BITS}, IrOperand{member_addr_var});
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
					ExprResult object_operands = visitExpressionNode(object_node.as<ExpressionNode>(), ExpressionContext::LValueAddress);

					{
						Type object_type = object_operands.type;

						// Get the struct type index
						TypeIndex type_index {};
						if (object_operands.type_index.is_valid()) {
							type_index = object_operands.type_index;
						}

						// Look up member information
						if (type_index.is_valid() && type_index.value < gTypeInfo.size() && object_type == Type::Struct) {
							std::string_view member_name = memberAccess.member_name();
							StringHandle member_handle = StringTable::getOrInternStringHandle(std::string(member_name));
							auto member_result = FlashCpp::gLazyMemberResolver.resolve(type_index, member_handle);

							if (member_result) {
								TempVar result_var = var_counter.next();

								// For simple identifiers, generate a MemberAddressOp or use AddressOf with member context
								// For now, use a simpler approach: emit AddressOf, then Add offset in generated code
								// But mark the intermediate as NOT a reference to avoid dereferencing

								if (std::holds_alternative<StringHandle>(object_operands.value)) {
									StringHandle obj_name = std::get<StringHandle>(object_operands.value);

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
									return makeExprResult(member_result.member->type, SizeInBits{POINTER_SIZE_BITS}, IrOperand{result_var});
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
									assign_op.lhs = makeTypedValue(Type::UnsignedLongLong, SizeInBits{64}, flat_index);
									assign_op.rhs = toTypedValue(idx_operands);
									ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
									first_term = false;
								} else {
									// flat_index += indices[k]
									TempVar new_flat = var_counter.next();
									BinaryOp add_op;
									add_op.lhs = makeTypedValue(Type::UnsignedLongLong, SizeInBits{64}, flat_index);
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
								mul_op.rhs = makeTypedValue(Type::UnsignedLongLong, SizeInBits{64}, static_cast<unsigned long long>(strides[k]));
								mul_op.result = IrValue{temp_prod};
								ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));

								if (first_term) {
									flat_index = temp_prod;
									first_term = false;
								} else {
									// flat_index += temp
									TempVar new_flat = var_counter.next();
									BinaryOp add_op;
									add_op.lhs = makeTypedValue(Type::UnsignedLongLong, SizeInBits{64}, flat_index);
									add_op.rhs = makeTypedValue(Type::UnsignedLongLong, SizeInBits{64}, temp_prod);
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
						payload.index.ir_type = IrType::Integer;
						payload.index.size_in_bits = SizeInBits{64};
						payload.index.value = flat_index;
						payload.is_pointer_to_array = false;  // Multidimensional arrays are actual arrays, not pointers

						ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(payload), arraySubscript.bracket_token()));

						return makeExprResult(element_type, SizeInBits{64}, addr_var, element_type_index);
					}
				}

				// Fall through to single-dimensional array handling
				single_dim_handling:
				// Get the array and index operands
				ExprResult array_operands = visitExpressionNode(arraySubscript.array_expr().as<ExpressionNode>());
				ExprResult index_operands = visitExpressionNode(arraySubscript.index_expr().as<ExpressionNode>());

				Type element_type = array_operands.type;
				int element_size_bits = array_operands.size_in_bits.value;

				// For arrays, array_operands[1] is the pointer size (64), not element size
				// We need to calculate the actual element size from the array declaration
				if (std::holds_alternative<StringHandle>(array_operands.value)) {
					StringHandle array_name = std::get<StringHandle>(array_operands.value);
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
							if (type_index.is_valid() && type_index.value < gTypeInfo.size()) {
								const TypeInfo& type_info = gTypeInfo[type_index.value];
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
				if (const auto* string = std::get_if<StringHandle>(&array_operands.value)) {
					payload.array = *string;
				} else if (const auto* temp_var = std::get_if<TempVar>(&array_operands.value)) {
					payload.array = *temp_var;
				}

				// Set index as TypedValue
				payload.index = toTypedValue(index_operands);

				ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(payload), arraySubscript.bracket_token()));

				// Return pointer to element (64-bit pointer)
				return makeExprResult(element_type, SizeInBits{64}, IrOperand{addr_var});
			}
		}

		// Helper lambda to generate member increment/decrement IR
		// Returns the result operands, or empty if not applicable
		// adjusted_offset must be provided (from LazyMemberResolver result)
		auto generateMemberIncDec = [&](StringHandle object_name,
		const StructMember* member, bool is_reference_capture,
		const Token& token, size_t adjusted_offset) -> ExprResult {
			int member_size_bits = static_cast<int>(member->size * 8);
			int increment_amount = (member->pointer_depth > 0) ?
				getPointerElementSize(member->type, member->type_index, member->pointer_depth) : 1;
			TempVar result_var = var_counter.next();
			StringHandle member_name = member->getName();

			if (is_reference_capture) {
				// By-reference: load pointer, dereference, inc/dec, store back through pointer
				TempVar ptr_temp = var_counter.next();
				MemberLoadOp member_load;
				member_load.result.value = ptr_temp;
				member_load.result.type = member->type;
				member_load.result.size_in_bits = SizeInBits{64};  // pointer
				member_load.object = object_name;
				member_load.member_name = member_name;
				member_load.offset = static_cast<int>(adjusted_offset);
				member_load.ref_qualifier = CVReferenceQualifier::LValueReference;
				member_load.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), token));

				// Load current value through pointer
				TempVar current_val = emitDereference(member->type, 64, 1, ptr_temp, token);

				bool is_prefix = unaryOperatorNode.is_prefix();
				BinaryOp add_op{
					.lhs = { member->type, SizeInBits{member_size_bits}, current_val },
					.rhs = { Type::Int, SizeInBits{32}, static_cast<unsigned long long>(increment_amount) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(
					unaryOperatorNode.op() == "++" ? IrOpcode::Add : IrOpcode::Subtract,
					std::move(add_op), token));

				// Store back through pointer
				DereferenceStoreOp store_op;
				store_op.pointer.type = member->type;
				store_op.pointer.size_in_bits = SizeInBits{64};  // Pointer is always 64 bits
				store_op.pointer.pointer_depth = PointerDepth{1};  // Single pointer dereference
				store_op.pointer.value = ptr_temp;
				store_op.value = { member->type, SizeInBits{member_size_bits}, result_var };
				ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_op), token));

				TempVar return_val = is_prefix ? result_var : current_val;
				return makeExprResult(member->type, SizeInBits{member_size_bits}, IrOperand{return_val});
			} else {
				// By-value: load member, inc/dec, store back to member
				TempVar current_val = var_counter.next();
				MemberLoadOp member_load;
				member_load.result.value = current_val;
				member_load.result.type = member->type;
				member_load.result.size_in_bits = SizeInBits{static_cast<int>(member_size_bits)};
				member_load.object = object_name;
				member_load.member_name = member_name;
				member_load.offset = static_cast<int>(adjusted_offset);
				member_load.ref_qualifier = CVReferenceQualifier::None;
				member_load.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), token));

				bool is_prefix = unaryOperatorNode.is_prefix();
				BinaryOp add_op{
					.lhs = { member->type, SizeInBits{member_size_bits}, current_val },
					.rhs = { Type::Int, SizeInBits{32}, static_cast<unsigned long long>(increment_amount) },
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
				store_op.value = { member->type, SizeInBits{member_size_bits}, result_var };
				store_op.ref_qualifier = CVReferenceQualifier::None;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_op), token));

				TempVar return_val = is_prefix ? result_var : current_val;
				return makeExprResult(member->type, SizeInBits{member_size_bits}, IrOperand{return_val});
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
									if (type_index.value < gTypeInfo.size()) {
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

		ExprResult operandIrOperands;
		bool operandHandledAsIdentifier = false;
		// For ++, --, and & operators on identifiers, use tryBuildIdentifierOperand
		// This ensures we get the variable name (or static local's mangled name) directly
		// rather than generating a load that would lose the variable identity
		if ((unaryOperatorNode.op() == "++" || unaryOperatorNode.op() == "--" || unaryOperatorNode.op() == "&") && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (const auto* identifier_ptr = std::get_if<IdentifierNode>(&operandExpr)) {
				const IdentifierNode& identifier = *identifier_ptr;
				operandHandledAsIdentifier = tryBuildIdentifierOperand(identifier, operandIrOperands);
			}
		}

		// Special case: unary plus on lambda triggers decay to function pointer
		// Handle both lambda literals and identifiers bound to captureless lambdas
		if (unaryOperatorNode.op() == "+" && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			const LambdaExpressionNode* lambda_ptr = nullptr;
			const StructTypeInfo* lambda_struct_info = nullptr;

			if (const auto* lambda_expression = std::get_if<LambdaExpressionNode>(&operandExpr)) {
				lambda_ptr = lambda_expression;
			} else if (std::holds_alternative<IdentifierNode>(operandExpr)) {
				const IdentifierNode& ident = std::get<IdentifierNode>(operandExpr);
				auto symbol = lookupSymbol(ident.nameHandle());
				if (symbol.has_value() && symbol->is<DeclarationNode>()) {
					const auto& decl = symbol->as<DeclarationNode>();
					const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
					// If the variable's type is the closure struct for a lambda, derive invoke signature from struct info
					if (type_node.type() == Type::Struct && type_node.type_index().value < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_node.type_index().value];
						const StructTypeInfo* struct_info = type_info.getStructInfo();
						if (struct_info && isLambdaClosureStruct(*struct_info)) {
							lambda_struct_info = struct_info;
							FLASH_LOG_FORMAT(Codegen, Debug, "Unary plus on lambda identifier '{}' -> using struct info", StringTable::getStringView(type_info.name()));
						}
					}
					// Fallback: if struct info path didn't find a captureless closure,
					// try extracting the lambda AST node from the initializer.
					if (!lambda_struct_info && !lambda_ptr && decl.has_default_value()) {
						const ASTNode& init = decl.default_value();
						if (init.is<LambdaExpressionNode>()) {
							lambda_ptr = &init.as<LambdaExpressionNode>();
						} else if (init.is<ExpressionNode>() && std::holds_alternative<LambdaExpressionNode>(init.as<ExpressionNode>())) {
							lambda_ptr = &std::get<LambdaExpressionNode>(init.as<ExpressionNode>());
						}
					}
				}
			}

			if (lambda_ptr && lambda_ptr->captures().empty()) {
				// Generate the lambda functions (operator(), __invoke, etc.)
				generateLambdaExpressionIr(*lambda_ptr);

				// Return the address of the __invoke function
				TempVar func_addr_var = generateLambdaInvokeFunctionAddress(*lambda_ptr);
				return makeExprResult(Type::FunctionPointer, SizeInBits{64}, IrOperand{func_addr_var});
			} else if (lambda_struct_info) {
				if (auto fp_operands = decayLambdaStructToFunctionPointer(*lambda_struct_info, unaryOperatorNode.get_token())) {
					return *fp_operands;
				}
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
							return makeExprResult(
								member_result.member->type,
								SizeInBits{64},
								IrOperand{static_cast<unsigned long long>(member_result.adjusted_offset)},
								TypeIndex{member_result.member->type_index}
							);
						}
					}
				}
			}
		}

		if (!operandHandledAsIdentifier) {
			operandIrOperands = visitExpressionNode(unaryOperatorNode.get_operand().as<ExpressionNode>());
		}

		// Get the type of the operand
		Type operandType = operandIrOperands.type;
		[[maybe_unused]] int operandSize = operandIrOperands.size_in_bits.value;

		// Fallback: if operand is a captureless lambda closure object, decay to function pointer using struct info
		if (unaryOperatorNode.op() == "+" && operandType == Type::Struct) {
			size_t struct_type_index = operandIrOperands.type_index.value;
			if (struct_type_index == 0 && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
				const ExpressionNode& op_expr = unaryOperatorNode.get_operand().as<ExpressionNode>();
				if (const auto* identifier = std::get_if<IdentifierNode>(&op_expr)) {
					auto sym = lookupSymbol(identifier->nameHandle());
					if (sym.has_value() && sym->is<DeclarationNode>()) {
						struct_type_index = sym->as<DeclarationNode>().type_node().as<TypeSpecifierNode>().type_index().value;
					}
				}
			}
			if (struct_type_index > 0 && struct_type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[struct_type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info && isLambdaClosureStruct(*struct_info)) {
					FLASH_LOG_FORMAT(Codegen, Debug, "Unary plus decay via struct info: type_index={}, name={}", struct_type_index, StringTable::getStringView(type_info.name()));
					if (auto fp_operands = decayLambdaStructToFunctionPointer(*struct_info, unaryOperatorNode.get_token())) {
						return *fp_operands;
					}
				}
			}
		}

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();

		// Generate the IR for the operation based on the operator
		if (unaryOperatorNode.op() == "!") {
			// C++20 [expr.unary.op]/9: the operand of ! is contextually converted
			// to bool. For float/double operands this requires an explicit
			// FloatToInt conversion (same -0.0 fix as conditions / && / ||).
			operandIrOperands = applyConditionBoolConversion(operandIrOperands, unaryOperatorNode.get_operand(), Token());
			// Logical NOT - use UnaryOp struct
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};
			ir_.addInstruction(IrInstruction(IrOpcode::LogicalNot, unary_op, Token()));
			// Logical NOT always returns bool8
			return makeExprResult(Type::Bool, SizeInBits{8}, IrOperand{result_var});
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
			ExprResult operand_result = operandIrOperands;

			// Check for user-defined operator++ overload on struct types
			auto inc_result = generateUnaryIncDecOverloadCall(OverloadableOperator::Increment, operandType, operand_result, unaryOperatorNode.is_prefix());
			if (inc_result.has_value()) {
				return *inc_result;
			}

			return generateBuiltinIncDec(true, unaryOperatorNode.is_prefix(), operandHandledAsIdentifier, unaryOperatorNode, operand_result, operandType, result_var);
		}
		else if (unaryOperatorNode.op() == "--") {
			// Decrement operator (prefix or postfix)
			ExprResult operand_result = operandIrOperands;

			// Check for user-defined operator-- overload on struct types
			auto dec_result = generateUnaryIncDecOverloadCall(OverloadableOperator::Decrement, operandType, operand_result, unaryOperatorNode.is_prefix());
			if (dec_result.has_value()) {
				return *dec_result;
			}

			return generateBuiltinIncDec(false, unaryOperatorNode.is_prefix(), operandHandledAsIdentifier, unaryOperatorNode, operand_result, operandType, result_var);
		}
		else if (unaryOperatorNode.op() == "&") {
			// Address-of operator: &x
			// Get the current pointer depth from operandIrOperands
			unsigned long long operand_ptr_depth = static_cast<unsigned long long>(operandIrOperands.pointer_depth.value);

			// Create typed payload with TypedValue
			AddressOfOp op;
			op.result = result_var;

			// Populate TypedValue with full type information
			op.operand.type = operandType;
			op.operand.size_in_bits = operandIrOperands.size_in_bits;
			op.operand.pointer_depth = PointerDepth{static_cast<int>(operand_ptr_depth)};

			// Get the operand value - it's at index 2 in operandIrOperands
			if (const auto* string = std::get_if<StringHandle>(&operandIrOperands.value)) {
				op.operand.value = *string;
			} else if (const auto* temp_var = std::get_if<TempVar>(&operandIrOperands.value)) {
				op.operand.value = *temp_var;
			} else {
				throw InternalError("AddressOf operand must be StringHandle or TempVar");
			}

			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, op, Token()));
			// Return 64-bit pointer with incremented pointer depth
			return makeExprResult(
				operandType,
				SizeInBits{64},
				IrOperand{result_var},
				operandIrOperands.type_index,
				PointerDepth{static_cast<int>(operand_ptr_depth + 1)}
			);
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
				if (operandIrOperands.pointer_depth.is_pointer()) {
					pointer_depth = operandIrOperands.pointer_depth.value;
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
				if (const auto* string = std::get_if<StringHandle>(&operandIrOperands.value)) {
					base = *string;
				} else if (const auto* temp_var_ptr = std::get_if<TempVar>(&operandIrOperands.value)) {
					base = *temp_var_ptr;
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
				if (const auto* string = std::get_if<StringHandle>(&operandIrOperands.value)) {
					rhs_value = *string;
				} else if (const auto* temp_var = std::get_if<TempVar>(&operandIrOperands.value)) {
					rhs_value = *temp_var;
				} else if (const auto* ull_val = std::get_if<unsigned long long>(&operandIrOperands.value)) {
					rhs_value = *ull_val;
				} else {
					rhs_value = 0ULL;
				}
				AssignmentOp copy_op;
				copy_op.result = lvalue_temp;
				copy_op.lhs = makeTypedValue(operandType, SizeInBits{64}, lvalue_temp);
				copy_op.rhs = makeTypedValue(operandType, SizeInBits{64}, rhs_value);
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
				return makeExprResult(
					operandType,
					SizeInBits{64},
					IrOperand{lvalue_temp},
					operandIrOperands.type_index,
					PointerDepth{static_cast<int>(result_ptr_depth)}
				);
			}

			int element_size = 64; // Default to pointer size
			int pointer_depth = 0;

			// First, try to get pointer depth from operandIrOperands (for TempVar results from previous operations)
			pointer_depth = operandIrOperands.pointer_depth.value;
			// If pointer_depth is still 0, look up the pointer operand in the symbol table.
			if (pointer_depth == 0 && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
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
			op.pointer.size_in_bits = SizeInBits{static_cast<int>(element_size)};
			op.pointer.pointer_depth = PointerDepth{pointer_depth};

			// Get the pointer value - it's at index 2 in operandIrOperands
			if (const auto* string = std::get_if<StringHandle>(&operandIrOperands.value)) {
				op.pointer.value = *string;
			} else if (const auto* temp_var = std::get_if<TempVar>(&operandIrOperands.value)) {
				op.pointer.value = *temp_var;
			} else {
				throw InternalError("Dereference pointer must be StringHandle or TempVar");
			}

			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, op, Token()));

			// Mark dereference result as lvalue (Option 2: Value Category Tracking)
			// *ptr is an lvalue - it designates the dereferenced object
			// Extract StringHandle or TempVar from pointer.value (IrValue)
			std::variant<StringHandle, TempVar> base;
			if (const auto* string = std::get_if<StringHandle>(&op.pointer.value)) {
				base = *string;
			} else if (const auto* temp_var = std::get_if<TempVar>(&op.pointer.value)) {
				base = *temp_var;
			}
			LValueInfo lvalue_info(
				LValueInfo::Kind::Indirect,
				base,
				0  // offset is 0 for simple dereference
			);
			setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));

			// Return the dereferenced value with the decremented pointer depth
			unsigned long long result_ptr_depth = (pointer_depth > 0) ? (pointer_depth - 1) : 0;
			return makeExprResult(
				operandType,
				SizeInBits{static_cast<int>(element_size)},
				IrOperand{result_var},
				operandIrOperands.type_index,
				PointerDepth{static_cast<int>(result_ptr_depth)}
			);
		}
		else {
			throw InternalError("Unary operator not implemented yet");
		}

		// Return the result
		return makeExprResult(operandType, operandIrOperands.size_in_bits, IrOperand{result_var});
	}



// Helper: generate a member function call for user-defined operator++/-- overloads on structs.
// Returns the IR operands {result_type, SizeInBits{result_size}, ret_var, result_type_index} on success,
// or std::nullopt if no overload was found.
std::optional<ExprResult> AstToIr::generateUnaryIncDecOverloadCall(
	OverloadableOperator op_kind,  // Increment or Decrement
	Type operandType,
	const ExprResult& operandIrResult,
	bool is_prefix
) {
	if (operandType != Type::Struct)
		return std::nullopt;

	TypeIndex operand_type_index = operandIrResult.type_index;
	if (!operand_type_index.is_valid())
		return std::nullopt;

	// For ++/--, we need to distinguish prefix (0 params) from postfix (1 param: dummy int).
	// findUnaryOperatorOverload returns the first match; scan all member functions to pick
	// the overload whose parameter count matches the call form.
	size_t expected_param_count = is_prefix ? 0 : 1;
	const StructMemberFunction* matched_func = nullptr;
	const StructMemberFunction* fallback_func = nullptr;
	if (operand_type_index.value < gTypeInfo.size()) {
		const StructTypeInfo* struct_info = gTypeInfo[operand_type_index.value].getStructInfo();
		if (struct_info) {
			for (const auto& mf : struct_info->member_functions) {
				if (mf.operator_kind == op_kind) {
					const auto& fd = mf.function_decl.as<FunctionDeclarationNode>();
					if (fd.parameter_nodes().size() == expected_param_count) {
						matched_func = &mf;
						break;
					}
					if (!fallback_func) fallback_func = &mf;
				}
			}
		}
	}
	// Fallback: if no exact arity match, use any operator++ / operator-- overload.
	// This handles the common case where only one form (prefix or postfix) is defined.
	if (!matched_func) matched_func = fallback_func;
	if (!matched_func)
		return std::nullopt;

	const StructMemberFunction& member_func = *matched_func;
	const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
	std::string_view struct_name = StringTable::getStringView(gTypeInfo[operand_type_index.value].name());
	TypeSpecifierNode return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
	resolveSelfReferentialType(return_type, operand_type_index);

	std::vector<TypeSpecifierNode> param_types;
	// Use the matched function's actual parameter count for mangling, not the call form.
	// When the fallback path is taken (e.g., only prefix defined but postfix called),
	// we must mangle to match the definition, not the call site.
	const auto& actual_params = func_decl.parameter_nodes();
	if (actual_params.size() == 1 && actual_params[0].is<DeclarationNode>()) {
		// Postfix overload has a dummy int parameter
		TypeSpecifierNode int_type(Type::Int, TypeQualifier::None, 32, Token());
		param_types.push_back(int_type);
	}
	std::vector<std::string_view> empty_namespace;
	auto op_func_name = StringBuilder().append("operator").append(overloadableOperatorToString(op_kind)).commit();
	auto mangled_name = NameMangling::generateMangledName(
		op_func_name, return_type, param_types, false,
		struct_name, empty_namespace, Linkage::CPlusPlus
	);

	TempVar ret_var = var_counter.next();
	CallOp call_op;
	call_op.result = ret_var;
	call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
	call_op.return_type = return_type.type();
	call_op.return_size_in_bits = SizeInBits{static_cast<int>(return_type.size_in_bits())};
	if (!call_op.return_size_in_bits.is_set() && return_type.type_index().is_valid() && return_type.type_index().value < gTypeInfo.size() && gTypeInfo[return_type.type_index().value].struct_info_) {
		call_op.return_size_in_bits = SizeInBits{static_cast<int>(gTypeInfo[return_type.type_index().value].struct_info_->total_size * 8)};
	}
	call_op.return_type_index = return_type.type_index();
	call_op.is_member_function = true;

	// Detect if returning struct by value (needs hidden return parameter for RVO).
	// Small structs (≤ ABI threshold) return in registers and need no return_slot.
	if (needsHiddenReturnParam(return_type.type(), return_type.pointer_depth(), return_type.is_reference(), call_op.return_size_in_bits.value, context_->isLLP64())) {
		call_op.return_slot = ret_var;
	}

	// Take address of operand for 'this' pointer
	TempVar this_addr = var_counter.next();
	AddressOfOp addr_op;
	addr_op.result = this_addr;
	addr_op.operand = toTypedValue(operandIrResult);
	addr_op.operand.pointer_depth = PointerDepth{};
	ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));

	TypedValue this_arg;
	this_arg.type = operandType;
	this_arg.ir_type = toIrType(operandType);
	this_arg.size_in_bits = SizeInBits{64};
	this_arg.value = this_addr;
	call_op.args.push_back(this_arg);

	// For postfix operators, pass dummy int argument (value 0)
	// For postfix operators, pass dummy int argument (value 0)
	// Use the matched function's actual parameter count (not the call form) to decide,
	// since the fallback path may match a prefix function for a postfix call or vice versa.
	if (actual_params.size() == 1) {
		TypedValue dummy_arg;
		dummy_arg.type = Type::Int;
		dummy_arg.ir_type = IrType::Integer;
		dummy_arg.size_in_bits = SizeInBits{32};
		dummy_arg.value = 0ULL;
		call_op.args.push_back(dummy_arg);
	}

	int result_size = call_op.return_size_in_bits.value;
	TypeIndex result_type_index = call_op.return_type_index;
	Type result_type = call_op.return_type;
	ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), Token()));
	return makeExprResult(result_type, SizeInBits{static_cast<int>(result_size)}, ret_var, result_type_index);
}



// Helper: generate built-in pointer or integer increment/decrement IR.
// Handles pointer arithmetic (add/subtract element_size) and integer pre/post inc/dec.
// is_increment: true for ++, false for --
ExprResult AstToIr::generateBuiltinIncDec(
	bool is_increment,
	bool is_prefix,
	bool operandHandledAsIdentifier,
	const UnaryOperatorNode& unaryOperatorNode,
	const ExprResult& operandIrResult,
	Type operandType,
	TempVar result_var
) {
	auto getOperandPointerDepth = [&]() -> int {
		return operandIrResult.pointer_depth.value;
	};

	int operand_pointer_depth = getOperandPointerDepth();
	auto makeUpdatedPointerResult = [&](TempVar value_var) {
		return makeExprResult(
			operandType,
			SizeInBits{64},
			value_var,
			operandIrResult.type_index,
			PointerDepth{operand_pointer_depth}
		);
	};

	auto populateIncDecTypedValueMetadata = [&](TypedValue& typed_value) {
		if (carriesSemanticTypeIndex(typed_value.type) && operandIrResult.type_index.is_valid()) {
			typed_value.type_index = operandIrResult.type_index;
		}
		if (operand_pointer_depth > 0) {
			typed_value.pointer_depth = PointerDepth{operand_pointer_depth};
			typed_value.size_in_bits = SizeInBits{64};
		}
	};

	auto storeBackUpdatedValue = [&](const ExprResult& rhs_operands) -> bool {
		if (std::holds_alternative<StringHandle>(operandIrResult.value)) {
			AssignmentOp assign_op;
			auto lhs_value = std::get<StringHandle>(operandIrResult.value);
			assign_op.result = lhs_value;
			assign_op.lhs = makeTypedValue(operandIrResult.type, operandIrResult.size_in_bits, lhs_value);
			populateIncDecTypedValueMetadata(assign_op.lhs);
			assign_op.rhs = toTypedValue(rhs_operands);
			populateIncDecTypedValueMetadata(assign_op.rhs);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), unaryOperatorNode.get_token()));
			return true;
		}
		if (std::holds_alternative<TempVar>(operandIrResult.value)) {
			if (handleLValueAssignment(operandIrResult, rhs_operands, unaryOperatorNode.get_token())) {
				return true;
			}
			AssignmentOp assign_op;
			auto lhs_value = std::get<TempVar>(operandIrResult.value);
			assign_op.result = lhs_value;
			assign_op.lhs = makeTypedValue(operandIrResult.type, operandIrResult.size_in_bits, lhs_value);
			populateIncDecTypedValueMetadata(assign_op.lhs);
			assign_op.rhs = toTypedValue(rhs_operands);
			populateIncDecTypedValueMetadata(assign_op.rhs);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), unaryOperatorNode.get_token()));
			return true;
		}
		return false;
	};

	// Check if this is a pointer increment/decrement (requires pointer arithmetic)
	bool is_pointer = false;
	int element_size = 1;
	if (operand_pointer_depth > 0) {
		is_pointer = true;
		element_size = getPointerElementSize(operandType, operandIrResult.type_index, operand_pointer_depth);
	}
	if (!is_pointer && std::holds_alternative<TempVar>(operandIrResult.value)) {
		TempVar operand_temp = std::get<TempVar>(operandIrResult.value);
		if (auto lvalue_info = getTempVarLValueInfo(operand_temp);
			lvalue_info.has_value() && lvalue_info->kind == LValueInfo::Kind::Member &&
			lvalue_info->member_name.has_value() && current_struct_name_.isValid()) {
			auto type_it = gTypesByName.find(current_struct_name_);
			if (type_it != gTypesByName.end() && type_it->second && type_it->second->getStructInfo()) {
				if (auto member_result = FlashCpp::gLazyMemberResolver.resolve(
					TypeIndex{type_it->second->type_index_},
					lvalue_info->member_name.value());
					member_result && member_result.member->pointer_depth > 0) {
					is_pointer = true;
					operand_pointer_depth = member_result.member->pointer_depth;
					element_size = getPointerElementSize(member_result.member->type, member_result.member->type_index, operand_pointer_depth);
				}
			}
		}
	}
	auto applyPointerTypeInfo = [&](const TypeSpecifierNode& type_node, size_t consumed_dereferences = 0) {
		if (type_node.pointer_depth() <= consumed_dereferences) {
			return;
		}
		size_t remaining_pointer_depth = type_node.pointer_depth() - consumed_dereferences;
		is_pointer = true;
		operand_pointer_depth = static_cast<int>(remaining_pointer_depth);
		if (remaining_pointer_depth > 1) {
			element_size = 8;  // Multi-level pointer: element is a pointer
		} else {
			element_size = getSizeInBytes(type_node.type(), type_node.type_index(), get_type_size_bits(type_node.type()));
			if (element_size == 0) {
				element_size = 1;
			}
		}
	};
	auto tryApplyPointerInfoFromIdentifier = [&](const IdentifierNode& identifier, size_t consumed_dereferences = 0) -> bool {
		auto tryApplyCurrentStructMemberPointerInfo = [&](StringHandle member_name) -> bool {
			if (!current_struct_name_.isValid()) {
				return false;
			}
			auto type_it = gTypesByName.find(current_struct_name_);
			if (type_it == gTypesByName.end() || !type_it->second || !type_it->second->getStructInfo()) {
				return false;
			}
			auto member_result = FlashCpp::gLazyMemberResolver.resolve(
				TypeIndex{type_it->second->type_index_},
				member_name);
			if (!member_result || member_result.member->pointer_depth <= 0) {
				return false;
			}
			int resolved_pointer_depth = member_result.member->pointer_depth - static_cast<int>(consumed_dereferences);
			if (resolved_pointer_depth <= 0) {
				return false;
			}
			is_pointer = true;
			operand_pointer_depth = resolved_pointer_depth;
			element_size = getPointerElementSize(member_result.member->type, member_result.member->type_index, operand_pointer_depth);
			return true;
		};

		if (identifier.binding() == IdentifierBinding::NonStaticMember &&
			tryApplyCurrentStructMemberPointerInfo(identifier.nameHandle())) {
			return true;
		}

		auto symbol = symbol_table.lookup(identifier.name());
		if (!symbol.has_value()) {
			return tryApplyCurrentStructMemberPointerInfo(identifier.nameHandle());
		}
		if (const DeclarationNode* decl = get_decl_from_symbol(*symbol)) {
			applyPointerTypeInfo(decl->type_node().as<TypeSpecifierNode>(), consumed_dereferences);
			if (!is_pointer && !operand_pointer_depth) {
				return tryApplyCurrentStructMemberPointerInfo(identifier.nameHandle());
			}
			return true;
		}
		return tryApplyCurrentStructMemberPointerInfo(identifier.nameHandle());
	};
	if (unaryOperatorNode.get_operand().is<IdentifierNode>()) {
		tryApplyPointerInfoFromIdentifier(unaryOperatorNode.get_operand().as<IdentifierNode>());
	}
	if (unaryOperatorNode.get_operand().is<ExpressionNode>()) {
		const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
		if (parser_ && operand_pointer_depth == 0) {
			if (auto operand_type_opt = parser_->get_expression_type(unaryOperatorNode.get_operand()); operand_type_opt.has_value()) {
				applyPointerTypeInfo(*operand_type_opt);
			}
		}
		if (operandHandledAsIdentifier && std::holds_alternative<IdentifierNode>(operandExpr)) {
			tryApplyPointerInfoFromIdentifier(std::get<IdentifierNode>(operandExpr));
		} else if (std::holds_alternative<MemberAccessNode>(operandExpr)) {
			const auto& member_access = std::get<MemberAccessNode>(operandExpr);
			std::optional<TypeSpecifierNode> object_type_opt;
			if (member_access.object().is<ExpressionNode>()) {
				const ExpressionNode& object_expr = member_access.object().as<ExpressionNode>();
				if (const auto* identifier = std::get_if<IdentifierNode>(&object_expr)) {
					if (identifier->name() == "this" && current_struct_name_.isValid()) {
						auto type_it = gTypesByName.find(current_struct_name_);
						if (type_it != gTypesByName.end() && type_it->second) {
							const TypeInfo& type_info = *type_it->second;
							object_type_opt = TypeSpecifierNode(Type::Struct, type_info.type_index_, type_info.type_size_ * 8);
						}
					} else if (auto symbol = symbol_table.lookup(identifier->name()); symbol.has_value()) {
						if (const DeclarationNode* decl = get_decl_from_symbol(*symbol)) {
							object_type_opt = decl->type_node().as<TypeSpecifierNode>();
						}
					}
				}
			}
			if (!object_type_opt.has_value() && parser_) {
				object_type_opt = parser_->get_expression_type(member_access.object());
			}
			if (object_type_opt.has_value() &&
				(object_type_opt->type() == Type::Struct || object_type_opt->type() == Type::UserDefined) &&
				object_type_opt->type_index().is_valid() && object_type_opt->type_index().value < gTypeInfo.size()) {
				auto member_result = FlashCpp::gLazyMemberResolver.resolve(
					object_type_opt->type_index(),
					StringTable::getOrInternStringHandle(member_access.member_name()));
				if (member_result && member_result.member->pointer_depth > 0) {
					is_pointer = true;
					operand_pointer_depth = member_result.member->pointer_depth;
					element_size = getPointerElementSize(member_result.member->type, member_result.member->type_index, operand_pointer_depth);
				}
			}
		} else if (!operandHandledAsIdentifier && std::holds_alternative<UnaryOperatorNode>(operandExpr)) {
			const UnaryOperatorNode& deref_expr = std::get<UnaryOperatorNode>(operandExpr);
			if (deref_expr.op() == "*" && deref_expr.get_operand().is<ExpressionNode>()) {
				const ExpressionNode& inner_expr = deref_expr.get_operand().as<ExpressionNode>();
				if (const auto* identifier_ptr = std::get_if<IdentifierNode>(&inner_expr)) {
					tryApplyPointerInfoFromIdentifier(*identifier_ptr, 1);
				}
			}
		}
	}

	UnaryOp unary_op{
		.value = toTypedValue(operandIrResult),
		.result = result_var
	};

	IrOpcode arith_opcode = is_increment ? IrOpcode::Add : IrOpcode::Subtract;

	if (is_pointer) {
		// For pointers, use a BinaryOp to add/subtract element_size
		// Extract the pointer operand value once (used in multiple BinaryOp/AssignmentOp below)
		IrValue ptr_operand = toIrValue(operandIrResult.value);

		if (is_prefix) {
			BinaryOp bin_op{
				.lhs = { Type::UnsignedLongLong, SizeInBits{64}, ptr_operand },
				.rhs = { Type::Int, SizeInBits{32}, static_cast<unsigned long long>(element_size) },
				.result = result_var,
			};
			ir_.addInstruction(IrInstruction(arith_opcode, std::move(bin_op), unaryOperatorNode.get_token()));
			ExprResult rhs_operands = makeUpdatedPointerResult(result_var);
			if (!storeBackUpdatedValue(rhs_operands)) {
				FLASH_LOG(Codegen, Error, "Failed to store back pointer increment/decrement result");
				return ExprResult{};
			}
			return makeExprResult(
				operandType,
				SizeInBits{64},
				IrOperand{result_var},
				TypeIndex{},
				PointerDepth{operand_pointer_depth}
			);
		} else {
			// Postfix: save old value, modify, return old value
			TempVar old_value = var_counter.next();
			AssignmentOp save_op;
			save_op.result = old_value;
			save_op.lhs = { Type::UnsignedLongLong, SizeInBits{64}, old_value };
			save_op.rhs = toTypedValue(operandIrResult);
			populateIncDecTypedValueMetadata(save_op.rhs);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(save_op), unaryOperatorNode.get_token()));
			BinaryOp bin_op{
				.lhs = { Type::UnsignedLongLong, SizeInBits{64}, ptr_operand },
				.rhs = { Type::Int, SizeInBits{32}, static_cast<unsigned long long>(element_size) },
				.result = result_var,
			};
			ir_.addInstruction(IrInstruction(arith_opcode, std::move(bin_op), unaryOperatorNode.get_token()));
			ExprResult rhs_operands = makeUpdatedPointerResult(result_var);
			if (!storeBackUpdatedValue(rhs_operands)) {
				FLASH_LOG(Codegen, Error, "Failed to store back pointer postfix increment/decrement result");
				return ExprResult{};
			}
			return makeExprResult(
				operandType,
				SizeInBits{64},
				IrOperand{old_value},
				TypeIndex{},
				PointerDepth{operand_pointer_depth}
			);
		}
	} else {
		// Regular integer increment/decrement
		IrOpcode pre_opcode = is_increment ? IrOpcode::PreIncrement : IrOpcode::PreDecrement;
		IrOpcode post_opcode = is_increment ? IrOpcode::PostIncrement : IrOpcode::PostDecrement;

		// Check if the operand is a global/static variable that needs GlobalStore.
		GlobalStaticBindingInfo gsi;
		if (unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (const auto* identifier = std::get_if<IdentifierNode>(&operandExpr)) {
				gsi = resolveGlobalOrStaticBinding(*identifier);
			}
		}

		if (gsi.is_global_or_static) {
			// For global/static: manually do load → add/sub 1 → store
			IrOpcode arith_opcode_int = is_increment ? IrOpcode::Add : IrOpcode::Subtract;
			Type elem_type = operandIrResult.type;
			int elem_size = operandIrResult.size_in_bits.value;
			IrValue loaded_val = toIrValue(operandIrResult.value);

			if (is_prefix) {
				BinaryOp bin_op{
					.lhs = {elem_type, SizeInBits{elem_size}, loaded_val},
					.rhs = {Type::Int, SizeInBits{32}, 1ULL},
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(arith_opcode_int, std::move(bin_op), unaryOperatorNode.get_token()));
				std::vector<IrOperand> store_ops;
				store_ops.emplace_back(gsi.store_name);
				store_ops.emplace_back(result_var);
				ir_.addInstruction(IrOpcode::GlobalStore, std::move(store_ops), Token());
			} else {
				// Postfix: save old value, compute new, store, return old
				TempVar old_val = var_counter.next();
				AssignmentOp save_op;
				save_op.result = old_val;
				save_op.lhs = {elem_type, SizeInBits{elem_size}, old_val};
				save_op.rhs = {elem_type, SizeInBits{elem_size}, loaded_val};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(save_op), unaryOperatorNode.get_token()));

				BinaryOp bin_op{
					.lhs = {elem_type, SizeInBits{elem_size}, loaded_val},
					.rhs = {Type::Int, SizeInBits{32}, 1ULL},
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(arith_opcode_int, std::move(bin_op), unaryOperatorNode.get_token()));
				std::vector<IrOperand> store_ops;
				store_ops.emplace_back(gsi.store_name);
				store_ops.emplace_back(result_var);
				ir_.addInstruction(IrOpcode::GlobalStore, std::move(store_ops), Token());
				return makeExprResult(operandType, SizeInBits{static_cast<int>(elem_size)}, IrOperand{old_val});
			}
		} else {
			ir_.addInstruction(IrInstruction(is_prefix ? pre_opcode : post_opcode, unary_op, unaryOperatorNode.get_token()));
		}
	}

	return makeExprResult(operandType, operandIrResult.size_in_bits, IrOperand{result_var});
}



// Helper function to evaluate whether an expression is noexcept
// Returns true if the expression is guaranteed not to throw, false otherwise
bool AstToIr::isExpressionNoexcept(const ExpressionNode& expr) const {
	auto isFunctionDeclNoexcept = [&](const FunctionDeclarationNode& func_decl) -> bool {
		if (!func_decl.is_noexcept()) {
			return false;
		}

		if (!func_decl.has_noexcept_expression()) {
			return true;
		}

		ConstExpr::EvaluationContext ctx(symbol_table);
		if (global_symbol_table_) {
			ctx.global_symbols = global_symbol_table_;
		}
		ctx.parser = parser_;

		auto eval_result = ConstExpr::Evaluator::evaluate(*func_decl.noexcept_expression(), ctx);
		return eval_result.success() && eval_result.as_bool();
	};

	// Literals are always noexcept
	if (std::holds_alternative<BoolLiteralNode>(expr) ||
	std::holds_alternative<NumericLiteralNode>(expr) ||
	std::holds_alternative<StringLiteralNode>(expr)) {
		return true;
	}

	// Identifiers (variable references) are noexcept
	if (std::holds_alternative<IdentifierNode>(expr) ||
	std::holds_alternative<QualifiedIdentifierNode>(expr)) {
		return true;
	}

	// Template parameter references are noexcept
	if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
		return true;
	}

	// Built-in operators on primitives are noexcept
	if (std::holds_alternative<BinaryOperatorNode>(expr)) {
		const auto& binop = std::get<BinaryOperatorNode>(expr);
		// Recursively check operands
		if (binop.get_lhs().is<ExpressionNode>() && binop.get_rhs().is<ExpressionNode>()) {
			return isExpressionNoexcept(binop.get_lhs().as<ExpressionNode>()) &&
			isExpressionNoexcept(binop.get_rhs().as<ExpressionNode>());
		}
		// If operands are not expressions, assume noexcept for built-ins
		return true;
	}

	if (const auto* unop = std::get_if<UnaryOperatorNode>(&expr)) {
		if (unop->get_operand().is<ExpressionNode>()) {
			return isExpressionNoexcept(unop->get_operand().as<ExpressionNode>());
		}
		return true;
	}

	// Ternary operator: check all three sub-expressions
	if (std::holds_alternative<TernaryOperatorNode>(expr)) {
		const auto& ternary = std::get<TernaryOperatorNode>(expr);
		bool cond_noexcept = true, then_noexcept = true, else_noexcept = true;
		if (ternary.condition().is<ExpressionNode>()) {
			cond_noexcept = isExpressionNoexcept(ternary.condition().as<ExpressionNode>());
		}
		if (ternary.true_expr().is<ExpressionNode>()) {
			then_noexcept = isExpressionNoexcept(ternary.true_expr().as<ExpressionNode>());
		}
		if (ternary.false_expr().is<ExpressionNode>()) {
			else_noexcept = isExpressionNoexcept(ternary.false_expr().as<ExpressionNode>());
		}
		return cond_noexcept && then_noexcept && else_noexcept;
	}

	// Function calls: check if function is declared noexcept
	if (std::holds_alternative<FunctionCallNode>(expr)) {
		const auto& func_call = std::get<FunctionCallNode>(expr);
		// Check if function_declaration is available and noexcept
		// The FunctionCallNode contains a reference to the function's DeclarationNode
		// We need to look up the FunctionDeclarationNode to check noexcept
		const DeclarationNode& decl = func_call.function_declaration();
		std::string_view func_name = decl.identifier_token().value();

		// Look up the function in the symbol table
		extern SymbolTable gSymbolTable;
		auto symbol = gSymbolTable.lookup(StringTable::getOrInternStringHandle(func_name));
		if (symbol.has_value() && symbol->is<FunctionDeclarationNode>()) {
			const FunctionDeclarationNode& func_decl = symbol->as<FunctionDeclarationNode>();
			return isFunctionDeclNoexcept(func_decl);
		}
		// If we can't determine, conservatively assume it may throw
		return false;
	}

	// Member function calls: check if method is declared noexcept
	if (const auto* member_call = std::get_if<MemberFunctionCallNode>(&expr)) {
		const FunctionDeclarationNode& func_decl = member_call->function_declaration();
		return isFunctionDeclNoexcept(func_decl);
	}

	// Constructor calls: check if constructor is noexcept
	if (std::holds_alternative<ConstructorCallNode>(expr)) {
		// For now, conservatively assume constructors may throw
		// A complete implementation would check the constructor declaration
		return false;
	}

	// Array subscript: noexcept if index expression is noexcept
	if (const auto* subscript = std::get_if<ArraySubscriptNode>(&expr)) {
		if (subscript->index_expr().is<ExpressionNode>()) {
			return isExpressionNoexcept(subscript->index_expr().as<ExpressionNode>());
		}
		return true;
	}

	// Member access is noexcept
	if (std::holds_alternative<MemberAccessNode>(expr)) {
		return true;
	}

	// sizeof, alignof, offsetof are always noexcept
	if (std::holds_alternative<SizeofExprNode>(expr) ||
	std::holds_alternative<SizeofPackNode>(expr) ||
	std::holds_alternative<AlignofExprNode>(expr) ||
	std::holds_alternative<OffsetofExprNode>(expr)) {
		return true;
	}

	// Type traits are noexcept
	if (std::holds_alternative<TypeTraitExprNode>(expr)) {
		return true;
	}

	// new/delete can throw (unless using nothrow variant)
	if (std::holds_alternative<NewExpressionNode>(expr) ||
	std::holds_alternative<DeleteExpressionNode>(expr)) {
		return false;
	}

	// Cast expressions: check the operand
	if (const auto* cast = std::get_if<StaticCastNode>(&expr)) {
		if (cast->expr().is<ExpressionNode>()) {
			return isExpressionNoexcept(cast->expr().as<ExpressionNode>());
		}
		return true;
	}
	if (std::holds_alternative<DynamicCastNode>(expr)) {
		// dynamic_cast can throw std::bad_cast
		return false;
	}
	if (const auto* cast = std::get_if<ConstCastNode>(&expr)) {
		if (cast->expr().is<ExpressionNode>()) {
			return isExpressionNoexcept(cast->expr().as<ExpressionNode>());
		}
		return true;
	}
	if (const auto* cast = std::get_if<ReinterpretCastNode>(&expr)) {
		if (cast->expr().is<ExpressionNode>()) {
			return isExpressionNoexcept(cast->expr().as<ExpressionNode>());
		}
		return true;
	}

	// typeid can throw for dereferencing null polymorphic pointers
	if (std::holds_alternative<TypeidNode>(expr)) {
		return false;
	}

	// Lambda expressions themselves are noexcept (creating the closure)
	if (std::holds_alternative<LambdaExpressionNode>(expr)) {
		return true;
	}

	// Fold expressions: would need to check all sub-expressions
	if (std::holds_alternative<FoldExpressionNode>(expr)) {
		// Conservatively assume may throw
		return false;
	}

	// Pseudo-destructor calls: noexcept iff the type's destructor is noexcept.
	if (const auto* pseudo_dtor = std::get_if<PseudoDestructorCallNode>(&expr)) {
		return isPseudoDestructorCallNoexcept(*pseudo_dtor, symbol_table);
	}

	// Nested noexcept expression
	if (std::holds_alternative<NoexceptExprNode>(expr)) {
		// noexcept(noexcept(x)) - the outer noexcept doesn't evaluate its operand
		return true;
	}

	// Default: conservatively assume may throw
	return false;
}

	Type AstToIr::getSemaAnnotatedTargetType(const ASTNode& node) const {
		if (!sema_ || !node.is<ExpressionNode>()) return Type::Invalid;
		const void* key = &node.as<ExpressionNode>();
		const auto slot = sema_->getSlot(key);
		if (!slot.has_value() || !slot->has_cast()) return Type::Invalid;
		const ImplicitCastInfo& ci = sema_->castInfoTable()[slot->cast_info_index.value - 1];
		const Type from_t = sema_->typeContext().get(ci.source_type_id).base_type;
		const Type to_t = sema_->typeContext().get(ci.target_type_id).base_type;
		if (from_t == Type::Struct || to_t == Type::Struct) return Type::Invalid;
		return to_t;
	}

	ExprResult AstToIr::applyConditionBoolConversion(ExprResult condition, const ASTNode& cond_node, const Token& source_token) {
		// C++20 [conv.bool]: convert condition to bool for control-flow statements.
		//
		// Integer types: the backend's TEST instruction already implements the
		// correct "nonzero → true, zero → false" semantics.  No conversion needed.
		//
		// Floating-point types: we emit a FloatNotEqual comparison against 0.0.
		// This correctly handles:
		//   - -0.0: UCOMISD/UCOMISS treats -0.0 == +0.0, so SETNE → 0 (false). ✓
		//   - Fractional values (e.g. 0.5): 0.5 != 0.0 → SETNE → 1 (true). ✓
		//   - NaN: unordered comparison → PF=1, SETNE → 1 (truthy per C++20). ✓
		// The previous FloatToInt (cvttsd2si) truncation was wrong because it
		// mapped fractional values like 0.5 to integer 0 (false).

		auto emitFloatNonZeroTest = [&](ExprResult cond) -> ExprResult {
			// Materialize a 0.0 constant with the same float type as the condition.
			// The caller guarantees cond.type is Float or Double.
			ExprResult zero = makeExprResult(cond.type, cond.size_in_bits, IrOperand{0.0});
			// Emit: result = (cond != 0.0)
			TempVar result_var = var_counter.next();
			BinaryOp bin_op{
				.lhs = toTypedValue(cond),
				.rhs = toTypedValue(zero),
				.result = result_var,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::FloatNotEqual, std::move(bin_op), source_token));
			// FloatNotEqual produces a bool8 result via SETNE; the backend's
			// conditional branch already handles bool8 values correctly.
			return makeExprResult(Type::Bool, SizeInBits{8}, IrOperand{result_var});
		};

		// 1. Try sema annotation (Phase 6 contextual bool).
		if (sema_ && cond_node.is<ExpressionNode>()) {
			const void* key = &cond_node.as<ExpressionNode>();
			const auto slot = sema_->getSlot(key);
			if (slot.has_value() && slot->has_cast()) {
				const ImplicitCastInfo& cast_info =
					sema_->castInfoTable()[slot->cast_info_index.value - 1];
				const Type from_type = sema_->typeContext().get(cast_info.source_type_id).base_type;
				// Only emit an explicit conversion for float/double → bool.
				// Integer → bool is already handled correctly by the backend (TEST).
				if (is_floating_point_type(from_type)) {
					return emitFloatNonZeroTest(condition);
				}
			}
		}
		// 2. Fallback: floating-point conditions need explicit conversion because
		//    the backend's TEST instruction operates on integer bit patterns and
		//    would mishandle -0.0, which has nonzero bits but is semantically false.
		if (is_floating_point_type(condition.type)) {
			return emitFloatNonZeroTest(condition);
		}
		return condition;
	}
