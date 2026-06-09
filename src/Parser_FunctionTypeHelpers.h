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
	return nullptr;
}

inline TypeSpecifierNode buildFunctionPointerTypeFromFunctionDeclaration(const FunctionDeclarationNode& func_decl) {
	FunctionSignature sig;
	const TypeSpecifierNode& return_type = func_decl.decl_node().type_specifier_node();
	sig.return_type_index = return_type.type_index();
	sig.return_pointer_depth = static_cast<int>(return_type.pointer_depth());
	sig.return_reference_qualifier = return_type.reference_qualifier();
	for (const auto& param_node : func_decl.parameter_nodes()) {
		if (!param_node.is<DeclarationNode>()) {
			continue;
		}
		const auto& param_type = param_node.as<DeclarationNode>().type_specifier_node();
		sig.parameter_type_indices.push_back(param_type.type_index());
	}

	TypeSpecifierNode fp_type(TypeCategory::FunctionPointer, TypeQualifier::None, 64, func_decl.decl_node().identifier_token(), CVQualifier::None);
	fp_type.set_function_signature(sig);
	return fp_type;
}

inline std::optional<TypeSpecifierNode> tryGetReturnTypeFromFunctionType(
	const TypeSpecifierNode& function_like_type,
	const Token& source_token) {
	if (!function_like_type.has_function_signature()) {
		return std::nullopt;
	}

	const FunctionSignature& sig = function_like_type.function_signature();
	TypeIndex return_type_index = sig.return_type_index;
	TypeCategory return_category = sig.returnType();
	if (return_type_index.is_valid()) {
		return_type_index = return_type_index.withCategory(return_category);
	}

	SizeInBits return_size_bits{};
	if (sig.return_pointer_depth > 0 ||
		return_category == TypeCategory::FunctionPointer ||
		return_category == TypeCategory::MemberFunctionPointer ||
		return_category == TypeCategory::MemberObjectPointer) {
		return_size_bits = SizeInBits{64};
	} else if (return_type_index.is_valid()) {
		if (const TypeInfo* type_info = tryGetTypeInfo(return_type_index)) {
			if (const StructTypeInfo* struct_info = type_info->getStructInfo()) {
				return_size_bits = struct_info->sizeInBits();
			} else if (type_info->hasStoredSize()) {
				return_size_bits = type_info->sizeInBits();
			}
		}
	}
	if (!return_size_bits.is_set() &&
		return_category != TypeCategory::Invalid &&
		return_category != TypeCategory::Void) {
		return_size_bits = SizeInBits{get_type_size_bits(return_category)};
	}

	TypeSpecifierNode return_type(
		return_category,
		TypeQualifier::None,
		return_size_bits.is_set() ? return_size_bits.value : 0,
		source_token,
		CVQualifier::None);
	if (return_type_index.is_valid()) {
		return_type.set_type_index(return_type_index);
	}
	return_type.set_reference_qualifier(sig.return_reference_qualifier);
	return_type.add_pointer_levels(sig.return_pointer_depth);
	return return_type;
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
