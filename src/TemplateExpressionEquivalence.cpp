#include "TemplateExpressionEquivalence.h"
#include "CompileError.h"
#include "TemplateRegistry_Types.h"
#include "TemplateTypes.h"
#include <cstring>

namespace FlashCpp {

namespace {

template <typename T>
inline void hashCombine(size_t& seed, const T& value) {
	seed ^= std::hash<T>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

inline void hashCombineStringView(size_t& seed, std::string_view value) {
	hashCombine(seed, std::hash<std::string_view>{}(value));
}

bool equalTypeSpecifierIdentityImpl(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs);
size_t hashTypeSpecifierIdentityImpl(const TypeSpecifierNode& type);
bool equalDependentExpressionIdentityImpl(const ASTNode& lhs, const ASTNode& rhs);
size_t hashDependentExpressionIdentityImpl(const ASTNode& node);
bool equalDeclarationIdentity(const DeclarationNode& lhs, const DeclarationNode& rhs);
size_t hashDeclarationIdentity(const DeclarationNode& decl);

bool equalTemplateArgInfoIdentity(
	const TypeInfo::TemplateArgInfo& lhs,
	const TypeInfo::TemplateArgInfo& rhs);
size_t hashTemplateArgInfoIdentity(const TypeInfo::TemplateArgInfo& arg);

template <typename Range>
bool equalAstRange(const Range& lhs, const Range& rhs) {
	if (lhs.size() != rhs.size()) {
		return false;
	}
	for (size_t i = 0; i < lhs.size(); ++i) {
		if (!equalDependentExpressionIdentityImpl(lhs[i], rhs[i])) {
			return false;
		}
	}
	return true;
}

template <typename Range>
size_t hashAstRange(const Range& range) {
	size_t seed = std::hash<size_t>{}(range.size());
	for (const ASTNode& node : range) {
		hashCombine(seed, hashDependentExpressionIdentityImpl(node));
	}
	return seed;
}

template <typename Range>
bool equalDeclarationAstRange(const Range& lhs, const Range& rhs, std::string_view context) {
	if (lhs.size() != rhs.size()) {
		return false;
	}
	for (size_t i = 0; i < lhs.size(); ++i) {
		if (!lhs[i].template is<DeclarationNode>() || !rhs[i].template is<DeclarationNode>()) {
			throw InternalError(
				std::string(context) +
				" expected DeclarationNode parameter at index " +
				std::to_string(i));
		}
		if (!equalDeclarationIdentity(
				lhs[i].template as<DeclarationNode>(),
				rhs[i].template as<DeclarationNode>())) {
			return false;
		}
	}
	return true;
}

template <typename Range>
size_t hashDeclarationAstRange(const Range& range, std::string_view context) {
	size_t seed = std::hash<size_t>{}(range.size());
	for (size_t i = 0; i < range.size(); ++i) {
		if (!range[i].template is<DeclarationNode>()) {
			throw InternalError(
				std::string(context) +
				" expected DeclarationNode parameter at index " +
				std::to_string(i));
		}
		hashCombine(seed, hashDeclarationIdentity(range[i].template as<DeclarationNode>()));
	}
	return seed;
}

template <typename Range>
bool equalTemplateArgInfoRange(const Range& lhs, const Range& rhs) {
	if (lhs.size() != rhs.size()) {
		return false;
	}
	for (size_t i = 0; i < lhs.size(); ++i) {
		if (!equalTemplateArgInfoIdentity(lhs[i], rhs[i])) {
			return false;
		}
	}
	return true;
}

template <typename Range>
size_t hashTemplateArgInfoRange(const Range& range) {
	size_t seed = std::hash<size_t>{}(range.size());
	for (const TypeInfo::TemplateArgInfo& arg : range) {
		hashCombine(seed, hashTemplateArgInfoIdentity(arg));
	}
	return seed;
}

template <typename Range>
bool equalTokenValueRange(const Range& lhs, const Range& rhs) {
	if (lhs.size() != rhs.size()) {
		return false;
	}
	for (size_t i = 0; i < lhs.size(); ++i) {
		if (lhs[i].value() != rhs[i].value()) {
			return false;
		}
	}
	return true;
}

template <typename Range>
size_t hashTokenValueRange(const Range& range) {
	size_t seed = std::hash<size_t>{}(range.size());
	for (const Token& token : range) {
		hashCombineStringView(seed, token.value());
	}
	return seed;
}

bool equalDependentQualifiedNameRecord(
	const TypeInfo::DependentQualifiedNameRecord& lhs,
	const TypeInfo::DependentQualifiedNameRecord& rhs) {
	if (lhs.owner_kind != rhs.owner_kind ||
		lhs.owner_name != rhs.owner_name ||
		!equalTypeIndexIdentity(lhs.owner_type, rhs.owner_type) ||
		lhs.names_current_instantiation != rhs.names_current_instantiation ||
		!equalTemplateArgInfoRange(lhs.owner_template_arguments, rhs.owner_template_arguments) ||
		lhs.member_chain.size() != rhs.member_chain.size()) {
		return false;
	}
	for (size_t i = 0; i < lhs.member_chain.size(); ++i) {
		const auto& lhs_member = lhs.member_chain[i];
		const auto& rhs_member = rhs.member_chain[i];
		if (lhs_member.name != rhs_member.name ||
			lhs_member.has_template_arguments != rhs_member.has_template_arguments ||
			lhs_member.has_template_keyword != rhs_member.has_template_keyword) {
			return false;
		}
		if (!equalTemplateArgInfoRange(lhs_member.template_arguments, rhs_member.template_arguments)) {
			return false;
		}
	}
	return true;
}

size_t hashDependentQualifiedNameRecord(const TypeInfo::DependentQualifiedNameRecord& record) {
	size_t seed = std::hash<uint8_t>{}(static_cast<uint8_t>(record.owner_kind));
	hashCombine(seed, std::hash<StringHandle>{}(record.owner_name));
	hashCombine(seed, hashTypeIndexIdentity(record.owner_type));
	hashCombine(seed, std::hash<bool>{}(record.names_current_instantiation));
	hashCombine(seed, hashTemplateArgInfoRange(record.owner_template_arguments));
	hashCombine(seed, std::hash<size_t>{}(record.member_chain.size()));
	for (const auto& member : record.member_chain) {
		hashCombine(seed, std::hash<StringHandle>{}(member.name));
		hashCombine(seed, std::hash<bool>{}(member.has_template_arguments));
		hashCombine(seed, std::hash<bool>{}(member.has_template_keyword));
		hashCombine(seed, hashTemplateArgInfoRange(member.template_arguments));
	}
	return seed;
}

bool equalTypeSpecifierIdentityImpl(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) {
	if (lhs.category() != rhs.category() ||
		!equalTypeIndexIdentity(lhs.type_index(), rhs.type_index()) ||
		lhs.qualifier() != rhs.qualifier() ||
		lhs.cv_qualifier() != rhs.cv_qualifier() ||
		lhs.reference_qualifier() != rhs.reference_qualifier() ||
		lhs.pointer_depth() != rhs.pointer_depth() ||
		lhs.is_array() != rhs.is_array() ||
		lhs.has_unsized_outer_array_dimension() != rhs.has_unsized_outer_array_dimension() ||
		lhs.array_dimension_count() != rhs.array_dimension_count() ||
		lhs.is_pack_expansion() != rhs.is_pack_expansion() ||
		lhs.has_member_class() != rhs.has_member_class() ||
		lhs.has_function_signature() != rhs.has_function_signature() ||
		lhs.has_concept_constraint() != rhs.has_concept_constraint()) {
		return false;
	}

	const auto& lhs_levels = lhs.pointer_levels();
	const auto& rhs_levels = rhs.pointer_levels();
	if (lhs_levels.size() != rhs_levels.size()) {
		return false;
	}
	for (size_t i = 0; i < lhs_levels.size(); ++i) {
		if (lhs_levels[i].cv_qualifier != rhs_levels[i].cv_qualifier) {
			return false;
		}
	}

	if (lhs.array_dimensions().size() != rhs.array_dimensions().size()) {
		return false;
	}
	for (size_t i = 0; i < lhs.array_dimensions().size(); ++i) {
		if (lhs.array_dimensions()[i] != rhs.array_dimensions()[i]) {
			return false;
		}
	}

	if (lhs.has_member_class() && lhs.member_class_name() != rhs.member_class_name()) {
		return false;
	}
	if (lhs.has_function_signature() &&
		!equalFunctionSignatureIdentity(lhs.function_signature(), rhs.function_signature())) {
		return false;
	}
	if (lhs.has_concept_constraint() &&
		lhs.concept_constraint() != rhs.concept_constraint()) {
		return false;
	}
	return true;
}

size_t hashTypeSpecifierIdentityImpl(const TypeSpecifierNode& type) {
	size_t seed = std::hash<uint8_t>{}(static_cast<uint8_t>(type.category()));
	hashCombine(seed, hashTypeIndexIdentity(type.type_index()));
	hashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(type.qualifier())));
	hashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(type.cv_qualifier())));
	hashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(type.reference_qualifier())));
	hashCombine(seed, std::hash<size_t>{}(type.pointer_depth()));
	for (const PointerLevel& level : type.pointer_levels()) {
		hashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(level.cv_qualifier)));
	}
	hashCombine(seed, std::hash<bool>{}(type.is_array()));
	hashCombine(seed, std::hash<bool>{}(type.has_unsized_outer_array_dimension()));
	hashCombine(seed, std::hash<size_t>{}(type.array_dimension_count()));
	for (size_t dim : type.array_dimensions()) {
		hashCombine(seed, std::hash<size_t>{}(dim));
	}
	hashCombine(seed, std::hash<bool>{}(type.is_pack_expansion()));
	hashCombine(seed, std::hash<bool>{}(type.has_member_class()));
	if (type.has_member_class()) {
		hashCombine(seed, std::hash<StringHandle>{}(type.member_class_name()));
	}
	hashCombine(seed, std::hash<bool>{}(type.has_function_signature()));
	if (type.has_function_signature()) {
		hashCombine(seed, hashFunctionSignatureIdentity(type.function_signature()));
	}
	hashCombine(seed, std::hash<bool>{}(type.has_concept_constraint()));
	if (type.has_concept_constraint()) {
		hashCombineStringView(seed, type.concept_constraint());
	}
	return seed;
}

bool equalDeclarationIdentity(const DeclarationNode& lhs, const DeclarationNode& rhs) {
	return equalTypeSpecifierIdentityImpl(lhs.type_specifier_node(), rhs.type_specifier_node()) &&
		   lhs.identifier_token().value() == rhs.identifier_token().value() &&
		   lhs.is_parameter_pack() == rhs.is_parameter_pack() &&
		   lhs.is_unsized_array() == rhs.is_unsized_array();
}

size_t hashDeclarationIdentity(const DeclarationNode& decl) {
	size_t seed = hashTypeSpecifierIdentityImpl(decl.type_specifier_node());
	hashCombineStringView(seed, decl.identifier_token().value());
	hashCombine(seed, std::hash<bool>{}(decl.is_parameter_pack()));
	hashCombine(seed, std::hash<bool>{}(decl.is_unsized_array()));
	return seed;
}

bool equalTemplateArgInfoIdentity(
	const TypeInfo::TemplateArgInfo& lhs,
	const TypeInfo::TemplateArgInfo& rhs) {
	return toTemplateTypeArg(lhs) == toTemplateTypeArg(rhs);
}

size_t hashTemplateArgInfoIdentity(const TypeInfo::TemplateArgInfo& arg) {
	return toTemplateTypeArg(arg).hash();
}

bool equalCalleeDescriptorIdentity(const CalleeDescriptor& lhs, const CalleeDescriptor& rhs) {
	return lhs.kind() == rhs.kind() &&
		   equalDeclarationIdentity(lhs.declaration(), rhs.declaration());
}

size_t hashCalleeDescriptorIdentity(const CalleeDescriptor& callee) {
	size_t seed = std::hash<uint8_t>{}(static_cast<uint8_t>(callee.kind()));
	hashCombine(seed, hashDeclarationIdentity(callee.declaration()));
	return seed;
}

bool equalNumericLiteralIdentity(const NumericLiteralNode& lhs, const NumericLiteralNode& rhs) {
	if (lhs.type() != rhs.type() ||
		lhs.qualifier() != rhs.qualifier() ||
		lhs.sizeInBits() != rhs.sizeInBits() ||
		lhs.value().index() != rhs.value().index()) {
		return false;
	}
	if (std::holds_alternative<unsigned long long>(lhs.value())) {
		return std::get<unsigned long long>(lhs.value()) ==
			   std::get<unsigned long long>(rhs.value());
	}
	double lhs_double = std::get<double>(lhs.value());
	double rhs_double = std::get<double>(rhs.value());
	uint64_t lhs_bits = 0;
	uint64_t rhs_bits = 0;
	std::memcpy(&lhs_bits, &lhs_double, sizeof(lhs_bits));
	std::memcpy(&rhs_bits, &rhs_double, sizeof(rhs_bits));
	return lhs_bits == rhs_bits;
}

size_t hashNumericLiteralIdentity(const NumericLiteralNode& numeric) {
	size_t seed = std::hash<uint8_t>{}(static_cast<uint8_t>(numeric.type()));
	hashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(numeric.qualifier())));
	hashCombine(seed, std::hash<size_t>{}(numeric.sizeInBits()));
	if (std::holds_alternative<unsigned long long>(numeric.value())) {
		hashCombine(seed, std::hash<unsigned long long>{}(std::get<unsigned long long>(numeric.value())));
	} else {
		double value = std::get<double>(numeric.value());
		uint64_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		hashCombine(seed, std::hash<uint64_t>{}(bits));
	}
	return seed;
}

bool equalLambdaCaptureIdentity(const LambdaCaptureNode& lhs, const LambdaCaptureNode& rhs) {
	return lhs.kind() == rhs.kind() &&
		   lhs.identifier_name() == rhs.identifier_name() &&
		   lhs.has_initializer() == rhs.has_initializer() &&
		   (!lhs.has_initializer() ||
			equalDependentExpressionIdentityImpl(*lhs.initializer(), *rhs.initializer()));
}

size_t hashLambdaCaptureIdentity(const LambdaCaptureNode& capture) {
	size_t seed = std::hash<uint8_t>{}(static_cast<uint8_t>(capture.kind()));
	hashCombineStringView(seed, capture.identifier_name());
	hashCombine(seed, std::hash<bool>{}(capture.has_initializer()));
	if (capture.has_initializer()) {
		hashCombine(seed, hashDependentExpressionIdentityImpl(*capture.initializer()));
	}
	return seed;
}

bool isSupportedDependentExpressionIdentityNode(const ASTNode& node) {
	return node.is<TypeSpecifierNode>() ||
		   tryGetIdentifier(node) != nullptr ||
		   tryGetNode<QualifiedIdentifierNode>(node) != nullptr ||
		   tryGetNode<NumericLiteralNode>(node) != nullptr ||
		   tryGetNode<BoolLiteralNode>(node) != nullptr ||
		   tryGetNode<StringLiteralNode>(node) != nullptr ||
		   tryGetNode<BinaryOperatorNode>(node) != nullptr ||
		   tryGetNode<UnaryOperatorNode>(node) != nullptr ||
		   tryGetNode<TernaryOperatorNode>(node) != nullptr ||
		   tryGetNode<MemberAccessNode>(node) != nullptr ||
		   tryGetNode<PointerToMemberAccessNode>(node) != nullptr ||
		   tryGetNode<PseudoDestructorCallNode>(node) != nullptr ||
		   tryGetNode<ArraySubscriptNode>(node) != nullptr ||
		   tryGetNode<SizeofExprNode>(node) != nullptr ||
		   tryGetNode<SizeofPackNode>(node) != nullptr ||
		   tryGetNode<AlignofExprNode>(node) != nullptr ||
		   tryGetNode<NoexceptExprNode>(node) != nullptr ||
		   tryGetNode<OffsetofExprNode>(node) != nullptr ||
		   tryGetNode<TypeTraitExprNode>(node) != nullptr ||
		   tryGetNode<NewExpressionNode>(node) != nullptr ||
		   tryGetNode<DeleteExpressionNode>(node) != nullptr ||
		   tryGetNode<StaticCastNode>(node) != nullptr ||
		   tryGetNode<DynamicCastNode>(node) != nullptr ||
		   tryGetNode<ConstCastNode>(node) != nullptr ||
		   tryGetNode<ReinterpretCastNode>(node) != nullptr ||
		   tryGetNode<TypeidNode>(node) != nullptr ||
		   tryGetNode<LambdaExpressionNode>(node) != nullptr ||
		   tryGetNode<TemplateParameterReferenceNode>(node) != nullptr ||
		   tryGetNode<FoldExpressionNode>(node) != nullptr ||
		   tryGetNode<PackExpansionExprNode>(node) != nullptr ||
		   tryGetNode<InitializerListConstructionNode>(node) != nullptr ||
		   tryGetNode<ThrowExpressionNode>(node) != nullptr ||
		   tryGetNode<CallExprNode>(node) != nullptr ||
		   tryGetNode<ConstructorCallNode>(node) != nullptr;
}

[[noreturn]] void throwUnsupportedDependentExpressionIdentityNode(std::string_view context, const ASTNode& node) {
	throw InternalError(
		std::string(context) +
		" does not support AST node kind '" +
		std::string(node.type_name()) +
		"'");
}

bool equalDependentExpressionIdentityImpl(const ASTNode& lhs, const ASTNode& rhs) {
	if (!lhs.has_value() || !rhs.has_value()) {
		return lhs.has_value() == rhs.has_value();
	}
	if (lhs.is<TypeSpecifierNode>() || rhs.is<TypeSpecifierNode>()) {
		return lhs.is<TypeSpecifierNode>() &&
			   rhs.is<TypeSpecifierNode>() &&
			   equalTypeSpecifierIdentityImpl(lhs.as<TypeSpecifierNode>(), rhs.as<TypeSpecifierNode>());
	}
	if (const IdentifierNode* lhs_identifier = tryGetIdentifier(lhs)) {
		const IdentifierNode* rhs_identifier = tryGetIdentifier(rhs);
		return rhs_identifier != nullptr &&
			   lhs_identifier->name() == rhs_identifier->name();
	}
	if (const QualifiedIdentifierNode* lhs_qualified = tryGetNode<QualifiedIdentifierNode>(lhs)) {
		const QualifiedIdentifierNode* rhs_qualified = tryGetNode<QualifiedIdentifierNode>(rhs);
		if (rhs_qualified == nullptr ||
			lhs_qualified->namespace_handle() != rhs_qualified->namespace_handle() ||
			lhs_qualified->name() != rhs_qualified->name() ||
			lhs_qualified->has_template_arguments() != rhs_qualified->has_template_arguments() ||
			lhs_qualified->hasDependentQualifiedName() != rhs_qualified->hasDependentQualifiedName()) {
			return false;
		}
		if (lhs_qualified->has_template_arguments() &&
			!equalAstRange(
				lhs_qualified->template_arguments(),
				rhs_qualified->template_arguments())) {
			return false;
		}
		if (!lhs_qualified->hasDependentQualifiedName()) {
			return true;
		}
		return equalDependentQualifiedNameRecord(
			*lhs_qualified->dependentQualifiedName(),
			*rhs_qualified->dependentQualifiedName());
	}
	if (const NumericLiteralNode* lhs_numeric = tryGetNode<NumericLiteralNode>(lhs)) {
		const NumericLiteralNode* rhs_numeric = tryGetNode<NumericLiteralNode>(rhs);
		return rhs_numeric != nullptr &&
			   equalNumericLiteralIdentity(*lhs_numeric, *rhs_numeric);
	}
	if (const BoolLiteralNode* lhs_bool = tryGetNode<BoolLiteralNode>(lhs)) {
		const BoolLiteralNode* rhs_bool = tryGetNode<BoolLiteralNode>(rhs);
		return rhs_bool != nullptr &&
			   lhs_bool->value() == rhs_bool->value();
	}
	if (const StringLiteralNode* lhs_string = tryGetNode<StringLiteralNode>(lhs)) {
		const StringLiteralNode* rhs_string = tryGetNode<StringLiteralNode>(rhs);
		return rhs_string != nullptr &&
			   lhs_string->value() == rhs_string->value();
	}
	if (const BinaryOperatorNode* lhs_binary = tryGetNode<BinaryOperatorNode>(lhs)) {
		const BinaryOperatorNode* rhs_binary = tryGetNode<BinaryOperatorNode>(rhs);
		return rhs_binary != nullptr &&
			   lhs_binary->op() == rhs_binary->op() &&
			   equalDependentExpressionIdentityImpl(lhs_binary->get_lhs(), rhs_binary->get_lhs()) &&
			   equalDependentExpressionIdentityImpl(lhs_binary->get_rhs(), rhs_binary->get_rhs());
	}
	if (const UnaryOperatorNode* lhs_unary = tryGetNode<UnaryOperatorNode>(lhs)) {
		const UnaryOperatorNode* rhs_unary = tryGetNode<UnaryOperatorNode>(rhs);
		return rhs_unary != nullptr &&
			   lhs_unary->op() == rhs_unary->op() &&
			   lhs_unary->is_prefix() == rhs_unary->is_prefix() &&
			   lhs_unary->is_builtin_addressof() == rhs_unary->is_builtin_addressof() &&
			   equalDependentExpressionIdentityImpl(lhs_unary->get_operand(), rhs_unary->get_operand());
	}
	if (const TernaryOperatorNode* lhs_ternary = tryGetNode<TernaryOperatorNode>(lhs)) {
		const TernaryOperatorNode* rhs_ternary = tryGetNode<TernaryOperatorNode>(rhs);
		return rhs_ternary != nullptr &&
			   equalDependentExpressionIdentityImpl(lhs_ternary->condition(), rhs_ternary->condition()) &&
			   equalDependentExpressionIdentityImpl(lhs_ternary->true_expr(), rhs_ternary->true_expr()) &&
			   equalDependentExpressionIdentityImpl(lhs_ternary->false_expr(), rhs_ternary->false_expr());
	}
	if (const MemberAccessNode* lhs_member = tryGetNode<MemberAccessNode>(lhs)) {
		const MemberAccessNode* rhs_member = tryGetNode<MemberAccessNode>(rhs);
		return rhs_member != nullptr &&
			   lhs_member->is_arrow() == rhs_member->is_arrow() &&
			   lhs_member->member_name() == rhs_member->member_name() &&
			   equalDependentExpressionIdentityImpl(lhs_member->object(), rhs_member->object());
	}
	if (const PointerToMemberAccessNode* lhs_member_ptr = tryGetNode<PointerToMemberAccessNode>(lhs)) {
		const PointerToMemberAccessNode* rhs_member_ptr = tryGetNode<PointerToMemberAccessNode>(rhs);
		return rhs_member_ptr != nullptr &&
			   lhs_member_ptr->is_arrow() == rhs_member_ptr->is_arrow() &&
			   equalDependentExpressionIdentityImpl(lhs_member_ptr->object(), rhs_member_ptr->object()) &&
			   equalDependentExpressionIdentityImpl(lhs_member_ptr->member_pointer(), rhs_member_ptr->member_pointer());
	}
	if (const PseudoDestructorCallNode* lhs_pseudo_dtor = tryGetNode<PseudoDestructorCallNode>(lhs)) {
		const PseudoDestructorCallNode* rhs_pseudo_dtor = tryGetNode<PseudoDestructorCallNode>(rhs);
		return rhs_pseudo_dtor != nullptr &&
			   lhs_pseudo_dtor->is_arrow_access() == rhs_pseudo_dtor->is_arrow_access() &&
			   lhs_pseudo_dtor->qualified_type_name() == rhs_pseudo_dtor->qualified_type_name() &&
			   lhs_pseudo_dtor->type_name() == rhs_pseudo_dtor->type_name() &&
			   equalDependentExpressionIdentityImpl(lhs_pseudo_dtor->object(), rhs_pseudo_dtor->object());
	}
	if (const ArraySubscriptNode* lhs_subscript = tryGetNode<ArraySubscriptNode>(lhs)) {
		const ArraySubscriptNode* rhs_subscript = tryGetNode<ArraySubscriptNode>(rhs);
		return rhs_subscript != nullptr &&
			   equalDependentExpressionIdentityImpl(lhs_subscript->array_expr(), rhs_subscript->array_expr()) &&
			   equalDependentExpressionIdentityImpl(lhs_subscript->index_expr(), rhs_subscript->index_expr());
	}
	if (const SizeofExprNode* lhs_sizeof = tryGetNode<SizeofExprNode>(lhs)) {
		const SizeofExprNode* rhs_sizeof = tryGetNode<SizeofExprNode>(rhs);
		return rhs_sizeof != nullptr &&
			   lhs_sizeof->is_type() == rhs_sizeof->is_type() &&
			   equalDependentExpressionIdentityImpl(lhs_sizeof->type_or_expr(), rhs_sizeof->type_or_expr());
	}
	if (const SizeofPackNode* lhs_sizeof_pack = tryGetNode<SizeofPackNode>(lhs)) {
		const SizeofPackNode* rhs_sizeof_pack = tryGetNode<SizeofPackNode>(rhs);
		return rhs_sizeof_pack != nullptr &&
			   lhs_sizeof_pack->pack_name() == rhs_sizeof_pack->pack_name();
	}
	if (const AlignofExprNode* lhs_alignof = tryGetNode<AlignofExprNode>(lhs)) {
		const AlignofExprNode* rhs_alignof = tryGetNode<AlignofExprNode>(rhs);
		return rhs_alignof != nullptr &&
			   lhs_alignof->is_type() == rhs_alignof->is_type() &&
			   equalDependentExpressionIdentityImpl(lhs_alignof->type_or_expr(), rhs_alignof->type_or_expr());
	}
	if (const NoexceptExprNode* lhs_noexcept = tryGetNode<NoexceptExprNode>(lhs)) {
		const NoexceptExprNode* rhs_noexcept = tryGetNode<NoexceptExprNode>(rhs);
		return rhs_noexcept != nullptr &&
			   equalDependentExpressionIdentityImpl(lhs_noexcept->expr(), rhs_noexcept->expr());
	}
	if (const OffsetofExprNode* lhs_offsetof = tryGetNode<OffsetofExprNode>(lhs)) {
		const OffsetofExprNode* rhs_offsetof = tryGetNode<OffsetofExprNode>(rhs);
		return rhs_offsetof != nullptr &&
			   equalTypeSpecifierIdentityImpl(lhs_offsetof->type_node(), rhs_offsetof->type_node()) &&
			   equalTokenValueRange(lhs_offsetof->member_path(), rhs_offsetof->member_path());
	}
	if (const TypeTraitExprNode* lhs_type_trait = tryGetNode<TypeTraitExprNode>(lhs)) {
		const TypeTraitExprNode* rhs_type_trait = tryGetNode<TypeTraitExprNode>(rhs);
		return rhs_type_trait != nullptr &&
			   lhs_type_trait->kind() == rhs_type_trait->kind() &&
			   lhs_type_trait->has_type() == rhs_type_trait->has_type() &&
			   (!lhs_type_trait->has_type() ||
				equalDependentExpressionIdentityImpl(lhs_type_trait->type_node(), rhs_type_trait->type_node())) &&
			   lhs_type_trait->has_second_type() == rhs_type_trait->has_second_type() &&
			   (!lhs_type_trait->has_second_type() ||
				equalDependentExpressionIdentityImpl(lhs_type_trait->second_type_node(), rhs_type_trait->second_type_node())) &&
			   equalAstRange(lhs_type_trait->additional_type_nodes(), rhs_type_trait->additional_type_nodes());
	}
	if (const NewExpressionNode* lhs_new = tryGetNode<NewExpressionNode>(lhs)) {
		const NewExpressionNode* rhs_new = tryGetNode<NewExpressionNode>(rhs);
		return rhs_new != nullptr &&
			   lhs_new->is_array() == rhs_new->is_array() &&
			   lhs_new->has_value_init() == rhs_new->has_value_init() &&
			   lhs_new->is_brace_init() == rhs_new->is_brace_init() &&
			   equalDependentExpressionIdentityImpl(lhs_new->type_node(), rhs_new->type_node()) &&
			   lhs_new->size_expr().has_value() == rhs_new->size_expr().has_value() &&
			   (!lhs_new->size_expr().has_value() ||
				equalDependentExpressionIdentityImpl(*lhs_new->size_expr(), *rhs_new->size_expr())) &&
			   equalAstRange(lhs_new->constructor_args(), rhs_new->constructor_args()) &&
			   equalAstRange(lhs_new->placement_args(), rhs_new->placement_args());
	}
	if (const DeleteExpressionNode* lhs_delete = tryGetNode<DeleteExpressionNode>(lhs)) {
		const DeleteExpressionNode* rhs_delete = tryGetNode<DeleteExpressionNode>(rhs);
		return rhs_delete != nullptr &&
			   lhs_delete->is_array() == rhs_delete->is_array() &&
			   equalDependentExpressionIdentityImpl(lhs_delete->expr(), rhs_delete->expr());
	}
	if (const StaticCastNode* lhs_static_cast = tryGetNode<StaticCastNode>(lhs)) {
		const StaticCastNode* rhs_static_cast = tryGetNode<StaticCastNode>(rhs);
		return rhs_static_cast != nullptr &&
			   equalTypeSpecifierIdentityImpl(lhs_static_cast->target_type(), rhs_static_cast->target_type()) &&
			   equalDependentExpressionIdentityImpl(lhs_static_cast->expr(), rhs_static_cast->expr());
	}
	if (const DynamicCastNode* lhs_dynamic_cast = tryGetNode<DynamicCastNode>(lhs)) {
		const DynamicCastNode* rhs_dynamic_cast = tryGetNode<DynamicCastNode>(rhs);
		return rhs_dynamic_cast != nullptr &&
			   equalTypeSpecifierIdentityImpl(lhs_dynamic_cast->target_type(), rhs_dynamic_cast->target_type()) &&
			   equalDependentExpressionIdentityImpl(lhs_dynamic_cast->expr(), rhs_dynamic_cast->expr());
	}
	if (const ConstCastNode* lhs_const_cast = tryGetNode<ConstCastNode>(lhs)) {
		const ConstCastNode* rhs_const_cast = tryGetNode<ConstCastNode>(rhs);
		return rhs_const_cast != nullptr &&
			   equalTypeSpecifierIdentityImpl(lhs_const_cast->target_type(), rhs_const_cast->target_type()) &&
			   equalDependentExpressionIdentityImpl(lhs_const_cast->expr(), rhs_const_cast->expr());
	}
	if (const ReinterpretCastNode* lhs_reinterpret_cast = tryGetNode<ReinterpretCastNode>(lhs)) {
		const ReinterpretCastNode* rhs_reinterpret_cast = tryGetNode<ReinterpretCastNode>(rhs);
		return rhs_reinterpret_cast != nullptr &&
			   equalTypeSpecifierIdentityImpl(lhs_reinterpret_cast->target_type(), rhs_reinterpret_cast->target_type()) &&
			   equalDependentExpressionIdentityImpl(lhs_reinterpret_cast->expr(), rhs_reinterpret_cast->expr());
	}
	if (const TypeidNode* lhs_typeid = tryGetNode<TypeidNode>(lhs)) {
		const TypeidNode* rhs_typeid = tryGetNode<TypeidNode>(rhs);
		return rhs_typeid != nullptr &&
			   lhs_typeid->is_type() == rhs_typeid->is_type() &&
			   equalDependentExpressionIdentityImpl(lhs_typeid->operand(), rhs_typeid->operand());
	}
	if (const LambdaExpressionNode* lhs_lambda = tryGetNode<LambdaExpressionNode>(lhs)) {
		const LambdaExpressionNode* rhs_lambda = tryGetNode<LambdaExpressionNode>(rhs);
		if (rhs_lambda == nullptr ||
			lhs_lambda->lambda_id() != rhs_lambda->lambda_id() ||
			lhs_lambda->is_mutable() != rhs_lambda->is_mutable() ||
			lhs_lambda->is_noexcept() != rhs_lambda->is_noexcept() ||
			lhs_lambda->is_constexpr() != rhs_lambda->is_constexpr() ||
			lhs_lambda->is_consteval() != rhs_lambda->is_consteval() ||
			lhs_lambda->template_params().size() != rhs_lambda->template_params().size() ||
			lhs_lambda->captures().size() != rhs_lambda->captures().size() ||
			lhs_lambda->parameters().size() != rhs_lambda->parameters().size() ||
			lhs_lambda->return_type().has_value() != rhs_lambda->return_type().has_value()) {
			return false;
		}
		for (size_t i = 0; i < lhs_lambda->template_params().size(); ++i) {
			if (lhs_lambda->template_params()[i] != rhs_lambda->template_params()[i]) {
				return false;
			}
		}
		for (size_t i = 0; i < lhs_lambda->captures().size(); ++i) {
			if (!equalLambdaCaptureIdentity(lhs_lambda->captures()[i], rhs_lambda->captures()[i])) {
				return false;
			}
		}
		if (!equalDeclarationAstRange(
				lhs_lambda->parameters(),
				rhs_lambda->parameters(),
				"equalDependentExpressionIdentity(lambda parameters)")) {
			return false;
		}
		if (lhs_lambda->return_type().has_value() &&
			!equalDependentExpressionIdentityImpl(*lhs_lambda->return_type(), *rhs_lambda->return_type())) {
			return false;
		}
		return true;
	}
	if (const TemplateParameterReferenceNode* lhs_template_param = tryGetNode<TemplateParameterReferenceNode>(lhs)) {
		const TemplateParameterReferenceNode* rhs_template_param = tryGetNode<TemplateParameterReferenceNode>(rhs);
		return rhs_template_param != nullptr &&
			   lhs_template_param->param_name() == rhs_template_param->param_name();
	}
	if (const FoldExpressionNode* lhs_fold = tryGetNode<FoldExpressionNode>(lhs)) {
		const FoldExpressionNode* rhs_fold = tryGetNode<FoldExpressionNode>(rhs);
		return rhs_fold != nullptr &&
			   lhs_fold->pack_name() == rhs_fold->pack_name() &&
			   lhs_fold->op() == rhs_fold->op() &&
			   lhs_fold->direction() == rhs_fold->direction() &&
			   lhs_fold->type() == rhs_fold->type() &&
			   lhs_fold->has_complex_pack_expr() == rhs_fold->has_complex_pack_expr() &&
			   (!lhs_fold->has_complex_pack_expr() ||
				equalDependentExpressionIdentityImpl(*lhs_fold->pack_expr(), *rhs_fold->pack_expr())) &&
			   lhs_fold->init_expr().has_value() == rhs_fold->init_expr().has_value() &&
			   (!lhs_fold->init_expr().has_value() ||
				equalDependentExpressionIdentityImpl(*lhs_fold->init_expr(), *rhs_fold->init_expr()));
	}
	if (const PackExpansionExprNode* lhs_pack_expansion = tryGetNode<PackExpansionExprNode>(lhs)) {
		const PackExpansionExprNode* rhs_pack_expansion = tryGetNode<PackExpansionExprNode>(rhs);
		return rhs_pack_expansion != nullptr &&
			   equalDependentExpressionIdentityImpl(lhs_pack_expansion->pattern(), rhs_pack_expansion->pattern());
	}
	if (const InitializerListConstructionNode* lhs_init_list = tryGetNode<InitializerListConstructionNode>(lhs)) {
		const InitializerListConstructionNode* rhs_init_list = tryGetNode<InitializerListConstructionNode>(rhs);
		return rhs_init_list != nullptr &&
			   equalDependentExpressionIdentityImpl(lhs_init_list->element_type(), rhs_init_list->element_type()) &&
			   equalDependentExpressionIdentityImpl(lhs_init_list->target_type(), rhs_init_list->target_type()) &&
			   equalAstRange(lhs_init_list->elements(), rhs_init_list->elements());
	}
	if (const ThrowExpressionNode* lhs_throw = tryGetNode<ThrowExpressionNode>(lhs)) {
		const ThrowExpressionNode* rhs_throw = tryGetNode<ThrowExpressionNode>(rhs);
		return rhs_throw != nullptr &&
			   lhs_throw->is_rethrow() == rhs_throw->is_rethrow() &&
			   lhs_throw->expression().has_value() == rhs_throw->expression().has_value() &&
			   (!lhs_throw->expression().has_value() ||
				equalDependentExpressionIdentityImpl(*lhs_throw->expression(), *rhs_throw->expression()));
	}
	if (const CallExprNode* lhs_call = tryGetNode<CallExprNode>(lhs)) {
		const CallExprNode* rhs_call = tryGetNode<CallExprNode>(rhs);
		return rhs_call != nullptr &&
			   equalCalleeDescriptorIdentity(lhs_call->callee(), rhs_call->callee()) &&
			   lhs_call->has_receiver() == rhs_call->has_receiver() &&
			   (!lhs_call->has_receiver() ||
				equalDependentExpressionIdentityImpl(lhs_call->receiver(), rhs_call->receiver())) &&
			   equalAstRange(lhs_call->arguments(), rhs_call->arguments()) &&
			   equalAstRange(lhs_call->template_arguments(), rhs_call->template_arguments());
	}
	if (const ConstructorCallNode* lhs_ctor = tryGetNode<ConstructorCallNode>(lhs)) {
		const ConstructorCallNode* rhs_ctor = tryGetNode<ConstructorCallNode>(rhs);
		return rhs_ctor != nullptr &&
			   equalTypeSpecifierIdentityImpl(lhs_ctor->type_node(), rhs_ctor->type_node()) &&
			   equalAstRange(lhs_ctor->arguments(), rhs_ctor->arguments());
	}
	if (!isSupportedDependentExpressionIdentityNode(lhs)) {
		throwUnsupportedDependentExpressionIdentityNode("equalDependentExpressionIdentity(lhs)", lhs);
	}
	if (!isSupportedDependentExpressionIdentityNode(rhs)) {
		throwUnsupportedDependentExpressionIdentityNode("equalDependentExpressionIdentity(rhs)", rhs);
	}
	return false;
}

size_t hashDependentExpressionIdentityImpl(const ASTNode& node) {
	if (!node.has_value()) {
		return std::hash<uint8_t>{}(0);
	}
	if (node.is<TypeSpecifierNode>()) {
		size_t seed = std::hash<uint8_t>{}(1);
		hashCombine(seed, hashTypeSpecifierIdentityImpl(node.as<TypeSpecifierNode>()));
		return seed;
	}
	if (const IdentifierNode* identifier = tryGetIdentifier(node)) {
		size_t seed = std::hash<uint8_t>{}(2);
		hashCombineStringView(seed, identifier->name());
		return seed;
	}
	if (const QualifiedIdentifierNode* qualified = tryGetNode<QualifiedIdentifierNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(3);
		hashCombine(seed, std::hash<NamespaceHandle>{}(qualified->namespace_handle()));
		hashCombineStringView(seed, qualified->name());
		hashCombine(seed, std::hash<bool>{}(qualified->has_template_arguments()));
		if (qualified->has_template_arguments()) {
			hashCombine(seed, hashAstRange(qualified->template_arguments()));
		}
		hashCombine(seed, std::hash<bool>{}(qualified->hasDependentQualifiedName()));
		if (qualified->hasDependentQualifiedName()) {
			hashCombine(seed, hashDependentQualifiedNameRecord(*qualified->dependentQualifiedName()));
		}
		return seed;
	}
	if (const NumericLiteralNode* numeric = tryGetNode<NumericLiteralNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(4);
		hashCombine(seed, hashNumericLiteralIdentity(*numeric));
		return seed;
	}
	if (const BoolLiteralNode* bool_literal = tryGetNode<BoolLiteralNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(5);
		hashCombine(seed, std::hash<bool>{}(bool_literal->value()));
		return seed;
	}
	if (const StringLiteralNode* string_literal = tryGetNode<StringLiteralNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(6);
		hashCombineStringView(seed, string_literal->value());
		return seed;
	}
	if (const BinaryOperatorNode* binary = tryGetNode<BinaryOperatorNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(7);
		hashCombineStringView(seed, binary->op());
		hashCombine(seed, hashDependentExpressionIdentityImpl(binary->get_lhs()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(binary->get_rhs()));
		return seed;
	}
	if (const UnaryOperatorNode* unary = tryGetNode<UnaryOperatorNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(8);
		hashCombineStringView(seed, unary->op());
		hashCombine(seed, std::hash<bool>{}(unary->is_prefix()));
		hashCombine(seed, std::hash<bool>{}(unary->is_builtin_addressof()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(unary->get_operand()));
		return seed;
	}
	if (const TernaryOperatorNode* ternary = tryGetNode<TernaryOperatorNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(9);
		hashCombine(seed, hashDependentExpressionIdentityImpl(ternary->condition()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(ternary->true_expr()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(ternary->false_expr()));
		return seed;
	}
	if (const MemberAccessNode* member = tryGetNode<MemberAccessNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(10);
		hashCombine(seed, std::hash<bool>{}(member->is_arrow()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(member->object()));
		hashCombineStringView(seed, member->member_name());
		return seed;
	}
	if (const PointerToMemberAccessNode* member_ptr = tryGetNode<PointerToMemberAccessNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(11);
		hashCombine(seed, std::hash<bool>{}(member_ptr->is_arrow()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(member_ptr->object()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(member_ptr->member_pointer()));
		return seed;
	}
	if (const PseudoDestructorCallNode* pseudo_dtor = tryGetNode<PseudoDestructorCallNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(12);
		hashCombine(seed, std::hash<bool>{}(pseudo_dtor->is_arrow_access()));
		hashCombine(seed, std::hash<StringHandle>{}(pseudo_dtor->qualified_type_name()));
		hashCombineStringView(seed, pseudo_dtor->type_name());
		hashCombine(seed, hashDependentExpressionIdentityImpl(pseudo_dtor->object()));
		return seed;
	}
	if (const ArraySubscriptNode* subscript = tryGetNode<ArraySubscriptNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(13);
		hashCombine(seed, hashDependentExpressionIdentityImpl(subscript->array_expr()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(subscript->index_expr()));
		return seed;
	}
	if (const SizeofExprNode* sizeof_expr = tryGetNode<SizeofExprNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(14);
		hashCombine(seed, std::hash<bool>{}(sizeof_expr->is_type()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(sizeof_expr->type_or_expr()));
		return seed;
	}
	if (const SizeofPackNode* sizeof_pack = tryGetNode<SizeofPackNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(15);
		hashCombineStringView(seed, sizeof_pack->pack_name());
		return seed;
	}
	if (const AlignofExprNode* alignof_expr = tryGetNode<AlignofExprNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(16);
		hashCombine(seed, std::hash<bool>{}(alignof_expr->is_type()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(alignof_expr->type_or_expr()));
		return seed;
	}
	if (const NoexceptExprNode* noexcept_expr = tryGetNode<NoexceptExprNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(17);
		hashCombine(seed, hashDependentExpressionIdentityImpl(noexcept_expr->expr()));
		return seed;
	}
	if (const OffsetofExprNode* offsetof_expr = tryGetNode<OffsetofExprNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(18);
		hashCombine(seed, hashTypeSpecifierIdentityImpl(offsetof_expr->type_node()));
		hashCombine(seed, hashTokenValueRange(offsetof_expr->member_path()));
		return seed;
	}
	if (const TypeTraitExprNode* type_trait = tryGetNode<TypeTraitExprNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(19);
		hashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(type_trait->kind())));
		hashCombine(seed, std::hash<bool>{}(type_trait->has_type()));
		if (type_trait->has_type()) {
			hashCombine(seed, hashDependentExpressionIdentityImpl(type_trait->type_node()));
		}
		hashCombine(seed, std::hash<bool>{}(type_trait->has_second_type()));
		if (type_trait->has_second_type()) {
			hashCombine(seed, hashDependentExpressionIdentityImpl(type_trait->second_type_node()));
		}
		hashCombine(seed, hashAstRange(type_trait->additional_type_nodes()));
		return seed;
	}
	if (const NewExpressionNode* new_expr = tryGetNode<NewExpressionNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(20);
		hashCombine(seed, hashDependentExpressionIdentityImpl(new_expr->type_node()));
		hashCombine(seed, std::hash<bool>{}(new_expr->is_array()));
		hashCombine(seed, std::hash<bool>{}(new_expr->has_value_init()));
		hashCombine(seed, std::hash<bool>{}(new_expr->is_brace_init()));
		hashCombine(seed, std::hash<bool>{}(new_expr->size_expr().has_value()));
		if (new_expr->size_expr().has_value()) {
			hashCombine(seed, hashDependentExpressionIdentityImpl(*new_expr->size_expr()));
		}
		hashCombine(seed, hashAstRange(new_expr->constructor_args()));
		hashCombine(seed, hashAstRange(new_expr->placement_args()));
		return seed;
	}
	if (const DeleteExpressionNode* delete_expr = tryGetNode<DeleteExpressionNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(21);
		hashCombine(seed, std::hash<bool>{}(delete_expr->is_array()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(delete_expr->expr()));
		return seed;
	}
	if (const StaticCastNode* static_cast_expr = tryGetNode<StaticCastNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(22);
		hashCombine(seed, hashTypeSpecifierIdentityImpl(static_cast_expr->target_type()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(static_cast_expr->expr()));
		return seed;
	}
	if (const DynamicCastNode* dynamic_cast_expr = tryGetNode<DynamicCastNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(23);
		hashCombine(seed, hashTypeSpecifierIdentityImpl(dynamic_cast_expr->target_type()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(dynamic_cast_expr->expr()));
		return seed;
	}
	if (const ConstCastNode* const_cast_expr = tryGetNode<ConstCastNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(24);
		hashCombine(seed, hashTypeSpecifierIdentityImpl(const_cast_expr->target_type()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(const_cast_expr->expr()));
		return seed;
	}
	if (const ReinterpretCastNode* reinterpret_cast_expr = tryGetNode<ReinterpretCastNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(25);
		hashCombine(seed, hashTypeSpecifierIdentityImpl(reinterpret_cast_expr->target_type()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(reinterpret_cast_expr->expr()));
		return seed;
	}
	if (const TypeidNode* typeid_expr = tryGetNode<TypeidNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(26);
		hashCombine(seed, std::hash<bool>{}(typeid_expr->is_type()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(typeid_expr->operand()));
		return seed;
	}
	if (const LambdaExpressionNode* lambda = tryGetNode<LambdaExpressionNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(27);
		hashCombine(seed, std::hash<size_t>{}(lambda->lambda_id()));
		hashCombine(seed, std::hash<bool>{}(lambda->is_mutable()));
		hashCombine(seed, std::hash<bool>{}(lambda->is_noexcept()));
		hashCombine(seed, std::hash<bool>{}(lambda->is_constexpr()));
		hashCombine(seed, std::hash<bool>{}(lambda->is_consteval()));
		for (std::string_view param : lambda->template_params()) {
			hashCombineStringView(seed, param);
		}
		for (const LambdaCaptureNode& capture : lambda->captures()) {
			hashCombine(seed, hashLambdaCaptureIdentity(capture));
		}
		hashCombine(
			seed,
			hashDeclarationAstRange(
				lambda->parameters(),
				"hashDependentExpressionIdentity(lambda parameters)"));
		hashCombine(seed, std::hash<bool>{}(lambda->return_type().has_value()));
		if (lambda->return_type().has_value()) {
			hashCombine(seed, hashDependentExpressionIdentityImpl(*lambda->return_type()));
		}
		return seed;
	}
	if (const TemplateParameterReferenceNode* template_param = tryGetNode<TemplateParameterReferenceNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(28);
		hashCombine(seed, std::hash<StringHandle>{}(template_param->param_name()));
		return seed;
	}
	if (const FoldExpressionNode* fold = tryGetNode<FoldExpressionNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(29);
		hashCombineStringView(seed, fold->pack_name());
		hashCombineStringView(seed, fold->op());
		hashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(fold->direction())));
		hashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(fold->type())));
		hashCombine(seed, std::hash<bool>{}(fold->has_complex_pack_expr()));
		if (fold->has_complex_pack_expr()) {
			hashCombine(seed, hashDependentExpressionIdentityImpl(*fold->pack_expr()));
		}
		hashCombine(seed, std::hash<bool>{}(fold->init_expr().has_value()));
		if (fold->init_expr().has_value()) {
			hashCombine(seed, hashDependentExpressionIdentityImpl(*fold->init_expr()));
		}
		return seed;
	}
	if (const PackExpansionExprNode* pack_expansion = tryGetNode<PackExpansionExprNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(30);
		hashCombine(seed, hashDependentExpressionIdentityImpl(pack_expansion->pattern()));
		return seed;
	}
	if (const InitializerListConstructionNode* init_list = tryGetNode<InitializerListConstructionNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(31);
		hashCombine(seed, hashDependentExpressionIdentityImpl(init_list->element_type()));
		hashCombine(seed, hashDependentExpressionIdentityImpl(init_list->target_type()));
		hashCombine(seed, hashAstRange(init_list->elements()));
		return seed;
	}
	if (const ThrowExpressionNode* throw_expr = tryGetNode<ThrowExpressionNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(32);
		hashCombine(seed, std::hash<bool>{}(throw_expr->is_rethrow()));
		hashCombine(seed, std::hash<bool>{}(throw_expr->expression().has_value()));
		if (throw_expr->expression().has_value()) {
			hashCombine(seed, hashDependentExpressionIdentityImpl(*throw_expr->expression()));
		}
		return seed;
	}
	if (const CallExprNode* call = tryGetNode<CallExprNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(33);
		hashCombine(seed, hashCalleeDescriptorIdentity(call->callee()));
		hashCombine(seed, std::hash<bool>{}(call->has_receiver()));
		if (call->has_receiver()) {
			hashCombine(seed, hashDependentExpressionIdentityImpl(call->receiver()));
		}
		hashCombine(seed, hashAstRange(call->arguments()));
		hashCombine(seed, hashAstRange(call->template_arguments()));
		return seed;
	}
	if (const ConstructorCallNode* ctor = tryGetNode<ConstructorCallNode>(node)) {
		size_t seed = std::hash<uint8_t>{}(34);
		hashCombine(seed, hashTypeSpecifierIdentityImpl(ctor->type_node()));
		hashCombine(seed, hashAstRange(ctor->arguments()));
		return seed;
	}
	throwUnsupportedDependentExpressionIdentityNode("hashDependentExpressionIdentity", node);
}

} // namespace

bool equalDependentExpressionIdentity(const ASTNode& lhs, const ASTNode& rhs) {
	return equalDependentExpressionIdentityImpl(lhs, rhs);
}

size_t hashDependentExpressionIdentity(const ASTNode& node) {
	return hashDependentExpressionIdentityImpl(node);
}

bool NonTypeValueIdentity::operator==(const NonTypeValueIdentity& other) const {
	if (is_dependent != other.is_dependent)
		return false;
	if (is_dependent) {
		if (dependent_expression.has_value() || other.dependent_expression.has_value()) {
			return dependent_expression.has_value() == other.dependent_expression.has_value() &&
				   equalValueTypeIdentity(value_type_index, other.value_type_index) &&
				   (!dependent_expression.has_value() ||
					equalDependentExpressionIdentity(*dependent_expression, *other.dependent_expression));
		}
		return dependent_name == other.dependent_name;
	}
	if (kind != other.kind ||
		!equalValueTypeIdentity(value_type_index, other.value_type_index)) {
		return false;
	}
	switch (kind) {
	case NonTypeValueIdentityKind::Integral:
	case NonTypeValueIdentityKind::Floating:
	case NonTypeValueIdentityKind::StructuralClass:
	case NonTypeValueIdentityKind::Unsupported:
		return value == other.value;
	case NonTypeValueIdentityKind::Nullptr:
		return true;
	case NonTypeValueIdentityKind::ObjectPointer:
		return entity_name == other.entity_name &&
			   pointer_offset == other.pointer_offset;
	case NonTypeValueIdentityKind::Reference:
	case NonTypeValueIdentityKind::FunctionPointer:
		return entity_name == other.entity_name;
	case NonTypeValueIdentityKind::MemberPointer:
		if (isNullMemberPointer() && other.isNullMemberPointer()) {
			return true;
		}
		return member_name == other.member_name &&
			   member_class_name == other.member_class_name &&
			   function_signature.has_value() == other.function_signature.has_value() &&
			   (!function_signature.has_value() ||
				equalFunctionSignatureIdentity(*function_signature, *other.function_signature)) &&
			   value == other.value;
	}
	return false;
}

size_t NonTypeValueIdentity::hash() const {
	size_t h = std::hash<bool>{}(is_dependent);
	if (is_dependent) {
		if (dependent_expression.has_value()) {
			h ^= hashValueTypeIdentity(value_type_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
			h ^= hashDependentExpressionIdentity(*dependent_expression) + 0x9e3779b9 + (h << 6) + (h >> 2);
		} else if (dependent_name.isValid()) {
			h ^= std::hash<StringHandle>{}(dependent_name) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		return h;
	}
	h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(kind)) + 0x9e3779b9 + (h << 6) + (h >> 2);
	if (!(kind == NonTypeValueIdentityKind::MemberPointer && !member_name.isValid())) {
		h ^= std::hash<int64_t>{}(value) + 0x9e3779b9 + (h << 6) + (h >> 2);
	}
	if (entity_name.isValid()) {
		h ^= std::hash<StringHandle>{}(entity_name) + 0x9e3779b9 + (h << 6) + (h >> 2);
	}
	if (member_name.isValid()) {
		h ^= std::hash<StringHandle>{}(member_name) + 0x9e3779b9 + (h << 6) + (h >> 2);
	} else if (kind == NonTypeValueIdentityKind::MemberPointer) {
		h ^= std::hash<uint8_t>{}(0x5a) + 0x9e3779b9 + (h << 6) + (h >> 2);
	}
	if (member_class_name.isValid()) {
		h ^= std::hash<StringHandle>{}(member_class_name) + 0x9e3779b9 + (h << 6) + (h >> 2);
	}
	if (kind == NonTypeValueIdentityKind::ObjectPointer) {
		h ^= std::hash<int64_t>{}(pointer_offset) + 0x9e3779b9 + (h << 6) + (h >> 2);
	}
	if (function_signature.has_value()) {
		h ^= hashFunctionSignatureIdentity(*function_signature) + 0x9e3779b9 + (h << 6) + (h >> 2);
	}
	h ^= hashValueTypeIdentity(value_type_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
	return h;
}

std::string NonTypeValueIdentity::toString() const {
	if (is_dependent && dependent_expression.has_value()) {
		char buf[17];
		snprintf(buf, sizeof(buf), "%016zx", hashDependentExpressionIdentity(*dependent_expression));
		StringBuilder builder;
		builder.append("dep_expr$");
		builder.append(std::string_view(buf));
		return std::string(builder.commit());
	}
	if (is_dependent && dependent_name.isValid()) {
		return std::string(StringTable::getStringView(dependent_name));
	}
	if (valueTypeCategory() == TypeCategory::Bool) {
		return value != 0 ? "true" : "false";
	}
	if (kind == NonTypeValueIdentityKind::Nullptr) {
		return "nullptr";
	}
	if (entity_name.isValid()) {
		std::string result;
		if (kind == NonTypeValueIdentityKind::ObjectPointer ||
			kind == NonTypeValueIdentityKind::FunctionPointer) {
			result += "&";
		}
		result += StringTable::getStringView(entity_name);
		if (pointer_offset != 0) {
			result += "+";
			result += std::to_string(pointer_offset);
		}
		return result;
	}
	if (member_name.isValid()) {
		std::string result = "&";
		if (member_class_name.isValid()) {
			result += StringTable::getStringView(member_class_name);
			result += "::";
		}
		result += StringTable::getStringView(member_name);
		return result;
	}
	if (isNullMemberPointer()) {
		return "nullptr";
	}
	return std::to_string(value);
}

} // namespace FlashCpp
