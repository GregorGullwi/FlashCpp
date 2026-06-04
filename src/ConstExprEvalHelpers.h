#pragma once

#include "Parser.h"
#include "ConstExprEvaluator.h"
#include "BuiltinListInitNarrowing.h"
#include "CallNodeHelpers.h"
#include "ExpressionSubstitutor.h"
#include "MemberFunctionLookupShared.h"
#include "OverloadResolution.h"
#include "SemanticAnalysis.h"
#include "TypeTraitEvaluator.h"
#include <algorithm>
#include <limits>

namespace ConstExpr {

constexpr int kDefaultShiftWidthBits = 64;
constexpr size_t kSyntheticTokenLine = 0;
constexpr size_t kSyntheticTokenColumn = 0;
constexpr size_t kSyntheticTokenFileIndex = 0;
constexpr std::string_view kNestedTypeAliasName = "type";
constexpr size_t kInitializerListBeginMemberIndex = 0;
constexpr size_t kInitializerListEndOrSizeMemberIndex = 1;
constexpr const char* kMemberFunctionMaterializationLookupOp = "member function materialization lookup";
constexpr const char* kMemberFunctionMaterializationReplayOp = "member function materialization replay";
constexpr size_t kMaxReferenceAliasChainDepth = 8;

inline const std::unordered_map<std::string_view, EvalResult>& emptyEvalBindings() {
	static const std::unordered_map<std::string_view, EvalResult> kEmpty;
	return kEmpty;
}

std::string_view normalizeConstexprLookupName(std::string_view name);
bool isRecoverablePointerDerefFailure(const EvalResult& result);
bool sameConstexprMemberParameterTypes(
	const FunctionDeclarationNode& lhs,
	const FunctionDeclarationNode& rhs);
bool matchesConstexprFunctionName(
	const StructMemberFunction& member_func,
	StringHandle function_name_handle);
bool sameConstexprFunctionIdentity(
	const FunctionDeclarationNode& lhs,
	const FunctionDeclarationNode& rhs);
const StructMemberFunction* findCollectedMemberFunctionByIdentity(
	std::span<const StructMemberFunction* const> candidates,
	const FunctionDeclarationNode& target_function);
const FunctionDeclarationNode* findMatchingSymbolTableMemberDefinition(
	const FunctionDeclarationNode& target_function,
	EvaluationContext& context);

TypeSpecifierNode makeArrayTypeSpec(TypeIndex type_index, std::span<const size_t> array_dimensions);
EvalResult materializeArrayInitializer(
	TypeIndex type_index,
	std::span<const size_t> array_dimensions,
	const InitializerListNode& init_list,
	ConstExpr::EvaluationContext& context);
std::optional<EvalResult> tryMaterializeMultidimArrayRow(
	const TypeSpecifierNode* type_spec,
	const InitializerListNode& init_list,
	size_t index,
	ConstExpr::EvaluationContext& context);

std::optional<EvalResult> tryEvaluateEnumConstant(
	const EnumTypeInfo& enum_info,
	TypeIndex enum_type_index,
	StringHandle enumerator_name);
TemplateParameterKind inferTemplateBindingKindForLookup(const TemplateTypeArg& arg);
TemplateEnvironment buildOuterFunctionTemplateEnvironment(
	const InlineVector<StringHandle, 4>& param_names,
	const InlineVector<TypeInfo::TemplateArgInfo, 4>& args);
const StructMemberFunction* findMemberFunctionMetadataRecursive(
	const StructTypeInfo* struct_info,
	const FunctionDeclarationNode& target_function);
const StructMemberFunction* findFinalOverrider(
	const StructTypeInfo* dynamic_struct_info,
	const StructMemberFunction& base_virtual,
	size_t param_count);
InlineVector<TemplateParameterNode, 4> getTemplateParametersForTypeInfo(
	const TypeInfo& owner_type_info,
	Parser& parser_context);
std::optional<TemplateTypeArg> trySubstituteDependentTemplateArgForLookup(
	const TemplateTypeArg& dependent_arg,
	EvaluationContext& context,
	const TypeInfo* owner_type_info,
	int recursion_depth);

struct ShiftEvaluationInfo {
	int width_bits = kDefaultShiftWidthBits;
	std::optional<TypeSpecifierNode> promoted_type;
};

const StructMember* findMemberInfoRecursive(const StructTypeInfo* struct_info, StringHandle member_name_handle);
TypeSpecifierNode makeMemberTypeSpecForDefaultInit(const StructMember& member);
TypeSpecifierNode makeTypeSpecForDefaultInit(TypeIndex type_index);
EvalResult makeConstructorDefaultInitFromType(const TypeSpecifierNode& type_spec, EvaluationContext& context);

inline EvalResult makeConstructorMemberDefaultInit(const StructMember& member, EvaluationContext& context) {
	return makeConstructorDefaultInitFromType(makeMemberTypeSpecForDefaultInit(member), context);
}

std::optional<TypeSpecifierNode> try_get_type_from_eval_result(const EvalResult& value);

bool isReferenceAliasBinding(const EvalResult& value);
const EvalResult* resolveReadThroughReferenceAlias(
	const EvalResult& bound_value,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context);
EvalResult read_bound_identifier_value(
	const EvalResult& bound_value,
	std::string_view name,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context);

inline int normalize_shift_width(int width_bits) {
	return (width_bits > 0 && width_bits <= kDefaultShiftWidthBits) ? width_bits : kDefaultShiftWidthBits;
}

std::optional<TypeSpecifierNode> try_get_promoted_shift_operand_type(const EvalResult& value);
ShiftEvaluationInfo get_shift_evaluation_info(const EvalResult& value);
EvalResult make_shift_result(const std::optional<TypeSpecifierNode>& promoted_type, unsigned long long value);
EvalResult make_shift_result(const std::optional<TypeSpecifierNode>& promoted_type, long long value);
TypeSpecifierNode makeAggregateMemberTypeSpec(const StructMember& member_info);
EvalResult applyAggregateMemberScalarInitialization(
	const StructMember& member_info,
	EvalResult value,
	bool enforce_list_narrowing);
std::optional<size_t> try_get_constexpr_pointer_upper_bound(
	std::string_view var_name,
	EvaluationContext* context,
	const std::unordered_map<std::string_view, EvalResult>* bindings);
EvalResult make_checked_constexpr_pointer_result(
	std::string_view var_name,
	int64_t offset,
	EvaluationContext* context,
	const std::unordered_map<std::string_view, EvalResult>* bindings);
std::optional<TypeSpecifierNode> get_binary_arithmetic_result_type(
	const EvalResult& lhs, const EvalResult& rhs);
unsigned long long apply_uint_type_mask(
	unsigned long long value, const std::optional<TypeSpecifierNode>& type_opt);
const EvalResult* findLocalBinding(std::string_view name, EvaluationContext& context);
EvalResult* findMutableLocalBinding(std::string_view name, EvaluationContext& context);
const EvalResult* findBindingValue(
	std::string_view name,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context);

inline bool isReferenceAliasBinding(const EvalResult& value) {
	return value.pointer_to_var.isValid() &&
		value.exact_type.has_value() &&
		value.exact_type->reference_qualifier() != ReferenceQualifier::None;
}

const EvalResult* resolveReadThroughReferenceAlias(
	const EvalResult& bound_value,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context);
bool resolveMutableReferenceAliasTarget(
	EvalResult*& target_binding,
	std::string_view& target_name,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	std::string_view diagnostic_name,
	std::optional<EvalResult>& resolve_error);
EvalResult* findMutableBindingValue(
	std::string_view name,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context);
void refreshPointerSnapshotsForBindingInMap(
	std::unordered_map<std::string_view, EvalResult>& binding_map,
	std::string_view target_name,
	const EvalResult& target_value);
void refreshPointerSnapshotsForBinding(
	std::string_view target_name,
	const EvalResult& target_value,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context);

struct BoundWriteTarget {
	EvalResult* slot = nullptr;
	std::string_view root_name;
};

std::optional<BoundWriteTarget> resolveBoundWriteTarget(
	const ASTNode& expr,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	EvalResult (*evaluate_index_expression)(
		const ASTNode&,
		const std::unordered_map<std::string_view, EvalResult>&,
		EvaluationContext&),
	std::optional<EvalResult>& resolve_error);

std::string_view getArrayNameForAddressOf(const ASTNode& array_expr);

EvalResult make_zero_array_for_dims(std::span<const size_t> dims, TypeCategory element_type);

inline TypeSpecifierNode makeArrayTypeSpec(TypeIndex type_index, std::span<const size_t> array_dimensions) {
	TypeSpecifierNode type_spec;
	type_spec.set_type_index(type_index);
	type_spec.set_array_dimensions(array_dimensions);
	return type_spec;
}

EvalResult materializeArrayInitializer(
	TypeIndex type_index,
	std::span<const size_t> array_dimensions,
	const InitializerListNode& init_list,
	ConstExpr::EvaluationContext& context);
std::optional<EvalResult> tryMaterializeMultidimArrayRow(
	const TypeSpecifierNode* type_spec,
	const InitializerListNode& init_list,
	size_t index,
	ConstExpr::EvaluationContext& context);
EvalResult materialize_member_initializer_value(
	const StructMember& member_info,
	const ASTNode& initializer,
	EvaluationContext& context,
	const std::unordered_map<std::string_view, EvalResult>* evaluation_bindings,
	bool enforce_list_narrowing);

} // namespace ConstExpr
