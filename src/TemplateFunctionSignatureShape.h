#pragma once

#include "AstNodeTypes.h"
#include "TemplateRegistry_Types.h"
#include "TemplateTypes.h"

#include <algorithm>
#include <ranges>

// Structural signature shape for free function-template TemplateDeclId keys.
// Distinguishes overloads without using source location. Template parameter
// spellings still participate via type-specifier tokens (same as existing
// instantiation shape checks); forward→definition merges rely on registry
// replace preserving TemplateDeclId when parameter names change.

inline bool sameTypeSpecifierShape(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) {
	if (lhs.category() != rhs.category() ||
		lhs.cv_qualifier() != rhs.cv_qualifier() ||
		lhs.reference_qualifier() != rhs.reference_qualifier() ||
		lhs.pointer_levels().size() != rhs.pointer_levels().size() ||
		lhs.is_array() != rhs.is_array()) {
		return false;
	}
	for (size_t i = 0; i < lhs.pointer_levels().size(); ++i) {
		if (lhs.pointer_levels()[i].cv_qualifier != rhs.pointer_levels()[i].cv_qualifier) {
			return false;
		}
	}
	if (!std::ranges::equal(lhs.array_dimensions(), rhs.array_dimensions())) {
		return false;
	}
	if (lhs.has_function_signature() != rhs.has_function_signature()) {
		return false;
	}
	if (lhs.has_function_signature()) {
		const FunctionSignature& lhs_sig = lhs.function_signature();
		const FunctionSignature& rhs_sig = rhs.function_signature();
		if (!FlashCpp::equalFunctionSignatureIdentity(lhs_sig, rhs_sig)) {
			return false;
		}
	}
	TypeIndex lhs_type_index = lhs.type_index();
	TypeIndex rhs_type_index = rhs.type_index();
	if (lhs_type_index.needsTypeIndex() != rhs_type_index.needsTypeIndex()) {
		return false;
	}
	if (lhs_type_index.needsTypeIndex() && rhs_type_index.needsTypeIndex() &&
		lhs_type_index != rhs_type_index &&
		lhs.token().value() != rhs.token().value()) {
		return false;
	}
	return true;
}

template <typename LeftParamContainer, typename RightParamContainer>
inline bool templateParameterListsHaveMatchingShape(
	const LeftParamContainer& lhs,
	const RightParamContainer& rhs) {
	auto same_shape = [&](const auto& self, const auto& lhs_params, const auto& rhs_params) -> bool {
		if (lhs_params.size() != rhs_params.size()) {
			return false;
		}
		for (size_t i = 0; i < lhs_params.size(); ++i) {
			const TemplateParameterNode* lhs_param_ptr = tryGetTemplateParameterNode(lhs_params[i]);
			const TemplateParameterNode* rhs_param_ptr = tryGetTemplateParameterNode(rhs_params[i]);
			if (lhs_param_ptr == nullptr || rhs_param_ptr == nullptr) {
				return false;
			}
			const TemplateParameterNode& lhs_param = *lhs_param_ptr;
			const TemplateParameterNode& rhs_param = *rhs_param_ptr;
			if (lhs_param.kind() != rhs_param.kind() ||
				lhs_param.is_variadic() != rhs_param.is_variadic() ||
				lhs_param.has_concept_constraint() != rhs_param.has_concept_constraint()) {
				return false;
			}
			if (lhs_param.kind() == TemplateParameterKind::Template &&
				!self(self, lhs_param.nested_parameters(), rhs_param.nested_parameters())) {
				return false;
			}
			if (lhs_param.kind() == TemplateParameterKind::NonType) {
				if (lhs_param.has_type() != rhs_param.has_type()) {
					return false;
				}
				if (lhs_param.has_type()) {
					if (!sameTypeSpecifierShape(
							   lhs_param.type_specifier_node(),
							   rhs_param.type_specifier_node())) {
						return false;
					}
				}
			}
		}
		return true;
	};
	return same_shape(same_shape, lhs, rhs);
}

inline bool functionDeclarationsHaveMatchingShape(
	const FunctionDeclarationNode& lhs,
	const FunctionDeclarationNode& rhs) {
	if (lhs.parameter_nodes().size() != rhs.parameter_nodes().size() ||
		lhs.is_variadic() != rhs.is_variadic() ||
		lhs.is_const_member_function() != rhs.is_const_member_function() ||
		lhs.is_volatile_member_function() != rhs.is_volatile_member_function() ||
		lhs.is_noexcept() != rhs.is_noexcept() ||
		lhs.calling_convention() != rhs.calling_convention() ||
		lhs.linkage() != rhs.linkage()) {
		return false;
	}
	if (!sameTypeSpecifierShape(
			lhs.decl_node().type_specifier_node(),
			rhs.decl_node().type_specifier_node())) {
		return false;
	}
	for (size_t i = 0; i < lhs.parameter_nodes().size(); ++i) {
		if (!lhs.parameter_nodes()[i].is<DeclarationNode>() ||
			!rhs.parameter_nodes()[i].is<DeclarationNode>()) {
			return false;
		}
		const DeclarationNode& lhs_param = lhs.parameter_nodes()[i].as<DeclarationNode>();
		const DeclarationNode& rhs_param = rhs.parameter_nodes()[i].as<DeclarationNode>();
		if (lhs_param.is_parameter_pack() != rhs_param.is_parameter_pack() ||
			!sameTypeSpecifierShape(
				lhs_param.type_specifier_node(),
				rhs_param.type_specifier_node())) {
			return false;
		}
	}
	return true;
}

inline bool primaryFunctionTemplatesHaveMatchingSignature(
	const TemplateFunctionDeclarationNode& lhs,
	const TemplateFunctionDeclarationNode& rhs) {
	return templateParameterListsHaveMatchingShape(
			   lhs.template_parameters(),
			   rhs.template_parameters()) &&
		functionDeclarationsHaveMatchingShape(
			lhs.function_decl_node(),
			rhs.function_decl_node());
}
