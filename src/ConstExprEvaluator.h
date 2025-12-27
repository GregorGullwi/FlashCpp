#pragma once

#include "AstNodeTypes.h"
#include <optional>
#include <string>
#include <variant>
#include <climits>  // For LLONG_MAX, LLONG_MIN

// Forward declarations
class SymbolTable;
struct TypeInfo;

namespace ConstExpr {

// Result of constant expression evaluation
struct EvalResult {
	bool success;
	std::variant<
		bool,                    // Boolean constant
		long long,               // Signed integer constant
		unsigned long long,      // Unsigned integer constant
		double                   // Floating-point constant
	> value;
	std::string error_message;
	
	// Array support for local arrays in constexpr functions
	bool is_array = false;
	std::vector<int64_t> array_values;

	// Convenience constructors
	static EvalResult from_bool(bool val) {
		return EvalResult{true, val, ""};
	}

	static EvalResult from_int(long long val) {
		return EvalResult{true, val, ""};
	}

	static EvalResult from_uint(unsigned long long val) {
		return EvalResult{true, val, ""};
	}

	static EvalResult from_double(double val) {
		return EvalResult{true, val, ""};
	}

	static EvalResult error(const std::string& msg) {
		return EvalResult{false, false, msg};
	}

	// Convenience helpers for common operations
	bool as_bool() const {
		if (!success) return false;
		
		// Any non-zero value is true
		if (std::holds_alternative<bool>(value)) {
			return std::get<bool>(value);
		} else if (std::holds_alternative<long long>(value)) {
			return std::get<long long>(value) != 0;
		} else if (std::holds_alternative<unsigned long long>(value)) {
			return std::get<unsigned long long>(value) != 0;
		} else if (std::holds_alternative<double>(value)) {
			return std::get<double>(value) != 0.0;
		}
		return false;
	}

	long long as_int() const {
		if (!success) return 0;
		
		if (std::holds_alternative<bool>(value)) {
			return std::get<bool>(value) ? 1 : 0;
		} else if (std::holds_alternative<long long>(value)) {
			return std::get<long long>(value);
		} else if (std::holds_alternative<unsigned long long>(value)) {
			return static_cast<long long>(std::get<unsigned long long>(value));
		} else if (std::holds_alternative<double>(value)) {
			return static_cast<long long>(std::get<double>(value));
		}
		return 0;
	}

	double as_double() const {
		if (!success) return 0.0;
		
		if (std::holds_alternative<bool>(value)) {
			return std::get<bool>(value) ? 1.0 : 0.0;
		} else if (std::holds_alternative<long long>(value)) {
			return static_cast<double>(std::get<long long>(value));
		} else if (std::holds_alternative<unsigned long long>(value)) {
			return static_cast<double>(std::get<unsigned long long>(value));
		} else if (std::holds_alternative<double>(value)) {
			return std::get<double>(value);
		}
		return 0.0;
	}
};

// Storage duration for variable declarations
enum class StorageDuration {
	Automatic,    // Local variables (automatic storage)
	Static,       // Static locals, static members
	Thread,       // thread_local variables
	Global        // Global/namespace scope variables
};

// Context for evaluation - provides access to compile-time information
struct EvaluationContext {
	// Symbol table for looking up constexpr variables/functions (required)
	const SymbolTable* symbols;

	// Type information for sizeof, alignof, etc. (future use)
	const TypeInfo* type_info = nullptr;

	// Storage duration of the variable being evaluated (for constinit validation)
	StorageDuration storage_duration = StorageDuration::Automatic;

	// Whether we're evaluating for constinit (requires static/thread storage duration)
	bool is_constinit = false;

	// Complexity limits to prevent infinite loops during evaluation
	size_t step_count = 0;
	size_t max_steps = 1000000;

	// Maximum recursion depth for constexpr functions
	size_t max_recursion_depth = 512;

	// Track current recursion depth
	size_t current_depth = 0;

	// Constructor requires symbol table to prevent missing it
	explicit EvaluationContext(const SymbolTable& symbol_table)
		: symbols(&symbol_table) {}
};

// Main constant expression evaluator class
class Evaluator {
public:
	// Main evaluation entry point
	// Evaluates a constant expression and returns the result
	static EvalResult evaluate(const ASTNode& expr_node, EvaluationContext& context) {
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

		// Check what type of expression it is
		if (std::holds_alternative<BoolLiteralNode>(expr)) {
			return EvalResult::from_bool(std::get<BoolLiteralNode>(expr).value());
		}

		if (std::holds_alternative<NumericLiteralNode>(expr)) {
			return evaluate_numeric_literal(std::get<NumericLiteralNode>(expr));
		}

		// For BinaryOperatorNode, we need to check if it's in the variant
		if (std::holds_alternative<BinaryOperatorNode>(expr)) {
			const auto& bin_op = std::get<BinaryOperatorNode>(expr);
			return evaluate_binary_operator(bin_op.get_lhs(), bin_op.get_rhs(), bin_op.op(), context);
		}

		// For UnaryOperatorNode
		if (std::holds_alternative<UnaryOperatorNode>(expr)) {
			const auto& unary_op = std::get<UnaryOperatorNode>(expr);
			return evaluate_unary_operator(unary_op.get_operand(), unary_op.op(), context);
		}

		// For SizeofExprNode
		if (std::holds_alternative<SizeofExprNode>(expr)) {
			return evaluate_sizeof(std::get<SizeofExprNode>(expr), context);
		}

		// For AlignofExprNode
		if (std::holds_alternative<AlignofExprNode>(expr)) {
			return evaluate_alignof(std::get<AlignofExprNode>(expr), context);
		}

		// For ConstructorCallNode (type conversions like float(3.14), int(100))
		if (std::holds_alternative<ConstructorCallNode>(expr)) {
			return evaluate_constructor_call(std::get<ConstructorCallNode>(expr), context);
		}

		// For IdentifierNode (variable references like 'x' in 'constexpr int y = x + 1;')
		if (std::holds_alternative<IdentifierNode>(expr)) {
			return evaluate_identifier(std::get<IdentifierNode>(expr), context);
		}

		// For TernaryOperatorNode (condition ? true_expr : false_expr)
		if (std::holds_alternative<TernaryOperatorNode>(expr)) {
			return evaluate_ternary_operator(std::get<TernaryOperatorNode>(expr), context);
		}

		// For FunctionCallNode (constexpr function calls)
		if (std::holds_alternative<FunctionCallNode>(expr)) {
			return evaluate_function_call(std::get<FunctionCallNode>(expr), context);
		}

		// For QualifiedIdentifierNode (e.g., Template<T>::member)
		if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
			return evaluate_qualified_identifier(std::get<QualifiedIdentifierNode>(expr), context);
		}

		// For MemberAccessNode (e.g., obj.member or ptr->member)
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			return evaluate_member_access(std::get<MemberAccessNode>(expr), context);
		}

		// For MemberFunctionCallNode (e.g., obj.method() in constexpr context)
		if (std::holds_alternative<MemberFunctionCallNode>(expr)) {
			return evaluate_member_function_call(std::get<MemberFunctionCallNode>(expr), context);
		}

		// For StaticCastNode (static_cast<Type>(expr) and C-style casts)
		if (std::holds_alternative<StaticCastNode>(expr)) {
			return evaluate_static_cast(std::get<StaticCastNode>(expr), context);
		}

		// For ArraySubscriptNode (e.g., arr[0] or obj.data[1])
		if (std::holds_alternative<ArraySubscriptNode>(expr)) {
			return evaluate_array_subscript(std::get<ArraySubscriptNode>(expr), context);
		}

		// For TypeTraitExprNode (e.g., __is_void(int), __is_constant_evaluated())
		if (std::holds_alternative<TypeTraitExprNode>(expr)) {
			return evaluate_type_trait(std::get<TypeTraitExprNode>(expr));
		}

		// Other expression types are not supported as constant expressions yet
		return EvalResult::error("Expression type not supported in constant expressions");
	}

private:
	// Internal evaluation methods for different node types
	static EvalResult evaluate_numeric_literal(const NumericLiteralNode& literal) {
		const auto& value = literal.value();

		if (std::holds_alternative<unsigned long long>(value)) {
			unsigned long long val = std::get<unsigned long long>(value);
			return EvalResult::from_uint(val);
		} else if (std::holds_alternative<double>(value)) {
			double val = std::get<double>(value);
			return EvalResult::from_double(val);
		}

		return EvalResult::error("Unknown numeric literal type");
	}

	static EvalResult evaluate_binary_operator(const ASTNode& lhs_node, const ASTNode& rhs_node, 
	                                            std::string_view op, EvaluationContext& context) {
		// Recursively evaluate left and right operands
		auto lhs_result = evaluate(lhs_node, context);
		auto rhs_result = evaluate(rhs_node, context);

		if (!lhs_result.success) {
			return lhs_result;
		}
		if (!rhs_result.success) {
			return rhs_result;
		}

		return apply_binary_op(lhs_result, rhs_result, op);
	}

	static EvalResult evaluate_unary_operator(const ASTNode& operand_node, std::string_view op,
	                                           EvaluationContext& context) {
		// Recursively evaluate operand
		auto operand_result = evaluate(operand_node, context);

		if (!operand_result.success) {
			return operand_result;
		}

		return apply_unary_op(operand_result, op);
	}

	// Helper function to get struct size from gTypeInfo
	static size_t get_struct_size_from_typeinfo(const TypeSpecifierNode& type_spec) {
		if (type_spec.type() != Type::Struct) {
			return 0;
		}
		
		size_t type_index = type_spec.type_index();
		if (type_index >= gTypeInfo.size()) {
			return 0;
		}
		
		const TypeInfo& type_info = gTypeInfo[type_index];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		if (!struct_info) {
			return 0;
		}
		
		return struct_info->total_size;
	}
	
	// Helper function to get the size in bytes for a type specifier
	// Handles both primitive types and struct types
	static size_t get_typespec_size_bytes(const TypeSpecifierNode& type_spec) {
		size_t size_in_bytes = type_spec.size_in_bits() / 8;
		
		// If size_in_bits is 0, look it up
		if (size_in_bytes == 0) {
			if (type_spec.type() == Type::Struct) {
				size_in_bytes = get_struct_size_from_typeinfo(type_spec);
			} else {
				size_in_bytes = get_type_size_bits(type_spec.type()) / 8;
			}
		}
		
		return size_in_bytes;
	}

	static EvalResult evaluate_sizeof(const SizeofExprNode& sizeof_expr, EvaluationContext& context) {
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
					
					// Look up the identifier in the symbol table
					std::optional<ASTNode> symbol = context.symbols->lookup(identifier);
					if (symbol.has_value()) {
						const DeclarationNode* decl = get_decl_from_symbol(*symbol);
						if (decl) {
							// Check if it's an array
							if (decl->is_array()) {
								const auto& array_type_spec = decl->type_node().as<TypeSpecifierNode>();
								size_t element_size = get_typespec_size_bytes(array_type_spec);
								
								// Get array size from declaration
								if (decl->array_size().has_value()) {
									const ASTNode& size_expr = *decl->array_size();
									auto eval_result = evaluate(size_expr, context);
									if (eval_result.success) {
										long long array_count = eval_result.as_int();
										if (array_count > 0 && element_size > 0) {
											return EvalResult::from_int(static_cast<long long>(element_size * array_count));
										}
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
				}
				
				// size_in_bits() returns bits, convert to bytes
				unsigned long long size_in_bytes = get_typespec_size_bytes(type_spec);
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
					
					// Look up the identifier in the symbol table
					if (context.symbols) {
						auto symbol = context.symbols->lookup(id_node.name());
						if (symbol.has_value()) {
							// Get the declaration and extract the type
							const DeclarationNode* decl = get_decl_from_symbol(*symbol);
							
							if (decl) {
								// Check if it's an array - if so, calculate total size
								if (decl->is_array()) {
									const auto& type_spec = decl->type_node().as<TypeSpecifierNode>();
									size_t element_size = get_typespec_size_bytes(type_spec);
									
									// Get array size from declaration
									if (decl->array_size().has_value()) {
										const ASTNode& size_expr = *decl->array_size();
										auto eval_result = evaluate(size_expr, context);
										if (eval_result.success) {
											long long array_count = eval_result.as_int();
											if (array_count > 0 && element_size > 0) {
												return EvalResult::from_int(static_cast<long long>(element_size * array_count));
											}
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
				if (std::holds_alternative<NumericLiteralNode>(expr)) {
					const auto& lit = std::get<NumericLiteralNode>(expr);
					unsigned long long size_in_bytes = lit.sizeInBits() / 8;
					return EvalResult::from_int(static_cast<long long>(size_in_bytes));
				}
				
				// For other expressions, we would need full type inference
				// which requires tracking expression types through the AST
				// This is a compiler limitation, not a C++20 limitation
				return EvalResult::error("sizeof with complex expression not yet supported in constexpr");
			}
		}
		
		return EvalResult::error("Invalid sizeof operand");
	}

	static EvalResult evaluate_alignof(const AlignofExprNode& alignof_expr, EvaluationContext& context) {
		// alignof is always a constant expression
		// Get the actual alignment from the type
		if (alignof_expr.is_type()) {
			// alignof(type) - get alignment from TypeSpecifierNode
			const auto& type_node = alignof_expr.type_or_expr();
			if (type_node.is<TypeSpecifierNode>()) {
				const auto& type_spec = type_node.as<TypeSpecifierNode>();
				
				// For struct types, look up alignment from type info
				if (type_spec.type() == Type::Struct) {
					size_t type_index = type_spec.type_index();
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
										size_t type_index = type_spec.type_index();
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

	static EvalResult evaluate_constructor_call(const ConstructorCallNode& ctor_call, EvaluationContext& context) {
		// Constructor calls like float(3.14), int(100), double(2.718)
		// These are essentially type conversions/casts in constant expressions
		// Get the argument(s) - for basic type conversions, should have exactly 1 argument
		const auto& args = ctor_call.arguments();
		if (args.size() != 1) {
			return EvalResult::error("Constructor call must have exactly 1 argument for constant evaluation");
		}

		// Get the target type
		const ASTNode& type_node = ctor_call.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Constructor call without valid type specifier");
		}
		
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		return evaluate_expr_node(type_spec.type(), args[0], context, "Unsupported type in constructor call for constant evaluation");
	}

	static EvalResult evaluate_static_cast(const StaticCastNode& cast_node, EvaluationContext& context) {
		// Evaluate static_cast<Type>(expr) and C-style casts in constant expressions
		
		// Get the target type
		const ASTNode& type_node = cast_node.target_type();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Cast without valid type specifier");
		}
		
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		
		// Evaluate the expression being cast
		return evaluate_expr_node(type_spec.type(), cast_node.expr(), context, "Unsupported type in static_cast for constant evaluation");
	}

	static EvalResult evaluate_expr_node(Type target_type, const ASTNode& expr, EvaluationContext& context, const char* invalidTypeErrorStr) {
		auto expr_result = evaluate(expr, context);
		if (!expr_result.success) {
			return expr_result;
		}
		
		// Perform the type conversion
		switch (target_type) {
			case Type::Bool:
				return EvalResult::from_bool(expr_result.as_bool());
			
			case Type::Char:
			case Type::Short:
			case Type::Int:
			case Type::Long:
			case Type::LongLong:
				return EvalResult::from_int(expr_result.as_int());
			
			case Type::UnsignedChar:
			case Type::UnsignedShort:
			case Type::UnsignedInt:
			case Type::UnsignedLong:
			case Type::UnsignedLongLong:
				// For unsigned types, convert to unsigned
				return EvalResult::from_uint(static_cast<unsigned long long>(expr_result.as_int()));
			
			case Type::Float:
			case Type::Double:
			case Type::LongDouble:
				return EvalResult::from_double(expr_result.as_double());
			
			default:
				return EvalResult::error(invalidTypeErrorStr);
		}
	}

	static EvalResult evaluate_identifier(const IdentifierNode& identifier, EvaluationContext& context) {
		// Look up the identifier in the symbol table
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate variable reference: no symbol table provided");
		}

		std::string_view var_name = identifier.name();
		auto symbol_opt = context.symbols->lookup(var_name);
		if (!symbol_opt.has_value()) {
			return EvalResult::error("Undefined variable in constant expression: " + std::string(var_name));
		}

		const ASTNode& symbol_node = symbol_opt.value();
		
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
			const auto& initializers = init_list.initializers();
			
			// Evaluate each element
			std::vector<int64_t> array_values;
			for (const auto& elem : initializers) {
				auto elem_result = evaluate(elem, context);
				if (!elem_result.success) {
					return elem_result;
				}
				array_values.push_back(elem_result.as_int());
			}
			
			// Return as an array result
			EvalResult array_result;
			array_result.success = true;
			array_result.is_array = true;
			array_result.array_values = std::move(array_values);
			return array_result;
		}

		// Recursively evaluate the initializer
		return evaluate(initializer.value(), context);
	}

	static EvalResult evaluate_ternary_operator(const TernaryOperatorNode& ternary, EvaluationContext& context) {
		// Evaluate the condition
		auto cond_result = evaluate(ternary.condition(), context);
		if (!cond_result.success) {
			return cond_result;
		}

		// Evaluate the appropriate branch based on the condition
		if (cond_result.as_bool()) {
			return evaluate(ternary.true_expr(), context);
		} else {
			return evaluate(ternary.false_expr(), context);
		}
	}

	// Helper to extract a LambdaExpressionNode from a variable's initializer
	// Returns nullptr if the initializer is not a lambda
	static const LambdaExpressionNode* extract_lambda_from_initializer(const std::optional<ASTNode>& initializer) {
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
			if (std::holds_alternative<LambdaExpressionNode>(expr)) {
				return &std::get<LambdaExpressionNode>(expr);
			}
		}
		
		return nullptr;
	}

	// Evaluate lambda captures and add their values to bindings
	static EvalResult evaluate_lambda_captures(
		const std::vector<LambdaCaptureNode>& captures,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		for (const auto& capture : captures) {
			using CaptureKind = LambdaCaptureNode::CaptureKind;
			
			switch (capture.kind()) {
				case CaptureKind::ByValue:
				case CaptureKind::ByReference: {
					// Named capture: [x] or [&x]
					std::string_view var_name = capture.identifier_name();
					
					// Check for init-capture: [x = expr]
					if (capture.has_initializer()) {
						auto init_result = evaluate(capture.initializer().value(), context);
						if (!init_result.success) {
							return EvalResult::error("Failed to evaluate init-capture '" + 
								std::string(var_name) + "': " + init_result.error_message);
						}
						bindings[var_name] = init_result;
					} else {
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
						if (!var_result.success) {
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
					// [this] or [*this] - capturing this pointer
					// This would require being in a member function context
					return EvalResult::error("Capture of 'this' not supported in constexpr lambdas");
			}
		}
		
		// Success - all captures evaluated
		EvalResult success;
		success.success = true;
		success.value = 0LL;  // Dummy value, not used
		return success;
	}

	// Evaluate a callable object (lambda or user-defined functor with operator())
	static EvalResult evaluate_callable_object(
		const VariableDeclarationNode& var_decl,
		const ChunkedVector<ASTNode>& arguments,
		EvaluationContext& context) {
		
		// Check for lambda
		const LambdaExpressionNode* lambda = extract_lambda_from_initializer(var_decl.initializer());
		if (lambda) {
			return evaluate_lambda_call(*lambda, arguments, context);
		}
		
		// Check for ConstructorCallNode (user-defined functor)
		const auto& initializer = var_decl.initializer();
		if (initializer.has_value() && initializer->is<ConstructorCallNode>()) {
			// TODO: Look up operator() in the struct and call it
			return EvalResult::error("User-defined functor constexpr calls not yet implemented");
		}
		
		return EvalResult::error("Object is not callable in constant expression");
	}

	// Evaluate a lambda call
	static EvalResult evaluate_lambda_call(
		const LambdaExpressionNode& lambda,
		const ChunkedVector<ASTNode>& arguments,
		EvaluationContext& context) {
		
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
		
		for (size_t i = 0; i < arguments.size(); ++i) {
			const ASTNode& param_node = parameters[i];
			if (!param_node.is<DeclarationNode>()) {
				return EvalResult::error("Invalid parameter node in constexpr lambda");
			}
			const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
			std::string_view param_name = param_decl.identifier_token().value();
			
			// Evaluate argument
			auto arg_result = evaluate(arguments[i], context);
			if (!arg_result.success) {
				return arg_result;
			}
			bindings[param_name] = arg_result;
		}
		
		// Handle captures - evaluate each captured variable and add to bindings
		const auto& captures = lambda.captures();
		auto capture_result = evaluate_lambda_captures(captures, bindings, context);
		if (!capture_result.success) {
			return capture_result;
		}
		
		// Increase recursion depth
		context.current_depth++;
		
		// Evaluate the lambda body
		const ASTNode& body_node = lambda.body();
		
		EvalResult result;
		if (body_node.is<BlockNode>()) {
			// Block body - look for return statement
			const BlockNode& body = body_node.as<BlockNode>();
			const auto& statements = body.get_statements();
			
			if (statements.size() != 1) {
				context.current_depth--;
				return EvalResult::error("Constexpr lambda must have a single return statement (complex statements not yet supported)");
			}
			
			result = evaluate_statement_with_bindings(statements[0], bindings, context);
		} else if (body_node.is<ExpressionNode>()) {
			// Expression body (implicit return)
			result = evaluate_expression_with_bindings(body_node, bindings, context);
		} else {
			context.current_depth--;
			return EvalResult::error("Invalid lambda body in constant expression");
		}
		
		context.current_depth--;
		return result;
	}

	static EvalResult evaluate_function_call(const FunctionCallNode& func_call, EvaluationContext& context) {
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
		auto symbol_opt = context.symbols->lookup(func_name);
		
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
						// Found a matching member function - check if it's constexpr
						const ASTNode& func_node = member_func.function_decl;
						if (func_node.is<FunctionDeclarationNode>()) {
							const FunctionDeclarationNode& func_decl = func_node.as<FunctionDeclarationNode>();
							
							// Only constexpr functions can be evaluated at compile time
							if (func_decl.is_constexpr()) {
								// Get the function body
								const auto& definition = func_decl.get_definition();
								if (definition.has_value()) {
									// Evaluate arguments
									const auto& arguments = func_call.arguments();
									const auto& parameters = func_decl.parameter_nodes();
									
									// This parameter count check implicitly ensures we're calling static members:
									// Non-static members would have a conceptual 'this' parameter that we're not providing
									if (arguments.size() == parameters.size()) {
										// Pass empty bindings for static member function calls
										std::unordered_map<std::string_view, EvalResult> empty_bindings;
										return evaluate_function_call_with_bindings(func_decl, arguments, empty_bindings, context);
									}
								}
							}
						}
					}
				}
			}
			
			return EvalResult::error("Undefined function in constant expression: " + std::string(func_name));
		}

		const ASTNode& symbol_node = symbol_opt.value();
		
		// Check if it's a FunctionDeclarationNode (regular function)
		if (symbol_node.is<FunctionDeclarationNode>()) {
			const FunctionDeclarationNode& func_decl = symbol_node.as<FunctionDeclarationNode>();
			
			// Check if it's a constexpr function
			if (!func_decl.is_constexpr()) {
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
			return evaluate_function_call_with_bindings(func_decl, arguments, empty_bindings, context);
		}
		
		// Check if it's a VariableDeclarationNode (could be a lambda/functor callable object)
		if (symbol_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
			return evaluate_callable_object(var_decl, func_call.arguments(), context);
		}
		
		return EvalResult::error("Identifier is not a function or callable object: " + std::string(func_name));
	}

	static EvalResult evaluate_function_call_with_bindings(
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
		
		for (size_t i = 0; i < arguments.size(); ++i) {
			// Evaluate the argument with outer bindings (for nested calls)
			auto arg_result = evaluate_expression_with_bindings_const(arguments[i], outer_bindings, context);
			if (!arg_result.success) {
				return arg_result;
			}
			
			// Get parameter name
			const ASTNode& param_node = parameters[i];
			if (!param_node.is<DeclarationNode>()) {
				return EvalResult::error("Invalid parameter node");
			}
			
			const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
			std::string_view param_name = param_decl.identifier_token().value();
			
			// Bind parameter to its value
			param_bindings[param_name] = arg_result;
		}

		// Increase recursion depth
		context.current_depth++;
		
		// Evaluate the function body with parameter bindings
		const ASTNode& body_node = definition.value();
		if (!body_node.is<BlockNode>()) {
			context.current_depth--;
			return EvalResult::error("Function body is not a block");
		}
		
		const BlockNode& body = body_node.as<BlockNode>();
		const auto& statements = body.get_statements();
		
		// Evaluate all statements in the function body
		// Local variable bindings are mutable - they can be added to as we process statements
		std::unordered_map<std::string_view, EvalResult> local_bindings = param_bindings;
		
		for (size_t i = 0; i < statements.size(); i++) {
			auto result = evaluate_statement_with_bindings(statements[i], local_bindings, context);
			
			// If the result is successful, it means a return value was computed
			// This can happen either directly from a return statement, or indirectly
			// from an if/while/for statement that contains a return
			if (result.success) {
				context.current_depth--;
				return result;
			}
			
			// For other statements (like variable declarations), result contains the binding info
			// The binding has already been added to local_bindings by evaluate_statement_with_bindings
			if (!result.success && result.error_message != "Statement executed (not a return)") {
				// An actual error occurred
				context.current_depth--;
				return result;
			}
		}
		
		context.current_depth--;
		return EvalResult::error("Constexpr function did not return a value");
	}

	static EvalResult evaluate_statement_with_bindings(
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
			
			return evaluate_expression_with_bindings(return_expr.value(), bindings, context);
		}
		
		// Handle variable declarations
		if (stmt_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = stmt_node.as<VariableDeclarationNode>();
			const DeclarationNode& decl = var_decl.declaration_node().as<DeclarationNode>();
			std::string_view var_name = decl.identifier_token().value();
			
			// Evaluate the initializer if present
			if (var_decl.initializer().has_value()) {
				const ASTNode& init_expr = var_decl.initializer().value();
				
				// Handle array initialization with InitializerListNode
				if (init_expr.is<InitializerListNode>()) {
					const InitializerListNode& init_list = init_expr.as<InitializerListNode>();
					const auto& initializers = init_list.initializers();
					
					// Create array value - evaluate each element
					std::vector<int64_t> array_values;
					for (size_t i = 0; i < initializers.size(); i++) {
						auto elem_result = evaluate_expression_with_bindings(initializers[i], bindings, context);
						if (!elem_result.success) {
							return elem_result;
						}
						array_values.push_back(elem_result.as_int());
					}
					
					// Store as an array binding
					EvalResult array_result;
					array_result.success = true;
					array_result.is_array = true;
					array_result.array_values = std::move(array_values);
					bindings[var_name] = array_result;
					
					// Return a sentinel indicating statement executed successfully
					return EvalResult::error("Statement executed (not a return)");
				}
				
				// Regular expression initializer
				auto init_result = evaluate_expression_with_bindings(init_expr, bindings, context);
				if (!init_result.success) {
					return init_result;
				}
				
				// Add to bindings
				bindings[var_name] = init_result;
				return EvalResult::error("Statement executed (not a return)");
			}
			
			// Uninitialized variable - set to 0
			bindings[var_name] = EvalResult::from_int(0);
			return EvalResult::error("Statement executed (not a return)");
		}
		
		// Handle for loops (C++14 constexpr)
		if (stmt_node.is<ForStatementNode>()) {
			const ForStatementNode& for_stmt = stmt_node.as<ForStatementNode>();
			
			// Execute init statement if present
			if (for_stmt.has_init()) {
				auto init_result = evaluate_statement_with_bindings(for_stmt.get_init_statement().value(), bindings, context);
				// Ignore result for init statement (it's usually a variable declaration)
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
					if (!cond_result.success) {
						return cond_result;
					}
					if (!cond_result.as_bool()) {
						break;  // Exit loop when condition is false
					}
				}
				
				// Execute loop body
				const ASTNode& body = for_stmt.get_body_statement();
				if (body.is<BlockNode>()) {
					const BlockNode& block = body.as<BlockNode>();
					const auto& statements = block.get_statements();
					for (size_t i = 0; i < statements.size(); i++) {
						auto result = evaluate_statement_with_bindings(statements[i], bindings, context);
						// If this was a return statement, propagate it up
						if (statements[i].is<ReturnStatementNode>()) {
							return result;
						}
					}
				} else {
					auto result = evaluate_statement_with_bindings(body, bindings, context);
					// If this was a return statement, propagate it up
					if (body.is<ReturnStatementNode>()) {
						return result;
					}
				}
				
				// Execute update expression if present
				if (for_stmt.has_update()) {
					auto update_result = evaluate_expression_with_bindings(for_stmt.get_update_expression().value(), bindings, context);
					// Update expression result is ignored (side effects have been applied)
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
				if (!cond_result.success) {
					return cond_result;
				}
				if (!cond_result.as_bool()) {
					break;  // Exit loop when condition is false
				}
				
				// Execute loop body
				const ASTNode& body = while_stmt.get_body_statement();
				if (body.is<BlockNode>()) {
					const BlockNode& block = body.as<BlockNode>();
					const auto& statements = block.get_statements();
					for (size_t i = 0; i < statements.size(); i++) {
						auto result = evaluate_statement_with_bindings(statements[i], bindings, context);
						// If this was a return statement, propagate it up
						if (statements[i].is<ReturnStatementNode>()) {
							return result;
						}
					}
				} else {
					auto result = evaluate_statement_with_bindings(body, bindings, context);
					// If this was a return statement, propagate it up
					if (body.is<ReturnStatementNode>()) {
						return result;
					}
				}
			}
			
			return EvalResult::error("Statement executed (not a return)");
		}
		
		// Handle if statements (C++14 constexpr)
		if (stmt_node.is<IfStatementNode>()) {
			const IfStatementNode& if_stmt = stmt_node.as<IfStatementNode>();
			
			// Execute init statement if present (C++17 feature)
			if (if_stmt.has_init()) {
				auto init_result = evaluate_statement_with_bindings(if_stmt.get_init_statement().value(), bindings, context);
				// Ignore result for init statement
			}
			
			// Evaluate condition
			auto cond_result = evaluate_expression_with_bindings(if_stmt.get_condition(), bindings, context);
			if (!cond_result.success) {
				return cond_result;
			}
			
			// Execute then or else branch
			if (cond_result.as_bool()) {
				// Execute then branch
				const ASTNode& then_stmt = if_stmt.get_then_statement();
				if (then_stmt.is<BlockNode>()) {
					const BlockNode& block = then_stmt.as<BlockNode>();
					const auto& statements = block.get_statements();
					for (size_t i = 0; i < statements.size(); i++) {
						auto result = evaluate_statement_with_bindings(statements[i], bindings, context);
						// If this was a return statement, propagate it up
						if (statements[i].is<ReturnStatementNode>()) {
							return result;
						}
					}
				} else {
					auto result = evaluate_statement_with_bindings(then_stmt, bindings, context);
					if (then_stmt.is<ReturnStatementNode>()) {
						return result;
					}
				}
			} else if (if_stmt.has_else()) {
				// Execute else branch
				// Fix dangling reference warning by storing the value first
				std::optional<ASTNode> else_stmt_opt = if_stmt.get_else_statement();
				if (else_stmt_opt.has_value()) {
					const ASTNode& else_stmt = *else_stmt_opt;
					if (else_stmt.is<BlockNode>()) {
						const BlockNode& block = else_stmt.as<BlockNode>();
						const auto& statements = block.get_statements();
						for (size_t i = 0; i < statements.size(); i++) {
							auto result = evaluate_statement_with_bindings(statements[i], bindings, context);
							// If this was a return statement, propagate it up
							if (statements[i].is<ReturnStatementNode>()) {
								return result;
							}
						}
					} else {
						auto result = evaluate_statement_with_bindings(else_stmt, bindings, context);
						if (else_stmt.is<ReturnStatementNode>()) {
							return result;
						}
					}
				}
			}
			
			return EvalResult::error("Statement executed (not a return)");
		}
		
		// Handle expression statements (assignments, increments, etc.)
		if (stmt_node.is<ExpressionNode>()) {
			// Evaluate the expression (which may have side effects like assignments)
			auto result = evaluate_expression_with_bindings(stmt_node, bindings, context);
			// Expression statements don't return values to the caller
			return EvalResult::error("Statement executed (not a return)");
		}
		
		// Handle block statements (nested blocks)
		if (stmt_node.is<BlockNode>()) {
			const BlockNode& block = stmt_node.as<BlockNode>();
			const auto& statements = block.get_statements();
			for (size_t i = 0; i < statements.size(); i++) {
				auto result = evaluate_statement_with_bindings(statements[i], bindings, context);
				// If this was a return statement, propagate it up
				if (statements[i].is<ReturnStatementNode>()) {
					return result;
				}
			}
			return EvalResult::error("Statement executed (not a return)");
		}
		
		return EvalResult::error("Unsupported statement type in constexpr function");
	}

	// Overload for mutable bindings (used in statements with side effects like assignments)
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
						if (!rhs_result.success) return rhs_result;
						
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
							
							if (!new_value.success) return new_value;
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
			
			if (!lhs_result.success) return lhs_result;
			if (!rhs_result.success) return rhs_result;
			
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
						
						if (!new_value.success) return new_value;
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
			if (!operand_result.success) return operand_result;
			return apply_unary_op(operand_result, op);
		}
		
		// For ternary operators
		if (std::holds_alternative<TernaryOperatorNode>(expr)) {
			const auto& ternary = std::get<TernaryOperatorNode>(expr);
			auto cond_result = evaluate_expression_with_bindings(ternary.condition(), bindings, context);
			
			if (!cond_result.success) return cond_result;
			
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
				return EvalResult::error("Undefined function in constant expression: " + std::string(func_name));
			}
			
			const ASTNode& symbol_node = symbol_opt.value();
			if (!symbol_node.is<FunctionDeclarationNode>()) {
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
				// For now, fall back to regular evaluation
			}
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
			
			if (!lhs_result.success) return lhs_result;
			if (!rhs_result.success) return rhs_result;
			
			return apply_binary_op(lhs_result, rhs_result, bin_op.op());
		}
		
		// For ternary operators
		if (std::holds_alternative<TernaryOperatorNode>(expr)) {
			const auto& ternary = std::get<TernaryOperatorNode>(expr);
			auto cond_result = evaluate_expression_with_bindings_const(ternary.condition(), bindings, context);
			
			if (!cond_result.success) return cond_result;
			
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
				return EvalResult::error("Undefined function in constant expression: " + std::string(func_name));
			}
			
			const ASTNode& symbol_node = symbol_opt.value();
			if (!symbol_node.is<FunctionDeclarationNode>()) {
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
			if (!index_result.success) {
				return index_result;
			}
			
			long long index = index_result.as_int();
			if (index < 0) {
				return EvalResult::error("Negative array index in constant expression");
			}
			
			// Get the array expression
			const ASTNode& array_expr = subscript.array_expr();
			if (array_expr.is<ExpressionNode>()) {
				const ExpressionNode& expr = array_expr.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(expr)) {
					std::string_view var_name = std::get<IdentifierNode>(expr).name();
					
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
		auto symbol_opt = context.symbols->lookup_qualified(qualified_id.namespaces(), qualified_id.name());
		if (!symbol_opt.has_value()) {
			// PHASE 3 FIX: If not found in symbol table, try looking up as struct static member
			// This handles cases like is_pointer_impl<int*>::value where value is a static member
			const auto& namespaces = qualified_id.namespaces();
			if (!namespaces.empty()) {
				// The struct name is the last namespace component
				std::string struct_name(namespaces.back());
				StringHandle struct_handle = StringTable::getOrInternStringHandle(struct_name);
				
				FLASH_LOG(General, Debug, "Phase 3 ConstExpr: Looking up struct '", struct_name, "' for member '", qualified_id.name(), "'");
				
				// Look up the struct in gTypesByName
				auto struct_type_it = gTypesByName.find(struct_handle);
				if (struct_type_it != gTypesByName.end() && struct_type_it->second->isStruct()) {
					const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
					
					// Resolve type alias if needed
					if (!struct_info && struct_type_it->second->type_index_ < gTypeInfo.size()) {
						const TypeInfo* resolved_type = &gTypeInfo[struct_type_it->second->type_index_];
						if (resolved_type && resolved_type->isStruct()) {
							struct_info = resolved_type->getStructInfo();
						}
					}
					
					if (struct_info) {
						// Look for static member recursively (checks base classes too)
						StringHandle member_handle = StringTable::getOrInternStringHandle(qualified_id.name());
						auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_handle);
						
						FLASH_LOG(General, Debug, "Phase 3 ConstExpr: Static member found: ", (static_member != nullptr), 
						          ", owner: ", (owner_struct != nullptr));
						
						if (static_member && owner_struct) {
							FLASH_LOG(General, Debug, "Phase 3 ConstExpr: Static member is_const: ", static_member->is_const, 
							          ", has_initializer: ", static_member->initializer.has_value());
							
							// Found a static member - evaluate its initializer if available
							// Note: Even if not marked const, we can evaluate constexpr initializers
							if (static_member->initializer.has_value()) {
								FLASH_LOG(General, Debug, "Phase 3 ConstExpr: Evaluating static member initializer");
								return evaluate(static_member->initializer.value(), context);
							}
							
							// If not constexpr or no initializer, return default value based on type
							FLASH_LOG(General, Debug, "Phase 3 ConstExpr: Returning default value for type: ", static_cast<int>(static_member->type));
							if (static_member->type == Type::Bool) {
								return EvalResult::from_bool(false);
							}
							return EvalResult::from_int(0);
						}
					}
				}
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
		
		// Check if it's a constexpr variable
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
				if (!arg_result.success) {
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
		std::string_view member_name,
		EvaluationContext& context) {
		
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
			if (mem_init.member_name == member_name) {
				return mem_init.initializer_expr;
			}
		}
		
		// Check for default member initializer
		StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
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
					if (!arg_result.success) {
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
		if (!init_arg_result.success) {
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
		const ArraySubscriptNode& subscript,
		std::string_view member_name,
		EvaluationContext& context) {
		// For now, return an error - this is more complex
		return EvalResult::error("Array subscript followed by member access not yet supported");
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
		if (!member_extraction_result.success) {
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
			if (!arg_result.success) {
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
				if (!arg_result.success) {
					return arg_result;
				}
				ctor_param_bindings[param_name] = arg_result;
			}
		}
		
		// Extract member values from the initializer list
		for (const auto& mem_init : matching_ctor->member_initializers()) {
			auto member_result = evaluate_expression_with_bindings(mem_init.initializer_expr, ctor_param_bindings, context);
			if (!member_result.success) {
				return member_result;
			}
			member_bindings[mem_init.member_name] = member_result;
		}
		
		// Also check for default member initializers for members not in the initializer list
		for (const auto& member : struct_info->members) {
			std::string_view name_view = StringTable::getStringView(member.getName());
			if (member_bindings.find(name_view) == member_bindings.end() && member.default_initializer.has_value()) {
				auto default_result = evaluate(member.default_initializer.value(), context);
				if (default_result.success) {
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
		if (!index_result.success) {
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
						if (!arg_result.success) {
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

			// Add more type traits as needed
			// For now, other type traits return false during constexpr evaluation
			default:
				result = false;
				break;
		}

		return EvalResult::from_bool(result);
	}
};

} // namespace ConstExpr
