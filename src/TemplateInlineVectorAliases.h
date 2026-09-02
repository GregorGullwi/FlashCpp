#pragma once

#include "InlineVector.h"
#include "ChunkedString.h"
#include <string_view>

enum class TemplateParameterKind : int;
enum class TypeCategory : uint8_t;

using TemplateParamNameVector =
	TemplateVector<StringHandle, 4>;
using TemplateParameterKindVector =
	TemplateVector<TemplateParameterKind, 4>;
using TemplateTypeCategoryVector =
	TemplateVector<TypeCategory, 4>;
using TemplateParamNameViewVector =
	TemplateVector<std::string_view, 4>;
using TemplateCycleStack =
	TemplateVector<StringHandle, 8>;
