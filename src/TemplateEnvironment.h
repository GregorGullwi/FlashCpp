#pragma once

#include <span>

#include "AstNodeTypes_DeclNodes.h"

TypeInfo::TemplateArgInfo toTemplateArgInfo(const TemplateTypeArg& arg);
TemplateTypeArg toTemplateTypeArg(const TypeInfo::TemplateArgInfo& arg);
InlineVector<TypeInfo::TemplateArgInfo, 4> toTemplateArgInfoList(std::span<const TemplateTypeArg> args);
InlineVector<TemplateTypeArg, 4> toTemplateTypeArgList(std::span<const TypeInfo::TemplateArgInfo> args);

