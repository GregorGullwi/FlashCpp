#include "CodeGen.h"

	std::vector<IrOperand> AstToIr::generateTernaryOperatorIr(const TernaryOperatorNode& ternaryNode) {
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

	std::vector<IrOperand> AstToIr::generateBinaryOperatorIr(const BinaryOperatorNode& binaryOperatorNode) {
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
			// Assignment operators (=) are handled separately above; compound assignments (+=, etc.)
			// fall through here when the LHS is a struct with user-defined operator overloads
			static const std::unordered_set<std::string_view> overloadable_binary_ops = {
				"+", "-", "*", "/", "%",           // Arithmetic
				"==", "!=", "<", ">", "<=", ">=",  // Comparison
				"&&", "||",                        // Logical
				"&", "|", "^",                     // Bitwise
				"<<", ">>",                        // Shift
				",",                               // Comma (already handled above)
				"<=>",                             // Spaceship (handled below)
				// Compound assignment operators (dispatched as member function calls for structs)
				"+=", "-=", "*=", "/=", "%=",
				"&=", "|=", "^=", "<<=", ">>=",
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
									TypeSpecifierNode param_type = param_decl.type_node().as<TypeSpecifierNode>();
									resolveSelfReferentialType(param_type, lhs_type_index);
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
				throw InternalError("Assignment LHS cannot be an immediate value");
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
					throw InternalError("Unsupported float operator");
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
					throw InternalError("Unsupported float comparison operator");
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
				throw InternalError("Unsupported floating-point binary operator");
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

	std::string_view AstToIr::generateMangledNameForCall(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& param_types, bool is_variadic, std::string_view struct_name, const std::vector<std::string>& namespace_path) {
		return NameMangling::generateMangledName(name, return_type, param_types, is_variadic, struct_name, namespace_path).view();
	}

	std::string_view AstToIr::generateMangledNameForCall(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<ASTNode>& param_nodes, bool is_variadic, std::string_view struct_name, const std::vector<std::string>& namespace_path) {
		return NameMangling::generateMangledName(name, return_type, param_nodes, is_variadic, struct_name, namespace_path).view();
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
			auto struct_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
			if (struct_it != gTypesByName.end()) {
				TypeIndex struct_type_index = struct_it->second->type_index_;
				bool needs_resolution = false;
				// Check return type for self-referential struct
				if (return_type.type() == Type::Struct && return_type.type_index() > 0 && return_type.type_index() < gTypeInfo.size()) {
					auto& rti = gTypeInfo[return_type.type_index()];
					if (!rti.struct_info_ || rti.struct_info_->total_size == 0) {
						needs_resolution = true;
					}
				}
				if (!needs_resolution) {
					for (const auto& param : func_node.parameter_nodes()) {
						if (param.is<DeclarationNode>()) {
							const auto& pt = param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
							if (pt.type() == Type::Struct && pt.type_index() > 0 && pt.type_index() < gTypeInfo.size()) {
								auto& ti = gTypeInfo[pt.type_index()];
								if (!ti.struct_info_ || ti.struct_info_->total_size == 0) {
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
						func_node.is_variadic(), struct_name, namespace_path, func_node.linkage()).view();
				}
			}
		}
		
		// Pass linkage from the function node to ensure extern "C" functions aren't mangled
		return NameMangling::generateMangledName(func_name, return_type, func_node.parameter_nodes(),
			func_node.is_variadic(), struct_name, namespace_path, func_node.linkage()).view();
	}

	std::optional<std::vector<IrOperand>> AstToIr::tryGenerateIntrinsicIr(std::string_view func_name, const FunctionCallNode& functionCallNode) {
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

	std::vector<IrOperand> AstToIr::generateBuiltinAbsIntIntrinsic(const FunctionCallNode& functionCallNode) {
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

	std::vector<IrOperand> AstToIr::generateBuiltinAbsFloatIntrinsic(const FunctionCallNode& functionCallNode, std::string_view func_name) {
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

	bool AstToIr::isVaListPointerType(const ASTNode& arg, const std::vector<IrOperand>& ir_result) const {
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

	std::vector<IrOperand> AstToIr::generateVaArgIntrinsic(const FunctionCallNode& functionCallNode) {
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

	std::vector<IrOperand> AstToIr::generateVaStartIntrinsic(const FunctionCallNode& functionCallNode) {
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
			TempVar va_list_struct_addr = emitAddressOf(Type::Char, 8, IrValue(StringTable::getOrInternStringHandle("__varargs_va_list_struct__"sv)), functionCallNode.called_from());
			
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
				TempVar va_struct_addr = emitAddressOf(Type::Char, 8, IrValue(StringTable::getOrInternStringHandle("__varargs_va_list_struct__"sv)), functionCallNode.called_from());
				
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

	std::vector<IrOperand> AstToIr::generateBuiltinUnreachableIntrinsic(const FunctionCallNode& functionCallNode) {
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

	std::vector<IrOperand> AstToIr::generateBuiltinAssumeIntrinsic(const FunctionCallNode& functionCallNode) {
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

	std::vector<IrOperand> AstToIr::generateBuiltinExpectIntrinsic(const FunctionCallNode& functionCallNode) {
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

	std::vector<IrOperand> AstToIr::generateBuiltinLaunderIntrinsic(const FunctionCallNode& functionCallNode) {
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

	std::vector<IrOperand> AstToIr::generateGetExceptionCodeIntrinsic(const FunctionCallNode& functionCallNode) {
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

	std::vector<IrOperand> AstToIr::generateAbnormalTerminationIntrinsic(const FunctionCallNode& functionCallNode) {
		TempVar result = var_counter.next();
		SehAbnormalTerminationOp op;
		op.result = result;
		ir_.addInstruction(IrInstruction(IrOpcode::SehAbnormalTermination, std::move(op), functionCallNode.called_from()));
		return {Type::Int, 32, result, 0ULL};
	}

	std::vector<IrOperand> AstToIr::generateGetExceptionInformationIntrinsic(const FunctionCallNode& functionCallNode) {
		TempVar result = var_counter.next();
		SehExceptionIntrinsicOp op;
		op.result = result;
		ir_.addInstruction(IrInstruction(IrOpcode::SehGetExceptionInfo, std::move(op), functionCallNode.called_from()));
		return {Type::UnsignedLongLong, 64, result, 0ULL};
	}

