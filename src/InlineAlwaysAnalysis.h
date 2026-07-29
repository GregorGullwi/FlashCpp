#pragma once

#include "AstNodeTypes.h"

// The call-site fast path replaces a call with the address of its argument.
// Prove that transformation from the instantiated body instead of inferring it
// from purity or matching types, which do not establish expression equivalence.
inline bool isReferenceIdentityInlineCandidate(const FunctionDeclarationNode& function) {
	const TypeSpecifierNode& return_type =
		function.decl_node().type_specifier_node();
	if (!return_type.is_reference() ||
		function.parameter_nodes().size() != 1 ||
		!function.parameter_nodes()[0].is<DeclarationNode>()) {
		return false;
	}

	const std::optional<ASTNode>& definition = function.get_definition();
	if (!definition.has_value() || !definition->is<BlockNode>()) {
		return false;
	}

	const DeclarationNode& parameter =
		function.parameter_nodes()[0].as<DeclarationNode>();
	const StringHandle parameter_name =
		parameter.identifier_token().handle().isValid()
			? parameter.identifier_token().handle()
			: StringTable::getOrInternStringHandle(
				  parameter.identifier_token().value());

	const ExpressionNode* return_expression = nullptr;
	bool valid_body = true;
	definition->as<BlockNode>().get_statements().visit(
		[&](const ASTNode& statement) {
			if (!valid_body ||
				!statement.has_value() ||
				statement.is<TypedefDeclarationNode>()) {
				return;
			}
			if (!statement.is<ReturnStatementNode>() ||
				return_expression != nullptr) {
				valid_body = false;
				return;
			}
			const std::optional<ASTNode>& expression =
				statement.as<ReturnStatementNode>().expression();
			if (!expression.has_value() ||
				!expression->is<ExpressionNode>()) {
				valid_body = false;
				return;
			}
			return_expression = &expression->as<ExpressionNode>();
		});
	if (!valid_body || return_expression == nullptr) {
		return false;
	}

	auto is_parameter_identifier =
		[parameter_name](const ExpressionNode& expression) {
			const IdentifierNode* identifier =
				std::get_if<IdentifierNode>(&expression);
			return identifier != nullptr &&
				   identifier->getOrInternNameHandle() == parameter_name;
		};
	if (is_parameter_identifier(*return_expression)) {
		return true;
	}

	const StaticCastNode* cast =
		std::get_if<StaticCastNode>(return_expression);
	if (cast == nullptr ||
		!cast->target_type().is_reference() ||
		!cast->target_type().matches_signature(return_type) ||
		!cast->expr().is<ExpressionNode>()) {
		return false;
	}
	return is_parameter_identifier(cast->expr().as<ExpressionNode>());
}
