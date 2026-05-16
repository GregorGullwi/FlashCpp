#pragma once

#include <optional>
#include <span>
#include <vector>

#include "AstNodeTypes_DeclNodes.h"
#include "TemplateRegistry_Types.h"

enum class TemplateParameterKind;
class TemplateParameterNode;
struct OuterTemplateBinding;

enum class TemplateSubstitutionFailurePolicy {
	SfinaeProbe,
	HardUse,
	ShapeOnly
};

namespace ConstExpr {
struct EvaluationContext;
}

class SemanticAnalysis;

struct TemplateBinding {
	StringHandle name;
	TemplateParameterKind kind{};
	bool is_pack = false;
	InlineVector<TemplateTypeArg, 1> args;
};

struct TemplateBindingSnapshot {
	StringHandle name;
	TemplateParameterKind kind{};
	bool is_pack = false;
	InlineVector<TypeInfo::TemplateArgInfo, 1> args;
};

struct TemplateEnvironmentSnapshotNode {
	const TemplateEnvironmentSnapshotNode* parent = nullptr;
	InlineVector<TemplateBindingSnapshot, 4> bindings;
};

struct TemplateEnvironmentSnapshot {
	const TemplateEnvironmentSnapshotNode* node = nullptr;
};

struct TemplateEnvironment {
	InlineVector<TemplateBinding, 4> bindings;
	const TemplateEnvironment* parent = nullptr;

	const TemplateTypeArg* findOne(StringHandle name) const;
	std::span<const TemplateTypeArg> findPack(StringHandle name) const;
};

struct TemplatePackExpansionState {
	StringHandle pack_name{};
	size_t current_index = 0;
	size_t element_count = 0;
	bool expanding = false;
};

struct TemplateLookupContext {
	TemplateNameLookupKind lookup_kind = TemplateNameLookupKind::Ordinary;
	TemplateNameLookupTiming timing = TemplateNameLookupTiming::PointOfDefinition;
	NamespaceHandle definition_namespace{};
	NamespaceHandle point_of_instantiation_namespace{};
	StringHandle current_scope_name{};
	bool is_dependent = false;

	TemplateNameLookupRequest makeRequest(StringHandle name) const {
		TemplateNameLookupRequest request;
		request.name = name;
		request.lookup_kind = lookup_kind;
		request.timing = timing;
		request.is_dependent = is_dependent;
		request.definition_namespace = definition_namespace;
		request.point_of_instantiation_namespace = point_of_instantiation_namespace;
		request.current_instantiation_name = current_scope_name;
		return request;
	}
};

struct TemplateInstantiationContext {
	const ASTNode* selected_template_declaration = nullptr;
	std::span<const TemplateParameterNode> template_parameters;
	std::span<const TemplateTypeArg> template_arguments;
	TemplateEnvironment environment;
	InlineVector<TemplatePackExpansionState, 2> pack_state;
	StringHandle current_instantiation_name{};
	TypeIndex current_instantiation_type{};
	TemplateLookupContext lookup_context;
	const TemplateDefinitionLookupContext* definition_lookup_context = nullptr;
	const ConstExpr::EvaluationContext* constexpr_context = nullptr;
	TemplateSubstitutionFailurePolicy failure_policy = TemplateSubstitutionFailurePolicy::HardUse;

	bool isSfinaeProbe() const {
		return failure_policy == TemplateSubstitutionFailurePolicy::SfinaeProbe;
	}

	bool isShapeOnly() const {
		return failure_policy == TemplateSubstitutionFailurePolicy::ShapeOnly;
	}
};

TypeInfo::TemplateArgInfo toTemplateArgInfo(const TemplateTypeArg& arg);
TemplateTypeArg toTemplateTypeArg(const TypeInfo::TemplateArgInfo& arg);
InlineVector<TypeInfo::TemplateArgInfo, 4> toTemplateArgInfoList(std::span<const TemplateTypeArg> args);
InlineVector<TemplateTypeArg, 4> toTemplateTypeArgList(std::span<const TypeInfo::TemplateArgInfo> args);
TemplateEnvironmentSnapshot buildTemplateEnvironmentSnapshot(
	std::span<const StringHandle> param_names,
	std::span<const TypeInfo::TemplateArgInfo> args,
	const TemplateEnvironmentSnapshot* parent);
TemplateEnvironmentSnapshot buildTemplateEnvironmentSnapshot(
	std::span<const StringHandle> param_names,
	std::span<const TypeInfo::TemplateArgInfo> args);
bool hasTemplateEnvironmentSnapshotBindings(const TemplateEnvironmentSnapshot& snapshot);
void populateTemplateEnvironmentLegacyViews(
	const TemplateEnvironmentSnapshot& snapshot,
	InlineVector<StringHandle, 4>& out_param_names,
	InlineVector<TypeInfo::TemplateArgInfo, 4>& out_args);

TemplateEnvironment buildTemplateEnvironment(
	std::span<const TemplateParameterNode> params,
	std::span<const TemplateTypeArg> args,
	const TemplateEnvironment* parent);
TemplateEnvironment buildTemplateEnvironment(const TemplateEnvironmentSnapshot& snapshot);
TemplateEnvironment buildTemplateEnvironment(const OuterTemplateBinding& binding);
TemplateEnvironment buildTemplateEnvironment(const TypeInfo::InstantiationContext& context);

TemplateInstantiationContext buildTemplateInstantiationContext(
	std::span<const TemplateParameterNode> params,
	std::span<const TemplateTypeArg> args,
	const TemplateEnvironment* parent,
	TemplateSubstitutionFailurePolicy failure_policy);

std::optional<TemplateTypeArg> resolveContextBinding(
	StringHandle target_name,
	const TemplateEnvironment& environment);
