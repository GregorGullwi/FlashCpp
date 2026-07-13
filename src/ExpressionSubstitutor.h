#pragma once

#include "AstNodeTypes.h"
#include "Parser.h"
#include "TemplateEnvironment.h"
#include "TemplateRegistry.h"
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <string_view>
#include <vector>

// Forward declaration
class Parser;

/// @file ExpressionSubstitutor.h
/// @brief Template parameter substitution in expression AST nodes.
///
/// ## Purpose
///
/// ExpressionSubstitutor performs **AST transformation** during template instantiation.
/// It replaces template parameter references (T, Args...) with concrete types (int, etc.).
///
/// ## Key Differences from ConstExpr::Evaluator
///
/// | Aspect      | ExpressionSubstitutor        | ConstExpr::Evaluator         |
/// |-------------|------------------------------|------------------------------|
/// | Operation   | AST transformation           | Value computation            |
/// | Input       | AST with template params     | AST with concrete types      |
/// | Output      | Modified AST                 | Primitive value (int/bool)   |
/// | When used   | Template instantiation       | static_assert, constexpr     |
///
/// ## Typical Flow
///
/// ```
/// Parser.instantiate_template()
///   → ExpressionSubstitutor.substitute()
///     → substituteFunctionCall() → TemplateInstantiationHelper
///     → substituteConstructorCall()
///     → substituteBinaryOp() / substituteUnaryOp()
///   → Modified AST with concrete types
/// ```
///
/// @see ConstExpr::Evaluator for constant expression evaluation
/// @see TemplateInstantiationHelper for shared template instantiation utilities

/// ExpressionSubstitutor - Performs template parameter substitution in expression AST nodes
///
/// This class traverses expression AST nodes and substitutes template parameters with concrete
/// template arguments. It is primarily used for evaluating template-dependent decltype expressions
/// in base class specifications during template instantiation.
///
/// Example usage:
///   template<typename T>
///   struct wrapper : decltype(base_trait<T>()) { };
///
/// When instantiating wrapper<int>, the decltype expression needs to have T substituted with int,
/// which this class handles.
class ExpressionSubstitutor {
public:
	/// Construct a substitutor with a parameter-to-argument mapping
	/// @param param_map Maps template parameter names to concrete template arguments
	/// @param parser Reference to the parser for triggering template instantiations
	/// @param template_param_order Optional vector preserving the original template parameter declaration order
	ExpressionSubstitutor(
		const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
		Parser& parser);

	ExpressionSubstitutor(
		const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
		Parser& parser,
		std::span<const std::string_view> template_param_order);

	/// Construct a substitutor with both scalar and pack parameter mappings
	/// @param param_map Maps scalar template parameter names to concrete template arguments
	/// @param pack_map Maps pack parameter names to vectors of template arguments
	/// @param parser Reference to the parser for triggering template instantiations
	/// @param template_param_order Optional vector preserving the original template parameter declaration order
	ExpressionSubstitutor(
		const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
		const std::unordered_map<StringHandle, std::vector<TemplateTypeArg>, TransparentStringHash, std::equal_to<>>& pack_map,
		Parser& parser);

	ExpressionSubstitutor(
		const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
		const std::unordered_map<StringHandle, std::vector<TemplateTypeArg>, TransparentStringHash, std::equal_to<>>& pack_map,
		Parser& parser,
		std::span<const std::string_view> template_param_order);

	ExpressionSubstitutor(
		const TemplateEnvironment& environment,
		Parser& parser);

	ExpressionSubstitutor(
		const TemplateInstantiationContext& context,
		Parser& parser);

	/// Main entry point: Substitute template parameters in an expression
	/// @param expr The expression AST node to process
	/// @return A new expression with template parameters substituted
	ASTNode substitute(const ASTNode& expr);

	// Substitute a type-id without routing it through expression-node wrapping.
	TypeSpecifierNode substituteTypeSpecifier(const TypeSpecifierNode& type);

	const TypeInfo* resolveDependentMemberTypeForSubstitution(const TypeInfo& type_info);

	void setCurrentOwnerTypeName(StringHandle owner_type_name) {
		current_owner_type_name_ = owner_type_name;
	}

private:
	struct MaterializedStoredTemplateArgs {
		std::vector<TemplateTypeArg> args;
		bool had_substitution = false;
	};
	struct MaterializedDependentMemberLookup {
		const TypeInfo* resolved_type = nullptr;
		StringHandle materialized_member_handle{};
		StringHandle terminal_member_name{};
	};
	struct MaterializedQualifiedLookupOwner {
		const TypeInfo* owner_type = nullptr;
		StringHandle owner_name{};
		OuterTemplateBinding outer_binding;
	};

	// Handlers for different expression types
	ASTNode substituteConstructorCall(const ConstructorCallNode& ctor);

	ASTNode substituteCallExpr(const CallExprNode& call);
	ASTNode substituteFunctionCallImpl(const CallExprNode& call);

	ASTNode substituteBinaryOp(const BinaryOperatorNode& binop);
	ASTNode substituteUnaryOp(const UnaryOperatorNode& unop);
	ASTNode substituteTernaryOp(const TernaryOperatorNode& ternary);
	ASTNode substituteIdentifier(const IdentifierNode& id);
	ASTNode substituteQualifiedIdentifier(const QualifiedIdentifierNode& qual_id);
	ASTNode substituteMemberAccess(const MemberAccessNode& member_access);
	ASTNode substituteSizeofExpr(const SizeofExprNode& sizeof_expr);
	ASTNode substituteTypeTraitExpr(const TypeTraitExprNode& trait_expr);
	ASTNode substituteStaticCast(const StaticCastNode& cast_node);
	ASTNode substituteInitializerListConstruction(const InitializerListConstructionNode& init_list);
	ASTNode substituteLiteral(const ASTNode& literal);
	void captureParserPackState();
	void substituteCallArgumentPreservingPackExpansion(
		const ASTNode& arg,
		ChunkedVector<ASTNode>& out);
	ChunkedVector<ASTNode> substituteCallArgumentsPreservingPackExpansion(
		const ChunkedVector<ASTNode>& args);

	// Helper: substitute in a type specifier with template args
	TypeSpecifierNode substituteInType(const TypeSpecifierNode& type);

	// Helper: check if a template argument node is a pack expansion
	bool isPackExpansion(const ASTNode& arg_node, std::string_view& pack_name);

	// Helper: expand pack parameters in template arguments
	std::vector<TemplateTypeArg> expandPacksInArguments(
		std::span<const ASTNode> template_arg_nodes);
	std::vector<TemplateTypeArg> collectCurrentBoundTemplateArgs(std::string_view use_site) const;
	MaterializedStoredTemplateArgs materializeStoredTemplateArgs(
		const TypeInfo& template_instantiation_info,
		bool evaluate_dependent_member_values,
		int depth);
	InlineVector<TemplateTypeArg, 4> materializeDependentRecordTemplateArgs(
		std::span<const TypeInfo::TemplateArgInfo> stored_args,
		int depth);
	bool templateArgsStillDependent(std::span<const TemplateTypeArg> args) const;
	Parser::AliasTemplateMaterializationResult materializeDependentQualifiedRecordOwner(
		const TypeInfo::DependentQualifiedNameRecord& dependent_name,
		int depth,
		bool prefer_current_owner_type_name,
		bool allow_current_context_dependent_owner_materialization);
	MaterializedQualifiedLookupOwner materializeDependentQualifiedMemberPrefixOwner(
		StringHandle owner_name,
		std::string_view recorded_owner_name,
		std::span<const TypeInfo::DependentQualifiedNameRecord::Member> prefix_chain,
		int depth);
	MaterializedDependentMemberLookup lookupMaterializedDependentMember(
		const TypeInfo& type_info,
		int depth);
	const TypeInfo* resolveMaterializedMemberAliasLookup(
		const MaterializedDependentMemberLookup& lookup);
	const TypeInfo* resolveQualifiedMemberNamespaceChain(
		std::string_view owner_name,
		std::span<QualifiedTypeMemberAccess> member_chain);
	const TypeInfo* resolveCurrentOwnerQualifiedNamespaceMember(
		StringHandle member_name_handle);
	const TypeInfo* resolveDependentMemberType(const TypeInfo& type_info, int depth);
	std::optional<ASTNode> tryEvaluateConcreteConceptCall(const CallExprNode& call) const;
	void rebuildEnvironmentFromCurrentBindings();

// Substitution context
	std::unordered_map<std::string_view, TemplateTypeArg> param_map_;
	std::unordered_map<StringHandle, std::vector<TemplateTypeArg>, TransparentStringHash, std::equal_to<>> pack_map_;
	Parser& parser_;
	std::vector<std::string_view> template_param_order_;
	TemplateEnvironment environment_;
	std::vector<Parser::PackParamInfo> captured_pack_param_info_;
	StringHandle current_owner_type_name_{};
	// Cycle-detection guard for materializeStoredTemplateArgs:
	// tracks TypeInfo pointers currently being materialized to prevent
	// infinite mutual recursion with substituteQualifiedIdentifier.
	std::unordered_set<const TypeInfo*> materializing_type_infos_;
	// Cycle-detection guard for substituteQualifiedIdentifier:
	// tracks (namespace_handle << 32 | member_name_handle) keys to prevent
	// re-entrant substitution of the same qualified identifier.
	std::unordered_set<uint64_t> substituting_qual_ids_;
};
