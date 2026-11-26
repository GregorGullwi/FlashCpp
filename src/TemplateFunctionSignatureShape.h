#pragma once

#include "AstNodeTypes.h"
#include "CanonicalIdentityCompare.h"
#include "TemplateRegistry_Types.h"
#include "TemplateTypes.h"

#include <algorithm>
#include <ranges>
#include <vector>

// Structural signature shape for free function-template TemplateDeclId keys.
// Distinguishes overloads without using source location. Template parameter
// spellings still participate via type-specifier tokens (same as existing
// instantiation shape checks); forward→definition merges rely on registry
// replace preserving TemplateDeclId when parameter names change.
//
// The compare is canonical-first: two specifiers that both publish a canonical
// identity compare by that identity, so aliases and equivalent declarator
// spellings keep matching. The flat fields below still decide the shapes with no
// publishable identity (unresolved template parameters, pre-deduction auto,
// opaque nominal indexes).

inline bool sameTypeSpecifierShape(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) {
	CanonicalTypeTable& table = requireFrontendContext().canonicalTypes();
	const FlashCpp::CanonicalIdentityVerdict verdict = FlashCpp::canonicalTypeIdentityVerdict(table, lhs, rhs, FlashCpp::CanonicalIdentityMode::Exact);
	if (verdict != FlashCpp::CanonicalIdentityVerdict::Unpublished) {
		return verdict == FlashCpp::CanonicalIdentityVerdict::Identical;
	}
	if (lhs.category() != rhs.category() ||
		lhs.cv_qualifier() != rhs.cv_qualifier() ||
		lhs.reference_qualifier() != rhs.reference_qualifier() ||
		!lhs.has_same_pointer_shape(rhs) ||
		!lhs.has_same_ordered_declarator(rhs) ||
		lhs.is_array() != rhs.is_array()) {
		return false;
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

// Default template arguments are not part of a function template's signature,
// and a later declaration or a definition may omit them. When both spellings
// publish a type default, compare those types so different defaults cannot make
// two distinct declarations look like one template. Non-type default
// expressions do not participate in signature identity.
inline bool templateParameterDefaultsHaveMatchingShape(const TemplateParameterNode& lhs, const TemplateParameterNode& rhs) {
	if (!lhs.has_default() || !rhs.has_default()) {
		return true;
	}
	const ASTNode lhs_default = lhs.default_value();
	const ASTNode rhs_default = rhs.default_value();
	if (lhs_default.is<TypeSpecifierNode>() != rhs_default.is<TypeSpecifierNode>()) {
		return false;
	}
	if (!lhs_default.is<TypeSpecifierNode>()) {
		return true;
	}
	return sameTypeSpecifierShape(lhs_default.as<TypeSpecifierNode>(), rhs_default.as<TypeSpecifierNode>());
}

template <typename LeftParamContainer, typename RightParamContainer> inline bool templateParameterListsHaveMatchingDefaults(const LeftParamContainer& lhs,
	const RightParamContainer& rhs) {
	auto same_defaults = [&](const auto& self, const auto& lhs_params, const auto& rhs_params) -> bool {
		if (lhs_params.size() != rhs_params.size()) {
			return false;
		}
		for (size_t i = 0; i < lhs_params.size(); ++i) {
			const TemplateParameterNode* lhs_param_ptr = tryGetTemplateParameterNode(lhs_params[i]);
			const TemplateParameterNode* rhs_param_ptr = tryGetTemplateParameterNode(rhs_params[i]);
			if (lhs_param_ptr == nullptr || rhs_param_ptr == nullptr) {
				return false;
			}
			if (!templateParameterDefaultsHaveMatchingShape(*lhs_param_ptr, *rhs_param_ptr)) {
				return false;
			}
			if (lhs_param_ptr->kind() == TemplateParameterKind::Template &&
				!self(self, lhs_param_ptr->nested_parameters(), rhs_param_ptr->nested_parameters())) {
				return false;
			}
		}
		return true;
	};
	return same_defaults(same_defaults, lhs, rhs);
}

template <typename LeftParamContainer, typename RightParamContainer> inline bool templateParameterListsHaveMatchingShape(const LeftParamContainer& lhs,
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
	return templateParameterListsHaveMatchingShape(lhs.template_parameters(), rhs.template_parameters()) &&
		templateParameterListsHaveMatchingDefaults(lhs.template_parameters(), rhs.template_parameters()) &&
		functionDeclarationsHaveMatchingShape(lhs.function_decl_node(), rhs.function_decl_node());
}

// Stamp specifiers that name a Type parameter of this function template with
// template_decl_id and the parameter's position. Publication calls this with the
// function's own id. Redeclaration probing calls it with a candidate's id,
// because a member definition parsed inside a class template can still carry
// the enclosing template's provisional stamp on its own parameters.
inline void stampFunctionTemplateOwnParameters(TemplateFunctionDeclarationNode& template_decl, TemplateDeclId template_decl_id) {
	if (!template_decl_id) {
		return;
	}
	TemplateParameterVector& template_parameters = template_decl.template_parameters();
	const auto stampFunctionSignature = [&](FunctionSignature& root_signature) {
		std::vector<FunctionSignature*> pending_signatures{&root_signature};
		while (!pending_signatures.empty()) {
			FunctionSignature& signature = *pending_signatures.back();
			pending_signatures.pop_back();
			const auto stampFunctionType = [&](FunctionType& function_type) {
				if (function_type.template_parameter_name.isValid()) {
					for (size_t index = 0; index < template_parameters.size(); ++index) {
						const TemplateParameterNode& parameter = template_parameters[index];
						if (parameter.kind() == TemplateParameterKind::Type && parameter.nameHandle() == function_type.template_parameter_name) {
							function_type.template_parameter_decl = template_decl_id;
							function_type.template_parameter_index = static_cast<uint32_t>(index);
							break;
						}
					}
				}
				if (function_type.callable_signature) {
					function_type.callable_signature = allocatePersistentFunctionSignature(*function_type.callable_signature);
					pending_signatures.push_back(function_type.callable_signature);
				}
			};
			if (signature.hasStructuredTypes()) {
				signature.updateReturnType(stampFunctionType);
				signature.updateParameterTypes([&](OverloadVector<FunctionType, 4>& parameter_types) {
					for (FunctionType& parameter_type : parameter_types) {
						stampFunctionType(parameter_type);
					}
				});
			}
		}
	};
	const auto stampTypeSpecifier = [&](TypeSpecifierNode& root_type) {
		std::vector<TypeSpecifierNode*> pending_types{&root_type};
		while (!pending_types.empty()) {
			TypeSpecifierNode& type_spec = *pending_types.back();
			pending_types.pop_back();
			if (type_spec.has_template_parameter_identity()) {
				const StringHandle param_name = type_spec.template_parameter_name();
				for (size_t index = 0; index < template_parameters.size(); ++index) {
					const TemplateParameterNode& parameter = template_parameters[index];
					if (parameter.kind() == TemplateParameterKind::Type && parameter.nameHandle() == param_name) {
						type_spec.set_template_parameter_decl(template_decl_id, static_cast<uint32_t>(index));
						break;
					}
				}
			}
			if (type_spec.has_function_signature()) {
				FunctionSignature signature = type_spec.function_signature();
				stampFunctionSignature(signature);
				type_spec.set_function_signature(signature);
			}
			if (type_spec.has_template_specialization()) {
				for (size_t index = 0; index < type_spec.specialization_arg_count(); ++index) {
					if (type_spec.specialization_arg_is_type(index)) {
						pending_types.push_back(&type_spec.specialization_arg_type(index));
					}
				}
			}
		}
	};
	FunctionDeclarationNode& function_decl = template_decl.function_decl_node();
	stampTypeSpecifier(function_decl.decl_node().type_specifier_node());
	for (ASTNode& parameter_node : function_decl.parameter_nodes()) {
		if (parameter_node.is<DeclarationNode>()) {
			stampTypeSpecifier(parameter_node.as<DeclarationNode>().type_specifier_node());
		}
	}
	// A non-type parameter type or a default type can also name a Type parameter
	// of this same function template; those specifiers participate in the
	// template parameter shape comparison just like signature specifiers.
	for (TemplateParameterNode& parameter : template_parameters) {
		if (parameter.has_type()) {
			stampTypeSpecifier(parameter.type_specifier_node());
		}
		if (parameter.has_default()) {
			ASTNode default_value = parameter.default_value();
			if (default_value.is<TypeSpecifierNode>()) {
				stampTypeSpecifier(default_value.as<TypeSpecifierNode>());
			}
		}
	}
}
