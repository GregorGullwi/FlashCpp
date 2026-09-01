#pragma once

#include "InlineVector.h"
#include "ChunkedString.h"
#include <string_view>

enum class TemplateParameterKind : int;
enum class TypeCategory : uint8_t;

using TemplateParamNameVector =
	InlineVector<StringHandle, 4, FlashCpp::InlineVectorSpillFamily::TemplateArgument>;
using TemplateParameterKindVector =
	InlineVector<TemplateParameterKind, 4, FlashCpp::InlineVectorSpillFamily::TemplateArgument>;
using TemplateTypeCategoryVector =
	InlineVector<TypeCategory, 4, FlashCpp::InlineVectorSpillFamily::TemplateArgument>;
using TemplateParamNameViewVector =
	InlineVector<std::string_view, 4, FlashCpp::InlineVectorSpillFamily::TemplateArgument>;
using TemplateCycleStack =
	InlineVector<StringHandle, 8, FlashCpp::InlineVectorSpillFamily::TemplateArgument>;
