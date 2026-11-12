#include "TypeTraitEvaluator.h"

#include <algorithm>
#include <ranges>
#include <unordered_set>
#include <vector>

#include "CanonicalConstructorSelection.h"
#include "CanonicalTypeAdapter.h"
#include "ExpressionStructure.h"
#include "FlatHashTable.h"
#include "FrontendContext.h"
#include "MigrationStats.h"
#include "OverloadResolution.h"

// Canonical probe shared with overload resolution. Defined here because this
// unit can include both OverloadResolution.h and the constructor-selection
// service without creating a header cycle.
std::optional<bool> tryHasCanonicalImplicitConvertingConstructorForArgument(const TypeSpecifierNode& target_type,
	const TypeSpecifierNode& source_type, const ASTNode* argument_node) {
	FrontendContext& context = requireFrontendContext();
	CanonicalTypeTable& table = context.canonicalTypes();
	CanonicalTypeTransaction transaction(table);
	TypeSpecifierNode canonical_target = target_type;
	TypeSpecifierNode canonical_source = source_type;
	tryBindPublishedTypeEntity(canonical_target);
	tryBindPublishedTypeEntity(canonical_source);
	const CanonicalTypeImport target_import = importCanonicalTypeWithClassFallback(table, canonical_target);
	const CanonicalTypeImport source_import = importCanonicalTypeWithClassFallback(table, canonical_source);
	if (target_import.status != CanonicalTypeImportStatus::Supported || source_import.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	const TypeId target_class = stripCanonicalTopCv(table, canonicalTypeWithoutReference(table, target_import.type)).first;
	const CanonicalTypeKind target_kind = table.node(target_class).kind;
	if (target_kind != CanonicalTypeKind::Record && target_kind != CanonicalTypeKind::TemplateSpecialization) {
		return false;
	}
	if (!table.hasRecordConstructors(target_class)) {
		return std::nullopt;
	}
	const bool source_is_lvalue = table.node(source_import.type).kind == CanonicalTypeKind::LValueReference;
	const bool is_null_pointer_constant = argument_node != nullptr && isIntegerLiteralZeroNullPointerConstant(*argument_node) &&
		(source_type.category() == TypeCategory::Nullptr || isIntegralType(source_type.category()));
	const CanonicalConstructorArgument argument{source_import.type, source_is_lvalue, is_null_pointer_constant, CanonicalDefaultException::Unknown};
	const CanonicalConstructorSelection selection = selectCanonicalConstructor(table, target_class,
		std::span<const CanonicalConstructorArgument>(&argument, 1), CanonicalConstructorSelectionPolicy::AllowUserDefinedArgumentConversion |
			CanonicalConstructorSelectionPolicy::ExistenceOnly);
	if (selection.status == CanonicalConstructorSelectionStatus::Selected) {
		return true;
	}
	if (selection.status == CanonicalConstructorSelectionStatus::Unknown) {
		return std::nullopt;
	}
	return false;
}

namespace TypeTraitEval {

inline bool isScalarType(TypeCategory cat, bool is_reference, size_t pointer_depth) {
	if (is_reference)
		return false;
	if (pointer_depth > 0)
		return true;	 // Pointers are scalar
	return (cat == TypeCategory::Bool || cat == TypeCategory::Char || cat == TypeCategory::Short ||
			cat == TypeCategory::Int || cat == TypeCategory::Long || cat == TypeCategory::LongLong ||
			cat == TypeCategory::UnsignedChar || cat == TypeCategory::UnsignedShort ||
			cat == TypeCategory::UnsignedInt || cat == TypeCategory::UnsignedLong ||
			cat == TypeCategory::UnsignedLongLong || cat == TypeCategory::Float ||
			cat == TypeCategory::Double || cat == TypeCategory::LongDouble ||
			cat == TypeCategory::WChar || cat == TypeCategory::Char8 ||
			cat == TypeCategory::Char16 || cat == TypeCategory::Char32 ||
			cat == TypeCategory::Enum ||
			cat == TypeCategory::Nullptr || cat == TypeCategory::MemberObjectPointer ||
			cat == TypeCategory::MemberFunctionPointer);
}

inline bool isIntegral(TypeCategory cat) {
	return (cat == TypeCategory::Bool || cat == TypeCategory::Char ||
			cat == TypeCategory::UnsignedChar || cat == TypeCategory::Short ||
			cat == TypeCategory::UnsignedShort || cat == TypeCategory::Int ||
			cat == TypeCategory::UnsignedInt || cat == TypeCategory::Long ||
			cat == TypeCategory::UnsignedLong || cat == TypeCategory::LongLong ||
			cat == TypeCategory::UnsignedLongLong ||
			cat == TypeCategory::WChar || cat == TypeCategory::Char8 ||
			cat == TypeCategory::Char16 || cat == TypeCategory::Char32);
}

inline bool isFloatingPoint(TypeCategory cat) {
	return (cat == TypeCategory::Float || cat == TypeCategory::Double || cat == TypeCategory::LongDouble);
}

// Compatibility adapters. The signedness of a type is decided once, from its
// canonical builtin; these project a flat category onto the builtin that decides
// it so the legacy path cannot restate the policy. The projection is only used
// where a canonical import is unavailable.
inline bool isSigned(TypeCategory cat) {
	const std::optional<CanonicalBuiltinKind> builtin =
		canonicalBuiltinForSignedness(cat);
	return builtin.has_value() && canonicalBuiltinIsSigned(*builtin);
}

inline bool isUnsigned(TypeCategory cat) {
	const std::optional<CanonicalBuiltinKind> builtin =
		canonicalBuiltinForSignedness(cat);
	return builtin.has_value() && canonicalBuiltinIsUnsigned(*builtin);
}

} // namespace TypeTraitEval
std::optional<TypeId> tryCanonicalDefaultLiteralType(CanonicalTypeTable& table, const ASTNode& initializer);

bool isCanonicalNothrowLiteralExpression(const ASTNode& initializer) {
	const ExpressionNode* expression = initializer.get_if<ExpressionNode>();
	while (expression != nullptr) {
		if (const NumericLiteralNode* literal = std::get_if<NumericLiteralNode>(expression)) {
			return isArithmeticType(literal->type());
		}
		if (std::holds_alternative<BoolLiteralNode>(*expression)) {
			return true;
		}
		const UnaryOperatorNode* unary = std::get_if<UnaryOperatorNode>(expression);
		if (unary == nullptr || (unary->op() != "+" && unary->op() != "-")) {
			return false;
		}
		expression = unary->get_operand().get_if<ExpressionNode>();
	}
	return false;
}

// A class-typed default may be a constructor call that sema has not resolved
// yet. Resolve it from the constructed class's published schema when every
// argument is a scalar literal and exactly one public non-explicit constructor
// with a scalar parameter is viable. A null result leaves the exception fact
// unknown.
std::optional<bool> tryCanonicalConstructorCallDefaultNoexcept(CanonicalTypeTable& table, const ConstructorCallNode& call) {
	const std::optional<TypeId> imported = tryImportSupportedCanonicalWithClassFallback(table, call.type_node(), CanonicalTypeImportContext::Exact);
	if (!imported.has_value() || !table.hasRecordConstructors(*imported)) {
		return std::nullopt;
	}
	const auto& arguments = call.arguments();
	std::vector<CanonicalConstructorArgument> selection_arguments;
	selection_arguments.reserve(arguments.size());
	for (const ASTNode& argument : arguments) {
		const std::optional<TypeId> argument_type = tryCanonicalDefaultLiteralType(table, argument);
		const CanonicalDefaultException exception = isCanonicalNothrowLiteralExpression(argument)
			? CanonicalDefaultException::Nothrow
			: CanonicalDefaultException::Unknown;
		selection_arguments.push_back({argument_type.value_or(TypeId{}), false, false, exception});
	}
	const CanonicalConstructorSelection selection = selectCanonicalConstructor(
		table, *imported, selection_arguments,
		CanonicalConstructorSelectionPolicy::RequireCompleteSchema |
			CanonicalConstructorSelectionPolicy::LiteralArgumentsOnly |
			CanonicalConstructorSelectionPolicy::AllowUserDefinedArgumentConversion |
			CanonicalConstructorSelectionPolicy::AllowEllipsis);
	if (selection.status == CanonicalConstructorSelectionStatus::Unknown) {
		return std::nullopt;
	}
	if (selection.status == CanonicalConstructorSelectionStatus::NoMatch) {
		return selection.saw_known_throwing_candidate ? std::optional<bool>(false) : std::nullopt;
	}
	if (selection.saw_known_throwing_candidate && selection.selected_uses_class_parameter) {
		return std::nullopt;
	}
	if (!selection.is_noexcept || selection.omitted_defaults_known_throwing) {
		return false;
	}
	if (selection.omitted_default_count > 0 && !selection.omitted_defaults_nothrow) {
		return std::nullopt;
	}
	return true;
}

// A call default is nothrow when the resolved target is noexcept, its return
// type has no user-defined conversion, and every argument is a scalar literal.
bool isCanonicalNothrowCallDefault(const CallExprNode& call) {
	const FunctionDeclarationNode* function = call.callee().function_declaration_or_null();
	if (function == nullptr || !function->is_noexcept()) {
		return false;
	}
	const ASTNode& return_type_node = function->decl_node().type_node();
	if (!return_type_node.is<TypeSpecifierNode>() || is_struct_type(return_type_node.as<TypeSpecifierNode>().category())) {
		return false;
	}
	for (const ASTNode& argument : call.arguments()) {
		if (!isCanonicalNothrowLiteralExpression(argument)) {
			return false;
		}
	}
	return true;
}

// Prove both literal evaluation and builtin arithmetic parameter initialization.
// A class-typed default may also be a fully specified call to a noexcept
// constructor whose scalar literal arguments and destructor are nothrow.
// Other expressions and parameter families leave the exception fact unknown.
bool isCanonicalNothrowLiteralDefault(CanonicalTypeTable& table, TypeId parameter_type, const ASTNode& initializer) {
	TypeId parameter_type_without_reference = table.withoutTopLevelQualifiers(parameter_type);
	while (table.node(parameter_type_without_reference).kind == CanonicalTypeKind::LValueReference ||
		table.node(parameter_type_without_reference).kind == CanonicalTypeKind::RValueReference) {
		parameter_type_without_reference = table.withoutTopLevelQualifiers(table.node(parameter_type_without_reference).child);
	}
	const CanonicalTypeNode parameter = table.node(parameter_type_without_reference);
	const ExpressionNode* expression = initializer.get_if<ExpressionNode>();
	if (!isCanonicalClassTypeKind(parameter.kind) && expression != nullptr) {
		if (const CallExprNode* call = std::get_if<CallExprNode>(expression)) {
			return isCanonicalNothrowCallDefault(*call);
		}
	}
	if (parameter.kind == CanonicalTypeKind::Pointer) {
		if (expression == nullptr) {
			return false;
		}
		const NumericLiteralNode* literal = std::get_if<NumericLiteralNode>(expression);
		if (literal == nullptr) {
			return false;
		}
		const NumericLiteralValue value = literal->value();
		const auto* integral_value = std::get_if<unsigned long long>(&value);
		return integral_value != nullptr && *integral_value == 0;
	}
	if (isCanonicalClassTypeKind(parameter.kind)) {
		if (expression == nullptr || !table.hasRecordProperties(parameter_type_without_reference) ||
			!hasCanonicalRecordFact(table.recordProperties(parameter_type_without_reference).facts, CanonicalRecordFacts::NothrowDestructible)) {
			return false;
		}
		const ConstructorCallNode* constructor_call = std::get_if<ConstructorCallNode>(expression);
		if (constructor_call == nullptr) {
			return false;
		}
		const ConstructorDeclarationNode* constructor = constructor_call->resolved_constructor();
		if (constructor == nullptr) {
			return tryCanonicalConstructorCallDefaultNoexcept(table, *constructor_call).value_or(false);
		}
		if (!constructor->is_noexcept()) {
			return false;
		}
		const std::span<const ASTNode> parameters = constructor->parameter_nodes();
		const auto& arguments = constructor_call->arguments();
		if (arguments.size() != parameters.size()) {
			return false;  // omitted nested defaults may throw independently
		}
		for (size_t index = 0; index < arguments.size(); ++index) {
			if (!parameters[index].is<DeclarationNode>()) {
				return false;
			}
			const TypeSpecifierNode& nested_parameter = parameters[index].as<DeclarationNode>().type_specifier_node();
			if (is_struct_type(nested_parameter.category()) || !isCanonicalNothrowLiteralExpression(arguments[index])) {
				return false;
			}
		}
		return true;
	}
	if (parameter.kind != CanonicalTypeKind::Builtin || parameter.builtin == CanonicalBuiltinKind::Void ||
		parameter.builtin == CanonicalBuiltinKind::Nullptr || parameter.builtin == CanonicalBuiltinKind::Count) {
		return false;
	}
	return isCanonicalNothrowLiteralExpression(initializer);
}

// Read-only expression-fact lookup for recursive default analysis. A known
// throwing fact short-circuits deeper operator and call analysis; a nothrow
// fact settles the expression without re-walking its operands.
CanonicalDefaultException cachedExpressionExceptionFact(CanonicalTypeTable& table, const ASTNode& expression) {
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr || !expression.is<ExpressionNode>()) {
		return CanonicalDefaultException::Unknown;
	}
	return table.expressionExceptionFact(context->dependentExpressions().intern(expression));
}

// Prove a default expression can throw when its call target is known not to be
// noexcept. Unresolved targets stay unknown so the compatibility path is not
// made less accurate for constructors whose defaults cannot yet be classified.
bool isCanonicalKnownThrowingDefault(CanonicalTypeTable& table, const ASTNode& initializer) {
	if (const CanonicalDefaultException cached = cachedExpressionExceptionFact(table, initializer);
		cached != CanonicalDefaultException::Unknown) {
		return cached == CanonicalDefaultException::KnownThrowing;
	}
	if (initializer.is<InitializerListNode>()) {
		for (const ASTNode& element : initializer.as<InitializerListNode>().initializers()) {
			if (isCanonicalKnownThrowingDefault(table, element)) {
				return true;
			}
		}
		return false;
	}
	const ExpressionNode* expression = initializer.get_if<ExpressionNode>();
	if (expression == nullptr) {
		return false;
	}
	if (std::holds_alternative<ThrowExpressionNode>(*expression) || std::holds_alternative<NewExpressionNode>(*expression)) {
		return true;
	}
	if (const CallExprNode* call = std::get_if<CallExprNode>(expression)) {
		const FunctionDeclarationNode* function = call->callee().function_declaration_or_null();
		if (function != nullptr && !function->is_noexcept()) {
			return true;
		}
		if (function == nullptr && call->call_kind() == CalleeKind::IndirectCall) {
			return true;
		}
		for (const ASTNode& argument : call->arguments()) {
			if (isCanonicalKnownThrowingDefault(table, argument)) {
				return true;
			}
		}
		return false;
	}
	if (const ConstructorCallNode* constructor_call = std::get_if<ConstructorCallNode>(expression)) {
		const ConstructorDeclarationNode* constructor = constructor_call->resolved_constructor();
		if (constructor != nullptr) {
			if (!constructor->is_noexcept()) {
				return true;
			}
		} else {
			const std::optional<TypeId> constructed = tryImportSupportedCanonicalWithClassFallback(
				table, constructor_call->type_node(), CanonicalTypeImportContext::Exact);
			if (constructed.has_value() && table.hasRecordProperties(*constructed) &&
				!hasCanonicalRecordFact(table.recordProperties(*constructed).facts, CanonicalRecordFacts::NothrowDestructible)) {
				return true;
			}
			if (const std::optional<bool> noexcept_result = tryCanonicalConstructorCallDefaultNoexcept(table, *constructor_call);
				noexcept_result.has_value() && !*noexcept_result) {
				return true;
			}
		}
		for (const ASTNode& argument : constructor_call->arguments()) {
			if (isCanonicalKnownThrowingDefault(table, argument)) {
				return true;
			}
		}
		return false;
	}
	if (const BinaryOperatorNode* binary = std::get_if<BinaryOperatorNode>(expression)) {
		if (const StructMemberFunction* member = binary->resolved_member_operator_overload();
			member != nullptr && member->function_decl.is<FunctionDeclarationNode>() && !member->function_decl.as<FunctionDeclarationNode>().is_noexcept()) {
			return true;
		}
		if (const FunctionDeclarationNode* function = binary->resolved_free_function_operator_overload();
			function != nullptr && !function->is_noexcept()) {
			return true;
		}
		return isCanonicalKnownThrowingDefault(table, binary->get_lhs()) || isCanonicalKnownThrowingDefault(table, binary->get_rhs());
	}
	if (const UnaryOperatorNode* unary = std::get_if<UnaryOperatorNode>(expression)) {
		const FunctionDeclarationNode* function = unary->resolved_arithmetic_operator();
		if (function != nullptr && !function->is_noexcept()) {
			return true;
		}
		return isCanonicalKnownThrowingDefault(table, unary->get_operand());
	}
	if (const MemberAccessNode* member_access = std::get_if<MemberAccessNode>(expression)) {
		return isCanonicalKnownThrowingDefault(table, member_access->object());
	}
	if (const PointerToMemberAccessNode* member_access = std::get_if<PointerToMemberAccessNode>(expression)) {
		return isCanonicalKnownThrowingDefault(table, member_access->object()) || isCanonicalKnownThrowingDefault(table, member_access->member_pointer());
	}
	if (const ArraySubscriptNode* subscript = std::get_if<ArraySubscriptNode>(expression)) {
		return isCanonicalKnownThrowingDefault(table, subscript->array_expr()) || isCanonicalKnownThrowingDefault(table, subscript->index_expr());
	}
	if (const InitializerListConstructionNode* construction = std::get_if<InitializerListConstructionNode>(expression)) {
		for (const ASTNode& element : construction->elements()) {
			if (isCanonicalKnownThrowingDefault(table, element)) {
				return true;
			}
		}
		return false;
	}
	if (const TernaryOperatorNode* ternary = std::get_if<TernaryOperatorNode>(expression)) {
		return isCanonicalKnownThrowingDefault(table, ternary->condition()) || isCanonicalKnownThrowingDefault(table, ternary->true_expr()) ||
			isCanonicalKnownThrowingDefault(table, ternary->false_expr());
	}
	return false;
}

CanonicalDefaultException canonicalExpressionExceptionFact(CanonicalTypeTable& table, const ASTNode& expression) {
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr || !expression.is<ExpressionNode>()) {
		return CanonicalDefaultException::Unknown;
	}
	const ExprId identity = context->dependentExpressions().intern(expression);
	const CanonicalDefaultException cached = table.expressionExceptionFact(identity);
	if (cached != CanonicalDefaultException::Unknown) {
		return cached;
	}
	CanonicalDefaultException fact = CanonicalDefaultException::Unknown;
	if (isCanonicalNothrowLiteralExpression(expression)) {
		fact = CanonicalDefaultException::Nothrow;
	} else if (isCanonicalKnownThrowingDefault(table, expression)) {
		fact = CanonicalDefaultException::KnownThrowing;
	}
	if (fact != CanonicalDefaultException::Unknown) {
		table.publishExpressionExceptionFact(identity, fact);
	}
	return fact;
}

std::optional<TypeId> tryCanonicalDefaultLiteralType(CanonicalTypeTable& table, const ASTNode& initializer) {
	const ExpressionNode* expression = initializer.get_if<ExpressionNode>();
	if (expression == nullptr) {
		return std::nullopt;
	}
	if (const NumericLiteralNode* literal = std::get_if<NumericLiteralNode>(expression)) {
		const std::optional<CanonicalBuiltinKind> builtin = canonicalBuiltinForSignedness(literal->type());
		if (builtin.has_value()) {
			return table.builtin(*builtin);
		}
		return std::nullopt;
	}
	if (std::holds_alternative<BoolLiteralNode>(*expression)) {
		return table.builtin(CanonicalBuiltinKind::Bool);
	}
	return std::nullopt;
}

// A class-typed default parameter performs an implicit conversion during
// parameter initialization. If a simple literal selects one canonical
// constructor and that constructor is not noexcept, the default is known to
// throw independently of the enclosing constructor's exception specification.
bool isCanonicalKnownThrowingConversionDefault(CanonicalTypeTable& table, TypeId parameter_type, const ASTNode& initializer) {
	parameter_type = table.withoutTopLevelQualifiers(parameter_type);
	if (!isCanonicalClassTypeKind(table.node(parameter_type).kind) || !table.hasRecordConstructors(parameter_type)) {
		return false;
	}
	const std::optional<TypeId> argument_type = tryCanonicalDefaultLiteralType(table, initializer);
	if (!argument_type.has_value()) {
		return false;
	}
	std::optional<bool> selected_nothrow;
	size_t match_count = 0;
	const size_t constructor_count = table.recordConstructorCount(parameter_type);
	for (size_t index = 0; index < constructor_count; ++index) {
		const CanonicalRecordConstructor candidate = table.recordConstructorAt(parameter_type, index);
		if (candidate.status == CanonicalRecordConstructorStatus::Deleted) {
			continue;
		}
		if (hasCanonicalRecordFunctionFlag(candidate.flags, CanonicalRecordFunctionFlags::Deleted)) {
			continue;
		}
		if (candidate.status != CanonicalRecordConstructorStatus::Complete) {
			if (canonicalConstructorArityMatches(candidate, 1)) {
				return false;
			}
			continue;
		}
		const bool variadic = hasCanonicalRecordFunctionFlag(candidate.flags, CanonicalRecordFunctionFlags::Variadic);
		if (1 < candidate.minimum_parameter_count || (!variadic && candidate.parameter_count < 1)) {
			continue;
		}
		if (table.recordConstructorParameterAt(parameter_type, index, 0) != *argument_type) {
			continue;
		}
		++match_count;
		selected_nothrow = candidate.is_noexcept != 0;
	}
	return match_count == 1 && selected_nothrow.has_value() && !*selected_nothrow;
}


namespace {

const StructTypeInfo* structInfoFromTypeIndex(TypeIndex type_index) {
	if (const TypeInfo* type_info = tryGetTypeInfo(type_index)) {
		return type_info->getStructInfo();
	}
	return nullptr;
}

std::optional<TypeIndex> resolvePseudoDestructorObjectTypeIndex(const ASTNode& object, const SymbolTable& symbols);

ReferenceQualifier mergeReferenceQualifier(
	ReferenceQualifier lhs,
	ReferenceQualifier rhs) {
	if (lhs == ReferenceQualifier::LValueReference ||
		rhs == ReferenceQualifier::LValueReference) {
		return ReferenceQualifier::LValueReference;
	}
	if (lhs != ReferenceQualifier::None) {
		return lhs;
	}
	return rhs;
}

TypeSpecifierNode normalizeTypeTraitOperand(const TypeSpecifierNode& type_spec) {
	const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(type_spec.type_index());
	TypeCategory normalized_category = type_spec.type();
	TypeIndex normalized_type_index = type_spec.type_index();
	if (resolved_alias.type_index.is_valid()) {
		normalized_category = resolved_alias.typeEnum();
		normalized_type_index = resolved_alias.type_index.withCategory(normalized_category);
	} else if (normalized_type_index.is_valid() &&
			   normalized_category == TypeCategory::Invalid) {
		if (const TypeInfo* type_info = tryGetTypeInfo(normalized_type_index)) {
			normalized_category = type_info->typeEnum();
			normalized_type_index = normalized_type_index.withCategory(normalized_category);
		}
	} else if (!normalized_type_index.is_valid()) {
		normalized_type_index = nativeTypeIndex(normalized_category);
	} else if (normalized_type_index.category() == TypeCategory::Invalid &&
			   normalized_category != TypeCategory::Invalid) {
		normalized_type_index = normalized_type_index.withCategory(normalized_category);
	}

	TypeSpecifierNode normalized_type(
		normalized_type_index,
		0,
		type_spec.token(),
		type_spec.cv_qualifier() | resolved_alias.cv_qualifier,
		mergeReferenceQualifier(
			type_spec.reference_qualifier(),
			resolved_alias.reference_qualifier));

	for (const PointerLevel& pointer_level : type_spec.pointer_levels()) {
		normalized_type.add_pointer_level(pointer_level.cv_qualifier);
	}
	normalized_type.add_pointer_levels(static_cast<int>(resolved_alias.pointer_depth));

	std::vector<size_t> array_dimensions;
	if (type_spec.is_array()) {
		array_dimensions.assign(
			type_spec.array_dimensions().begin(),
			type_spec.array_dimensions().end());
	}
	array_dimensions.insert(
		array_dimensions.end(),
		resolved_alias.array_dimensions.begin(),
		resolved_alias.array_dimensions.end());
	if (!array_dimensions.empty()) {
		normalized_type.set_array_dimensions(array_dimensions);
	}

	if (type_spec.has_function_signature()) {
		normalized_type.set_function_signature(type_spec.function_signature());
	} else if (resolved_alias.function_signature.has_value()) {
		normalized_type.set_function_signature(*resolved_alias.function_signature);
	}

	if (type_spec.has_member_class()) {
		normalized_type.set_member_class_name(type_spec.member_class_name());
		if (type_spec.has_member_class_type_id()) {
			normalized_type.set_member_class_type_id(type_spec.member_class_type_id());
		} else if (type_spec.has_member_class_entity()) {
			normalized_type.set_member_class_entity(type_spec.member_class_entity());
		}
	} else if (resolved_alias.has_member_class_owner()) {
		applyResolvedAliasMemberOwner(normalized_type, resolved_alias);
	}
	if (type_spec.has_type_entity()) {
		normalized_type.set_type_entity(type_spec.type_entity());
	}
	if (type_spec.has_injected_class_declaration()) {
		normalized_type.set_injected_class_declaration(type_spec.injected_class_declaration());
	}
	tryBindPublishedTypeEntity(normalized_type);

	return normalized_type;
}

bool isDependentTypeTraitOperand(const TypeSpecifierNode& type_spec) {
	if (type_spec.has_template_parameter_identity()) {
		return true;
	}
	if (!type_spec.type_index().is_valid()) {
		return type_spec.category() == TypeCategory::Template ||
			   type_spec.category() == TypeCategory::Invalid;
	}
	return typeIndexContainsDependentPlaceholder(type_spec.type_index());
}

bool functionSignaturesMatch(
	const FunctionSignature& lhs,
	const FunctionSignature& rhs) {
	return FlashCpp::equalFunctionSignatureIdentity(lhs, rhs);
}

bool areSameTypeTraitOperands(
	const TypeSpecifierNode& lhs,
	const TypeSpecifierNode& rhs) {
	const TypeSpecifierNode normalized_lhs = normalizeTypeTraitOperand(lhs);
	const TypeSpecifierNode normalized_rhs = normalizeTypeTraitOperand(rhs);
	if (normalized_lhs.type() != normalized_rhs.type() ||
		(!normalized_lhs.has_function_signature() &&
		 !normalized_rhs.has_function_signature() &&
		 normalized_lhs.type_index() != normalized_rhs.type_index()) ||
		normalized_lhs.cv_qualifier() != normalized_rhs.cv_qualifier() ||
		normalized_lhs.reference_qualifier() != normalized_rhs.reference_qualifier() ||
		normalized_lhs.pointer_levels().size() != normalized_rhs.pointer_levels().size() ||
		!std::ranges::equal(
			normalized_lhs.array_dimensions(),
			normalized_rhs.array_dimensions()) ||
		normalized_lhs.has_member_class() != normalized_rhs.has_member_class() ||
		normalized_lhs.has_function_signature() != normalized_rhs.has_function_signature()) {
		return false;
	}
	for (size_t i = 0; i < normalized_lhs.pointer_levels().size(); ++i) {
		if (normalized_lhs.pointer_levels()[i].cv_qualifier !=
			normalized_rhs.pointer_levels()[i].cv_qualifier) {
			return false;
		}
	}
	if (normalized_lhs.has_member_class() &&
		normalized_lhs.member_class_name() != normalized_rhs.member_class_name()) {
		return false;
	}
	if (normalized_lhs.has_function_signature() &&
		!functionSignaturesMatch(
			normalized_lhs.function_signature(),
			normalized_rhs.function_signature())) {
		return false;
	}
	return true;
}

} // namespace

namespace {

// Unary [meta.unary.prop] properties answered from canonical type shape or
// published nominal record facts. This enumeration is the single authority
// for family membership: the answer switch is exhaustive over it, so a trait
// cannot be classified without an answer from canonical type data.
enum class CanonicalTraitProperty : uint8_t {
	None,
	IsReference,
	IsLvalueReference,
	IsRvalueReference,
	IsPointer,
	IsArray,
	IsBoundedArray,
	IsUnboundedArray,
	IsCompleteOrUnbounded,
	IsFunction,
	IsMemberObjectPointer,
	IsMemberFunctionPointer,
	IsEnum,
	IsVoid,
	IsNullptr,
	IsIntegral,
	IsFloatingPoint,
	IsArithmetic,
	IsFundamental,
	IsScalar,
	IsObject,
	IsCompound,
	IsClass,
	IsUnion,
	IsPolymorphic,
	IsFinal,
	IsAbstract,
	IsTriviallyCopyable,
	IsTrivial,
	IsPod,
	IsStandardLayout,
	IsAggregate,
	IsEmpty,
	IsDestructible,
	IsTriviallyDestructible,
	IsNothrowDestructible,
	HasTrivialDestructor,
	HasVirtualDestructor,
	IsConst,
	IsVolatile,
	IsSigned,
	IsUnsigned,
	HasUniqueObjectRepresentations,
};

CanonicalTraitProperty canonicalTraitProperty(TypeTraitKind kind) {
	switch (kind) {
	case TypeTraitKind::IsReference: return CanonicalTraitProperty::IsReference;
	case TypeTraitKind::IsLvalueReference:
		return CanonicalTraitProperty::IsLvalueReference;
	case TypeTraitKind::IsRvalueReference:
		return CanonicalTraitProperty::IsRvalueReference;
	case TypeTraitKind::IsPointer: return CanonicalTraitProperty::IsPointer;
	case TypeTraitKind::IsArray: return CanonicalTraitProperty::IsArray;
	case TypeTraitKind::IsBoundedArray: return CanonicalTraitProperty::IsBoundedArray;
	case TypeTraitKind::IsUnboundedArray: return CanonicalTraitProperty::IsUnboundedArray;
	case TypeTraitKind::IsCompleteOrUnbounded: return CanonicalTraitProperty::IsCompleteOrUnbounded;
	case TypeTraitKind::IsFunction: return CanonicalTraitProperty::IsFunction;
	case TypeTraitKind::IsMemberObjectPointer:
		return CanonicalTraitProperty::IsMemberObjectPointer;
	case TypeTraitKind::IsMemberFunctionPointer:
		return CanonicalTraitProperty::IsMemberFunctionPointer;
	case TypeTraitKind::IsEnum: return CanonicalTraitProperty::IsEnum;
	case TypeTraitKind::IsVoid: return CanonicalTraitProperty::IsVoid;
	case TypeTraitKind::IsNullptr: return CanonicalTraitProperty::IsNullptr;
	case TypeTraitKind::IsIntegral: return CanonicalTraitProperty::IsIntegral;
	case TypeTraitKind::IsFloatingPoint: return CanonicalTraitProperty::IsFloatingPoint;
	case TypeTraitKind::IsArithmetic: return CanonicalTraitProperty::IsArithmetic;
	case TypeTraitKind::IsFundamental: return CanonicalTraitProperty::IsFundamental;
	case TypeTraitKind::IsScalar: return CanonicalTraitProperty::IsScalar;
	case TypeTraitKind::IsObject: return CanonicalTraitProperty::IsObject;
	case TypeTraitKind::IsCompound: return CanonicalTraitProperty::IsCompound;
	case TypeTraitKind::IsClass: return CanonicalTraitProperty::IsClass;
	case TypeTraitKind::IsUnion: return CanonicalTraitProperty::IsUnion;
	case TypeTraitKind::IsPolymorphic: return CanonicalTraitProperty::IsPolymorphic;
	case TypeTraitKind::IsFinal: return CanonicalTraitProperty::IsFinal;
	case TypeTraitKind::IsAbstract: return CanonicalTraitProperty::IsAbstract;
	case TypeTraitKind::IsTriviallyCopyable:
		return CanonicalTraitProperty::IsTriviallyCopyable;
	case TypeTraitKind::IsTrivial: return CanonicalTraitProperty::IsTrivial;
	case TypeTraitKind::IsPod: return CanonicalTraitProperty::IsPod;
	case TypeTraitKind::IsStandardLayout:
		return CanonicalTraitProperty::IsStandardLayout;
	case TypeTraitKind::IsAggregate: return CanonicalTraitProperty::IsAggregate;
	case TypeTraitKind::IsEmpty: return CanonicalTraitProperty::IsEmpty;
	case TypeTraitKind::IsDestructible:
		return CanonicalTraitProperty::IsDestructible;
	case TypeTraitKind::IsTriviallyDestructible:
		return CanonicalTraitProperty::IsTriviallyDestructible;
	case TypeTraitKind::IsNothrowDestructible:
		return CanonicalTraitProperty::IsNothrowDestructible;
	case TypeTraitKind::HasTrivialDestructor:
		return CanonicalTraitProperty::HasTrivialDestructor;
	case TypeTraitKind::HasVirtualDestructor:
		return CanonicalTraitProperty::HasVirtualDestructor;
	case TypeTraitKind::IsConst: return CanonicalTraitProperty::IsConst;
	case TypeTraitKind::IsVolatile: return CanonicalTraitProperty::IsVolatile;
	case TypeTraitKind::IsSigned: return CanonicalTraitProperty::IsSigned;
	case TypeTraitKind::IsUnsigned: return CanonicalTraitProperty::IsUnsigned;
	case TypeTraitKind::HasUniqueObjectRepresentations: return CanonicalTraitProperty::HasUniqueObjectRepresentations;
	default:
		return CanonicalTraitProperty::None;
	}
}

bool isCanonicalIntegralBuiltin(CanonicalBuiltinKind builtin) {
	switch (builtin) {
	case CanonicalBuiltinKind::Bool:
	case CanonicalBuiltinKind::Char:
	case CanonicalBuiltinKind::SignedChar:
	case CanonicalBuiltinKind::UnsignedChar:
	case CanonicalBuiltinKind::WChar:
	case CanonicalBuiltinKind::Char8:
	case CanonicalBuiltinKind::Char16:
	case CanonicalBuiltinKind::Char32:
	case CanonicalBuiltinKind::Short:
	case CanonicalBuiltinKind::UnsignedShort:
	case CanonicalBuiltinKind::Int:
	case CanonicalBuiltinKind::UnsignedInt:
	case CanonicalBuiltinKind::Long:
	case CanonicalBuiltinKind::UnsignedLong:
	case CanonicalBuiltinKind::LongLong:
	case CanonicalBuiltinKind::UnsignedLongLong:
		return true;
	default:
		return false;
	}
}

bool isCanonicalFloatingPointBuiltin(CanonicalBuiltinKind builtin) {
	return builtin == CanonicalBuiltinKind::Float ||
		builtin == CanonicalBuiltinKind::Double ||
		builtin == CanonicalBuiltinKind::LongDouble;
}

bool isCanonicalScalarType(CanonicalTypeKind kind, CanonicalBuiltinKind builtin) {
	switch (kind) {
	case CanonicalTypeKind::Builtin:
		return builtin != CanonicalBuiltinKind::Void && builtin != CanonicalBuiltinKind::Count;
	case CanonicalTypeKind::Enum:
	case CanonicalTypeKind::Pointer:
	case CanonicalTypeKind::MemberObjectPointer:
	case CanonicalTypeKind::MemberFunctionPointer:
		return true;
	default:
		return false;
	}
}

// [basic.types] and [meta.unary.prop] classify a dependent identity by its
// eventual shape, which the canonical table cannot answer before substitution.
// These families keep their compatibility answer until the dependent families
// import. A class-template specialization is not in this set: a specialization
// is a complete class type, so its structural properties are already decided.
CanonicalRecordFacts canonicalRecordPropertyFlag(
	CanonicalTraitProperty property) {
	switch (property) {
	case CanonicalTraitProperty::IsPolymorphic:
		return CanonicalRecordFacts::Polymorphic;
	case CanonicalTraitProperty::IsFinal:
		return CanonicalRecordFacts::Final;
	case CanonicalTraitProperty::IsAbstract:
		return CanonicalRecordFacts::Abstract;
	case CanonicalTraitProperty::IsTriviallyCopyable:
		return CanonicalRecordFacts::TriviallyCopyable;
	case CanonicalTraitProperty::IsTrivial:
		return CanonicalRecordFacts::Trivial;
	case CanonicalTraitProperty::IsPod:
		return CanonicalRecordFacts::Pod;
	case CanonicalTraitProperty::IsStandardLayout:
		return CanonicalRecordFacts::StandardLayout;
	case CanonicalTraitProperty::IsAggregate:
		return CanonicalRecordFacts::Aggregate;
	case CanonicalTraitProperty::IsEmpty:
		return CanonicalRecordFacts::Empty;
	case CanonicalTraitProperty::IsDestructible:
		return CanonicalRecordFacts::Destructible;
	case CanonicalTraitProperty::IsTriviallyDestructible:
		return CanonicalRecordFacts::TriviallyDestructible;
	case CanonicalTraitProperty::IsNothrowDestructible:
		return CanonicalRecordFacts::NothrowDestructible;
	case CanonicalTraitProperty::HasTrivialDestructor:
		return CanonicalRecordFacts::HasTrivialDestructor;
	case CanonicalTraitProperty::HasVirtualDestructor:
		return CanonicalRecordFacts::HasVirtualDestructor;
	default:
		throw InternalError("canonical trait: property has no record fact");
	}
}

bool isCanonicalNonClassSubobject(CanonicalTypeKind kind) {
	switch (kind) {
	case CanonicalTypeKind::Pointer:
	case CanonicalTypeKind::LValueReference:
	case CanonicalTypeKind::RValueReference:
	case CanonicalTypeKind::MemberObjectPointer:
	case CanonicalTypeKind::MemberFunctionPointer:
	case CanonicalTypeKind::Enum:
		return true;
	default:
		return false;
	}
}

std::optional<bool> canonicalRecordDestructionProperty(CanonicalTraitProperty property, const CanonicalTypeTable& table, TypeId root_type) {
	const TypeId unqualified_root = table.withoutTopLevelQualifiers(root_type);
	std::vector<TypeId> pending{unqualified_root};
	std::unordered_set<uint32_t> visited;
	while (!pending.empty()) {
		TypeId current = pending.back();
		pending.pop_back();
		CanonicalTypeNode node = table.node(current);
		if (node.kind == CanonicalTypeKind::Qualified || node.kind == CanonicalTypeKind::Array) {
			pending.push_back(node.child);
			continue;
		}
		if (node.kind == CanonicalTypeKind::Builtin) {
			if (node.builtin == CanonicalBuiltinKind::Void) {
				return false;
			}
			continue;
		}
		if (isCanonicalNonClassSubobject(node.kind)) {
			continue;
		}
		if (node.kind != CanonicalTypeKind::Record && node.kind != CanonicalTypeKind::TemplateSpecialization) {
			return std::nullopt;
		}
		if (!visited.insert(current.value).second) {
			continue;
		}
		if (!table.hasRecordProperties(current)) {
			return std::nullopt;
		}
		const CanonicalRecordFacts facts = table.recordProperties(current).facts;
		const bool is_root = current == unqualified_root;
		const bool is_virtual_query = property == CanonicalTraitProperty::HasVirtualDestructor;
		if (!is_virtual_query && (hasCanonicalRecordFact(facts, CanonicalRecordFacts::DestructorDeleted) ||
			(is_root && hasCanonicalRecordFact(facts, CanonicalRecordFacts::DestructorNonPublic)))) {
			return false;
		}
		if (!is_virtual_query && node.kind == CanonicalTypeKind::Record &&
			!hasCanonicalRecordFact(facts, CanonicalRecordFacts::DestructorNonTrivial)) {
			bool is_union_like = false;
			if (table.hasRecordFieldSchema(current)) {
				for (size_t index = 0; index < table.recordSubobjectMemberCount(current); ++index) {
					if (hasCanonicalRecordMemberFlag(table.recordMemberAt(current, index).flags, CanonicalRecordMemberFlags::UnionVariant)) {
						is_union_like = true;
						break;
					}
				}
			}
			if (!is_union_like) {
				const EntityId entity = table.recordEntity(current);
				is_union_like = entity && table.hasRecordLayout(entity) &&
					hasCanonicalRecordLayoutFlag(table.recordLayout(entity).flags, CanonicalRecordLayoutFlags::Union);
			}
			if (is_union_like) {
				if (!table.hasRecordFieldSchema(current)) {
					return std::nullopt;
				}
				for (size_t index = 0; index < table.recordSubobjectMemberCount(current); ++index) {
					const CanonicalRecordMember member = table.recordMemberAt(current, index);
					if (!hasCanonicalRecordMemberFlag(member.flags, CanonicalRecordMemberFlags::UnionVariant)) {
						continue;
					}
					const std::optional<bool> member_trivial = canonicalRecordDestructionProperty(
						CanonicalTraitProperty::IsTriviallyDestructible, table, member.type);
					if (!member_trivial.has_value()) {
						return std::nullopt;
					}
					if (!*member_trivial) {
						return false;
					}
				}
			}
		}
		if (property == CanonicalTraitProperty::IsTriviallyDestructible || property == CanonicalTraitProperty::HasTrivialDestructor) {
			if (hasCanonicalRecordFact(facts, CanonicalRecordFacts::DestructorNonTrivial) ||
				hasCanonicalRecordFact(facts, CanonicalRecordFacts::DestructorVirtual)) {
				return false;
			}
		} else if (property == CanonicalTraitProperty::IsNothrowDestructible &&
			hasCanonicalRecordFact(facts, CanonicalRecordFacts::DestructorHasNoexceptSpecifier)) {
			if (!hasCanonicalRecordFact(facts, CanonicalRecordFacts::DestructorNoexcept)) {
				return false;
			}
			continue;
		}
		if (property == CanonicalTraitProperty::HasVirtualDestructor &&
			hasCanonicalRecordFact(facts, CanonicalRecordFacts::DestructorVirtual)) {
			return true;
		}
		if (!table.hasClassBaseSchema(current)) {
			return std::nullopt;
		}
		const size_t base_count = table.classBaseCount(current);
		for (size_t index = 0; index < base_count; ++index) {
			pending.push_back(table.classBaseAt(current, index).type);
		}
		if (property == CanonicalTraitProperty::HasVirtualDestructor) {
			continue;
		}
		if (!table.hasRecordSubobjectTypes(current)) {
			return std::nullopt;
		}
		const size_t member_count = table.recordSubobjectMemberCount(current);
		for (size_t index = 0; index < member_count; ++index) {
			pending.push_back(table.recordSubobjectMemberAt(current, index));
		}
	}
	return property != CanonicalTraitProperty::HasVirtualDestructor;
}

std::optional<bool> canonicalRecordTrivialityProperty(CanonicalTraitProperty property, const CanonicalTypeTable& table, TypeId root_type) {
	const TypeId unqualified_root = table.withoutTopLevelQualifiers(root_type);
	const bool require_trivial_default_constructor = property != CanonicalTraitProperty::IsTriviallyCopyable;
	if (property == CanonicalTraitProperty::IsPod) {
		const CanonicalTypeNode root_node = table.node(unqualified_root);
		if (root_node.kind != CanonicalTypeKind::Record && root_node.kind != CanonicalTypeKind::TemplateSpecialization) {
			return std::nullopt;
		}
	}
	std::vector<TypeId> pending{unqualified_root};
	std::unordered_set<uint32_t> visited;
	while (!pending.empty()) {
		TypeId current = pending.back();
		pending.pop_back();
		const CanonicalTypeNode node = table.node(current);
		if (node.kind == CanonicalTypeKind::Qualified || node.kind == CanonicalTypeKind::Array) {
			pending.push_back(node.child);
			continue;
		}
		if (node.kind == CanonicalTypeKind::Builtin) {
			if (node.builtin == CanonicalBuiltinKind::Void) {
				return false;
			}
			continue;
		}
		if (isCanonicalNonClassSubobject(node.kind)) {
			continue;
		}
		if (node.kind != CanonicalTypeKind::Record && node.kind != CanonicalTypeKind::TemplateSpecialization) {
			return std::nullopt;
		}
		if (!visited.insert(current.value).second) {
			continue;
		}
		if (!table.hasRecordProperties(current)) {
			return std::nullopt;
		}
		const CanonicalRecordFacts facts = table.recordProperties(current).facts;
		if (!hasCanonicalRecordFact(facts, CanonicalRecordFacts::TrivialCopyingSpecialMembers) ||
			(require_trivial_default_constructor &&
				!hasCanonicalRecordFact(facts, CanonicalRecordFacts::TrivialDefaultConstructor))) {
			return false;
		}
		if (!table.hasClassBaseSchema(current) || !table.hasRecordSubobjectTypes(current)) {
			return std::nullopt;
		}
		const size_t base_count = table.classBaseCount(current);
		for (size_t index = 0; index < base_count; ++index) {
			const CanonicalClassBase base = table.classBaseAt(current, index);
			if (hasCanonicalRecordBaseFlag(base.flags, CanonicalRecordBaseFlags::Virtual)) {
				return false;
			}
			pending.push_back(base.type);
		}
		const size_t member_count = table.recordSubobjectMemberCount(current);
		for (size_t index = 0; index < member_count; ++index) {
			pending.push_back(table.recordSubobjectMemberAt(current, index));
		}
	}
	if (property == CanonicalTraitProperty::IsPod) {
		return hasCanonicalRecordFact(table.recordProperties(unqualified_root).facts, CanonicalRecordFacts::StandardLayout);
	}
	return true;
}

// Classifies one canonical type. `type` is the imported identity, not a peeled
// node: cv qualification and array bounds are part of the answer for some
// properties, so each property decides how far to walk.
std::optional<bool> canonicalNodeSatisfies(CanonicalTraitProperty property, const CanonicalTypeTable& table, TypeId type) {
	// [dcl.array] an array type is identically cv-qualified to its element, and
	// [dcl.ref] cv-qualifiers introduced through a reference are ignored. The
	// array walk is iterative so array rank stays off the native stack.
	TypeId walk = type;
	const CanonicalTypeNode outer = table.node(walk);
	const bool is_reference = outer.kind == CanonicalTypeKind::LValueReference || outer.kind == CanonicalTypeKind::RValueReference;
	switch (property) {
	case CanonicalTraitProperty::IsConst:
	case CanonicalTraitProperty::IsVolatile: {
		if (is_reference) {
			return false;
		}
		CVQualifier accumulated = CVQualifier::None;
		while (true) {
			const CanonicalTypeNode node = table.node(walk);
			if (node.kind == CanonicalTypeKind::Array) {
				walk = node.child;
				continue;
			}
			if (node.kind == CanonicalTypeKind::Qualified) {
				accumulated |= node.qualifiers;
			}
			break;
		}
		const CVQualifier bit = property == CanonicalTraitProperty::IsConst ? CVQualifier::Const : CVQualifier::Volatile;
		return (static_cast<uint8_t>(accumulated) & static_cast<uint8_t>(bit)) != 0;
	}
	default:
		break;
	}

	// Every remaining property reads the outermost component; top-level cv does
	// not change a structural classification.
	// `IsConst` and `IsVolatile` returned above; listing them keeps this switch
	// exhaustive, so a new property cannot be added without an answer.
	// Peeling the qualifier has to advance the identity too, not just the node:
	// a record's EntityId is read from the Record node, not from a wrapper.
	TypeId peeled = walk;
	if (outer.kind == CanonicalTypeKind::Qualified) {
		peeled = outer.child;
	}
	const CanonicalTypeNode node = table.node(peeled);
	const CanonicalTypeKind kind = node.kind;
	const bool is_builtin = kind == CanonicalTypeKind::Builtin;
	const CanonicalBuiltinKind builtin = node.builtin;
	const bool is_scalar = !is_reference && isCanonicalScalarType(kind, builtin);
	const bool is_bounded_array = kind == CanonicalTypeKind::Array && hasCanonicalTypeNodeFlag(node.flags, CanonicalTypeNodeFlags::KnownArrayBound);
	const bool is_unbounded_array = kind == CanonicalTypeKind::Array && !is_bounded_array;
	switch (property) {
	case CanonicalTraitProperty::IsReference:
		return is_reference;
	case CanonicalTraitProperty::IsLvalueReference:
		return kind == CanonicalTypeKind::LValueReference;
	case CanonicalTraitProperty::IsRvalueReference:
		return kind == CanonicalTypeKind::RValueReference;
	case CanonicalTraitProperty::IsPointer:
		return kind == CanonicalTypeKind::Pointer;
	case CanonicalTraitProperty::IsArray:
		return is_bounded_array || is_unbounded_array;
	case CanonicalTraitProperty::IsBoundedArray:
		return is_bounded_array;
	case CanonicalTraitProperty::IsUnboundedArray:
		return is_unbounded_array;
	case CanonicalTraitProperty::IsCompleteOrUnbounded: {
		TypeId complete_type = type;
		for (;;) {
			const CanonicalTypeNode complete_node = table.node(complete_type);
			if (complete_node.kind == CanonicalTypeKind::Qualified) {
				complete_type = complete_node.child;
				continue;
			}
			if (complete_node.kind == CanonicalTypeKind::Array) {
				if (!hasCanonicalTypeNodeFlag(complete_node.flags, CanonicalTypeNodeFlags::KnownArrayBound)) {
					return true;
				}
				complete_type = complete_node.child;
				continue;
			}
			if (complete_node.kind == CanonicalTypeKind::Builtin) {
				return complete_node.builtin != CanonicalBuiltinKind::Void;
			}
			if (complete_node.kind == CanonicalTypeKind::Record) {
				const EntityId entity = table.recordEntity(complete_type);
				return entity && table.hasRecordLayout(entity);
			}
			if (complete_node.kind == CanonicalTypeKind::TemplateSpecialization) {
				if (table.hasRecordProperties(complete_type)) {
					return true;
				}
				return std::nullopt;
			}
			return true;
		}
	}
	case CanonicalTraitProperty::IsFunction:
		return kind == CanonicalTypeKind::Function;
	case CanonicalTraitProperty::IsMemberObjectPointer:
		return kind == CanonicalTypeKind::MemberObjectPointer;
	case CanonicalTraitProperty::IsMemberFunctionPointer:
		return kind == CanonicalTypeKind::MemberFunctionPointer;
	case CanonicalTraitProperty::IsEnum:
		return kind == CanonicalTypeKind::Enum;
	case CanonicalTraitProperty::IsVoid:
		return !is_reference && is_builtin && builtin == CanonicalBuiltinKind::Void;
	case CanonicalTraitProperty::IsNullptr:
		return !is_reference && is_builtin && builtin == CanonicalBuiltinKind::Nullptr;
	case CanonicalTraitProperty::IsIntegral:
		return !is_reference && is_builtin && isCanonicalIntegralBuiltin(builtin);
	case CanonicalTraitProperty::IsFloatingPoint:
		return !is_reference && is_builtin && isCanonicalFloatingPointBuiltin(builtin);
	case CanonicalTraitProperty::IsArithmetic:
		return !is_reference && is_builtin && (isCanonicalIntegralBuiltin(builtin) || isCanonicalFloatingPointBuiltin(builtin));
	case CanonicalTraitProperty::IsFundamental:
		return !is_reference && is_builtin;
	case CanonicalTraitProperty::IsScalar:
		return is_scalar;
	case CanonicalTraitProperty::IsObject:
		return !is_reference && kind != CanonicalTypeKind::Function && !(is_builtin && builtin == CanonicalBuiltinKind::Void);
	case CanonicalTraitProperty::IsCompound:
		return !is_builtin;
	case CanonicalTraitProperty::IsSigned:
		return !is_reference && is_builtin && canonicalBuiltinIsSigned(builtin);
	case CanonicalTraitProperty::IsUnsigned:
		return !is_reference && is_builtin && canonicalBuiltinIsUnsigned(builtin);
	case CanonicalTraitProperty::HasUniqueObjectRepresentations:
		return !is_reference && is_builtin && isCanonicalIntegralBuiltin(builtin) && builtin != CanonicalBuiltinKind::Bool;
	case CanonicalTraitProperty::IsClass:
	case CanonicalTraitProperty::IsUnion: {
		// A class-template specialization is a class type and never a union.
		if (kind == CanonicalTypeKind::TemplateSpecialization) {
			return property == CanonicalTraitProperty::IsClass;
		}
		if (kind != CanonicalTypeKind::Record) {
			return false;
		}
		// A record with no published complete-object layout may be a forward
		// declaration, so the class/union split is not yet decidable. Fail
		// closed to the compatibility classifier rather than guessing.
		const EntityId entity = table.recordEntity(peeled);
		if (!entity || !table.hasRecordLayout(entity)) {
			throw InternalError("canonical trait: class trait needs a published record layout");
		}
		const CanonicalRecordLayout layout = table.recordLayout(entity);
		const bool is_union = hasCanonicalRecordLayoutFlag(layout.flags, CanonicalRecordLayoutFlags::Union);
		return property == CanonicalTraitProperty::IsUnion ? is_union : !is_union;
	}
	case CanonicalTraitProperty::IsPolymorphic:
	case CanonicalTraitProperty::IsFinal:
	case CanonicalTraitProperty::IsAbstract:
	case CanonicalTraitProperty::IsTriviallyCopyable:
	case CanonicalTraitProperty::IsTrivial:
	case CanonicalTraitProperty::IsPod:
	case CanonicalTraitProperty::IsStandardLayout:
	case CanonicalTraitProperty::IsAggregate:
	case CanonicalTraitProperty::IsEmpty:
	case CanonicalTraitProperty::IsDestructible:
	case CanonicalTraitProperty::IsTriviallyDestructible:
	case CanonicalTraitProperty::IsNothrowDestructible:
	case CanonicalTraitProperty::HasTrivialDestructor:
	case CanonicalTraitProperty::HasVirtualDestructor: {
		if (is_scalar) {
			switch (property) {
			case CanonicalTraitProperty::IsTriviallyCopyable:
			case CanonicalTraitProperty::IsTrivial:
			case CanonicalTraitProperty::IsPod:
			case CanonicalTraitProperty::IsStandardLayout:
			case CanonicalTraitProperty::IsDestructible:
			case CanonicalTraitProperty::IsTriviallyDestructible:
			case CanonicalTraitProperty::IsNothrowDestructible:
			case CanonicalTraitProperty::HasTrivialDestructor:
				return true;
			case CanonicalTraitProperty::IsAggregate:
			case CanonicalTraitProperty::IsEmpty:
			case CanonicalTraitProperty::HasVirtualDestructor:
				return false;
			default:
				break;
			}
		}
		if (is_reference || (kind != CanonicalTypeKind::Record && kind != CanonicalTypeKind::TemplateSpecialization)) {
			const bool is_existing_class_trait = property == CanonicalTraitProperty::IsPolymorphic || property == CanonicalTraitProperty::IsFinal ||
				property == CanonicalTraitProperty::IsAbstract;
			if (is_existing_class_trait) {
				return false;
			}
			return std::nullopt;
		}
		if (!table.hasRecordProperties(peeled)) {
			if (kind == CanonicalTypeKind::Record) {
				const EntityId entity = table.recordEntity(peeled);
				if (entity && table.hasRecordLayout(entity)) {
					throw InternalError("canonical trait: complete record has no published property facts");
				}
			}
			return std::nullopt;
		}
		if (property == CanonicalTraitProperty::HasTrivialDestructor) {
			return hasCanonicalRecordFact(table.recordProperties(peeled).facts, CanonicalRecordFacts::HasTrivialDestructor);
		}
		if (property == CanonicalTraitProperty::IsDestructible || property == CanonicalTraitProperty::IsTriviallyDestructible ||
			property == CanonicalTraitProperty::IsNothrowDestructible || property == CanonicalTraitProperty::HasVirtualDestructor) {
			return canonicalRecordDestructionProperty(property, table, peeled);
		}
		if (property == CanonicalTraitProperty::IsTriviallyCopyable ||
			property == CanonicalTraitProperty::IsTrivial || property == CanonicalTraitProperty::IsPod) {
			return canonicalRecordTrivialityProperty(property, table, peeled);
		}
		const CanonicalRecordFacts facts = table.recordProperties(peeled).facts;
		return hasCanonicalRecordFact(facts, canonicalRecordPropertyFlag(property));
	}
	case CanonicalTraitProperty::IsConst:
	case CanonicalTraitProperty::IsVolatile:
	case CanonicalTraitProperty::None:
		break;
	}
	throw InternalError("canonical trait: unclassified structural property");
}

CanonicalTypeImport importCanonicalTraitOperand(CanonicalTypeTable& table, const TypeSpecifierNode& syntax) {
	const CanonicalTypeImport imported = importCanonicalStructuralTraitOperand(table, syntax);
	const bool has_injected_class_entity = syntax.has_injected_class_declaration() && syntax.injected_class_declaration()->has_entity_id();
	const bool has_syntax_entity = syntax.has_type_entity() || has_injected_class_entity;
	if (imported.status == CanonicalTypeImportStatus::Supported && !has_syntax_entity && syntax.type_index().is_valid()) {
		const TypeInfo* type_info = tryGetTypeInfo(syntax.type_index());
		const StructTypeInfo* struct_info = type_info == nullptr ? nullptr : canonicalClassStructInfoFromTypeInfo(*type_info);
		if (struct_info != nullptr && struct_info->declaration_node != nullptr && struct_info->declaration_node->has_entity_id()) {
			recordCanonicalTypeEntityImportRecovery();
		}
	}
	return imported;
}

} // namespace

std::optional<TypeTraitResult> tryEvaluateCanonicalSameTrait(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) {
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		return std::nullopt;
	}
	CanonicalTypeTable& table = context->canonicalTypes();
	CanonicalTypeTransaction transaction(table);
	// A class-template specialization projected as a nominal specifier needs the
	// structural-trait importer to recover its published EntityId.
	const CanonicalTypeImport lhs_type = importCanonicalTraitOperand(table, lhs);
	if (lhs_type.status == CanonicalTypeImportStatus::Invalid) {
		return TypeTraitResult::failure();
	}
	if (lhs_type.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	const CanonicalTypeImport rhs_type = importCanonicalTraitOperand(table, rhs);
	if (rhs_type.status == CanonicalTypeImportStatus::Invalid) {
		return TypeTraitResult::failure();
	}
	if (rhs_type.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	if (isDependentCanonicalNode(table.node(lhs_type.type).kind) || isDependentCanonicalNode(table.node(rhs_type.type).kind)) {
		// [temp.over.link] compares dependent names without the result of lookup
		// in the template context; a dependent operand keeps its compatibility
		// answer until substitution.
		return std::nullopt;
	}
	return lhs_type.type == rhs_type.type ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
}

std::optional<TypeTraitResult> tryEvaluateCanonicalStructuralTrait(TypeTraitKind kind, const TypeSpecifierNode& type_spec) {
	const CanonicalTraitProperty property = canonicalTraitProperty(kind);
	if (property == CanonicalTraitProperty::None) {
		return std::nullopt;
	}
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		recordCanonicalStructuralTraitFallback();
		return std::nullopt;
	}

	CanonicalTypeTable& table = context->canonicalTypes();
	CanonicalTypeTransaction transaction(table);
	const CanonicalTypeImport imported_type = importCanonicalTraitOperand(table, type_spec);
	if (imported_type.status == CanonicalTypeImportStatus::Invalid) {
		return TypeTraitResult::failure();
	}
	if (imported_type.status != CanonicalTypeImportStatus::Supported || isDependentCanonicalNode(table.node(imported_type.type).kind)) {
		if (type_spec.has_ordered_declarator() && !type_spec.ordered_declarator_has_legacy_projection()) {
			return TypeTraitResult::failure();
		}
		recordCanonicalStructuralTraitFallback();
		return std::nullopt;
	}

	// Each property decides how far to walk the canonical chain: cv
	// qualification and array bounds are part of some answers, so the peel is not
	// hoisted out here.
	const std::optional<bool> canonical_result = canonicalNodeSatisfies(property, table, imported_type.type);
	if (!canonical_result.has_value()) {
		recordCanonicalStructuralTraitFallback();
		return std::nullopt;
	}
	return *canonical_result ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
}

CanonicalRecordFacts canonicalConstructionFlagForTrait(
	TypeTraitKind kind) {
	switch (kind) {
	case TypeTraitKind::IsConstructible:
		return CanonicalRecordFacts::DefaultConstructible;
	case TypeTraitKind::IsTriviallyConstructible:
		return CanonicalRecordFacts::TriviallyDefaultConstructible;
	case TypeTraitKind::IsNothrowConstructible:
		return CanonicalRecordFacts::NothrowDefaultConstructible;
	default:
		return CanonicalRecordFacts::None;
	}
}

// Zero-argument default-construction query keyed by canonical identity. Records
// and class-template specializations answer from the published construction
// fact for the requested variant; builtins, pointers, and enums are
// constructible, references, arrays, functions, and void are not. Returns
// nullopt when the operand cannot be imported or its fact is not published, so
// the caller keeps its compatibility answer.
static std::optional<TypeTraitResult> tryEvaluateCanonicalDefaultConstructionTrait(TypeTraitKind kind, const TypeSpecifierNode& type_spec) {
	const CanonicalRecordFacts property = canonicalConstructionFlagForTrait(kind);
	if (property == CanonicalRecordFacts::None) {
		return std::nullopt;
	}
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		return std::nullopt;
	}
	CanonicalTypeTable& table = context->canonicalTypes();
	CanonicalTypeTransaction transaction(table);
	// The lazy operand materializer projects a class-template specialization as
	// a nominal specifier; the structural-trait importer recovers its published
	// EntityId before the plain importer would defer it.
	const CanonicalTypeImport imported = importCanonicalTraitOperand(table, type_spec);
	if (imported.status == CanonicalTypeImportStatus::Invalid) {
		return TypeTraitResult::failure();
	}
	if (imported.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	TypeId type = imported.type;
	const CanonicalTypeKind imported_kind = table.node(type).kind;
	if (imported_kind == CanonicalTypeKind::LValueReference || imported_kind == CanonicalTypeKind::RValueReference) {
		return TypeTraitResult::success_false();
	}
	type = table.withoutTopLevelQualifiers(type);
	const CanonicalTypeNode node = table.node(type);
	switch (node.kind) {
	case CanonicalTypeKind::Record:
	case CanonicalTypeKind::TemplateSpecialization:
		if (!table.hasRecordProperties(type)) {
			return std::nullopt;
		}
		return hasCanonicalRecordFact(table.recordProperties(type).facts, property) ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
	case CanonicalTypeKind::Builtin:
		return node.builtin == CanonicalBuiltinKind::Void ? TypeTraitResult::success_false() : TypeTraitResult::success_true();
	case CanonicalTypeKind::Pointer:
	case CanonicalTypeKind::MemberObjectPointer:
	case CanonicalTypeKind::MemberFunctionPointer:
	case CanonicalTypeKind::Enum:
		return TypeTraitResult::success_true();
	case CanonicalTypeKind::Array:
	case CanonicalTypeKind::Function:
		return TypeTraitResult::success_false();
	default:
		return std::nullopt;
	}
}

namespace {

using StructInfoSet = FlashCpp::FlatHashSet<const StructTypeInfo*, std::hash<const StructTypeInfo*>, std::equal_to<const StructTypeInfo*>, 8>;
using TypeIndexSet = FlashCpp::FlatHashSet<TypeIndex, std::hash<TypeIndex>, std::equal_to<TypeIndex>, 8>;

struct CachedRecordFactsEntry {
	CanonicalRecordFacts facts = CanonicalRecordFacts::None;
	bool resolved = false;
};

std::optional<TypeIndex> resolvePseudoDestructorExpressionTypeIndex(const ExpressionNode& expr, const SymbolTable& symbols) {
	if (const auto* ctor_call = std::get_if<ConstructorCallNode>(&expr)) {
		return ctor_call->type_node().type_index();
	}
	if (const auto* init_list = std::get_if<InitializerListConstructionNode>(&expr)) {
		const ASTNode& target_type = init_list->target_type();
		if (target_type.is<TypeSpecifierNode>()) {
			return target_type.as<TypeSpecifierNode>().type_index();
		}
		return std::nullopt;
	}
	if (const auto* obj_id = std::get_if<IdentifierNode>(&expr)) {
		auto symbol = symbols.lookup(obj_id->name());
		if (symbol.has_value()) {
			const DeclarationNode* decl = get_decl_from_symbol(*symbol);
			if (decl) {
				return decl->type_specifier_node().type_index();
			}
		}
		return std::nullopt;
	}
	if (const auto* member_access = std::get_if<MemberAccessNode>(&expr)) {
		std::optional<TypeIndex> object_type_index =
			resolvePseudoDestructorObjectTypeIndex(member_access->object(), symbols);
		if (!object_type_index.has_value()) {
			return std::nullopt;
		}
		const StructTypeInfo* object_struct = structInfoFromTypeIndex(*object_type_index);
		if (!object_struct) {
			return std::nullopt;
		}
		std::optional<StructMember> member =
			object_struct->findMemberRecursive(member_access->member_token().handle());
		if (member.has_value()) {
			return member->type_index;
		}
		return std::nullopt;
	}
	if (const auto* subscript = std::get_if<ArraySubscriptNode>(&expr)) {
		return resolvePseudoDestructorObjectTypeIndex(subscript->array_expr(), symbols);
	}
	if (const auto* unary = std::get_if<UnaryOperatorNode>(&expr)) {
		// (*p).~T(): the dereference target is the pointee type.
		if (unary->op() == "*" && unary->get_operand().is<ExpressionNode>()) {
			return resolvePseudoDestructorExpressionTypeIndex(unary->get_operand().as<ExpressionNode>(), symbols);
		}
		return std::nullopt;
	}
	if (const auto* call_expr = std::get_if<CallExprNode>(&expr)) {
		return call_expr->callee().declaration().type_specifier_node().type_index();
	}
	if (const auto* cast = std::get_if<StaticCastNode>(&expr)) {
		return cast->target_type().type_index();
	}
	if (const auto* cast = std::get_if<DynamicCastNode>(&expr)) {
		return cast->target_type().type_index();
	}
	if (const auto* cast = std::get_if<ConstCastNode>(&expr)) {
		return cast->target_type().type_index();
	}
	if (const auto* cast = std::get_if<ReinterpretCastNode>(&expr)) {
		return cast->target_type().type_index();
	}
	return std::nullopt;
}

std::optional<TypeIndex> resolvePseudoDestructorObjectTypeIndex(const ASTNode& object, const SymbolTable& symbols) {
	if (object.is<ConstructorCallNode>()) {
		return object.as<ConstructorCallNode>().type_node().type_index();
	}
	if (object.is<InitializerListConstructionNode>()) {
		const ASTNode& target_type = object.as<InitializerListConstructionNode>().target_type();
		if (target_type.is<TypeSpecifierNode>()) {
			return target_type.as<TypeSpecifierNode>().type_index();
		}
		return std::nullopt;
	}
	if (object.is<ExpressionNode>()) {
		return resolvePseudoDestructorExpressionTypeIndex(object.as<ExpressionNode>(), symbols);
	}
	return std::nullopt;
}

const StructTypeInfo* resolvePseudoDestructorObjectStruct(const ASTNode& object, const SymbolTable& symbols) {
	std::optional<TypeIndex> type_index = resolvePseudoDestructorObjectTypeIndex(object, symbols);
	if (type_index.has_value()) {
		return structInfoFromTypeIndex(*type_index);
	}
	return nullptr;
}

bool hasVirtualBaseClass(const StructTypeInfo& struct_info) {
	for (const BaseClassSpecifier& base : struct_info.base_classes) {
		if (base.is_virtual) {
			return true;
		}
	}
	return false;
}

// A destructor is trivial when it is not user-provided and is not virtual;
// access and deletion are irrelevant to triviality ([class.dtor]). Subobject
// triviality is checked by walking the record graph.
bool hasTrivialDestructorDeclaration(const StructTypeInfo* struct_info) {
	if (!struct_info) {
		return false;
	}
	if (const StructMemberFunction* destructor = struct_info->findDestructor()) {
		return !destructor->is_virtual &&
			destructor->function_decl.is<DestructorDeclarationNode>() &&
			destructor->function_decl.as<DestructorDeclarationNode>().was_defaulted_on_first_declaration();
	}
	return true;
}

bool isNontrivialDeclaredSpecialMember(const StructMemberFunction* function) {
	if (function == nullptr) {
		return false;
	}
	if (function->is_constructor) {
		if (!function->function_decl.is<ConstructorDeclarationNode>()) {
			return true;
		}
		const ConstructorDeclarationNode& declaration = function->function_decl.as<ConstructorDeclarationNode>();
		return !declaration.is_explicitly_defaulted();
	}
	if (!function->function_decl.is<FunctionDeclarationNode>()) {
		return true;
	}
	const FunctionDeclarationNode& declaration = function->function_decl.as<FunctionDeclarationNode>();
	return !declaration.is_explicitly_defaulted() && !declaration.is_deleted();
}

bool hasTrivialSpecialMemberSetForCopying(const StructTypeInfo* struct_info) {
	if (!struct_info || struct_info->has_vtable || hasVirtualBaseClass(*struct_info)) {
		return false;
	}
	if (isNontrivialDeclaredSpecialMember(struct_info->findCopyConstructor()) ||
		isNontrivialDeclaredSpecialMember(struct_info->findMoveConstructor()) ||
		isNontrivialDeclaredSpecialMember(struct_info->findCopyAssignmentOperator()) ||
		isNontrivialDeclaredSpecialMember(struct_info->findMoveAssignmentOperator())) {
		return false;
	}
	return hasTrivialDestructorDeclaration(struct_info);
}

bool hasTrivialDefaultConstructor(const StructTypeInfo* struct_info) {
	if (struct_info == nullptr || hasVirtualBaseClass(*struct_info) ||
		struct_info->hasDefaultMemberInitializers() ||
		struct_info->isDefaultConstructorDeleted()) {
		return false;
	}
	if (const StructMemberFunction* default_constructor =
			struct_info->findDefaultConstructor()) {
		if (!default_constructor->function_decl.is<ConstructorDeclarationNode>()) {
			throw InternalError("record property: default constructor has invalid AST node");
		}
		const ConstructorDeclarationNode& constructor =
			default_constructor->function_decl.as<ConstructorDeclarationNode>();
		if (constructor.was_defaulted_on_first_declaration()) {
			return true;
		}
		if (constructor.is_explicitly_defaulted()) {
			return false;
		}
		if (constructor.is_implicit()) {
			return !struct_info->hasUserDeclaredConstructor();
		}
		return false;
	}
	// A class with a user-declared constructor has no implicit default
	// constructor. This includes a deleted copy/move constructor, which is
	// recorded in StructTypeInfo even though it has no callable AST entry.
	return !struct_info->hasUserDeclaredConstructor();
}

// A default member initializer that value- or default-constructs its member
// runs that class's default constructor. Detected structurally so the nothrow
// walk can include it; other initializer expressions need expression-level
// noexcept evaluation and stay deferred.
bool defaultInitializerIsDefaultConstruction(const ASTNode& initializer) {
	if (initializer.is<InitializerListNode>()) {
		return initializer.as<InitializerListNode>().initializers().empty();
	}
	if (initializer.is<ExpressionNode>()) {
		const ExpressionNode& expression = initializer.as<ExpressionNode>();
		if (const auto* constructor_call =
				std::get_if<ConstructorCallNode>(&expression)) {
			if (const InitializerListNode* initializer_list = constructor_call->aggregateInitializer()) {
				return initializer_list->initializers().empty();
			}
			return constructor_call->arguments().empty();
		}
	}
	return false;
}

// The constructor selected by a class-type default member initializer. The
// parser records the selection for a non-default initializer so the nothrow
// walk can read the constructor's exception specification; an initializer whose
// selection the parser could not resolve returns null and stays deferred.
const ConstructorDeclarationNode* defaultInitializerResolvedConstructor(const ASTNode& initializer) {
	if (const auto* init_list = initializer.get_if<InitializerListNode>()) {
		return init_list->resolved_constructor();
	}
	if (const auto* expression = initializer.get_if<ExpressionNode>()) {
		if (const auto* constructor_call = std::get_if<ConstructorCallNode>(expression)) {
			return constructor_call->resolved_constructor();
		}
	}
	return nullptr;
}

// Whether the copy or move construction of a class, following implicitly-defined
// special members, can throw. A user-provided subobject constructor states its
// own exception specification; an implicit or defaulted one is followed
// recursively. Uses an explicit worklist so subobject depth stays off the native
// stack.
bool recordNothrowCopyOrMoveConstruction(const StructTypeInfo& root, bool prefer_move) {
	TemplateVector<const StructTypeInfo*, 8> pending{&root};
	TemplateVector<const StructTypeInfo*, 8> visited;
	while (!pending.empty()) {
		const StructTypeInfo* info = pending.back();
		pending.pop_back();
		if (info == nullptr) {
			return false;
		}
		if (std::find(visited.begin(), visited.end(), info) != visited.end()) {
			continue;
		}
		visited.push_back(info);
		const StructMemberFunction* constructor = info->findPreferredSameTypeConstructor(prefer_move, true);
		if (constructor != nullptr && constructor->access != AccessSpecifier::Public) {
			return false;
		}
		if (constructor != nullptr && constructor->function_decl.is<ConstructorDeclarationNode>()) {
			const ConstructorDeclarationNode& declaration = constructor->function_decl.as<ConstructorDeclarationNode>();
			if (!declaration.is_implicit() && !declaration.is_explicitly_defaulted()) {
				if (!declaration.is_noexcept()) {
					return false;
				}
				continue;
			}
		}
		for (const BaseClassSpecifier& base : info->base_classes) {
			if (base.is_deferred) {
				continue;
			}
			pending.push_back(structInfoFromTypeIndex(base.type_index));
		}
		for (const StructMember& member : info->members) {
			if (member.pointer_depth > 0 || member.is_reference() || !is_struct_type(member.type_index.category())) {
				continue;
			}
			pending.push_back(structInfoFromTypeIndex(member.type_index));
		}
	}
	return true;
}

// Whether a resolved default member initializer selects a potentially-throwing
// constructor. A user-provided constructor states its own exception
// specification; an implicit or defaulted one derives it from the member type's
// copy or move construction. An unresolved selection returns nullopt so the walk
// defers rather than guesses.
std::optional<bool> defaultInitializerConstructorThrows(const ASTNode& initializer, const StructTypeInfo& member_type) {
	const ConstructorDeclarationNode* selected = defaultInitializerResolvedConstructor(initializer);
	if (selected == nullptr) {
		return std::nullopt;
	}
	if (selected->is_implicit() || selected->is_explicitly_defaulted()) {
		return !recordNothrowCopyOrMoveConstruction(member_type, true);
	}
	return !selected->is_noexcept();
}

// Whether any element leaf of an array member's default member initializer
// selects a potentially-throwing constructor. An element leaf that
// default-constructs the element type (a prvalue `T{}` or an empty brace `{}`)
// carries no recorded constructor selection; `schedule_default_construction`
// hands its element type back to the caller's worklist so that type's own
// default construction is classified. Nested brace lists (rows of a
// multidimensional array) are flattened to their leaves with an explicit
// worklist so nesting depth stays off the native stack.
template <typename ScheduleDefaultConstruction>
bool defaultInitializerArrayElementThrows(
	const ASTNode& initializer,
	const StructTypeInfo& element_type,
	ScheduleDefaultConstruction schedule_default_construction) {
	TemplateVector<ASTNode, 4> pending;
	pending.push_back(initializer);
	while (!pending.empty()) {
		const ASTNode element = pending.back();
		pending.pop_back();
		if (defaultInitializerIsDefaultConstruction(element)) {
			if (!schedule_default_construction(element_type)) {
				return true;
			}
			continue;
		}
		if (const auto* nested = element.get_if<InitializerListNode>()) {
			std::ranges::copy(nested->initializers(), std::back_inserter(pending));
			continue;
		}
		const std::optional<bool> throws = defaultInitializerConstructorThrows(element, element_type);
		if (throws.has_value() && *throws) {
			return true;
		}
	}
	return false;
}

// Published canonical facts are immutable for a completed class, so a caller
// that can reach the canonical table may use them to stop at an already
// classified direct subobject instead of walking its whole ancestry again.
struct PublishedRecordFactsLookup {
	CanonicalTypeTable& table;
	mutable FlashCpp::FlatHashTable<TypeIndex, CachedRecordFactsEntry, std::hash<TypeIndex>, std::equal_to<TypeIndex>, 8> cache;

	explicit PublishedRecordFactsLookup(CanonicalTypeTable& table_reference) : table(table_reference) {}
	PublishedRecordFactsLookup(const PublishedRecordFactsLookup&) = delete;
	PublishedRecordFactsLookup& operator=(const PublishedRecordFactsLookup&) = delete;

	std::optional<CanonicalRecordFacts> forType(TypeIndex type_index) const {
		std::optional<CanonicalRecordFacts> cached_facts;
		const bool cache_hit = cache.forEachValue(type_index, [&](const CachedRecordFactsEntry& entry) {
			if (entry.resolved) {
				cached_facts = entry.facts;
			}
			return true;
		});
		if (cache_hit) {
			return cached_facts;
		}
		std::optional<CanonicalRecordFacts> result;
		const TypeInfo* type_info = tryGetTypeInfo(type_index);
		if (type_info != nullptr) {
			const StructTypeInfo* struct_info = type_info->getStructInfo();
			if (struct_info != nullptr && struct_info->layout_is_complete && !struct_info->has_deferred_base_classes) {
				const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *type_info);
				if (imported.status == CanonicalTypeImportStatus::Supported && table.hasRecordProperties(imported.type)) {
					result = table.recordProperties(imported.type).facts;
				}
			}
		}
		cache.insert(type_index, CachedRecordFactsEntry{result.value_or(CanonicalRecordFacts::None), result.has_value()});
		return result;
	}
};

struct NoPublishedRecordFacts {
	std::optional<CanonicalRecordFacts> forType(TypeIndex) const {
		return std::nullopt;
	}
};

struct DefaultConstructionPredicate {
	bool operator()(const StructTypeInfo&, const StructMemberFunction*) const {
		return true;
	}
};

struct TrivialDefaultConstructionPredicate {
	bool operator()(const StructTypeInfo& info, const StructMemberFunction*) const {
		return hasTrivialDefaultConstructor(&info);
	}
};

struct NothrowDefaultConstructionPredicate {
	bool operator()(const StructTypeInfo& info, const StructMemberFunction* default_ctor) const {
		if (hasTrivialDefaultConstructor(&info)) {
			return true;
		}
		if (default_ctor != nullptr && default_ctor->function_decl.is<ConstructorDeclarationNode>()) {
			const ConstructorDeclarationNode& constructor = default_ctor->function_decl.as<ConstructorDeclarationNode>();
			if (!constructor.is_implicit() && !constructor.is_explicitly_defaulted()) {
				return constructor.is_noexcept();
			}
		}
		return true;
	}
};

// A class whose default constructor is implicit or explicitly defaulted
// inherits its default-construction property from every base class and member
// that constructor initializes. A user-provided default constructor initializes
// its own subobjects, so recursion stops there. Uses an explicit worklist so
// subobject depth does not grow the native stack. `class_property` decides the
// property for one class; the walk owns the shared preconditions (abstract,
// deleted, inaccessible, reference member).
template <typename ClassProperty, typename FactsLookup>
bool recordSubobjectsSatisfyDefaultConstruction(
	const StructTypeInfo& root,
	ClassProperty class_property,
	bool recurse_default_initialized_members,
	const FactsLookup& facts_lookup,
	CanonicalRecordFacts cached_fact) {
	std::vector<const StructTypeInfo*> pending{&root};
	StructInfoSet visited;
	while (!pending.empty()) {
		const StructTypeInfo* info = pending.back();
		pending.pop_back();
		if (info == nullptr) {
			return false;
		}
		if (!visited.insert(info)) {
			continue;
		}
		if (info->is_abstract || info->isDefaultConstructorDeleted()) {
			return false;
		}
		const StructMemberFunction* default_ctor = info->findDefaultConstructor();
		if (default_ctor != nullptr) {
			if (default_ctor->access != AccessSpecifier::Public) {
				return false;
			}
		} else if (info->hasUserDeclaredConstructor()) {
			return false;
		}
		if (!class_property(*info, default_ctor)) {
			return false;
		}
		bool recurse = true;
		if (default_ctor != nullptr &&
			default_ctor->function_decl.is<ConstructorDeclarationNode>()) {
			const ConstructorDeclarationNode& constructor =
				default_ctor->function_decl.as<ConstructorDeclarationNode>();
			if (!constructor.is_implicit() &&
				!constructor.is_explicitly_defaulted()) {
				recurse = false;  // user-provided initializes its own subobjects
			}
		}
		if (!recurse) {
			continue;
		}
		auto schedule_type = [&](TypeIndex type_index) {
			if (const std::optional<CanonicalRecordFacts> facts = facts_lookup.forType(type_index); facts.has_value()) {
				return hasCanonicalRecordFact(*facts, cached_fact);
			}
			pending.push_back(structInfoFromTypeIndex(type_index));
			return true;
		};
		for (const BaseClassSpecifier& base : info->base_classes) {
			if (base.is_deferred) {
				continue;
			}
			if (!schedule_type(base.type_index)) {
				return false;
			}
		}
		for (const StructMember& member : info->members) {
			if (!info->isPotentiallyConstructedByDefaultConstructor(member)) {
				continue;
			}
			if (member.default_initializer.has_value()) {
				// A default member initializer of a class type that default
				// constructs the member still runs that class's default
				// constructor, so the nothrow answer must include it. A
				// non-default initializer selects another constructor, whose
				// exception specification the parser records on the initializer
				// or, for an implicit or defaulted one, derives from the member
				// type's copy or move construction; an unresolved selection
				// stays deferred.
				if (recurse_default_initialized_members &&
					member.pointer_depth == 0 &&
					is_struct_type(member.type_index.category())) {
					const ASTNode& initializer = *member.default_initializer;
					if (defaultInitializerIsDefaultConstruction(initializer)) {
						if (!schedule_type(member.type_index)) {
							return false;
						}
					} else if (const StructTypeInfo* member_struct_info = structInfoFromTypeIndex(member.type_index)) {
						if (member.is_array) {
							// Each element leaf either default-constructs the
							// element type or selects a constructor. A
							// default-constructing leaf recurses through the
							// element type's own default construction; a
							// selecting leaf contributes its exception spec.
							const bool element_throws = defaultInitializerArrayElementThrows(
								initializer, *member_struct_info,
								[&](const StructTypeInfo& element_type) {
									if (element_type.own_type_index_.has_value()) {
										return schedule_type(*element_type.own_type_index_);
									}
									pending.push_back(&element_type);
									return true;
								});
							if (element_throws) {
								return false;
							}
						} else {
							const std::optional<bool> throws = defaultInitializerConstructorThrows(initializer, *member_struct_info);
							if (throws.has_value() && *throws) {
								return false;
							}
						}
					}
				}
				continue;
			}
			if (member.is_reference()) {
				return false;  // reference member without an initializer
			}
			if (member.pointer_depth > 0 ||
				!is_struct_type(member.type_index.category())) {
				continue;
			}
			if (!schedule_type(member.type_index)) {
				return false;
			}
		}
	}
	return true;
}

bool recordDefaultConstructible(const StructTypeInfo& root) {
	return recordSubobjectsSatisfyDefaultConstruction(
		root,
		DefaultConstructionPredicate{},
		false,
		NoPublishedRecordFacts{},
		CanonicalRecordFacts::None);
}

bool recordTriviallyConstructible(const StructTypeInfo& root) {
	return recordSubobjectsSatisfyDefaultConstruction(
		root,
		TrivialDefaultConstructionPredicate{},
		false,
		NoPublishedRecordFacts{},
		CanonicalRecordFacts::None);
}

bool recordNothrowConstructible(const StructTypeInfo& root) {
	return recordSubobjectsSatisfyDefaultConstruction(
		root,
		NothrowDefaultConstructionPredicate{},
		true,
		NoPublishedRecordFacts{},
		CanonicalRecordFacts::None);
}

template<typename Pred>
bool allRecordSubobjectsSatisfy(const StructTypeInfo* struct_info, Pred pred) {
	if (!struct_info)
		return false;
	std::vector<const StructTypeInfo*> pending{struct_info};
	StructInfoSet visited;
	while (!pending.empty()) {
		const StructTypeInfo* current = pending.back();
		pending.pop_back();
		if (!current)
			return false;
		if (!visited.insert(current))
			continue;
		if (!pred(current))
			return false;

		for (const StructMember& member : current->members) {
			if (member.pointer_depth > 0 || member.is_reference() ||
				!is_struct_type(member.type_index.category())) {
				continue;
			}
			const TypeInfo* member_type = tryGetTypeInfo(member.type_index);
			const StructTypeInfo* member_struct = member_type ? member_type->getStructInfo() : nullptr;
			if (!member_struct)
				return false;
			pending.push_back(member_struct);
		}
		for (const BaseClassSpecifier& base : current->base_classes) {
			if (base.is_deferred)
				continue;
			const TypeInfo* base_type = tryGetTypeInfo(base.type_index);
			const StructTypeInfo* base_struct = base_type ? base_type->getStructInfo() : nullptr;
			if (!base_struct)
				return false;
			pending.push_back(base_struct);
		}
	}
	return true;
}

bool isStructHasTrivialDestructorImpl(const StructTypeInfo* struct_info) {
	return allRecordSubobjectsSatisfy(struct_info, [](const StructTypeInfo* current) { return hasTrivialDestructorDeclaration(current); });
}

// C++20 class-property walks use an explicit worklist because record nesting
// comes from source input and is not bounded by the native stack. Pointer and
// reference members are scalar indirections and do not add record edges.
bool isStructTriviallyCopyableImpl(const StructTypeInfo* struct_info) {
	return allRecordSubobjectsSatisfy(
		struct_info,
		[](const StructTypeInfo* current) {
			return hasTrivialSpecialMemberSetForCopying(current);
		});
}

// A trivial class is trivially copyable and has no user-declared constructor.
// Apply both checks to the complete record graph in the same iterative walk.
bool isStructTrivialImpl(const StructTypeInfo* struct_info) {
	return allRecordSubobjectsSatisfy(
		struct_info,
		[](const StructTypeInfo* current) {
			return hasTrivialSpecialMemberSetForCopying(current) &&
				hasTrivialDefaultConstructor(current);
		});
}

template <typename FactsLookup>
bool isStructEmptyImpl(const FactsLookup& facts_lookup, const StructTypeInfo* struct_info) {
	if (struct_info == nullptr) {
		return false;
	}
	std::vector<const StructTypeInfo*> pending{struct_info};
	StructInfoSet visited;
	while (!pending.empty()) {
		const StructTypeInfo* current = pending.back();
		pending.pop_back();
		if (current == nullptr || !visited.insert(current)) {
			continue;
		}
		bool has_nonzero_size_data_member = false;
		for (const StructMember& member : current->members) {
			if (StructTypeInfo::isZeroWidthBitfield(member.bitfield_width)) {
				continue;
			}
			if (member.is_no_unique_address && !member.is_array && member.pointer_depth == 0 && !member.is_reference()) {
				if (const std::optional<CanonicalRecordFacts> facts = facts_lookup.forType(member.type_index); facts.has_value()) {
					if (!hasCanonicalRecordFact(*facts, CanonicalRecordFacts::Empty)) {
						has_nonzero_size_data_member = true;
						break;
					}
					continue;
				}
				const TypeInfo* member_type = tryGetTypeInfo(member.type_index);
				const StructTypeInfo* member_struct = member_type == nullptr ? nullptr : member_type->getStructInfo();
				if (member_struct != nullptr) {
					pending.push_back(member_struct);
					continue;
				}
			}
			has_nonzero_size_data_member = true;
			break;
		}
		if (current->is_union || has_nonzero_size_data_member || current->has_vtable) {
			return false;
		}
		for (const BaseClassSpecifier& base : current->base_classes) {
			if (base.is_deferred || base.is_virtual) {
				return false;
			}
			if (const std::optional<CanonicalRecordFacts> facts = facts_lookup.forType(base.type_index); facts.has_value()) {
				if (!hasCanonicalRecordFact(*facts, CanonicalRecordFacts::Empty)) {
					return false;
				}
				continue;
			}
			const TypeInfo* base_type = tryGetTypeInfo(base.type_index);
			const StructTypeInfo* base_struct = base_type == nullptr ? nullptr : base_type->getStructInfo();
			if (base_struct == nullptr) {
				return false;
			}
			pending.push_back(base_struct);
		}
	}
	return true;
}

bool isStructEmptyImpl(const StructTypeInfo* struct_info) {
	return isStructEmptyImpl(NoPublishedRecordFacts{}, struct_info);
}

bool isStructDestructibleImpl(const StructTypeInfo* struct_info) {
	if (struct_info == nullptr || struct_info->has_deleted_destructor) {
		return false;
	}
	const StructMemberFunction* destructor = struct_info->findDestructor();
	return destructor == nullptr || destructor->access == AccessSpecifier::Public;
}

bool isStructTriviallyDestructibleImpl(const StructTypeInfo* struct_info) {
	if (!struct_info)
		return false;

	std::vector<const StructTypeInfo*> pending{struct_info};
	StructInfoSet visited;
	while (!pending.empty()) {
		const StructTypeInfo* current = pending.back();
		pending.pop_back();
		if (!current || !visited.insert(current)) {
			continue;
		}
		if (current->has_deleted_destructor)
			return false;
		if (const StructMemberFunction* destructor = current->findDestructor()) {
			if (current == struct_info &&
				destructor->access != AccessSpecifier::Public) {
				return false;
			}
			if (destructor->is_virtual ||
				!destructor->function_decl.is<DestructorDeclarationNode>() ||
				!destructor->function_decl.as<DestructorDeclarationNode>().was_defaulted_on_first_declaration()) {
				return false;
			}
		}

		for (const BaseClassSpecifier& base : current->base_classes) {
			if (base.is_deferred)
				return false;
			const TypeInfo* base_type = tryGetTypeInfo(base.type_index);
			const StructTypeInfo* base_struct = base_type ? base_type->getStructInfo() : nullptr;
			if (!base_struct)
				return false;
			pending.push_back(base_struct);
		}
		for (const StructMember& member : current->members) {
			if (member.pointer_depth > 0 || member.is_reference() ||
				!is_struct_type(member.type_index.category())) {
				continue;
			}
			const TypeInfo* member_type = tryGetTypeInfo(member.type_index);
			const StructTypeInfo* member_struct = member_type ? member_type->getStructInfo() : nullptr;
			if (!member_struct)
				return false;
			pending.push_back(member_struct);
		}
	}
	return true;
}

bool hasVirtualDestructorImpl(const StructTypeInfo* struct_info) {
	if (!struct_info)
		return false;

	std::vector<const StructTypeInfo*> pending{struct_info};
	StructInfoSet visited;
	while (!pending.empty()) {
		const StructTypeInfo* current = pending.back();
		pending.pop_back();
		if (!current || !visited.insert(current))
			continue;
		if (const StructMemberFunction* destructor = current->findDestructor();
			destructor && destructor->is_virtual) {
			return true;
		}
		for (const BaseClassSpecifier& base : current->base_classes) {
			if (base.is_deferred)
				continue;
			const TypeInfo* base_type = tryGetTypeInfo(base.type_index);
			if (const StructTypeInfo* base_struct = base_type ? base_type->getStructInfo() : nullptr) {
				pending.push_back(base_struct);
			}
		}
	}
	return false;
}

bool isStructNothrowDestructibleImpl(const StructTypeInfo* struct_info) {
	if (struct_info == nullptr) {
		return true;
	}
	std::vector<const StructTypeInfo*> pending{struct_info};
	StructInfoSet visited;
	while (!pending.empty()) {
		const StructTypeInfo* current = pending.back();
		pending.pop_back();
		if (current == nullptr || !visited.insert(current)) {
			continue;
		}
		if (current->has_deleted_destructor) {
			return false;
		}
		const StructMemberFunction* destructor = current->findDestructor();
		if (destructor != nullptr) {
			if (current == struct_info &&
				destructor->access != AccessSpecifier::Public) {
				return false;
			}
			if (!destructor->function_decl.is<DestructorDeclarationNode>()) {
				throw InternalError("record property: destructor has invalid AST node");
			}
			const DestructorDeclarationNode& declaration =
				destructor->function_decl.as<DestructorDeclarationNode>();
			if (declaration.has_noexcept_specifier()) {
				if (!declaration.is_noexcept()) {
					return false;
				}
				continue;
			}
		}

		for (const BaseClassSpecifier& base : current->base_classes) {
			if (base.is_deferred) {
				return false;
			}
			const TypeInfo* base_type = tryGetTypeInfo(base.type_index);
			const StructTypeInfo* base_struct =
				base_type == nullptr ? nullptr : base_type->getStructInfo();
			if (base_struct == nullptr) {
				return false;
			}
			pending.push_back(base_struct);
		}
		for (const StructMember& member : current->members) {
			if (member.pointer_depth > 0 || member.is_reference() ||
				!is_struct_type(member.type_index.category())) {
				continue;
			}
			const TypeInfo* member_type = tryGetTypeInfo(member.type_index);
			const StructTypeInfo* member_struct =
				member_type == nullptr ? nullptr : member_type->getStructInfo();
			if (member_struct == nullptr) {
				return false;
			}
			pending.push_back(member_struct);
		}
	}
	return true;
}

bool hasBaseTypeAmongZeroOffsetMembers(
	const StructTypeInfo* struct_info,
	const TypeIndexSet& base_subobject_types) {
	std::vector<const StructTypeInfo*> pending{struct_info};
	StructInfoSet visited;
	std::vector<TypeIndex> zero_offset_member_types;
	while (!pending.empty()) {
		const StructTypeInfo* current = pending.back();
		pending.pop_back();
		if (!current || !visited.insert(current))
			continue;

		for (size_t index = 0; index < current->members.size(); ++index) {
			const StructMember& member = current->members[index];
			if (!current->is_union && index != 0 && !member.is_no_unique_address)
				continue;
			if (member.pointer_depth > 0 || member.is_reference() ||
				!is_struct_type(member.type_index.category())) {
				continue;
			}

			zero_offset_member_types.push_back(member.type_index);
			const TypeInfo* member_type = tryGetTypeInfo(member.type_index);
			const StructTypeInfo* member_struct = member_type ? member_type->getStructInfo() : nullptr;
			if (!member_struct)
				return true;
			pending.push_back(member_struct);
		}
	}
	for (TypeIndex member_type : zero_offset_member_types) {
		if (base_subobject_types.contains(member_type))
			return true;
	}
	return false;
}

bool isStructStandardLayoutImpl(const StructTypeInfo* struct_info) {
	if (!struct_info)
		return false;

	std::vector<const StructTypeInfo*> pending_roots{struct_info};
	StructInfoSet checked_roots;
	while (!pending_roots.empty()) {
		const StructTypeInfo* root = pending_roots.back();
		pending_roots.pop_back();
		if (!root || !checked_roots.insert(root))
			continue;

		std::vector<const StructTypeInfo*> inheritance_worklist{root};
		TypeIndexSet base_subobject_types;
		const StructTypeInfo* data_member_owner = nullptr;
		std::optional<AccessSpecifier> data_member_access;
		while (!inheritance_worklist.empty()) {
			const StructTypeInfo* current = inheritance_worklist.back();
			inheritance_worklist.pop_back();
			if (!current || current->has_vtable)
				return false;
			if (!current->members.empty()) {
				if (data_member_owner && data_member_owner != current)
					return false;
				data_member_owner = current;
			}

			for (const StructMember& member : current->members) {
				if (member.is_reference())
					return false;
				if (data_member_access.has_value() &&
					*data_member_access != member.access) {
					return false;
				}
				data_member_access = member.access;
				if (member.pointer_depth > 0 ||
					!is_struct_type(member.type_index.category())) {
					continue;
				}
				const TypeInfo* member_type = tryGetTypeInfo(member.type_index);
				const StructTypeInfo* member_struct = member_type ? member_type->getStructInfo() : nullptr;
				if (!member_struct)
					return false;
				pending_roots.push_back(member_struct);
			}

			for (const BaseClassSpecifier& base : current->base_classes) {
				if (base.is_deferred || base.is_virtual || !base_subobject_types.insert(base.type_index)) {
					return false;
				}
				const TypeInfo* base_type = tryGetTypeInfo(base.type_index);
				const StructTypeInfo* base_struct = base_type ? base_type->getStructInfo() : nullptr;
				if (!base_struct)
					return false;
				inheritance_worklist.push_back(base_struct);
			}
		}
		if (hasBaseTypeAmongZeroOffsetMembers(root, base_subobject_types))
			return false;
	}
	return true;
}

template <typename FactsLookup>
bool isStructStandardLayoutWithPublishedFacts(
	const FactsLookup& facts_lookup,
	const StructTypeInfo* struct_info) {
	if (struct_info != nullptr && !struct_info->has_vtable && !struct_info->is_union &&
		struct_info->members.empty() && struct_info->base_classes.size() == 1) {
		const BaseClassSpecifier& base = struct_info->base_classes.front();
		if (!base.is_deferred && !base.is_virtual) {
			if (const std::optional<CanonicalRecordFacts> facts = facts_lookup.forType(base.type_index); facts.has_value()) {
				return hasCanonicalRecordFact(*facts, CanonicalRecordFacts::StandardLayout);
			}
		}
	}
	return isStructStandardLayoutImpl(struct_info);
}


} // namespace

bool typeTraitHasDependentOperands(const TypeTraitExprNode& trait_expr) {
	bool dependent = false;
	ExpressionStructure::visitExpressionChildren(
		ExpressionNode(trait_expr),
		[&](ExpressionStructure::ExpressionChildRole, const ASTNode& node) {
			if (!node.is<TypeSpecifierNode>() ||
				isDependentTypeTraitOperand(node.as<TypeSpecifierNode>())) {
				dependent = true;
			}
		});
	return dependent;
}

bool isStructTriviallyCopyable(const StructTypeInfo* struct_info) {
	return isStructTriviallyCopyableImpl(struct_info);
}

bool isStructTrivial(const StructTypeInfo* struct_info) {
	return isStructTrivialImpl(struct_info);
}

bool isStructNothrowDestructible(const StructTypeInfo* struct_info) {
	return isStructNothrowDestructibleImpl(struct_info);
}

bool unionHasDeletedImplicitDestructor(const StructTypeInfo& struct_info) {
	if (const StructMemberFunction* destructor = struct_info.findDestructor();
		destructor != nullptr && destructor->function_decl.is<DestructorDeclarationNode>() &&
		!destructor->function_decl.as<DestructorDeclarationNode>().was_defaulted_on_first_declaration()) {
		return false;
	}
	for (const StructMember& member : struct_info.members) {
		if ((!struct_info.is_union && !member.anonymous_union_group_index.has_value()) ||
			StructTypeInfo::isZeroWidthBitfield(member.bitfield_width) ||
			member.pointer_depth > 0 || member.is_reference()) {
			continue;
		}
		const TypeInfo* member_type = tryGetTypeInfo(member.type_index);
		const StructTypeInfo* member_struct = member_type == nullptr ? nullptr : member_type->getStructInfo();
		if (member_struct != nullptr && !isStructTriviallyDestructibleImpl(member_struct)) {
			return true;
		}
	}
	return false;
}

template <typename FactsLookup>
CanonicalRecordFacts computeCanonicalRecordFactsWithLookup(const StructTypeInfo& struct_info, const FactsLookup& facts_lookup) {
	CanonicalRecordFacts facts = CanonicalRecordFacts::None;
	const bool is_standard_layout = isStructStandardLayoutWithPublishedFacts(facts_lookup, &struct_info);
	if (struct_info.has_deleted_destructor || (struct_info.is_union && unionHasDeletedImplicitDestructor(struct_info))) {
		facts |= CanonicalRecordFacts::DestructorDeleted;
	}
	if (isStructNothrowDestructibleImpl(&struct_info)) {
		facts |= CanonicalRecordFacts::NothrowDestructible;
	}
	if (hasTrivialSpecialMemberSetForCopying(&struct_info)) {
		facts |= CanonicalRecordFacts::TrivialCopyingSpecialMembers;
	}
	if (hasTrivialDefaultConstructor(&struct_info)) {
		facts |= CanonicalRecordFacts::TrivialDefaultConstructor;
	}
	if (isStructHasTrivialDestructorImpl(&struct_info)) {
		facts |= CanonicalRecordFacts::HasTrivialDestructor;
	}
	if (const StructMemberFunction* destructor = struct_info.findDestructor()) {
		if (destructor->access != AccessSpecifier::Public) {
			facts |= CanonicalRecordFacts::DestructorNonPublic;
		}
		if (destructor->is_virtual) {
			facts |= CanonicalRecordFacts::DestructorVirtual;
		}
		if (!destructor->function_decl.is<DestructorDeclarationNode>()) {
			throw InternalError("record property: destructor has invalid AST node");
		}
		const DestructorDeclarationNode& declaration = destructor->function_decl.as<DestructorDeclarationNode>();
		if (!declaration.was_defaulted_on_first_declaration()) {
			facts |= CanonicalRecordFacts::DestructorNonTrivial;
		}
		if (declaration.has_noexcept_specifier()) {
			facts |= CanonicalRecordFacts::DestructorHasNoexceptSpecifier;
			if (declaration.is_noexcept()) {
				facts |= CanonicalRecordFacts::DestructorNoexcept;
			}
		}
	}
	if (struct_info.has_vtable) {
		facts |= CanonicalRecordFacts::Polymorphic;
	}
	if (struct_info.is_final) {
		facts |= CanonicalRecordFacts::Final;
	}
	if (struct_info.is_abstract) {
		facts |= CanonicalRecordFacts::Abstract;
	}
	if (is_standard_layout) {
		facts |= CanonicalRecordFacts::StandardLayout;
	}
	if (struct_info.isAggregate()) {
		facts |= CanonicalRecordFacts::Aggregate;
	}
	if (isStructEmptyImpl(facts_lookup, &struct_info)) {
		facts |= CanonicalRecordFacts::Empty;
	}
	if (recordSubobjectsSatisfyDefaultConstruction(
			struct_info,
			DefaultConstructionPredicate{},
			false,
			facts_lookup,
			CanonicalRecordFacts::DefaultConstructible)) {
		facts |= CanonicalRecordFacts::DefaultConstructible;
	}
	if (recordSubobjectsSatisfyDefaultConstruction(
			struct_info,
			TrivialDefaultConstructionPredicate{},
			false,
			facts_lookup,
			CanonicalRecordFacts::TriviallyDefaultConstructible)) {
		facts |= CanonicalRecordFacts::TriviallyDefaultConstructible;
	}
	if (recordSubobjectsSatisfyDefaultConstruction(
			struct_info,
			NothrowDefaultConstructionPredicate{},
			true,
			facts_lookup,
			CanonicalRecordFacts::NothrowDefaultConstructible)) {
		facts |= CanonicalRecordFacts::NothrowDefaultConstructible;
	}
	return facts;
}

CanonicalRecordFacts computeCanonicalRecordFacts(CanonicalTypeTable& table, const StructTypeInfo& struct_info) {
	return computeCanonicalRecordFactsWithLookup(struct_info, PublishedRecordFactsLookup{table});
}

bool isPseudoDestructorCallNoexcept(const PseudoDestructorCallNode& pseudo_dtor, const SymbolTable& symbols) {
	// Resolve the actual object type first so template specializations and
	// temporary objects do not depend on the destructor-name token.
	if (const StructTypeInfo* struct_info = resolvePseudoDestructorObjectStruct(pseudo_dtor.object(), symbols)) {
		return isStructNothrowDestructible(struct_info);
	}

	// Compatibility path for scalar pseudo-destructors and legacy non-template
	// object shapes where only the destructor type token is available.
	std::string_view type_name = pseudo_dtor.type_name();
	auto it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(type_name));
	if (it != getTypesByNameMap().end()) {
		const StructTypeInfo* struct_info = it->second->getStructInfo();
		if (struct_info) {
			return isStructNothrowDestructible(struct_info);
		}
	}
	return true;	 // Scalar types: pseudo-destructor is a no-op, always noexcept
}

TypeTraitResult evaluateTypeTrait(
	TypeTraitKind kind,
	TypeIndex type_idx,
	bool is_reference,
	bool is_rvalue_reference,
	bool is_lvalue_reference,
	size_t pointer_depth,
	CVQualifier cv_qualifier,
	bool is_array,
	std::optional<size_t> array_size,
	const StructTypeInfo* struct_info) {
	using namespace TypeTraitEval;
	const TypeCategory cat = type_idx.category();
	bool result = false;

	switch (kind) {
	case TypeTraitKind::IsConstantEvaluated:
			// In compile-time context, return true; in runtime context, return false
			// This is context-dependent, so caller should handle this case specially
		return TypeTraitResult::failure();

	case TypeTraitKind::IsCompleteOrUnbounded:
			// __is_complete_or_unbounded evaluates to true if either:
			// 1. T is a complete type, or
			// 2. T is an unbounded array type (e.g. int[])
			// It evaluates to false for:
			// - Incomplete class types
			// - void
			// - Bounded array types with incomplete element types

			// Check for void - always incomplete
		if (cat == TypeCategory::Void && pointer_depth == 0 && !is_reference) {
			return TypeTraitResult::success_false();
		}

			// Check for unbounded array - always returns true
		if (is_array && (!array_size.has_value() || *array_size == 0)) {
			return TypeTraitResult::success_true();
		}

			// Check for incomplete class/struct types
		if (is_struct_type(cat) &&
			(!struct_info || !struct_info->hasCompleteObjectLayout()) &&
			pointer_depth == 0 && !is_reference) {
			return TypeTraitResult::success_false();
		}

			// All other types are considered complete
		return TypeTraitResult::success_true();

	case TypeTraitKind::IsVoid:
		result = (cat == TypeCategory::Void && !is_reference && pointer_depth == 0);
		break;

	case TypeTraitKind::IsNullptr:
		result = (cat == TypeCategory::Nullptr && !is_reference && pointer_depth == 0);
		break;

	case TypeTraitKind::IsIntegral:
		result = isIntegral(cat) && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsFloatingPoint:
		result = isFloatingPoint(cat) && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsArray:
		result = is_array && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsPointer:
		result = (pointer_depth > 0) && !is_reference;
		break;

	case TypeTraitKind::IsLvalueReference:
		result = is_lvalue_reference || (is_reference && !is_rvalue_reference);
		break;

	case TypeTraitKind::IsRvalueReference:
		result = is_rvalue_reference;
		break;

	case TypeTraitKind::IsMemberObjectPointer:
		result = (cat == TypeCategory::MemberObjectPointer && !is_reference && pointer_depth == 0);
		break;

	case TypeTraitKind::IsMemberFunctionPointer:
		result = (cat == TypeCategory::MemberFunctionPointer && !is_reference && pointer_depth == 0);
		break;

	case TypeTraitKind::IsEnum:
		result = (cat == TypeCategory::Enum && !is_reference && pointer_depth == 0);
		break;

	case TypeTraitKind::IsUnion:
		result = struct_info && struct_info->is_union && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsClass:
		result = is_struct_type(cat) &&
				 struct_info && !struct_info->is_union && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsFunction:
		result = (cat == TypeCategory::Function && !is_reference && pointer_depth == 0);
		break;

	case TypeTraitKind::IsReference:
		result = is_reference || is_rvalue_reference;
		break;

	case TypeTraitKind::IsArithmetic:
		result = isArithmeticType(cat) && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsFundamental:
		result = isFundamentalType(cat) && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsObject:
		result = (cat != TypeCategory::Function) && (cat != TypeCategory::Void) && !is_reference && !is_rvalue_reference;
		break;

	case TypeTraitKind::IsScalar:
		result = isScalarType(cat, is_reference, pointer_depth);
		break;

	case TypeTraitKind::IsCompound:
		result = !(isFundamentalType(cat) && !is_reference && pointer_depth == 0);
		break;

	case TypeTraitKind::IsConst:
		result = (cv_qualifier == CVQualifier::Const || cv_qualifier == CVQualifier::ConstVolatile);
		break;

	case TypeTraitKind::IsVolatile:
		result = (cv_qualifier == CVQualifier::Volatile || cv_qualifier == CVQualifier::ConstVolatile);
		break;

	case TypeTraitKind::IsSigned:
		result = isSigned(cat) && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsUnsigned:
		result = isUnsigned(cat) && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsBoundedArray:
		result = is_array && array_size.has_value() && *array_size > 0 && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsUnboundedArray:
		result = is_array && (!array_size.has_value() || *array_size == 0) && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsPolymorphic:
		result = struct_info && struct_info->has_vtable && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsFinal:
		result = struct_info && struct_info->is_final && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsAbstract:
		result = struct_info && struct_info->is_abstract && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsEmpty:
		if (struct_info && !is_reference && pointer_depth == 0) {
			result = isStructEmptyImpl(struct_info);
		}
		break;

	case TypeTraitKind::IsAggregate:
		if (struct_info && !is_reference && pointer_depth == 0) {
			result = struct_info->isAggregate();
		} else if (is_array && !is_reference && pointer_depth == 0) {
			result = true;
		}
		break;

	case TypeTraitKind::IsStandardLayout:
		if (struct_info && !is_reference && pointer_depth == 0) {
			result = isStructStandardLayoutImpl(struct_info);
		} else if (isScalarType(cat, is_reference, pointer_depth)) {
			result = true;
		}
		break;

	case TypeTraitKind::HasUniqueObjectRepresentations:
		result = (isIntegral(cat) && cat != TypeCategory::Bool && !is_reference && pointer_depth == 0);
		break;

	case TypeTraitKind::IsTriviallyCopyable:
		if (isScalarType(cat, is_reference, pointer_depth)) {
			result = true;
		} else if (struct_info && !is_reference && pointer_depth == 0) {
			result = isStructTriviallyCopyable(struct_info);
		}
		break;

	case TypeTraitKind::IsTrivial:
		if (isScalarType(cat, is_reference, pointer_depth)) {
			result = true;
		} else if (struct_info && !is_reference && pointer_depth == 0) {
			result = isStructTrivial(struct_info);
		}
		break;

	case TypeTraitKind::IsPod:
		if (isScalarType(cat, is_reference, pointer_depth)) {
			result = true;
		} else if (struct_info && !is_reference && pointer_depth == 0) {
			result = isStructTrivial(struct_info) &&
				isStructStandardLayoutImpl(struct_info);
		}
		break;

	case TypeTraitKind::IsLiteralType:
		if (isScalarType(cat, is_reference, pointer_depth) || is_reference) {
			result = true;
		} else if (struct_info && pointer_depth == 0) {
			result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
		}
		break;

	case TypeTraitKind::IsDestructible:
		if (isScalarType(cat, is_reference, pointer_depth)) {
			result = true;
		} else if (struct_info && !is_reference && pointer_depth == 0) {
			result = isStructDestructibleImpl(struct_info);
		}
		break;

	case TypeTraitKind::IsTriviallyDestructible:
		if (isScalarType(cat, is_reference, pointer_depth)) {
			result = true;
		} else if (struct_info && !is_reference && pointer_depth == 0) {
			result = isStructTriviallyDestructibleImpl(struct_info);
		}
		break;

	case TypeTraitKind::HasTrivialDestructor:
		if (isScalarType(cat, is_reference, pointer_depth)) {
			result = true;
		} else if (struct_info && !is_reference && pointer_depth == 0) {
			result = isStructHasTrivialDestructorImpl(struct_info);
		}
		break;

	case TypeTraitKind::IsNothrowDestructible:
		if (isScalarType(cat, is_reference, pointer_depth)) {
			result = true;
		} else if (struct_info && !is_reference && pointer_depth == 0) {
			result = isStructNothrowDestructible(struct_info);
		}
		break;

	case TypeTraitKind::HasVirtualDestructor:
		if (struct_info && !is_reference && pointer_depth == 0) {
			result = hasVirtualDestructorImpl(struct_info);
		}
		break;

	case TypeTraitKind::IsConstructible:
	case TypeTraitKind::IsTriviallyConstructible:
	case TypeTraitKind::IsNothrowConstructible:
		// These need variadic type arguments, return failure for simple evaluation
		if (isScalarType(cat, is_reference, pointer_depth)) {
			result = true;  // Scalars are always default constructible
		} else if (struct_info && !struct_info->is_union && !is_reference && pointer_depth == 0) {
			if (kind == TypeTraitKind::IsConstructible) {
				result = !struct_info->hasUserDefinedConstructor() || struct_info->hasConstructor();
			} else {
				result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
			}
		}
		break;

		// Binary traits and variadic traits need special handling with second type
	case TypeTraitKind::IsBaseOf:
	case TypeTraitKind::IsSame:
	case TypeTraitKind::IsConvertible:
	case TypeTraitKind::IsNothrowConvertible:
	case TypeTraitKind::IsAssignable:
	case TypeTraitKind::IsTriviallyAssignable:
	case TypeTraitKind::IsNothrowAssignable:
	case TypeTraitKind::IsLayoutCompatible:
	case TypeTraitKind::IsPointerInterconvertibleBaseOf:
		// These need the second type argument, return failure
		return TypeTraitResult::failure();

	case TypeTraitKind::UnderlyingType:
		// This returns a type, not a bool, so handle specially
		return TypeTraitResult::failure();

	default:
		return TypeTraitResult::failure();
	}

	return TypeTraitResult{true, result};
}

// Convenience overload that takes a TypeSpecifierNode directly
// This extracts all the necessary fields and calls the main evaluateTypeTrait function
TypeTraitResult evaluateTypeTrait(
	TypeTraitKind kind,
	const TypeSpecifierNode& type_spec,
	const StructTypeInfo* struct_info) {
	if (const std::optional<TypeTraitResult> canonical_result =
			tryEvaluateCanonicalStructuralTrait(kind, type_spec);
		canonical_result.has_value()) {
		return *canonical_result;
	}
	return evaluateTypeTrait(
		kind,
		type_spec.type_index(),
		type_spec.is_reference(),
		type_spec.is_rvalue_reference(),
		type_spec.is_lvalue_reference(),
		type_spec.runtime_pointer_depth(),
		type_spec.cv_qualifier(),
		type_spec.is_array(),
		type_spec.array_size(),
		struct_info);
}

static TypeTraitResult evaluateAssignableTrait(
	TypeTraitKind kind,
	const TypeSpecifierNode& target_type,
	const TypeSpecifierNode& source_type) {
	if (target_type.is_const()) {
		return TypeTraitResult::success_false();
	}

	TypeSpecifierNode assigned_type = target_type;
	assigned_type.set_reference_qualifier(ReferenceQualifier::None);
	const bool scalar_assignment =
		target_type.is_lvalue_reference() &&
		TypeTraitEval::isScalarType(
			assigned_type.category(),
			false,
			assigned_type.runtime_pointer_depth()) &&
		(areSameTypeTraitOperands(assigned_type, source_type) ||
		 can_convert_type(source_type, assigned_type).is_valid);
	if (scalar_assignment) {
		return TypeTraitResult::success_true();
	}

	TypeSpecifierNode source_object_type = source_type;
	source_object_type.set_reference_qualifier(ReferenceQualifier::None);
	source_object_type.set_cv_qualifier(CVQualifier::None);
	const StructTypeInfo* assigned_struct =
		structInfoFromTypeIndex(assigned_type.type_index());
	if (!assigned_struct ||
		assigned_struct->is_union ||
		assigned_type.runtime_pointer_depth() != 0 ||
		!areSameTypeTraitOperands(assigned_type, source_object_type)) {
		return TypeTraitResult::success_false();
	}

	auto assignmentParameterMatchesSource =
		[&source_type](const TypeSpecifierNode& parameter_type) {
			if (parameter_type.type() != source_type.type()) {
				return false;
			}
			if (parameter_type.category() == TypeCategory::Struct &&
				parameter_type.type_index() != source_type.type_index()) {
				return false;
			}
			if (parameter_type.reference_qualifier() == source_type.reference_qualifier()) {
				return parameter_type.cv_qualifier() == source_type.cv_qualifier();
			}
			if (parameter_type.is_lvalue_reference()) {
				if (parameter_type.is_const()) {
					return true;
				}
				return source_type.is_lvalue_reference() && !source_type.is_const();
			}
			if (parameter_type.is_rvalue_reference()) {
				return !source_type.is_lvalue_reference() &&
					(!parameter_type.is_const() || source_type.is_const());
			}
			return false;
		};
	auto assignmentParameterExactlyMatchesSource =
		[&source_type](const TypeSpecifierNode& parameter_type) {
			if (parameter_type.type() != source_type.type()) {
				return false;
			}
			if (parameter_type.category() == TypeCategory::Struct &&
				parameter_type.type_index() != source_type.type_index()) {
				return false;
			}
			ReferenceQualifier source_reference_qualifier = source_type.reference_qualifier();
			if (source_reference_qualifier == ReferenceQualifier::None) {
				source_reference_qualifier = ReferenceQualifier::RValueReference;
			}
			return parameter_type.reference_qualifier() == source_reference_qualifier &&
				parameter_type.cv_qualifier() == source_type.cv_qualifier();
		};
	auto findSelectedAssignmentOperator = [&]() -> const StructMemberFunction* {
		auto isAssignmentCandidate = [](const StructMemberFunction& member_function) {
			return isAssignOperator(member_function.operator_kind) &&
				get_function_decl_node(member_function.function_decl) != nullptr;
		};
		auto parameterMatches = [&](const StructMemberFunction& member_function, bool require_exact) {
			if (!isAssignmentCandidate(member_function)) {
				return false;
			}
			const FunctionDeclarationNode* function_node =
				get_function_decl_node(member_function.function_decl);
			if (function_node->is_implicit()) {
				return false;
			}
			const auto& params = function_node->parameter_nodes();
			if (params.size() != 1 || !params[0].is<DeclarationNode>()) {
				return false;
			}
			const TypeSpecifierNode& parameter_type =
				params[0].as<DeclarationNode>().type_specifier_node();
			return require_exact
				? assignmentParameterExactlyMatchesSource(parameter_type)
				: assignmentParameterMatchesSource(parameter_type);
		};
		auto selected = std::ranges::find_if(
			assigned_struct->member_functions,
			[&](const StructMemberFunction& member_function) {
				return parameterMatches(member_function, true);
			});
		if (selected != assigned_struct->member_functions.end()) {
			return &*selected;
		}
		selected = std::ranges::find_if(
			assigned_struct->member_functions,
			[&](const StructMemberFunction& member_function) {
				return parameterMatches(member_function, false);
			});
		return selected == assigned_struct->member_functions.end()
			? nullptr
			: &*selected;
	};
	const StructMemberFunction* selected_assignment_operator =
		findSelectedAssignmentOperator();

	if (kind == TypeTraitKind::IsAssignable) {
		const bool has_user_assignment =
			assigned_struct->hasCopyAssignmentOperator() ||
			assigned_struct->hasMoveAssignmentOperator();
		return selected_assignment_operator ||
			(!has_user_assignment &&
			 !assigned_struct->isCopyAssignmentDeleted() &&
			 !assigned_struct->isMoveAssignmentDeleted())
			? TypeTraitResult::success_true()
			: TypeTraitResult::success_false();
	}

	if (kind == TypeTraitKind::IsNothrowAssignable) {
		const bool has_user_assignment =
			assigned_struct->hasCopyAssignmentOperator() ||
			assigned_struct->hasMoveAssignmentOperator();
		if (!has_user_assignment) {
			return (!assigned_struct->has_vtable &&
					!assigned_struct->isCopyAssignmentDeleted() &&
					!assigned_struct->isMoveAssignmentDeleted())
				? TypeTraitResult::success_true()
				: TypeTraitResult::success_false();
		}
		return selected_assignment_operator && selected_assignment_operator->is_noexcept
			? TypeTraitResult::success_true()
			: TypeTraitResult::success_false();
	}

	return (!assigned_struct->has_vtable &&
			!assigned_struct->hasCopyAssignmentOperator() &&
			!assigned_struct->hasMoveAssignmentOperator() &&
			!assigned_struct->isCopyAssignmentDeleted() &&
			!assigned_struct->isMoveAssignmentDeleted())
		? TypeTraitResult::success_true()
		: TypeTraitResult::success_false();
}

bool isRecordPropertyTraitOwnedBySharedEvaluator(TypeTraitKind kind) {
	switch (kind) {
	case TypeTraitKind::IsTriviallyCopyable:
	case TypeTraitKind::IsTrivial:
	case TypeTraitKind::IsPod:
	case TypeTraitKind::IsStandardLayout:
	case TypeTraitKind::IsAggregate:
	case TypeTraitKind::IsEmpty:
	case TypeTraitKind::IsPolymorphic:
	case TypeTraitKind::IsFinal:
	case TypeTraitKind::IsAbstract:
	case TypeTraitKind::IsDestructible:
	case TypeTraitKind::IsTriviallyDestructible:
	case TypeTraitKind::IsNothrowDestructible:
	case TypeTraitKind::HasTrivialDestructor:
	case TypeTraitKind::HasVirtualDestructor:
	case TypeTraitKind::IsConstructible:
	case TypeTraitKind::IsTriviallyConstructible:
	case TypeTraitKind::IsNothrowConstructible:
	case TypeTraitKind::IsAssignable:
	case TypeTraitKind::IsTriviallyAssignable:
	case TypeTraitKind::IsNothrowAssignable:
		return true;
	default:
		return false;
	}
}

// Whether a single already-resolved argument type can initialize a scalar,
// reference, or pointer target. Shared by the folded and lazy constructibility
// queries.
bool constructibleFromArgument(
	const TypeSpecifierNode& target,
	const TypeSpecifierNode& arg) {
	if (target.category() == TypeCategory::Enum &&
		arg.category() == TypeCategory::Enum) {
		if (!target.type_index().is_valid() || !arg.type_index().is_valid()) {
			return false;
		}
		if (target.type_index() != arg.type_index()) {
			return false;
		}
	}
	if (target.runtime_pointer_depth() > 0) {
		if (arg.runtime_pointer_depth() == 0) {
			return arg.category() == TypeCategory::Nullptr;
		}
		if (arg.runtime_pointer_depth() != target.runtime_pointer_depth()) {
			return false;
		}
		if (target.category() != arg.category() &&
			target.category() != TypeCategory::Void &&
			arg.category() != TypeCategory::Void) {
			return false;
		}
	}
	return can_convert_type(arg, target).is_valid;
}

static std::optional<TypeTraitResult> tryEvaluateCanonicalScalarConstructionFromArgument(const TypeSpecifierNode& target,
	const TypeSpecifierNode& argument) {
	if (target.is_reference()) {
		if (FrontendContext* context = FrontendContext::active()) {
			CanonicalTypeTable& table = context->canonicalTypes();
			CanonicalTypeTransaction transaction(table);
			const CanonicalTypeImport imported_target = importCanonicalTraitOperand(table, target);
			const std::optional<TypeId> imported_argument = tryImportSupportedCanonical(table, argument);
			if (imported_target.status == CanonicalTypeImportStatus::Supported && imported_argument.has_value()) {
				const auto binding = tryBuildCanonicalReferenceArgumentConversionPlan(table, *imported_argument, imported_target.type);
				if (binding.has_value()) {
					return binding->is_valid ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
				}
			}
		}
		const std::optional<ConversionPlan> binding =
			tryBuildCanonicalReferenceBindingPlan(argument, target, CanonicalReferenceBindingContext::Initialization);
		if (binding.has_value()) {
			return binding->is_valid ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
		}
		return std::nullopt;
	}
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		return std::nullopt;
	}
	CanonicalTypeTable& table = context->canonicalTypes();
	CanonicalTypeTransaction transaction(table);
	const CanonicalTypeImport imported_target = importCanonicalTraitOperand(table, target);
	const std::optional<TypeId> imported_argument = tryImportSupportedCanonical(table, argument);
	if (imported_target.status != CanonicalTypeImportStatus::Supported || !imported_argument.has_value()) {
		return std::nullopt;
	}
	TypeId source_type = *imported_argument;
	while (table.node(source_type).kind == CanonicalTypeKind::LValueReference || table.node(source_type).kind == CanonicalTypeKind::RValueReference) {
		source_type = table.node(source_type).child;
	}
	const TypeId source_base = table.withoutTopLevelQualifiers(source_type);
	const TypeId target_base = table.withoutTopLevelQualifiers(imported_target.type);
	const CanonicalTypeNode source_node = table.node(source_base);
	const CanonicalTypeNode target_node = table.node(target_base);
	if (source_node.kind == CanonicalTypeKind::Builtin && target_node.kind == CanonicalTypeKind::Builtin &&
		source_node.builtin == CanonicalBuiltinKind::Nullptr && target_node.builtin == CanonicalBuiltinKind::Bool) {
		return TypeTraitResult::success_true();
	}
	if (source_node.kind == CanonicalTypeKind::Enum && target_node.kind == CanonicalTypeKind::Builtin) {
		const std::optional<ConversionPlan> conversion = tryBuildCanonicalEnumToBuiltinConversionPlan(table, source_type, imported_target.type);
		if (conversion.has_value()) {
			return conversion->is_valid ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
		}
	}
	const ConversionPlan conversion = buildCanonicalStructuralConversionPlan(table, *imported_argument, imported_target.type);
	if (conversion.is_valid) {
		// A valid standard scalar conversion is trivial and cannot throw.
		return TypeTraitResult::success_true();
	}
	if (containsUnresolvedConversionType(table, *imported_argument) || containsUnresolvedConversionType(table, imported_target.type)) {
		return std::nullopt;
	}
	return TypeTraitResult::success_false();
}

// Constructor templates need deduction before overload resolution can compare
// them with ordinary constructors. This handles the direct-template-parameter
// and ellipsis shapes whose fixed parameters are independently convertible;
// constrained or non-direct dependent patterns still defer.
static const ConstructorDeclarationNode* findMatchingSimpleConstructorTemplate(
	const StructTypeInfo& struct_info, std::span<const TypeSpecifierNode> arguments) {
	const ConstructorDeclarationNode* match = nullptr;
	for (const StructMemberFunction& member : struct_info.member_functions) {
		if (!member.is_constructor || !member.function_decl.is<ConstructorDeclarationNode>()) {
			continue;
		}
		const ConstructorDeclarationNode& constructor = member.function_decl.as<ConstructorDeclarationNode>();
		if (!constructor.has_template_parameters() || constructor.explicit_condition() || constructor.has_requires_clause()) {
			continue;
		}
		const std::span<const ASTNode> parameters = constructor.parameter_nodes();
		if (arguments.size() < countMinRequiredArgs(constructor) ||
			(!constructor.is_variadic() && arguments.size() > parameters.size())) {
			continue;
		}
		std::unordered_set<StringHandle> directly_deduced_parameters;
		const size_t fixed_argument_count = std::min(arguments.size(), parameters.size());
		for (size_t index = 0; index < fixed_argument_count; ++index) {
			if (!parameters[index].is<DeclarationNode>()) {
				continue;
			}
			const ASTNode& parameter_type_node = parameters[index].as<DeclarationNode>().type_node();
			if (!parameter_type_node.is<TypeSpecifierNode>()) {
				continue;
			}
			const TypeSpecifierNode& parameter_type = parameter_type_node.as<TypeSpecifierNode>();
			if (isDirectCtorTemplateParameterMatch(member, parameter_type)) {
				directly_deduced_parameters.insert(parameter_type.token().handle());
			}
		}
		bool undeducible_parameter = false;
		for (const TemplateParameterNode& template_parameter : constructor.template_parameters()) {
			if (!template_parameter.has_default() &&
				!directly_deduced_parameters.contains(template_parameter.nameHandle())) {
				undeducible_parameter = true;
				break;
			}
		}
		if (undeducible_parameter) {
			continue;
		}
		bool matches = true;
		for (size_t index = 0; index < arguments.size(); ++index) {
			if (index >= parameters.size()) {
				// [over.ics.ellipsis]: extra arguments use the ellipsis conversion
				// sequence. A class argument is conditionally supported and adds no
				// copy/move requirement, so it does not reject a variadic template
				// constructor here.
				if (isDependentTypeTraitOperand(arguments[index])) {
					matches = false;
					break;
				}
				continue;
			}
			if (!parameters[index].is<DeclarationNode>()) {
				matches = false;
				break;
			}
			const ASTNode& parameter_type_node = parameters[index].as<DeclarationNode>().type_node();
			if (!parameter_type_node.is<TypeSpecifierNode>()) {
				matches = false;
				break;
			}
			const TypeSpecifierNode& parameter_type = parameter_type_node.as<TypeSpecifierNode>();
			if (isDirectCtorTemplateParameterMatch(member, parameter_type)) {
				continue;
			}
			if (!buildConversionPlan(arguments[index], parameter_type, nullptr).is_valid) {
				matches = false;
				break;
			}
		}
		if (!matches) {
			continue;
		}
		if (match != nullptr) {
			return nullptr;
		}
		match = &constructor;
	}
	return match;
}

// Shared record branch of the argument-bearing constructibility query. The
// caller supplies the already-resolved argument types; a record target is
// constructible when constructor-overload resolution finds a match, with the
// trivial and nothrow variants keeping the record's own constructor property.
TypeTraitResult evaluateRecordConstructibleFromArgs(TypeTraitKind kind, const StructTypeInfo& struct_info,
	std::span<const TypeSpecifierNode> arguments, size_t target_pointer_depth) {
	if (struct_info.is_union || target_pointer_depth != 0) {
		return TypeTraitResult::success_false();
	}
	const ConstructorOverloadResolutionResult ctor_resolution = resolve_constructor_overload(struct_info, arguments, false);
	if (!ctor_resolution.has_match) {
		const ConstructorDeclarationNode* template_match = findMatchingSimpleConstructorTemplate(struct_info, arguments);
		if (template_match == nullptr) {
			return TypeTraitResult::success_false();
		}
		if (kind == TypeTraitKind::IsConstructible) {
			return TypeTraitResult::success_true();
		}
		if (kind == TypeTraitKind::IsNothrowConstructible) {
			return template_match->is_noexcept() ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
		}
		return TypeTraitResult::success_false();
	}
	if (kind == TypeTraitKind::IsConstructible) {
		return TypeTraitResult::success_true();
	}
	if (kind == TypeTraitKind::IsNothrowConstructible) {
		const ConstructorDeclarationNode* selected = ctor_resolution.selected_overload;
		if (selected != nullptr && !selected->is_implicit() && !selected->is_explicitly_defaulted()) {
			return selected->is_noexcept() ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
		}
		if (selected != nullptr) {
			// An implicit or defaulted selection is a copy or move, so its
			// exception specification derives from the subobjects.
			const bool prefer_move = !arguments.empty() && arguments.front().is_rvalue_reference();
			return recordNothrowCopyOrMoveConstruction(struct_info, prefer_move) ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
		}
	}
	if (kind == TypeTraitKind::IsTriviallyConstructible) {
		const ConstructorDeclarationNode* selected = ctor_resolution.selected_overload;
		if (selected != nullptr && !selected->is_implicit() && !selected->is_explicitly_defaulted()) {
			return TypeTraitResult::success_false();  // user-provided is non-trivial
		}
		// An argument-bearing trivial construction is a copy or move, so it is
		// trivial exactly when the class is trivially copyable.
		return isStructTriviallyCopyable(&struct_info) ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
	}
	return (!struct_info.has_vtable && !struct_info.hasUserDefinedConstructor()) ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
}

// Canonical constructor query for complete schemas and supported standard conversion sequences.
// Other overload shapes defer to the compatibility resolver.
static std::optional<TypeTraitResult> tryEvaluateCanonicalRecordConstructibleFromArgs(TypeTraitKind kind, const TypeSpecifierNode& target,
	std::span<const TypeSpecifierNode> arguments) {
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		return std::nullopt;
	}
	CanonicalTypeTable& table = context->canonicalTypes();
	CanonicalTypeTransaction transaction(table);
	const CanonicalTypeImport imported = importCanonicalTraitOperand(table, target);
	if (imported.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	const CanonicalTypeKind type_kind = table.node(imported.type).kind;
	if (type_kind != CanonicalTypeKind::Record && type_kind != CanonicalTypeKind::TemplateSpecialization) {
		return std::nullopt;
	}
	if (!table.hasRecordConstructors(imported.type)) {
		return std::nullopt;
	}
	std::vector<CanonicalConstructorArgument> selection_arguments;
	selection_arguments.reserve(arguments.size());
	for (const TypeSpecifierNode& argument : arguments) {
		const std::optional<TypeId> imported_argument = tryImportSupportedCanonical(table, argument);
		if (!imported_argument.has_value()) {
			return std::nullopt;
		}
		const bool is_lvalue = table.node(*imported_argument).kind == CanonicalTypeKind::LValueReference;
		selection_arguments.push_back(CanonicalConstructorArgument{*imported_argument, is_lvalue, false, CanonicalDefaultException::Unknown});
	}
	const CanonicalConstructorSelection selection = selectCanonicalConstructor(
		table,
		imported.type,
		selection_arguments,
		CanonicalConstructorSelectionPolicy::AllowExplicit |
			CanonicalConstructorSelectionPolicy::AllowUserDefinedArgumentConversion |
			CanonicalConstructorSelectionPolicy::AllowEllipsis |
			CanonicalConstructorSelectionPolicy::IncludeCopyMoveConstructors |
			CanonicalConstructorSelectionPolicy::RequireCompleteSchema);
	if (selection.status == CanonicalConstructorSelectionStatus::Unknown) {
		return std::nullopt;
	}
	if (selection.status == CanonicalConstructorSelectionStatus::NoMatch) {
		return TypeTraitResult::success_false();
	}
	if (kind == TypeTraitKind::IsConstructible) {
		return TypeTraitResult::success_true();
	}
	if (kind == TypeTraitKind::IsNothrowConstructible) {
		if (selection.omitted_default_count > 0) {
			if (selection.omitted_defaults_known_throwing) {
				return TypeTraitResult::success_false();
			}
			if (!selection.omitted_defaults_nothrow) {
				return std::nullopt;
			}
		}
		if (selection.is_implicit_or_defaulted) {
			return std::nullopt;
		}
		return selection.is_noexcept ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
	}
	if (kind == TypeTraitKind::IsTriviallyConstructible) {
		if (!selection.is_implicit_or_defaulted) {
			return TypeTraitResult::success_false();
		}
		return std::nullopt;
	}
	throw InternalError("canonical constructibility: unexpected trait kind");
}

static std::optional<TypeTraitResult> tryEvaluateCanonicalConstructibleFromArgs(TypeTraitKind kind, const TypeSpecifierNode& target,
	std::span<const TypeSpecifierNode> arguments) {
	if (arguments.empty()) {
		return std::nullopt;  // the zero-argument query owns this case
	}
	if (kind != TypeTraitKind::IsConstructible && kind != TypeTraitKind::IsTriviallyConstructible && kind != TypeTraitKind::IsNothrowConstructible) {
		return std::nullopt;
	}
	// A reference or scalar target accepts at most one source type and uses the
	// implicit conversion rules rather than constructor overload resolution.
	if (target.is_reference() || TypeTraitEval::isScalarType(target.category(), target.is_reference(), target.runtime_pointer_depth())) {
		if (arguments.size() != 1) {
			return TypeTraitResult::success_false();
		}
		const std::optional<TypeTraitResult> canonical = tryEvaluateCanonicalScalarConstructionFromArgument(target, arguments.front());
		if (canonical.has_value()) {
			return canonical;
		}
		return constructibleFromArgument(target, arguments.front()) ? TypeTraitResult::success_true() : TypeTraitResult::success_false();
	}
	if (const std::optional<TypeTraitResult> canonical = tryEvaluateCanonicalRecordConstructibleFromArgs(kind, target, arguments);
		canonical.has_value()) {
		return canonical;
	}
	const StructTypeInfo* struct_info = structInfoFromTypeIndex(target.type_index());
	if (struct_info == nullptr) {
		return std::nullopt;  // unmigrated target shapes defer
	}
	return evaluateRecordConstructibleFromArgs(kind, *struct_info, arguments, target.runtime_pointer_depth());
}

// Canonical-only constructibility: the zero-argument query answers from the
// published default-construction fact and the argument-bearing query resolves a
// constructor. An empty result means the canonical table cannot decide.
static std::optional<TypeTraitResult> tryEvaluateCanonicalConstructibility(
	TypeTraitKind kind,
	const TypeSpecifierNode& target,
	std::span<const TypeSpecifierNode> arguments) {
	if (arguments.empty()) {
		return tryEvaluateCanonicalDefaultConstructionTrait(kind, target);
	}
	return tryEvaluateCanonicalConstructibleFromArgs(kind, target, arguments);
}

// Single constructibility authority shared by the folded/constexpr path, the
// lazy constraint path, and code generation. `fallback` decides whether an
// operand the canonical table cannot decide keeps an empty result (the lazy
// path, which reports an unknown constraint) or falls through to the sema
// compatibility answer (the folded path). The canonical query and the
// compatibility answer live here together so the reference, scalar, and record
// rules have one implementation.
std::optional<TypeTraitResult> evaluateConstructibility(
	TypeTraitKind kind,
	const TypeSpecifierNode& target,
	std::span<const TypeSpecifierNode> arguments,
	ConstructibilityFallback fallback) {
	const bool is_reference_target = target.is_reference();
	const bool is_scalar_target = TypeTraitEval::isScalarType(
		target.category(), target.is_reference(), target.runtime_pointer_depth());
	const StructTypeInfo* struct_info = structInfoFromTypeIndex(target.type_index());
	// The compatibility path rejects a union or pointer-shaped record before a
	// published fact can answer; the canonical-only path does not apply it.
	if (fallback == ConstructibilityFallback::Sema &&
		!is_reference_target && !is_scalar_target &&
		(!struct_info || struct_info->is_union || target.runtime_pointer_depth() != 0)) {
		return TypeTraitResult::success_false();
	}
	if (const std::optional<TypeTraitResult> canonical =
			tryEvaluateCanonicalConstructibility(kind, target, arguments);
		canonical.has_value()) {
		return canonical;
	}
	if (fallback == ConstructibilityFallback::None) {
		return std::nullopt;
	}
	if (is_reference_target) {
		if (arguments.size() != 1) {
			return TypeTraitResult::success_false();
		}
		return constructibleFromArgument(target, arguments.front())
			? TypeTraitResult::success_true()
			: TypeTraitResult::success_false();
	}
	if (is_scalar_target) {
		if (arguments.empty()) {
			return TypeTraitResult::success_true();
		}
		if (arguments.size() != 1) {
			return TypeTraitResult::success_false();
		}
		return constructibleFromArgument(target, arguments.front())
			? TypeTraitResult::success_true()
			: TypeTraitResult::success_false();
	}
	if (!struct_info) {
		return TypeTraitResult::success_false();
	}
	if (kind == TypeTraitKind::IsConstructible) {
		return recordDefaultConstructible(*struct_info)
			? TypeTraitResult::success_true()
			: TypeTraitResult::success_false();
	}
	if (kind == TypeTraitKind::IsTriviallyConstructible) {
		return recordTriviallyConstructible(*struct_info)
			? TypeTraitResult::success_true()
			: TypeTraitResult::success_false();
	}
	if (kind == TypeTraitKind::IsNothrowConstructible) {
		return recordNothrowConstructible(*struct_info)
			? TypeTraitResult::success_true()
			: TypeTraitResult::success_false();
	}
	TypeTraitResult base_result = evaluateTypeTrait(kind, target, struct_info);
	return base_result.success
		? base_result
		: TypeTraitResult::success_false();
}

TypeTraitResult evaluateTypeTrait(const TypeTraitExprNode& trait_expr) {
	if (trait_expr.is_no_arg_trait()) {
		return trait_expr.kind() == TypeTraitKind::IsConstantEvaluated
			? TypeTraitResult::success_true()
			: TypeTraitResult::failure();
	}
	if (!trait_expr.has_type() || !trait_expr.type_node().is<TypeSpecifierNode>()) {
		return TypeTraitResult::failure();
	}

	const TypeSpecifierNode& raw_type_spec =
		trait_expr.type_node().as<TypeSpecifierNode>();
	if (isDependentTypeTraitOperand(raw_type_spec)) {
		return TypeTraitResult::failure();
	}
	if (const std::optional<TypeTraitResult> canonical_result =
			tryEvaluateCanonicalStructuralTrait(trait_expr.kind(), raw_type_spec);
		canonical_result.has_value()) {
		return *canonical_result;
	}
	const TypeSpecifierNode type_spec = normalizeTypeTraitOperand(raw_type_spec);
	if (trait_expr.is_variadic_trait()) {
		std::vector<TypeSpecifierNode> additional_types;
		additional_types.reserve(trait_expr.additional_type_nodes().size());
		for (const ASTNode& additional_type_node : trait_expr.additional_type_nodes()) {
			if (!additional_type_node.is<TypeSpecifierNode>()) {
				return TypeTraitResult::failure();
			}
			if (isDependentTypeTraitOperand(
					additional_type_node.as<TypeSpecifierNode>())) {
				return TypeTraitResult::failure();
			}
			additional_types.push_back(
				normalizeTypeTraitOperand(additional_type_node.as<TypeSpecifierNode>()));
		}
		// The constructible family has one shared authority, including the sema
		// compatibility answer for an operand the canonical table cannot decide.
		const std::optional<TypeTraitResult> constructibility = evaluateConstructibility(
			trait_expr.kind(), type_spec, additional_types, ConstructibilityFallback::Sema);
		return constructibility.value_or(TypeTraitResult::success_false());
	}

	if (trait_expr.has_second_type()) {
		if (!trait_expr.second_type_node().is<TypeSpecifierNode>()) {
			return TypeTraitResult::failure();
		}
		const TypeSpecifierNode& raw_second_type_spec =
			trait_expr.second_type_node().as<TypeSpecifierNode>();
		if (isDependentTypeTraitOperand(raw_second_type_spec)) {
			return TypeTraitResult::failure();
		}
		const TypeSpecifierNode second_type_spec = normalizeTypeTraitOperand(
			raw_second_type_spec);
		switch (trait_expr.kind()) {
		case TypeTraitKind::IsSame:
			if (const std::optional<TypeTraitResult> canonical_result =
					tryEvaluateCanonicalSameTrait(raw_type_spec, raw_second_type_spec);
				canonical_result.has_value()) {
				return *canonical_result;
			}
			return areSameTypeTraitOperands(type_spec, second_type_spec)
				? TypeTraitResult::success_true()
				: TypeTraitResult::success_false();
		case TypeTraitKind::IsAssignable:
		case TypeTraitKind::IsTriviallyAssignable:
		case TypeTraitKind::IsNothrowAssignable:
			return evaluateAssignableTrait(
				trait_expr.kind(),
				type_spec,
				second_type_spec);
		default:
			return TypeTraitResult::failure();
		}
	}

	const StructTypeInfo* struct_info = structInfoFromTypeIndex(type_spec.type_index());
	return evaluateTypeTrait(trait_expr.kind(), type_spec, struct_info);
}
