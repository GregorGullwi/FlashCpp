#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "AstNodeTypes.h"
#include "CanonicalTypeAdapter.h"
#include "ExpressionStructure.h"
#include "TemplateRegistry_Lazy.h"

namespace FlashCpp::detail {

enum class ConstraintSubsumptionOrdering : uint8_t {
	Neither,
	Equivalent,
	FirstSubsumesSecond,
	SecondSubsumesFirst,
	Unsupported,
};

namespace ConstraintSubsumption {

enum class FormulaKind : uint8_t {
	Atom,
	Conjunction,
	Disjunction,
};

enum class MappingKind : uint8_t {
	TemplateParameter,
	CanonicalType,
	// A dependent type built from a template parameter plus outer pointer,
	// reference, cv, or array decoration (for example `T*` or `const T&`).
	// It is kept distinct from the bare parameter mapping so decorated forms do
	// not collapse onto the undecorated parameter during subsumption.
	DependentType,
};

struct ParameterMapping {
	MappingKind kind;
	TemplateParameterKind parameter_kind;
	uint32_t parameter_index;
	TypeId type_id;
	uint8_t pointer_depth;
	uint8_t is_array;
	CVQualifier cv_qualifier;
	ReferenceQualifier ref_qualifier;

	static ParameterMapping templateParameter(TemplateParameterKind parameter_kind, uint32_t parameter_index) {
		return ParameterMapping{
			MappingKind::TemplateParameter,
			parameter_kind, parameter_index, TypeId{}, 0, 0, CVQualifier::None, ReferenceQualifier::None};
	}

	static ParameterMapping canonicalType(TypeId type_id) {
		return ParameterMapping{
			MappingKind::CanonicalType,
			TemplateParameterKind::Type, 0, type_id, 0, 0, CVQualifier::None, ReferenceQualifier::None};
	}

	friend bool operator==(const ParameterMapping&, const ParameterMapping&) = default;
};

struct AtomicConstraint {
	Token source_token;
	std::vector<ParameterMapping> parameter_mapping;
};

struct FormulaNode {
	FormulaKind kind;
	size_t left;
	size_t right;
	size_t atom;
};

struct Formula {
	std::vector<FormulaNode> nodes;
	std::vector<AtomicConstraint> atoms;
	size_t root;
};

struct ScopeEntry {
	StringHandle name;
	ParameterMapping mapping;
};

struct Scope {
	size_t begin;
	size_t size;
};

inline bool sameSourceToken(const Token& lhs, const Token& rhs) {
	return lhs.type() == rhs.type() &&
		lhs.file_index() == rhs.file_index() &&
		lhs.line() == rhs.line() && lhs.column() == rhs.column() && lhs.value() == rhs.value();
}

inline bool sameAtomicConstraint(const AtomicConstraint& lhs, const AtomicConstraint& rhs) {
	return sameSourceToken(lhs.source_token, rhs.source_token) && lhs.parameter_mapping == rhs.parameter_mapping;
}

template <typename Expression>
std::optional<Token> tryGetSourceToken(const Expression& expression) {
	using ExpressionType = std::remove_cvref_t<Expression>;
	if constexpr (std::is_same_v<ExpressionType, IdentifierNode>) {
		return expression.identifier_token();
	} else if constexpr (std::is_same_v<ExpressionType, QualifiedIdentifierNode>) {
		return expression.identifier_token();
	} else if constexpr (std::is_same_v<ExpressionType, NumericLiteralNode> ||
		std::is_same_v<ExpressionType, StringLiteralNode> || std::is_same_v<ExpressionType, BoolLiteralNode>) {
		return expression.source_token();
	} else if constexpr (std::is_same_v<ExpressionType, BinaryOperatorNode> ||
		std::is_same_v<ExpressionType, UnaryOperatorNode> ||
		std::is_same_v<ExpressionType, TernaryOperatorNode> ||
		std::is_same_v<ExpressionType, FoldExpressionNode> || std::is_same_v<ExpressionType, PackExpansionExprNode>) {
		return expression.get_token();
	} else if constexpr (std::is_same_v<ExpressionType, ConstructorCallNode> ||
		std::is_same_v<ExpressionType, CallExprNode>) {
		return expression.called_from();
	} else if constexpr (std::is_same_v<ExpressionType, MemberAccessNode>) {
		return expression.member_token();
	} else if constexpr (std::is_same_v<ExpressionType, PointerToMemberAccessNode>) {
		return expression.operator_token();
	} else if constexpr (std::is_same_v<ExpressionType, PseudoDestructorCallNode>) {
		return expression.type_name_token();
	} else if constexpr (std::is_same_v<ExpressionType, ArraySubscriptNode>) {
		return expression.bracket_token();
	} else if constexpr (std::is_same_v<ExpressionType, SizeofExprNode> ||
		std::is_same_v<ExpressionType, SizeofPackNode>) {
		return expression.sizeof_token();
	} else if constexpr (std::is_same_v<ExpressionType, AlignofExprNode>) {
		return expression.alignof_token();
	} else if constexpr (std::is_same_v<ExpressionType, OffsetofExprNode>) {
		return expression.offsetof_token();
	} else if constexpr (std::is_same_v<ExpressionType, TypeTraitExprNode>) {
		return expression.trait_token();
	} else if constexpr (std::is_same_v<ExpressionType, StaticCastNode> ||
		std::is_same_v<ExpressionType, DynamicCastNode> ||
		std::is_same_v<ExpressionType, ConstCastNode> || std::is_same_v<ExpressionType, ReinterpretCastNode>) {
		return expression.cast_token();
	} else if constexpr (std::is_same_v<ExpressionType, TypeidNode>) {
		return expression.typeid_token();
	} else if constexpr (std::is_same_v<ExpressionType, LambdaExpressionNode>) {
		return expression.lambda_token();
	} else if constexpr (std::is_same_v<ExpressionType, TemplateParameterReferenceNode>) {
		return expression.token();
	} else if constexpr (std::is_same_v<ExpressionType, NoexceptExprNode>) {
		return expression.noexcept_token();
	} else if constexpr (std::is_same_v<ExpressionType, ThrowExpressionNode>) {
		return expression.throw_token();
	} else if constexpr (std::is_same_v<ExpressionType, RequiresExpressionNode>) {
		return expression.requires_token();
	} else if constexpr (std::is_same_v<ExpressionType, InitializerListConstructionNode>) {
		return expression.called_from();
	} else {
		return std::nullopt;
	}
}

template <size_t... Index>
std::optional<Token> tryGetDirectSourceToken(const ASTNode& expression, std::index_sequence<Index...>) {
	std::optional<Token> source_token;
	const bool found = ([&]() {
		using ExpressionType = std::variant_alternative_t<Index, ExpressionNode>;
		if (!expression.is<ExpressionType>()) {
			return false;
		}
		source_token = tryGetSourceToken(expression.as<ExpressionType>());
		return true;
	}() || ...);
	if (!found) {
		return std::nullopt;
	}
	return source_token;
}

inline std::optional<Token> tryGetSourceToken(const ASTNode& expression) {
	if (expression.is<ExpressionNode>()) {
		return std::visit([](const auto& expression_node) {
			return tryGetSourceToken(expression_node);
		}, expression.as<ExpressionNode>());
	}
	return tryGetDirectSourceToken(expression, std::make_index_sequence<std::variant_size_v<ExpressionNode>>{});
}

inline ASTNode unwrapExpressionNode(const ASTNode& expression) {
	if (!expression.is<ExpressionNode>()) {
		return expression;
	}
	ASTNode unwrapped;
	std::visit([&unwrapped](const auto& expression_node) {
		unwrapped = ASTNode(&expression_node);
	}, expression.as<ExpressionNode>());
	return unwrapped;
}

struct ConceptUse {
	const ConceptDeclarationNode* declaration;
	std::span<const ASTNode> arguments;
};

inline std::optional<ConceptUse> tryGetConceptUse(const ASTNode& expression) {
	const ASTNode unwrapped = unwrapExpressionNode(expression);
	std::string_view concept_name;
	std::span<const ASTNode> arguments;
	if (unwrapped.is<CallExprNode>()) {
		const CallExprNode& call = unwrapped.as<CallExprNode>();
		concept_name = call.called_from().value();
		arguments = call.template_arguments();
	} else if (unwrapped.is<IdentifierNode>()) {
		concept_name = unwrapped.as<IdentifierNode>().name();
	} else {
		return std::nullopt;
	}
	const std::optional<ASTNode> concept_node = gConceptRegistry.lookupConcept(concept_name);
	if (!concept_node.has_value() || !concept_node->is<ConceptDeclarationNode>()) {
		return std::nullopt;
	}
	return ConceptUse{
		&concept_node->as<ConceptDeclarationNode>(), arguments};
}

class FormulaBuilder {
public:
	FormulaBuilder(
		CanonicalTypeTable& canonical_types,
		const TemplateFunctionDeclarationNode& function_template)
		: canonical_types_(canonical_types), function_template_(function_template) {}

	std::optional<Formula> build() {
		const TemplateParameterVector& parameters = function_template_.template_parameters();
		std::vector<ParameterMapping> parameter_mappings;
		parameter_mappings.reserve(parameters.size());
		for (size_t index = 0; index < parameters.size(); ++index) {
			parameter_mappings.push_back(ParameterMapping::templateParameter(
				parameters[index].kind(), static_cast<uint32_t>(index)));
		}
		const std::optional<size_t> function_scope = appendScope(parameters, parameter_mappings);
		if (!function_scope.has_value()) {
			return std::nullopt;
		}

		std::vector<size_t> roots;
		if (function_template_.has_requires_clause()) {
			const RequiresClauseNode& requires_clause = function_template_.requires_clause()->as<RequiresClauseNode>();
			const std::optional<size_t> root = buildExpression(requires_clause.constraint_expr(), *function_scope, nullptr);
			if (!root.has_value()) {
				return std::nullopt;
			}
			roots.push_back(*root);
		}

		for (size_t parameter_index = 0;
			 parameter_index < parameters.size();
			 ++parameter_index) {
			const TemplateParameterNode& parameter = parameters[parameter_index];
			if (!parameter.has_concept_constraint()) {
				continue;
			}
			if (parameter.kind() != TemplateParameterKind::Type || !parameter.nameHandle().isValid()) {
				return std::nullopt;
			}
			const std::optional<ASTNode> concept_node = gConceptRegistry.lookupConcept(parameter.concept_constraint());
			if (!concept_node.has_value() || !concept_node->is<ConceptDeclarationNode>()) {
				return std::nullopt;
			}
			const ConceptDeclarationNode& concept_declaration = concept_node->as<ConceptDeclarationNode>();
			std::vector<ParameterMapping> mappings;
			mappings.reserve(parameter.concept_args().size() + 1);
			mappings.push_back(ParameterMapping::templateParameter(parameter.kind(), static_cast<uint32_t>(parameter_index)));
			for (const ASTNode& argument : parameter.concept_args()) {
				const std::optional<ParameterMapping> mapping = mapArgument(argument, *function_scope);
				if (!mapping.has_value()) {
					return std::nullopt;
				}
				mappings.push_back(*mapping);
			}
			const std::optional<size_t> root = buildConcept(concept_declaration, mappings);
			if (!root.has_value()) {
				return std::nullopt;
			}
			roots.push_back(*root);
		}

		if (roots.empty()) {
			return std::nullopt;
		}
		size_t root = roots.front();
		for (size_t index = 1; index < roots.size(); ++index) {
			root = appendBinary(FormulaKind::Conjunction, root, roots[index]);
		}
		formula_.root = root;
		return std::move(formula_);
	}

private:
	enum class TaskKind : uint8_t {
		Visit,
		Combine,
		ExitConcept,
	};

	struct BuildTask {
		TaskKind kind;
		ASTNode expression;
		size_t scope;
		FormulaKind formula_kind;
		const ConceptDeclarationNode* concept_declaration;
	};

	std::optional<size_t> appendScope(
		std::span<const TemplateParameterNode> parameters, std::span<const ParameterMapping> mappings) {
		if (parameters.size() != mappings.size()) {
			return std::nullopt;
		}
		const size_t begin = scope_entries_.size();
		for (size_t index = 0; index < parameters.size(); ++index) {
			const TemplateParameterNode& parameter = parameters[index];
			if (!parameter.nameHandle().isValid() || parameter.is_variadic()) {
				return std::nullopt;
			}
			const ParameterMapping& mapping = mappings[index];
			if (parameter.kind() == TemplateParameterKind::Template ||
				(mapping.kind == MappingKind::TemplateParameter &&
				 mapping.parameter_kind != parameter.kind()) ||
				(mapping.kind == MappingKind::CanonicalType && parameter.kind() != TemplateParameterKind::Type)) {
				return std::nullopt;
			}
			scope_entries_.push_back(ScopeEntry{
				parameter.nameHandle(), mapping});
		}
		const size_t scope = scopes_.size();
		scopes_.push_back(Scope{begin, parameters.size()});
		return scope;
	}

	std::optional<size_t> appendScope(
		const TemplateParameterVector& parameters, std::span<const ParameterMapping> mappings) {
		return appendScope(std::span<const TemplateParameterNode>(parameters.data(), parameters.size()), mappings);
	}

	std::optional<size_t> lookupScopeEntry(size_t scope, StringHandle name) const {
		if (!name.isValid()) {
			return std::nullopt;
		}
		const Scope& current_scope = scopes_[scope];
		for (size_t index = current_scope.begin;
			 index < current_scope.begin + current_scope.size;
			 ++index) {
			if (scope_entries_[index].name == name) {
				return index;
			}
		}
		return std::nullopt;
	}

	std::optional<ParameterMapping> lookupMapping(size_t scope, StringHandle name) const {
		const std::optional<size_t> entry = lookupScopeEntry(scope, name);
		if (!entry.has_value()) {
			return std::nullopt;
		}
		return scope_entries_[*entry].mapping;
	}

	bool isPlainTemplateParameterType(const TypeSpecifierNode& type) const {
		return !type.is_pointer() && !type.is_reference() && !type.is_array() &&
			!type.has_ordered_declarator() &&
			type.cv_qualifier() == CVQualifier::None &&
			type.qualifier() == TypeQualifier::None && !type.has_member_class() && !type.has_function_signature();
	}

	std::optional<std::vector<ParameterMapping>> tryCollectAtomicParameterMapping(
		const ASTNode& root_expression, size_t scope) const {
		const Scope& current_scope = scopes_[scope];
		std::vector<uint8_t> referenced(current_scope.size, 0);
		std::vector<ASTNode> worklist{root_expression};
		auto mark_scope_entry = [&](std::optional<size_t> entry) {
			if (!entry.has_value()) {
				return false;
			}
			referenced[*entry - current_scope.begin] = 1;
			return true;
		};
		while (!worklist.empty()) {
			const ASTNode expression = unwrapExpressionNode(worklist.back());
			worklist.pop_back();
			if (!expression.has_value()) {
				continue;
			}
			if (expression.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& type = expression.as<TypeSpecifierNode>();
				if (type.has_template_parameter_identity() || type.has_template_parameter_decl()) {
					// Decorated dependent forms such as `T*`, `const T`, `T&`,
					// `T[N]`, or a nested dependent structure cannot be reduced
					// to the bare template parameter; the whole atomic
					// constraint is unsupported instead of being mis-mapped.
					if (!isPlainTemplateParameterType(type)) {
						return std::nullopt;
					}
					std::optional<size_t> entry;
					if (type.has_template_parameter_identity()) {
						entry = lookupScopeEntry(scope, type.template_parameter_name());
					}
					if (!entry.has_value() &&
						type.has_template_parameter_decl() && type.template_decl_id() == function_template_.template_decl_id()) {
						for (size_t index = current_scope.begin;
							 index < current_scope.begin + current_scope.size;
							 ++index) {
							const ParameterMapping& mapping = scope_entries_[index].mapping;
							if (mapping.kind == MappingKind::TemplateParameter &&
								mapping.parameter_index == type.template_parameter_index()) {
								entry = index;
								break;
							}
						}
					}
					if (!mark_scope_entry(entry)) {
						return std::nullopt;
					}
				} else if (typeSpecStillUsesDependentPlaceholder(type)) {
					// A dependent type structure (for example `X<T>` or
					// `T::type`) that is not a bare template parameter would
					// otherwise contribute no mapping and silently equate with
					// unrelated constants. Fail closed.
					return std::nullopt;
				}
			} else if (
				expression.is<IdentifierNode>() &&
				expression.as<IdentifierNode>().binding() == IdentifierBinding::TemplateParameter) {
				if (!mark_scope_entry(lookupScopeEntry(scope, expression.as<IdentifierNode>().getOrInternNameHandle()))) {
					return std::nullopt;
				}
			} else if (expression.is<TemplateParameterReferenceNode>()) {
				if (!mark_scope_entry(lookupScopeEntry(scope, expression.as<TemplateParameterReferenceNode>().param_name()))) {
					return std::nullopt;
				}
			} else if (expression.is<RequiresExpressionNode>()) {
				return std::nullopt;
			}

			const bool is_supported_leaf =
				expression.is<TypeSpecifierNode>() ||
				expression.is<IdentifierNode>() || expression.is<TemplateParameterReferenceNode>();
			const bool has_children = ExpressionStructure::visitExpressionChildren(
				expression, [&worklist](ExpressionStructure::ExpressionChildRole, const ASTNode& child) {
					worklist.push_back(child);
				});
			if (!has_children && !is_supported_leaf) {
				return std::nullopt;
			}
		}

		std::vector<ParameterMapping> mappings;
		mappings.reserve(current_scope.size);
		for (size_t index = 0; index < current_scope.size; ++index) {
			if (referenced[index] != 0) {
				mappings.push_back(scope_entries_[current_scope.begin + index].mapping);
			}
		}
		return mappings;
	}

	std::optional<ParameterMapping> mapArgument(const ASTNode& argument, size_t parent_scope) const {
		const ASTNode unwrapped = unwrapExpressionNode(argument);
		if (unwrapped.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type = unwrapped.as<TypeSpecifierNode>();
			const bool references_parameter = type.has_template_parameter_identity() || type.has_template_parameter_decl();
			if (!references_parameter) {
				if (type.is_pack_expansion() || typeSpecStillUsesDependentPlaceholder(type)) {
					return std::nullopt;
				}
				const std::optional<TypeId> type_id = tryImportSupportedCanonical(canonical_types_, type);
				if (!type_id.has_value()) {
					return std::nullopt;
				}
				return ParameterMapping::canonicalType(*type_id);
			}
			std::optional<ParameterMapping> base_mapping;
			if (type.has_template_parameter_identity()) {
				base_mapping = lookupMapping(parent_scope, type.template_parameter_name());
			}
			if (!base_mapping.has_value() &&
				type.has_template_parameter_decl() &&
				type.template_decl_id() == function_template_.template_decl_id() &&
				type.template_parameter_index() <
					function_template_.template_parameters().size()) {
				const TemplateParameterNode& parameter =
					function_template_.template_parameters()[
						type.template_parameter_index()];
				base_mapping = ParameterMapping::templateParameter(parameter.kind(), type.template_parameter_index());
			}
			if (!base_mapping.has_value()) {
				return std::nullopt;
			}
			if (isPlainTemplateParameterType(type)) {
				return base_mapping;
			}
			// Only pointer/reference/cv decoration is representable. Array
			// bounds, member classes, function signatures, ordered declarators,
			// and declaration qualifiers remain unsupported rather than being
			// collapsed onto the bare parameter.
			if (type.has_ordered_declarator() || type.has_member_class() ||
				type.has_function_signature() || type.is_array() ||
				type.is_pack_expansion() || type.qualifier() != TypeQualifier::None) {
				return std::nullopt;
			}
			ParameterMapping decorated = *base_mapping;
			decorated.kind = MappingKind::DependentType;
			decorated.pointer_depth = static_cast<uint8_t>(type.pointer_depth());
			decorated.cv_qualifier = type.cv_qualifier();
			decorated.ref_qualifier = type.reference_qualifier();
			return decorated;
		}
		if (unwrapped.is<IdentifierNode>()) {
			return lookupMapping(parent_scope, unwrapped.as<IdentifierNode>().getOrInternNameHandle());
		}
		if (unwrapped.is<TemplateParameterReferenceNode>()) {
			return lookupMapping(parent_scope, unwrapped.as<TemplateParameterReferenceNode>().param_name());
		}
		return std::nullopt;
	}

	std::optional<size_t> buildConcept(
		const ConceptDeclarationNode& concept_declaration, std::span<const ParameterMapping> mappings) {
		const std::optional<size_t> concept_scope = appendScope(concept_declaration.template_params(), mappings);
		if (!concept_scope.has_value()) {
			return std::nullopt;
		}
		return buildExpression(concept_declaration.constraint_expr(), *concept_scope, &concept_declaration);
	}

	std::optional<size_t> buildExpression(
		const ASTNode& root_expression, size_t root_scope, const ConceptDeclarationNode* root_concept) {
		std::vector<BuildTask> tasks;
		std::vector<size_t> results;
		std::vector<const ConceptDeclarationNode*> active_concepts;
		if (root_concept != nullptr) {
			active_concepts.push_back(root_concept);
		}
		tasks.push_back(BuildTask{
			TaskKind::Visit, root_expression, root_scope, FormulaKind::Atom, nullptr});
		while (!tasks.empty()) {
			const BuildTask task = tasks.back();
			tasks.pop_back();
			if (task.kind == TaskKind::ExitConcept) {
				if (active_concepts.empty() || active_concepts.back() != task.concept_declaration) {
					throw InternalError("constraint concept expansion worklist is unbalanced");
				}
				active_concepts.pop_back();
				continue;
			}
			if (task.kind == TaskKind::Combine) {
				if (results.size() < 2) {
					throw InternalError("constraint formula worklist has too few operands");
				}
				const size_t right = results.back();
				results.pop_back();
				const size_t left = results.back();
				results.pop_back();
				results.push_back(appendBinary(task.formula_kind, left, right));
				continue;
			}

			const ASTNode expression = unwrapExpressionNode(task.expression);
			if (expression.is<BinaryOperatorNode>()) {
				const BinaryOperatorNode& binary = expression.as<BinaryOperatorNode>();
				if (binary.op() == "&&"sv || binary.op() == "||"sv) {
					const FormulaKind kind = binary.op() == "&&"sv
						? FormulaKind::Conjunction
						: FormulaKind::Disjunction;
					tasks.push_back(BuildTask{
						TaskKind::Combine, ASTNode{}, task.scope, kind, nullptr});
					tasks.push_back(BuildTask{
						TaskKind::Visit, binary.get_rhs(), task.scope, FormulaKind::Atom, nullptr});
					tasks.push_back(BuildTask{
						TaskKind::Visit, binary.get_lhs(), task.scope, FormulaKind::Atom, nullptr});
					continue;
				}
			}

			const std::optional<ConceptUse> concept_use = tryGetConceptUse(expression);
			if (concept_use.has_value()) {
				const ConceptDeclarationNode* concept_declaration = concept_use->declaration;
				if (std::find(active_concepts.begin(), active_concepts.end(), concept_declaration) != active_concepts.end()) {
					return std::nullopt;
				}
				const TemplateParameterVector& concept_parameters = concept_declaration->template_params();
				if (concept_parameters.size() != concept_use->arguments.size()) {
					return std::nullopt;
				}
				std::vector<ParameterMapping> mappings;
				mappings.reserve(concept_use->arguments.size());
				for (const ASTNode& argument : concept_use->arguments) {
					const std::optional<ParameterMapping> mapping = mapArgument(argument, task.scope);
					if (!mapping.has_value()) {
						return std::nullopt;
					}
					mappings.push_back(*mapping);
				}
				const std::optional<size_t> concept_scope = appendScope(concept_parameters, mappings);
				if (!concept_scope.has_value()) {
					return std::nullopt;
				}
				active_concepts.push_back(concept_declaration);
				tasks.push_back(BuildTask{
					TaskKind::ExitConcept, ASTNode{}, task.scope, FormulaKind::Atom, concept_declaration});
				tasks.push_back(BuildTask{
					TaskKind::Visit, concept_declaration->constraint_expr(), *concept_scope, FormulaKind::Atom, nullptr});
				continue;
			}

			const std::optional<size_t> atom = appendAtom(expression, task.scope);
			if (!atom.has_value()) {
				return std::nullopt;
			}
			results.push_back(*atom);
		}
		if (results.size() != 1) {
			throw InternalError("constraint formula worklist produced an invalid result count");
		}
		return results.front();
	}

	std::optional<size_t> appendAtom(const ASTNode& expression, size_t scope) {
		const std::optional<Token> source_token = tryGetSourceToken(expression);
		if (!source_token.has_value() || source_token->type() == Token::Type::Uninitialized) {
			return std::nullopt;
		}
		std::optional<std::vector<ParameterMapping>> parameter_mapping = tryCollectAtomicParameterMapping(expression, scope);
		if (!parameter_mapping.has_value()) {
			return std::nullopt;
		}
		AtomicConstraint atom{*source_token, std::move(*parameter_mapping)};
		for (size_t atom_index = 0; atom_index < formula_.atoms.size(); ++atom_index) {
			if (sameAtomicConstraint(formula_.atoms[atom_index], atom)) {
				formula_.nodes.push_back(FormulaNode{
					FormulaKind::Atom, 0, 0, atom_index});
				return formula_.nodes.size() - 1;
			}
		}
		const size_t atom_index = formula_.atoms.size();
		formula_.atoms.push_back(std::move(atom));
		formula_.nodes.push_back(FormulaNode{
			FormulaKind::Atom, 0, 0, atom_index});
		return formula_.nodes.size() - 1;
	}

	size_t appendBinary(FormulaKind kind, size_t left, size_t right) {
		formula_.nodes.push_back(FormulaNode{kind, left, right, 0});
		return formula_.nodes.size() - 1;
	}

	CanonicalTypeTable& canonical_types_;
	const TemplateFunctionDeclarationNode& function_template_;
	Formula formula_;
	std::vector<ScopeEntry> scope_entries_;
	std::vector<Scope> scopes_;
};

struct CnfClause {
	std::array<int, 3> literals;
	uint8_t size;
};

class ConstraintSatSolver {
public:
	int appendFormula(const Formula& formula) {
		std::vector<int> node_variables(formula.nodes.size());
		for (size_t index = 0; index < formula.nodes.size(); ++index) {
			const FormulaNode& node = formula.nodes[index];
			if (node.kind == FormulaKind::Atom) {
				const AtomicConstraint& atom = formula.atoms[node.atom];
				const auto existing = std::find_if(atom_variables_.begin(), atom_variables_.end(), [&atom](const auto& entry) {
						return sameAtomicConstraint(*entry.first, atom);
					});
				if (existing != atom_variables_.end()) {
					node_variables[index] = existing->second;
					continue;
				}
				const int variable = nextVariable();
				atom_variables_.emplace_back(&atom, variable);
				node_variables[index] = variable;
				continue;
			}

			const int variable = nextVariable();
			const int left = node_variables[node.left];
			const int right = node_variables[node.right];
			if (node.kind == FormulaKind::Conjunction) {
				addClause(-variable, left);
				addClause(-variable, right);
				addClause(variable, -left, -right);
			} else {
				addClause(-variable, left, right);
				addClause(variable, -left);
				addClause(variable, -right);
			}
			node_variables[index] = variable;
		}
		return node_variables[formula.root];
	}

	bool isSatisfiable(int true_root, int false_root) const {
		std::vector<int8_t> assignments(static_cast<size_t>(variable_count_) + 1, 0);
		std::vector<int> trail;
		struct Decision {
			int variable;
			size_t trail_size;
			bool tried_false;
		};
		std::vector<Decision> decisions;
		auto assign_literal = [&](int literal) {
			const int variable = literal > 0 ? literal : -literal;
			const int8_t value = literal > 0 ? 1 : -1;
			int8_t& assignment = assignments[static_cast<size_t>(variable)];
			if (assignment == value) {
				return true;
			}
			if (assignment != 0) {
				return false;
			}
			assignment = value;
			trail.push_back(variable);
			return true;
		};
		auto rollback = [&](size_t target_size) {
			while (trail.size() > target_size) {
				assignments[static_cast<size_t>(trail.back())] = 0;
				trail.pop_back();
			}
		};
		auto propagate = [&]() {
			bool changed = true;
			while (changed) {
				changed = false;
				for (const CnfClause& clause : clauses_) {
					bool satisfied = false;
					int unassigned_count = 0;
					int unassigned_literal = 0;
					for (uint8_t index = 0; index < clause.size; ++index) {
						const int literal = clause.literals[index];
						const int variable = literal > 0 ? literal : -literal;
						const int8_t assignment = assignments[static_cast<size_t>(variable)];
						if (assignment == 0) {
							++unassigned_count;
							unassigned_literal = literal;
						} else if ((assignment > 0) == (literal > 0)) {
							satisfied = true;
							break;
						}
					}
					if (satisfied) {
						continue;
					}
					if (unassigned_count == 0) {
						return false;
					}
					if (unassigned_count == 1) {
						if (!assign_literal(unassigned_literal)) {
							return false;
						}
						changed = true;
					}
				}
			}
			return true;
		};

		if (!assign_literal(true_root) || !assign_literal(-false_root)) {
			return false;
		}
		while (true) {
			if (!propagate()) {
				bool resumed = false;
				while (!decisions.empty()) {
					Decision& decision = decisions.back();
					rollback(decision.trail_size);
					if (!decision.tried_false) {
						decision.tried_false = true;
						assign_literal(-decision.variable);
						resumed = true;
						break;
					}
					decisions.pop_back();
				}
				if (!resumed) {
					return false;
				}
				continue;
			}

			int next_variable = 0;
			for (int variable = 1; variable <= variable_count_; ++variable) {
				if (assignments[static_cast<size_t>(variable)] == 0) {
					next_variable = variable;
					break;
				}
			}
			if (next_variable == 0) {
				return true;
			}
			decisions.push_back(Decision{next_variable, trail.size(), false});
			assign_literal(next_variable);
		}
	}

private:
	int nextVariable() {
		if (variable_count_ == std::numeric_limits<int>::max()) {
			throw InternalError("constraint subsumption formula has too many nodes");
		}
		return ++variable_count_;
	}

	void addClause(int first, int second) {
		clauses_.push_back(CnfClause{{first, second, 0}, 2});
	}

	void addClause(int first, int second, int third) {
		clauses_.push_back(CnfClause{{first, second, third}, 3});
	}

	std::vector<std::pair<const AtomicConstraint*, int>> atom_variables_;
	std::vector<CnfClause> clauses_;
	int variable_count_ = 0;
};

inline bool implies(const Formula& antecedent, const Formula& consequent) {
	ConstraintSatSolver solver;
	const int antecedent_root = solver.appendFormula(antecedent);
	const int consequent_root = solver.appendFormula(consequent);
	return !solver.isSatisfiable(antecedent_root, consequent_root);
}

} // namespace ConstraintSubsumption

inline ConstraintSubsumptionOrdering compareMemberTemplateConstraints(
	CanonicalTypeTable& canonical_types,
	const TemplateFunctionDeclarationNode& first, const TemplateFunctionDeclarationNode& second) {
	// Atomic constraints are keyed by parameter mapping position. Comparing two
	// templates that way is only sound when their template parameter lists
	// correspond element-wise; otherwise the same index would denote different
	// parameters in the two candidates. Fail closed instead of reporting a bogus
	// ordering.
	const TemplateParameterVector& first_parameters = first.template_parameters();
	const TemplateParameterVector& second_parameters = second.template_parameters();
	if (first_parameters.size() != second_parameters.size()) {
		return ConstraintSubsumptionOrdering::Unsupported;
	}
	for (size_t index = 0; index < first_parameters.size(); ++index) {
		if (first_parameters[index].kind() != second_parameters[index].kind()) {
			return ConstraintSubsumptionOrdering::Unsupported;
		}
	}
	ConstraintSubsumption::FormulaBuilder first_builder(canonical_types, first);
	ConstraintSubsumption::FormulaBuilder second_builder(canonical_types, second);
	std::optional<ConstraintSubsumption::Formula> first_formula = first_builder.build();
	std::optional<ConstraintSubsumption::Formula> second_formula = second_builder.build();
	if (!first_formula.has_value() || !second_formula.has_value()) {
		return ConstraintSubsumptionOrdering::Unsupported;
	}
	const bool first_subsumes_second = ConstraintSubsumption::implies(*first_formula, *second_formula);
	const bool second_subsumes_first = ConstraintSubsumption::implies(*second_formula, *first_formula);
	if (first_subsumes_second && second_subsumes_first) {
		return ConstraintSubsumptionOrdering::Equivalent;
	}
	if (first_subsumes_second) {
		return ConstraintSubsumptionOrdering::FirstSubsumesSecond;
	}
	if (second_subsumes_first) {
		return ConstraintSubsumptionOrdering::SecondSubsumesFirst;
	}
	return ConstraintSubsumptionOrdering::Neither;
}

} // namespace FlashCpp::detail
