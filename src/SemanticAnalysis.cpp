#include "SemanticAnalysis.h"
#include "Parser.h"
#include "CompileContext.h"
#include "CompileError.h"
#include "SymbolTable.h"
#include "AstNodeTypes.h"
#include "AstNodeTypes_Expr.h"
#include "OverloadResolution.h"
#include "Log.h"
#include "IrGenerator.h"

namespace {
// Placeholder return-type finalization requires every return statement in the
// body to deduce to the same full type identity, including cv/reference and
// pointer qualifiers. This prevents plain `auto` and `decltype(auto)` from
// accidentally merging distinct return categories during semantic rewriting.
bool placeholderReturnTypesMatch(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) {
	if (lhs.type() != rhs.type() ||
		lhs.type_index() != rhs.type_index() ||
		lhs.cv_qualifier() != rhs.cv_qualifier() ||
		lhs.reference_qualifier() != rhs.reference_qualifier() ||
		lhs.pointer_depth() != rhs.pointer_depth() ||
		lhs.array_dimensions() != rhs.array_dimensions()) {
		return false;
	}

	for (size_t i = 0; i < lhs.pointer_depth(); ++i) {
		if (lhs.pointer_levels()[i].cv_qualifier != rhs.pointer_levels()[i].cv_qualifier) {
			return false;
		}
	}

	return true;
}

bool callableOperatorAcceptsArgumentCount(const FunctionDeclarationNode& candidate, size_t argument_count) {
	const size_t min_required = countMinRequiredArgs(candidate);
	if (argument_count < min_required) {
		return false;
	}
	if (candidate.is_variadic()) {
		return true;
	}
	return argument_count <= candidate.parameter_nodes().size();
}

ASTNode resolveRangedForLoopDeclNode(const VariableDeclarationNode& original_var_decl, const TypeSpecifierNode& deduced_type) {
	const DeclarationNode& original_decl = original_var_decl.declaration();
	const TypeSpecifierNode& placeholder_type = original_decl.type_node().as<TypeSpecifierNode>();
	if (!isPlaceholderAutoType(placeholder_type.type())) {
		return original_var_decl.declaration_node();
	}

	TypeSpecifierNode resolved_type = finalizePlaceholderTypeDeduction(placeholder_type.type(), deduced_type);
	if (placeholder_type.type() == Type::Auto) {
		resolved_type.set_reference_qualifier(placeholder_type.reference_qualifier());
		if (placeholder_type.cv_qualifier() != CVQualifier::None) {
			resolved_type.set_cv_qualifier(placeholder_type.cv_qualifier());
		}
	}

	ASTNode resolved_type_node = ASTNode::emplace_node<TypeSpecifierNode>(resolved_type);
	DeclarationNode& resolved_decl = gChunkedAnyStorage.emplace_back<DeclarationNode>(original_decl);
	resolved_decl.set_type_node(resolved_type_node);
	return ASTNode::emplace_node<DeclarationNode>(resolved_decl);
}

const FunctionDeclarationNode* getRangeIteratorDereferenceFunctionForSema(const TypeSpecifierNode& iterator_type, bool prefer_const) {
	if (!iterator_type.type_index().is_valid() || iterator_type.type_index().value >= gTypeInfo.size()) {
		return nullptr;
	}

	const StructTypeInfo* struct_info = gTypeInfo[iterator_type.type_index().value].getStructInfo();
	if (!struct_info) {
		return nullptr;
	}

	const FunctionDeclarationNode* fallback = nullptr;
	for (const auto& member_func : struct_info->member_functions) {
		if (member_func.operator_kind != OverloadableOperator::Multiply ||
			!member_func.function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}

		const auto& func = member_func.function_decl.as<FunctionDeclarationNode>();
		if (!func.parameter_nodes().empty()) {
			continue;
		}

		if (prefer_const && member_func.is_const()) {
			return &func;
		}
		if (!prefer_const && !member_func.is_const()) {
			return &func;
		}
		if (!fallback) {
			fallback = &func;
		}
	}

	return fallback;
}

TypeSpecifierNode materializeTypeSpecifier(const CanonicalTypeDesc& desc) {
	TypeSpecifierNode type_node(desc.base_type, desc.type_index, 0);
	type_node.set_cv_qualifier(desc.base_cv);
	type_node.set_reference_qualifier(desc.ref_qualifier);
	for (const auto& pointer_level : desc.pointer_levels) {
		type_node.add_pointer_level(pointer_level.cv_qualifier);
	}
	if (!desc.array_dimensions.empty()) {
		type_node.set_array(true);
		type_node.set_array_dimensions(desc.array_dimensions);
	}
	if (desc.function_signature.has_value()) {
		type_node.set_function_signature(*desc.function_signature);
	}

	int size_bits = 0;
	if (desc.ref_qualifier != ReferenceQualifier::None || !desc.pointer_levels.empty()) {
		size_bits = 64;
	} else if (desc.base_type == Type::Invalid) {
		size_bits = 0;
	} else {
		size_bits = getTypeSpecSizeBits(type_node);
	}
	type_node.set_size_in_bits(size_bits);
	return type_node;
}

CanonicalTypeDesc canonicalTypeDescFromStructMember(const StructMember& member, CVQualifier object_cv) {
	CanonicalTypeDesc desc;
	desc.base_type = member.type;
	desc.type_index = member.type_index;
	desc.ref_qualifier = member.reference_qualifier;
	if (member.is_array) {
		desc.array_dimensions = member.array_dimensions;
	}
	if (member.function_signature.has_value()) {
		desc.function_signature = member.function_signature;
	}
	for (int i = 0; i < member.pointer_depth; ++i) {
		desc.pointer_levels.push_back(PointerLevel{});
	}
	if (member.reference_qualifier == ReferenceQualifier::None) {
		desc.base_cv = object_cv;
	}
	return desc;
}
}

// --- CanonicalTypeDesc::operator== ---

bool CanonicalTypeDesc::operator==(const CanonicalTypeDesc& other) const {
	if (base_type != other.base_type) return false;
	if (type_index != other.type_index) return false;
	if (base_cv != other.base_cv) return false;
	if (ref_qualifier != other.ref_qualifier) return false;
	if (flags != other.flags) return false;
	if (pointer_levels.size() != other.pointer_levels.size()) return false;
	for (size_t i = 0; i < pointer_levels.size(); ++i) {
		if (pointer_levels[i].cv_qualifier != other.pointer_levels[i].cv_qualifier) return false;
	}
	if (array_dimensions != other.array_dimensions) return false;
	if (function_signature.has_value() != other.function_signature.has_value()) return false;
	if (function_signature.has_value()) {
		const auto& a = *function_signature;
		const auto& b = *other.function_signature;
		if (a.return_type != b.return_type) return false;
		if (a.parameter_types != b.parameter_types) return false;
		if (a.linkage != b.linkage) return false;
		if (a.is_const != b.is_const) return false;
		if (a.is_volatile != b.is_volatile) return false;
		if (a.class_name != b.class_name) return false;
	}
	return true;
}

// --- TypeContext ---

CanonicalTypeId TypeContext::intern(const CanonicalTypeDesc& desc) {
	auto it = index_.find(desc);
	if (it != index_.end())
		return it->second;
	types_.push_back(desc);
	const CanonicalTypeId id{static_cast<uint32_t>(types_.size())};  // 1-based
	index_.emplace(desc, id);
	return id;
}

const CanonicalTypeDesc& TypeContext::get(CanonicalTypeId id) const {
	assert(id.value > 0 && id.value <= types_.size());
	return types_[id.value - 1];
}

// --- Determine StandardConversionKind from two primitive Type values ---
// TODO: Consider unifying this with can_convert_type (OverloadResolution.h) into a single
// buildConversionPlan() helper that returns both the rank and the StandardConversionKind,
// per the plan (§B.3 / appendix §B). For now the two analyses are separate but consistent.

static StandardConversionKind determineConversionKind(Type from, Type to) {
	if (to == Type::Bool) return StandardConversionKind::BooleanConversion;

	const bool from_int = is_integer_type(from) || from == Type::Bool;
	const bool to_int   = is_integer_type(to);
	const bool from_flt = is_floating_point_type(from);
	const bool to_flt   = is_floating_point_type(to);

	if (from_int && to_int) {
		// C++20 [conv.prom]: IntegralPromotion only applies to types with rank < int being promoted
		// to int or unsigned int.  int→long, int→long long, etc. are IntegralConversion.
		const int INT_RANK = 3;  // rank of int/unsigned int in get_integer_rank()
		const int from_rank = get_integer_rank(from);
		const int to_rank   = get_integer_rank(to);
		if (from_rank < INT_RANK && to_rank == INT_RANK)
			return StandardConversionKind::IntegralPromotion;
		return StandardConversionKind::IntegralConversion;
	}
	if (from_flt && to_flt) {
		if (get_type_size_bits(from) < get_type_size_bits(to))
			return StandardConversionKind::FloatingPromotion;
		return StandardConversionKind::FloatingConversion;
	}
	if (from_int && to_flt) return StandardConversionKind::FloatingIntegralConversion;
	if (from_flt && to_int) return StandardConversionKind::FloatingIntegralConversion;

	// Should not be reached: tryAnnotateReturnConversion guards against struct/pointer/invalid types.
	throw InternalError("determineConversionKind: unhandled type pair");
}

// --- SemanticAnalysis ---

SemanticAnalysis::SemanticAnalysis(Parser& parser, CompileContext& context, SymbolTable& symbols)
	: parser_(parser), context_(context), symbols_(symbols) {
	(void)context_;
	(void)symbols_;

	// Pre-intern the canonical bool type so tryAnnotateContextualBool avoids
	// repeated interning on every call.
	CanonicalTypeDesc bool_desc;
	bool_desc.base_type = Type::Bool;
	bool_type_id_ = type_context_.intern(bool_desc);
}

void SemanticAnalysis::run() {
	const auto& nodes = parser_.get_nodes();
	stats_.total_roots = nodes.size();

	FLASH_LOG(General, Debug, "SemanticAnalysis: starting pass over ", nodes.size(), " top-level nodes");

	for (auto& node : nodes) {
		normalizeTopLevelNode(node);
	}

	resolveRemainingAutoReturns();

	FLASH_LOG(General, Debug, "SemanticAnalysis: pass complete - ",
		stats_.roots_visited, " roots visited, ",
		stats_.expressions_visited, " expressions, ",
		stats_.statements_visited, " statements, ",
		stats_.canonical_types_interned, " canonical types");
}

std::vector<ASTNode> SemanticAnalysis::normalizeGenericLambdaParams(
	const std::vector<ASTNode>& parameter_nodes,
	const std::vector<std::pair<size_t, TypeSpecifierNode>>& deduced_types) const {
	if (deduced_types.empty()) {
		return parameter_nodes;
	}

	std::vector<ASTNode> resolved_nodes;
	resolved_nodes.reserve(parameter_nodes.size());

	for (size_t index = 0; index < parameter_nodes.size(); ++index) {
		const ASTNode& param_node = parameter_nodes[index];
		if (!param_node.is<DeclarationNode>()) {
			resolved_nodes.push_back(param_node);
			continue;
		}

		const TypeSpecifierNode* deduced_type = nullptr;
		for (const auto& [deduced_index, type_node] : deduced_types) {
			if (deduced_index == index) {
				deduced_type = &type_node;
				break;
			}
		}

		if (!deduced_type) {
			resolved_nodes.push_back(param_node);
			continue;
		}

		const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
		DeclarationNode& resolved_decl = gChunkedAnyStorage.emplace_back<DeclarationNode>(param_decl);
		ASTNode resolved_type_node = ASTNode::emplace_node<TypeSpecifierNode>(*deduced_type);
		resolved_decl.set_type_node(resolved_type_node);
		resolved_nodes.push_back(ASTNode::emplace_node<DeclarationNode>(resolved_decl));
	}

	return resolved_nodes;
}

void SemanticAnalysis::normalizeInstantiatedLambdaBody(LambdaInfo& lambda_info) {
	if (!lambda_info.is_generic || lambda_info.deduced_auto_types.empty()) {
		return;
	}

	if (lambda_info.normalized_deduced_auto_types_generation == lambda_info.deduced_auto_types_generation) {
		return;
	}

	lambda_info.resolved_param_nodes = normalizeGenericLambdaParams(
		lambda_info.parameter_nodes,
		lambda_info.deduced_auto_types);

	const auto& param_nodes = lambda_info.resolved_param_nodes.empty()
		? lambda_info.parameter_nodes
		: lambda_info.resolved_param_nodes;

	symbols_.enter_scope(ScopeType::Function);
	pushScope();
	auto cleanup = ScopeGuard([&]() {
		popScope();
		symbols_.exit_scope();
	});

	for (const auto& param_node : param_nodes) {
		if (!param_node.is<DeclarationNode>()) {
			continue;
		}

		const auto& param_decl = param_node.as<DeclarationNode>();
		symbols_.insert(param_decl.identifier_token().value(), param_node);

		const ASTNode& param_type_node = param_decl.type_node();
		if (param_type_node.is<TypeSpecifierNode>()) {
			const CanonicalTypeId tid = canonicalizeType(param_type_node.as<TypeSpecifierNode>());
			const StringHandle pname = param_decl.identifier_token().handle();
			if (pname.isValid()) {
				addLocalType(pname, tid);
			}
		}
	}

	for (const auto& capture_decl_node : lambda_info.captured_var_decls) {
		if (const DeclarationNode* capture_decl = get_decl_from_symbol(capture_decl_node)) {
			ASTNode symbol_node = capture_decl_node.is<DeclarationNode>()
				? capture_decl_node
				: ASTNode::emplace_node<DeclarationNode>(*capture_decl);
			symbols_.insert(capture_decl->identifier_token().value(), symbol_node);
			if (capture_decl->type_node().is<TypeSpecifierNode>()) {
				const CanonicalTypeId tid = canonicalizeType(capture_decl->type_node().as<TypeSpecifierNode>());
				const StringHandle name = capture_decl->identifier_token().handle();
				if (name.isValid()) {
					addLocalType(name, tid);
				}
			}
		}
	}

	if (isPlaceholderAutoType(lambda_info.return_type)) {
		if (auto deduced_type = deducePlaceholderReturnType(lambda_info.lambda_body, lambda_info.return_type);
			deduced_type.has_value()) {
			lambda_info.return_type = deduced_type->type();
			lambda_info.return_type_index = deduced_type->type_index();
			lambda_info.returns_reference =
				deduced_type->is_reference() || deduced_type->is_rvalue_reference();
			int deduced_size = getTypeSpecSizeBits(*deduced_type);
			lambda_info.return_size = lambda_info.returns_reference ? 64 : deduced_size;
		}
	}

	SemanticContext lambda_ctx;
	if (!isPlaceholderAutoType(lambda_info.return_type)) {
		TypeSpecifierNode lambda_return_type(
			lambda_info.return_type,
			lambda_info.return_type_index,
			lambda_info.return_size,
			lambda_info.lambda_token);
		if (lambda_info.returns_reference) {
			lambda_return_type.set_reference_qualifier(ReferenceQualifier::LValueReference);
			lambda_return_type.set_size_in_bits(64);
		}
		lambda_ctx.current_function_return_type_id = canonicalizeType(lambda_return_type);
	}
	normalizeStatement(lambda_info.lambda_body, lambda_ctx);
	lambda_info.normalized_deduced_auto_types_generation = lambda_info.deduced_auto_types_generation;
}

ASTNode SemanticAnalysis::normalizeRangedForLoopDecl(const VariableDeclarationNode& original_var_decl,
	const TypeSpecifierNode& deduced_type) const {
	return resolveRangedForLoopDeclNode(original_var_decl, deduced_type);
}

ASTNode SemanticAnalysis::normalizeRangedForLoopDecl(const VariableDeclarationNode& original_var_decl,
	const TypeSpecifierNode& range_type,
	const TypeSpecifierNode& begin_return_type,
	const FunctionDeclarationNode* dereference_func) const {
	if (!isPlaceholderAutoType(original_var_decl.declaration().type_node().as<TypeSpecifierNode>().type())) {
		return original_var_decl.declaration_node();
	}

	if (begin_return_type.pointer_depth() > 0) {
		TypeSpecifierNode deduced_loop_type = begin_return_type;
		deduced_loop_type.remove_pointer_level();
		return resolveRangedForLoopDeclNode(original_var_decl, deduced_loop_type);
	}

	if (dereference_func) {
		TypeSpecifierNode deduced_loop_type = dereference_func->decl_node().type_node().as<TypeSpecifierNode>();
		deduced_loop_type.set_reference_qualifier(ReferenceQualifier::None);
		return resolveRangedForLoopDeclNode(original_var_decl, deduced_loop_type);
	}

	throw InternalError(std::string(StringBuilder()
		.append("Could not deduce range-for element type from iterator type '")
		.append(begin_return_type.getReadableString())
		.append("' for range type '")
		.append(range_type.getReadableString())
		.append("'")
		.commit()));
}

ASTNode SemanticAnalysis::normalizeRangedForLoopDecl(const RangedForStatementNode& stmt) {
	const ASTNode loop_var_decl = stmt.get_loop_variable_decl();
	if (!loop_var_decl.is<VariableDeclarationNode>()) {
		return loop_var_decl;
	}

	const VariableDeclarationNode& original_var_decl = loop_var_decl.as<VariableDeclarationNode>();
	const ASTNode range_expr = stmt.get_range_expression();
	std::optional<TypeSpecifierNode> range_type;
	if (range_expr.is<ExpressionNode>() &&
		std::holds_alternative<IdentifierNode>(range_expr.as<ExpressionNode>())) {
		const auto& range_ident = std::get<IdentifierNode>(range_expr.as<ExpressionNode>());
		const std::optional<ASTNode> range_symbol = symbols_.lookup(range_ident.name());
		if (range_symbol.has_value()) {
			if (const DeclarationNode* range_decl_ptr = get_decl_from_symbol(*range_symbol)) {
				range_type = range_decl_ptr->type_node().as<TypeSpecifierNode>();
			}
		}
	}
	if (!range_type.has_value()) {
		const CanonicalTypeId range_type_id = inferExpressionType(range_expr);
		if (!range_type_id) {
			return original_var_decl.declaration_node();
		}
		range_type = materializeTypeSpecifier(type_context_.get(range_type_id));
	}

	if (range_type->is_array()) {
		const_cast<RangedForStatementNode&>(stmt).set_resolved_dereference_function(nullptr);
		return resolveRangedForLoopDeclNode(original_var_decl, *range_type);
	}

	if (range_type->type() != Type::Struct ||
		!range_type->type_index().is_valid() ||
		range_type->type_index().value >= gTypeInfo.size()) {
		return original_var_decl.declaration_node();
	}

	const StructTypeInfo* struct_info = gTypeInfo[range_type->type_index().value].getStructInfo();
	if (!struct_info) {
		return original_var_decl.declaration_node();
	}

	const StructMemberFunction* begin_func = struct_info->findMemberFunction("begin"sv);
	if (!begin_func || !begin_func->function_decl.is<FunctionDeclarationNode>()) {
		return original_var_decl.declaration_node();
	}

	const auto& begin_func_decl = begin_func->function_decl.as<FunctionDeclarationNode>();
	const TypeSpecifierNode& begin_return_type = begin_func_decl.decl_node().type_node().as<TypeSpecifierNode>();
	const bool prefer_const_deref = range_type->is_const() || begin_func->is_const();
	const FunctionDeclarationNode* dereference_func = nullptr;
	if (begin_return_type.pointer_depth() == 0) {
		dereference_func = resolveRangedForIteratorDereference(begin_return_type, prefer_const_deref);
	}
	const_cast<RangedForStatementNode&>(stmt).set_resolved_dereference_function(dereference_func);
	return normalizeRangedForLoopDecl(original_var_decl, *range_type, begin_return_type, dereference_func);
}

const FunctionDeclarationNode* SemanticAnalysis::resolveRangedForIteratorDereference(
	const TypeSpecifierNode& iterator_type,
	bool prefer_const) const {
	return getRangeIteratorDereferenceFunctionForSema(iterator_type, prefer_const);
}

void SemanticAnalysis::resolveRemainingAutoReturns() {
	auto& nodes = const_cast<std::vector<ASTNode>&>(parser_.get_nodes());
	for (auto& node : nodes) {
		resolveRemainingAutoReturnsInNode(node);
	}
}

void SemanticAnalysis::resolveRemainingAutoReturnsInNode(ASTNode& node) {
	if (!node.has_value()) {
		return;
	}

	if (node.is<FunctionDeclarationNode>()) {
		FunctionDeclarationNode& func = node.as<FunctionDeclarationNode>();
		const ASTNode type_node = func.decl_node().type_node();
		if (type_node.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& return_type = type_node.as<TypeSpecifierNode>();
			if (isPlaceholderAutoType(return_type.type())) {
				pushScope();
				auto cleanup = ScopeGuard([this]() { popScope(); });
				for (const auto& param_node : func.parameter_nodes()) {
					if (!param_node.is<DeclarationNode>()) {
						continue;
					}
					const auto& param_decl = param_node.as<DeclarationNode>();
					const ASTNode param_type_node = param_decl.type_node();
					if (!param_type_node.is<TypeSpecifierNode>()) {
						continue;
					}
					const CanonicalTypeId tid = canonicalizeType(param_type_node.as<TypeSpecifierNode>());
					const StringHandle name = param_decl.identifier_token().handle();
					if (name.isValid()) {
						addLocalType(name, tid);
					}
				}
				if (auto deduced_type = deducePlaceholderReturnType(func.get_definition().value_or(ASTNode{}), return_type.type());
					deduced_type.has_value()) {
					func.decl_node().set_type_node(ASTNode::emplace_node<TypeSpecifierNode>(*deduced_type));
					parser_.compute_and_set_mangled_name(func, true);
				}
			}
		}
		return;
	}

	if (node.is<StructDeclarationNode>()) {
		for (auto& member_func : node.as<StructDeclarationNode>().member_functions()) {
			resolveRemainingAutoReturnsInNode(member_func.function_declaration);
		}
		return;
	}

	if (node.is<NamespaceDeclarationNode>()) {
		auto& declarations = const_cast<std::vector<ASTNode>&>(node.as<NamespaceDeclarationNode>().declarations());
		for (auto& decl : declarations) {
			resolveRemainingAutoReturnsInNode(decl);
		}
		return;
	}

	if (node.is<BlockNode>()) {
		for (const auto& stmt : node.as<BlockNode>().get_statements()) {
			ASTNode& mutable_stmt = const_cast<ASTNode&>(stmt);
			resolveRemainingAutoReturnsInNode(mutable_stmt);
		}
	}
}

std::optional<TypeSpecifierNode> SemanticAnalysis::deducePlaceholderReturnType(const ASTNode& body, Type placeholder_type) {
	if (!body.has_value()) {
		return std::nullopt;
	}

	std::optional<TypeSpecifierNode> deduced_type;

	auto get_expression_type_for_return = [&](const ASTNode& expr_node) -> std::optional<TypeSpecifierNode> {
		// Prefer semantic inference so callable-object return deduction uses the
		// sema-resolved operator() target when available.
		const CanonicalTypeId inferred_type_id = inferExpressionType(expr_node);
		if (inferred_type_id) {
			return materializeTypeSpecifier(type_context_.get(inferred_type_id));
		}

		auto expr_type = parser_.get_expression_type(expr_node);
		if (expr_type.has_value() && !isPlaceholderAutoType(expr_type->type())) {
			return expr_type;
		}
		return std::nullopt;
	};

	std::function<bool(const ASTNode&)> visit_returns = [&](const ASTNode& node) -> bool {
		if (!node.has_value()) {
			return true;
		}

		if (node.is<ReturnStatementNode>()) {
			const ReturnStatementNode& ret = node.as<ReturnStatementNode>();
			TypeSpecifierNode current_type(Type::Void, TypeQualifier::None, 0, ret.return_token());
			if (ret.expression().has_value()) {
				auto expr_type = get_expression_type_for_return(*ret.expression());
				if (!expr_type.has_value()) {
					return false;
				}
				current_type = finalizePlaceholderDeduction(placeholder_type, *expr_type);
			}

			if (!deduced_type.has_value()) {
				deduced_type = current_type;
				return true;
			}

			return placeholderReturnTypesMatch(*deduced_type, current_type);
		}

		if (node.is<BlockNode>()) {
			pushScope();
			auto guard = ScopeGuard([this]() { popScope(); });
			for (const auto& stmt : node.as<BlockNode>().get_statements()) {
				if (stmt.is<VariableDeclarationNode>()) {
					const auto& var = stmt.as<VariableDeclarationNode>();
					if (var.declaration().type_node().is<TypeSpecifierNode>()) {
						const CanonicalTypeId tid = canonicalizeType(var.declaration().type_node().as<TypeSpecifierNode>());
						const StringHandle name = var.declaration().identifier_token().handle();
						if (name.isValid()) {
							addLocalType(name, tid);
						}
					}
				}
				if (!visit_returns(stmt)) {
					return false;
				}
			}
			return true;
		}

		if (node.is<IfStatementNode>()) {
			const auto& stmt = node.as<IfStatementNode>();
			pushScope();
			auto guard = ScopeGuard([this]() { popScope(); });
			if (stmt.has_init() && !visit_returns(stmt.get_init_statement().value())) {
				return false;
			}
			return visit_returns(stmt.get_then_statement()) &&
				(!stmt.has_else() || visit_returns(stmt.get_else_statement().value()));
		}

		if (node.is<ForStatementNode>()) {
			const auto& stmt = node.as<ForStatementNode>();
			pushScope();
			auto guard = ScopeGuard([this]() { popScope(); });
			if (stmt.has_init() && !visit_returns(stmt.get_init_statement().value())) {
				return false;
			}
			return visit_returns(stmt.get_body_statement());
		}

		if (node.is<WhileStatementNode>()) {
			return visit_returns(node.as<WhileStatementNode>().get_body_statement());
		}

		if (node.is<DoWhileStatementNode>()) {
			return visit_returns(node.as<DoWhileStatementNode>().get_body_statement());
		}

		if (node.is<RangedForStatementNode>()) {
			const auto& stmt = node.as<RangedForStatementNode>();
			pushScope();
			auto guard = ScopeGuard([this]() { popScope(); });
			if (stmt.has_init_statement() && !visit_returns(*stmt.get_init_statement())) {
				return false;
			}
			ASTNode loop_decl_node = normalizeRangedForLoopDecl(stmt);
			if (loop_decl_node.is<DeclarationNode>()) {
				const auto& loop_decl = loop_decl_node.as<DeclarationNode>();
				if (loop_decl.type_node().is<TypeSpecifierNode>()) {
					const CanonicalTypeId tid = canonicalizeType(loop_decl.type_node().as<TypeSpecifierNode>());
					const StringHandle name = loop_decl.identifier_token().handle();
					if (name.isValid()) {
						addLocalType(name, tid);
					}
				}
			}
			return visit_returns(stmt.get_body_statement());
		}

		if (node.is<SwitchStatementNode>()) {
			return visit_returns(node.as<SwitchStatementNode>().get_body());
		}

		if (node.is<CaseLabelNode>()) {
			const auto& case_node = node.as<CaseLabelNode>();
			return !case_node.has_statement() || visit_returns(*case_node.get_statement());
		}

		if (node.is<DefaultLabelNode>()) {
			const auto& default_node = node.as<DefaultLabelNode>();
			return !default_node.has_statement() || visit_returns(*default_node.get_statement());
		}

		if (node.is<TryStatementNode>()) {
			const auto& try_node = node.as<TryStatementNode>();
			if (!visit_returns(try_node.try_block())) {
				return false;
			}
			for (const auto& handler : try_node.catch_clauses()) {
				if (handler.is<CatchClauseNode>()) {
					pushScope();
					auto guard = ScopeGuard([this]() { popScope(); });
					const auto& catch_clause = handler.as<CatchClauseNode>();
					if (catch_clause.exception_declaration().has_value() &&
						catch_clause.exception_declaration()->is<DeclarationNode>()) {
						const auto& catch_decl = catch_clause.exception_declaration()->as<DeclarationNode>();
						if (catch_decl.type_node().is<TypeSpecifierNode>()) {
							const CanonicalTypeId tid = canonicalizeType(catch_decl.type_node().as<TypeSpecifierNode>());
							const StringHandle name = catch_decl.identifier_token().handle();
							if (name.isValid()) {
								addLocalType(name, tid);
							}
						}
					}
					if (!visit_returns(catch_clause.body())) {
						return false;
					}
				}
			}
			return true;
		}

		return true;
	};

	if (!visit_returns(body)) {
		return std::nullopt;
	}

	return deduced_type.value_or(TypeSpecifierNode(
		Type::Void,
		TypeQualifier::None,
		get_type_size_bits(Type::Void)));
}

TypeSpecifierNode SemanticAnalysis::finalizePlaceholderDeduction(Type placeholder_type, const TypeSpecifierNode& deduced_type) const {
	return finalizePlaceholderTypeDeduction(placeholder_type, deduced_type);
}

// --- Top-level dispatch ---

void SemanticAnalysis::normalizeTopLevelNode(const ASTNode& node) {
	if (!node.has_value()) return;
	stats_.roots_visited++;

	if (node.is<FunctionDeclarationNode>()) {
		normalizeFunctionDeclaration(node.as<FunctionDeclarationNode>());
	}
	else if (node.is<StructDeclarationNode>()) {
		normalizeStructDeclaration(node.as<StructDeclarationNode>());
	}
	else if (node.is<NamespaceDeclarationNode>()) {
		normalizeNamespace(node.as<NamespaceDeclarationNode>());
	}
	else if (node.is<ConstructorDeclarationNode>()) {
		auto& ctor = node.as<ConstructorDeclarationNode>();
		const auto& def = ctor.get_definition();
		if (def.has_value()) {
			SemanticContext ctx;
			normalizeStatement(*def, ctx);
		}
	}
	else if (node.is<DestructorDeclarationNode>()) {
		auto& dtor = node.as<DestructorDeclarationNode>();
		const auto& def = dtor.get_definition();
		if (def.has_value()) {
			SemanticContext ctx;
			normalizeStatement(*def, ctx);
		}
	}
	else if (node.is<VariableDeclarationNode>()) {
		auto& var = node.as<VariableDeclarationNode>();
		const auto& init = var.initializer();
		if (init.has_value()) {
			SemanticContext ctx;
			normalizeExpression(*init, ctx);
		}
	}
	// Template declarations, forward declarations, typedefs, using directives,
	// enums, concepts - no semantic normalization needed in Phase 1
}

// --- Declaration handlers ---

void SemanticAnalysis::normalizeFunctionDeclaration(const FunctionDeclarationNode& func) {
	const auto& def = func.get_definition();
	if (!def.has_value()) return;  // Forward declaration only

	SemanticContext ctx;
	// Track return type for return-statement conversion annotation
	ASTNode type_node = func.decl_node().type_node();
	if (type_node.has_value() && type_node.is<TypeSpecifierNode>()) {
		ctx.current_function_return_type_id = canonicalizeType(type_node.as<TypeSpecifierNode>());
	}

	// Push a scope for this function's parameters
	pushScope();
	for (const auto& param_node : func.parameter_nodes()) {
		if (param_node.is<DeclarationNode>()) {
			const auto& decl = param_node.as<DeclarationNode>();
			const ASTNode ptype = decl.type_node();
			if (ptype.has_value() && ptype.is<TypeSpecifierNode>()) {
				const CanonicalTypeId tid = canonicalizeType(ptype.as<TypeSpecifierNode>());
				const StringHandle pname = decl.identifier_token().handle();
				if (pname.isValid())
					addLocalType(pname, tid);
			}
		}
	}

	normalizeStatement(*def, ctx);
	popScope();
}

void SemanticAnalysis::normalizeStructDeclaration(const StructDeclarationNode& decl) {
	// Walk member function bodies (includes constructors and destructors)
	for (const auto& member_func : decl.member_functions()) {
		const auto& func_node = member_func.function_declaration;
		if (!func_node.has_value()) continue;

		if (member_func.is_constructor && func_node.is<ConstructorDeclarationNode>()) {
			const auto& def = func_node.as<ConstructorDeclarationNode>().get_definition();
			if (def.has_value()) {
				SemanticContext ctx;
				normalizeStatement(*def, ctx);
			}
		}
		else if (member_func.is_destructor && func_node.is<DestructorDeclarationNode>()) {
			const auto& def = func_node.as<DestructorDeclarationNode>().get_definition();
			if (def.has_value()) {
				SemanticContext ctx;
				normalizeStatement(*def, ctx);
			}
		}
		else if (func_node.is<FunctionDeclarationNode>()) {
			normalizeFunctionDeclaration(func_node.as<FunctionDeclarationNode>());
		}
	}
}

void SemanticAnalysis::normalizeNamespace(const NamespaceDeclarationNode& ns) {
	for (auto& decl : ns.declarations()) {
		normalizeTopLevelNode(decl);
	}
}

// --- Statement handler ---

void SemanticAnalysis::normalizeStatement(const ASTNode& node, const SemanticContext& ctx) {
	if (!node.has_value()) return;
	stats_.statements_visited++;

	if (node.is<BlockNode>()) {
		normalizeBlock(node.as<BlockNode>(), ctx);
	}
	else if (node.is<ExpressionNode>()) {
		normalizeExpression(node, ctx);
	}
	else if (node.is<ReturnStatementNode>()) {
		const auto& ret = node.as<ReturnStatementNode>();
		const auto& expr = ret.expression();
		if (expr.has_value()) {
			// Try to annotate the return expression with implicit cast info
			// before the normal traversal (which just counts).
			if (ctx.current_function_return_type_id) {
				tryAnnotateReturnConversion(*expr, ctx);
			}
			normalizeExpression(*expr, ctx);
		}
	}
	else if (node.is<VariableDeclarationNode>()) {
		const auto& var = node.as<VariableDeclarationNode>();
		// Record local variable type in the current scope for expression inference
		const auto& decl = var.declaration();
		const ASTNode vtype = decl.type_node();
		CanonicalTypeId decl_type_id{};  // default: invalid (value==0)
		if (vtype.has_value() && vtype.is<TypeSpecifierNode>()) {
			decl_type_id = canonicalizeType(vtype.as<TypeSpecifierNode>());
			const StringHandle vname = decl.identifier_token().handle();
			if (vname.isValid())
				addLocalType(vname, decl_type_id);
		}
		const auto& init = var.initializer();
		if (init.has_value()) {
			// Annotate the initializer with any needed implicit conversion to the declared type.
			if (decl_type_id)
				tryAnnotateConversion(*init, decl_type_id);
			normalizeExpression(*init, ctx);
		}
	}
	else if (node.is<IfStatementNode>()) {
		const auto& stmt = node.as<IfStatementNode>();
		// C++17 [stmt.if]/2: variables declared in the init-statement go out of
		// scope at the end of the if statement.  Push/pop a scope to mirror this.
		pushScope();
		if (stmt.has_init()) {
			normalizeStatement(stmt.get_init_statement().value(), ctx);
		}
		// C++20 [stmt.select]: the condition is contextually converted to bool.
		tryAnnotateContextualBool(stmt.get_condition());
		normalizeExpression(stmt.get_condition(), ctx);
		normalizeStatement(stmt.get_then_statement(), ctx);
		if (stmt.has_else()) {
			normalizeStatement(stmt.get_else_statement().value(), ctx);
		}
		popScope();
	}
	else if (node.is<ForStatementNode>()) {
		const auto& stmt = node.as<ForStatementNode>();
		// C++20 [stmt.for]/1: the init-statement's scope extends to the end of
		// the for statement.  Push/pop a scope to prevent type leakage.
		pushScope();
		if (stmt.has_init()) {
			normalizeStatement(stmt.get_init_statement().value(), ctx);
		}
		if (stmt.has_condition()) {
			// C++20 [stmt.for]: the condition is contextually converted to bool.
			tryAnnotateContextualBool(stmt.get_condition().value());
			normalizeExpression(stmt.get_condition().value(), ctx);
		}
		if (stmt.has_update()) {
			normalizeExpression(stmt.get_update_expression().value(), ctx);
		}
		normalizeStatement(stmt.get_body_statement(), ctx);
		popScope();
	}
	else if (node.is<WhileStatementNode>()) {
		const auto& stmt = node.as<WhileStatementNode>();
		// C++20 [stmt.while]: the condition is contextually converted to bool.
		tryAnnotateContextualBool(stmt.get_condition());
		normalizeExpression(stmt.get_condition(), ctx);
		normalizeStatement(stmt.get_body_statement(), ctx);
	}
	else if (node.is<DoWhileStatementNode>()) {
		const auto& stmt = node.as<DoWhileStatementNode>();
		normalizeStatement(stmt.get_body_statement(), ctx);
		// C++20 [stmt.do]: the condition is contextually converted to bool.
		tryAnnotateContextualBool(stmt.get_condition());
		normalizeExpression(stmt.get_condition(), ctx);
	}
	else if (node.is<SwitchStatementNode>()) {
		const auto& stmt = node.as<SwitchStatementNode>();
		normalizeExpression(stmt.get_condition(), ctx);
		normalizeStatement(stmt.get_body(), ctx);
	}
	else if (node.is<RangedForStatementNode>()) {
		const auto& stmt = node.as<RangedForStatementNode>();
		pushScope();
		if (stmt.has_init_statement()) {
			normalizeStatement(*stmt.get_init_statement(), ctx);
		}
		normalizeExpression(stmt.get_range_expression(), ctx);
		ASTNode loop_decl_node = normalizeRangedForLoopDecl(stmt);
		if (loop_decl_node.is<DeclarationNode>()) {
			const auto& loop_decl = loop_decl_node.as<DeclarationNode>();
			const ASTNode loop_type_node = loop_decl.type_node();
			if (loop_type_node.is<TypeSpecifierNode>()) {
				const CanonicalTypeId tid = canonicalizeType(loop_type_node.as<TypeSpecifierNode>());
				const StringHandle name = loop_decl.identifier_token().handle();
				if (name.isValid()) {
					addLocalType(name, tid);
				}
			}
		}
		normalizeStatement(stmt.get_body_statement(), ctx);
		popScope();
	}
	else if (node.is<TryStatementNode>()) {
		const auto& stmt = node.as<TryStatementNode>();
		normalizeStatement(stmt.try_block(), ctx);
		for (const auto& handler : stmt.catch_clauses()) {
			if (handler.is<CatchClauseNode>()) {
				normalizeStatement(handler.as<CatchClauseNode>().body(), ctx);
			}
		}
	}
	// BreakStatementNode, ContinueStatementNode, GotoStatementNode,
	// LabelStatementNode, ThrowStatementNode, etc. - no children to walk in Phase 1
}

void SemanticAnalysis::normalizeBlock(const BlockNode& block, const SemanticContext& ctx) {
	pushScope();
	for (auto& stmt : block.get_statements()) {
		normalizeStatement(stmt, ctx);
	}
	popScope();
}

// --- Expression handler (Phase 2: counting + type inference for annotatable nodes) ---

SemanticExprInfo SemanticAnalysis::normalizeExpression(const ASTNode& node, const SemanticContext& ctx) {
	if (!node.has_value()) return {};
	stats_.expressions_visited++;

	// Walk children for counting; Phase 2 type annotation is done in tryAnnotateReturnConversion.

	if (node.is<ExpressionNode>()) {
		// Walk into variant-based expression nodes to count children
		const auto& expr = node.as<ExpressionNode>();
		std::visit([&](const auto& e) {
			using T = std::decay_t<decltype(e)>;
			if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
				const std::string_view op = e.op();
				const bool is_arithmetic =
					op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
				const bool is_comparison =
					op == "<" || op == ">" || op == "<=" || op == ">=" ||
					op == "==" || op == "!=";
				const bool is_logical = op == "&&" || op == "||";
				if ((is_arithmetic || is_comparison) &&
					e.get_lhs().template is<ExpressionNode>() &&
					e.get_rhs().template is<ExpressionNode>()) {
					tryAnnotateBinaryOperandConversions(e);
				}
				// C++20 [expr.log.and], [expr.log.or]: each operand is
				// contextually converted to bool.
				if (is_logical) {
					tryAnnotateContextualBool(e.get_lhs());
					tryAnnotateContextualBool(e.get_rhs());
				}
				// For simple assignment, annotate the RHS with the LHS type.
				if (op == "=" &&
					e.get_lhs().template is<ExpressionNode>() &&
					e.get_rhs().template is<ExpressionNode>()) {
					const CanonicalTypeId lhs_type_id = inferExpressionType(e.get_lhs());
					if (lhs_type_id)
						tryAnnotateConversion(e.get_rhs(), lhs_type_id);
				}
				normalizeExpression(e.get_lhs(), ctx);
				normalizeExpression(e.get_rhs(), ctx);
			}
			else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
				// C++20 [expr.unary.op]/9: the operand of ! is contextually
				// converted to bool.
				if (e.op() == "!") {
					tryAnnotateContextualBool(e.get_operand());
				}
				normalizeExpression(e.get_operand(), ctx);
			}
			else if constexpr (std::is_same_v<T, TernaryOperatorNode>) {
				// C++20 [expr.cond]/1: the condition is contextually converted to bool.
				tryAnnotateContextualBool(e.condition());
				// C++20 [expr.cond]/7: usual arithmetic conversions on branches.
				tryAnnotateTernaryBranchConversions(e);
				normalizeExpression(e.condition(), ctx);
				normalizeExpression(e.true_expr(), ctx);
				normalizeExpression(e.false_expr(), ctx);
			}
			else if constexpr (std::is_same_v<T, FunctionCallNode>) {
				tryAnnotateCallArgConversions(e);
				tryResolveCallableOperator(e);
				for (const auto& arg : e.arguments()) {
					normalizeExpression(arg, ctx);
				}
			}
			else if constexpr (std::is_same_v<T, ConstructorCallNode>) {
				for (const auto& arg : e.arguments()) {
					normalizeExpression(arg, ctx);
				}
			}
			else if constexpr (std::is_same_v<T, MemberAccessNode>) {
				normalizeExpression(e.object(), ctx);
			}
			else if constexpr (std::is_same_v<T, PointerToMemberAccessNode>) {
				normalizeExpression(e.object(), ctx);
				normalizeExpression(e.member_pointer(), ctx);
			}
			else if constexpr (std::is_same_v<T, MemberFunctionCallNode>) {
				normalizeExpression(e.object(), ctx);
				for (const auto& arg : e.arguments()) {
					normalizeExpression(arg, ctx);
				}
			}
			else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
				normalizeExpression(e.array_expr(), ctx);
				normalizeExpression(e.index_expr(), ctx);
			}
			else if constexpr (std::is_same_v<T, SizeofExprNode>) {
				if (!e.is_type()) {
					normalizeExpression(e.type_or_expr(), ctx);
				}
			}
			else if constexpr (std::is_same_v<T, AlignofExprNode>) {
				if (!e.is_type()) {
					normalizeExpression(e.type_or_expr(), ctx);
				}
			}
			else if constexpr (std::is_same_v<T, NewExpressionNode>) {
				if (e.size_expr().has_value()) {
					normalizeExpression(*e.size_expr(), ctx);
				}
				for (const auto& arg : e.constructor_args()) {
					normalizeExpression(arg, ctx);
				}
				for (const auto& arg : e.placement_args()) {
					normalizeExpression(arg, ctx);
				}
			}
			else if constexpr (std::is_same_v<T, DeleteExpressionNode>) {
				normalizeExpression(e.expr(), ctx);
			}
			else if constexpr (std::is_same_v<T, StaticCastNode>) {
				normalizeExpression(e.expr(), ctx);
			}
			else if constexpr (std::is_same_v<T, DynamicCastNode>) {
				normalizeExpression(e.expr(), ctx);
			}
			else if constexpr (std::is_same_v<T, ConstCastNode>) {
				normalizeExpression(e.expr(), ctx);
			}
			else if constexpr (std::is_same_v<T, ReinterpretCastNode>) {
				normalizeExpression(e.expr(), ctx);
			}
			else if constexpr (std::is_same_v<T, TypeidNode>) {
				if (!e.is_type()) {
					normalizeExpression(e.operand(), ctx);
				}
			}
			else if constexpr (std::is_same_v<T, LambdaExpressionNode>) {
				for (const auto& capture : e.captures()) {
					// Non-init captures are stored as identifier tokens only; the
					// initializer is the only child expression carried by the node.
					if (capture.has_initializer()) {
						normalizeExpression(*capture.initializer(), ctx);
					}
				}
				// Build a fresh SemanticContext for the lambda body so that the
				// enclosing function's return type does not leak into the lambda's
				// return-statement annotations.  If the lambda has an explicit
				// trailing return type, use it; otherwise leave the return type
				// unset so that tryAnnotateReturnConversion is a no-op (auto
				// deduction is handled by codegen).
				SemanticContext lambda_ctx;
				if (e.return_type().has_value() &&
					e.return_type()->template is<TypeSpecifierNode>()) {
					lambda_ctx.current_function_return_type_id =
						canonicalizeType(e.return_type()->template as<TypeSpecifierNode>());
				}
				// Push a scope for lambda parameters so they are visible inside
				// the body but do not leak into the enclosing scope.
				pushScope();
				for (const auto& param : e.parameters()) {
					if (param.template is<DeclarationNode>()) {
						const auto& decl = param.template as<DeclarationNode>();
						const ASTNode ptype = decl.type_node();
						if (ptype.has_value() && ptype.template is<TypeSpecifierNode>()) {
							const CanonicalTypeId tid = canonicalizeType(ptype.template as<TypeSpecifierNode>());
							const StringHandle pname = decl.identifier_token().handle();
							if (pname.isValid())
								addLocalType(pname, tid);
						}
						for (const auto& dim : decl.array_dimensions()) {
							normalizeExpression(dim, lambda_ctx);
						}
						if (decl.has_default_value()) {
							normalizeExpression(decl.default_value(), lambda_ctx);
						}
					}
					else {
						throw InternalError("Lambda parameter must be a DeclarationNode");
					}
				}
				normalizeStatement(e.body(), lambda_ctx);
				popScope();
			}
			else if constexpr (std::is_same_v<T, FoldExpressionNode>) {
				if (e.init_expr().has_value()) {
					normalizeExpression(*e.init_expr(), ctx);
				}
				if (e.pack_expr().has_value()) {
					normalizeExpression(*e.pack_expr(), ctx);
				}
			}
			else if constexpr (std::is_same_v<T, PackExpansionExprNode>) {
				normalizeExpression(e.pattern(), ctx);
			}
			else if constexpr (std::is_same_v<T, NoexceptExprNode>) {
				normalizeExpression(e.expr(), ctx);
			}
			else if constexpr (std::is_same_v<T, InitializerListConstructionNode>) {
				for (const auto& element : e.elements()) {
					normalizeExpression(element, ctx);
				}
			}
			else if constexpr (std::is_same_v<T, ThrowExpressionNode>) {
				if (e.expression().has_value()) {
					normalizeExpression(*e.expression(), ctx);
				}
			}
			// Leaf nodes (IdentifierNode, QualifiedIdentifierNode, StringLiteralNode,
			// NumericLiteralNode, BoolLiteralNode, SizeofPackNode, OffsetofExprNode,
			// TypeTraitExprNode, TemplateParameterReferenceNode, PseudoDestructorCallNode)
			// do not recurse into child expressions here.
		}, expr);
	}

	return {};
}

// --- Helpers ---

CanonicalTypeId SemanticAnalysis::canonicalizeType(const TypeSpecifierNode& type) {
	CanonicalTypeDesc desc;
	desc.base_type = type.type();
	desc.type_index = type.type_index();
	desc.base_cv = type.cv_qualifier();
	desc.ref_qualifier = type.reference_qualifier();

	// Copy pointer levels
	for (const auto& pl : type.pointer_levels()) {
		desc.pointer_levels.push_back(pl);
	}

	// Copy array dimensions
	if (type.is_array()) {
		for (auto dim : type.array_dimensions()) {
			desc.array_dimensions.push_back(dim);
		}
	}

	// Function signature
	if ((type.is_function_pointer() || type.is_member_function_pointer()) && type.has_function_signature()) {
		desc.function_signature = type.function_signature();
		desc.flags = desc.flags | CanonicalTypeFlags::IsFunctionType;
	}

	auto id = type_context_.intern(desc);
	stats_.canonical_types_interned++;
	return id;
}

std::optional<SemanticSlot> SemanticAnalysis::getSlot(const void* key) const {
	auto it = semantic_slots_.find(key);
	if (it != semantic_slots_.end())
		return it->second;
	return std::nullopt;
}

const FunctionDeclarationNode* SemanticAnalysis::getResolvedOpCall(const FunctionCallNode* key) const {
	auto it = op_call_table_.find(key);
	return it != op_call_table_.end() ? it->second : nullptr;
}

void SemanticAnalysis::setSlot(const void* key, const SemanticSlot& slot) {
	semantic_slots_[key] = slot;
}

CastInfoIndex SemanticAnalysis::allocateCastInfo(const ImplicitCastInfo& info) {
	cast_info_table_.push_back(info);
	stats_.cast_infos_allocated++;
	// CastInfoIndex is 1-based; 0 is the "no cast" sentinel
	return CastInfoIndex{static_cast<uint16_t>(cast_info_table_.size())};
}

// --- Scope tracking ---

void SemanticAnalysis::pushScope() {
	scope_stack_.emplace_back();
}

void SemanticAnalysis::popScope() {
	if (!scope_stack_.empty())
		scope_stack_.pop_back();
}

void SemanticAnalysis::addLocalType(StringHandle name, CanonicalTypeId type_id) {
	if (!scope_stack_.empty())
		scope_stack_.back()[name] = type_id;
}

CanonicalTypeId SemanticAnalysis::lookupLocalType(StringHandle name) const {
	for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
		auto found = it->find(name);
		if (found != it->end())
			return found->second;
	}
	return {};
}

// --- Expression type inference ---

CanonicalTypeId SemanticAnalysis::inferExpressionType(const ASTNode& node) {
	if (!node.has_value()) return {};

	if (node.is<ExpressionNode>()) {
		const auto& expr = node.as<ExpressionNode>();
		return std::visit([this](const auto& e) -> CanonicalTypeId {
			using T = std::decay_t<decltype(e)>;
			if constexpr (std::is_same_v<T, NumericLiteralNode>) {
				CanonicalTypeDesc desc;
				desc.base_type = e.type();
				return type_context_.intern(desc);
			}
			else if constexpr (std::is_same_v<T, BoolLiteralNode>) {
				CanonicalTypeDesc desc;
				desc.base_type = Type::Bool;
				return type_context_.intern(desc);
			}
			else if constexpr (std::is_same_v<T, IdentifierNode>) {
				return lookupLocalType(e.nameHandle());
			}
			else if constexpr (std::is_same_v<T, MemberAccessNode>) {
				const CanonicalTypeId object_type_id = inferExpressionType(e.object());
				if (!object_type_id) {
					return {};
				}
				const CanonicalTypeDesc& object_desc = type_context_.get(object_type_id);
				if (object_desc.base_type != Type::Struct ||
					!object_desc.type_index.is_valid() ||
					object_desc.type_index.value >= gTypeInfo.size()) {
					return {};
				}
				const StructTypeInfo* struct_info = gTypeInfo[object_desc.type_index.value].getStructInfo();
				if (!struct_info) {
					return {};
				}
				const StringHandle member_name = e.member_token().handle();
				for (const auto& member : struct_info->members) {
					if (member.name != member_name) {
						continue;
					}
					return type_context_.intern(canonicalTypeDescFromStructMember(member, object_desc.base_cv));
				}
				return {};
			}
			else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
				// Unary + and - apply integral promotion: types with rank < int become int.
				const std::string_view op = e.op();
				if (op == "+" || op == "-") {
					const CanonicalTypeId operand_id = inferExpressionType(e.get_operand());
					if (!operand_id) return {};
					const CanonicalTypeDesc& operand_desc = type_context_.get(operand_id);
					const bool is_small_int =
						(is_integer_type(operand_desc.base_type) || operand_desc.base_type == Type::Bool)
						&& get_integer_rank(operand_desc.base_type) < 3;  // rank of int
					if (is_small_int) {
						CanonicalTypeDesc promoted;
						promoted.base_type = Type::Int;
						return type_context_.intern(promoted);
					}
					return operand_id;
				}
				return {};
			}
			else if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
				const std::string_view op = e.op();
				// Comparison and logical operators always produce bool
				if (op == "==" || op == "!=" || op == "<" || op == ">" ||
					op == "<=" || op == ">=" || op == "&&" || op == "||")
				{
					CanonicalTypeDesc desc;
					desc.base_type = Type::Bool;
					return type_context_.intern(desc);
				}
				// Comma: result is the RHS type
				if (op == ",") return inferExpressionType(e.get_rhs());
				// Assignment: result is LHS type (lvalue of LHS)
				if (op == "=" || op == "+=" || op == "-=" || op == "*=" ||
					op == "/=" || op == "%=" || op == "&=" || op == "|=" ||
					op == "^=" || op == "<<=" || op == ">>=")
					return inferExpressionType(e.get_lhs());
				// Arithmetic/bitwise: usual arithmetic conversions
				{
					const CanonicalTypeId lhs_id = inferExpressionType(e.get_lhs());
					const CanonicalTypeId rhs_id = inferExpressionType(e.get_rhs());
					if (!lhs_id || !rhs_id) return {};
					const CanonicalTypeDesc& l = type_context_.get(lhs_id);
					const CanonicalTypeDesc& r = type_context_.get(rhs_id);
					if (l.base_type == Type::Invalid || r.base_type == Type::Invalid) return {};
					if (isPlaceholderAutoType(l.base_type) || isPlaceholderAutoType(r.base_type)) return {};
					if (!l.pointer_levels.empty()   || !r.pointer_levels.empty())    return {};
					const Type common = get_common_type(l.base_type, r.base_type);
					if (common == Type::Invalid) return {};
					CanonicalTypeDesc desc;
					desc.base_type = common;
					return type_context_.intern(desc);
				}
			}
			else if constexpr (std::is_same_v<T, FunctionCallNode>) {
				if (const FunctionDeclarationNode* resolved_callable = getResolvedOpCall(&e)) {
					const ASTNode resolved_return_type = resolved_callable->decl_node().type_node();
					if (resolved_return_type.has_value() && resolved_return_type.is<TypeSpecifierNode>()) {
						return canonicalizeType(resolved_return_type.as<TypeSpecifierNode>());
					}
				}

				tryResolveCallableOperator(e);
				if (const FunctionDeclarationNode* resolved_callable = getResolvedOpCall(&e)) {
					const ASTNode resolved_return_type = resolved_callable->decl_node().type_node();
					if (resolved_return_type.has_value() && resolved_return_type.is<TypeSpecifierNode>()) {
						return canonicalizeType(resolved_return_type.as<TypeSpecifierNode>());
					}
				}

				// Infer result type from the resolved function declaration's return type.
				const DeclarationNode& decl = e.function_declaration();
				const ASTNode ret_type_node = decl.type_node();
				if (ret_type_node.has_value() && ret_type_node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& type_node = ret_type_node.as<TypeSpecifierNode>();
					if (!isPlaceholderAutoType(type_node.type())) {
						return canonicalizeType(type_node);
					}
				}

				const StringHandle callee_name = decl.identifier_token().handle();
				const CanonicalTypeId callee_type_id = callee_name.isValid() ? lookupLocalType(callee_name) : CanonicalTypeId{};
				if (!callee_type_id) return {};

				const CanonicalTypeDesc& callee_desc = type_context_.get(callee_type_id);
				if (callee_desc.base_type == Type::Struct &&
					callee_desc.type_index.is_valid() &&
					callee_desc.type_index.value < gTypeInfo.size()) {
					const StructTypeInfo* struct_info = gTypeInfo[callee_desc.type_index.value].getStructInfo();
					if (!struct_info) {
						return {};
					}

					for (const auto& member_func : struct_info->member_functions) {
						if (member_func.operator_kind == OverloadableOperator::Call &&
							member_func.function_decl.is<FunctionDeclarationNode>()) {
							return canonicalizeType(
								member_func.function_decl.as<FunctionDeclarationNode>().decl_node().type_node().as<TypeSpecifierNode>());
						}
					}
				}

				return {};
			}
			else if constexpr (std::is_same_v<T, MemberFunctionCallNode>) {
				return canonicalizeType(e.function_declaration().decl_node().type_node().template as<TypeSpecifierNode>());
			}
			else if constexpr (std::is_same_v<T, StaticCastNode> ||
			                   std::is_same_v<T, ConstCastNode> ||
			                   std::is_same_v<T, ReinterpretCastNode>) {
				// Explicit casts: the result type is the declared target type.
				const ASTNode& tt = e.target_type();
				if (tt.has_value() && tt.template is<TypeSpecifierNode>())
					return canonicalizeType(tt.template as<TypeSpecifierNode>());
				return {};
			}
			return {};
		}, expr);
	}

	// Handle old-style nodes stored directly (not wrapped in ExpressionNode)
	if (node.is<NumericLiteralNode>()) {
		CanonicalTypeDesc desc;
		desc.base_type = node.as<NumericLiteralNode>().type();
		return type_context_.intern(desc);
	}
	if (node.is<BoolLiteralNode>()) {
		CanonicalTypeDesc desc;
		desc.base_type = Type::Bool;
		return type_context_.intern(desc);
	}

	return {};
}

// --- Core conversion annotation helper ---

bool SemanticAnalysis::tryAnnotateConversion(const ASTNode& expr_node, CanonicalTypeId target_type_id) {
	if (!target_type_id) return false;
	if (!expr_node.is<ExpressionNode>()) return false;

	const CanonicalTypeId expr_type_id = inferExpressionType(expr_node);
	if (!expr_type_id) return false;
	if (expr_type_id == target_type_id) return false;  // exact match, no cast needed

	const CanonicalTypeDesc& from_desc = type_context_.get(expr_type_id);
	const CanonicalTypeDesc& to_desc   = type_context_.get(target_type_id);

	// Same base type but different canonical IDs (differ only in qualifiers or type_index,
	// e.g. two UserDefined aliases, const vs non-const, etc.): no primitive conversion needed.
	if (from_desc.base_type == to_desc.base_type) return false;

	// Bail out if either side is not a plain primitive scalar.
	// Checking one lambda against both sides covers all non-handleable types in one place.
	auto is_non_primitive = [](Type t) {
		return t == Type::Struct || t == Type::Enum || t == Type::UserDefined ||
		       t == Type::Invalid || isPlaceholderAutoType(t);
	};
	if (!from_desc.pointer_levels.empty() || !to_desc.pointer_levels.empty()) return false;
	if (!from_desc.array_dimensions.empty() || !to_desc.array_dimensions.empty()) return false;
	if (is_non_primitive(from_desc.base_type) || is_non_primitive(to_desc.base_type)) return false;
	if (from_desc.ref_qualifier != ReferenceQualifier::None) return false;

	const TypeConversionResult conv = can_convert_type(from_desc.base_type, to_desc.base_type);
	if (!conv.is_valid || conv.rank == ConversionRank::UserDefined) return false;

	const StandardConversionKind kind = determineConversionKind(from_desc.base_type, to_desc.base_type);

	ImplicitCastInfo cast_info;
	cast_info.source_type_id = expr_type_id;
	cast_info.target_type_id = target_type_id;
	cast_info.cast_kind = kind;
	cast_info.value_category_after = ValueCategory::PRValue;

	const CastInfoIndex idx = allocateCastInfo(cast_info);

	SemanticSlot slot;
	slot.type_id = target_type_id;
	slot.cast_info_index = idx;
	slot.value_category = ValueCategory::PRValue;

	const void* key = static_cast<const void*>(&expr_node.as<ExpressionNode>());
	setSlot(key, slot);
	stats_.slots_filled++;

	FLASH_LOG(General, Debug, "SemanticAnalysis: annotated conversion ",
		static_cast<int>(from_desc.base_type), " → ",
		static_cast<int>(to_desc.base_type),
		" (kind=", static_cast<int>(kind), ")");
	return true;
}

// --- Return conversion annotation ---

void SemanticAnalysis::tryAnnotateReturnConversion(const ASTNode& expr_node, const SemanticContext& ctx) {
	if (!ctx.current_function_return_type_id) return;
	tryAnnotateConversion(expr_node, *ctx.current_function_return_type_id);
}

// --- Binary operand conversion annotation ---

void SemanticAnalysis::tryAnnotateBinaryOperandConversions(const BinaryOperatorNode& bin_op) {
	const CanonicalTypeId lhs_type_id = inferExpressionType(bin_op.get_lhs());
	const CanonicalTypeId rhs_type_id = inferExpressionType(bin_op.get_rhs());
	if (!lhs_type_id || !rhs_type_id) return;

	const CanonicalTypeDesc& lhs_desc = type_context_.get(lhs_type_id);
	const CanonicalTypeDesc& rhs_desc = type_context_.get(rhs_type_id);

	// Only handle plain primitive types (no pointers, no arrays, no structs, no enums)
	if (!lhs_desc.pointer_levels.empty() || !rhs_desc.pointer_levels.empty()) return;
	if (!lhs_desc.array_dimensions.empty() || !rhs_desc.array_dimensions.empty()) return;
	if (lhs_desc.base_type == Type::Struct || rhs_desc.base_type == Type::Struct) return;
	if (lhs_desc.base_type == Type::Enum   || rhs_desc.base_type == Type::Enum)   return;
	if (lhs_desc.base_type == Type::Invalid || rhs_desc.base_type == Type::Invalid) return;
	if (isPlaceholderAutoType(lhs_desc.base_type) || isPlaceholderAutoType(rhs_desc.base_type)) return;

	const Type common = get_common_type(lhs_desc.base_type, rhs_desc.base_type);
	if (common == Type::Invalid) return;

	// Intern the common type
	CanonicalTypeDesc common_desc;
	common_desc.base_type = common;
	const CanonicalTypeId common_type_id = type_context_.intern(common_desc);

	tryAnnotateConversion(bin_op.get_lhs(), common_type_id);
	tryAnnotateConversion(bin_op.get_rhs(), common_type_id);
}

// --- Contextual bool annotation ---
// C++20 [conv.bool]: A prvalue of arithmetic, unscoped enumeration, pointer, or
// pointer-to-member type can be converted to a prvalue of type bool.
// Used for: if/while/for/do-while conditions, ternary condition, && / || operands.

void SemanticAnalysis::tryAnnotateContextualBool(const ASTNode& expr_node) {
	tryAnnotateConversion(expr_node, bool_type_id_);
}

// --- Callable operator() resolution ---

void SemanticAnalysis::tryResolveCallableOperator(const FunctionCallNode& call_node) {
	// Identify the callee: use the identifier token stored in the call's function_declaration.
	// For a callable object `f(args)`, function_declaration() holds the DeclarationNode of
	// the variable `f`; its identifier token gives us the lookup name.
	const DeclarationNode& callee_decl = call_node.function_declaration();
	const StringHandle callee_name = callee_decl.identifier_token().handle();
	if (!callee_name.isValid()) return;

	// Look up the callee's canonical type from the current scope stack.
	// This correctly handles shadowed names: if a local variable `apply` of struct type
	// shadows a free function `apply`, lookupLocalType returns the struct type.
	const CanonicalTypeId callee_type_id = lookupLocalType(callee_name);
	if (!callee_type_id) return;

	const CanonicalTypeDesc& callee_desc = type_context_.get(callee_type_id);
	if (callee_desc.base_type != Type::Struct) return;
	if (!callee_desc.type_index.is_valid()) return;
	if (callee_desc.type_index.value >= gTypeInfo.size()) return;

	const StructTypeInfo* struct_info = gTypeInfo[callee_desc.type_index.value].getStructInfo();
	if (!struct_info) return;

	const size_t arg_count = call_node.arguments().size();

	// Collect all operator() candidates as ASTNodes for overload resolution.
	std::vector<ASTNode> candidates;
	for (const auto& member_func : struct_info->member_functions) {
		if (member_func.operator_kind != OverloadableOperator::Call) continue;
		if (!member_func.function_decl.is<FunctionDeclarationNode>()) continue;
		candidates.push_back(member_func.function_decl);
	}
	if (candidates.empty()) return;

	// Try to build argument type specifiers for the real overload-resolution path.
	// inferExpressionType handles most expression forms (numeric/bool literals, identifiers,
	// member access, binary ops, sub-calls); parser_.get_expression_type is the fallback for
	// expression kinds not yet covered (e.g. cast expressions, initializer lists).
	// If neither can supply a type for any argument, all_types_known is set to false and
	// the arity-only heuristic is used instead.
	std::vector<TypeSpecifierNode> arg_types;
	arg_types.reserve(arg_count);
	bool all_types_known = true;
	for (const ASTNode& arg : call_node.arguments()) {
		const CanonicalTypeId arg_type_id = inferExpressionType(arg);
		if (arg_type_id) {
			arg_types.push_back(materializeTypeSpecifier(type_context_.get(arg_type_id)));
		} else {
			auto parser_type = parser_.get_expression_type(arg);
			if (parser_type.has_value()) {
				arg_types.push_back(*parser_type);
			} else {
				all_types_known = false;
				break;
			}
		}
	}

	const FunctionDeclarationNode* best_match = nullptr;
	bool explicitly_ambiguous = false;

	if (all_types_known) {
		// Use the compiler's real overload-resolution logic (conversion ranking,
		// default arguments, cv/ref qualification, ambiguity detection).
		const OverloadResolutionResult result = resolve_overload(candidates, arg_types);
		if (result.has_match && !result.is_ambiguous) {
			best_match = &result.selected_overload->as<FunctionDeclarationNode>();
		}
		if (result.is_ambiguous) {
			explicitly_ambiguous = true;
		}
	}

	if (!best_match && !explicitly_ambiguous) {
		// Fall back to the arity-only heuristic when argument types could not
		// be inferred (e.g. dependent expressions, template operator() overloads).
		// When resolve_overload explicitly reported ambiguity the call is
		// ill-formed and must not be silently resolved by declaration order.
		const FunctionDeclarationNode* default_argument_match = nullptr;
		bool default_argument_match_ambiguous = false;
		for (const auto& candidate_node : candidates) {
			const auto& candidate = candidate_node.as<FunctionDeclarationNode>();
			if (!candidate.is_variadic() && candidate.parameter_nodes().size() == arg_count) {
				best_match = &candidate;
				break;
			}
			if (!callableOperatorAcceptsArgumentCount(candidate, arg_count)) continue;
			if (!default_argument_match) {
				default_argument_match = &candidate;
			} else {
				default_argument_match_ambiguous = true;
			}
		}
		if (!best_match) {
			best_match = default_argument_match_ambiguous ? nullptr : default_argument_match;
		}
	}

	if (!best_match) return;

	op_call_table_[&call_node] = best_match;
	stats_.op_calls_resolved++;

	FLASH_LOG_FORMAT(General, Debug,
		"SemanticAnalysis: resolved operator() for '{}' → {} params",
		callee_decl.identifier_token().value(),
		best_match->parameter_nodes().size());
}

// --- Function call argument conversion annotation ---

void SemanticAnalysis::tryAnnotateCallArgConversions(const FunctionCallNode& call_node) {
	const auto& arguments = call_node.arguments();
	const FunctionDeclarationNode* func_decl = getResolvedOpCall(&call_node);
	if (!func_decl) {
		tryResolveCallableOperator(call_node);
		func_decl = getResolvedOpCall(&call_node);
	}

	if (!func_decl) {
		const auto& decl = call_node.function_declaration();
		const std::string_view name = call_node.has_qualified_name()
			? call_node.qualified_name()
			: decl.identifier_token().value();

		// Collect all overloads from the symbol table.
		const auto overloads = symbols_.lookup_all(name);
		if (overloads.empty()) return;

		// Find the overload whose DeclarationNode address matches the one stored in the call.
		// The parser resolved the overload at parse time; we just need to recover the
		// FunctionDeclarationNode that wraps it so we can read the parameter types.
		for (const auto& overload : overloads) {
			if (!overload.is<FunctionDeclarationNode>()) continue;
			const auto& candidate = overload.as<FunctionDeclarationNode>();
			if (&candidate.decl_node() == &decl) {
				func_decl = &candidate;
				break;
			}
		}

		// If pointer match failed (e.g. indirect call or template instance), fall back to
		// picking the sole overload or the first one whose parameter count fits.
		if (!func_decl) {
			auto find_by_arg_count = [&]() -> const FunctionDeclarationNode* {
				for (const auto& overload : overloads) {
					if (!overload.is<FunctionDeclarationNode>()) continue;
					const auto& candidate = overload.as<FunctionDeclarationNode>();
					if (arguments.size() == candidate.parameter_nodes().size())
						return &candidate;
				}
				return nullptr;
			};
			if (overloads.size() == 1 && overloads[0].is<FunctionDeclarationNode>())
				func_decl = &overloads[0].as<FunctionDeclarationNode>();
			else
				func_decl = find_by_arg_count();
			if (!func_decl) return;
		}
	}

	if (func_decl->is_variadic()) return;  // can't annotate variadic calls

	const auto& param_nodes = func_decl->parameter_nodes();

	if (arguments.size() < countMinRequiredArgs(*func_decl) || arguments.size() > param_nodes.size()) return;

	for (size_t i = 0; i < arguments.size(); ++i) {
		const ASTNode& arg = arguments[i];
		if (!arg.is<ExpressionNode>()) continue;

		const ASTNode& param_node = param_nodes[i];
		if (!param_node.is<DeclarationNode>()) continue;
		const ASTNode param_type_node = param_node.as<DeclarationNode>().type_node();
		if (!param_type_node.has_value() || !param_type_node.is<TypeSpecifierNode>()) continue;

		const CanonicalTypeId param_type_id = canonicalizeType(param_type_node.as<TypeSpecifierNode>());
		// Quick exit when both types are inferable and already identical (no cast needed).
		// tryAnnotateConversion will re-infer if arg_type_id is invalid, so no information is lost.
		const CanonicalTypeId arg_type_id = inferExpressionType(arg);
		if (arg_type_id && canonical_types_match(arg_type_id, param_type_id)) continue;
		tryAnnotateConversion(arg, param_type_id);
	}
}

void SemanticAnalysis::tryAnnotateTernaryBranchConversions(const TernaryOperatorNode& ternary_node) {
	// C++20 [expr.cond]/7: if the second and third operands have different
	// arithmetic types, the usual arithmetic conversions are applied.
	const CanonicalTypeId true_type_id = inferExpressionType(ternary_node.true_expr());
	const CanonicalTypeId false_type_id = inferExpressionType(ternary_node.false_expr());
	if (!true_type_id || !false_type_id) return;
	if (canonical_types_match(true_type_id, false_type_id)) return;

	const auto& true_desc = type_context_.get(true_type_id);
	const auto& false_desc = type_context_.get(false_type_id);

	// Only handle primitive arithmetic types (not structs, pointers, etc.)
	if (true_desc.base_type == Type::Struct || false_desc.base_type == Type::Struct) return;
	if (!true_desc.pointer_levels.empty() || !false_desc.pointer_levels.empty()) return;

	Type common = get_common_type(true_desc.base_type, false_desc.base_type);
	CanonicalTypeDesc common_desc;
	common_desc.base_type = common;
	CanonicalTypeId common_type_id = type_context_.intern(common_desc);

	// Annotate each branch if it needs conversion to the common type.
	if (!canonical_types_match(true_type_id, common_type_id))
		tryAnnotateConversion(ternary_node.true_expr(), common_type_id);
	if (!canonical_types_match(false_type_id, common_type_id))
		tryAnnotateConversion(ternary_node.false_expr(), common_type_id);
}
