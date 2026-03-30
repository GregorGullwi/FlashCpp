#pragma once

#include <string_view>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

inline constexpr size_t MAX_PACK_ELEMENTS = 1000;

extern const std::unordered_set<std::string_view> type_keywords;

struct CallingConventionMapping {
	std::string_view keyword;
	CallingConvention convention;
};

inline constexpr CallingConventionMapping calling_convention_map[] = {
	{"__cdecl", CallingConvention::Cdecl},
	{"_cdecl", CallingConvention::Cdecl},
	{"__stdcall", CallingConvention::Stdcall},
	{"_stdcall", CallingConvention::Stdcall},
	{"__fastcall", CallingConvention::Fastcall},
	{"_fastcall", CallingConvention::Fastcall},
	{"__vectorcall", CallingConvention::Vectorcall},
	{"__thiscall", CallingConvention::Thiscall},
	{"__clrcall", CallingConvention::Clrcall}
};

struct MemberSizeAndAlignment {
	size_t size;
	size_t alignment;
};

const TypeInfo* lookupTypeInCurrentContext(StringHandle type_handle);
MemberSizeAndAlignment calculateMemberSizeAndAlignment(const TypeSpecifierNode& type_spec);
const StructTypeInfo* tryGetStructTypeInfo(TypeIndex type_index);
size_t getResolvedTypeSizeBytes(const TypeSpecifierNode& type_spec, TypeIndex resolved_type_index);
MemberSizeAndAlignment calculateResolvedMemberSizeAndAlignment(const TypeSpecifierNode& type_spec, TypeIndex resolved_type_index);
int getTypeSizeFromTemplateArgument(const TemplateTypeArg& arg);
InlineVector<TypeInfo::TemplateArgInfo, 4> convertToTemplateArgInfo(const std::vector<TemplateTypeArg>& template_args);
std::pair<bool, std::string_view> isDependentTemplatePlaceholder(std::string_view type_name);
std::vector<std::string_view> splitQualifiedNamespace(std::string_view qualified_namespace);
void collectLambdaCaptureCandidates(
	const ASTNode& node,
	std::unordered_set<StringHandle>& capture_candidates,
	bool& uses_implicit_this_capture
);
void findLocalVariableDeclarations(const ASTNode& node, std::unordered_set<StringHandle>& var_names);

void registerTypeParamsInScope(
	const InlineVector<StringHandle, 4>& param_names,
	const InlineVector<TemplateTypeArg, 4>& type_args,
	FlashCpp::TemplateParameterScope& scope,
	bool preserve_ref_qualifier = false
);

void registerTypeParamsInScope(
	const std::vector<ASTNode>& template_param_nodes,
	const std::vector<TemplateTypeArg>& template_args,
	FlashCpp::TemplateParameterScope& scope,
	std::unordered_map<StringHandle, TypeIndex, StringHash, StringEqual>* sfinae_map = nullptr
);

void registerOuterBindingInScope(
	const OuterTemplateBinding& outer_binding,
	FlashCpp::TemplateParameterScope& scope,
	std::unordered_map<StringHandle, TypeIndex, StringHash, StringEqual>* sfinae_map = nullptr
);

bool is_known_type_trait_name(std::string_view name);

struct TraitInfo {
	TypeTraitKind kind = TypeTraitKind::IsVoid;
	bool is_binary = false;
	bool is_variadic = false;
	bool is_no_arg = false;
};

extern const std::unordered_map<std::string_view, TraitInfo> trait_map;
std::string_view normalize_trait_name(std::string_view name);
