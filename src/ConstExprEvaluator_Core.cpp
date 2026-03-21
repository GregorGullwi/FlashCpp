#include "Parser.h"
#include "ConstExprEvaluator.h"

namespace ConstExpr {

namespace {
	constexpr std::string_view kStatementExecutedWithoutReturn = "Statement executed (not a return)";
	constexpr std::string_view kBreakExecuted = "Break executed";
	constexpr std::string_view kContinueExecuted = "Continue executed";

	bool isStatementExecutedWithoutReturn(const EvalResult& result) {
		return !result.success() && result.error_message == kStatementExecutedWithoutReturn;
	}

	bool isBreakExecuted(const EvalResult& result) {
		return !result.success() && result.error_message == kBreakExecuted;
	}

	bool isContinueExecuted(const EvalResult& result) {
		return !result.success() && result.error_message == kContinueExecuted;
	}

	bool should_preserve_exact_type(const TypeSpecifierNode& type_spec) {
		return !isPlaceholderAutoType(type_spec.type());
	}

	void maybe_set_exact_type(EvalResult& result, const TypeSpecifierNode& type_spec) {
		if (should_preserve_exact_type(type_spec)) {
			result.set_exact_type(type_spec);
		}
	}

	void maybe_set_exact_type_from_declaration(EvalResult& result, const DeclarationNode& decl) {
		if (decl.is_array() || !decl.type_node().is<TypeSpecifierNode>()) {
			return;
		}

		maybe_set_exact_type(result, decl.type_node().as<TypeSpecifierNode>());
	}

	void maybe_set_exact_type_from_initializer(EvalResult& result, const ASTNode& initializer, EvaluationContext& context) {
		if (!context.parser) {
			return;
		}

		auto init_type = context.parser->get_expression_type(initializer);
		if (init_type.has_value()) {
			maybe_set_exact_type(result, *init_type);
		}
	}

	void maybe_set_binding_result_exact_type(EvalResult& result, const DeclarationNode& decl, const ASTNode* initializer, EvaluationContext& context) {
		if (!decl.is_array() && decl.type_node().is<TypeSpecifierNode>()) {
			const auto& type_spec = decl.type_node().as<TypeSpecifierNode>();
			if (should_preserve_exact_type(type_spec)) {
				result.set_exact_type(type_spec);
				return;
			}
		}

		if (initializer) {
			maybe_set_exact_type_from_initializer(result, *initializer, context);
		}
	}
}

// Main evaluation entry point
// Evaluates a constant expression and returns the result
EvalResult Evaluator::evaluate(const ASTNode& expr_node, EvaluationContext& context) {
	// Check complexity limit
	if (++context.step_count > context.max_steps) {
		return EvalResult::error("Constexpr evaluation exceeded complexity limit (infinite loop?)");
	}

	// Evaluate a constant expression
	// Returns the result or an error if not a constant expression

	// The expr_node should be an ExpressionNode variant
	if (!expr_node.is<ExpressionNode>()) {
		return EvalResult::error("AST node is not an expression");
	}

	const ExpressionNode& expr = expr_node.as<ExpressionNode>();
	
	// Debug logging - show what type of expression we're evaluating
	FLASH_LOG(ConstExpr, Trace, "ConstExpr::evaluate: expr index=", expr.index());

	// Check what type of expression it is
	if (const auto* bool_literal = std::get_if<BoolLiteralNode>(&expr)) {
		EvalResult result = EvalResult::from_bool(bool_literal->value());
		result.set_exact_type(TypeSpecifierNode(Type::Bool, TypeQualifier::None, 8));
		return result;
	}

	if (const auto* numeric_literal = std::get_if<NumericLiteralNode>(&expr)) {
		return evaluate_numeric_literal(*numeric_literal);
	}

	// For BinaryOperatorNode, we need to check if it's in the variant
	if (const auto* bin_op = std::get_if<BinaryOperatorNode>(&expr)) {
		return evaluate_binary_operator(bin_op->get_lhs(), bin_op->get_rhs(), bin_op->op(), context);
	}

	// For UnaryOperatorNode
	if (const auto* unary_op = std::get_if<UnaryOperatorNode>(&expr)) {
		return evaluate_unary_operator(unary_op->get_operand(), unary_op->op(), context);
	}

	// For SizeofExprNode
	if (const auto* sizeof_expr = std::get_if<SizeofExprNode>(&expr)) {
		return evaluate_sizeof(*sizeof_expr, context);
	}
	
	// For SizeofPackNode (sizeof... operator)
	if (std::holds_alternative<SizeofPackNode>(expr)) {
		const auto& sizeof_pack = std::get<SizeofPackNode>(expr);
		std::string_view pack_name = sizeof_pack.pack_name();
		
		// Try to get pack size from the parser's pack parameter info
		if (context.parser) {
			auto pack_size = context.parser->get_pack_size(pack_name);
			if (pack_size.has_value()) {
				return EvalResult::from_int(static_cast<long long>(*pack_size));
			}
			// Also check class template pack context
			auto class_pack_size = context.parser->get_class_template_pack_size(pack_name);
			if (class_pack_size.has_value()) {
				return EvalResult::from_int(static_cast<long long>(*class_pack_size));
			}
			return EvalResult::error("sizeof... requires template instantiation context for pack: " + std::string(pack_name), EvalErrorType::TemplateDependentExpression);
		}
		
		return EvalResult::error("sizeof... operator requires template context");
	}

	// For AlignofExprNode
	if (const auto* alignof_expr = std::get_if<AlignofExprNode>(&expr)) {
		return evaluate_alignof(*alignof_expr, context);
	}

	// For OffsetofExprNode
	if (const auto* offsetof_expr = std::get_if<OffsetofExprNode>(&expr)) {
		return evaluate_offsetof(*offsetof_expr);
	}

	// For NoexceptExprNode
	if (const auto* noexcept_expr = std::get_if<NoexceptExprNode>(&expr)) {
		return evaluate_noexcept_expr(*noexcept_expr, context);
	}

	// For ConstructorCallNode (type conversions like float(3.14), int(100))
	if (const auto* constructor_call = std::get_if<ConstructorCallNode>(&expr)) {
		return evaluate_constructor_call(*constructor_call, context);
	}

	// For IdentifierNode (variable references like 'x' in 'constexpr int y = x + 1;')
	if (const auto* identifier = std::get_if<IdentifierNode>(&expr)) {
		return evaluate_identifier(*identifier, context);
	}

	// For TemplateParameterReferenceNode (references to template parameters like 'T' or 'N')
	if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
		const auto& template_param = std::get<TemplateParameterReferenceNode>(expr);
		// Template parameters cannot be evaluated at template definition time
		// This is a template-dependent expression that needs to be deferred
		return EvalResult::error("Template parameter in constant expression: " + 
		                         std::string(StringTable::getStringView(template_param.param_name())),
		                         EvalErrorType::TemplateDependentExpression);
	}

	// For TernaryOperatorNode (condition ? true_expr : false_expr)
	if (const auto* ternary_operator = std::get_if<TernaryOperatorNode>(&expr)) {
		return evaluate_ternary_operator(*ternary_operator, context);
	}

	// For FunctionCallNode (constexpr function calls)
	if (const auto* function_call = std::get_if<FunctionCallNode>(&expr)) {
		return evaluate_function_call(*function_call, context);
	}

	// For LambdaExpressionNode (callable lambda values)
	if (const auto* lambda_expression = std::get_if<LambdaExpressionNode>(&expr)) {
		return materialize_lambda_value(*lambda_expression, context);
	}

	// For QualifiedIdentifierNode (e.g., Template<T>::member)
	if (const auto* qualified_identifier = std::get_if<QualifiedIdentifierNode>(&expr)) {
		return evaluate_qualified_identifier(*qualified_identifier, context);
	}

	// For MemberAccessNode (e.g., obj.member or ptr->member)
	if (const auto* member_access = std::get_if<MemberAccessNode>(&expr)) {
		return evaluate_member_access(*member_access, context);
	}

	// For MemberFunctionCallNode (e.g., obj.method() in constexpr context)
	if (const auto* member_function_call = std::get_if<MemberFunctionCallNode>(&expr)) {
		return evaluate_member_function_call(*member_function_call, context);
	}

	// For StaticCastNode (static_cast<Type>(expr) and C-style casts)
	if (const auto* static_cast_node = std::get_if<StaticCastNode>(&expr)) {
		return evaluate_static_cast(*static_cast_node, context);
	}

	// For ArraySubscriptNode (e.g., arr[0] or obj.data[1])
	if (const auto* array_subscript = std::get_if<ArraySubscriptNode>(&expr)) {
		return evaluate_array_subscript(*array_subscript, context);
	}

	// For TypeTraitExprNode (e.g., __is_void(int), __is_constant_evaluated())
	if (const auto* type_trait_expr = std::get_if<TypeTraitExprNode>(&expr)) {
		return evaluate_type_trait(*type_trait_expr);
	}

	// For FoldExpressionNode (e.g., (args && ...))
	// Fold expressions depend on template parameter packs and must be evaluated during template instantiation
	if (std::holds_alternative<FoldExpressionNode>(expr)) {
		return EvalResult::error("Fold expression requires template instantiation context",
		                         EvalErrorType::TemplateDependentExpression);
	}

	// For PackExpansionExprNode (e.g., args...)
	// Pack expansions depend on template parameter packs and must be evaluated during template instantiation
	if (std::holds_alternative<PackExpansionExprNode>(expr)) {
		return EvalResult::error("Pack expansion requires template instantiation context",
		                         EvalErrorType::TemplateDependentExpression);
	}

	// Other expression types are not supported as constant expressions yet
	return EvalResult::error("Expression type not supported in constant expressions");
}

// Internal evaluation methods for different node types
EvalResult Evaluator::evaluate_numeric_literal(const NumericLiteralNode& literal) {
	const auto& value = literal.value();
	const TypeSpecifierNode literal_type(literal.type(), literal.qualifier(), literal.sizeInBits());

	if (std::holds_alternative<unsigned long long>(value)) {
		unsigned long long val = std::get<unsigned long long>(value);
		EvalResult result = is_unsigned_integer_type(literal.type())
			? EvalResult::from_uint(val)
			: EvalResult::from_int(static_cast<long long>(val));
		result.set_exact_type(literal_type);
		return result;
	} else if (const auto* d_val = std::get_if<double>(&value)) {
		double val = *d_val;
		EvalResult result = EvalResult::from_double(val);
		result.set_exact_type(literal_type);
		return result;
	}

	return EvalResult::error("Unknown numeric literal type");
}

EvalResult Evaluator::evaluate_binary_operator(const ASTNode& lhs_node, const ASTNode& rhs_node,
	std::string_view op, EvaluationContext& context) {
	// TODO: short-circuit && / || — see docs/CONSTEXPR_LIMITATIONS.md

	// Recursively evaluate left and right operands
	auto lhs_result = evaluate(lhs_node, context);
	auto rhs_result = evaluate(rhs_node, context);

	if (!lhs_result.success()) {
		return lhs_result;
	}
	if (!rhs_result.success()) {
		return rhs_result;
	}

	return apply_binary_op(lhs_result, rhs_result, op, &context);
}

EvalResult Evaluator::evaluate_unary_operator(const ASTNode& operand_node, std::string_view op,
	EvaluationContext& context) {
	// Handle address-of (&) without evaluating the operand: the result is a pointer to the named variable.
	if (op == "&") {
		if (operand_node.is<ExpressionNode>()) {
			const ExpressionNode& expr = operand_node.as<ExpressionNode>();
			if (const auto* id = std::get_if<IdentifierNode>(&expr)) {
				return EvalResult::from_pointer(id->name());
			}
			// &arr[i]: address of array element → pointer with offset
			if (const auto* subscript = std::get_if<ArraySubscriptNode>(&expr)) {
				std::string_view arr_name = getIdentifierNameFromAstNode(subscript->array_expr());
				if (!arr_name.empty()) {
					auto index_result = evaluate(subscript->index_expr(), context);
					if (!index_result.success()) return index_result;
					return EvalResult::from_pointer(arr_name, index_result.as_int());
				}
			}
		}
		return EvalResult::error("Address-of operator (&) is only supported on named variables and array elements in constant expressions");
	}

	// Recursively evaluate operand
	auto operand_result = evaluate(operand_node, context);

	if (!operand_result.success()) {
		return operand_result;
	}

	// Handle dereference (*): if the operand is a constexpr pointer, look up and evaluate the target variable.
	if (op == "*") {
		if (operand_result.pointer_to_var.isValid()) {
			return dereference_constexpr_pointer(
				StringTable::getStringView(operand_result.pointer_to_var),
				context, operand_result.pointer_offset);
		}
		return EvalResult::error("Dereference operator (*) on a non-pointer value in constant expressions");
	}

	return apply_unary_op(operand_result, op);
}

// Look up the named constexpr variable and evaluate it (used to dereference constexpr pointers).
// When offset != 0, the variable must be an array and we dereference element [offset].
// When offset == 0 and the variable is an array, element [0] is returned.
EvalResult Evaluator::dereference_constexpr_pointer(std::string_view var_name, EvaluationContext& context, int64_t offset) {
	if (!context.symbols) {
		return EvalResult::error("Cannot dereference constexpr pointer: no symbol table available");
	}
	auto symbol = context.symbols->lookup(var_name);
	if (!symbol.has_value() && context.global_symbols) {
		symbol = context.global_symbols->lookup(var_name);
	}
	if (!symbol.has_value()) {
		return EvalResult::error("Cannot dereference constexpr pointer: variable '" + std::string(var_name) + "' not found");
	}
	// The symbol should be a VariableDeclarationNode; evaluate its initializer.
	if (!symbol->is<VariableDeclarationNode>()) {
		return EvalResult::error("Cannot dereference constexpr pointer: '" + std::string(var_name) + "' is not a variable");
	}
	const VariableDeclarationNode& var_decl = symbol->as<VariableDeclarationNode>();
	if (!var_decl.is_constexpr()) {
		return EvalResult::error("Cannot dereference pointer to non-constexpr variable: " + std::string(var_name));
	}
	const auto& initializer = var_decl.initializer();
	if (!initializer.has_value()) {
		return EvalResult::error("Cannot dereference constexpr pointer: variable '" + std::string(var_name) + "' has no initializer");
	}

	// Reject negative pointer offsets regardless of variable type.
	if (offset < 0) {
		return EvalResult::error("Negative pointer offset " + std::to_string(offset) + " in constant expression");
	}

	// Check if the target variable is an array — if so, always use array element access.
	bool is_array_var = var_decl.declaration().is_array();

	if (is_array_var) {
		// Handle InitializerListNode directly (arrays store their initializer as
		// InitializerListNode, not ExpressionNode, so evaluate() would reject it).
		if (initializer->is<InitializerListNode>()) {
			const auto& init_list = initializer->as<InitializerListNode>();
			const auto& elements = init_list.initializers();
			if (static_cast<size_t>(offset) >= elements.size()) {
				return EvalResult::error("Pointer dereference at offset " + std::to_string(offset) +
					" out of bounds (size " + std::to_string(elements.size()) + ")", EvalErrorType::NotConstantExpression);
			}
			return evaluate(elements[static_cast<size_t>(offset)], context);
		}

		// For other array forms, materialize then index.
		EvalResult arr_result = evaluate(initializer.value(), context);
		if (!arr_result.success()) return arr_result;
		if (arr_result.is_array) {
			if (!arr_result.array_elements.empty()) {
				if (static_cast<size_t>(offset) >= arr_result.array_elements.size()) {
					return EvalResult::error("Pointer dereference at offset " + std::to_string(offset) +
						" out of bounds (array size " + std::to_string(arr_result.array_elements.size()) + ")", EvalErrorType::NotConstantExpression);
				}
				return arr_result.array_elements[static_cast<size_t>(offset)];
			}
			if (!arr_result.array_values.empty()) {
				if (static_cast<size_t>(offset) >= arr_result.array_values.size()) {
					return EvalResult::error("Pointer dereference at offset " + std::to_string(offset) +
						" out of bounds (array size " + std::to_string(arr_result.array_values.size()) + ")", EvalErrorType::NotConstantExpression);
				}
				return EvalResult::from_int(arr_result.array_values[static_cast<size_t>(offset)]);
			}
		}
		return EvalResult::error("Cannot dereference pointer with offset: variable '" + std::string(var_name) + "' is not evaluable as an array");
	}

	// Non-array variable: only offset 0 is valid (pointer to scalar)
	if (offset != 0) {
		return EvalResult::error("Cannot dereference pointer with non-zero offset on non-array variable '" + std::string(var_name) + "'");
	}

	EvalResult result = evaluate(initializer.value(), context);
	if (result.success()) {
		maybe_set_binding_result_exact_type(result, var_decl.declaration(), &initializer.value(), context);
	}
	return result;
}

// Dereference a pointer (EvalResult with pointer_to_var set) against local bindings first,
// then the symbol table.  Handles scalars (offset == 0) and arrays (any offset).
// This is the preferred deref helper for all bindings-aware evaluation paths.
EvalResult Evaluator::deref_pointer_with_bindings(
	const EvalResult& ptr,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	std::string_view var_name = StringTable::getStringView(ptr.pointer_to_var);
	int64_t offset = ptr.pointer_offset;

	// Check local bindings first (handles local scalars and arrays at any offset).
	auto it = bindings.find(var_name);
	if (it != bindings.end()) {
		const EvalResult& bound = it->second;
		if (bound.is_array) {
			if (offset < 0) return EvalResult::error("Negative pointer offset in dereference");
			size_t idx = static_cast<size_t>(offset);
			if (!bound.array_elements.empty()) {
				if (idx >= bound.array_elements.size())
					return EvalResult::error("Array index out of bounds in constant expression");
				return bound.array_elements[idx];
			}
			if (!bound.array_values.empty()) {
				if (idx >= bound.array_values.size())
					return EvalResult::error("Array index out of bounds in constant expression");
				return EvalResult::from_int(bound.array_values[idx]);
			}
		} else if (offset == 0) {
			return bound;
		} else {
			return EvalResult::error("Cannot dereference pointer with non-zero offset on non-array variable '" + std::string(var_name) + "'");
		}
	}
	// Check for a value snapshot stored in the pointer EvalResult.
	// For scalar pointers: array_elements = {pointed_value}, offset = 0.
	// For array-element pointers: array_elements = {element_at_offset}, any offset.
	// In both cases, array_elements[0] is exactly what this pointer dereferences to.
	if (!ptr.array_elements.empty()) {
		return ptr.array_elements[0];
	}
	return dereference_constexpr_pointer(var_name, context, offset);
}

EvalResult Evaluator::evaluate_sizeof(const SizeofExprNode& sizeof_expr, EvaluationContext& context) {
	// sizeof is always a constant expression
	// Get the actual size from the type
	if (sizeof_expr.is_type()) {
		// sizeof(type) - get size from TypeSpecifierNode
		const auto& type_node = sizeof_expr.type_or_expr();
		if (type_node.is<TypeSpecifierNode>()) {
			const auto& type_spec = type_node.as<TypeSpecifierNode>();
			
			// Workaround for parser limitation: when sizeof(arr) is parsed where arr is an
			// array variable, the parser may incorrectly parse it as a type.
			// If size_in_bits is 0, try looking up the identifier in the symbol table.
			if (type_spec.size_in_bits() == 0 && type_spec.token().type() == Token::Type::Identifier && context.symbols) {
				std::string_view identifier = type_spec.token().value();
				
				// Look up the identifier in the symbol table (local first, then global)
				std::optional<ASTNode> symbol = context.symbols->lookup(identifier);
				if (!symbol.has_value() && context.global_symbols) {
					symbol = context.global_symbols->lookup(identifier);
				}
				if (symbol.has_value()) {
					const DeclarationNode* decl = get_decl_from_symbol(*symbol);
					if (decl) {
						// Check if it's an array
						if (decl->is_array()) {
							const auto& array_type_spec = decl->type_node().as<TypeSpecifierNode>();
							size_t element_size = get_typespec_size_bytes(array_type_spec);
							
							// Get total array size from all dimensions
							const auto& dims = decl->array_dimensions();
							if (!dims.empty()) {
								long long total_count = 1;
								bool all_evaluated = true;
								for (const auto& dim_expr : dims) {
									auto eval_result = evaluate(dim_expr, context);
									if (eval_result.success() && eval_result.as_int() > 0) {
										total_count *= eval_result.as_int();
									} else {
										all_evaluated = false;
										break;
									}
								}
								if (all_evaluated && element_size > 0) {
									return EvalResult::from_int(static_cast<long long>(element_size * total_count));
								}
							}
						}
						
						// Not an array, just return the variable's type size
						const auto& var_type = decl->type_node().as<TypeSpecifierNode>();
						size_t var_size = get_typespec_size_bytes(var_type);
						if (var_size > 0) {
							return EvalResult::from_int(static_cast<long long>(var_size));
						}
					}
				}
				
				// If not found in symbol table and we're in a template class member function,
				// try to resolve as a template parameter from the struct name
				if (!symbol.has_value() && context.struct_info) {
					std::string_view struct_name = StringTable::getStringView(context.struct_info->getName());
					
					// Parse the struct name to extract template arguments
					// e.g., "Container_int" -> T = int (4 bytes), "Processor_char" -> T = char (1 byte)
					// For variadic templates like "List_int_char", try all arguments in order
					// Pointer types have "P" suffix: "Container_intP" -> T = int* (8 bytes)
					// Reference types have "R" or "RR" suffix: "Container_intR" -> T = int& (sizeof returns size of int)
					
					// Find the first underscore (start of template arguments)
					size_t first_underscore = struct_name.find('_');
					if (first_underscore != std::string_view::npos && first_underscore + 1 < struct_name.size()) {
						// Extract all template arguments by splitting on underscores
						std::vector<std::string_view> template_args;
						size_t start = first_underscore + 1;
						while (start < struct_name.size()) {
							size_t next = struct_name.find('_', start);
							if (next == std::string_view::npos) {
								template_args.push_back(struct_name.substr(start));
								break;
							} else {
								template_args.push_back(struct_name.substr(start, next - start));
								start = next + 1;
							}
						}
						
						// Try each template argument in order until we find one with a valid size
						// For templates like List<Tp, Up...>, the first argument corresponds to Tp
						for (const auto& type_suffix_raw : template_args) {
							std::string_view type_suffix = type_suffix_raw;
						
						// Strip CV qualifier prefixes ('C' for const, 'V' for volatile)
						// TemplateTypeArg::toString() adds CV qualifiers as prefixes (e.g., "Cint" for const int)
						// sizeof(const T) and sizeof(volatile T) return the same size as sizeof(T)
						while (!type_suffix.empty() && (type_suffix.front() == 'C' || type_suffix.front() == 'V')) {
							type_suffix = type_suffix.substr(1);
						}
						
						// Check for reference types (suffix ends with 'R' or 'RR')
						// TemplateTypeArg::toString() appends "R" for lvalue reference, "RR" for rvalue reference
						// sizeof(T&) and sizeof(T&&) return the size of T, not the size of the reference itself
						if (type_suffix.size() >= 2 && type_suffix.ends_with("RR")) {
							// Rvalue reference - strip "RR" and get base type size
							type_suffix = type_suffix.substr(0, type_suffix.size() - 2);
						} else if (!type_suffix.empty() && type_suffix.back() == 'R') {
							// Lvalue reference - strip "R" and get base type size
							type_suffix = type_suffix.substr(0, type_suffix.size() - 1);
						}
						
						// Check for pointer types (suffix ends with 'P')
						// TemplateTypeArg::toString() appends 'P' for each pointer level
						// e.g., "intP" for int*, "intPP" for int**, etc.
						if (!type_suffix.empty() && type_suffix.back() == 'P') {
							// All pointers are 8 bytes on x64
							return EvalResult::from_int(8);
						}
						
						// Check for array types (suffix contains 'A')
						// Arrays are like "intA[10]" - sizeof(array) = element_size * element_count
						size_t array_pos = type_suffix.find('A');
						if (array_pos != std::string_view::npos) {
							// Extract base type and array dimensions
							std::string_view base_type = type_suffix.substr(0, array_pos);
							std::string_view array_part = type_suffix.substr(array_pos + 1); // Skip 'A'
							
							// Strip CV qualifiers from base_type (already stripped from type_suffix earlier, but double-check)
							while (!base_type.empty() && (base_type.front() == 'C' || base_type.front() == 'V')) {
								base_type = base_type.substr(1);
							}
							
							// Parse array dimensions like "[10]" or "[]"
							if (array_part.starts_with('[') && array_part.ends_with(']')) {
								std::string_view dimensions = array_part.substr(1, array_part.size() - 2);
								if (!dimensions.empty()) {
									// Parse the dimension as a number
									size_t array_count = 0;
									auto result = std::from_chars(dimensions.data(), dimensions.data() + dimensions.size(), array_count);
									if (result.ec == std::errc{} && array_count > 0) {
										// Get base type size
										size_t base_size = 0;
										
										// Check if base_type is a pointer (ends with 'P')
										// e.g., "intP" for int*, "charPP" for char**, etc.
										if (!base_type.empty() && base_type.back() == 'P') {
											// All pointers are 8 bytes on x64
											base_size = 8;
										} else {
											// Look up non-pointer base type size
											if (base_type == "int") base_size = 4;
											else if (base_type == "char") base_size = 1;
											else if (base_type == "short") base_size = 2;
											else if (base_type == "long") base_size = get_long_size_bits() / 8;
											else if (base_type == "float") base_size = 4;
											else if (base_type == "double") base_size = 8;
											else if (base_type == "bool") base_size = 1;
											else if (base_type == "uint") base_size = 4;
											else if (base_type == "uchar") base_size = 1;
											else if (base_type == "ushort") base_size = 2;
											else if (base_type == "ulong") base_size = get_long_size_bits() / 8;
											else if (base_type == "ulonglong") base_size = 8;
											else if (base_type == "longlong") base_size = 8;
										}
										
										if (base_size > 0) {
											return EvalResult::from_int(static_cast<long long>(base_size * array_count));
										}
									}
								}
							}
							// Failed to parse array dimensions - fall through
						} else {
							// Map common type suffixes to their sizes
							// Note: Must match the output of TemplateTypeArg::toString() in TemplateRegistry.h
							// This logic is duplicated in CodeGen.h::resolveTemplateSizeFromStructName
							size_t param_size_bytes = 0;
							if (type_suffix == "int") param_size_bytes = 4;
							else if (type_suffix == "char") param_size_bytes = 1;
							else if (type_suffix == "short") param_size_bytes = 2;
							else if (type_suffix == "long") param_size_bytes = get_long_size_bits() / 8;
							else if (type_suffix == "float") param_size_bytes = 4;
							else if (type_suffix == "double") param_size_bytes = 8;
							else if (type_suffix == "bool") param_size_bytes = 1;
							else if (type_suffix == "uint") param_size_bytes = 4;
							else if (type_suffix == "uchar") param_size_bytes = 1;
							else if (type_suffix == "ushort") param_size_bytes = 2;
							else if (type_suffix == "ulong") param_size_bytes = get_long_size_bits() / 8;
							else if (type_suffix == "ulonglong") param_size_bytes = 8;
							else if (type_suffix == "longlong") param_size_bytes = 8;
							
							if (param_size_bytes > 0) {
								return EvalResult::from_int(static_cast<long long>(param_size_bytes));
							}
						}
						}  // End of for loop over template_args
					}
				}
			}
			
			// size_in_bits() returns bits, convert to bytes
			unsigned long long size_in_bytes = get_typespec_size_bytes(type_spec);
			// sizeof never returns 0 in valid C++ (sizeof(char) == 1, all complete types >= 1).
			// A zero result indicates an incomplete or template-dependent type.
			// Before returning an error, try context.template_param_names (e.g., T=int from Box<int>).
			if (size_in_bytes == 0 && !context.template_param_names.empty()) {
				std::string_view type_name = type_spec.token().value();
				for (size_t i = 0; i < context.template_param_names.size() && i < context.template_args.size(); ++i) {
					if (context.template_param_names[i] == type_name) {
						const TemplateTypeArg& arg = context.template_args[i];
						if (arg.isTypeArgument()) {
							size_t param_size = get_type_size_bits(arg.base_type) / 8;
							if (param_size == 0 && arg.base_type == Type::Struct && arg.type_index.is_valid() &&
									arg.type_index.value < gTypeInfo.size()) {
								const StructTypeInfo* si = gTypeInfo[arg.type_index.value].getStructInfo();
								if (si) param_size = si->total_size;
							}
							if (param_size > 0) {
								return EvalResult::from_int(static_cast<long long>(param_size));
							}
						}
						break;
					}
				}
			}
			if (size_in_bytes == 0) {
				return EvalResult::error(
					"sizeof evaluated to 0 for type '" + std::string(type_spec.token().value()) + "' (incomplete or dependent type)",
					EvalErrorType::TemplateDependentExpression);
			}
			return EvalResult::from_int(static_cast<long long>(size_in_bytes));
		}
	}
	else {
		// sizeof(expression) - determine the size from the expression's type
		const auto& expr_node = sizeof_expr.type_or_expr();
		if (expr_node.is<ExpressionNode>()) {
			const ExpressionNode& expr = expr_node.as<ExpressionNode>();
			
			// Handle identifier - get type from its declaration
			if (std::holds_alternative<IdentifierNode>(expr)) {
				const auto& id_node = std::get<IdentifierNode>(expr);
				
				// Look up the identifier in the symbol table (local first, then global)
				if (context.symbols) {
					auto symbol = context.symbols->lookup(id_node.name());
					if (!symbol.has_value() && context.global_symbols) {
						symbol = context.global_symbols->lookup(id_node.name());
					}
					if (symbol.has_value()) {
						// Get the declaration and extract the type
						const DeclarationNode* decl = get_decl_from_symbol(*symbol);
						
						if (decl) {
							// Check if it's an array - if so, calculate total size
							if (decl->is_array()) {
								const auto& type_spec = decl->type_node().as<TypeSpecifierNode>();
								size_t element_size = get_typespec_size_bytes(type_spec);
								
								// Get total array size from all dimensions
								const auto& dims = decl->array_dimensions();
								if (!dims.empty()) {
									long long total_count = 1;
									bool all_evaluated = true;
									for (const auto& dim_expr : dims) {
										auto eval_result = evaluate(dim_expr, context);
										if (eval_result.success() && eval_result.as_int() > 0) {
											total_count *= eval_result.as_int();
										} else {
											all_evaluated = false;
											break;
										}
									}
									if (all_evaluated && element_size > 0) {
										return EvalResult::from_int(static_cast<long long>(element_size * total_count));
									}
								}
							}
							
							const auto& type_node = decl->type_node();
							if (type_node.is<TypeSpecifierNode>()) {
								const auto& type_spec = type_node.as<TypeSpecifierNode>();
								unsigned long long size_in_bytes = get_typespec_size_bytes(type_spec);
								return EvalResult::from_int(static_cast<long long>(size_in_bytes));
							}
						}
					}
				}
				
				// If we couldn't look up the identifier, return error
				return EvalResult::error("sizeof: identifier not found in symbol table");
			}
			
			// For numeric literals, we can determine the size from the literal itself
			if (const auto* lit = std::get_if<NumericLiteralNode>(&expr)) {
				unsigned long long size_in_bytes = lit->sizeInBits() / 8;
				return EvalResult::from_int(static_cast<long long>(size_in_bytes));
			}
			
			// Handle array subscript: sizeof(arr[index])
			// For single dimension: returns element size
			// For multidimensional (e.g. int arr[3][4]): sizeof(arr[0]) returns sizeof(int[4]) = 16
			if (std::holds_alternative<ArraySubscriptNode>(expr)) {
				const auto& array_subscript = std::get<ArraySubscriptNode>(expr);
				const ASTNode& array_expr_node = array_subscript.array_expr();
				
				// Check if the array expression is an identifier
				if (array_expr_node.is<ExpressionNode>()) {
					const ExpressionNode& array_expr = array_expr_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(array_expr)) {
						const auto& id_node = std::get<IdentifierNode>(array_expr);
						
						// Look up the array identifier in the symbol table
						if (context.symbols) {
							auto symbol = context.symbols->lookup(id_node.name());
							if (symbol.has_value()) {
								const DeclarationNode* decl = get_decl_from_symbol(*symbol);
								if (decl && decl->is_array()) {
									const auto& array_type_spec = decl->type_node().as<TypeSpecifierNode>();
									size_t element_size = get_typespec_size_bytes(array_type_spec);
									
									// For multidimensional arrays, calculate sub-array size
									const auto& dims = decl->array_dimensions();
									if (dims.size() > 1) {
										// Calculate size of sub-array: element_size * product of dims[1..]
										long long sub_array_count = 1;
										bool all_evaluated = true;
										for (size_t i = 1; i < dims.size(); ++i) {
											auto eval_result = evaluate(dims[i], context);
											if (eval_result.success() && eval_result.as_int() > 0) {
												sub_array_count *= eval_result.as_int();
											} else {
												all_evaluated = false;
												break;
											}
										}
										if (all_evaluated && element_size > 0) {
											return EvalResult::from_int(static_cast<long long>(element_size * sub_array_count));
										}
									} else {
										// Single dimension array, return element size
										if (element_size > 0) {
											return EvalResult::from_int(static_cast<long long>(element_size));
										}
									}
								}
							}
						}
					}
				}
			}
			
			// For other expressions, we would need full type inference
			// which requires tracking expression types through the AST
			// This is a compiler limitation, not a C++20 limitation
			return EvalResult::error("sizeof with complex expression not yet supported in constexpr");
		}
	}
	
	return EvalResult::error("Invalid sizeof operand");
}

EvalResult Evaluator::evaluate_alignof(const AlignofExprNode& alignof_expr, EvaluationContext& context) {
	// alignof is always a constant expression
	// Get the actual alignment from the type
	if (alignof_expr.is_type()) {
		// alignof(type) - get alignment from TypeSpecifierNode
		const auto& type_node = alignof_expr.type_or_expr();
		if (type_node.is<TypeSpecifierNode>()) {
			const auto& type_spec = type_node.as<TypeSpecifierNode>();
			
			// For struct types, look up alignment from type info
			if (type_spec.type() == Type::Struct) {
				size_t type_index = type_spec.type_index().value;
				if (type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_index];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						return EvalResult::from_int(static_cast<long long>(struct_info->alignment));
					}
				}
				return EvalResult::error("Struct alignment not available");
			}
			
			// For primitive types, use standard alignment calculation
			int size_bits = type_spec.size_in_bits();
			if (size_bits == 0) {
				size_bits = get_type_size_bits(type_spec.type());
			}
			size_t size_in_bytes = size_bits / 8;
			size_t alignment = calculate_alignment_from_size(size_in_bytes, type_spec.type());
			
			return EvalResult::from_int(static_cast<long long>(alignment));
		}
	}
	else {
		// alignof(expression) - determine the alignment from the expression's type
		const auto& expr_node = alignof_expr.type_or_expr();
		if (expr_node.is<ExpressionNode>()) {
			const ExpressionNode& expr = expr_node.as<ExpressionNode>();
			
			// Handle identifier - get type from its declaration
			if (std::holds_alternative<IdentifierNode>(expr)) {
				const auto& id_node = std::get<IdentifierNode>(expr);
				
				// Look up the identifier in the symbol table
				if (context.symbols) {
					auto symbol = context.symbols->lookup(id_node.name());
					if (symbol.has_value()) {
						// Get the declaration and extract the type
						const DeclarationNode* decl = get_decl_from_symbol(*symbol);
						
						if (decl) {
							const auto& type_node = decl->type_node();
							if (type_node.is<TypeSpecifierNode>()) {
								const auto& type_spec = type_node.as<TypeSpecifierNode>();
								
								// Handle struct types
								if (type_spec.type() == Type::Struct) {
									size_t type_index = type_spec.type_index().value;
									if (type_index < gTypeInfo.size()) {
										const TypeInfo& type_info = gTypeInfo[type_index];
										const StructTypeInfo* struct_info = type_info.getStructInfo();
										if (struct_info) {
											return EvalResult::from_int(static_cast<long long>(struct_info->alignment));
										}
									}
								}
								
								// For primitive types
								int size_bits = type_spec.size_in_bits();
								if (size_bits == 0) {
									size_bits = get_type_size_bits(type_spec.type());
								}
								size_t size_in_bytes = size_bits / 8;
								size_t alignment = calculate_alignment_from_size(size_in_bytes, type_spec.type());
								
								return EvalResult::from_int(static_cast<long long>(alignment));
							}
						}
					}
				}
				
				// If we couldn't look up the identifier, return error
				return EvalResult::error("alignof: identifier not found in symbol table");
			}
			
			// For other expressions, return error
			return EvalResult::error("alignof with complex expression not yet supported in constexpr");
		}
	}
	
	return EvalResult::error("Invalid alignof operand");
}

EvalResult Evaluator::evaluate_offsetof(const OffsetofExprNode& offsetof_expr) {
	const ASTNode& type_node = offsetof_expr.type_node();
	if (!type_node.is<TypeSpecifierNode>()) {
		return EvalResult::error("offsetof type argument must be TypeSpecifierNode");
	}

	const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
	if (type_spec.type() != Type::Struct) {
		return EvalResult::error("offsetof requires a struct type");
	}

	TypeIndex type_index = type_spec.type_index();
	if (type_index.value >= gTypeInfo.size()) {
		return EvalResult::error("Invalid type index for struct");
	}

	auto path_result = FlashCpp::resolveOffsetofMemberPath(type_index, offsetof_expr.member_path());
	if (!path_result.success()) {
		return EvalResult::error(path_result.error_message);
	}

	return EvalResult::from_uint(static_cast<unsigned long long>(path_result.total_offset));
}

EvalResult Evaluator::evaluate_noexcept_expr(const NoexceptExprNode& noexcept_expr, EvaluationContext& context) {
	if (!noexcept_expr.expr().is<ExpressionNode>()) {
		return EvalResult::from_bool(false);
	}

	return EvalResult::from_bool(is_expression_noexcept(noexcept_expr.expr().as<ExpressionNode>(), context));
}

bool Evaluator::is_function_decl_noexcept(const FunctionDeclarationNode& func_decl, EvaluationContext& context) {
	if (!func_decl.is_noexcept()) {
		return false;
	}

	if (!func_decl.has_noexcept_expression()) {
		return true;
	}

	auto eval_result = evaluate(*func_decl.noexcept_expression(), context);
	if (!eval_result.success()) {
		return false;
	}

	return eval_result.as_bool();
}

const FunctionDeclarationNode* Evaluator::resolve_function_call_decl(const FunctionCallNode& func_call, EvaluationContext& context) {
	StringHandle function_name_handle = func_call.function_declaration().identifier_token().handle();
	auto current_match = find_current_struct_member_function_candidate(
		function_name_handle,
		func_call.arguments().size(),
		context,
		MemberFunctionLookupMode::LookupOnly,
		false,
		true);
	if (current_match.function) {
		return current_match.function;
	}

	if (!context.symbols) {
		return nullptr;
	}

	auto lookup_function = [&](const SymbolTable* symbols, StringHandle name_handle) -> const FunctionDeclarationNode* {
		if (!symbols || !name_handle.isValid()) {
			return nullptr;
		}

		auto symbol = symbols->lookup(name_handle);
		if (!symbol.has_value()) {
			return nullptr;
		}

		return get_function_decl_node(*symbol);
	};

	if (func_call.has_qualified_name()) {
		if (const FunctionDeclarationNode* qualified_decl = lookup_function(
				context.symbols,
				StringTable::getOrInternStringHandle(func_call.qualified_name()))) {
			return qualified_decl;
		}
	}

	if (const FunctionDeclarationNode* direct_decl = lookup_function(context.symbols, function_name_handle)) {
		return direct_decl;
	}

	if (context.global_symbols && context.global_symbols != context.symbols) {
		if (func_call.has_qualified_name()) {
			if (const FunctionDeclarationNode* qualified_decl = lookup_function(
					context.global_symbols,
					StringTable::getOrInternStringHandle(func_call.qualified_name()))) {
				return qualified_decl;
			}
		}

		if (const FunctionDeclarationNode* global_decl = lookup_function(context.global_symbols, function_name_handle)) {
			return global_decl;
		}
	}

	return nullptr;
}

bool Evaluator::is_expression_noexcept(const ExpressionNode& expr, EvaluationContext& context) {
	if (context.current_depth >= context.max_recursion_depth) {
		return false;
	}

	struct NoexceptDepthGuard {
		EvaluationContext& context;

		explicit NoexceptDepthGuard(EvaluationContext& eval_context)
			: context(eval_context) {
			++context.current_depth;
		}

		~NoexceptDepthGuard() {
			--context.current_depth;
		}
	} depth_guard(context);

	if (std::holds_alternative<BoolLiteralNode>(expr) ||
		std::holds_alternative<NumericLiteralNode>(expr) ||
		std::holds_alternative<StringLiteralNode>(expr)) {
		return true;
	}

	if (std::holds_alternative<IdentifierNode>(expr) ||
		std::holds_alternative<QualifiedIdentifierNode>(expr) ||
		std::holds_alternative<TemplateParameterReferenceNode>(expr) ||
		std::holds_alternative<MemberAccessNode>(expr) ||
		std::holds_alternative<TypeTraitExprNode>(expr) ||
		std::holds_alternative<LambdaExpressionNode>(expr) ||
		std::holds_alternative<NoexceptExprNode>(expr) ||
		std::holds_alternative<SizeofExprNode>(expr) ||
		std::holds_alternative<SizeofPackNode>(expr) ||
		std::holds_alternative<AlignofExprNode>(expr) ||
		std::holds_alternative<OffsetofExprNode>(expr)) {
		return true;
	}

	// Pseudo-destructor calls: noexcept iff the type's destructor is noexcept.
	if (const auto* pseudo_dtor = std::get_if<PseudoDestructorCallNode>(&expr)) {
		if (context.symbols) {
			return isPseudoDestructorCallNoexcept(*pseudo_dtor, *context.symbols);
		}
		return true;  // No symbol table — assume noexcept (scalar types)
	}

	if (std::holds_alternative<BinaryOperatorNode>(expr)) {
		const auto& binop = std::get<BinaryOperatorNode>(expr);
		bool lhs_noexcept = !binop.get_lhs().is<ExpressionNode>() ||
			is_expression_noexcept(binop.get_lhs().as<ExpressionNode>(), context);
		bool rhs_noexcept = !binop.get_rhs().is<ExpressionNode>() ||
			is_expression_noexcept(binop.get_rhs().as<ExpressionNode>(), context);
		return lhs_noexcept && rhs_noexcept;
	}

	if (const auto* unary = std::get_if<UnaryOperatorNode>(&expr)) {
		return !unary->get_operand().is<ExpressionNode>() ||
			is_expression_noexcept(unary->get_operand().as<ExpressionNode>(), context);
	}

	if (std::holds_alternative<TernaryOperatorNode>(expr)) {
		const auto& ternary = std::get<TernaryOperatorNode>(expr);
		bool cond_noexcept = !ternary.condition().is<ExpressionNode>() ||
			is_expression_noexcept(ternary.condition().as<ExpressionNode>(), context);
		bool true_noexcept = !ternary.true_expr().is<ExpressionNode>() ||
			is_expression_noexcept(ternary.true_expr().as<ExpressionNode>(), context);
		bool false_noexcept = !ternary.false_expr().is<ExpressionNode>() ||
			is_expression_noexcept(ternary.false_expr().as<ExpressionNode>(), context);
		return cond_noexcept && true_noexcept && false_noexcept;
	}

	if (const auto* func_call = std::get_if<FunctionCallNode>(&expr)) {
		const FunctionDeclarationNode* func_decl = resolve_function_call_decl(*func_call, context);
		return func_decl && is_function_decl_noexcept(*func_decl, context);
	}

	if (const auto* member_call = std::get_if<MemberFunctionCallNode>(&expr)) {
		return is_function_decl_noexcept(member_call->function_declaration(), context);
	}

	if (const auto* subscript = std::get_if<ArraySubscriptNode>(&expr)) {
		return !subscript->index_expr().is<ExpressionNode>() ||
			is_expression_noexcept(subscript->index_expr().as<ExpressionNode>(), context);
	}

	if (const auto* cast = std::get_if<StaticCastNode>(&expr)) {
		return !cast->expr().is<ExpressionNode>() ||
			is_expression_noexcept(cast->expr().as<ExpressionNode>(), context);
	}

	if (const auto* cast = std::get_if<ConstCastNode>(&expr)) {
		return !cast->expr().is<ExpressionNode>() ||
			is_expression_noexcept(cast->expr().as<ExpressionNode>(), context);
	}

	if (const auto* cast = std::get_if<ReinterpretCastNode>(&expr)) {
		return !cast->expr().is<ExpressionNode>() ||
			is_expression_noexcept(cast->expr().as<ExpressionNode>(), context);
	}

	if (std::holds_alternative<DynamicCastNode>(expr) ||
		std::holds_alternative<TypeidNode>(expr) ||
		std::holds_alternative<NewExpressionNode>(expr) ||
		std::holds_alternative<DeleteExpressionNode>(expr) ||
		std::holds_alternative<FoldExpressionNode>(expr) ||
		std::holds_alternative<ThrowExpressionNode>(expr) ||
		std::holds_alternative<ConstructorCallNode>(expr)) {
		return false;
	}

	return false;
}

EvalResult Evaluator::evaluate_constructor_call(const ConstructorCallNode& ctor_call, EvaluationContext& context) {
	// Constructor calls like float(3.14), int(100), double(2.718), or type_identity<int>{}
	// These are essentially type conversions/casts in constant expressions
	// Get the argument(s)
	const auto& args = ctor_call.arguments();
	
	// Get the target type
	const ASTNode& type_node = ctor_call.type_node();
	if (!type_node.is<TypeSpecifierNode>()) {
		return EvalResult::error("Constructor call without valid type specifier");
	}
	
	const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
	
	// Handle empty constructor calls (default/value initialization): Type{}
	if (args.size() == 0) {
		// For struct types, this is valid - it's default initialization
		// Return a success result with default value (0 for integers, false for bool, etc.)
		// This allows the constructor call to be used for template argument deduction
		switch (type_spec.type()) {
			case Type::Bool:
				{
					EvalResult result = EvalResult::from_bool(false);
					result.set_exact_type(type_spec);
					return result;
				}
			case Type::Char:
			case Type::Short:
			case Type::Int:
			case Type::Long:
			case Type::LongLong:
				{
					EvalResult result = EvalResult::from_int(0);
					result.set_exact_type(type_spec);
					return result;
				}
			case Type::UnsignedChar:
			case Type::UnsignedShort:
			case Type::UnsignedInt:
			case Type::UnsignedLong:
			case Type::UnsignedLongLong:
				{
					EvalResult result = EvalResult::from_uint(0);
					result.set_exact_type(type_spec);
					return result;
				}
			case Type::Float:
			case Type::Double:
			case Type::LongDouble:
				{
					EvalResult result = EvalResult::from_double(0.0);
					result.set_exact_type(type_spec);
					return result;
				}
			case Type::Struct:
			case Type::UserDefined:
				// For struct types, return a success result with value 0
				// This indicates successful default construction
				return EvalResult::from_int(0);
			default:
				return EvalResult::error("Unsupported type for default construction in constant expression");
		}
	}
	
	// For basic type conversions with 1 argument: Type(value)
	if (args.size() != 1) {
		return EvalResult::error("Constructor call must have 0 or 1 arguments for constant evaluation");
	}
	
	return evaluate_expr_node(type_spec, args[0], context, "Unsupported type in constructor call for constant evaluation");
}

EvalResult Evaluator::evaluate_static_cast(const StaticCastNode& cast_node, EvaluationContext& context) {
	// Evaluate static_cast<Type>(expr) and C-style casts in constant expressions
	
	// Get the target type
	const ASTNode& type_node = cast_node.target_type();
	if (!type_node.is<TypeSpecifierNode>()) {
		return EvalResult::error("Cast without valid type specifier");
	}
	
	const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
	
	// Evaluate the expression being cast
	return evaluate_expr_node(type_spec, cast_node.expr(), context, "Unsupported type in static_cast for constant evaluation");
}

EvalResult Evaluator::evaluate_expr_node(const TypeSpecifierNode& target_type, const ASTNode& expr, EvaluationContext& context, const char* invalidTypeErrorStr) {
	auto expr_result = evaluate(expr, context);
	if (!expr_result.success()) {
		return expr_result;
	}
	
	// Perform the type conversion
	switch (target_type.type()) {
		case Type::Bool:
		{
			EvalResult result = EvalResult::from_bool(expr_result.as_bool());
			result.set_exact_type(target_type);
			return result;
		}
		
		case Type::Char:
		case Type::Short:
		case Type::Int:
		case Type::Long:
		case Type::LongLong:
		{
			EvalResult result = EvalResult::from_int(expr_result.as_int());
			result.set_exact_type(target_type);
			return result;
		}
		
		case Type::UnsignedChar:
		case Type::UnsignedShort:
		case Type::UnsignedInt:
		case Type::UnsignedLong:
		case Type::UnsignedLongLong:
			// For unsigned types, convert to unsigned.
		{
			// Read the source value preserving full bit-width using as_uint_raw()
			// to avoid the signed round-trip in as_int() for values above LLONG_MAX.
			EvalResult result = EvalResult::from_uint(expr_result.as_uint_raw());
			result.set_exact_type(target_type);
			return result;
		}
		
		case Type::Float:
		case Type::Double:
		case Type::LongDouble:
		{
			EvalResult result = EvalResult::from_double(expr_result.as_double());
			result.set_exact_type(target_type);
			return result;
		}
		
		default:
			return EvalResult::error(invalidTypeErrorStr);
	}
}

EvalResult Evaluator::evaluate_identifier(const IdentifierNode& identifier, EvaluationContext& context) {
	// Look up the identifier in the symbol table
	if (!context.symbols) {
		return EvalResult::error("Cannot evaluate variable reference: no symbol table provided");
	}

	std::string_view var_name = identifier.name();
	StringHandle name_handle = identifier.nameHandle();
	if (!name_handle.isValid()) {
		name_handle = StringTable::getOrInternStringHandle(var_name);
	}

	std::optional<ASTNode> symbol_opt;
	if (identifier.binding() == IdentifierBinding::StaticMember) {
		auto bound_static_initializer = resolve_current_struct_static_initializer(
			&identifier,
			context,
			CurrentStructStaticLookupMode::BoundOnly);
		bool found_bound_static_member = bound_static_initializer.found;
		if (found_bound_static_member && bound_static_initializer.initializer && bound_static_initializer.initializer->has_value()) {
			return evaluate(bound_static_initializer.initializer->value(), context);
		}

		if (identifier.resolved_name().isValid()) {
			symbol_opt = context.symbols->lookup(identifier.resolved_name());
		}

		if (!symbol_opt.has_value()) {
			if (found_bound_static_member) {
				return EvalResult::error("Static member has no initializer: " + std::string(var_name));
			}
			return EvalResult::error("Bound static member not found in constant expression: " + std::string(var_name));
		}
	} else {
		symbol_opt = lookup_identifier_symbol(&identifier, var_name, *context.symbols);
	}
	
	// If not found in symbol table, check for static members in the current struct
	if (!symbol_opt.has_value()) {
		auto preferred_static_initializer = resolve_current_struct_static_initializer(
			&identifier,
			context,
			CurrentStructStaticLookupMode::PreferCurrentStruct);
		if (preferred_static_initializer.found) {
			if (preferred_static_initializer.initializer && preferred_static_initializer.initializer->has_value()) {
				return evaluate(preferred_static_initializer.initializer->value(), context);
			}
			return EvalResult::error("Static member has no initializer: " + std::string(var_name));
		}
		
		// Variable not found - might be a template parameter that hasn't been substituted yet
		// Check if we have a parser context (indicates we're in template definition)
		// Template parameters have short names (typically single letters like T, N, etc.)
		// If the identifier looks like a template parameter, mark it as template-dependent
		if (context.parser != nullptr || var_name.length() <= 2) {
			// Likely a template parameter - return template-dependent error
			return EvalResult::error("Template parameter or undefined variable in constant expression: " + std::string(var_name),
			                         EvalErrorType::TemplateDependentExpression);
		}
		
		return EvalResult::error("Undefined variable in constant expression: " + std::string(var_name));
	}

	const ASTNode& symbol_node = symbol_opt.value();
	
	// Check if it's a TemplateVariableDeclarationNode - these are template-dependent
	if (symbol_node.is<TemplateVariableDeclarationNode>()) {
		// Variable template references with template arguments are template-dependent
		// They need to be evaluated during template instantiation
		return EvalResult::error("Variable template in constant expression - instantiation required: " + std::string(var_name),
		                         EvalErrorType::TemplateDependentExpression);
	}
	
	// Check if it's a DeclarationNode for an enum constant
	if (symbol_node.is<DeclarationNode>()) {
		const DeclarationNode& decl = symbol_node.as<DeclarationNode>();
		if (decl.type_node().is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
			if (type_spec.type() == Type::Enum) {
				// Look up the enumerator value from the type info
				auto type_index = type_spec.type_index();
				if (type_index.is_valid() && type_index.value < gTypeInfo.size()) {
					const TypeInfo& ti = gTypeInfo[type_index.value];
					const EnumTypeInfo* enum_info = ti.getEnumInfo();
					if (enum_info) {
						const Enumerator* e = enum_info->findEnumerator(name_handle);
						if (e) {
							EvalResult result = EvalResult::from_int(static_cast<long long>(e->value));
							result.set_exact_type(type_spec);
							return result;
						}
					}
				}
				// Enum constant but value not found - not an error per se, just unknown
				return EvalResult::error("Enum constant value not found: " + std::string(var_name));
			}
		}
	}
	
	// Check if it's a VariableDeclarationNode
	if (!symbol_node.is<VariableDeclarationNode>()) {
		return EvalResult::error("Identifier in constant expression is not a variable: " + std::string(var_name));
	}

	const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
	
	// Check if it's a constexpr variable
	if (!var_decl.is_constexpr()) {
		return EvalResult::error("Variable in constant expression must be constexpr: " + std::string(var_name));
	}

	// Get the initializer
	const auto& initializer = var_decl.initializer();
	if (!initializer.has_value()) {
		return EvalResult::error("Constexpr variable has no initializer: " + std::string(var_name));
	}

	// Check if the initializer is an InitializerListNode (for arrays)
	if (initializer->is<InitializerListNode>()) {
		const InitializerListNode& init_list = initializer->as<InitializerListNode>();
		if (var_decl.declaration().is_array()) {
			if (var_decl.declaration().type_node().is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& type_spec = var_decl.declaration().type_node().as<TypeSpecifierNode>();
				return materialize_array_value(type_spec.type(), type_spec.type_index(), init_list, context);
			}

			// Preserve the older generic array materialization for declarations whose
			// array element type is not represented as a TypeSpecifierNode (for example,
			// decltype()-spelled or still-dependent array element types).
			return materialize_array_value(Type::Auto, TypeIndex{}, init_list, context);
		}
	}

	// Recursively evaluate the initializer
	EvalResult result = evaluate(initializer.value(), context);
	if (!result.success()) {
		return result;
	}

	maybe_set_binding_result_exact_type(result, var_decl.declaration(), &initializer.value(), context);
	return result;
}

EvalResult Evaluator::evaluate_ternary_operator(const TernaryOperatorNode& ternary, EvaluationContext& context) {
	// Evaluate the condition
	auto cond_result = evaluate(ternary.condition(), context);
	if (!cond_result.success()) {
		return cond_result;
	}

	// Evaluate the appropriate branch based on the condition.
	// A valid constexpr pointer (pointer_to_var.isValid()) is always non-null (truthy).
	if (cond_result.pointer_to_var.isValid() ? true : cond_result.as_bool()) {
		return evaluate(ternary.true_expr(), context);
	} else {
		return evaluate(ternary.false_expr(), context);
	}
}

// Helper to extract a LambdaExpressionNode from a variable's initializer
// Returns nullptr if the initializer is not a lambda
const LambdaExpressionNode* Evaluator::extract_lambda_from_initializer(const std::optional<ASTNode>& initializer) {
	if (!initializer.has_value()) {
		return nullptr;
	}
	
	// Check for lambda expression (direct)
	if (initializer->is<LambdaExpressionNode>()) {
		return &initializer->as<LambdaExpressionNode>();
	}
	
	// Check for lambda expression (wrapped in ExpressionNode)
	if (initializer->is<ExpressionNode>()) {
		const ExpressionNode& expr = initializer->as<ExpressionNode>();
		if (const auto* lambda_expression = std::get_if<LambdaExpressionNode>(&expr)) {
			return lambda_expression;
		}
	}
	
	return nullptr;
}

std::optional<Evaluator::ExtractedIdentifier> Evaluator::extract_identifier_from_expression(const ASTNode& object_expr) {
	if (const IdentifierNode* id_node = tryGetIdentifier(object_expr)) {
		return ExtractedIdentifier{ id_node, id_node->name() };
	}
	return std::nullopt;
}

EvalResult Evaluator::materialize_lambda_value(
	const LambdaExpressionNode& lambda,
	EvaluationContext& context,
	const std::unordered_map<std::string_view, EvalResult>* outer_bindings) {
	EvalResult callable_result = EvalResult::from_lambda(lambda);
	auto capture_result = evaluate_lambda_captures(lambda.captures(), callable_result.callable_bindings, context, outer_bindings);
	if (!capture_result.success()) {
		return capture_result;
	}
	return callable_result;
}

const ConstructorCallNode* Evaluator::extract_constructor_call(const std::optional<ASTNode>& initializer) {
	if (!initializer.has_value()) return nullptr;
	if (initializer->is<ConstructorCallNode>())
		return &initializer->as<ConstructorCallNode>();
	if (initializer->is<ExpressionNode>()) {
		const ExpressionNode& expr = initializer->as<ExpressionNode>();
		if (const auto* constructor_call = std::get_if<ConstructorCallNode>(&expr))
			return constructor_call;
	}
	return nullptr;
}


EvalResult Evaluator::evaluate_lambda_captures(
	const std::vector<LambdaCaptureNode>& captures,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	const std::unordered_map<std::string_view, EvalResult>* outer_bindings,
	const std::unordered_map<std::string_view, EvalResult>* stored_capture_bindings) {
	
	for (const auto& capture : captures) {
		using CaptureKind = LambdaCaptureNode::CaptureKind;
		
		switch (capture.kind()) {
			case CaptureKind::ByValue:
			case CaptureKind::ByReference: {
				// Named capture: [x] or [&x]
				std::string_view var_name = capture.identifier_name();
				if (capture.kind() == CaptureKind::ByValue && stored_capture_bindings) {
					auto stored_it = stored_capture_bindings->find(var_name);
					if (stored_it != stored_capture_bindings->end()) {
						bindings[var_name] = stored_it->second;
						break;
					}
				}
				
				// Check for init-capture: [x = expr]
				if (capture.has_initializer()) {
					auto init_result = (outer_bindings && capture.initializer().value().is<ExpressionNode>())
						? evaluate_expression_with_bindings_const(capture.initializer().value(), *outer_bindings, context)
						: evaluate(capture.initializer().value(), context);
					if (!init_result.success()) {
						return EvalResult::error("Failed to evaluate init-capture '" + 
							std::string(var_name) + "': " + init_result.error_message);
					}
					bindings[var_name] = init_result;
				} else {
					if (outer_bindings) {
						auto outer_it = outer_bindings->find(var_name);
						if (outer_it != outer_bindings->end()) {
							bindings[var_name] = outer_it->second;
							break;
						}
					}

					// Look up the variable in the symbol table
					if (!context.symbols) {
						return EvalResult::error("Cannot evaluate capture: no symbol table provided");
					}
					
					auto symbol_opt = context.symbols->lookup(var_name);
					if (!symbol_opt.has_value()) {
						return EvalResult::error("Captured variable not found: " + std::string(var_name));
					}
					
					const ASTNode& symbol_node = symbol_opt.value();
					if (!symbol_node.is<VariableDeclarationNode>()) {
						return EvalResult::error("Captured identifier is not a variable: " + std::string(var_name));
					}
					
					const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
					
					// For constexpr evaluation, the captured variable must be constexpr
					if (!var_decl.is_constexpr()) {
						return EvalResult::error("Captured variable must be constexpr in constant expression: " + 
							std::string(var_name));
					}
					
					// Evaluate the variable's initializer
					if (!var_decl.initializer().has_value()) {
						return EvalResult::error("Captured constexpr variable has no initializer: " + 
							std::string(var_name));
					}
					
					auto var_result = evaluate(var_decl.initializer().value(), context);
					if (!var_result.success()) {
						return EvalResult::error("Failed to evaluate captured variable '" + 
							std::string(var_name) + "': " + var_result.error_message);
					}
					bindings[var_name] = var_result;
				}
				break;
			}
			
			case CaptureKind::AllByValue:
			case CaptureKind::AllByReference:
				// [=] or [&] - implicit capture
				// In constexpr context, we don't know which variables are used without analyzing the body
				// For now, this is a limitation - we'd need body analysis to support this
				return EvalResult::error("Implicit capture [=] or [&] not supported in constexpr lambdas - use explicit captures");
			
				case CaptureKind::This:
				case CaptureKind::CopyThis:
						// [this] or [*this] - materialize the enclosing object's constexpr members.
						if (capture.kind() == CaptureKind::CopyThis && stored_capture_bindings && !context.struct_info) {
							for (const auto& [member_name, member_value] : *stored_capture_bindings) {
								bindings[member_name] = member_value;
							}
							break;
						}
						if (!outer_bindings) {
							return EvalResult::error("Capture of 'this' requires outer constexpr bindings");
						}
						if (!context.struct_info) {
							return EvalResult::error("Capture of 'this' requires constexpr member function context");
						}

						for (const auto& member : context.struct_info->members) {
							std::string_view member_name = StringTable::getStringView(member.getName());
							if (capture.kind() == CaptureKind::CopyThis && stored_capture_bindings) {
								auto stored_it = stored_capture_bindings->find(member_name);
								if (stored_it != stored_capture_bindings->end()) {
									bindings[member_name] = stored_it->second;
									continue;
								}
							}
							auto outer_it = outer_bindings->find(member_name);
							if (outer_it != outer_bindings->end()) {
								bindings[member_name] = outer_it->second;
							}
						}
						break;
		}
	}
	
	// Success - all captures evaluated
	EvalResult success;
	success.error_type = EvalErrorType::None;
	success.value = 0LL;  // Dummy value, not used
	return success;
}

// Evaluate a callable object (lambda or user-defined functor with operator())
EvalResult Evaluator::evaluate_callable_object(
	const VariableDeclarationNode& var_decl,
	const ChunkedVector<ASTNode>& arguments,
	EvaluationContext& context,
	const std::unordered_map<std::string_view, EvalResult>* outer_bindings,
	std::unordered_map<std::string_view, EvalResult>* mutable_outer_bindings,
	EvalResult* callable_state) {
	
	// Check for lambda
	const LambdaExpressionNode* lambda = extract_lambda_from_initializer(var_decl.initializer());
	if (lambda) {
		const auto* stored_capture_bindings = callable_state ? &callable_state->callable_bindings : nullptr;
		auto* mutable_stored_capture_bindings = callable_state ? &callable_state->callable_bindings : nullptr;
		return evaluate_lambda_call(*lambda, arguments, context, outer_bindings, mutable_outer_bindings,
			stored_capture_bindings, mutable_stored_capture_bindings);
	}
	
	// Check for ConstructorCallNode (user-defined functor), handling both direct storage
	// and ExpressionNode-wrapping (e.g., Add() parsed as ExpressionNode(ConstructorCallNode(...))).
	const auto& initializer = var_decl.initializer();
	const ConstructorCallNode* ctor_call_ptr = extract_constructor_call(initializer);
	if (ctor_call_ptr) {
		const ConstructorCallNode& ctor_call = *ctor_call_ptr;
		const ASTNode& type_node = ctor_call.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Callable object constructor has invalid type");
		}

		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		const StructTypeInfo* struct_info = get_struct_info_from_type(type_spec);
		if (!struct_info) {
			return EvalResult::error("Callable object is not a struct/class type");
		}

		// Known limitation: overload selection currently matches by arity only.
		// If multiple same-arity operator() overloads exist, we reject as ambiguous.
		auto call_operator_match = find_call_operator_candidate(struct_info, arguments.size(), true);
		if (call_operator_match.ambiguous) {
			return EvalResult::error("Ambiguous operator() overload: multiple candidates with same arity");
		}
		const FunctionDeclarationNode* call_operator = call_operator_match.function;

		if (!call_operator) {
			return EvalResult::error("Callable object has no matching operator()");
		}

		if (!call_operator->is_constexpr()) {
			return EvalResult::error("Callable object operator() in constant expression must be constexpr");
		}

		const auto& definition = call_operator->get_definition();
		if (!definition.has_value()) {
			return EvalResult::error("Callable object operator() has no body");
		}
		if (context.current_depth >= context.max_recursion_depth) {
			return EvalResult::error("Constexpr recursion depth limit exceeded in callable object call");
		}

		// Build object member bindings from the full constructor materialization path.
		std::unordered_map<std::string_view, EvalResult> evaluation_bindings;
		const auto& ctor_args = ctor_call.arguments();
			const ConstructorDeclarationNode* matching_ctor = find_matching_constructor(struct_info, ctor_args, context, outer_bindings);
		if (!matching_ctor) {
			return EvalResult::error("No matching constructor found for callable object");
		}

		std::unordered_map<std::string_view, EvalResult> ctor_param_bindings;
		const auto& ctor_params = matching_ctor->parameter_nodes();
		auto ctor_bind_result = bind_evaluated_arguments(
			ctor_params,
			ctor_args,
			ctor_param_bindings,
			context,
			"Invalid parameter node in callable object constructor",
			outer_bindings,
			true);
		if (!ctor_bind_result.success()) {
			return ctor_bind_result;
		}

		auto member_bind_result = materialize_members_from_constructor(
			struct_info,
			*matching_ctor,
			ctor_param_bindings,
			evaluation_bindings,
			context,
			false);
		if (!member_bind_result.success()) {
			return member_bind_result;
		}

		const auto& parameters = call_operator->parameter_nodes();
		auto call_bind_result = bind_evaluated_arguments(
			parameters,
			arguments,
			evaluation_bindings,
			context,
			"Invalid parameter node in callable object operator()",
			outer_bindings,
			false);
		if (!call_bind_result.success()) {
			return call_bind_result;
		}

		if (context.current_depth >= context.max_recursion_depth) {
			return EvalResult::error("Constexpr recursion depth limit exceeded in callable object call");
		}
			auto saved_struct_info = context.struct_info;
			context.struct_info = struct_info;
		context.current_depth++;
		auto result = evaluate_block_with_bindings(
			definition.value(),
			evaluation_bindings,
			context,
			"Callable object operator() body is not a block",
			"Constexpr callable object operator() did not return a value");
		context.current_depth--;
			context.struct_info = saved_struct_info;
		return result;
	}

	// Handle brace-initialized callable objects: constexpr Add add{args...}
	// The initializer is an InitializerListNode; get the struct type from the variable's type.
	if (initializer.has_value() && initializer->is<InitializerListNode>()) {
		const DeclarationNode& decl = var_decl.declaration();
		if (!decl.type_node().is<TypeSpecifierNode>()) {
			return EvalResult::error("Brace-initialized callable object has invalid type");
		}
		const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
		const StructTypeInfo* struct_info = get_struct_info_from_type(type_spec);
		if (!struct_info) {
			return EvalResult::error("Brace-initialized callable object is not a struct/class type");
		}

		// Find operator()
		auto call_operator_match = find_call_operator_candidate(struct_info, arguments.size(), true);
		if (call_operator_match.ambiguous)
			return EvalResult::error("Ambiguous operator() overload in brace-initialized callable object");
		const FunctionDeclarationNode* call_operator = call_operator_match.function;
		if (!call_operator)
			return EvalResult::error("Brace-initialized callable object has no matching operator()");
		if (!call_operator->is_constexpr())
			return EvalResult::error("operator() in brace-initialized callable object must be constexpr");
		const auto& definition = call_operator->get_definition();
		if (!definition.has_value())
			return EvalResult::error("operator() in brace-initialized callable object has no body");

		// Build member bindings from the InitializerListNode (aggregate initialization).
		const InitializerListNode& init_list = initializer->as<InitializerListNode>();
		std::unordered_map<std::string_view, EvalResult> evaluation_bindings;
		auto member_bind_result = bind_members_from_initializer_list(struct_info, init_list, evaluation_bindings, context);
		if (!member_bind_result.success()) return member_bind_result;

		// Bind call arguments to operator() parameters.
		const auto& parameters = call_operator->parameter_nodes();
		auto bind_result = bind_evaluated_arguments(
			parameters,
			arguments,
			evaluation_bindings,
			context,
			"Invalid parameter node in brace-initialized callable object operator()",
			outer_bindings,
			true);
		if (!bind_result.success()) return bind_result;

			if (context.current_depth >= context.max_recursion_depth) {
				return EvalResult::error("Constexpr recursion depth limit exceeded");
			}
			auto saved_struct_info = context.struct_info;
			context.struct_info = struct_info;
		context.current_depth++;
		auto result = evaluate_block_with_bindings(
			definition.value(),
			evaluation_bindings,
			context,
			"operator() body in brace-initialized callable is not a block",
			"Constexpr operator() in brace-initialized callable did not return a value");
		context.current_depth--;
			context.struct_info = saved_struct_info;
		return result;
	}
	
	return EvalResult::error("Object is not callable in constant expression");
}

// Evaluate a lambda call
EvalResult Evaluator::evaluate_lambda_call(
	const LambdaExpressionNode& lambda,
	const ChunkedVector<ASTNode>& arguments,
	EvaluationContext& context,
	const std::unordered_map<std::string_view, EvalResult>* outer_bindings,
	std::unordered_map<std::string_view, EvalResult>* mutable_outer_bindings,
	const std::unordered_map<std::string_view, EvalResult>* stored_capture_bindings,
	std::unordered_map<std::string_view, EvalResult>* mutable_stored_capture_bindings) {
	
	// Check recursion depth
	if (context.current_depth >= context.max_recursion_depth) {
		return EvalResult::error("Constexpr recursion depth limit exceeded in lambda call");
	}
	
	// Get lambda parameters
	const auto& parameters = lambda.parameters();
	
	if (arguments.size() != parameters.size()) {
		return EvalResult::error("Lambda argument count mismatch in constant expression");
	}
	
	// Build parameter bindings
	std::unordered_map<std::string_view, EvalResult> bindings;
	auto bind_result = bind_evaluated_arguments(
		parameters,
		arguments,
		bindings,
		context,
		"Invalid parameter node in constexpr lambda",
		outer_bindings);
	if (!bind_result.success()) {
		return bind_result;
	}
	
	// Handle captures - evaluate each captured variable and add to bindings
	const auto& captures = lambda.captures();
	auto capture_result = evaluate_lambda_captures(captures, bindings, context, outer_bindings, stored_capture_bindings);
	if (!capture_result.success()) {
		return capture_result;
	}

	bool captures_this_by_reference = false;
	bool captures_copy_this = false;
	std::vector<std::string_view> by_reference_capture_names;
	std::vector<std::pair<std::string_view, std::string_view>> by_reference_init_capture_aliases;
	std::vector<std::string_view> by_value_capture_names;
	for (const auto& capture : captures) {
		if (capture.kind() == LambdaCaptureNode::CaptureKind::This) {
			captures_this_by_reference = true;
			continue;
		}
		if (capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
			captures_copy_this = true;
			continue;
		}
		if (capture.kind() == LambdaCaptureNode::CaptureKind::ByValue) {
			by_value_capture_names.push_back(capture.identifier_name());
			continue;
		}
		if (capture.kind() == LambdaCaptureNode::CaptureKind::ByReference) {
			if (!capture.has_initializer()) {
				by_reference_capture_names.push_back(capture.identifier_name());
				continue;
			}
			std::string_view aliased_name = getIdentifierNameFromAstNode(capture.initializer().value());
			if (!aliased_name.empty()) {
				by_reference_init_capture_aliases.emplace_back(capture.identifier_name(), aliased_name);
			}
		}
	}
	
	// Increase recursion depth
	context.current_depth++;
	
	// Evaluate the lambda body
	const ASTNode& body_node = lambda.body();
	
	EvalResult result;
	if (body_node.is<BlockNode>()) {
		result = evaluate_block_with_bindings(
			body_node,
			bindings,
			context,
			"Constexpr lambda body is not a block",
			"Constexpr lambda did not return a value");
	} else if (body_node.is<ExpressionNode>()) {
		// Expression body (implicit return)
		result = evaluate_expression_with_bindings(body_node, bindings, context);
	} else {
		context.current_depth--;
		return EvalResult::error("Invalid lambda body in constant expression");
	}
	
		context.current_depth--;
		if (result.success() && mutable_stored_capture_bindings) {
			for (std::string_view capture_name : by_value_capture_names) {
				auto binding_it = bindings.find(capture_name);
				if (binding_it != bindings.end()) {
					(*mutable_stored_capture_bindings)[capture_name] = binding_it->second;
				}
			}
				if (captures_copy_this) {
					if (context.struct_info) {
						for (const auto& member : context.struct_info->members) {
							std::string_view member_name = StringTable::getStringView(member.getName());
							auto binding_it = bindings.find(member_name);
							if (binding_it != bindings.end()) {
								(*mutable_stored_capture_bindings)[member_name] = binding_it->second;
							}
						}
					} else {
						for (auto& [member_name, member_value] : *mutable_stored_capture_bindings) {
							auto binding_it = bindings.find(member_name);
							if (binding_it != bindings.end()) {
								member_value = binding_it->second;
							}
						}
					}
			}
		}
		if (result.success() && mutable_outer_bindings) {
			for (std::string_view capture_name : by_reference_capture_names) {
				auto binding_it = bindings.find(capture_name);
				if (binding_it != bindings.end()) {
					(*mutable_outer_bindings)[capture_name] = binding_it->second;
				}
			}
			for (const auto& [capture_name, aliased_name] : by_reference_init_capture_aliases) {
				auto binding_it = bindings.find(capture_name);
				if (binding_it != bindings.end()) {
					(*mutable_outer_bindings)[aliased_name] = binding_it->second;
				}
			}
		}
		if (result.success() && captures_this_by_reference && mutable_outer_bindings && context.struct_info) {
			for (const auto& member : context.struct_info->members) {
				std::string_view member_name = StringTable::getStringView(member.getName());
				auto binding_it = bindings.find(member_name);
				if (binding_it != bindings.end()) {
					(*mutable_outer_bindings)[member_name] = binding_it->second;
				}
			}
		}
	return result;
}

// Evaluate compiler builtin functions at compile time
EvalResult Evaluator::evaluate_builtin_function(std::string_view func_name, const ChunkedVector<ASTNode>& arguments, EvaluationContext& context) {
	// Handle __builtin_clzll - count leading zeros for long long
	if (func_name == "__builtin_clzll") {
		if (arguments.size() != 1) {
			return EvalResult::error("__builtin_clzll requires exactly 1 argument");
		}
		auto arg_result = evaluate(arguments[0], context);
		if (!arg_result.success()) {
			return arg_result;
		}
		unsigned long long value;
		if (const auto* ull_val = std::get_if<unsigned long long>(&arg_result.value)) {
			value = *ull_val;
		} else if (const auto* ll_val = std::get_if<long long>(&arg_result.value)) {
			value = static_cast<unsigned long long>(*ll_val);
		} else {
			return EvalResult::error("__builtin_clzll argument must be an integer");
		}
		
		if (value == 0) {
			// __builtin_clzll(0) is undefined behavior in GCC/Clang. We return the
			// bit width (64 on typical systems) which matches what some implementations
			// do, and is a reasonable choice for constexpr evaluation. This allows
			// code that guards against zero to work correctly at compile time.
			return EvalResult::from_int(static_cast<long long>(sizeof(long long) * 8));
		}
		
		// Count leading zeros
		int count = 0;
		unsigned long long mask = 1ULL << (sizeof(long long) * 8 - 1);
		while ((value & mask) == 0 && mask != 0) {
			count++;
			mask >>= 1;
		}
		return EvalResult::from_int(static_cast<long long>(count));
	}
	
	// Handle __builtin_clz - count leading zeros for int
	if (func_name == "__builtin_clz") {
		if (arguments.size() != 1) {
			return EvalResult::error("__builtin_clz requires exactly 1 argument");
		}
		auto arg_result = evaluate(arguments[0], context);
		if (!arg_result.success()) {
			return arg_result;
		}
		unsigned int value;
		if (const auto* ull_val = std::get_if<unsigned long long>(&arg_result.value)) {
			value = static_cast<unsigned int>(*ull_val);
		} else if (const auto* ll_val = std::get_if<long long>(&arg_result.value)) {
			value = static_cast<unsigned int>(*ll_val);
		} else {
			return EvalResult::error("__builtin_clz argument must be an integer");
		}
		
		if (value == 0) {
			return EvalResult::from_int(static_cast<long long>(sizeof(int) * 8));
		}
		
		int count = 0;
		unsigned int mask = 1U << (sizeof(int) * 8 - 1);
		while ((value & mask) == 0 && mask != 0) {
			count++;
			mask >>= 1;
		}
		return EvalResult::from_int(static_cast<long long>(count));
	}
	
	// Handle __builtin_ctzll - count trailing zeros for long long
	if (func_name == "__builtin_ctzll") {
		if (arguments.size() != 1) {
			return EvalResult::error("__builtin_ctzll requires exactly 1 argument");
		}
		auto arg_result = evaluate(arguments[0], context);
		if (!arg_result.success()) {
			return arg_result;
		}
		unsigned long long value;
		if (const auto* ull_val = std::get_if<unsigned long long>(&arg_result.value)) {
			value = *ull_val;
		} else if (const auto* ll_val = std::get_if<long long>(&arg_result.value)) {
			value = static_cast<unsigned long long>(*ll_val);
		} else {
			return EvalResult::error("__builtin_ctzll argument must be an integer");
		}
		
		if (value == 0) {
			return EvalResult::from_int(static_cast<long long>(sizeof(long long) * 8));
		}
		
		int count = 0;
		while ((value & 1) == 0) {
			count++;
			value >>= 1;
		}
		return EvalResult::from_int(static_cast<long long>(count));
	}
	
	// Handle __builtin_ctz - count trailing zeros for int
	if (func_name == "__builtin_ctz") {
		if (arguments.size() != 1) {
			return EvalResult::error("__builtin_ctz requires exactly 1 argument");
		}
		auto arg_result = evaluate(arguments[0], context);
		if (!arg_result.success()) {
			return arg_result;
		}
		unsigned int value;
		if (const auto* ull_val = std::get_if<unsigned long long>(&arg_result.value)) {
			value = static_cast<unsigned int>(*ull_val);
		} else if (const auto* ll_val = std::get_if<long long>(&arg_result.value)) {
			value = static_cast<unsigned int>(*ll_val);
		} else {
			return EvalResult::error("__builtin_ctz argument must be an integer");
		}
		
		if (value == 0) {
			return EvalResult::from_int(static_cast<long long>(sizeof(int) * 8));
		}
		
		int count = 0;
		while ((value & 1) == 0) {
			count++;
			value >>= 1;
		}
		return EvalResult::from_int(static_cast<long long>(count));
	}
	
	// Handle __builtin_popcountll - count set bits in long long
	if (func_name == "__builtin_popcountll") {
		if (arguments.size() != 1) {
			return EvalResult::error("__builtin_popcountll requires exactly 1 argument");
		}
		auto arg_result = evaluate(arguments[0], context);
		if (!arg_result.success()) {
			return arg_result;
		}
		unsigned long long value;
		if (const auto* ull_val = std::get_if<unsigned long long>(&arg_result.value)) {
			value = *ull_val;
		} else if (const auto* ll_val = std::get_if<long long>(&arg_result.value)) {
			value = static_cast<unsigned long long>(*ll_val);
		} else {
			return EvalResult::error("__builtin_popcountll argument must be an integer");
		}
		
		int count = 0;
		while (value != 0) {
			count += (value & 1);
			value >>= 1;
		}
		return EvalResult::from_int(static_cast<long long>(count));
	}
	
	// Handle __builtin_popcount - count set bits in int
	if (func_name == "__builtin_popcount") {
		if (arguments.size() != 1) {
			return EvalResult::error("__builtin_popcount requires exactly 1 argument");
		}
		auto arg_result = evaluate(arguments[0], context);
		if (!arg_result.success()) {
			return arg_result;
		}
		unsigned int value;
		if (const auto* ull_val = std::get_if<unsigned long long>(&arg_result.value)) {
			value = static_cast<unsigned int>(*ull_val);
		} else if (const auto* ll_val = std::get_if<long long>(&arg_result.value)) {
			value = static_cast<unsigned int>(*ll_val);
		} else {
			return EvalResult::error("__builtin_popcount argument must be an integer");
		}
		
		int count = 0;
		while (value != 0) {
			count += (value & 1);
			value >>= 1;
		}
		return EvalResult::from_int(static_cast<long long>(count));
	}
	
	// Handle __builtin_ffsll - find first set bit (1-indexed) in long long
	if (func_name == "__builtin_ffsll") {
		if (arguments.size() != 1) {
			return EvalResult::error("__builtin_ffsll requires exactly 1 argument");
		}
		auto arg_result = evaluate(arguments[0], context);
		if (!arg_result.success()) {
			return arg_result;
		}
		unsigned long long value;
		if (const auto* ull_val = std::get_if<unsigned long long>(&arg_result.value)) {
			value = *ull_val;
		} else if (const auto* ll_val = std::get_if<long long>(&arg_result.value)) {
			value = static_cast<unsigned long long>(*ll_val);
		} else {
			return EvalResult::error("__builtin_ffsll argument must be an integer");
		}
		
		if (value == 0) {
			return EvalResult::from_int(0LL);
		}
		
		int pos = 1;
		while ((value & 1) == 0) {
			pos++;
			value >>= 1;
		}
		return EvalResult::from_int(static_cast<long long>(pos));
	}
	
	// Handle __builtin_ffs - find first set bit (1-indexed) in int
	if (func_name == "__builtin_ffs") {
		if (arguments.size() != 1) {
			return EvalResult::error("__builtin_ffs requires exactly 1 argument");
		}
		auto arg_result = evaluate(arguments[0], context);
		if (!arg_result.success()) {
			return arg_result;
		}
		unsigned int value;
		if (const auto* ull_val = std::get_if<unsigned long long>(&arg_result.value)) {
			value = static_cast<unsigned int>(*ull_val);
		} else if (const auto* ll_val = std::get_if<long long>(&arg_result.value)) {
			value = static_cast<unsigned int>(*ll_val);
		} else {
			return EvalResult::error("__builtin_ffs argument must be an integer");
		}
		
		if (value == 0) {
			return EvalResult::from_int(0LL);
		}
		
		int pos = 1;
		while ((value & 1) == 0) {
			pos++;
			value >>= 1;
		}
		return EvalResult::from_int(static_cast<long long>(pos));
	}
	
	// Handle __builtin_constant_p - check if argument is a compile-time constant
	if (func_name == "__builtin_constant_p") {
		if (arguments.size() != 1) {
			return EvalResult::error("__builtin_constant_p requires exactly 1 argument");
		}
		// In a constexpr context, if we can evaluate the argument, it's a constant
		auto arg_result = evaluate(arguments[0], context);
		return EvalResult::from_int(arg_result.success() ? 1LL : 0LL);
	}
	
	// Handle __builtin_abs, __builtin_labs, __builtin_llabs
	if (func_name == "__builtin_abs" || func_name == "__builtin_labs" || func_name == "__builtin_llabs") {
		if (arguments.size() != 1) {
			return EvalResult::error(std::string(func_name) + " requires exactly 1 argument");
		}
		auto arg_result = evaluate(arguments[0], context);
		if (!arg_result.success()) {
			return arg_result;
		}
		long long value = arg_result.as_int();
		// abs(LLONG_MIN) is undefined behavior (overflow when negating)
		if (value == LLONG_MIN) {
			return EvalResult::error(std::string(func_name) + "(LLONG_MIN) is undefined behavior");
		}
		return EvalResult::from_int(value < 0 ? -value : value);
	}
	
	// Not a known builtin function - return a special error that callers can check
	return EvalResult::error("Unknown builtin function: " + std::string(func_name));
}

// Try to evaluate a FunctionCallNode as a variable template instantiation.
// Variable templates like __is_ratio_v<T> get parsed as FunctionCallNodes because
// identifier<args> looks like a function call syntactically. This helper extracts
// the template arguments, instantiates the variable template, and evaluates it.
EvalResult Evaluator::tryEvaluateAsVariableTemplate(std::string_view func_name, const FunctionCallNode& func_call, EvaluationContext& context) {
	if (!context.parser) {
		return EvalResult::error("No parser available for variable template instantiation");
	}
	
	if (!func_call.has_template_arguments()) {
		return EvalResult::error("No template arguments for variable template");
	}
	
	std::vector<TemplateTypeArg> template_args;
	for (const ASTNode& arg_node : func_call.template_arguments()) {
		if (arg_node.is<TypeSpecifierNode>()) {
			template_args.emplace_back(arg_node.as<TypeSpecifierNode>());
		} else if (arg_node.is<ExpressionNode>()) {
			// Evaluate the expression to get a constant value for a non-type template argument.
			// This handles literals, binary expressions like 1+2, and other constant expressions.
			EvalResult arg_val = evaluate(arg_node, context);
			if (!arg_val.success()) {
				return EvalResult::error("Failed to evaluate non-type template argument: " + arg_val.error_message);
			}
			Type arg_type = Type::Int;
			if (std::holds_alternative<bool>(arg_val.value)) {
				arg_type = Type::Bool;
			} else if (arg_val.is_uint()) {
				arg_type = Type::UnsignedLongLong;
			}
			template_args.emplace_back(arg_val.as_int(), arg_type);
		} else {
			return EvalResult::error("Unsupported template argument type for variable template");
		}
	}
	
	if (template_args.empty()) {
		return EvalResult::error("No template arguments extracted for variable template");
	}
	
	// Try to instantiate the variable template
	auto var_node = context.parser->try_instantiate_variable_template(func_name, template_args);
	
	// Try with qualified name if simple name didn't work
	if (!var_node.has_value() && func_call.has_qualified_name()) {
		var_node = context.parser->try_instantiate_variable_template(func_call.qualified_name(), template_args);
	}
	
	if (var_node.has_value() && var_node->is<VariableDeclarationNode>()) {
		const VariableDeclarationNode& var_decl = var_node->as<VariableDeclarationNode>();
		if (var_decl.initializer().has_value()) {
			return evaluate(var_decl.initializer().value(), context);
		}
	}
	
	return EvalResult::error("Variable template instantiation failed: " + std::string(func_name));
}

EvalResult Evaluator::evaluate_function_call(const FunctionCallNode& func_call, EvaluationContext& context) {
	// Check recursion depth
	if (context.current_depth >= context.max_recursion_depth) {
		return EvalResult::error("Constexpr recursion depth limit exceeded");
	}

	// Get the function declaration
	const DeclarationNode& func_decl_node = func_call.function_declaration();
	
	// Look up the function in the symbol table to get the FunctionDeclarationNode
	if (!context.symbols) {
		return EvalResult::error("Cannot evaluate function call: no symbol table provided");
	}

	std::string_view func_name = func_decl_node.identifier_token().value();
	
	// First try to get the qualified source name (e.g., "std::__is_complete_or_unbounded")
	// This is set by the parser for qualified function calls
	std::string_view qualified_name = func_name;
	if (func_call.has_qualified_name()) {
		qualified_name = func_call.qualified_name();
		FLASH_LOG(Templates, Debug, "Using qualified name for template lookup: ", qualified_name);
	}

	// If we have a struct context, prefer static member functions from the current struct.
	// This ensures that `helper()` in `static constexpr int value = helper()` resolves
	// to Box<T>::helper() rather than a global helper() when inside a struct definition.
	auto tryEvaluateCurrentStructStaticMemberFunction = [&]() -> std::optional<EvalResult> {
		const auto& arguments = func_call.arguments();
		StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
		auto current_match = find_current_struct_member_function_candidate(
			func_name_handle,
			arguments.size(),
			context,
			MemberFunctionLookupMode::ConstexprEvaluable,
			false,
			true);
		if (current_match.ambiguous) {
			return EvalResult::error("Ambiguous static member function overload in constant expression");
		}

		const FunctionDeclarationNode* matched_function = current_match.function;

		if (!matched_function) {
			return std::nullopt;
		}

		std::unordered_map<std::string_view, EvalResult> empty_bindings;
		return evaluate_function_call_with_template_context(
			*matched_function,
			arguments,
			empty_bindings,
			context,
			nullptr,
			FunctionCallTemplateBindingLoadMode::ForceCurrentStructIfAvailable);
	};

	if (auto current_struct_result = tryEvaluateCurrentStructStaticMemberFunction()) {
		return *current_struct_result;
	}

	// Special handling for std::__is_complete_or_unbounded
	// This is a helper function in the standard library that checks if a type is complete
	// __is_complete_or_unbounded evaluates to true if either:
	// 1. T is a complete type, or
	// 2. T is an unbounded array type (e.g. int[])
	if (qualified_name == "std::__is_complete_or_unbounded" || func_name == "__is_complete_or_unbounded") {
		FLASH_LOG(Templates, Debug, "Special handling for __is_complete_or_unbounded");
		
		// The function takes a __type_identity<T> argument
		// We need to extract the type T and check if it's complete or unbounded
		if (func_call.arguments().size() == 0) {
			return EvalResult::error("__is_complete_or_unbounded requires a type argument");
		}
		
		// Get the first argument (should be a ConstructorCallNode for __type_identity<T>{})
		const ASTNode& arg = func_call.arguments()[0];
		
		// Try to extract the type from the argument
		// The argument is typically __type_identity<T>{} which is a constructor call
		if (arg.is<ExpressionNode>()) {
			const ExpressionNode& expr = arg.as<ExpressionNode>();
			if (std::holds_alternative<ConstructorCallNode>(expr)) {
				const ConstructorCallNode& ctor = std::get<ConstructorCallNode>(expr);
				const ASTNode& type_node = ctor.type_node();
				
				if (type_node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
					Type base_type = type_spec.type();
					bool is_reference = type_spec.is_reference();
					size_t pointer_depth = type_spec.pointer_depth();
					bool is_array = type_spec.is_array();
					std::optional<size_t> array_size = type_spec.array_size();
					
					// Check for void - always incomplete
					if (base_type == Type::Void && pointer_depth == 0 && !is_reference) {
						return EvalResult::from_bool(false);
					}
					
					// Check for unbounded array - always returns true
					if (is_array && (!array_size.has_value() || *array_size == 0)) {
						return EvalResult::from_bool(true);
					}
					
					// Check for incomplete class/struct types
					// A type is incomplete if it's a struct/class with no StructTypeInfo
					TypeIndex type_idx = type_spec.type_index();
					if (type_idx.is_valid() && (base_type == Type::Struct || base_type == Type::UserDefined)) {
						const TypeInfo& type_info = gTypeInfo[type_idx.value];
						const StructTypeInfo* struct_info = type_info.getStructInfo();
						
						// If it's a struct/class type with no struct_info, it's incomplete
						if (!struct_info && pointer_depth == 0 && !is_reference) {
							return EvalResult::from_bool(false);
						}
					}
					
					// All other types are considered complete
					return EvalResult::from_bool(true);
				}
			}
		}
		
		// If we can't extract the type, return true as a fallback
		FLASH_LOG(Templates, Debug, "__is_complete_or_unbounded: couldn't extract type, returning true as fallback");
		return EvalResult::from_bool(true);
	}
	
	// Prefer the parser-stored exact call target before falling back to raw name lookup.
	auto symbol_opt = lookup_function_symbol(func_call, func_name, *context.symbols);

	// If not found in local symbol table, try the global symbol table (for free functions declared at global scope)
	if (!symbol_opt.has_value() && context.global_symbols && context.global_symbols != context.symbols) {
		symbol_opt = lookup_function_symbol(func_call, func_name, *context.global_symbols);
	}
	
	// If not found in symbol table, try the global template registry
	// This handles cases where a template function is defined but not yet instantiated
	if (!symbol_opt.has_value() && context.parser) {
		// Try to find the template in the global registry with qualified name first
		auto template_opt = gTemplateRegistry.lookupTemplate(qualified_name);
		
		// If not found with qualified name, try with simple name
		if (!template_opt.has_value() && qualified_name != func_name) {
			template_opt = gTemplateRegistry.lookupTemplate(func_name);
		}
		
		// If still not found with simple name, try with common namespace prefixes
		if (!template_opt.has_value()) {
			std::vector<std::string> name_candidates;
			name_candidates.push_back(std::string("std::") + std::string(func_name));
			name_candidates.push_back(std::string("__gnu_cxx::") + std::string(func_name));
			
			for (const auto& candidate_name : name_candidates) {
				template_opt = gTemplateRegistry.lookupTemplate(candidate_name);
				if (template_opt.has_value()) {
					break;
				}
			}
		}
		
		// If we found the template, use it
		if (template_opt.has_value()) {
			symbol_opt = template_opt;
		}
	}
	
	// If simple lookup fails, try to find the function as a static member in struct types
	if (!symbol_opt.has_value()) {
		// Search all struct types for a static member function with this name
		// This handles cases like Point::static_sum where the parser creates a FunctionCallNode
		// but the function name is just "static_sum" without the qualifier
		
		// Note: This search will find both static and non-static member functions.
		// For non-static members, the evaluation will naturally fail when we try to call them
		// without an instance (parameter count mismatch or missing 'this' context).
		// Static member functions have no implicit 'this' parameter, so they work correctly.
		
		for (const auto& type_info : ::gTypeInfo) {
			if (!type_info.struct_info_) continue;
			
			// Search member functions in this struct
			for (const auto& member_func : type_info.struct_info_->member_functions) {
				if (member_func.name == StringTable::getOrInternStringHandle(func_name)) {
					// Found a matching member function
					const ASTNode& func_node = member_func.function_decl;
					if (func_node.is<FunctionDeclarationNode>()) {
						const FunctionDeclarationNode& func_decl = func_node.as<FunctionDeclarationNode>();
						
						// For static storage duration, also try non-constexpr functions with simple bodies
						// (static initializers can call any function whose body is available)
						bool can_evaluate = func_decl.is_constexpr() || func_decl.is_consteval() ||
							(context.storage_duration == ConstExpr::StorageDuration::Static);
						if (can_evaluate) {
							// Get the function body
							const auto& definition = func_decl.get_definition();
							if (definition.has_value()) {
								// Evaluate arguments
								const auto& arguments = func_call.arguments();
								const auto& parameters = func_decl.parameter_nodes();
								
								// This parameter count check implicitly ensures we're calling static members:
								// Non-static members would have a conceptual 'this' parameter that we're not providing
								if (arguments.size() == parameters.size()) {
									std::unordered_map<std::string_view, EvalResult> empty_bindings;
									return evaluate_function_call_with_template_context(
										func_decl,
										arguments,
										empty_bindings,
										context,
										&type_info);
								}
							}
						}
					}
				}
			}
		}
		
		// Check if this is a compiler builtin function (starts with __builtin)
		if (func_name.starts_with("__builtin")) {
			auto builtin_result = evaluate_builtin_function(func_name, func_call.arguments(), context);
			if (builtin_result.success()) {
				return builtin_result;
			}
			// Builtin evaluation failed - propagate the specific error
			// (e.g., "argument must be an integer", "LLONG_MIN is undefined")
			return builtin_result;
		}
		
		// Try variable template instantiation before giving up
		// Variable templates like __is_ratio_v<T> might not be in the symbol table
		// but can be instantiated from the template registry
		if (func_call.has_template_arguments() && context.parser) {
			auto var_template_result = tryEvaluateAsVariableTemplate(func_name, func_call, context);
			if (var_template_result.success()) return var_template_result;
		}
		
		return EvalResult::error("Undefined function in constant expression: " + std::string(func_name));
	}

	const ASTNode& symbol_node = symbol_opt.value();
	
	// Check if it's a TemplateVariableDeclarationNode (variable template like __is_ratio_v<T>)
	// These get parsed as FunctionCallNodes because identifier<args> looks like a function call
	if (symbol_node.is<TemplateVariableDeclarationNode>()) {
		auto result = tryEvaluateAsVariableTemplate(func_name, func_call, context);
		if (result.success()) return result;
		// If variable template instantiation failed, fall through to try other lookups
	}
	
	// Check if it's a FunctionDeclarationNode (regular function)
	if (symbol_node.is<FunctionDeclarationNode>()) {
		const FunctionDeclarationNode& func_decl = symbol_node.as<FunctionDeclarationNode>();
		
		// For static storage duration, also try non-constexpr functions with simple bodies
		// (static initializers can call any function whose body is available)
		if (!func_decl.is_constexpr() && !func_decl.is_consteval() && context.storage_duration != ConstExpr::StorageDuration::Static) {
			return EvalResult::error("Function in constant expression must be constexpr: " + std::string(func_name));
		}

		// Get the function body
		const auto& definition = func_decl.get_definition();
		if (!definition.has_value()) {
			return EvalResult::error("Constexpr function has no body: " + std::string(func_name));
		}

		// Evaluate arguments
		const auto& arguments = func_call.arguments();
		const auto& parameters = func_decl.parameter_nodes();
		
		if (arguments.size() != parameters.size()) {
			return EvalResult::error("Function argument count mismatch in constant expression");
		}

		// Pass empty bindings for top-level function calls
		std::unordered_map<std::string_view, EvalResult> empty_bindings;
		return evaluate_function_call_with_template_context(
			func_decl,
			arguments,
			empty_bindings,
			context);
	}
	
	// Check if it's a TemplateFunctionDeclarationNode (template function)
	if (symbol_node.is<TemplateFunctionDeclarationNode>()) {
		const auto& arguments = func_call.arguments();
		
		// Try to find or instantiate the function with the given arguments
		// First, try to find an already-instantiated version in the symbol table
		// Try both qualified and unqualified names
		std::vector<ASTNode> all_overloads = context.symbols->lookup_all(qualified_name);
		if (all_overloads.empty() && qualified_name != func_name) {
			all_overloads = context.symbols->lookup_all(func_name);
		}
		
		// Look for a constexpr FunctionDeclarationNode that matches the argument count
		for (const auto& overload : all_overloads) {
			if (overload.is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode& candidate = overload.as<FunctionDeclarationNode>();
				if ((candidate.is_constexpr() || candidate.is_consteval()) && 
				    candidate.parameter_nodes().size() == arguments.size()) {
					// Found a potential match - try to evaluate it
					std::unordered_map<std::string_view, EvalResult> empty_bindings;
					return evaluate_function_call_with_bindings(candidate, arguments, empty_bindings, context);
				}
			}
		}
		
		// No pre-instantiated version found - try to instantiate on-demand if parser is available
		if (context.parser) {
			// Use shared helper to deduce template arguments from function call arguments
			std::vector<TemplateTypeArg> deduced_args = TemplateInstantiationHelper::deduceTemplateArgsFromCall(arguments);
			
			// Try to instantiate even if we have fewer deduced args than template params
			// The template might have default parameters that can fill in the rest
			if (!deduced_args.empty()) {
				// Use shared helper to try instantiation with various name variations
				auto instantiated_opt = TemplateInstantiationHelper::tryInstantiateTemplateFunction(
					*context.parser, qualified_name, func_name, deduced_args);
				
				if (instantiated_opt.has_value() && instantiated_opt->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& instantiated_func = instantiated_opt->as<FunctionDeclarationNode>();
					if (instantiated_func.is_constexpr() || instantiated_func.is_consteval()) {
						// Successfully instantiated - evaluate it
						std::unordered_map<std::string_view, EvalResult> empty_bindings;
						return evaluate_function_call_with_bindings(instantiated_func, arguments, empty_bindings, context);
					}
				} else if (instantiated_opt.has_value()) {
					FLASH_LOG(Templates, Debug, "Instantiation succeeded but result is not a FunctionDeclarationNode");
				}
			} else {
				FLASH_LOG(Templates, Debug, "No template arguments could be deduced from function call arguments");
			}
		}
		
		// No pre-instantiated version found and couldn't instantiate on-demand
		// Return a specific error indicating this is a template function issue
		return EvalResult::error("Template function in constant expression - instantiation required: " + std::string(qualified_name), 
		                         EvalErrorType::TemplateDependentExpression);
	}
	
	// Check if it's a VariableDeclarationNode (could be a lambda/functor callable object)
	if (symbol_node.is<VariableDeclarationNode>()) {
		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		return evaluate_callable_object(var_decl, func_call.arguments(), context);
	}
	
	
	return EvalResult::error("Identifier is not a function or callable object: " + std::string(func_name));
}

void Evaluator::load_template_bindings_from_type(const TypeInfo* source_type, EvaluationContext& context) {
	if (!source_type || !source_type->isTemplateInstantiation()) {
		return;
	}

	StringHandle base_template_name = source_type->baseTemplateName();
	auto param_handles = gTemplateRegistry.getTemplateParameters(base_template_name);
	context.template_param_names.clear();
	context.template_args.clear();
	context.template_args.reserve(source_type->templateArgs().size());
	if (param_handles.empty()) {
		auto template_node_opt = gTemplateRegistry.lookupTemplate(base_template_name);
		if (template_node_opt.has_value() && template_node_opt->is<TemplateClassDeclarationNode>()) {
			const auto& template_param_names = template_node_opt->as<TemplateClassDeclarationNode>().template_param_names();
			context.template_param_names.reserve(template_param_names.size());
			for (std::string_view param_name : template_param_names) {
				context.template_param_names.push_back(param_name);
			}
		}
	} else {
		context.template_param_names.reserve(param_handles.size());
		for (StringHandle param_handle : param_handles) {
			context.template_param_names.push_back(StringTable::getStringView(param_handle));
		}
	}
	for (const auto& arg_info : source_type->templateArgs()) {
		context.template_args.push_back(toTemplateTypeArg(arg_info));
	}
}

bool Evaluator::try_load_current_struct_template_bindings(EvaluationContext& context) {
	if (context.struct_info == nullptr) {
		return false;
	}

	if (const LazyClassInstantiationInfo* lazy_class_info =
			LazyClassInstantiationRegistry::getInstance().getLazyClassInfo(context.struct_info->name)) {
		context.template_param_names.clear();
		context.template_args = lazy_class_info->template_args;
		context.template_param_names.reserve(lazy_class_info->template_params.size());
		for (const auto& template_param : lazy_class_info->template_params) {
			if (template_param.is<TemplateParameterNode>()) {
				context.template_param_names.push_back(template_param.as<TemplateParameterNode>().name());
			}
		}
		return true;
	}

	auto current_struct_it = gTypesByName.find(context.struct_info->name);
	if (current_struct_it == gTypesByName.end()) {
		return false;
	}

	load_template_bindings_from_type(current_struct_it->second, context);
	return !context.template_param_names.empty() || !context.template_args.empty();
}

EvalResult Evaluator::evaluate_function_call_with_template_context(
	const FunctionDeclarationNode& func_decl,
	const ChunkedVector<ASTNode>& arguments,
	const std::unordered_map<std::string_view, EvalResult>& outer_bindings,
	EvaluationContext& context,
	const TypeInfo* fallback_template_type,
	FunctionCallTemplateBindingLoadMode binding_load_mode) {
	auto saved_template_param_names = context.template_param_names;
	auto saved_template_args = context.template_args;
	auto restore_template_bindings = [&]() {
		context.template_param_names = std::move(saved_template_param_names);
		context.template_args = std::move(saved_template_args);
	};

	if (binding_load_mode == FunctionCallTemplateBindingLoadMode::ForceCurrentStructIfAvailable) {
		try_load_current_struct_template_bindings(context);
	} else if (context.template_param_names.empty() && context.template_args.empty()) {
		try_load_current_struct_template_bindings(context);
	}
	if (context.template_param_names.empty() && context.template_args.empty() && fallback_template_type) {
		load_template_bindings_from_type(fallback_template_type, context);
	}

	EvalResult result = evaluate_function_call_with_bindings(func_decl, arguments, outer_bindings, context);
	restore_template_bindings();
	return result;
}

EvalResult Evaluator::evaluate_function_call_with_bindings(
	const FunctionDeclarationNode& func_decl,
	const ChunkedVector<ASTNode>& arguments,
	const std::unordered_map<std::string_view, EvalResult>& outer_bindings,
	EvaluationContext& context) {
	
	// Check recursion depth
	if (context.current_depth >= context.max_recursion_depth) {
		return EvalResult::error("Constexpr recursion depth limit exceeded");
	}

	// Get the function body
	const auto& definition = func_decl.get_definition();
	if (!definition.has_value()) {
		return EvalResult::error("Constexpr function has no body");
	}

	// Evaluate arguments
	const auto& parameters = func_decl.parameter_nodes();
	
	if (arguments.size() != parameters.size()) {
		return EvalResult::error("Function argument count mismatch in constant expression");
	}

	// Create a new symbol table scope for the function
	// We'll use a simple map to bind parameters to their evaluated values
	std::unordered_map<std::string_view, EvalResult> param_bindings;
	auto bind_result = bind_evaluated_arguments(
		parameters,
		arguments,
		param_bindings,
		context,
		"Invalid parameter node",
		&outer_bindings);
	if (!bind_result.success()) {
		return bind_result;
	}

	auto saved_template_param_names = context.template_param_names;
	auto saved_template_args = context.template_args;
	auto restore_template_bindings = [&]() {
		context.template_param_names = std::move(saved_template_param_names);
		context.template_args = std::move(saved_template_args);
	};

	if (func_decl.has_outer_template_bindings()) {
		context.template_param_names.clear();
		context.template_args.clear();
		context.template_param_names.reserve(func_decl.outer_template_param_names().size());
		context.template_args.reserve(func_decl.outer_template_args().size());
		for (StringHandle param_name : func_decl.outer_template_param_names()) {
			context.template_param_names.push_back(StringTable::getStringView(param_name));
		}
		for (const auto& arg : func_decl.outer_template_args()) {
			context.template_args.push_back(toTemplateTypeArg(arg));
		}
	}

	// Increase recursion depth
	context.current_depth++;

	// Record the function's return type so that aggregate-initializer returns
	// (e.g., return {0, 0} in a struct-returning function) can resolve member names.
	const TypeInfo* saved_return_type_info = context.return_type_info;
	context.return_type_info = nullptr;
	if (func_decl.decl_node().type_node().is<TypeSpecifierNode>()) {
		const TypeSpecifierNode& ret_spec =
			func_decl.decl_node().type_node().as<TypeSpecifierNode>();
		TypeIndex ret_idx = ret_spec.type_index();
		if (ret_idx.is_valid() && ret_idx.value < gTypeInfo.size())
			context.return_type_info = &gTypeInfo[ret_idx.value];
	}
	
	std::unordered_map<std::string_view, EvalResult> local_bindings = param_bindings;
	auto result = evaluate_block_with_bindings(
		definition.value(),
		local_bindings,
		context,
		"Function body is not a block",
		"Constexpr function did not return a value");

	context.current_depth--;
	context.return_type_info = saved_return_type_info;
	restore_template_bindings();
	return result;
}

EvalResult Evaluator::bind_evaluated_arguments(
	const std::vector<ASTNode>& parameters,
	const ChunkedVector<ASTNode>& arguments,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	std::string_view invalid_parameter_error,
	const std::unordered_map<std::string_view, EvalResult>* outer_bindings,
	bool skip_invalid_params) {
	for (size_t i = 0; i < parameters.size() && i < arguments.size(); ++i) {
		const ASTNode& param_node = parameters[i];
		if (!param_node.is<DeclarationNode>()) {
			if (skip_invalid_params) {
				continue;
			}
			return EvalResult::error(std::string(invalid_parameter_error));
		}

		EvalResult arg_result = outer_bindings
			? evaluate_expression_with_bindings_const(arguments[i], *outer_bindings, context)
			: evaluate(arguments[i], context);
		if (!arg_result.success()) {
			return arg_result;
		}

		const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
		maybe_set_exact_type_from_declaration(arg_result, param_decl);
		bindings[param_decl.identifier_token().value()] = arg_result;
	}

	return EvalResult::from_bool(true);
}

EvalResult Evaluator::bind_pre_evaluated_arguments(
	const std::vector<ASTNode>& parameters,
	const std::vector<EvalResult>& evaluated_arguments,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	std::string_view invalid_parameter_error,
	bool skip_invalid_params) {
	for (size_t i = 0; i < parameters.size() && i < evaluated_arguments.size(); ++i) {
		const ASTNode& param_node = parameters[i];
		if (!param_node.is<DeclarationNode>()) {
			if (skip_invalid_params) {
				continue;
			}
			return EvalResult::error(std::string(invalid_parameter_error));
		}

		const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
		EvalResult arg_result = evaluated_arguments[i];
		maybe_set_exact_type_from_declaration(arg_result, param_decl);
		bindings[param_decl.identifier_token().value()] = std::move(arg_result);
	}

	return EvalResult::from_bool(true);
}

EvalResult Evaluator::evaluate_block_with_bindings(
	const ASTNode& body_node,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	std::string_view non_block_error,
	std::string_view no_return_error) {
	if (!body_node.is<BlockNode>()) {
		return EvalResult::error(std::string(non_block_error));
	}

	const BlockNode& body = body_node.as<BlockNode>();
	const auto& statements = body.get_statements();

	// Install a scope tracker for this block.  The guard automatically
	// removes newly-declared variables and restores any shadowed outer
	// values when it goes out of scope, even on early returns.
	// Use resolve_declaration_bindings so the guard targets the same map
	// that variable declarations are written to (important when
	// context.local_bindings is non-null, e.g. constructor body eval).
	auto& decl_bindings = context.resolve_declaration_bindings(bindings);
	BlockScopeGuard guard(decl_bindings, context.current_scope);

	for (size_t i = 0; i < statements.size(); i++) {
		auto result = evaluate_statement_with_bindings(statements[i], bindings, context);
		if (result.success()) {
			return result;
		}
		if (!isStatementExecutedWithoutReturn(result)) {
			return result;
		}
	}

	return EvalResult::error(std::string(no_return_error));
}

EvalResult Evaluator::evaluate_statement_with_bindings(
	const ASTNode& stmt_node,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context) {
	
	// Check if it's a return statement
	if (stmt_node.is<ReturnStatementNode>()) {
		const ReturnStatementNode& ret_stmt = stmt_node.as<ReturnStatementNode>();
		const auto& return_expr = ret_stmt.expression();
		
		if (!return_expr.has_value()) {
			return EvalResult::error("Constexpr function return statement has no expression");
		}

		// Handle aggregate-initializer return: return {x, y, ...} in a struct-returning function.
		// evaluate_expression_with_bindings only accepts ExpressionNode; bypass it here.
		if (return_expr.value().is<InitializerListNode>() && context.return_type_info) {
			const StructTypeInfo* si = context.return_type_info->getStructInfo();
			if (si) {
				const InitializerListNode& init_list = return_expr.value().as<InitializerListNode>();
				EvalResult result = EvalResult::from_int(0LL); // struct result; value is a placeholder
				// Set the struct type index so downstream member-function chains can identify the type.
				for (size_t ti = 0; ti < gTypeInfo.size(); ++ti) {
					if (&gTypeInfo[ti] == context.return_type_info) {
						result.object_type_index = TypeIndex{ti};
						break;
					}
				}
				size_t positional_idx = 0;
				for (size_t i = 0; i < init_list.size() && positional_idx < si->members.size(); ++i) {
					const auto& member = si->members[positional_idx++];
					auto element_result = evaluate_expression_with_bindings(
						init_list.initializers()[i], bindings, context);
					if (!element_result.success()) return element_result;
					result.object_member_bindings[StringTable::getStringView(member.getName())] =
						std::move(element_result);
				}
				return result;
			}
		}

		return evaluate_expression_with_bindings(return_expr.value(), bindings, context);
	}
	
	// Handle variable declarations
	if (stmt_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = stmt_node.as<VariableDeclarationNode>();
			const DeclarationNode& decl = var_decl.declaration_node().as<DeclarationNode>();
			std::string_view var_name = decl.identifier_token().value();
			auto& declaration_bindings = context.resolve_declaration_bindings(bindings);

			// Register this declaration with the current block scope tracker so it
			// can be cleaned up (or the shadowed outer value restored) on block exit.
			if (context.current_scope) {
				context.current_scope->on_declare(var_name, declaration_bindings);
			}

			// Evaluate the initializer if present
			if (var_decl.initializer().has_value()) {
				const ASTNode& init_expr = var_decl.initializer().value();

				if (extract_lambda_from_initializer(var_decl.initializer())) {
					EvalResult callable_result = EvalResult::from_callable(var_decl);
					const LambdaExpressionNode* lambda = extract_lambda_from_initializer(var_decl.initializer());
					if (lambda) {
						std::unordered_map<std::string_view, EvalResult> merged_outer_bindings;
						const std::unordered_map<std::string_view, EvalResult>* capture_bindings = &bindings;
						if (context.local_bindings) {
							merged_outer_bindings = bindings;
							for (const auto& [name, value] : *context.local_bindings) {
								merged_outer_bindings[name] = value;
							}
							capture_bindings = &merged_outer_bindings;
						}
						auto capture_result = evaluate_lambda_captures(lambda->captures(), callable_result.callable_bindings, context, capture_bindings);
						if (!capture_result.success()) {
							return capture_result;
						}
					}
					declaration_bindings[var_name] = std::move(callable_result);
					return EvalResult::error("Statement executed (not a return)");
				}

				// Handle InitializerListNode initializers that the parser preserves for arrays
				// and aggregate/object brace-init. Scalar brace-init (e.g. int x{5} / int x = {5})
				// is normalized by parse_brace_initializer() into the contained expression and
				// should not reach this branch as an InitializerListNode.
				if (init_expr.is<InitializerListNode>()) {
					const InitializerListNode& init_list = init_expr.as<InitializerListNode>();
					if (decl.is_array()) {
						if (decl.type_node().is<TypeSpecifierNode>()) {
							const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
							auto array_result = materialize_array_value(type_spec.type(), type_spec.type_index(), init_list, context, &bindings);
							if (!array_result.success()) {
								return array_result;
							}
							declaration_bindings[var_name] = std::move(array_result);
							return EvalResult::error("Statement executed (not a return)");
						}

						auto array_result = materialize_array_value(Type::Auto, TypeIndex{}, init_list, context, &bindings);
						if (!array_result.success()) {
							return array_result;
						}
						declaration_bindings[var_name] = std::move(array_result);
						return EvalResult::error("Statement executed (not a return)");
					}

					if (decl.type_node().is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
						if ((type_spec.type() == Type::Struct || type_spec.type() == Type::UserDefined) &&
							type_spec.type_index().is_valid() && type_spec.type_index().value < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_spec.type_index().value];
							if (const StructTypeInfo* struct_info = type_info.getStructInfo()) {
								auto object_result = materialize_aggregate_object_value(struct_info, type_spec.type_index(), init_list, context);
								if (!object_result.success()) {
									return object_result;
								}
								maybe_set_binding_result_exact_type(object_result, decl, &init_expr, context);
								declaration_bindings[var_name] = std::move(object_result);
								return EvalResult::error("Statement executed (not a return)");
							}
						}
					}
				}

				const ConstructorCallNode* ctor_call = nullptr;
				if (init_expr.is<ConstructorCallNode>()) {
					ctor_call = &init_expr.as<ConstructorCallNode>();
				} else if (init_expr.is<ExpressionNode>()) {
					const ExpressionNode& expr = init_expr.as<ExpressionNode>();
					if (const auto* constructor_call = std::get_if<ConstructorCallNode>(&expr)) {
						ctor_call = constructor_call;
					}
				}

				if (ctor_call && decl.type_node().is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
					if (type_spec.type() == Type::Struct || type_spec.type() == Type::UserDefined) {
						auto object_result = materialize_constructor_object_value(*ctor_call, context, &bindings);
						if (!object_result.success()) {
							return object_result;
						}
						maybe_set_binding_result_exact_type(object_result, decl, &init_expr, context);
						declaration_bindings[var_name] = std::move(object_result);
						return EvalResult::error("Statement executed (not a return)");
					}
				}

				// Regular expression initializer
				auto init_result = evaluate_expression_with_bindings(init_expr, bindings, context);
				if (!init_result.success()) {
					return init_result;
				}

				maybe_set_binding_result_exact_type(init_result, decl, &init_expr, context);

				// Add to bindings
				declaration_bindings[var_name] = init_result;
				return EvalResult::error("Statement executed (not a return)");
			}

			// Uninitialized variable - set to 0
				EvalResult default_result = EvalResult::from_int(0);
				maybe_set_binding_result_exact_type(default_result, decl, nullptr, context);
				declaration_bindings[var_name] = std::move(default_result);
				return EvalResult::error("Statement executed (not a return)");
	}
	
	// Handle for loops (C++14 constexpr)
	if (stmt_node.is<ForStatementNode>()) {
		const ForStatementNode& for_stmt = stmt_node.as<ForStatementNode>();
		
		// The for-loop init variable (e.g. `int i = 0`) is scoped to the entire
		// loop (init + condition + body + update), not to the outer block.
		// The guard automatically cleans it up when the for-loop exits.
		BlockScopeGuard loop_guard(context.resolve_declaration_bindings(bindings), context.current_scope);
		
		// Execute init statement if present
		if (for_stmt.has_init()) {
			auto init_result = evaluate_statement_with_bindings(for_stmt.get_init_statement().value(), bindings, context);
			if (!isStatementExecutedWithoutReturn(init_result)) {
				return init_result;
			}
		}
		
		// Loop until condition is false
		while (true) {
			// Check complexity limit
			if (++context.step_count > context.max_steps) {
				return EvalResult::error("Constexpr evaluation exceeded complexity limit in for loop");
			}
			
			// Evaluate condition if present
			if (for_stmt.has_condition()) {
				auto cond_result = evaluate_expression_with_bindings(for_stmt.get_condition().value(), bindings, context);
				if (!cond_result.success()) {
					return cond_result;
				}
				if (!cond_result.as_bool()) {
					break;  // Exit loop when condition is false
				}
			}
			
			auto body_result = evaluate_statement_with_bindings(for_stmt.get_body_statement(), bindings, context);
			if (body_result.success()) {
				return body_result;
			}
			if (isBreakExecuted(body_result)) {
				break;  // break statement exits the loop
			}
			if (isContinueExecuted(body_result)) {
				// continue: skip to update expression, then next iteration
			} else if (!isStatementExecutedWithoutReturn(body_result)) {
				return body_result;
			}
			
			// Execute update expression if present
			if (for_stmt.has_update()) {
				evaluate_expression_with_bindings(for_stmt.get_update_expression().value(), bindings, context);
			}
		}
		
		return EvalResult::error("Statement executed (not a return)");
	}
	
	// Handle while loops (C++14 constexpr)
	if (stmt_node.is<WhileStatementNode>()) {
		const WhileStatementNode& while_stmt = stmt_node.as<WhileStatementNode>();
		
		while (true) {
			// Check complexity limit
			if (++context.step_count > context.max_steps) {
				return EvalResult::error("Constexpr evaluation exceeded complexity limit in while loop");
			}
			
			// Evaluate condition
			auto cond_result = evaluate_expression_with_bindings(while_stmt.get_condition(), bindings, context);
			if (!cond_result.success()) {
				return cond_result;
			}
			if (!cond_result.as_bool()) {
				break;  // Exit loop when condition is false
			}
			
			auto body_result = evaluate_statement_with_bindings(while_stmt.get_body_statement(), bindings, context);
			if (body_result.success()) {
				return body_result;
			}
			if (isBreakExecuted(body_result)) {
				break;  // break statement exits the loop
			}
			if (isContinueExecuted(body_result)) {
				continue;  // continue: go to next iteration
			}
			if (!isStatementExecutedWithoutReturn(body_result)) {
				return body_result;
			}
		}
		
		return EvalResult::error("Statement executed (not a return)");
	}
	
	// Handle if statements (C++14 constexpr)
	if (stmt_node.is<IfStatementNode>()) {
		const IfStatementNode& if_stmt = stmt_node.as<IfStatementNode>();
		
		// The if-init variable (C++17: `if (int x = foo(); x > 0)`) is scoped to
		// the entire if statement (condition + then + else), not to the outer block.
		// Only create the scope guard when there is actually an init statement,
		// since the vast majority of if-statements have no C++17 init-statement
		// and the guard would just add overhead (default-constructing a
		// BlockScopeTracker with its std::vector and std::unordered_map).
		std::optional<BlockScopeGuard> if_guard;
		if (if_stmt.has_init()) {
			if_guard.emplace(context.resolve_declaration_bindings(bindings), context.current_scope);
			auto init_result = evaluate_statement_with_bindings(if_stmt.get_init_statement().value(), bindings, context);
			if (!isStatementExecutedWithoutReturn(init_result)) {
				return init_result;
			}
		}
		
		// Evaluate condition
		auto cond_result = evaluate_expression_with_bindings(if_stmt.get_condition(), bindings, context);
		if (!cond_result.success()) {
			return cond_result;
		}
		
		// Execute then or else branch.
		// Both success() and non-trivial errors propagate; only
		// kStatementExecutedWithoutReturn is silently absorbed (the if
		// statement itself "executed without a return").
		if (cond_result.as_bool()) {
			auto then_result = evaluate_statement_with_bindings(if_stmt.get_then_statement(), bindings, context);
			if (then_result.success() || !isStatementExecutedWithoutReturn(then_result)) {
				return then_result;
			}
		} else if (if_stmt.has_else()) {
			// Fix dangling reference warning by storing the value first
			std::optional<ASTNode> else_stmt_opt = if_stmt.get_else_statement();
			if (else_stmt_opt.has_value()) {
				auto else_result = evaluate_statement_with_bindings(*else_stmt_opt, bindings, context);
				if (else_result.success() || !isStatementExecutedWithoutReturn(else_result)) {
					return else_result;
				}
			}
		}
		
		return EvalResult::error("Statement executed (not a return)");
	}
	
	// Handle expression statements (assignments, increments, etc.)
	if (stmt_node.is<ExpressionNode>()) {
		// Evaluate the expression (which may have side effects like assignments)
		auto result = evaluate_expression_with_bindings(stmt_node, bindings, context);
		// Propagate evaluation errors (e.g., failed assignment RHS).
		// Successful results are discarded — expression statements don't produce
		// return values for the caller; only the side effects (bindings mutations) matter.
		if (!result.success()) {
			return result;
		}
		return EvalResult::error("Statement executed (not a return)");
	}
	
	// Handle block statements (nested blocks)
	if (stmt_node.is<BlockNode>()) {
		return evaluate_block_with_bindings(
			stmt_node,
			bindings,
			context,
			"Constexpr block is not a block",
			kStatementExecutedWithoutReturn);
	}
	
	// Handle break statements
	if (stmt_node.is<BreakStatementNode>()) {
		return EvalResult::error(std::string(kBreakExecuted));
	}
	
	// Handle continue statements
	if (stmt_node.is<ContinueStatementNode>()) {
		return EvalResult::error(std::string(kContinueExecuted));
	}
	
	// Handle range-based for loops over arrays (C++11/C++14 constexpr)
	if (stmt_node.is<RangedForStatementNode>()) {
		const RangedForStatementNode& ranged_for = stmt_node.as<RangedForStatementNode>();
		
		// The loop variable and any C++20 init variable are scoped to the
		// range-for loop, not to the surrounding block.
		// The guard automatically cleans them up on any exit path.
		auto& range_decl_bindings = context.resolve_declaration_bindings(bindings);
		BlockScopeGuard loop_guard(range_decl_bindings, context.current_scope);
		
		// Execute optional init statement (C++20 feature)
		if (ranged_for.has_init_statement()) {
			auto init_result = evaluate_statement_with_bindings(*ranged_for.get_init_statement(), bindings, context);
			if (!isStatementExecutedWithoutReturn(init_result)) {
				return init_result;
			}
		}
		
		// Evaluate the range expression to get the array
		auto range_result = evaluate_expression_with_bindings(ranged_for.get_range_expression(), bindings, context);
		if (!range_result.success()) {
			return range_result;
		}
		
		// Only support array iteration for now
		if (!range_result.is_array || range_result.array_elements.empty()) {
			return EvalResult::error("Range-based for: range is not an array or has no elements in constexpr context");
		}
		
		// Get the loop variable name from the declaration
		const ASTNode& loop_var_decl_node = ranged_for.get_loop_variable_decl();
		std::string_view loop_var_name;
		if (loop_var_decl_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = loop_var_decl_node.as<VariableDeclarationNode>();
			if (var_decl.declaration_node().is<DeclarationNode>()) {
				loop_var_name = var_decl.declaration_node().as<DeclarationNode>().identifier_token().value();
			}
		}
		if (loop_var_name.empty()) {
			return EvalResult::error("Range-based for: could not determine loop variable name");
		}
		
		// Register the loop variable with the scope guard so it is cleaned up
		// when the loop ends (also handles the shadowing case if the outer scope
		// already has a variable with the same name).
		loop_guard.scope.on_declare(loop_var_name, range_decl_bindings);
		
		// Iterate over array elements
		for (const EvalResult& element : range_result.array_elements) {
			// Check complexity limit
			if (++context.step_count > context.max_steps) {
				return EvalResult::error("Constexpr evaluation exceeded complexity limit in range-based for loop");
			}
			
			// Bind the loop variable to the current element (overwrites on each iteration)
			range_decl_bindings[loop_var_name] = element;
			
			// Execute the body
			auto body_result = evaluate_statement_with_bindings(ranged_for.get_body_statement(), bindings, context);
			if (body_result.success()) {
				return body_result;
			}
			if (isBreakExecuted(body_result)) {
				break;  // break exits the loop
			}
			if (isContinueExecuted(body_result)) {
				continue;  // continue goes to next element
			}
			if (!isStatementExecutedWithoutReturn(body_result)) {
				return body_result;
			}
		}
		
		return EvalResult::error(std::string(kStatementExecutedWithoutReturn));
	}
	
	// Handle switch statements (C++14 constexpr)
	if (stmt_node.is<SwitchStatementNode>()) {
		const SwitchStatementNode& switch_stmt = stmt_node.as<SwitchStatementNode>();
		
		// Evaluate the switch condition
		auto cond_result = evaluate_expression_with_bindings(switch_stmt.get_condition(), bindings, context);
		if (!cond_result.success()) {
			return cond_result;
		}
		
		// Get the switch body (must be a BlockNode)
		const ASTNode& body_node = switch_stmt.get_body();
		if (!body_node.is<BlockNode>()) {
			return EvalResult::error("Switch body is not a block");
		}
		const BlockNode& body = body_node.as<BlockNode>();
		const auto& stmts = body.get_statements();
		const size_t num_stmts = stmts.size();
		
		// First pass: find the matching case index (or default index)
		size_t start_index = num_stmts;  // No match found yet
		size_t default_index = num_stmts;  // No default found yet
		
		auto to_long_long = [](const EvalResult& r) -> long long {
			return std::visit([](auto v) -> long long { return static_cast<long long>(v); }, r.value);
		};
		
		for (size_t i = 0; i < num_stmts; i++) {
			const ASTNode& s = stmts[i];
			if (s.is<CaseLabelNode>()) {
				const CaseLabelNode& case_node = s.as<CaseLabelNode>();
				auto case_val = evaluate_expression_with_bindings(case_node.get_case_value(), bindings, context);
				if (!case_val.success()) {
					return case_val;
				}
				// Compare condition with case value
				if (to_long_long(cond_result) == to_long_long(case_val) && start_index == num_stmts) {
					start_index = i;
				}
			} else if (s.is<DefaultLabelNode>()) {
				default_index = i;
			}
		}
		
		// If no case matched, use default (or skip if no default)
		if (start_index == num_stmts) {
			start_index = default_index;
		}
		
		if (start_index == num_stmts) {
			// No matching case or default — switch does nothing
			return EvalResult::error(std::string(kStatementExecutedWithoutReturn));
		}
		
		// Second pass: execute statements starting from the matching case
		for (size_t i = start_index; i < num_stmts; i++) {
			const ASTNode& s = stmts[i];
			ASTNode block_to_exec;
			if (s.is<CaseLabelNode>()) {
				const CaseLabelNode& case_node = s.as<CaseLabelNode>();
				if (!case_node.has_statement()) {
					continue;  // Empty case label — fall through to next
				}
				block_to_exec = *case_node.get_statement();
			} else if (s.is<DefaultLabelNode>()) {
				const DefaultLabelNode& default_node = s.as<DefaultLabelNode>();
				if (!default_node.has_statement()) {
					continue;  // Empty default label — fall through
				}
				block_to_exec = *default_node.get_statement();
			} else {
				// Unexpected non-label node in switch body
				continue;
			}
			
			// Execute the block of statements for this case/default.
			// The parser normally wraps case bodies in a BlockNode, but
			// handle bare statements (e.g. a single ReturnStatementNode)
			// gracefully in case the AST representation ever changes.
			EvalResult block_result = block_to_exec.is<BlockNode>()
				? evaluate_block_with_bindings(
					block_to_exec,
					bindings,
					context,
					"Switch case body is not a block",
					kStatementExecutedWithoutReturn)
				: evaluate_statement_with_bindings(block_to_exec, bindings, context);
			
			if (block_result.success()) {
				return block_result;  // Propagate return value
			}
			if (isBreakExecuted(block_result)) {
				return EvalResult::error(std::string(kStatementExecutedWithoutReturn));  // break exits switch
			}
			if (!isStatementExecutedWithoutReturn(block_result)) {
				return block_result;  // Propagate other errors
			}
			// Fall through to next case
		}
		
		return EvalResult::error(std::string(kStatementExecutedWithoutReturn));
	}
	
	return EvalResult::error("Unsupported statement type in constexpr function");
}

// Overload for mutable bindings (used in statements with side effects like assignments)

} // namespace ConstExpr
