#include "ExpressionSubstitutor.h"
#include "Parser.h"
#include "Log.h"

ASTNode ExpressionSubstitutor::substitute(const ASTNode& expr) {
	if (!expr.has_value()) {
		return expr;
	}

	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor::substitute: checking node type: ", expr.type_name());

	// Check if this is an ExpressionNode variant
	if (expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_variant = expr.as<ExpressionNode>();
		
		// Use std::visit to dispatch to the correct handler based on variant type
		auto& substitutor = *this;
		return std::visit([&substitutor](auto&& node) -> ASTNode {
			using T = std::decay_t<decltype(node)>;
			
			FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing variant type");
			
			if constexpr (std::is_same_v<T, ConstructorCallNode>) {
				FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Dispatching to substituteConstructorCall");
				return substitutor.substituteConstructorCall(node);
			} else if constexpr (std::is_same_v<T, FunctionCallNode>) {
				FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Dispatching to substituteFunctionCall");
				return substitutor.substituteFunctionCall(node);
			} else if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
				return substitutor.substituteBinaryOp(node);
			} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
				return substitutor.substituteUnaryOp(node);
			} else if constexpr (std::is_same_v<T, IdentifierNode>) {
				return substitutor.substituteIdentifier(node);
			} else if constexpr (std::is_same_v<T, NumericLiteralNode> || 
			                     std::is_same_v<T, BoolLiteralNode> || 
			                     std::is_same_v<T, StringLiteralNode>) {
				// Literals don't need substitution - return as ASTNode
				ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(node);
				return ASTNode(&new_expr);
			} else {
				// For other types, return as-is wrapped in ExpressionNode
				FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Unhandled expression variant type, returning as-is");
				ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(node);
				return ASTNode(&new_expr);
			}
		}, expr_variant);
	}

	// Handle direct node types (not wrapped in variant)
	if (expr.is<ConstructorCallNode>()) {
		return substituteConstructorCall(expr.as<ConstructorCallNode>());
	}
	else if (expr.is<FunctionCallNode>()) {
		return substituteFunctionCall(expr.as<FunctionCallNode>());
	}
	else if (expr.is<BinaryOperatorNode>()) {
		return substituteBinaryOp(expr.as<BinaryOperatorNode>());
	}
	else if (expr.is<UnaryOperatorNode>()) {
		return substituteUnaryOp(expr.as<UnaryOperatorNode>());
	}
	else if (expr.is<IdentifierNode>()) {
		return substituteIdentifier(expr.as<IdentifierNode>());
	}
	else if (expr.is<NumericLiteralNode>()) {
		// Literals don't need substitution
		return expr;
	}
	else if (expr.is<BoolLiteralNode>()) {
		return expr;
	}
	else if (expr.is<StringLiteralNode>()) {
		return expr;
	}
	else {
		// For any other node type, return as-is
		FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Unknown expression type: ", expr.type_name());
		return expr;
	}
}

ASTNode ExpressionSubstitutor::substituteConstructorCall(const ConstructorCallNode& ctor) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing constructor call");
	
	// Get the type being constructed
	const ASTNode& type_node = ctor.type_node();
	
	if (!type_node.is<TypeSpecifierNode>()) {
		FLASH_LOG(Templates, Warning, "ExpressionSubstitutor: Constructor type node is not TypeSpecifierNode");
		// Return wrapped in ExpressionNode
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(ctor);
		return ASTNode(&new_expr);
	}
	
	const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
	
	// Substitute template parameters in the type
	TypeSpecifierNode substituted_type = substituteInType(type_spec);
	
	// Create new ConstructorCallNode with substituted type
	// First, we need to copy the arguments
	ChunkedVector<ASTNode> substituted_args;
	for (size_t i = 0; i < ctor.arguments().size(); ++i) {
		substituted_args.push_back(substitute(ctor.arguments()[i]));
	}
	
	// Create new TypeSpecifierNode and ConstructorCallNode
	TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(substituted_type);
	ConstructorCallNode& new_ctor = gChunkedAnyStorage.emplace_back<ConstructorCallNode>(
		ASTNode(&new_type),
		std::move(substituted_args),
		ctor.called_from()
	);
	
	// Wrap in ExpressionNode
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_ctor);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteFunctionCall(const FunctionCallNode& call) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing function call");
	FLASH_LOG(Templates, Debug, "  has_mangled_name: ", call.has_mangled_name());
	
	// Check if this is actually a template constructor call like base_trait<T>()
	// In this case, the function_declaration might have template information
	const DeclarationNode& decl_node = call.function_declaration();
	FLASH_LOG(Templates, Debug, "  DeclarationNode identifier: ", decl_node.identifier_token().value());
	
	// Check if this has a mangled name that includes template information
	if (call.has_mangled_name()) {
		std::string_view mangled_name = call.mangled_name();
		FLASH_LOG(Templates, Debug, "  Function has mangled name: ", mangled_name);
		
		// Parse the mangled name to extract template name and arguments
		// Look for pattern like "detail::base_trait<T>"
		size_t template_start = mangled_name.find('<');
		if (template_start != std::string_view::npos) {
			std::string_view template_name = mangled_name.substr(0, template_start);
			size_t template_end = mangled_name.rfind('>');
			
			if (template_end != std::string_view::npos && template_end > template_start) {
				std::string_view args_str = mangled_name.substr(template_start + 1, template_end - template_start - 1);
				FLASH_LOG(Templates, Debug, "  Found template in mangled name: ", template_name, " with args: ", args_str);
				
				// Check if the argument needs substitution
				// Trim whitespace
				while (!args_str.empty() && (args_str.front() == ' ' || args_str.front() == '\t')) {
					args_str.remove_prefix(1);
				}
				while (!args_str.empty() && (args_str.back() == ' ' || args_str.back() == '\t')) {
					args_str.remove_suffix(1);
				}
				
				auto it = param_map_.find(args_str);
				if (it != param_map_.end()) {
					FLASH_LOG(Templates, Debug, "  Substituting template argument: ", args_str);
					
					std::vector<TemplateTypeArg> substituted_args;
					substituted_args.push_back(it->second);
					
					// Instantiate the template
					auto instantiated_node = parser_.try_instantiate_class_template(template_name, substituted_args, true);
					if (instantiated_node.has_value() && instantiated_node->is<StructDeclarationNode>()) {
						const StructDeclarationNode& class_decl = instantiated_node->as<StructDeclarationNode>();
						StringHandle instantiated_name = class_decl.name();
						
						FLASH_LOG(Templates, Debug, "  Successfully instantiated template, creating constructor call");
						
						// Look up the type index
						auto type_it = gTypesByName.find(instantiated_name);
						if (type_it != gTypesByName.end()) {
							TypeIndex new_type_index = type_it->second->type_index_;
							
							// Create a TypeSpecifierNode for the instantiated type
							TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(
								Type::Struct, new_type_index, 64, Token{}, CVQualifier::None
							);
							
							// Create a ConstructorCallNode instead of FunctionCallNode
							ChunkedVector<ASTNode> substituted_args_nodes;
							for (size_t i = 0; i < call.arguments().size(); ++i) {
								substituted_args_nodes.push_back(substitute(call.arguments()[i]));
							}
							
							ConstructorCallNode& new_ctor = gChunkedAnyStorage.emplace_back<ConstructorCallNode>(
								ASTNode(&new_type),
								std::move(substituted_args_nodes),
								call.called_from()
							);
							
							// Wrap in ExpressionNode
							ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_ctor);
							return ASTNode(&new_expr);
						}
					}
				}
			}
		}
	}
	
	// If not a template constructor call, return as-is
	FLASH_LOG(Templates, Debug, "  Returning function call as-is");
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(call);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteBinaryOp(const BinaryOperatorNode& binop) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing binary operator");
	
	// Recursively substitute in left and right operands
	ASTNode substituted_lhs = substitute(binop.get_lhs());
	ASTNode substituted_rhs = substitute(binop.get_rhs());
	
	// Create new BinaryOperatorNode with substituted operands
	BinaryOperatorNode& new_binop = gChunkedAnyStorage.emplace_back<BinaryOperatorNode>(
		binop.get_token(),
		substituted_lhs,
		substituted_rhs
	);
	
	return ASTNode(&new_binop);
}

ASTNode ExpressionSubstitutor::substituteUnaryOp(const UnaryOperatorNode& unop) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing unary operator");
	
	// Recursively substitute in operand
	ASTNode substituted_operand = substitute(unop.get_operand());
	
	// Create new UnaryOperatorNode with substituted operand
	UnaryOperatorNode& new_unop = gChunkedAnyStorage.emplace_back<UnaryOperatorNode>(
		unop.get_token(),
		substituted_operand,
		unop.is_prefix(),
		unop.is_builtin_addressof()
	);
	
	return ASTNode(&new_unop);
}

ASTNode ExpressionSubstitutor::substituteIdentifier(const IdentifierNode& id) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing identifier: ", id.name());
	
	// Check if this identifier is a template parameter that needs substitution
	auto it = param_map_.find(id.name());
	if (it != param_map_.end()) {
		const TemplateTypeArg& arg = it->second;
		FLASH_LOG(Templates, Debug, "  Found template parameter substitution: ", id.name(), 
			" -> type=", (int)arg.base_type, ", type_index=", arg.type_index);
		
		// Create a TypeSpecifierNode from the template argument
		TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(
			arg.base_type,
			arg.type_index,
			64,  // Default size, will be adjusted by type system
			Token{},
			arg.cv_qualifier
		);
		
		// Add pointer levels if needed
		for (size_t i = 0; i < arg.pointer_depth; ++i) {
			new_type.add_pointer_level();
		}
		
		// Set reference qualifiers
		if (arg.is_rvalue_reference) {
			new_type.set_reference(true);  // true for rvalue reference
		} else if (arg.is_reference) {
			new_type.set_reference(false);  // false for lvalue reference
		}
		
		return ASTNode(&new_type);
	}
	
	// Not a template parameter, return as-is
	return ASTNode(&const_cast<IdentifierNode&>(id));
}

ASTNode ExpressionSubstitutor::substituteLiteral(const ASTNode& literal) {
	// Literals don't need substitution
	return literal;
}

TypeSpecifierNode ExpressionSubstitutor::substituteInType(const TypeSpecifierNode& type) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Substituting in type");
	
	// Check if this is a struct/class type that might have template arguments
	if (type.type() == Type::Struct && type.type_index() < gTypeInfo.size()) {
		const TypeInfo& type_info = gTypeInfo[type.type_index()];
		std::string_view type_name = StringTable::getStringView(type_info.name());
		
		FLASH_LOG(Templates, Debug, "  Type is struct: ", type_name, " type_index=", type.type_index());
		
		// Parse the type name to see if it contains template arguments
		// Format: "name<arg1, arg2, ...>"
		size_t template_start = type_name.find('<');
		if (template_start != std::string_view::npos) {
			std::string_view base_name = type_name.substr(0, template_start);
			FLASH_LOG(Templates, Debug, "  Found template type: ", base_name);
			
			// Extract the template arguments part (between < and >)
			size_t template_end = type_name.rfind('>');
			if (template_end != std::string_view::npos && template_end > template_start) {
				std::string_view args_str = type_name.substr(template_start + 1, template_end - template_start - 1);
				FLASH_LOG(Templates, Debug, "  Template arguments string: ", args_str);
				
				// Parse the arguments and check if any need substitution
				// For now, we'll do a simple check: if any argument matches a template parameter name
				bool needs_substitution = false;
				for (const auto& [param_name, param_arg] : param_map_) {
					if (args_str.find(param_name) != std::string_view::npos) {
						needs_substitution = true;
						FLASH_LOG(Templates, Debug, "  Found template parameter ", param_name, " in arguments");
						break;
					}
				}
				
				if (needs_substitution) {
					// We need to parse and substitute the template arguments
					// For a simple case like "base_trait<T>", we substitute T
					
					// Simple parser for single template argument (most common case)
					// TODO: Handle multiple arguments separated by commas
					std::vector<TemplateTypeArg> substituted_args;
					
					// Trim whitespace
					while (!args_str.empty() && (args_str.front() == ' ' || args_str.front() == '\t')) {
						args_str.remove_prefix(1);
					}
					while (!args_str.empty() && (args_str.back() == ' ' || args_str.back() == '\t')) {
						args_str.remove_suffix(1);
					}
					
					// Check if this is a simple identifier that needs substitution
					auto it = param_map_.find(args_str);
					if (it != param_map_.end()) {
						FLASH_LOG(Templates, Debug, "  Substituting template argument: ", args_str, " -> type_index=", it->second.type_index);
						substituted_args.push_back(it->second);
						
						// Now instantiate the template with the substituted argument
						auto instantiated_node = parser_.try_instantiate_class_template(base_name, substituted_args, true);
						if (instantiated_node.has_value() && instantiated_node->is<StructDeclarationNode>()) {
							const StructDeclarationNode& class_decl = instantiated_node->as<StructDeclarationNode>();
							StringHandle instantiated_name = class_decl.name();
							
							// Look up the type index for the instantiated template
							auto type_it = gTypesByName.find(instantiated_name);
							if (type_it != gTypesByName.end()) {
								TypeIndex new_type_index = type_it->second->type_index_;
							
								FLASH_LOG(Templates, Debug, "  Successfully instantiated template: ", base_name, " with type_index=", new_type_index);
								
								// Create a new TypeSpecifierNode with the instantiated type
								return TypeSpecifierNode(Type::Struct, new_type_index, 64, Token{}, type.cv_qualifier());
							} else {
								FLASH_LOG(Templates, Warning, "  Instantiated template not found in gTypesByName: ", instantiated_name.view());
							}
						} else {
							FLASH_LOG(Templates, Warning, "  Failed to instantiate template: ", base_name);
						}
					}
				}
			}
		}
	}
	
	// If no substitution needed or failed, return a copy of the original type
	return type;
}

void ExpressionSubstitutor::ensureTemplateInstantiated(
	std::string_view template_name,
	const std::vector<TemplateTypeArg>& args) {
	
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Ensuring template instantiated: ", template_name);
	
	// TODO: Use parser to trigger template instantiation
	// This will be implemented once we integrate with the parser
}
