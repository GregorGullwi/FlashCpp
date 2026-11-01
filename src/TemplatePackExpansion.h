#pragma once

#include <algorithm>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include "AstNodeTypes.h"
#include "ExpressionStructure.h"
#include "TemplateRegistry_Types.h"

enum class TemplatePackReferenceScope : uint8_t {
	Current,
	Nested,
};

struct TemplatePackExpansionBinding {
	StringHandle name;
	std::span<const TemplateTypeArg> args;
};

struct TemplatePackExpansionBindings {
	std::vector<TemplatePackExpansionBinding> pack_bindings;
	size_t expansion_count = 1;
	bool invalid = false;

	void add(StringHandle name, std::span<const TemplateTypeArg> args) {
		for (const TemplatePackExpansionBinding& binding : pack_bindings) {
			if (binding.name == name) {
				if (binding.args.size() != args.size()) {
					invalid = true;
				}
				return;
			}
		}
		if (!pack_bindings.empty() && expansion_count != args.size()) {
			invalid = true;
			return;
		}
		expansion_count = args.size();
		pack_bindings.push_back(TemplatePackExpansionBinding{name, args});
	}
};

inline bool templatePackExpansionBindingsAreConcrete(const TemplatePackExpansionBindings& bindings) {
	if (bindings.invalid) return false;
	for (const TemplatePackExpansionBinding& binding : bindings.pack_bindings) {
		if (binding.args.size() != bindings.expansion_count) {
			return false;
		}
		for (const TemplateTypeArg& argument : binding.args) {
			if (templateArgIsStructurallyDependent(argument) || (!argument.is_value && typeIndexContainsDependentPlaceholder(argument.type_index))) {
				return false;
			}
		}
	}
	return true;
}

inline auto templatePackExpansionIndices(const TemplatePackExpansionBindings& bindings) {
	return std::views::iota(size_t{0}, bindings.invalid ? size_t{0} : bindings.expansion_count);
}

template <typename Callback>
inline bool forEachTemplatePackExpansionElement(const TemplatePackExpansionBindings& bindings, Callback&& callback) {
	if (bindings.pack_bindings.empty() || !templatePackExpansionBindingsAreConcrete(bindings)) return false;
	for (size_t index : templatePackExpansionIndices(bindings)) {
		if (!callback(index)) {
			return false;
		}
	}
	return true;
}

template <typename ScalarMap>
inline ScalarMap makeTemplatePackElementSubstitutionMap(const ScalarMap& base, const TemplatePackExpansionBindings& bindings, size_t index) {
	ScalarMap result = base;
	for (const TemplatePackExpansionBinding& binding : bindings.pack_bindings) {
		if (index >= binding.args.size()) throw InternalError("Template pack expansion index out of range");
		result[binding.name.view()] = binding.args[index];
	}
	return result;
}

namespace TemplatePackExpansionDetail {

template <typename LookupPack, typename Visitor>
inline bool scanPackReferences(const TypeInfo* root_type, const ASTNode* root_expression, LookupPack&& lookup_pack, Visitor&& visitor) {
	struct WorkItem {
		const TypeInfo* type_info;
		ASTNode node;
		TemplatePackReferenceScope scope;
	};
	struct VisitKey {
		TypeIndex type_index;
		TemplatePackReferenceScope scope;
		bool operator==(const VisitKey&) const = default;
	};
	std::vector<WorkItem> worklist;
	std::vector<VisitKey> visited;
	if (root_type != nullptr) {
		worklist.push_back(WorkItem{root_type, ASTNode{}, TemplatePackReferenceScope::Current});
	} else if (root_expression != nullptr) {
		worklist.push_back(WorkItem{nullptr, *root_expression, TemplatePackReferenceScope::Current});
	} else {
		return false;
	}
	auto&& lookup = lookup_pack;
	auto&& visit = visitor;
	while (!worklist.empty()) {
		const WorkItem current = worklist.back();
		worklist.pop_back();
		if (current.type_info != nullptr) {
			if (current.type_info->name().isValid()) {
				const StringHandle candidate = current.type_info->name();
				if (std::optional<std::span<const TemplateTypeArg>> binding = lookup(candidate); binding.has_value()) {
					visit(candidate, current.scope, *binding);
					continue;
				}
			}
			const TypeIndex type_index = current.type_info->registeredTypeIndex();
			const VisitKey visit_key{type_index, current.scope};
			if (std::find(visited.begin(), visited.end(), visit_key) != visited.end()) {
				continue;
			}
			visited.push_back(visit_key);
			const auto enqueue_argument = [&](const TypeInfo::TemplateArgInfo& argument) {
				const TemplatePackReferenceScope argument_scope = argument.is_pack
					? TemplatePackReferenceScope::Nested
					: current.scope;
				StringHandle candidate;
				std::optional<std::span<const TemplateTypeArg>> binding;
				if (argument.dependent_name.isValid()) {
					binding = lookup(argument.dependent_name);
					if (binding.has_value()) {
						candidate = argument.dependent_name;
					}
				}
				const TypeInfo* nested_info = nullptr;
				if (!candidate.isValid() && !argument.is_value && argument.type_index.is_valid()) {
					nested_info = tryGetTypeInfo(argument.type_index);
					if (nested_info != nullptr && nested_info->name().isValid()) {
						binding = lookup(nested_info->name());
						if (binding.has_value()) {
							candidate = nested_info->name();
						}
					}
				}
				if (candidate.isValid()) {
					visit(candidate, argument_scope, *binding);
				} else if (nested_info != nullptr) {
					worklist.push_back(WorkItem{nested_info, ASTNode{}, argument_scope});
				}
				if (argument.dependent_expr().has_value()) {
					worklist.push_back(WorkItem{nullptr, *argument.dependent_expr(), argument_scope});
				}
			};
			for (const TypeInfo::TemplateArgInfo& argument : current.type_info->templateArgs()) enqueue_argument(argument);
			if (const auto* qualified = current.type_info->dependentQualifiedName()) {
				if (qualified->owner_template_arguments.empty()) {
					if (const auto binding = lookup(qualified->owner_name)) visit(qualified->owner_name, current.scope, *binding);
					else if (const TypeInfo* owner = tryGetTypeInfo(qualified->owner_type)) {
						worklist.push_back(WorkItem{owner, ASTNode{}, current.scope});
					}
				} else {
					for (const auto& argument : qualified->owner_template_arguments) enqueue_argument(argument);
				}
				for (const auto& member : qualified->member_chain) {
					for (const auto& argument : member.template_arguments) enqueue_argument(argument);
				}
			}
			continue;
		}
		if (!current.node.has_value()) {
			continue;
		}
		StringHandle candidate;
		if (current.node.template is<ExpressionNode>()) {
			const ExpressionNode& expression_node = current.node.template as<ExpressionNode>();
			if (const auto* parameter_reference = std::get_if<TemplateParameterReferenceNode>(&expression_node)) {
				candidate = parameter_reference->param_name();
			} else if (const auto* identifier = std::get_if<IdentifierNode>(&expression_node)) {
				candidate = identifier->getOrInternNameHandle();
			} else if (const auto* qualified_identifier = std::get_if<QualifiedIdentifierNode>(&expression_node)) {
				const std::string_view namespace_name = gNamespaceRegistry.getQualifiedName(qualified_identifier->namespace_handle());
				candidate = namespace_name.empty()
					? qualified_identifier->nameHandle()
					: StringTable::getOrInternStringHandle(namespace_name);
			}
		} else if (current.node.template is<TemplateParameterReferenceNode>()) {
			candidate = current.node.template as<TemplateParameterReferenceNode>().param_name();
		} else if (current.node.template is<IdentifierNode>()) {
			candidate = current.node.template as<IdentifierNode>().getOrInternNameHandle();
		} else if (current.node.template is<QualifiedIdentifierNode>()) {
			const QualifiedIdentifierNode& qualified_identifier = current.node.template as<QualifiedIdentifierNode>();
			const std::string_view namespace_name = gNamespaceRegistry.getQualifiedName(qualified_identifier.namespace_handle());
			candidate = namespace_name.empty()
				? qualified_identifier.nameHandle()
				: StringTable::getOrInternStringHandle(namespace_name);
		}
		if (candidate.isValid()) {
			if (std::optional<std::span<const TemplateTypeArg>> binding = lookup(candidate); binding.has_value()) {
				visit(candidate, current.scope, *binding);
			}
		}
		if (current.node.template is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type_specifier = current.node.template as<TypeSpecifierNode>();
			const TemplatePackReferenceScope type_scope = type_specifier.is_pack_expansion()
				? TemplatePackReferenceScope::Nested
				: current.scope;
			bool found_type_pack = false;
			if (type_specifier.type_index().is_valid()) {
				if (const TypeInfo* type_info = tryGetTypeInfo(type_specifier.type_index())) {
					if (type_info->name().isValid()) {
						if (std::optional<std::span<const TemplateTypeArg>> binding = lookup(type_info->name()); binding.has_value()) {
							visit(type_info->name(), type_scope, *binding);
							found_type_pack = true;
						}
					}
					if (!found_type_pack) worklist.push_back(WorkItem{type_info, ASTNode{}, type_scope});
				}
			}
			if (!found_type_pack && type_specifier.token().handle().isValid()) {
				const StringHandle name = type_specifier.token().handle();
				if (std::optional<std::span<const TemplateTypeArg>> binding = lookup(name); binding.has_value()) {
					visit(name, type_scope, *binding);
				}
			}
		}
		const TemplatePackReferenceScope child_scope = current.node.template is<PackExpansionExprNode>()
			? TemplatePackReferenceScope::Nested
			: current.scope;
		ExpressionStructure::visitExpressionChildren(
			current.node,
			[&](ExpressionStructure::ExpressionChildRole role, const ASTNode& child) {
				const TemplatePackReferenceScope role_scope =
					(role == ExpressionStructure::ExpressionChildRole::FoldPackExpression || role == ExpressionStructure::ExpressionChildRole::PackPattern)
					? TemplatePackReferenceScope::Nested
					: child_scope;
				worklist.push_back(WorkItem{nullptr, child, role_scope});
			});
	}
	return true;
}

} // namespace TemplatePackExpansionDetail

template <typename LookupPack, typename Visitor>
inline bool scanTemplateTypePackReferences(const TypeInfo& pattern, LookupPack&& lookup_pack, Visitor&& visitor) {
	return TemplatePackExpansionDetail::scanPackReferences(&pattern, nullptr, std::forward<LookupPack>(lookup_pack), std::forward<Visitor>(visitor));
}

template <typename LookupPack, typename Visitor>
inline bool scanTemplateExpressionPackReferences(const ASTNode& expression, LookupPack&& lookup_pack, Visitor&& visitor) {
	return TemplatePackExpansionDetail::scanPackReferences(nullptr, &expression, std::forward<LookupPack>(lookup_pack), std::forward<Visitor>(visitor));
}

// A direct type-pack occurrence is bound by its retained declaration handle.
// Names remain source lookup keys; they must not select a different same-spelling pack.
template <typename ParamContainer, typename ArgContainer>
inline std::optional<std::span<const TemplateTypeArg>> findDirectTypePackExpansion(const TypeInfo::TemplateArgInfo& argument,
	const ParamContainer& template_params, const ArgContainer& template_args) {
	if (!argument.is_pack || argument.is_value || !argument.type_index.is_valid()) return std::nullopt;
	size_t argument_index = 0;
	for (size_t parameter_index = 0; parameter_index < template_params.size(); ++parameter_index) {
		const TemplateParameterNode* parameter = tryGetTemplateParameterNode(template_params[parameter_index]);
		if (parameter == nullptr) continue;
		if (!parameter->is_variadic()) {
			++argument_index;
			continue;
		}
		if (argument_index > template_args.size()) return std::nullopt;
		const size_t count = countTemplatePackArguments(template_params, template_args, parameter_index, argument_index);
		if (parameter->kind() == TemplateParameterKind::Type && parameter->registered_type_index() == argument.type_index) {
			const std::span<const TemplateTypeArg> arguments(template_args.data(), template_args.size());
			const auto pack = arguments.subspan(argument_index, count);
			for (const TemplateTypeArg& element : pack) {
				if (!element.isTypeArgument() || element.is_dependent || element.dependent_name.isValid() || element.dependent_expr.has_value()) {
					return std::nullopt;
				}
			}
			return pack;
		}
		argument_index += count;
	}
	return std::nullopt;
}

template <typename ParamContainer, typename ArgContainer>
inline bool appendDirectTypePackExpansion(std::vector<TemplateTypeArg>& result, const TypeInfo::TemplateArgInfo& argument,
	const ParamContainer& template_params, const ArgContainer& template_args) {
	const auto pack = findDirectTypePackExpansion(argument, template_params, template_args);
	if (!pack.has_value()) return false;
	TemplatePackExpansionBindings bindings;
	bindings.add({}, *pack);
	return forEachTemplatePackExpansionElement(bindings, [&](size_t index) {
		TemplateTypeArg expanded = rebindDependentTemplateTypeArg((*pack)[index], argument);
		expanded.is_pack = false;
		result.push_back(std::move(expanded));
		return true;
	});
}
