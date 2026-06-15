#pragma once

#include "AstNodeTypes.h"
#include "SymbolTable.h"
#include "TemplateEnvironment.h"

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

inline TypeSpecifierNode buildMemberFunctionPointerTypeFromFunctionDeclaration(const FunctionDeclarationNode& func_decl) {
	FunctionSignature sig;
	const TypeSpecifierNode& return_type = func_decl.decl_node().type_specifier_node();
	sig.return_type_index = return_type.type_index();
	sig.return_pointer_depth = static_cast<int>(return_type.pointer_depth());
	sig.return_reference_qualifier = return_type.reference_qualifier();
	sig.is_const = func_decl.is_const_member_function();
	sig.is_volatile = func_decl.is_volatile_member_function();
	sig.is_noexcept = func_decl.is_noexcept();
	for (const auto& param_node : func_decl.parameter_nodes()) {
		if (!param_node.is<DeclarationNode>()) {
			continue;
		}
		const auto& param_type = param_node.as<DeclarationNode>().type_specifier_node();
		sig.parameter_type_indices.push_back(param_type.type_index());
	}

	TypeSpecifierNode mfp_type(
		TypeCategory::MemberFunctionPointer,
		TypeQualifier::None,
		64,
		func_decl.decl_node().identifier_token(),
		CVQualifier::None);
	mfp_type.set_function_signature(sig);
	if (!func_decl.parent_struct_name().empty()) {
		mfp_type.set_member_class_name(
			StringTable::getOrInternStringHandle(func_decl.parent_struct_name()));
	}
	return mfp_type;
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

inline std::optional<TypeSpecifierNode> tryMaterializeTypeFromOuterTemplateBindings(
	const TypeSpecifierNode& dependent_type,
	const InlineVector<StringHandle, 4>& outer_template_param_names,
	const InlineVector<TypeInfo::TemplateArgInfo, 4>& outer_template_args,
	const Token& source_token) {
	const size_t binding_count =
		std::min(outer_template_param_names.size(), outer_template_args.size());
	if (binding_count == 0) {
		return std::nullopt;
	}

	TypeInfo::TemplateArgInfo dependent_type_arg =
		toTemplateArgInfo(TemplateTypeArg(dependent_type));
	InlineVector<TemplateParameterNode, 4> synthetic_params;
	synthetic_params.reserve(binding_count);
	InlineVector<TemplateTypeArg, 4> concrete_args;
	concrete_args.reserve(binding_count);
	for (size_t i = 0; i < binding_count; ++i) {
		const StringHandle param_name = outer_template_param_names[i];
		Token param_token(
			Token::Type::Identifier,
			StringTable::getStringView(param_name),
			source_token.line(),
			source_token.column(),
			source_token.file_index());
		synthetic_params.push_back(TemplateParameterNode(param_name, param_token));
		concrete_args.push_back(toTemplateTypeArg(outer_template_args[i]));
	}

	TemplateTypeArg materialized_type_arg = materializeTemplateArg(
		dependent_type_arg,
		synthetic_params,
		concrete_args);
	if (materialized_type_arg.is_value || materialized_type_arg.is_dependent) {
		return std::nullopt;
	}
	return makeTypeSpecifierFromTemplateTypeArg(
		materialized_type_arg,
		source_token);
}

inline std::optional<TypeSpecifierNode> tryResolveTypeFromRegisteredAlias(
	const TypeSpecifierNode& type_spec) {
	StringHandle type_name_handle = type_spec.token().handle();
	if (!type_name_handle.isValid()) {
		if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
			type_name_handle = type_info->name();
		}
	}
	if (!type_name_handle.isValid()) {
		return std::nullopt;
	}

	auto type_it = getTypesByNameMap().find(type_name_handle);
	if (type_it == getTypesByNameMap().end() || type_it->second == nullptr) {
		return std::nullopt;
	}

	const TypeInfo* type_info = type_it->second;
	if (!type_info->isTypeAlias() ||
		typeIndexContainsDependentPlaceholder(
			type_info->registeredTypeIndex().withCategory(type_info->typeEnum()))) {
		return std::nullopt;
	}

	TypeSpecifierNode outer_spec = type_spec;
	outer_spec.set_type_index(
		type_info->registeredTypeIndex().withCategory(type_info->typeEnum()));
	outer_spec.set_category(type_info->typeEnum());
	TypeSpecifierNode resolved_type =
		resolveTypeInfoToTypeSpec(*type_info, outer_spec);
	if (const int resolved_size_bits = getTypeSpecSizeBits(resolved_type);
		resolved_size_bits > 0) {
		resolved_type.set_size_in_bits(resolved_size_bits);
	}
	return resolved_type;
}

inline std::optional<TypeSpecifierNode> tryGetConcreteReturnTypeFromFunctionDeclaration(
	const FunctionDeclarationNode& func_decl,
	const Token& source_token) {
	const TypeSpecifierNode& declared_return_type =
		func_decl.decl_node().type_specifier_node();
	if (!func_decl.has_outer_template_bindings()) {
		return tryResolveTypeFromRegisteredAlias(declared_return_type);
	}
	if (std::optional<TypeSpecifierNode> concrete_type =
			tryMaterializeTypeFromOuterTemplateBindings(
				declared_return_type,
				func_decl.outer_template_param_names(),
				func_decl.outer_template_args(),
				source_token);
		concrete_type.has_value()) {
		return concrete_type;
	}
	return tryResolveTypeFromRegisteredAlias(declared_return_type);
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
