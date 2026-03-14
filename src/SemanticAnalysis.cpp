#include "SemanticAnalysis.h"
#include "Parser.h"
#include "CompileContext.h"
#include "CompileError.h"
#include "SymbolTable.h"
#include "AstNodeTypes.h"
#include "AstNodeTypes_Expr.h"
#include "OverloadResolution.h"
#include "Log.h"

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
	// Linear scan for now; acceptable at small type counts.
	// Phase 4 can add a hash map for O(1) lookup.
	for (size_t i = 0; i < types_.size(); ++i) {
		if (types_[i] == desc) {
			return CanonicalTypeId{static_cast<uint32_t>(i + 1)};  // 1-based
		}
	}
	types_.push_back(desc);
	return CanonicalTypeId{static_cast<uint32_t>(types_.size())};  // 1-based
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
		if (from_rank < INT_RANK && to_rank >= INT_RANK)
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
}

void SemanticAnalysis::run() {
	const auto& nodes = parser_.get_nodes();
	stats_.total_roots = nodes.size();

	FLASH_LOG(General, Debug, "SemanticAnalysis: starting pass over ", nodes.size(), " top-level nodes");

	for (auto& node : nodes) {
		normalizeTopLevelNode(node);
	}

	FLASH_LOG(General, Debug, "SemanticAnalysis: pass complete - ",
		stats_.roots_visited, " roots visited, ",
		stats_.expressions_visited, " expressions, ",
		stats_.statements_visited, " statements, ",
		stats_.canonical_types_interned, " canonical types");
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
		normalizeExpression(stmt.get_condition(), ctx);
		normalizeStatement(stmt.get_body_statement(), ctx);
	}
	else if (node.is<DoWhileStatementNode>()) {
		const auto& stmt = node.as<DoWhileStatementNode>();
		normalizeStatement(stmt.get_body_statement(), ctx);
		normalizeExpression(stmt.get_condition(), ctx);
	}
	else if (node.is<SwitchStatementNode>()) {
		const auto& stmt = node.as<SwitchStatementNode>();
		normalizeExpression(stmt.get_condition(), ctx);
		normalizeStatement(stmt.get_body(), ctx);
	}
	else if (node.is<RangedForStatementNode>()) {
		const auto& stmt = node.as<RangedForStatementNode>();
		normalizeExpression(stmt.get_range_expression(), ctx);
		normalizeStatement(stmt.get_body_statement(), ctx);
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
				if ((is_arithmetic || is_comparison) &&
					e.get_lhs().template is<ExpressionNode>() &&
					e.get_rhs().template is<ExpressionNode>()) {
					tryAnnotateBinaryOperandConversions(e);
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
				normalizeExpression(e.get_operand(), ctx);
			}
			else if constexpr (std::is_same_v<T, TernaryOperatorNode>) {
				normalizeExpression(e.condition(), ctx);
				normalizeExpression(e.true_expr(), ctx);
				normalizeExpression(e.false_expr(), ctx);
			}
			else if constexpr (std::is_same_v<T, FunctionCallNode>) {
				tryAnnotateCallArgConversions(e);
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
				for (const auto& param : e.parameters()) {
					if (param.template is<DeclarationNode>()) {
						const auto& decl = param.template as<DeclarationNode>();
						for (const auto& dim : decl.array_dimensions()) {
							normalizeExpression(dim, ctx);
						}
						if (decl.has_default_value()) {
							normalizeExpression(decl.default_value(), ctx);
						}
					}
					else {
						throw InternalError("Lambda parameter must be a DeclarationNode");
					}
				}
				normalizeStatement(e.body(), ctx);
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
					if (l.base_type == Type::Auto   || r.base_type == Type::Auto)    return {};
					if (!l.pointer_levels.empty()   || !r.pointer_levels.empty())    return {};
					const Type common = get_common_type(l.base_type, r.base_type);
					if (common == Type::Invalid) return {};
					CanonicalTypeDesc desc;
					desc.base_type = common;
					return type_context_.intern(desc);
				}
			}
			else if constexpr (std::is_same_v<T, FunctionCallNode>) {
				// Infer result type from the resolved function declaration's return type.
				const DeclarationNode& decl = e.function_declaration();
				const ASTNode ret_type_node = decl.type_node();
				if (!ret_type_node.has_value() || !ret_type_node.is<TypeSpecifierNode>()) return {};
				return canonicalizeType(ret_type_node.as<TypeSpecifierNode>());
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

	// Only handle plain primitive conversions (no pointers, no arrays, no structs, no enums, no auto)
	if (!from_desc.pointer_levels.empty() || !to_desc.pointer_levels.empty()) return false;
	if (!from_desc.array_dimensions.empty() || !to_desc.array_dimensions.empty()) return false;
	if (from_desc.base_type == Type::Struct || to_desc.base_type == Type::Struct) return false;
	if (from_desc.base_type == Type::Enum   || to_desc.base_type == Type::Enum)   return false;
	if (from_desc.base_type == Type::Invalid || to_desc.base_type == Type::Invalid) return false;
	if (from_desc.base_type == Type::Auto    || to_desc.base_type == Type::Auto)    return false;
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
	if (lhs_desc.base_type == Type::Auto    || rhs_desc.base_type == Type::Auto)    return;

	const Type common = get_common_type(lhs_desc.base_type, rhs_desc.base_type);
	if (common == Type::Invalid) return;

	// Intern the common type
	CanonicalTypeDesc common_desc;
	common_desc.base_type = common;
	const CanonicalTypeId common_type_id = type_context_.intern(common_desc);

	tryAnnotateConversion(bin_op.get_lhs(), common_type_id);
	tryAnnotateConversion(bin_op.get_rhs(), common_type_id);
}

// --- Function call argument conversion annotation ---

void SemanticAnalysis::tryAnnotateCallArgConversions(const FunctionCallNode& call_node) {
	const auto& decl = call_node.function_declaration();
	const std::string_view name = call_node.has_qualified_name()
		? call_node.qualified_name()
		: decl.identifier_token().value();

	// Look up the function in the symbol table to get parameter types
	const auto maybe_func = symbols_.lookup(name);
	if (!maybe_func.has_value() || !maybe_func->is<FunctionDeclarationNode>()) return;
	const auto& func_decl = maybe_func->as<FunctionDeclarationNode>();
	if (func_decl.is_variadic()) return;  // can't annotate variadic calls

	const auto& param_nodes = func_decl.parameter_nodes();
	const auto& arguments   = call_node.arguments();

	// Only annotate when argument count matches parameter count (no default args or packs)
	if (arguments.size() != param_nodes.size()) return;

	for (size_t i = 0; i < arguments.size(); ++i) {
		const ASTNode& arg = arguments[i];
		if (!arg.is<ExpressionNode>()) continue;

		const ASTNode& param_node = param_nodes[i];
		if (!param_node.is<DeclarationNode>()) continue;
		const ASTNode param_type_node = param_node.as<DeclarationNode>().type_node();
		if (!param_type_node.has_value() || !param_type_node.is<TypeSpecifierNode>()) continue;

		const CanonicalTypeId param_type_id = canonicalizeType(param_type_node.as<TypeSpecifierNode>());
		tryAnnotateConversion(arg, param_type_id);
	}
}
