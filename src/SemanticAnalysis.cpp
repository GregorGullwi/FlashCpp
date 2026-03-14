#include "SemanticAnalysis.h"
#include "Parser.h"
#include "CompileContext.h"
#include "SymbolTable.h"
#include "AstNodeTypes.h"
#include "AstNodeTypes_Expr.h"
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

// --- SemanticAnalysis ---

SemanticAnalysis::SemanticAnalysis(Parser& parser, CompileContext& context, SymbolTable& symbols)
	: parser_(parser), context_(context), symbols_(symbols) {
	// context_ and symbols_ are used by later phases but stored now to
	// avoid changing the constructor signature when those phases land.
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

void SemanticAnalysis::normalizeTopLevelNode(ASTNode node) {
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
	// Track return type for future return-statement conversion
	ASTNode type_node = func.decl_node().type_node();
	if (type_node.has_value() && type_node.is<TypeSpecifierNode>()) {
		ctx.current_function_return_type_id = canonicalizeType(type_node.as<TypeSpecifierNode>());
	}

	normalizeStatement(*def, ctx);
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

void SemanticAnalysis::normalizeStatement(ASTNode node, const SemanticContext& ctx) {
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
			normalizeExpression(*expr, ctx);
		}
	}
	else if (node.is<VariableDeclarationNode>()) {
		const auto& var = node.as<VariableDeclarationNode>();
		const auto& init = var.initializer();
		if (init.has_value()) {
			normalizeExpression(*init, ctx);
		}
	}
	else if (node.is<IfStatementNode>()) {
		const auto& stmt = node.as<IfStatementNode>();
		if (stmt.has_init()) {
			normalizeStatement(stmt.get_init_statement().value(), ctx);
		}
		normalizeExpression(stmt.get_condition(), ctx);
		normalizeStatement(stmt.get_then_statement(), ctx);
		if (stmt.has_else()) {
			normalizeStatement(stmt.get_else_statement().value(), ctx);
		}
	}
	else if (node.is<ForStatementNode>()) {
		const auto& stmt = node.as<ForStatementNode>();
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
	for (auto& stmt : block.get_statements()) {
		normalizeStatement(stmt, ctx);
	}
}

// --- Expression handler (Phase 1: counting only) ---

SemanticExprInfo SemanticAnalysis::normalizeExpression(ASTNode node, const SemanticContext& ctx) {
	if (!node.has_value()) return {};
	stats_.expressions_visited++;

	// Phase 1: just count. No actual normalization.
	// In later phases, this will dispatch on expression node type,
	// compute canonical types, and fill semantic slots.

	if (node.is<ExpressionNode>()) {
		// Walk into variant-based expression nodes to count children
		const auto& expr = node.as<ExpressionNode>();
		std::visit([&](const auto& e) {
			using T = std::decay_t<decltype(e)>;
			if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
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
			// Leaf nodes (IdentifierNode, NumericLiteralNode, etc.) have no children
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
