#pragma once

#include "AstNodeTypes.h"
#include "SymbolTable.h"

namespace FlashCpp::ParserFunctionTypeHelpers {

inline const FunctionDeclarationNode* findFunctionDeclarationForIdentifier(std::string_view identifier, const Token& token) {
	const auto overloads = gSymbolTable.lookup_all(identifier);
	for (const auto& overload : overloads) {
		if (!overload.is<FunctionDeclarationNode>()) {
			continue;
		}
		const auto& func_decl = overload.as<FunctionDeclarationNode>();
		const Token& func_token = func_decl.decl_node().identifier_token();
		if (func_token.value() == token.value() &&
			func_token.line() == token.line() &&
			func_token.column() == token.column() &&
			func_token.file_index() == token.file_index()) {
			return &func_decl;
		}
	}
	return nullptr;
}

inline const FunctionDeclarationNode* findFunctionDeclarationForSymbol(const ASTNode& symbol) {
	if (symbol.is<FunctionDeclarationNode>()) {
		return &symbol.as<FunctionDeclarationNode>();
	}
	if (symbol.is<DeclarationNode>()) {
		const auto& decl = symbol.as<DeclarationNode>();
		return findFunctionDeclarationForIdentifier(decl.identifier_token().value(), decl.identifier_token());
	}
	if (symbol.is<VariableDeclarationNode>()) {
		const auto& decl = symbol.as<VariableDeclarationNode>().declaration();
		return findFunctionDeclarationForIdentifier(decl.identifier_token().value(), decl.identifier_token());
	}
	return nullptr;
}

inline TypeSpecifierNode buildFunctionPointerTypeFromFunctionDeclaration(const FunctionDeclarationNode& func_decl) {
	FunctionSignature sig;
	sig.return_type_index = func_decl.decl_node().type_node().as<TypeSpecifierNode>().type_index();
	for (const auto& param_node : func_decl.parameter_nodes()) {
		if (!param_node.is<DeclarationNode>()) {
			continue;
		}
		const auto& param_type = param_node.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
		sig.parameter_type_indices.push_back(param_type.type_index());
	}

	TypeSpecifierNode fp_type(TypeCategory::FunctionPointer, TypeQualifier::None, 64, func_decl.decl_node().identifier_token(), CVQualifier::None);
	fp_type.set_function_signature(sig);
	return fp_type;
}

inline std::optional<TypeSpecifierNode> tryGetBareFunctionIdentifierType(const ASTNode& arg_node) {
	if (!arg_node.is<ExpressionNode>()) {
		return std::nullopt;
	}
	const ExpressionNode& expr = arg_node.as<ExpressionNode>();
	if (!std::holds_alternative<IdentifierNode>(expr)) {
		return std::nullopt;
	}
	const auto& ident = std::get<IdentifierNode>(expr);
	auto symbol = gSymbolTable.lookup(ident.nameHandle());
	if (!symbol.has_value()) {
		return std::nullopt;
	}
	if (const FunctionDeclarationNode* func_decl = findFunctionDeclarationForSymbol(*symbol)) {
		return buildFunctionPointerTypeFromFunctionDeclaration(*func_decl);
	}
	return std::nullopt;
}

} // namespace FlashCpp::ParserFunctionTypeHelpers
