#pragma once

#include "AstNodeTypes.h"
#include "TemplateRegistry.h"
#include <unordered_map>
#include <string_view>
#include <vector>

// Forward declaration
class Parser;

/// ExpressionSubstitutor - Performs template parameter substitution in expression AST nodes
///
/// This class traverses expression AST nodes and substitutes template parameters with concrete
/// template arguments. It is primarily used for evaluating template-dependent decltype expressions
/// in base class specifications during template instantiation.
///
/// Example usage:
///   template<typename T>
///   struct wrapper : decltype(base_trait<T>()) { };
///
/// When instantiating wrapper<int>, the decltype expression needs to have T substituted with int,
/// which this class handles.
class ExpressionSubstitutor {
public:
	/// Construct a substitutor with a parameter-to-argument mapping
	/// @param param_map Maps template parameter names to concrete template arguments
	/// @param parser Reference to the parser for triggering template instantiations
	ExpressionSubstitutor(
		const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
		Parser& parser)
		: param_map_(param_map), parser_(parser) {}

	/// Construct a substitutor with both scalar and pack parameter mappings
	/// @param param_map Maps scalar template parameter names to concrete template arguments
	/// @param pack_map Maps pack parameter names to vectors of template arguments
	/// @param parser Reference to the parser for triggering template instantiations
	ExpressionSubstitutor(
		const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
		const std::unordered_map<std::string_view, std::vector<TemplateTypeArg>>& pack_map,
		Parser& parser)
		: param_map_(param_map), pack_map_(pack_map), parser_(parser) {}

	/// Main entry point: Substitute template parameters in an expression
	/// @param expr The expression AST node to process
	/// @return A new expression with template parameters substituted
	ASTNode substitute(const ASTNode& expr);

private:
	// Handlers for different expression types
	ASTNode substituteConstructorCall(const ConstructorCallNode& ctor);
	ASTNode substituteFunctionCall(const FunctionCallNode& call);
	ASTNode substituteBinaryOp(const BinaryOperatorNode& binop);
	ASTNode substituteUnaryOp(const UnaryOperatorNode& unop);
	ASTNode substituteIdentifier(const IdentifierNode& id);
	ASTNode substituteLiteral(const ASTNode& literal);

	// Helper: substitute in a type specifier with template args
	TypeSpecifierNode substituteInType(const TypeSpecifierNode& type);

	// Helper: trigger template instantiation for a type
	void ensureTemplateInstantiated(
		std::string_view template_name,
		const std::vector<TemplateTypeArg>& args);

	// Helper: check if a template argument node is a pack expansion
	bool isPackExpansion(const ASTNode& arg_node, std::string_view& pack_name);

	// Helper: expand pack parameters in template arguments
	std::vector<TemplateTypeArg> expandPacksInArguments(
		const std::vector<ASTNode>& template_arg_nodes);

	// Substitution context
	const std::unordered_map<std::string_view, TemplateTypeArg>& param_map_;
	const std::unordered_map<std::string_view, std::vector<TemplateTypeArg>> pack_map_;
	Parser& parser_;
};
