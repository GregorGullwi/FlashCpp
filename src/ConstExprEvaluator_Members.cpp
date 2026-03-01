	static EvalResult evaluate_expression_with_bindings(
		const ASTNode& expr_node,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		if (!expr_node.is<ExpressionNode>()) {
			return EvalResult::error("Not an expression node");
		}
		
		const ExpressionNode& expr = expr_node.as<ExpressionNode>();
		
		// Check if it's an identifier that matches a parameter
		if (std::holds_alternative<IdentifierNode>(expr)) {
			const IdentifierNode& id = std::get<IdentifierNode>(expr);
			std::string_view name = id.name();
			
			// Check if it's a bound parameter
			auto it = bindings.find(name);
			if (it != bindings.end()) {
				return it->second;  // Return the bound value
			}
			
			// Not a parameter, evaluate normally
			return evaluate_identifier(id, context);
		}
		
		// For binary operators, recursively evaluate with bindings
		if (std::holds_alternative<BinaryOperatorNode>(expr)) {
			const auto& bin_op = std::get<BinaryOperatorNode>(expr);
			std::string_view op = bin_op.op();
			
			// Handle assignment operators specially (they modify bindings)
			if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=") {
				// Get the left-hand side variable name
				const ASTNode& lhs = bin_op.get_lhs();
				if (lhs.is<ExpressionNode>()) {
					const ExpressionNode& lhs_expr = lhs.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
						const IdentifierNode& id = std::get<IdentifierNode>(lhs_expr);
						std::string_view var_name = id.name();
						
						// Evaluate the right-hand side
						auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
						if (!rhs_result.success()) return rhs_result;
						
						// Perform the assignment
						if (op == "=") {
							bindings[var_name] = rhs_result;
							return rhs_result;
						} else {
							// Compound assignment - get current value first
							auto it = bindings.find(var_name);
							if (it == bindings.end()) {
								return EvalResult::error("Variable not found for compound assignment: " + std::string(var_name));
							}
							EvalResult current = it->second;
							
							// Apply the operation
							EvalResult new_value;
							if (op == "+=") {
								new_value = apply_binary_op(current, rhs_result, "+");
							} else if (op == "-=") {
								new_value = apply_binary_op(current, rhs_result, "-");
							} else if (op == "*=") {
								new_value = apply_binary_op(current, rhs_result, "*");
							} else if (op == "/=") {
								new_value = apply_binary_op(current, rhs_result, "/");
							} else if (op == "%=") {
								new_value = apply_binary_op(current, rhs_result, "%");
							}
							
							if (!new_value.success()) return new_value;
							bindings[var_name] = new_value;
							return new_value;
						}
					}
				}
				return EvalResult::error("Left-hand side of assignment must be a variable");
			}
			
			// Regular binary operators (non-assignment)
			auto lhs_result = evaluate_expression_with_bindings(bin_op.get_lhs(), bindings, context);
			auto rhs_result = evaluate_expression_with_bindings(bin_op.get_rhs(), bindings, context);
			
			if (!lhs_result.success()) return lhs_result;
			if (!rhs_result.success()) return rhs_result;
			
			return apply_binary_op(lhs_result, rhs_result, bin_op.op());
		}
		
		// Handle unary operators (including ++ and --)
		if (std::holds_alternative<UnaryOperatorNode>(expr)) {
			const auto& unary_op = std::get<UnaryOperatorNode>(expr);
			std::string_view op = unary_op.op();
			
			// Handle increment and decrement operators (they modify bindings)
			if (op == "++" || op == "--") {
				const ASTNode& operand = unary_op.get_operand();
				if (operand.is<ExpressionNode>()) {
					const ExpressionNode& operand_expr = operand.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(operand_expr)) {
						const IdentifierNode& id = std::get<IdentifierNode>(operand_expr);
						std::string_view var_name = id.name();
						
						// Get current value
						auto it = bindings.find(var_name);
						if (it == bindings.end()) {
							return EvalResult::error("Variable not found for increment/decrement: " + std::string(var_name));
						}
						EvalResult current = it->second;
						
						// Calculate new value
						EvalResult one = EvalResult::from_int(1);
						EvalResult new_value;
						if (op == "++") {
							new_value = apply_binary_op(current, one, "+");
						} else {
							new_value = apply_binary_op(current, one, "-");
						}
						
						if (!new_value.success()) return new_value;
						bindings[var_name] = new_value;
						
						// Return old value for postfix, new value for prefix
						if (unary_op.is_prefix()) {
							return new_value;  // Prefix: return new value
						} else {
							return current;  // Postfix: return old value
						}
					}
				}
				return EvalResult::error("Operand of increment/decrement must be a variable");
			}
			
			// Regular unary operators
			auto operand_result = evaluate_expression_with_bindings(unary_op.get_operand(), bindings, context);
			if (!operand_result.success()) return operand_result;
			return apply_unary_op(operand_result, op);
		}
		
		// For ternary operators
		if (std::holds_alternative<TernaryOperatorNode>(expr)) {
			const auto& ternary = std::get<TernaryOperatorNode>(expr);
			auto cond_result = evaluate_expression_with_bindings(ternary.condition(), bindings, context);
			
			if (!cond_result.success()) return cond_result;
			
			if (cond_result.as_bool()) {
				return evaluate_expression_with_bindings(ternary.true_expr(), bindings, context);
			} else {
				return evaluate_expression_with_bindings(ternary.false_expr(), bindings, context);
			}
		}
		
		// For function calls (for recursion)
		if (std::holds_alternative<FunctionCallNode>(expr)) {
			const auto& func_call = std::get<FunctionCallNode>(expr);
			
			// Look up the function
			const DeclarationNode& func_decl_node = func_call.function_declaration();
			std::string_view func_name = func_decl_node.identifier_token().value();
			
			if (!context.symbols) {
				return EvalResult::error("Cannot evaluate function call: no symbol table provided");
			}
			
			auto symbol_opt = context.symbols->lookup(func_name);
			if (!symbol_opt.has_value()) {
				// Try variable template instantiation before giving up
				if (func_call.has_template_arguments() && context.parser) {
					auto var_result = tryEvaluateAsVariableTemplate(func_name, func_call, context);
					if (var_result.success()) return var_result;
				}
				return EvalResult::error("Undefined function in constant expression: " + std::string(func_name));
			}
			
			const ASTNode& symbol_node = symbol_opt.value();
			if (!symbol_node.is<FunctionDeclarationNode>()) {
				// Check if it's a variable template (like __is_ratio_v<T>)
				if (symbol_node.is<TemplateVariableDeclarationNode>()) {
					auto var_result = tryEvaluateAsVariableTemplate(func_name, func_call, context);
					if (var_result.success()) return var_result;
				}
				return EvalResult::error("Identifier is not a function: " + std::string(func_name));
			}
			
			const FunctionDeclarationNode& func_decl = symbol_node.as<FunctionDeclarationNode>();
			
			// Check if it's a constexpr function
			if (!func_decl.is_constexpr()) {
				return EvalResult::error("Function in constant expression must be constexpr: " + std::string(func_name));
			}
			
			// Evaluate the function with bindings passed through
			return evaluate_function_call_with_bindings(func_decl, func_call.arguments(), bindings, context);
		}
		
		// For member access on 'this' (e.g., this->x in a member function)
		// This handles implicit member accesses like 'x' which parser transforms to 'this->x'
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			[[maybe_unused]] const auto& member_access = std::get<MemberAccessNode>(expr);
			// TODO: Handle member access evaluation for 'this->x' pattern
			// For now, fall back to regular evaluation
		}
		
		// For other expression types, use the const version (cast bindings to const)
		const std::unordered_map<std::string_view, EvalResult>& const_bindings = bindings;
		return evaluate_expression_with_bindings_const(expr_node, const_bindings, context);
	}
	
	// Original const version for backward compatibility
	static EvalResult evaluate_expression_with_bindings_const(
		const ASTNode& expr_node,
		const std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		if (!expr_node.is<ExpressionNode>()) {
			return EvalResult::error("Not an expression node");
		}
		
		const ExpressionNode& expr = expr_node.as<ExpressionNode>();
		
		// Check if it's an identifier that matches a parameter
		if (std::holds_alternative<IdentifierNode>(expr)) {
			const IdentifierNode& id = std::get<IdentifierNode>(expr);
			std::string_view name = id.name();
			
			// Check if it's a bound parameter
			auto it = bindings.find(name);
			if (it != bindings.end()) {
				return it->second;  // Return the bound value
			}
			
			// Not a parameter, evaluate normally
			return evaluate_identifier(id, context);
		}
		
		// For binary operators, recursively evaluate with bindings
		if (std::holds_alternative<BinaryOperatorNode>(expr)) {
			const auto& bin_op = std::get<BinaryOperatorNode>(expr);
			auto lhs_result = evaluate_expression_with_bindings_const(bin_op.get_lhs(), bindings, context);
			auto rhs_result = evaluate_expression_with_bindings_const(bin_op.get_rhs(), bindings, context);
			
			if (!lhs_result.success()) return lhs_result;
			if (!rhs_result.success()) return rhs_result;
			
			return apply_binary_op(lhs_result, rhs_result, bin_op.op());
		}
		
		// For ternary operators
		if (std::holds_alternative<TernaryOperatorNode>(expr)) {
			const auto& ternary = std::get<TernaryOperatorNode>(expr);
			auto cond_result = evaluate_expression_with_bindings_const(ternary.condition(), bindings, context);
			
			if (!cond_result.success()) return cond_result;
			
			if (cond_result.as_bool()) {
				return evaluate_expression_with_bindings_const(ternary.true_expr(), bindings, context);
			} else {
				return evaluate_expression_with_bindings_const(ternary.false_expr(), bindings, context);
			}
		}
		
		// For function calls (for recursion)
		if (std::holds_alternative<FunctionCallNode>(expr)) {
			const auto& func_call = std::get<FunctionCallNode>(expr);
			
			// Look up the function
			const DeclarationNode& func_decl_node = func_call.function_declaration();
			std::string_view func_name = func_decl_node.identifier_token().value();
			
			if (!context.symbols) {
				return EvalResult::error("Cannot evaluate function call: no symbol table provided");
			}
			
			auto symbol_opt = context.symbols->lookup(func_name);
			if (!symbol_opt.has_value()) {
				// Try variable template instantiation before giving up
				if (func_call.has_template_arguments() && context.parser) {
					auto var_result = tryEvaluateAsVariableTemplate(func_name, func_call, context);
					if (var_result.success()) return var_result;
				}
				return EvalResult::error("Undefined function in constant expression: " + std::string(func_name));
			}
			
			const ASTNode& symbol_node = symbol_opt.value();
			if (!symbol_node.is<FunctionDeclarationNode>()) {
				// Check if it's a variable template (like __is_ratio_v<T>)
				if (symbol_node.is<TemplateVariableDeclarationNode>()) {
					auto var_result = tryEvaluateAsVariableTemplate(func_name, func_call, context);
					if (var_result.success()) return var_result;
				}
				return EvalResult::error("Identifier is not a function: " + std::string(func_name));
			}
			
			const FunctionDeclarationNode& func_decl = symbol_node.as<FunctionDeclarationNode>();
			
			// Check if it's a constexpr function
			if (!func_decl.is_constexpr()) {
				return EvalResult::error("Function in constant expression must be constexpr: " + std::string(func_name));
			}
			
			// Evaluate the function with bindings passed through
			return evaluate_function_call_with_bindings(func_decl, func_call.arguments(), bindings, context);
		}
		
		// For member access on 'this' (e.g., this->x in a member function)
		// This handles implicit member accesses like 'x' which parser transforms to 'this->x'
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			const auto& member_access = std::get<MemberAccessNode>(expr);
			std::string_view member_name = member_access.member_name();
			
			// Check if the object is 'this' (implicit member access)
			const ASTNode& obj = member_access.object();
			if (obj.is<ExpressionNode>()) {
				const ExpressionNode& obj_expr = obj.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(obj_expr)) {
					const IdentifierNode& obj_id = std::get<IdentifierNode>(obj_expr);
					if (obj_id.name() == "this") {
						// This is an implicit member access - look up in bindings
						auto it = bindings.find(member_name);
						if (it != bindings.end()) {
							return it->second;  // Return the bound member value
						}
						return EvalResult::error("Member not found in constexpr object: " + std::string(member_name));
					}
				}
			}
			// Fall through to normal evaluation for non-this member access
		}
		
		// For array subscript (e.g., arr[i] where arr is a parameter)
		if (std::holds_alternative<ArraySubscriptNode>(expr)) {
			const auto& subscript = std::get<ArraySubscriptNode>(expr);
			
			// Evaluate the index
			auto index_result = evaluate_expression_with_bindings_const(subscript.index_expr(), bindings, context);
			if (!index_result.success()) {
				return index_result;
			}
			
			long long index = index_result.as_int();
			if (index < 0) {
				return EvalResult::error("Negative array index in constant expression");
			}
			
			// Get the array expression
			const ASTNode& array_expr = subscript.array_expr();
			if (array_expr.is<ExpressionNode>()) {
				const ExpressionNode& array_expr_node = array_expr.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(array_expr_node)) {
					std::string_view var_name = std::get<IdentifierNode>(array_expr_node).name();
					
					// Check if it's in bindings (parameter array)
					auto it = bindings.find(var_name);
					if (it != bindings.end()) {
						const EvalResult& array_result = it->second;
						if (array_result.is_array) {
							if (static_cast<size_t>(index) >= array_result.array_values.size()) {
								return EvalResult::error("Array index out of bounds in constant expression");
							}
							return EvalResult::from_int(array_result.array_values[static_cast<size_t>(index)]);
						}
						return EvalResult::error("Subscript on non-array variable in constant expression");
					}
					// Fall through to normal variable lookup
				}
			}
		}
		
		// For literals and other expressions without parameters, evaluate normally
		return evaluate(expr_node, context);
	}

	// Helper functions for overflow-safe arithmetic using compiler builtins
private:
	// Perform addition with overflow checking, return result or nullopt on overflow
	static std::optional<long long> safe_add(long long a, long long b) {
		long long result;
#if defined(_MSC_VER) && !defined(__clang__)
		// MSVC implementation using manual overflow detection
		if ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)) {
			return std::nullopt; // Overflow
		}
		result = a + b;
		bool overflow = false;
#else
		bool overflow = __builtin_add_overflow(a, b, &result);
#endif
		return overflow ? std::nullopt : std::optional<long long>(result);
	}

	// Perform subtraction with overflow checking, return result or nullopt on overflow
	static std::optional<long long> safe_sub(long long a, long long b) {
		long long result;
#if defined(_MSC_VER) && !defined(__clang__)
		// MSVC implementation using manual overflow detection
		if ((b < 0 && a > LLONG_MAX + b) || (b > 0 && a < LLONG_MIN + b)) {
			return std::nullopt; // Overflow
		}
		result = a - b;
		bool overflow = false;
#else
		bool overflow = __builtin_sub_overflow(a, b, &result);
#endif
		return overflow ? std::nullopt : std::optional<long long>(result);
	}

	// Perform multiplication with overflow checking, return result or nullopt on overflow
	static std::optional<long long> safe_mul(long long a, long long b) {
		long long result;
#if defined(_MSC_VER) && !defined(__clang__)
		// MSVC implementation using manual overflow detection
		if (a == 0 || b == 0) {
			result = 0;
		} else if (a == LLONG_MIN || b == LLONG_MIN) {
			// Special case: LLONG_MIN * anything except 0 or 1 overflows
			if ((a == LLONG_MIN && (b < -1 || b > 1)) || (b == LLONG_MIN && (a < -1 || a > 1))) {
				return std::nullopt;
			}
			result = a * b;
		} else if ((a > 0 && b > 0 && a > LLONG_MAX / b) ||
		           (a > 0 && b < 0 && b < LLONG_MIN / a) ||
		           (a < 0 && b > 0 && a < LLONG_MIN / b) ||
		           (a < 0 && b < 0 && a < LLONG_MAX / b)) {
			return std::nullopt; // Overflow
		} else {
			result = a * b;
		}
		bool overflow = false;
#else
		bool overflow = __builtin_mul_overflow(a, b, &result);
#endif
		return overflow ? std::nullopt : std::optional<long long>(result);
	}

	// Perform left shift with validation and overflow checking, return result or nullopt on error
	static std::optional<long long> safe_shl(long long a, long long b) {
		if (b < 0 || b >= 64) {
			return std::nullopt; // Negative shift or shift >= bit width is undefined
		}
		if (a == 0) {
			return 0; // Shifting zero is fine
		}
		
		// Check if the shift would cause bits to be lost
		// For left shift, check if any bits would be shifted out
		long long shifted = a << b;
		long long back_shifted = shifted >> b;
		if (back_shifted != a) {
			return std::nullopt; // Overflow detected
		}
		
		return shifted;
	}

	// Perform right shift with validation, return result or nullopt on error
	static std::optional<long long> safe_shr(long long a, long long b) {
		if (b < 0 || b >= 64) {
			return std::nullopt; // Negative shift or shift >= bit width is undefined
		}
		return a >> b; // Right shift never overflows mathematically
	}

public:
	// Helper to apply binary operators
	static EvalResult apply_binary_op(const EvalResult& lhs, const EvalResult& rhs, std::string_view op) {
		long long lhs_val = lhs.as_int();
		long long rhs_val = rhs.as_int();
		
		// Handle arithmetic operators with overflow checking
		if (op == "+") {
			if (auto result = safe_add(lhs_val, rhs_val)) {
				return EvalResult::from_int(*result);
			} else {
				return EvalResult::error("Signed integer overflow in constant expression");
			}
		} else if (op == "-") {
			if (auto result = safe_sub(lhs_val, rhs_val)) {
				return EvalResult::from_int(*result);
			} else {
				return EvalResult::error("Signed integer overflow in constant expression");
			}
		} else if (op == "*") {
			if (auto result = safe_mul(lhs_val, rhs_val)) {
				return EvalResult::from_int(*result);
			} else {
				return EvalResult::error("Signed integer overflow in constant expression");
			}
		} else if (op == "/") {
			if (rhs_val == 0) {
				return EvalResult::error("Division by zero in constant expression");
			}
			// Check for overflow in division (only happens with LLONG_MIN / -1)
			if (lhs_val == LLONG_MIN && rhs_val == -1) {
				return EvalResult::error("Signed integer overflow in constant expression");
			}
			return EvalResult::from_int(lhs_val / rhs_val);
		} else if (op == "%") {
			if (rhs_val == 0) {
				return EvalResult::error("Modulo by zero in constant expression");
			}
			return EvalResult::from_int(lhs_val % rhs_val);
		}
		
		// Handle bitwise operators
		else if (op == "&") {
			return EvalResult::from_int(lhs_val & rhs_val);
		} else if (op == "|") {
			return EvalResult::from_int(lhs_val | rhs_val);
		} else if (op == "^") {
			return EvalResult::from_int(lhs_val ^ rhs_val);
		} else if (op == "<<") {
			if (auto result = safe_shl(lhs_val, rhs_val)) {
				return EvalResult::from_int(*result);
			} else {
				return EvalResult::error("Left shift overflow or invalid shift count in constant expression");
			}
		} else if (op == ">>") {
			if (auto result = safe_shr(lhs_val, rhs_val)) {
				return EvalResult::from_int(*result);
			} else {
				return EvalResult::error("Invalid shift count in constant expression");
			}
		}
		
		// Handle comparison operators that work on integers
		if (op == "==") {
			// Compare as integers for all types
			return EvalResult::from_bool(lhs.as_int() == rhs.as_int());
		} else if (op == "!=") {
			return EvalResult::from_bool(lhs.as_int() != rhs.as_int());
		} else if (op == "<") {
			return EvalResult::from_bool(lhs.as_int() < rhs.as_int());
		} else if (op == "<=") {
			return EvalResult::from_bool(lhs.as_int() <= rhs.as_int());
		} else if (op == ">") {
			return EvalResult::from_bool(lhs.as_int() > rhs.as_int());
		} else if (op == ">=") {
			return EvalResult::from_bool(lhs.as_int() >= rhs.as_int());
		} else if (op == "&&") {
			return EvalResult::from_bool(lhs.as_bool() && rhs.as_bool());
		} else if (op == "||") {
			return EvalResult::from_bool(lhs.as_bool() || rhs.as_bool());
		}

		// Unsupported operator
		return EvalResult::error("Operator '" + std::string(op) + "' not supported in constant expressions");
	}

	static EvalResult apply_unary_op(const EvalResult& operand, std::string_view op) {
		if (op == "!") {
			return EvalResult::from_bool(!operand.as_bool());
		} else if (op == "~") {
			return EvalResult::from_int(~operand.as_int());
		} else if (op == "-") {
			// Unary minus - negate the value
			if (std::holds_alternative<double>(operand.value)) {
				return EvalResult::from_double(-operand.as_double());
			}
			// Check for overflow: negating LLONG_MIN overflows
			long long val = operand.as_int();
			if (val == LLONG_MIN) {
				return EvalResult::error("Signed integer overflow in unary minus");
			}
			return EvalResult::from_int(-val);
		} else if (op == "+") {
			// Unary plus - no-op, just return the value
			return operand;
		}

		// Unsupported operator
		return EvalResult::error("Unary operator '" + std::string(op) + "' not supported in constant expressions");
	}

	// Evaluate qualified identifier (e.g., Namespace::var or Template<T>::member)
	static EvalResult evaluate_qualified_identifier(const QualifiedIdentifierNode& qualified_id, EvaluationContext& context) {
		// Look up the qualified name in the symbol table
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate qualified identifier: no symbol table provided");
		}

		// Try to look up the qualified name
		auto symbol_opt = context.symbols->lookup_qualified(qualified_id.qualifiedIdentifier());
		if (!symbol_opt.has_value()) {
			// PHASE 3 FIX: If not found in symbol table, try looking up as struct static member
			// This handles cases like is_pointer_impl<int*>::value where value is a static member
			// Also handles type aliases like `using my_true = integral_constant<bool, true>; my_true::value`
			NamespaceHandle ns_handle = qualified_id.namespace_handle();
			StringHandle struct_handle;
			
			if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
				FLASH_LOG(ConstExpr, Debug, "ns_handle.isGlobal()=", ns_handle.isGlobal(),
				          ", qualified_id='", qualified_id.full_name(), "'");
			}
			
			if (!ns_handle.isGlobal()) {
				struct_handle = gNamespaceRegistry.getQualifiedNameHandle(ns_handle);
				if (!struct_handle.isValid()) {
					struct_handle = StringTable::getOrInternStringHandle(gNamespaceRegistry.getName(ns_handle));
				}
			}
			
			// If we still don't have a struct name, derive it from the qualified identifier.
			// Example: "std::is_integral<int>::value" -> "std::is_integral<int>"
			if (!struct_handle.isValid()) {
				std::string_view ns_name = gNamespaceRegistry.getQualifiedName(ns_handle);
				if (!ns_name.empty()) {
					struct_handle = StringTable::getOrInternStringHandle(ns_name);
					if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
						FLASH_LOG(ConstExpr, Debug, "Extracted struct_name='", ns_name, "' from qualified namespace");
					}
				}
			}
			
			if (struct_handle.isValid()) {
				if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
					FLASH_LOG(ConstExpr, Debug, "Looking up struct '", StringTable::getStringView(struct_handle), "' for member '", qualified_id.name(), "'");
				}
				
				// Look up the struct in gTypesByName
				auto struct_type_it = gTypesByName.find(struct_handle);
				
				// If not found with the full qualified name (e.g., "std::is_integral$hash"),
				// try without the namespace prefix (e.g., "is_integral$hash") since template
				// instantiations are often registered with just the short name
				if (struct_type_it == gTypesByName.end()) {
					std::string_view full_name = StringTable::getStringView(struct_handle);
					size_t last_colon = full_name.rfind("::");
					if (last_colon != std::string_view::npos) {
						std::string_view short_name = full_name.substr(last_colon + 2);
						StringHandle short_handle = StringTable::getOrInternStringHandle(short_name);
						struct_type_it = gTypesByName.find(short_handle);
						if (struct_type_it != gTypesByName.end()) {
							FLASH_LOG(ConstExpr, Debug, "Found type using short name '", short_name, "'");
						}
					}
				}
				
				// If not found directly, this might be a type alias
				// Type aliases are registered with their alias name pointing to the underlying type
				const StructTypeInfo* struct_info = nullptr;
				const TypeInfo* resolved_type_info = nullptr;
				
				if (struct_type_it != gTypesByName.end()) {
					const TypeInfo* type_info = struct_type_it->second;
					
					if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
						FLASH_LOG(ConstExpr, Debug, "Found type_info, isStruct=", type_info->isStruct(),
						          ", type_index=", type_info->type_index_, ", hasStructInfo=", (type_info->getStructInfo() != nullptr));
					}
					
					// Follow the type alias chain until we find a struct with actual StructTypeInfo
					// Type aliases may have isStruct()=true but getStructInfo()=null
					// Limit iterations to prevent infinite loops from cycles
					constexpr size_t MAX_ALIAS_CHAIN_DEPTH = 100;
					size_t alias_depth = 0;
					while (type_info && type_info->type_index_ > 0 && type_info->type_index_ < gTypeInfo.size() && alias_depth < MAX_ALIAS_CHAIN_DEPTH) {
						// Check if we already have StructInfo - if so, we're done
						if (type_info->isStruct() && type_info->getStructInfo() != nullptr) {
							break;
						}
						// Follow the type_index_ to find the underlying type
						const TypeInfo& underlying = gTypeInfo[type_info->type_index_];
						if (&underlying == type_info) break;  // Avoid direct self-reference
						if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
							FLASH_LOG(ConstExpr, Debug, "Following type alias to index ", type_info->type_index_);
						}
						type_info = &underlying;
						++alias_depth;
					}
					
					if (type_info && type_info->isStruct()) {
						struct_info = type_info->getStructInfo();
						resolved_type_info = type_info;
					}
				}
				
				// If still not found, try resolving by checking if there's a type alias in gTypeInfo
				// Note: This linear search is a fallback for edge cases; primary lookup uses gTypesByName
				if (!struct_info) {
					// Try looking up by iterating through gTypeInfo to find a type with matching name
					for (const auto& type_info : gTypeInfo) {
						if (type_info.isStruct()) {
							const StructTypeInfo* si = type_info.getStructInfo();
							if (si && si->name == struct_handle) {
								struct_info = si;
								resolved_type_info = &type_info;
								break;
							}
						}
					}
				}
				
				if (struct_info) {
					// Look for static member recursively (checks base classes too)
					StringHandle member_handle = StringTable::getOrInternStringHandle(qualified_id.name());
					if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
						FLASH_LOG(ConstExpr, Debug, "Static lookup in struct '", StringTable::getStringView(struct_handle), "', bases=", struct_info->base_classes.size());
						if (resolved_type_info) {
							FLASH_LOG(ConstExpr, Debug, "Resolved type base template='", StringTable::getStringView(resolved_type_info->baseTemplateName()), "', template args=", resolved_type_info->template_args_.size());
							for (size_t i = 0; i < resolved_type_info->template_args_.size(); ++i) {
								const auto& arg = resolved_type_info->template_args_[i];
								FLASH_LOG(ConstExpr, Debug, "  resolved arg[", i, "] is_value=", arg.is_value, ", base_type=", static_cast<int>(arg.base_type), ", type_index=", arg.type_index, ", value(int)=", arg.intValue());
							}
						}
						for (const auto& base : struct_info->base_classes) {
							if (base.type_index < gTypeInfo.size()) {
								FLASH_LOG(ConstExpr, Debug, "  base type_index=", base.type_index, " name='", StringTable::getStringView(gTypeInfo[base.type_index].name_), "'");
							}
						}
						FLASH_LOG(ConstExpr, Debug, "  static members=", struct_info->static_members.size(), ", non-static members=", struct_info->members.size());
						for (const auto& static_member : struct_info->static_members) {
							FLASH_LOG(ConstExpr, Debug, "    static member name='", StringTable::getStringView(static_member.getName()), "'");
						}
						for (const auto& member : struct_info->members) {
							FLASH_LOG(ConstExpr, Debug, "    member name='", StringTable::getStringView(member.name), "'");
						}
					}
					
					auto traitKindFromTemplateName = [](StringHandle template_name) -> std::optional<TypeTraitKind> {
						std::string_view name = StringTable::getStringView(template_name);
						if (name == "is_void") return TypeTraitKind::IsVoid;
						if (name == "is_null_pointer" || name == "is_nullptr") return TypeTraitKind::IsNullptr;
						if (name == "is_integral") return TypeTraitKind::IsIntegral;
						if (name == "is_floating_point") return TypeTraitKind::IsFloatingPoint;
						if (name == "is_array") return TypeTraitKind::IsArray;
						if (name == "is_pointer") return TypeTraitKind::IsPointer;
						if (name == "is_lvalue_reference") return TypeTraitKind::IsLvalueReference;
						if (name == "is_rvalue_reference") return TypeTraitKind::IsRvalueReference;
						if (name == "is_member_object_pointer") return TypeTraitKind::IsMemberObjectPointer;
						if (name == "is_member_function_pointer") return TypeTraitKind::IsMemberFunctionPointer;
						if (name == "is_enum") return TypeTraitKind::IsEnum;
						if (name == "is_union") return TypeTraitKind::IsUnion;
						if (name == "is_class") return TypeTraitKind::IsClass;
						if (name == "is_function") return TypeTraitKind::IsFunction;
						if (name == "is_reference") return TypeTraitKind::IsReference;
						if (name == "is_arithmetic") return TypeTraitKind::IsArithmetic;
						if (name == "is_fundamental") return TypeTraitKind::IsFundamental;
						if (name == "is_object") return TypeTraitKind::IsObject;
						if (name == "is_scalar") return TypeTraitKind::IsScalar;
						if (name == "is_compound") return TypeTraitKind::IsCompound;
						if (name == "is_const") return TypeTraitKind::IsConst;
						if (name == "is_volatile") return TypeTraitKind::IsVolatile;
						if (name == "is_signed") return TypeTraitKind::IsSigned;
						if (name == "is_unsigned") return TypeTraitKind::IsUnsigned;
						if (name == "is_bounded_array") return TypeTraitKind::IsBoundedArray;
						if (name == "is_unbounded_array") return TypeTraitKind::IsUnboundedArray;
						return std::nullopt;
					};

					struct TraitInput {
						Type base_type;
						TypeIndex type_index;
						size_t pointer_depth;
						ReferenceQualifier ref_qualifier;
						CVQualifier cv;
						bool is_array;
						std::optional<size_t> array_size;
						const TypeInfo* type_info;
						const StructTypeInfo* struct_info;
					};

					auto evaluateTypeTraitFromInput = [](TypeTraitKind trait_kind, const TraitInput& input) {
						return evaluateTypeTrait(trait_kind, input.base_type, input.type_index,
							input.ref_qualifier != ReferenceQualifier::None,
							input.ref_qualifier == ReferenceQualifier::RValueReference,
							input.ref_qualifier == ReferenceQualifier::LValueReference,
							input.pointer_depth, input.cv, input.is_array, input.array_size, input.type_info, input.struct_info);
					};

					auto evaluate_unary_trait_from_resolved = [&](StringHandle trait_template_name) -> std::optional<EvalResult> {
						if (!resolved_type_info || resolved_type_info->template_args_.empty()) {
							return std::nullopt;
						}
						
						auto trait_kind = traitKindFromTemplateName(trait_template_name);
						if (!trait_kind.has_value()) {
							return std::nullopt;
						}
						
						const auto& arg_info = resolved_type_info->template_args_[0];
						TraitInput input{
							.base_type = arg_info.base_type,
							.type_index = arg_info.type_index,
							.pointer_depth = arg_info.pointer_depth ? arg_info.pointer_depth : arg_info.pointer_cv_qualifiers.size(),
							.ref_qualifier = arg_info.ref_qualifier,
							.cv = arg_info.cv_qualifier,
							.is_array = arg_info.is_array,
							.array_size = arg_info.array_size,
							.type_info = nullptr,
							.struct_info = nullptr
						};
						
						if (input.type_index > 0 && input.type_index < gTypeInfo.size()) {
							input.type_info = &gTypeInfo[input.type_index];
							input.base_type = input.type_info->type_;
							input.pointer_depth = input.type_info->pointer_depth_;
							input.ref_qualifier = input.type_info->reference_qualifier_;
							input.struct_info = input.type_info->getStructInfo();
						}
						
						auto trait_result = evaluateTypeTraitFromInput(*trait_kind, input);
						if (trait_result.success) {
							return trait_result.value ? EvalResult::from_bool(true) : EvalResult::from_bool(false);
						}
						
						return std::nullopt;
					};
					auto evaluate_integral_constant_value = [](const TypeInfo& ti) -> std::optional<EvalResult> {
						if (!ti.isTemplateInstantiation()) {
							return std::nullopt;
						}
						
						const auto& args = ti.templateArgs();
						if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
							FLASH_LOG(ConstExpr, Debug, "Integral constant synthesis: template args=", args.size());
							for (size_t i = 0; i < args.size(); ++i) {
								FLASH_LOG(ConstExpr, Debug, "  arg[", i, "] is_value=", args[i].is_value, ", base_type=", static_cast<int>(args[i].base_type), ", type_index=", args[i].type_index, ", value(int)=", args[i].intValue());
							}
						}
						if (args.size() < 2) {
							FLASH_LOG(ConstExpr, Debug, "Integral constant synthesis failed: expected >=2 template args, got ", args.size());
							return std::nullopt;
						}
						
						const auto& value_arg = args[1];
						if (!value_arg.is_value) {
							FLASH_LOG(ConstExpr, Debug, "Integral constant synthesis failed: value arg is not non-type value");
							return std::nullopt;
						}
						
						// Convert the stored value based on the template argument type
						switch (value_arg.base_type) {
							case Type::Bool:
								return EvalResult::from_bool(value_arg.intValue() != 0);
							case Type::UnsignedChar:
							case Type::UnsignedShort:
							case Type::UnsignedInt:
							case Type::UnsignedLong:
							case Type::UnsignedLongLong:
								return EvalResult::from_uint(static_cast<unsigned long long>(value_arg.intValue()));
							default:
								return EvalResult::from_int(value_arg.intValue());
						}
					};
					
					auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_handle);
					
					if (IS_FLASH_LOG_ENABLED(ConstExpr, Debug)) {
						FLASH_LOG(ConstExpr, Debug, "Static member found: ", (static_member != nullptr),
						          ", owner: ", (owner_struct != nullptr));
					}
					
					// Fallback: synthesize integral_constant::value from template arguments when static member isn't registered
					const StringHandle value_handle = StringTable::getOrInternStringHandle("value");
					if (!static_member && member_handle == value_handle) {
						if (resolved_type_info) {
							if (auto trait_value = evaluate_unary_trait_from_resolved(resolved_type_info->baseTemplateName())) {
								FLASH_LOG(ConstExpr, Debug, "Synthesized value from unary trait evaluator for ", StringTable::getStringView(resolved_type_info->baseTemplateName()));
								return *trait_value;
							}
						}
						if (resolved_type_info) {
							if (auto synthesized = evaluate_integral_constant_value(*resolved_type_info)) {
								FLASH_LOG(ConstExpr, Debug, "Synthesized integral_constant value from template args (self)");
								return *synthesized;
							}
						}
						for (const auto& base : struct_info->base_classes) {
							if (base.type_index < gTypeInfo.size()) {
								if (auto synthesized = evaluate_integral_constant_value(gTypeInfo[base.type_index])) {
									FLASH_LOG(ConstExpr, Debug, "Synthesized integral_constant value from base template args");
									return *synthesized;
								}
							}
						}
					}
				
					if (static_member && owner_struct) {
						FLASH_LOG(ConstExpr, Debug, "Static member is_const: ", static_member->is_const(), 
						          ", has_initializer: ", static_member->initializer.has_value());
						
						// If static member has no initializer, try to trigger lazy instantiation
						// Note: context.parser may be null in some evaluation contexts (e.g., standalone constant evaluation)
						// In such cases, lazy instantiation is not possible and we fall through to default value
						if (!static_member->initializer.has_value() && context.parser != nullptr) {
							FLASH_LOG(ConstExpr, Debug, "Triggering lazy instantiation for '", 
							          StringTable::getStringView(owner_struct->name), "::", StringTable::getStringView(member_handle), "'");
							
							// Trigger lazy static member instantiation
							// This fills in the initializer from template substitution
							context.parser->instantiateLazyStaticMember(owner_struct->name, member_handle);
							
							// Re-lookup the static member after instantiation
							auto relookup_result = struct_info->findStaticMemberRecursive(member_handle);
							if (relookup_result.first && relookup_result.first->initializer.has_value()) {
								FLASH_LOG(ConstExpr, Debug, "After lazy instantiation, evaluating initializer");
								return evaluate(relookup_result.first->initializer.value(), context);
							}
						}
						
						// Found a static member - evaluate its initializer if available
						// Note: Even if not marked const, we can evaluate constexpr initializers
						if (static_member->initializer.has_value()) {
							FLASH_LOG(ConstExpr, Debug, "Evaluating static member initializer");
							return evaluate(static_member->initializer.value(), context);
						}
						
						// If not constexpr or no initializer, return default value based on type
						FLASH_LOG(ConstExpr, Debug, "Returning default value for type: ", static_cast<int>(static_member->type));
						if (static_member->type == Type::Bool) {
							return EvalResult::from_bool(false);
						}
						return EvalResult::from_int(0);
					}
				}
			}
			
			// Not found in symbol table or as struct static member
			// Check if this looks like a template instantiation with dependent arguments
			// Pattern: __template_name__DependentArg::member
			// Work with string_view to avoid unnecessary copies
			std::string_view ns_name = gNamespaceRegistry.getQualifiedName(qualified_id.namespace_handle());
			std::string_view member_name = qualified_id.name();
			
			// Check if the namespace part looks like a template instantiation (contains template argument separator)
			// Template names with arguments get mangled as template_name_arg1_arg2...
			// If any argument is a template parameter (starts with _ often), it's template-dependent
			// BUT: Don't treat names like "is_integral_int" as dependent - "int" is a concrete type!
			// Only treat as dependent if it contains identifiers that START with underscore (like _Tp, _Up)
			// or have double underscores (like __type_parameter)
			bool looks_dependent = false;
			if (!ns_name.empty() && ns_name.find('_') != std::string_view::npos) {
				// Check if any component looks like a template parameter (starts with _ or __)
				// Split by underscore and check each part
				for (size_t i = 0; i < ns_name.size(); ) {
					if (ns_name[i] == '_') {
						// Found underscore - check if next char is also underscore or uppercase
						// Template parameters often look like: _Tp, _Up, _T, __type, etc.
						if (i + 1 < ns_name.size()) {
							char next = ns_name[i + 1];
							if (next == '_' || std::isupper(static_cast<unsigned char>(next))) {
								looks_dependent = true;
								break;
							}
						}
					}
					++i;
				}
			}
			
			if (looks_dependent && context.parser != nullptr) {
				// This might be a template instantiation with dependent arguments
				// Treat it as template-dependent
				return EvalResult::error("Template instantiation with dependent arguments in constant expression: " + 
				                         std::string(ns_name) + "::" + std::string(member_name),
				                         EvalErrorType::TemplateDependentExpression);
			}
			
			// Not found in symbol table or as struct static member
			return EvalResult::error("Undefined qualified identifier in constant expression: " + qualified_id.full_name());
		}

		const ASTNode& symbol_node = *symbol_opt;

		// Check if it's a variable declaration (constexpr)
		if (symbol_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
			if (!var_decl.is_constexpr()) {
				return EvalResult::error("Qualified variable must be constexpr: " + qualified_id.full_name());
			}
			const auto& initializer = var_decl.initializer();
			if (!initializer.has_value()) {
				return EvalResult::error("Constexpr variable has no initializer: " + qualified_id.full_name());
			}
			return evaluate(initializer.value(), context);
		}

		// Could be other types like enum constants - add support as needed
		return EvalResult::error("Qualified identifier is not a constant expression: " + qualified_id.full_name());
	}

	// Evaluate member access (e.g., obj.member or struct_type::static_member)
	// Also supports nested member access (e.g., obj.inner.value)
	static EvalResult evaluate_member_access(const MemberAccessNode& member_access, EvaluationContext& context) {
		// Get the object expression (e.g., 'p1' in 'p1.x')
		const ASTNode& object_expr = member_access.object();
		std::string_view member_name = member_access.member_name();
		
		// Check if this is a nested member access (e.g., obj.inner.value)
		if (object_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
			if (std::holds_alternative<MemberAccessNode>(expr_node)) {
				// Nested member access - first get the intermediate struct initializer
				const MemberAccessNode& inner_access = std::get<MemberAccessNode>(expr_node);
				return evaluate_nested_member_access(inner_access, member_name, context);
			}
		}
		
		// For constexpr struct member access, we need to handle the case where:
		// - The object is an identifier referencing a constexpr variable
		// - The variable is initialized with a ConstructorCallNode
		// - We need to find the constructor declaration and its member initializer list
		// - Extract the member value from the initializer expression
		
		// The object might be wrapped in an ExpressionNode, so unwrap it
		// Extract the identifier name
		std::string_view var_name;
		
		if (object_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
			// The ExpressionNode uses std::variant, check if it contains an IdentifierNode
			if (!std::holds_alternative<IdentifierNode>(expr_node)) {
				// Check for ArraySubscriptNode
				if (std::holds_alternative<ArraySubscriptNode>(expr_node)) {
					// Array subscript on struct - evaluate array element then access member
					return evaluate_array_subscript_member_access(std::get<ArraySubscriptNode>(expr_node), member_name, context);
				}
				// Check for FunctionCallNode - evaluate the return type and access static member
				if (std::holds_alternative<FunctionCallNode>(expr_node)) {
					const FunctionCallNode& func_call = std::get<FunctionCallNode>(expr_node);
					return evaluate_function_call_member_access(func_call, member_name, context);
				}
				return EvalResult::error("Complex member access expressions not yet supported in constant expressions");
			}
			const IdentifierNode& id_node = std::get<IdentifierNode>(expr_node);
			var_name = id_node.name();
		} else if (object_expr.is<IdentifierNode>()) {
			const IdentifierNode& id_node = object_expr.as<IdentifierNode>();
			var_name = id_node.name();
		} else {
			return EvalResult::error("Complex member access expressions not yet supported in constant expressions");
		}
		
		// Look up the variable in the symbol table
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate member access: no symbol table provided");
		}
		
		auto symbol_opt = context.symbols->lookup(var_name);
		if (!symbol_opt.has_value()) {
			return EvalResult::error("Undefined variable in member access: " + std::string(var_name));
		}
		
		const ASTNode& symbol_node = symbol_opt.value();
		
		// Check if it's a VariableDeclarationNode
		if (!symbol_node.is<VariableDeclarationNode>()) {
			return EvalResult::error("Identifier in member access is not a variable: " + std::string(var_name));
		}
		
		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		
		// Before checking if the variable is constexpr, check if we're accessing a static member
		// Static members can be accessed through any instance (even non-constexpr)
		// because they don't depend on the instance
		
		// Get the type of the variable to look up the struct info
		const DeclarationNode& var_declaration = var_decl.declaration();
		const ASTNode& var_type_node = var_declaration.type_node();
		if (var_type_node.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& var_type_spec = var_type_node.as<TypeSpecifierNode>();
			TypeIndex var_type_index = var_type_spec.type_index();
			
			if (var_type_index != TypeIndex{0} && var_type_index < gTypeInfo.size()) {
				const TypeInfo& var_type_info = gTypeInfo[var_type_index];
				const StructTypeInfo* struct_info = var_type_info.getStructInfo();
				
				if (struct_info) {
					// Look for static member
					StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
					auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_handle);
					
					if (static_member && owner_struct) {
						FLASH_LOG(ConstExpr, Debug, "Accessing static member through instance: ", member_name);
						
						// Found a static member - evaluate its initializer if available
						if (static_member->initializer.has_value()) {
							return evaluate(static_member->initializer.value(), context);
						}
						
						// If no initializer, return default value based on type
						if (static_member->type == Type::Bool) {
							return EvalResult::from_bool(false);
						}
						return EvalResult::from_int(0);
					}
				}
			}
		}
		
		// If not a static member access, check if it's a constexpr variable
		if (!var_decl.is_constexpr()) {
			return EvalResult::error("Variable in member access must be constexpr: " + std::string(var_name));
		}
		
		// Get the initializer
		const auto& initializer = var_decl.initializer();
		if (!initializer.has_value()) {
			return EvalResult::error("Constexpr variable has no initializer: " + std::string(var_name));
		}
		
		// Check if the initializer is a ConstructorCallNode
		if (!initializer->is<ConstructorCallNode>()) {
			return EvalResult::error("Member access on non-struct constexpr variable not supported");
		}
		
		const ConstructorCallNode& ctor_call = initializer->as<ConstructorCallNode>();
		
		// Get the type being constructed
		const ASTNode& type_node = ctor_call.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Constructor call without valid type specifier");
		}
		
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		
		// Get the struct type info
		if (type_spec.type() != Type::Struct && type_spec.type() != Type::UserDefined) {
			return EvalResult::error("Member access requires a struct type");
		}
		
		TypeIndex type_index = type_spec.type_index();
		if (type_index >= gTypeInfo.size()) {
			return EvalResult::error("Invalid type index in member access");
		}
		
		const TypeInfo& struct_type_info = gTypeInfo[type_index];
		const StructTypeInfo* struct_info = struct_type_info.getStructInfo();
		if (!struct_info) {
			return EvalResult::error("Type is not a struct in member access");
		}
		
		// Get the constructor arguments from the call
		const auto& ctor_args = ctor_call.arguments();
		
		// Find the matching constructor in the struct
		// We need to find a constructor with the same number of parameters as arguments
		const ConstructorDeclarationNode* matching_ctor = nullptr;
		for (const auto& member_func : struct_info->member_functions) {
			if (!member_func.is_constructor) {
				continue;
			}
			if (!member_func.function_decl.is<ConstructorDeclarationNode>()) {
				continue;
			}
			const ConstructorDeclarationNode& ctor = member_func.function_decl.as<ConstructorDeclarationNode>();
			if (ctor.parameter_nodes().size() == ctor_args.size()) {
				// Found a constructor with matching parameter count
				// For full correctness, we should check parameter types too, but for constexpr
				// evaluation in simple cases, parameter count matching is sufficient
				matching_ctor = &ctor;
				break;
			}
		}
		
		if (!matching_ctor) {
			return EvalResult::error("No matching constructor found for constexpr evaluation");
		}
		
		// Build parameter bindings: map parameter names to their evaluated argument values
		std::unordered_map<std::string_view, EvalResult> param_bindings;
		const auto& params = matching_ctor->parameter_nodes();
		for (size_t i = 0; i < params.size() && i < ctor_args.size(); ++i) {
			if (params[i].is<DeclarationNode>()) {
				const DeclarationNode& param_decl = params[i].as<DeclarationNode>();
				std::string_view param_name = param_decl.identifier_token().value();
				auto arg_result = evaluate(ctor_args[i], context);
				if (!arg_result.success()) {
					return arg_result;
				}
				param_bindings[param_name] = arg_result;
			}
		}
		
		// Look for the member in the constructor's member initializer list
		const auto& member_inits = matching_ctor->member_initializers();
		for (const auto& mem_init : member_inits) {
			if (mem_init.member_name == member_name) {
				// Found the member initializer - evaluate it with parameter bindings
				const ASTNode& init_expr = mem_init.initializer_expr;
				
				// Use evaluate_expression_with_bindings to handle complex expressions
				return evaluate_expression_with_bindings(init_expr, param_bindings, context);
			}
		}
		
		// Member not found in initializer list - check for default member initializers
		// Look through the struct's member declarations
		StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
		for (const auto& member : struct_info->members) {
			if (member.getName() == member_name_handle && member.default_initializer.has_value()) {
				// Found a default member initializer
				return evaluate(member.default_initializer.value(), context);
			}
		}
		
		// Member not found in initializer list and no default value
		return EvalResult::error("Member '" + std::string(member_name) + "' not found in constructor initializer list and has no default value");
	}

	// Helper struct to hold a ConstructorCallNode reference and its type info
	struct StructObjectInfo {
		const ConstructorCallNode* ctor_call;
		const StructTypeInfo* struct_info;
		const ConstructorDeclarationNode* matching_ctor;
	};

	// Helper to extract a member's initializer expression from a ConstructorCallNode
	// Returns the initializer ASTNode for a struct member, or nullopt if not found
	static std::optional<ASTNode> get_member_initializer(
		const ConstructorCallNode& ctor_call,
		const StructTypeInfo* struct_info,
		std::string_view member_name_param,
		[[maybe_unused]] EvaluationContext& context) {
		
		const auto& ctor_args = ctor_call.arguments();
		
		// Find the matching constructor
		const ConstructorDeclarationNode* matching_ctor = nullptr;
		for (const auto& member_func : struct_info->member_functions) {
			if (!member_func.is_constructor) continue;
			if (!member_func.function_decl.is<ConstructorDeclarationNode>()) continue;
			const ConstructorDeclarationNode& ctor = member_func.function_decl.as<ConstructorDeclarationNode>();
			if (ctor.parameter_nodes().size() == ctor_args.size()) {
				matching_ctor = &ctor;
				break;
			}
		}
		
		if (!matching_ctor) {
			return std::nullopt;
		}
		
		// Look for the member in the initializer list
		for (const auto& mem_init : matching_ctor->member_initializers()) {
			if (mem_init.member_name == member_name_param) {
				return mem_init.initializer_expr;
			}
		}
		
		// Check for default member initializer
		StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name_param);
		for (const auto& member : struct_info->members) {
			if (member.getName() == member_name_handle && member.default_initializer.has_value()) {
				return member.default_initializer.value();
			}
		}
		
		return std::nullopt;
	}

	// Helper to get StructTypeInfo from a TypeSpecifierNode
	static const StructTypeInfo* get_struct_info_from_type(const TypeSpecifierNode& type_spec) {
		if (type_spec.type() != Type::Struct && type_spec.type() != Type::UserDefined) {
			return nullptr;
		}
		
		TypeIndex type_index = type_spec.type_index();
		if (type_index >= gTypeInfo.size()) {
			return nullptr;
		}
		
		const TypeInfo& type_info = gTypeInfo[type_index];
		return type_info.getStructInfo();
	}

	// Evaluate nested member access (e.g., obj.inner.value)
	static EvalResult evaluate_nested_member_access(
		const MemberAccessNode& inner_access,
		std::string_view final_member_name,
		EvaluationContext& context) {
		
		// First, we need to get the base object and the chain of member accesses
		// For obj.inner.value:
		// - inner_access.object() is 'obj' (identifier)
		// - inner_access.member_name() is 'inner'
		// - final_member_name is 'value'
		
		const ASTNode& base_obj_expr = inner_access.object();
		std::string_view intermediate_member = inner_access.member_name();
		
		// Get the base variable name
		std::string_view base_var_name;
		
		// Handle deeper nesting recursively
		if (base_obj_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = base_obj_expr.as<ExpressionNode>();
			if (std::holds_alternative<MemberAccessNode>(expr_node)) {
				// Even deeper nesting - this requires more complex logic
				// For now, we support up to one level of nesting
				return EvalResult::error("Deeply nested member access (more than 2 levels) not yet supported");
			}
			if (!std::holds_alternative<IdentifierNode>(expr_node)) {
				return EvalResult::error("Complex base expression in nested member access not supported");
			}
			base_var_name = std::get<IdentifierNode>(expr_node).name();
		} else if (base_obj_expr.is<IdentifierNode>()) {
			base_var_name = base_obj_expr.as<IdentifierNode>().name();
		} else {
			return EvalResult::error("Invalid base expression in nested member access");
		}
		
		// Look up the base variable
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate nested member access: no symbol table provided");
		}
		
		auto symbol_opt = context.symbols->lookup(base_var_name);
		if (!symbol_opt.has_value()) {
			return EvalResult::error("Undefined variable in nested member access: " + std::string(base_var_name));
		}
		
		const ASTNode& symbol_node = symbol_opt.value();
		if (!symbol_node.is<VariableDeclarationNode>()) {
			return EvalResult::error("Identifier in nested member access is not a variable");
		}
		
		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		if (!var_decl.is_constexpr()) {
			return EvalResult::error("Variable in nested member access must be constexpr");
		}
		
		const auto& initializer = var_decl.initializer();
		if (!initializer.has_value() || !initializer->is<ConstructorCallNode>()) {
			return EvalResult::error("Nested member access requires a struct with constructor");
		}
		
		const ConstructorCallNode& base_ctor = initializer->as<ConstructorCallNode>();
		
		// Get the base struct type info
		const ASTNode& type_node = base_ctor.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Invalid type specifier in nested member access");
		}
		
		const TypeSpecifierNode& base_type_spec = type_node.as<TypeSpecifierNode>();
		const StructTypeInfo* base_struct_info = get_struct_info_from_type(base_type_spec);
		if (!base_struct_info) {
			return EvalResult::error("Base type is not a struct in nested member access");
		}
		
		// Get the intermediate member's initializer (this should be a ConstructorCallNode for the nested struct)
		auto intermediate_init_opt = get_member_initializer(base_ctor, base_struct_info, intermediate_member, context);
		if (!intermediate_init_opt.has_value()) {
			return EvalResult::error("Intermediate member '" + std::string(intermediate_member) + "' not found");
		}
		
		const ASTNode& intermediate_init = intermediate_init_opt.value();
		
		// Build parameter bindings for the outer constructor
		const auto& base_ctor_args = base_ctor.arguments();
		std::unordered_map<std::string_view, EvalResult> param_bindings;
		
		// Find the matching constructor for the base struct
		const ConstructorDeclarationNode* base_matching_ctor = nullptr;
		for (const auto& member_func : base_struct_info->member_functions) {
			if (!member_func.is_constructor) continue;
			if (!member_func.function_decl.is<ConstructorDeclarationNode>()) continue;
			const ConstructorDeclarationNode& ctor = member_func.function_decl.as<ConstructorDeclarationNode>();
			if (ctor.parameter_nodes().size() == base_ctor_args.size()) {
				base_matching_ctor = &ctor;
				break;
			}
		}
		
		if (base_matching_ctor) {
			const auto& params = base_matching_ctor->parameter_nodes();
			for (size_t i = 0; i < params.size() && i < base_ctor_args.size(); ++i) {
				if (params[i].is<DeclarationNode>()) {
					const DeclarationNode& param_decl = params[i].as<DeclarationNode>();
					std::string_view param_name = param_decl.identifier_token().value();
					auto arg_result = evaluate(base_ctor_args[i], context);
					if (!arg_result.success()) {
						return arg_result;
					}
					param_bindings[param_name] = arg_result;
				}
			}
		}
		
		// The intermediate initializer could be:
		// 1. A ConstructorCallNode (e.g., Inner(42)) - rare, explicit construction
		// 2. A simple expression that should be passed to the inner struct's constructor
		// The parser stores member initializers as just the argument, not the full constructor call
		
		// Find the intermediate member's type from the struct's member list
		const StructMember* intermediate_member_info = nullptr;
		StringHandle intermediate_member_handle = StringTable::getOrInternStringHandle(intermediate_member);
		for (const auto& member : base_struct_info->members) {
			if (member.getName() == intermediate_member_handle) {
				intermediate_member_info = &member;
				break;
			}
		}
		
		if (!intermediate_member_info) {
			return EvalResult::error("Intermediate member '" + std::string(intermediate_member) + "' not found in struct");
		}
		
		// Get the inner struct's type info
		if (intermediate_member_info->type != Type::Struct && intermediate_member_info->type != Type::UserDefined) {
			return EvalResult::error("Intermediate member is not a struct type");
		}
		
		TypeIndex inner_type_index = intermediate_member_info->type_index;
		if (inner_type_index >= gTypeInfo.size()) {
			return EvalResult::error("Invalid inner type index");
		}
		
		const TypeInfo& inner_type_info = gTypeInfo[inner_type_index];
		const StructTypeInfo* inner_struct_info = inner_type_info.getStructInfo();
		if (!inner_struct_info) {
			return EvalResult::error("Inner member type is not a struct");
		}
		
		// Evaluate the intermediate initializer with parameter bindings
		// This gives us the argument value to pass to the inner struct's constructor
		auto init_arg_result = evaluate_expression_with_bindings(intermediate_init, param_bindings, context);
		if (!init_arg_result.success()) {
			return init_arg_result;
		}
		
		// Find a matching constructor in the inner struct (single argument)
		const ConstructorDeclarationNode* inner_matching_ctor = nullptr;
		for (const auto& member_func : inner_struct_info->member_functions) {
			if (!member_func.is_constructor) continue;
			if (!member_func.function_decl.is<ConstructorDeclarationNode>()) continue;
			const ConstructorDeclarationNode& ctor = member_func.function_decl.as<ConstructorDeclarationNode>();
			// For now, assume single-argument constructor
			if (ctor.parameter_nodes().size() == 1) {
				inner_matching_ctor = &ctor;
				break;
			}
		}
		
		if (!inner_matching_ctor) {
			return EvalResult::error("No matching single-argument constructor for inner struct");
		}
		
		// Build inner parameter bindings
		std::unordered_map<std::string_view, EvalResult> inner_param_bindings;
		const auto& inner_params = inner_matching_ctor->parameter_nodes();
		if (!inner_params.empty() && inner_params[0].is<DeclarationNode>()) {
			const DeclarationNode& param_decl = inner_params[0].as<DeclarationNode>();
			std::string_view param_name = param_decl.identifier_token().value();
			inner_param_bindings[param_name] = init_arg_result;
		}
		
		// Look for the final member in the inner constructor's initializer list
		for (const auto& mem_init : inner_matching_ctor->member_initializers()) {
			if (mem_init.member_name == final_member_name) {
				return evaluate_expression_with_bindings(mem_init.initializer_expr, inner_param_bindings, context);
			}
		}
		
		// Check for default member initializer
		StringHandle final_member_name_handle = StringTable::getOrInternStringHandle(final_member_name);
		for (const auto& member : inner_struct_info->members) {
			if (member.getName() == final_member_name_handle && member.default_initializer.has_value()) {
				return evaluate(member.default_initializer.value(), context);
			}
		}
		
		return EvalResult::error("Final member '" + std::string(final_member_name) + "' not found in inner struct");
	}

	// Evaluate array subscript followed by member access (e.g., arr[0].member)
	static EvalResult evaluate_array_subscript_member_access(
		[[maybe_unused]] const ArraySubscriptNode& subscript,
		[[maybe_unused]] std::string_view member_name,
		[[maybe_unused]] EvaluationContext& context) {
		// TODO: Implement array subscript followed by member access evaluation
		// For now, return an error - this is more complex
		return EvalResult::error("Array subscript followed by member access not yet supported");
	}

	// Helper function to look up and evaluate static member from struct info
	static EvalResult evaluate_static_member_from_struct(
		const StructTypeInfo* struct_info,
		const TypeInfo& type_info,
		StringHandle member_name_handle,
		std::string_view member_name,
		EvaluationContext& context) {
		
		// Look up the static member in the struct
		// Search for a static member variable with the given name
		for (const auto& static_member : struct_info->static_members) {
			if (static_member.getName() == member_name_handle) {
				// Found the static member - check if it has an initializer
				if (static_member.initializer.has_value()) {
					// Evaluate the initializer directly
					return evaluate(*static_member.initializer, context);
				}
				
				// If no inline initializer, try to find the definition in the symbol table
				// Build qualified member name using StringBuilder
				StringBuilder qualified_name_builder;
				qualified_name_builder.append(StringTable::getStringView(type_info.name_));
				qualified_name_builder.append("::");
				qualified_name_builder.append(member_name);
				std::string_view qualified_member_name = qualified_name_builder.commit();
				
				auto member_symbol = context.symbols->lookup(qualified_member_name);
				if (member_symbol.has_value()) {
					const ASTNode& member_node = member_symbol.value();
					if (member_node.is<VariableDeclarationNode>()) {
						const VariableDeclarationNode& var_decl = member_node.as<VariableDeclarationNode>();
						if (var_decl.is_constexpr() && var_decl.initializer().has_value()) {
							return evaluate(*var_decl.initializer(), context);
						}
					}
				}
				
				return EvalResult::error("Static member '" + std::string(member_name) + "' found but has no constexpr initializer");
			}
		}
		
		return EvalResult::error("Member '" + std::string(member_name) + "' not found in return type");
	}

	// Evaluate function call followed by member access (e.g., get_struct().member)
	// This is used for accessing static members of the return type
	static EvalResult evaluate_function_call_member_access(
		const FunctionCallNode& func_call,
		std::string_view member_name,
		EvaluationContext& context) {
		
		// Get the function declaration to determine return type
		const DeclarationNode& func_decl_node = func_call.function_declaration();
		
		// Look up the function in the symbol table
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate function call member access: no symbol table provided");
		}
		
		std::string_view func_name = func_decl_node.identifier_token().value();
		auto symbol_opt = context.symbols->lookup(func_name);
		
		if (!symbol_opt.has_value()) {
			return EvalResult::error("Function not found: " + std::string(func_name));
		}
		
		const ASTNode& symbol_node = symbol_opt.value();
		
		// Convert member_name to StringHandle once for efficient comparison
		StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
		
		// Extract FunctionDeclarationNode from either regular or template function
		const FunctionDeclarationNode* func_decl = nullptr;
		
		if (symbol_node.is<FunctionDeclarationNode>()) {
			func_decl = &symbol_node.as<FunctionDeclarationNode>();
		} else if (symbol_node.is<TemplateFunctionDeclarationNode>()) {
			const TemplateFunctionDeclarationNode& template_func = symbol_node.as<TemplateFunctionDeclarationNode>();
			const ASTNode& func_node = template_func.function_declaration();
			if (func_node.is<FunctionDeclarationNode>()) {
				func_decl = &func_node.as<FunctionDeclarationNode>();
			}
		}
		
		if (!func_decl) {
			return EvalResult::error("Unsupported function type for member access");
		}
		
		// Get the return type from the declaration node
		const ASTNode& type_node = func_decl->decl_node().type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Function return type is not a TypeSpecifierNode");
		}
		
		const TypeSpecifierNode& return_type = type_node.as<TypeSpecifierNode>();
		
		// Get the type name - this should be a struct/class type
		if (return_type.type() != Type::UserDefined && return_type.type() != Type::Struct) {
			return EvalResult::error("Function return type is not a struct - cannot access member");
		}
		
		// Get the struct type name
		TypeIndex type_index = return_type.type_index();
		if (type_index >= gTypeInfo.size()) {
			return EvalResult::error("Invalid type index for function return type");
		}
		
		const TypeInfo& type_info = gTypeInfo[type_index];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		if (!struct_info) {
			return EvalResult::error("Return type is not a struct");
		}
		
		// Use the helper function to look up and evaluate the static member
		return evaluate_static_member_from_struct(struct_info, type_info, member_name_handle, member_name, context);
	}

	// Evaluate constexpr member function call (e.g., p.sum() in constexpr context)
	static EvalResult evaluate_member_function_call(const MemberFunctionCallNode& member_func_call, EvaluationContext& context) {
		// Check recursion depth
		if (context.current_depth >= context.max_recursion_depth) {
			return EvalResult::error("Constexpr recursion depth limit exceeded in member function call");
		}
		
		// Get the object being called on
		const ASTNode& object_expr = member_func_call.object();
		
		// Get the function name from the placeholder FunctionDeclarationNode
		const FunctionDeclarationNode& placeholder_func = member_func_call.function_declaration();
		std::string_view func_name = placeholder_func.decl_node().identifier_token().value();
		
		// For lambda calls (operator()), we need special handling
		const bool is_operator_call = (func_name == "operator()");
		
		// First, we need to get the struct type from the object to look up the actual function
		std::string_view var_name;
		
		if (object_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
			if (!std::holds_alternative<IdentifierNode>(expr_node)) {
				return EvalResult::error("Complex object expressions not yet supported in constexpr member function calls");
			}
			const IdentifierNode& id_node = std::get<IdentifierNode>(expr_node);
			var_name = id_node.name();
		} else if (object_expr.is<IdentifierNode>()) {
			const IdentifierNode& id_node = object_expr.as<IdentifierNode>();
			var_name = id_node.name();
		} else {
			return EvalResult::error("Complex object expressions not yet supported in constexpr member function calls");
		}
		
		// Look up the variable in the symbol table
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate member function call: no symbol table provided");
		}
		
		auto symbol_opt = context.symbols->lookup(var_name);
		if (!symbol_opt.has_value()) {
			return EvalResult::error("Undefined variable in member function call: " + std::string(var_name));
		}
		
		const ASTNode& symbol_node = symbol_opt.value();
		
		if (!symbol_node.is<VariableDeclarationNode>()) {
			return EvalResult::error("Identifier in member function call is not a variable: " + std::string(var_name));
		}
		
		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		
		if (!var_decl.is_constexpr()) {
			return EvalResult::error("Variable in member function call must be constexpr: " + std::string(var_name));
		}
		
		const auto& initializer = var_decl.initializer();
		if (!initializer.has_value()) {
			return EvalResult::error("Constexpr variable has no initializer: " + std::string(var_name));
		}
		
		// Check if this is a lambda call (operator() on a lambda object)
		if (is_operator_call) {
			const LambdaExpressionNode* lambda = extract_lambda_from_initializer(initializer);
			if (lambda) {
				return evaluate_lambda_call(*lambda, member_func_call.arguments(), context);
			}
		}
		
		if (!initializer->is<ConstructorCallNode>()) {
			return EvalResult::error("Member function calls require struct/class objects");
		}
		
		const ConstructorCallNode& ctor_call = initializer->as<ConstructorCallNode>();
		
		// Get the struct type info
		const ASTNode& type_node = ctor_call.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Constructor call without valid type specifier");
		}
		
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		
		if (type_spec.type() != Type::Struct && type_spec.type() != Type::UserDefined) {
			return EvalResult::error("Member function call requires a struct type");
		}
		
		TypeIndex type_index = type_spec.type_index();
		if (type_index >= gTypeInfo.size()) {
			return EvalResult::error("Invalid type index in member function call");
		}
		
		const TypeInfo& struct_type_info = gTypeInfo[type_index];
		const StructTypeInfo* struct_info = struct_type_info.getStructInfo();
		if (!struct_info) {
			return EvalResult::error("Type is not a struct in member function call");
		}
		
		// Look up the actual member function in the struct's type info
		const FunctionDeclarationNode* actual_func = nullptr;
		StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
		for (const auto& member_func : struct_info->member_functions) {
			if (member_func.is_constructor || member_func.is_destructor) continue;
			if (member_func.getName() != func_name_handle) continue;
			
			if (member_func.function_decl.is<FunctionDeclarationNode>()) {
				actual_func = &member_func.function_decl.as<FunctionDeclarationNode>();
				break;
			}
		}
		
		if (!actual_func) {
			return EvalResult::error("Member function not found: " + std::string(func_name));
		}
		
		// Check if it's a constexpr function
		if (!actual_func->is_constexpr()) {
			return EvalResult::error("Member function must be constexpr: " + std::string(func_name));
		}
		
		// Get the function body
		const auto& definition = actual_func->get_definition();
		if (!definition.has_value()) {
			return EvalResult::error("Constexpr member function has no body: " + std::string(func_name));
		}
		
		// Extract member values from the object for 'this' access
		std::unordered_map<std::string_view, EvalResult> member_bindings;
		
		auto member_extraction_result = extract_object_members(object_expr, member_bindings, context);
		if (!member_extraction_result.success()) {
			return member_extraction_result;
		}
		
		// Evaluate function arguments and add to bindings
		const auto& arguments = member_func_call.arguments();
		const auto& parameters = actual_func->parameter_nodes();
		
		if (arguments.size() != parameters.size()) {
			return EvalResult::error("Member function argument count mismatch in constant expression");
		}
		
		for (size_t i = 0; i < arguments.size(); ++i) {
			// Get parameter name
			const ASTNode& param_node = parameters[i];
			if (!param_node.is<DeclarationNode>()) {
				return EvalResult::error("Invalid parameter node in constexpr member function");
			}
			const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
			std::string_view param_name = param_decl.identifier_token().value();
			
			// Evaluate argument
			auto arg_result = evaluate(arguments[i], context);
			if (!arg_result.success()) {
				return arg_result;
			}
			member_bindings[param_name] = arg_result;
		}
		
		// Increase recursion depth
		context.current_depth++;
		
		// Evaluate the function body
		const ASTNode& body_node = definition.value();
		if (!body_node.is<BlockNode>()) {
			context.current_depth--;
			return EvalResult::error("Member function body is not a block");
		}
		
		const BlockNode& body = body_node.as<BlockNode>();
		const auto& statements = body.get_statements();
		
		// For simple constexpr functions, we expect a single return statement
		if (statements.size() != 1) {
			context.current_depth--;
			return EvalResult::error("Constexpr member function must have a single return statement (complex statements not yet supported)");
		}
		
		auto result = evaluate_statement_with_bindings(statements[0], member_bindings, context);
		context.current_depth--;
		return result;
	}

	// Helper to extract member values from a constexpr object
	static EvalResult extract_object_members(
		const ASTNode& object_expr,
		std::unordered_map<std::string_view, EvalResult>& member_bindings,
		EvaluationContext& context) {
		
		// Get the object variable name
		std::string_view var_name;
		
		if (object_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
			if (!std::holds_alternative<IdentifierNode>(expr_node)) {
				return EvalResult::error("Complex object expressions not yet supported in constexpr member function calls");
			}
			const IdentifierNode& id_node = std::get<IdentifierNode>(expr_node);
			var_name = id_node.name();
		} else if (object_expr.is<IdentifierNode>()) {
			const IdentifierNode& id_node = object_expr.as<IdentifierNode>();
			var_name = id_node.name();
		} else {
			return EvalResult::error("Complex object expressions not yet supported in constexpr member function calls");
		}
		
		// Look up the variable in the symbol table
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate member function call: no symbol table provided");
		}
		
		auto symbol_opt = context.symbols->lookup(var_name);
		if (!symbol_opt.has_value()) {
			return EvalResult::error("Undefined variable in member function call: " + std::string(var_name));
		}
		
		const ASTNode& symbol_node = symbol_opt.value();
		
		if (!symbol_node.is<VariableDeclarationNode>()) {
			return EvalResult::error("Identifier in member function call is not a variable: " + std::string(var_name));
		}
		
		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		
		if (!var_decl.is_constexpr()) {
			return EvalResult::error("Variable in member function call must be constexpr: " + std::string(var_name));
		}
		
		const auto& initializer = var_decl.initializer();
		if (!initializer.has_value()) {
			return EvalResult::error("Constexpr variable has no initializer: " + std::string(var_name));
		}
		
		if (!initializer->is<ConstructorCallNode>()) {
			return EvalResult::error("Member function calls require struct/class objects");
		}
		
		const ConstructorCallNode& ctor_call = initializer->as<ConstructorCallNode>();
		
		// Get the struct type info
		const ASTNode& type_node = ctor_call.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Constructor call without valid type specifier");
		}
		
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		
		if (type_spec.type() != Type::Struct && type_spec.type() != Type::UserDefined) {
			return EvalResult::error("Member function call requires a struct type");
		}
		
		TypeIndex type_index = type_spec.type_index();
		if (type_index >= gTypeInfo.size()) {
			return EvalResult::error("Invalid type index in member function call");
		}
		
		const TypeInfo& struct_type_info = gTypeInfo[type_index];
		const StructTypeInfo* struct_info = struct_type_info.getStructInfo();
		if (!struct_info) {
			return EvalResult::error("Type is not a struct in member function call");
		}
		
		const auto& ctor_args = ctor_call.arguments();
		
		// Find the matching constructor
		const ConstructorDeclarationNode* matching_ctor = nullptr;
		for (const auto& member_func : struct_info->member_functions) {
			if (!member_func.is_constructor) continue;
			if (!member_func.function_decl.is<ConstructorDeclarationNode>()) continue;
			const ConstructorDeclarationNode& ctor = member_func.function_decl.as<ConstructorDeclarationNode>();
			if (ctor.parameter_nodes().size() == ctor_args.size()) {
				matching_ctor = &ctor;
				break;
			}
		}
		
		if (!matching_ctor) {
			return EvalResult::error("No matching constructor found for constexpr object");
		}
		
		// Build parameter bindings for the constructor
		std::unordered_map<std::string_view, EvalResult> ctor_param_bindings;
		const auto& params = matching_ctor->parameter_nodes();
		for (size_t i = 0; i < params.size() && i < ctor_args.size(); ++i) {
			if (params[i].is<DeclarationNode>()) {
				const DeclarationNode& param_decl = params[i].as<DeclarationNode>();
				std::string_view param_name = param_decl.identifier_token().value();
				auto arg_result = evaluate(ctor_args[i], context);
				if (!arg_result.success()) {
					return arg_result;
				}
				ctor_param_bindings[param_name] = arg_result;
			}
		}
		
		// Extract member values from the initializer list
		for (const auto& mem_init : matching_ctor->member_initializers()) {
			auto member_result = evaluate_expression_with_bindings(mem_init.initializer_expr, ctor_param_bindings, context);
			if (!member_result.success()) {
				return member_result;
			}
			member_bindings[mem_init.member_name] = member_result;
		}
		
		// Also check for default member initializers for members not in the initializer list
		for (const auto& member : struct_info->members) {
			std::string_view name_view = StringTable::getStringView(member.getName());
			if (member_bindings.find(name_view) == member_bindings.end() && member.default_initializer.has_value()) {
				auto default_result = evaluate(member.default_initializer.value(), context);
				if (default_result.success()) {
					member_bindings[name_view] = default_result;
				}
			}
		}
		
		return EvalResult::from_bool(true);  // Success
	}

	// Evaluate array subscript (e.g., arr[0] or obj.data[1])
	static EvalResult evaluate_array_subscript(const ArraySubscriptNode& subscript, EvaluationContext& context) {
		// First, evaluate the index expression to get the constant index
		auto index_result = evaluate(subscript.index_expr(), context);
		if (!index_result.success()) {
			return index_result;
		}
		
		long long index = index_result.as_int();
		if (index < 0) {
			return EvalResult::error("Negative array index in constant expression");
		}
		
		// Get the array expression - this could be:
		// 1. A member access (e.g., obj.data)
		// 2. An identifier (e.g., arr)
		const ASTNode& array_expr = subscript.array_expr();
		
		// Check if it's a member access (e.g., obj.data[0])
		if (array_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr = array_expr.as<ExpressionNode>();
			if (std::holds_alternative<MemberAccessNode>(expr)) {
				return evaluate_member_array_subscript(std::get<MemberAccessNode>(expr), static_cast<size_t>(index), context);
			}
			if (std::holds_alternative<IdentifierNode>(expr)) {
				return evaluate_variable_array_subscript(std::get<IdentifierNode>(expr).name(), static_cast<size_t>(index), context);
			}
		}
		
		return EvalResult::error("Array subscript on unsupported expression type");
	}

	// Evaluate array subscript on a member (e.g., obj.data[0])
	static EvalResult evaluate_member_array_subscript(
		const MemberAccessNode& member_access,
		size_t index,
		EvaluationContext& context) {
		
		const ASTNode& object_expr = member_access.object();
		std::string_view member_name = member_access.member_name();
		
		// Get the base variable name
		std::string_view var_name;
		
		if (object_expr.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = object_expr.as<ExpressionNode>();
			if (!std::holds_alternative<IdentifierNode>(expr_node)) {
				return EvalResult::error("Complex expressions in array member access not supported");
			}
			var_name = std::get<IdentifierNode>(expr_node).name();
		} else if (object_expr.is<IdentifierNode>()) {
			var_name = object_expr.as<IdentifierNode>().name();
		} else {
			return EvalResult::error("Invalid object expression in array member access");
		}
		
		// Look up the variable
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate array subscript: no symbol table provided");
		}
		
		auto symbol_opt = context.symbols->lookup(var_name);
		if (!symbol_opt.has_value()) {
			return EvalResult::error("Undefined variable in array subscript: " + std::string(var_name));
		}
		
		const ASTNode& symbol_node = symbol_opt.value();
		if (!symbol_node.is<VariableDeclarationNode>()) {
			return EvalResult::error("Identifier in array subscript is not a variable");
		}
		
		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		if (!var_decl.is_constexpr()) {
			return EvalResult::error("Variable in array subscript must be constexpr");
		}
		
		const auto& initializer = var_decl.initializer();
		if (!initializer.has_value() || !initializer->is<ConstructorCallNode>()) {
			return EvalResult::error("Array subscript requires a struct with constructor");
		}
		
		const ConstructorCallNode& ctor_call = initializer->as<ConstructorCallNode>();
		
		// Get the struct type info
		const ASTNode& type_node = ctor_call.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Invalid type specifier in array subscript");
		}
		
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		const StructTypeInfo* struct_info = get_struct_info_from_type(type_spec);
		if (!struct_info) {
			return EvalResult::error("Type is not a struct in array subscript");
		}
		
		// Get the array member's initializer
		auto member_init_opt = get_member_initializer(ctor_call, struct_info, member_name, context);
		if (!member_init_opt.has_value()) {
			return EvalResult::error("Array member '" + std::string(member_name) + "' not found");
		}
		
		const ASTNode& member_init = member_init_opt.value();
		
		// The member initializer should be an InitializerListNode for arrays
		if (member_init.is<InitializerListNode>()) {
			const InitializerListNode& init_list = member_init.as<InitializerListNode>();
			const auto& elements = init_list.initializers();
			
			if (index >= elements.size()) {
				return EvalResult::error("Array index " + std::to_string(index) + " out of bounds (size " + std::to_string(elements.size()) + ")");
			}
			
			// Build parameter bindings for the constructor
			const auto& ctor_args = ctor_call.arguments();
			std::unordered_map<std::string_view, EvalResult> param_bindings;
			
			// Find matching constructor
			const ConstructorDeclarationNode* matching_ctor = nullptr;
			for (const auto& member_func : struct_info->member_functions) {
				if (!member_func.is_constructor) continue;
				if (!member_func.function_decl.is<ConstructorDeclarationNode>()) continue;
				const ConstructorDeclarationNode& ctor = member_func.function_decl.as<ConstructorDeclarationNode>();
				if (ctor.parameter_nodes().size() == ctor_args.size()) {
					matching_ctor = &ctor;
					break;
				}
			}
			
			if (matching_ctor) {
				const auto& params = matching_ctor->parameter_nodes();
				for (size_t i = 0; i < params.size() && i < ctor_args.size(); ++i) {
					if (params[i].is<DeclarationNode>()) {
						const DeclarationNode& param_decl = params[i].as<DeclarationNode>();
						std::string_view param_name = param_decl.identifier_token().value();
						auto arg_result = evaluate(ctor_args[i], context);
						if (!arg_result.success()) {
							return arg_result;
						}
						param_bindings[param_name] = arg_result;
					}
				}
			}
			
			return evaluate_expression_with_bindings(elements[index], param_bindings, context);
		}
		
		return EvalResult::error("Array member is not initialized with an array initializer");
	}

	// Evaluate array subscript on a variable (e.g., arr[0] where arr is constexpr)
	static EvalResult evaluate_variable_array_subscript(
		std::string_view var_name,
		size_t index,
		EvaluationContext& context) {
		
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate array subscript: no symbol table provided");
		}
		
		auto symbol_opt = context.symbols->lookup(var_name);
		if (!symbol_opt.has_value()) {
			return EvalResult::error("Undefined variable in array subscript: " + std::string(var_name));
		}
		
		const ASTNode& symbol_node = symbol_opt.value();
		if (!symbol_node.is<VariableDeclarationNode>()) {
			return EvalResult::error("Identifier in array subscript is not a variable");
		}
		
		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		if (!var_decl.is_constexpr()) {
			return EvalResult::error("Variable in array subscript must be constexpr");
		}
		
		const auto& initializer = var_decl.initializer();
		if (!initializer.has_value()) {
			return EvalResult::error("Constexpr array has no initializer");
		}
		
		// The initializer should be an InitializerListNode for arrays
		if (initializer->is<InitializerListNode>()) {
			const InitializerListNode& init_list = initializer->as<InitializerListNode>();
			const auto& elements = init_list.initializers();
			
			if (index >= elements.size()) {
				return EvalResult::error("Array index " + std::to_string(index) + " out of bounds (size " + std::to_string(elements.size()) + ")");
			}
			
			return evaluate(elements[index], context);
		}
		
		return EvalResult::error("Array variable is not initialized with an array initializer");
	}

	// Helper functions for branchless type checking
	static bool isArithmeticType(Type type) {
		// Branchless: arithmetic types are Bool(1) through LongDouble(14)
		return (static_cast<int_fast16_t>(type) >= static_cast<int_fast16_t>(Type::Bool)) &
		       (static_cast<int_fast16_t>(type) <= static_cast<int_fast16_t>(Type::LongDouble));
	}

	static bool isFundamentalType(Type type) {
		// Branchless: fundamental types are Void(0), Nullptr(28), or arithmetic types
		return (type == Type::Void) | (type == Type::Nullptr) | isArithmeticType(type);
	}

	// Evaluate type trait expressions (e.g., __is_void(int), __is_constant_evaluated())
	static EvalResult evaluate_type_trait(const TypeTraitExprNode& trait_expr) {
		// Handle __is_constant_evaluated() specially - it returns true during constexpr evaluation
		if (trait_expr.kind() == TypeTraitKind::IsConstantEvaluated) {
			// When evaluated in constexpr context, this always returns true
			return EvalResult::from_bool(true);
		}

		// For other type traits, we need to evaluate them based on the type
		// Most type traits can be evaluated at compile time
		if (!trait_expr.has_type()) {
			return EvalResult::error("Type trait requires a type argument");
		}

		const ASTNode& type_node = trait_expr.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Type trait argument must be a type");
		}

		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		Type type = type_spec.type();
		bool is_reference = type_spec.is_reference();
		bool is_rvalue_reference = type_spec.is_rvalue_reference();
		size_t pointer_depth = type_spec.pointer_depth();

		bool result = false;

		// Evaluate the type trait based on its kind
		switch (trait_expr.kind()) {
			case TypeTraitKind::IsVoid:
				result = (type == Type::Void && !is_reference && pointer_depth == 0);
				break;

			case TypeTraitKind::IsIntegral:
				result = (type == Type::Bool ||
				         type == Type::Char ||
				         type == Type::Short || type == Type::Int || type == Type::Long || type == Type::LongLong ||
				         type == Type::UnsignedChar || type == Type::UnsignedShort || type == Type::UnsignedInt ||
				         type == Type::UnsignedLong || type == Type::UnsignedLongLong)
				         && !is_reference && pointer_depth == 0;
				break;

			case TypeTraitKind::IsFloatingPoint:
				result = (type == Type::Float || type == Type::Double || type == Type::LongDouble)
				         && !is_reference && pointer_depth == 0;
				break;

			case TypeTraitKind::IsPointer:
				result = (pointer_depth > 0) && !is_reference;
				break;

			case TypeTraitKind::IsLvalueReference:
				result = is_reference && !is_rvalue_reference;
				break;

			case TypeTraitKind::IsRvalueReference:
				result = is_rvalue_reference;
				break;

			case TypeTraitKind::IsArray:
				result = type_spec.is_array() && !is_reference && pointer_depth == 0;
				break;

			case TypeTraitKind::IsReference:
				result = is_reference | is_rvalue_reference;
				break;

			case TypeTraitKind::IsArithmetic:
				result = isArithmeticType(type) & !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsFundamental:
				result = isFundamentalType(type) & !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsObject:
				result = (type != Type::Function) & (type != Type::Void) & !is_reference & !is_rvalue_reference;
				break;

			case TypeTraitKind::IsScalar:
				result = (isArithmeticType(type) ||
				          type == Type::Enum || type == Type::Nullptr ||
				          type == Type::MemberObjectPointer || type == Type::MemberFunctionPointer ||
				          pointer_depth > 0)
				          && !is_reference;
				break;

			case TypeTraitKind::IsCompound:
				result = !(isFundamentalType(type) & !is_reference & (pointer_depth == 0));
				break;

			case TypeTraitKind::IsConst:
				result = type_spec.is_const();
				break;

			case TypeTraitKind::IsVolatile:
				result = type_spec.is_volatile();
				break;

			case TypeTraitKind::IsSigned:
				result = ((type == Type::Char || type == Type::Short || type == Type::Int ||
				          type == Type::Long || type == Type::LongLong)
				          && !is_reference && pointer_depth == 0);
				break;

			case TypeTraitKind::IsUnsigned:
				result = ((type == Type::Bool || type == Type::UnsignedChar || type == Type::UnsignedShort ||
				          type == Type::UnsignedInt || type == Type::UnsignedLong || type == Type::UnsignedLongLong)
				          && !is_reference && pointer_depth == 0);
				break;

			case TypeTraitKind::IsBoundedArray:
				result = type_spec.is_array() & int(type_spec.array_size() > 0) & !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsUnboundedArray:
				result = type_spec.is_array() & int(type_spec.array_size() == 0) & !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsAggregate:
				// Arrays are aggregates
				result = type_spec.is_array() & !is_reference & (pointer_depth == 0);
				// For struct types, we need runtime type info, so fall through to default
				break;

			case TypeTraitKind::IsCompleteOrUnbounded:
				// __is_complete_or_unbounded evaluates to true if either:
				// 1. T is a complete type, or
				// 2. T is an unbounded array type (e.g. int[])
				// Returns false for: void, incomplete class types, bounded arrays with incomplete elements
				
				// Check for void - always incomplete
				if (type == Type::Void && pointer_depth == 0 && !is_reference) {
					return EvalResult::from_bool(false);
				}
				
				// Check for unbounded array - always returns true
				if (type_spec.is_array() && type_spec.array_size() == 0) {
					return EvalResult::from_bool(true);
				}
				
				// Check for incomplete class/struct types
				// A type is incomplete if it's a struct/class with no StructTypeInfo
				if ((type == Type::Struct || type == Type::UserDefined) && 
				    pointer_depth == 0 && !is_reference) {
					TypeIndex type_idx = type_spec.type_index();
					if (type_idx != TypeIndex{0}) {
						const TypeInfo& type_info = gTypeInfo[type_idx];
						const StructTypeInfo* struct_info = type_info.getStructInfo();
						// If no struct_info, the type is incomplete
						if (!struct_info) {
							return EvalResult::from_bool(false);
						}
					}
				}
				
				// All other types are considered complete
				return EvalResult::from_bool(true);

			// Add more type traits as needed
			// For now, other type traits return false during constexpr evaluation
			default:
				result = false;
				break;
		}

		return EvalResult::from_bool(result);
	}
};
