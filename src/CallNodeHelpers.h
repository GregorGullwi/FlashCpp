#pragma once

#include "AstNodeTypes_DeclNodes.h"
#include "AstNodeTypes_Expr.h"
#include <vector>

// ============================================================================
// CallInfo — a lightweight read-only view over any call-expression node.
//
// Provides a uniform interface for extracting call metadata from CallExprNode.
// Downstream code can construct a CallInfo and then inspect fields without
// branching on the concrete node type.
//
// The view does NOT own the underlying data; it must not outlive the
// source node.
// ============================================================================

inline const FunctionDeclarationNode* getParserStoredDirectCallTarget(const CallExprNode& node) {
	// Dependent unqualified calls may carry a provisional parser-time callee only
	// to preserve the definition-bound ordinary lookup result.  Once the POI
	// completion record exists, downstream semantic consumers must not treat that
	// provisional target as authoritative.
	if (node.has_dependent_unqualified_lookup_record()) {
		return nullptr;
	}
	return node.callee().function_declaration_or_null();
}

inline bool canBuildFunctionCallDefinitionLookupRecord(
	const TemplateDefinitionLookupContext* definition_context,
	const Token& callee_token,
	std::span<const TypeSpecifierNode> arg_types,
	bool has_deferred_template_call_args) {
	if (definition_context == nullptr ||
		!definition_context->is_valid() ||
		has_deferred_template_call_args ||
		callee_token.type() != Token::Type::Identifier) {
		return false;
	}
	for (const TypeSpecifierNode& arg_type : arg_types) {
		if (arg_type.category() == TypeCategory::Auto ||
			arg_type.category() == TypeCategory::Template) {
			return false;
		}
		if (!arg_type.type_index().is_valid() &&
			!is_builtin_type(arg_type.category())) {
			return false;
		}
	}
	return true;
}

inline std::optional<FunctionCallDefinitionLookupRecord> tryBuildFunctionCallDefinitionLookupRecord(
	const TemplateDefinitionLookupContext* definition_context,
	const Token& callee_token,
	std::span<const TypeSpecifierNode> arg_types,
	bool has_deferred_template_call_args,
	const FunctionDeclarationNode& func_decl,
	bool ordinary_lookup_included,
	bool argument_dependent_lookup_included) {
	if (!canBuildFunctionCallDefinitionLookupRecord(
			definition_context,
			callee_token,
			arg_types,
			has_deferred_template_call_args)) {
		return std::nullopt;
	}

	FunctionCallDefinitionLookupRecord record;
	record.definition_context = *definition_context;
	record.callee_name = callee_token.handle();
	record.resolved_function = &func_decl;
	if (func_decl.has_mangled_name()) {
		record.resolved_mangled_name =
			StringTable::getOrInternStringHandle(func_decl.mangled_name());
	}
	record.ordinary_lookup_included = ordinary_lookup_included;
	record.argument_dependent_lookup_included =
		argument_dependent_lookup_included;
	return record;
}

inline std::optional<FunctionCallDefinitionLookupRecord>
tryBuildDeferredFunctionCallDefinitionLookupRecord(
	const TemplateDefinitionLookupContext* definition_context,
	const Token& callee_token,
	bool ordinary_lookup_included,
	bool argument_dependent_lookup_included) {
	if (definition_context == nullptr ||
		!definition_context->is_valid() ||
		callee_token.type() != Token::Type::Identifier) {
		return std::nullopt;
	}

	FunctionCallDefinitionLookupRecord record;
	record.definition_context = *definition_context;
	record.callee_name = callee_token.handle();
	record.ordinary_lookup_included = ordinary_lookup_included;
	record.argument_dependent_lookup_included =
		argument_dependent_lookup_included;
	return record;
}

inline std::optional<std::string_view> tryBuildQualifiedCallNameOverride(
	std::string_view owner_name,
	std::string_view member_name) {
	if (owner_name.empty() || member_name.empty()) {
		return std::nullopt;
	}

	return StringBuilder()
		.append(owner_name)
		.append("::")
		.append(member_name)
		.commit();
}

inline std::optional<std::string_view> tryBuildQualifiedCallNameOverride(
	const FunctionDeclarationNode& func_decl) {
	return tryBuildQualifiedCallNameOverride(
		func_decl.parent_struct_name(),
		func_decl.decl_node().identifier_token().value());
}

struct CallInfo {
	// Callee declaration (always present).
	const DeclarationNode* declaration;

	// Full function declaration (null when only a DeclarationNode is available).
	const FunctionDeclarationNode* function_declaration;

	// Arguments (always present).
	const ChunkedVector<ASTNode>* arguments;

	// Source location token.
	Token called_from;

	// Receiver object (empty ASTNode when there is no receiver).
	ASTNode receiver;
	bool has_receiver;

	// Optional metadata — empty/invalid when the source node does not carry them.
	StringHandle mangled_name;
	StringHandle qualified_name;
	std::span<const ASTNode> template_arguments;
	const std::optional<TypeSpecifierNode>* parser_return_type_hint;
	const std::optional<FunctionCallDefinitionLookupRecord>* definition_lookup_record;
	const std::optional<DependentUnqualifiedCallLookupRecord>* dependent_unqualified_lookup_record;
	const std::optional<TypeInfo::DependentQualifiedNameRecord>* dependent_qualified_lookup_record;
	bool is_indirect;

	// --- Factory helpers ---------------------------------------------------

	static CallInfo from(const CallExprNode& node) {
		CallInfo info;
		info.declaration           = &node.callee().declaration();
		info.function_declaration  = getParserStoredDirectCallTarget(node);
		info.arguments             = &node.arguments();
		info.called_from           = node.called_from();
		info.receiver              = node.has_receiver() ? node.receiver() : ASTNode();
		info.has_receiver          = node.has_receiver();
		info.mangled_name          = node.mangled_name_handle();
		info.qualified_name        = node.qualified_name_handle();
		info.template_arguments    = node.template_arguments();
		info.parser_return_type_hint = &node.parser_return_type_hint();
		info.definition_lookup_record = &node.definition_lookup_record();
		info.dependent_unqualified_lookup_record =
			&node.dependent_unqualified_lookup_record();
		info.dependent_qualified_lookup_record =
			&node.dependent_qualified_lookup_record();
		info.is_indirect           = node.callee().is_indirect();
		return info;
	}

	// Build a CallInfo from an ExpressionNode that holds one of the three
	// call-node alternatives.  Returns std::nullopt for any other alternative.
	static std::optional<CallInfo> tryFrom(const ExpressionNode& expr) {
		if (auto* p = std::get_if<CallExprNode>(&expr))
			return from(*p);
		return std::nullopt;
	}
};

struct CallMetadataCopyOptions {
	bool copy_mangled_name = true;
	bool copy_qualified_name = true;
	bool copy_template_arguments = true;
	bool copy_parser_return_type_hint = true;
	bool copy_definition_lookup_record = true;
	bool copy_dependent_unqualified_lookup_record = true;
	bool copy_dependent_qualified_lookup_record = true;
};

template <typename CallNodeT>
inline void copyCallMetadataFromInfo(
	CallNodeT& target,
	const CallInfo& source,
	const CallMetadataCopyOptions& options) {
	if (options.copy_mangled_name && source.mangled_name.isValid()) {
		target.set_mangled_name(source.mangled_name.view());
	}
	if (options.copy_qualified_name && source.qualified_name.isValid()) {
		target.set_qualified_name(source.qualified_name.view());
	}
	if (options.copy_template_arguments &&
		!source.template_arguments.empty()) {
		std::vector<ASTNode> copied_template_args;
		copied_template_args.reserve(source.template_arguments.size());
		for (const auto& template_arg : source.template_arguments) {
			copied_template_args.push_back(template_arg);
		}
		target.set_template_arguments(std::move(copied_template_args));
	}
	if (options.copy_parser_return_type_hint &&
		source.parser_return_type_hint != nullptr &&
		source.parser_return_type_hint->has_value()) {
		target.set_parser_return_type_hint(
			source.parser_return_type_hint->value());
	}
	if (options.copy_definition_lookup_record &&
		source.definition_lookup_record != nullptr &&
		source.definition_lookup_record->has_value()) {
		target.set_definition_lookup_record(**source.definition_lookup_record);
	}
	if (options.copy_dependent_unqualified_lookup_record &&
		source.dependent_unqualified_lookup_record != nullptr &&
		source.dependent_unqualified_lookup_record->has_value()) {
		target.set_dependent_unqualified_lookup_record(
			**source.dependent_unqualified_lookup_record);
	}
	if (options.copy_dependent_qualified_lookup_record &&
		source.dependent_qualified_lookup_record != nullptr &&
		source.dependent_qualified_lookup_record->has_value()) {
		target.set_dependent_qualified_lookup_record(
			**source.dependent_qualified_lookup_record);
	}
}

template <typename TargetCallNodeT, typename SourceCallNodeT>
inline void copyCallMetadata(
	TargetCallNodeT& target,
	const SourceCallNodeT& source,
	const CallMetadataCopyOptions& options) {
	copyCallMetadataFromInfo(target, CallInfo::from(source), options);
}

template <typename SourceCallNodeT, typename TransformFn>
inline std::vector<ASTNode> transformCallTemplateArguments(
	const SourceCallNodeT& source,
	TransformFn&& transform_template_arg) {
	const CallInfo source_info = CallInfo::from(source);
	std::vector<ASTNode> transformed_template_args;
	if (source_info.template_arguments.empty()) {
		return transformed_template_args;
	}

	transformed_template_args.reserve(source_info.template_arguments.size());
	for (const auto& template_arg : source_info.template_arguments) {
		transformed_template_args.push_back(transform_template_arg(template_arg));
	}
	return transformed_template_args;
}

template <typename TargetCallNodeT, typename SourceCallNodeT, typename TransformFn>
inline void copyCallMetadataWithTransformedTemplateArguments(
	TargetCallNodeT& target,
	const SourceCallNodeT& source,
	TransformFn&& transform_template_arg,
	const CallMetadataCopyOptions& options) {
	CallMetadataCopyOptions base_options = options;
	base_options.copy_template_arguments = false;
	copyCallMetadata(target, source, base_options);

	if (!options.copy_template_arguments) {
		return;
	}

	std::vector<ASTNode> transformed_template_args =
		transformCallTemplateArguments(source, transform_template_arg);
	if (!transformed_template_args.empty()) {
		target.set_template_arguments(std::move(transformed_template_args));
	}
}

inline ChunkedVector<ASTNode> copyCallArguments(const ChunkedVector<ASTNode>& arguments) {
	ChunkedVector<ASTNode> copied_arguments;
	arguments.visit([&](ASTNode arg) {
		copied_arguments.push_back(arg);
	});
	return copied_arguments;
}

inline CallExprNode makeDirectCallExpr(const DeclarationNode& decl, ChunkedVector<ASTNode>&& arguments, Token called_from_token) {
	return CallExprNode(CalleeDescriptor::freeFunction(decl), std::move(arguments), called_from_token);
}

inline CallExprNode makeResolvedCallExpr(const FunctionDeclarationNode& func_decl, ChunkedVector<ASTNode>&& arguments, Token called_from_token) {
	CalleeDescriptor callee = func_decl.is_static()
		? CalleeDescriptor::staticMemberFunction(func_decl)
		: CalleeDescriptor::freeFunctionResolved(func_decl);
	return CallExprNode(callee, std::move(arguments), called_from_token);
}

inline CallExprNode makeIndirectCallExpr(const DeclarationNode& decl, ChunkedVector<ASTNode>&& arguments, Token called_from_token) {
	return CallExprNode(CalleeDescriptor::indirectCall(decl), std::move(arguments), called_from_token);
}

inline ExpressionNode makeCallExprFromNode(const ASTNode& callee_node, ChunkedVector<ASTNode>&& arguments, Token called_from_token) {
	if (callee_node.is<FunctionDeclarationNode>()) {
		return makeResolvedCallExpr(callee_node.as<FunctionDeclarationNode>(), std::move(arguments), called_from_token);
	}
	if (callee_node.is<DeclarationNode>()) {
		return makeDirectCallExpr(callee_node.as<DeclarationNode>(), std::move(arguments), called_from_token);
	}
	if (callee_node.is<VariableDeclarationNode>()) {
		return makeDirectCallExpr(callee_node.as<VariableDeclarationNode>().declaration(), std::move(arguments), called_from_token);
	}
	if (callee_node.is<TemplateFunctionDeclarationNode>()) {
		const ASTNode& function_decl = callee_node.as<TemplateFunctionDeclarationNode>().function_declaration();
		if (function_decl.is<FunctionDeclarationNode>()) {
			return makeDirectCallExpr(function_decl.as<FunctionDeclarationNode>().decl_node(), std::move(arguments), called_from_token);
		}
		throw InternalError("TemplateFunctionDeclarationNode call target is missing FunctionDeclarationNode");
	}
	throw InternalError(std::string("Unsupported call target node type in makeCallExprFromNode: ") + callee_node.type_name());
}

inline CallExprNode makeResolvedMemberCallExpr(ASTNode receiver, const FunctionDeclarationNode& func_decl, ChunkedVector<ASTNode>&& arguments, Token called_from_token) {
	CalleeDescriptor callee = func_decl.is_static()
		? CalleeDescriptor::staticMemberFunction(func_decl)
		: CalleeDescriptor::memberFunction(func_decl);
	return CallExprNode(callee, receiver, std::move(arguments), called_from_token);
}

inline void setCallMangledName(ExpressionNode& expr, std::string_view name) {
	if (auto* call_expr = std::get_if<CallExprNode>(&expr)) {
		call_expr->set_mangled_name(name);
	}
}

inline void setCallMangledName(CallExprNode& call_expr, std::string_view name) {
	call_expr.set_mangled_name(name);
}

inline void setCallQualifiedName(ExpressionNode& expr, std::string_view name) {
	if (auto* call_expr = std::get_if<CallExprNode>(&expr)) {
		call_expr->set_qualified_name(name);
	}
}

inline void setCallQualifiedName(CallExprNode& call_expr, std::string_view name) {
	call_expr.set_qualified_name(name);
}

inline void setCallTemplateArguments(ExpressionNode& expr, std::vector<ASTNode>&& template_args) {
	if (auto* call_expr = std::get_if<CallExprNode>(&expr)) {
		call_expr->set_template_arguments(std::move(template_args));
	}
}

inline void setCallDefinitionLookupRecord(ExpressionNode& expr, const FunctionCallDefinitionLookupRecord& record) {
	if (auto* call_expr = std::get_if<CallExprNode>(&expr)) {
		call_expr->set_definition_lookup_record(record);
	}
}

inline void setCallDefinitionLookupRecord(CallExprNode& call_expr, const FunctionCallDefinitionLookupRecord& record) {
	call_expr.set_definition_lookup_record(record);
}

inline void setCallDependentUnqualifiedLookupRecord(
	ExpressionNode& expr,
	const DependentUnqualifiedCallLookupRecord& record) {
	if (auto* call_expr = std::get_if<CallExprNode>(&expr)) {
		call_expr->set_dependent_unqualified_lookup_record(record);
	}
}

inline void setCallDependentUnqualifiedLookupRecord(
	CallExprNode& call_expr,
	const DependentUnqualifiedCallLookupRecord& record) {
	call_expr.set_dependent_unqualified_lookup_record(record);
}

inline void setCallDependentQualifiedLookupRecord(
	ExpressionNode& expr,
	const TypeInfo::DependentQualifiedNameRecord& record) {
	if (auto* call_expr = std::get_if<CallExprNode>(&expr)) {
		call_expr->set_dependent_qualified_lookup_record(record);
	}
}

inline void setCallDependentQualifiedLookupRecord(
	CallExprNode& call_expr,
	const TypeInfo::DependentQualifiedNameRecord& record) {
	call_expr.set_dependent_qualified_lookup_record(record);
}

inline void setCallTemplateArguments(CallExprNode& call_expr, std::vector<ASTNode>&& template_args) {
	call_expr.set_template_arguments(std::move(template_args));
}

inline void setCallParserReturnTypeHint(
	ExpressionNode& expr,
	const TypeSpecifierNode& type_hint) {
	if (auto* call_expr = std::get_if<CallExprNode>(&expr)) {
		call_expr->set_parser_return_type_hint(type_hint);
	}
}

inline void setCallParserReturnTypeHint(
	CallExprNode& call_expr,
	const TypeSpecifierNode& type_hint) {
	call_expr.set_parser_return_type_hint(type_hint);
}

inline void setCallIndirect(ExpressionNode& expr) {
	if (auto* call_expr = std::get_if<CallExprNode>(&expr)) {
		CallExprNode indirect_call = makeIndirectCallExpr(
			call_expr->callee().declaration(),
			copyCallArguments(call_expr->arguments()),
			call_expr->called_from());
		copyCallMetadata(indirect_call, *call_expr, CallMetadataCopyOptions{});
		expr = std::move(indirect_call);
	}
}

inline void setCallIndirect(CallExprNode& call_expr) {
	CallExprNode indirect_call = makeIndirectCallExpr(
		call_expr.callee().declaration(),
		copyCallArguments(call_expr.arguments()),
		call_expr.called_from());
	copyCallMetadata(indirect_call, call_expr, CallMetadataCopyOptions{});
	call_expr = std::move(indirect_call);
}
