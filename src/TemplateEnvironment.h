#pragma once

#include <optional>
#include <span>
#include <vector>

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
	std::vector<TemplateTypeArg> args;
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

TemplateEnvironment buildTemplateEnvironment(
	std::span<const TemplateParameterNode> params,
	std::span<const TemplateTypeArg> args,
	const TemplateEnvironment* parent);
TemplateEnvironment buildTemplateEnvironment(const OuterTemplateBinding& binding);
TemplateEnvironment buildTemplateEnvironment(const TypeInfo::InstantiationContext& context);

std::optional<TemplateTypeArg> resolveContextBinding(
	StringHandle target_name,
	const TemplateEnvironment& environment);

