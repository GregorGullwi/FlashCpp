#pragma once

#include <memory>
#include <optional>
#include <span>

#include "AstNodeTypes_DeclNodes.h"

enum class TemplateParameterKind;
class TemplateParameterNode;
struct OuterTemplateBinding;

enum class TemplateSubstitutionFailurePolicy {
	SfinaeProbe,
	HardUse,
	ShapeOnly
};

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

struct TemplateEnvironmentSnapshot {
	InlineVector<TemplateBindingSnapshot, 4> bindings;
	std::shared_ptr<const TemplateEnvironmentSnapshot> parent;
};

struct TemplateEnvironment {
	InlineVector<TemplateBinding, 4> bindings;
	const TemplateEnvironment* parent = nullptr;

	const TemplateTypeArg* findOne(StringHandle name) const;
	std::span<const TemplateTypeArg> findPack(StringHandle name) const;
};

TypeInfo::TemplateArgInfo toTemplateArgInfo(const TemplateTypeArg& arg);
TemplateTypeArg toTemplateTypeArg(const TypeInfo::TemplateArgInfo& arg);
InlineVector<TypeInfo::TemplateArgInfo, 4> toTemplateArgInfoList(std::span<const TemplateTypeArg> args);
InlineVector<TemplateTypeArg, 4> toTemplateTypeArgList(std::span<const TypeInfo::TemplateArgInfo> args);
TemplateEnvironmentSnapshot buildTemplateEnvironmentSnapshot(
	std::span<const StringHandle> param_names,
	std::span<const TypeInfo::TemplateArgInfo> args,
	std::shared_ptr<const TemplateEnvironmentSnapshot> parent);
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

std::optional<TemplateTypeArg> resolveContextBinding(
	StringHandle target_name,
	const TemplateEnvironment& environment);
