#include "TemplateEnvironment.h"

TypeInfo::TemplateArgInfo toTemplateArgInfo(const TemplateTypeArg& arg) {
	TypeInfo::TemplateArgInfo info;
	info.type_index = arg.type_index; // carries both index and TypeCategory
	info.pointer_depth = arg.pointer_depth;
	info.pointer_cv_qualifiers = arg.pointer_cv_qualifiers;
	info.ref_qualifier = arg.ref_qualifier;
	info.cv_qualifier = arg.cv_qualifier;
	info.is_array = arg.is_array;
	info.array_size = arg.array_size();
	info.value = arg.value;
	info.is_value = arg.is_value;
	info.dependent_name = arg.dependent_name;
	info.function_signature = arg.function_signature;
	info.dependent_expr = arg.dependent_expr;
	return info;
}

TemplateTypeArg toTemplateTypeArg(const TypeInfo::TemplateArgInfo& arg) {
	TemplateTypeArg ta;
	ta.type_index = arg.type_index;
	ta.is_value = arg.is_value;
	ta.cv_qualifier = arg.cv_qualifier;
	ta.ref_qualifier = arg.ref_qualifier;
	ta.pointer_depth = static_cast<uint8_t>(arg.pointer_depth);
	ta.is_array = arg.is_array;
	ta.array_dimensions = arg.array_size.has_value()
		? std::vector<size_t>{*arg.array_size}
		: std::vector<size_t>{};
	ta.pointer_cv_qualifiers = arg.pointer_cv_qualifiers;
	ta.dependent_name = arg.dependent_name;
	ta.dependent_expr = arg.dependent_expr;
	ta.function_signature = arg.function_signature;
	ta.is_dependent = arg.dependent_name.isValid() || arg.dependent_expr.has_value();
	if (arg.is_value) {
		ta.value = arg.intValue();
	}
	return ta;
}

InlineVector<TypeInfo::TemplateArgInfo, 4> toTemplateArgInfoList(std::span<const TemplateTypeArg> args) {
	InlineVector<TypeInfo::TemplateArgInfo, 4> result;
	result.reserve(args.size());
	for (const TemplateTypeArg& arg : args) {
		result.push_back(toTemplateArgInfo(arg));
	}
	return result;
}

InlineVector<TemplateTypeArg, 4> toTemplateTypeArgList(std::span<const TypeInfo::TemplateArgInfo> args) {
	InlineVector<TemplateTypeArg, 4> result;
	result.reserve(args.size());
	for (const TypeInfo::TemplateArgInfo& arg : args) {
		result.push_back(toTemplateTypeArg(arg));
	}
	return result;
}

