#include "SemanticAnalysis.h"
#include "Parser.h"
#include "CompileContext.h"
#include "CompileError.h"
#include "SymbolTable.h"
#include "AstNodeTypes.h"
#include "AstNodeTypes_Expr.h"
#include "CallNodeHelpers.h"
#include "OverloadResolution.h"
#include "Log.h"
#include "IrGenerator.h"

namespace {
constexpr std::string_view kTemplatePatternStructSuffix = "$pattern__";

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
	if (placeholder_type.category() == TypeCategory::Auto) {
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
	const TypeInfo* iterator_type_info = tryGetTypeInfo(iterator_type.type_index());
	if (!iterator_type_info) {
		return nullptr;
	}

	const StructTypeInfo* struct_info = iterator_type_info->getStructInfo();
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
	TypeSpecifierNode type_node(desc.type_index.withCategory(desc.category()), 0, Token{}, CVQualifier::None, ReferenceQualifier::None);
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
	} else if (desc.category() == TypeCategory::Invalid) {
		size_bits = 0;
	} else {
		size_bits = getTypeSpecSizeBits(type_node);
	}
	type_node.set_size_in_bits(size_bits);
	return type_node;
}

const FunctionDeclarationNode* getCallTargetFunctionCandidate(const ASTNode& overload) {
	if (overload.is<FunctionDeclarationNode>()) {
		return &overload.as<FunctionDeclarationNode>();
	}
	if (overload.is<TemplateFunctionDeclarationNode>()) {
		return &overload.as<TemplateFunctionDeclarationNode>().function_decl_node();
	}
	return nullptr;
}

CanonicalTypeDesc canonicalTypeDescFromStructMember(const StructMember& member, CVQualifier object_cv) {
	CanonicalTypeDesc desc;
	desc.type_index = member.type_index.withCategory(member.memberType());
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

CanonicalTypeDesc canonicalTypeDescFromStaticMember(const StructStaticMember& member) {
	CanonicalTypeDesc desc;
	desc.type_index = member.type_index.withCategory(member.memberType());
	desc.base_cv = member.cv_qualifier;
	desc.ref_qualifier = member.reference_qualifier;
	for (int i = 0; i < member.pointer_depth; ++i) {
		desc.pointer_levels.push_back(PointerLevel{});
	}
	return desc;
}

CanonicalTypeDesc canonicalTypeDescFromTemplateArgInfo(const TypeInfo::TemplateArgInfo& arg) {
	CanonicalTypeDesc desc;
	desc.type_index = arg.type_index.withCategory(arg.typeEnum());
	desc.base_cv = arg.cv_qualifier;
	desc.ref_qualifier = arg.ref_qualifier;
	for (size_t i = 0; i < arg.pointer_depth; ++i) {
		CVQualifier level_cv = i < arg.pointer_cv_qualifiers.size()
								   ? arg.pointer_cv_qualifiers[i]
								   : CVQualifier::None;
		desc.pointer_levels.push_back(PointerLevel{level_cv});
	}
	if (arg.is_array && arg.array_size.has_value()) {
		desc.array_dimensions.push_back(*arg.array_size);
	}
	return desc;
}

const TypeInfo* findStructTypeInfoByNameFragment(std::string_view struct_name) {
	if (struct_name.empty()) {
		return nullptr;
	}

	const StringHandle struct_name_handle = StringTable::getOrInternStringHandle(struct_name);
	auto exact_it = getTypesByNameMap().find(struct_name_handle);
	if (exact_it != getTypesByNameMap().end() && exact_it->second && exact_it->second->getStructInfo()) {
		return exact_it->second;
	}

	for (const auto& [handle, type_info] : getTypesByNameMap()) {
		if (!type_info || !type_info->getStructInfo()) {
			continue;
		}

		const std::string_view registered_name = handle.view();
		if (registered_name == struct_name) {
			return type_info;
		}
		if (registered_name.size() > struct_name.size() + 1) {
			const size_t prefix_end = registered_name.size() - struct_name.size();
			if (registered_name.substr(prefix_end) == struct_name &&
				(registered_name[prefix_end - 1] == ':' || registered_name[prefix_end - 1] == '<')) {
				return type_info;
			}
		}
		if (registered_name.size() > struct_name.size() &&
			registered_name[struct_name.size()] == '<' &&
			registered_name.starts_with(struct_name)) {
			return type_info;
		}
	}

	return nullptr;
}
} // namespace

// --- CanonicalTypeDesc::operator== ---

bool CanonicalTypeDesc::operator==(const CanonicalTypeDesc& other) const {
	if (type_index.category() != other.type_index.category())
		return false;
	if (type_index != other.type_index)
		return false;
	if (base_cv != other.base_cv)
		return false;
	if (ref_qualifier != other.ref_qualifier)
		return false;
	if (flags != other.flags)
		return false;
	if (pointer_levels.size() != other.pointer_levels.size())
		return false;
	for (size_t i = 0; i < pointer_levels.size(); ++i) {
		if (pointer_levels[i].cv_qualifier != other.pointer_levels[i].cv_qualifier)
			return false;
	}
	if (array_dimensions != other.array_dimensions)
		return false;
	if (function_signature.has_value() != other.function_signature.has_value())
		return false;
	if (function_signature.has_value()) {
		const auto& a = *function_signature;
		const auto& b = *other.function_signature;
		if (a.returnType() != b.returnType() ||
			a.return_type_index != b.return_type_index ||
			a.return_pointer_depth != b.return_pointer_depth ||
			a.return_reference_qualifier != b.return_reference_qualifier)
			return false;
		if (a.parameter_type_indices.size() != b.parameter_type_indices.size())
			return false;
		for (size_t i = 0; i < a.parameter_type_indices.size(); ++i) {
			if (a.parameter_type_indices[i].category() != b.parameter_type_indices[i].category() ||
				a.parameter_type_indices[i] != b.parameter_type_indices[i])
				return false;
		}
		if (a.linkage != b.linkage)
			return false;
		if (a.is_const != b.is_const)
			return false;
		if (a.is_volatile != b.is_volatile)
			return false;
		if (a.class_name != b.class_name)
			return false;
	}
	return true;
}

// --- TypeContext ---

CanonicalTypeId TypeContext::intern(const CanonicalTypeDesc& desc) {
	auto it = index_.find(desc);
	if (it != index_.end())
		return it->second;
	types_.push_back(desc);
	const CanonicalTypeId id{static_cast<uint32_t>(types_.size())}; // 1-based
	index_.emplace(desc, id);
	return id;
}

const CanonicalTypeDesc& TypeContext::get(CanonicalTypeId id) const {
	assert(id.value > 0 && id.value <= types_.size());
	return types_[id.value - 1];
}

// NOTE: determineConversionKind() was removed in Phase 11 and unified into
// buildConversionPlan() (OverloadResolution.h), which returns both the
// ConversionRank and StandardConversionKind in a single call.
// See tryAnnotateConversion() below for usage.

namespace {
struct PostParseBoundarySample {
	const char* node_kind = "";
	Token token;
};

struct PostParseBoundaryReport {
	size_t fold_expression_count = 0;
	size_t pack_expansion_count = 0;
	std::vector<PostParseBoundarySample> samples;

	bool hasViolations() const {
		return fold_expression_count != 0 || pack_expansion_count != 0;
	}

	void recordFold(const Token& token) {
		++fold_expression_count;
		recordSample("FoldExpressionNode", token);
	}

	void recordPackExpansion(const Token& token) {
		++pack_expansion_count;
		recordSample("PackExpansionExprNode", token);
	}

	const PostParseBoundarySample* firstSample(std::string_view node_kind) const {
		for (const auto& sample : samples) {
			if (sample.node_kind == node_kind) {
				return &sample;
			}
		}
		return nullptr;
	}

private:
	void recordSample(const char* node_kind, const Token& token) {
		if (samples.size() >= 8) {
			return;
		}
		samples.push_back(PostParseBoundarySample{node_kind, token});
	}
};

class PostParseBoundaryChecker {
public:
	PostParseBoundaryReport run(const std::vector<ASTNode>& roots) {
		for (const auto& root : roots) {
			visit(root);
		}
		return report_;
	}

private:
	void visit(const ASTNode& node) {
		if (!node.has_value()) {
			return;
		}

		if (node.is<ExpressionNode>()) {
			visitExpression(node.as<ExpressionNode>());
			return;
		}

		if (node.is<FunctionDeclarationNode>()) {
			const auto& func = node.as<FunctionDeclarationNode>();
			// Skip functions with deferred template bodies - their bodies intentionally
			// contain PackExpansionExprNode that will be resolved during lazy instantiation.
			// Also skip function bodies generally: template member functions like emplace<_Args...>
			// legitimately retain PackExpansionExprNode for their own variadic template parameters,
			// which are only resolved when the function is called with concrete args.
			if (func.has_template_body_position()) {
				return;
			}
			// Only check parameters, not the body (bodies of template functions may have
			// pack expansions from the function's own variadic params, which are expected).
			for (const auto& param : func.parameter_nodes()) {
				visit(param);
			}
			return;
		}

		if (node.is<ConstructorDeclarationNode>()) {
			const auto& ctor = node.as<ConstructorDeclarationNode>();
			// If this constructor has a deferred (template) body position, it is an
			// uninstantiated template constructor. Its member/base initializers intentionally
			// contain PackExpansionExprNode that will be resolved during lazy instantiation.
			// Skip visiting them here to avoid false-positive boundary violations.
			if (ctor.has_template_body_position() || ctor.struct_name().view().find(kTemplatePatternStructSuffix) != std::string_view::npos) {
				return;
			}
			for (const auto& param : ctor.parameter_nodes()) {
				visit(param);
			}
			for (const auto& mi : ctor.member_initializers()) {
				visit(mi.initializer_expr);
			}
			for (const auto& bi : ctor.base_initializers()) {
				for (const auto& arg : bi.arguments) {
					visit(arg);
				}
			}
			if (ctor.delegating_initializer().has_value()) {
				for (const auto& arg : ctor.delegating_initializer()->arguments) {
					visit(arg);
				}
			}
			if (ctor.get_definition().has_value()) {
				visit(*ctor.get_definition());
			}
			return;
		}

		if (node.is<DestructorDeclarationNode>()) {
			const auto& dtor = node.as<DestructorDeclarationNode>();
			if (dtor.get_definition().has_value()) {
				visit(*dtor.get_definition());
			}
			return;
		}

		if (node.is<StructDeclarationNode>()) {
			const auto& decl = node.as<StructDeclarationNode>();
			for (const auto& member : decl.members()) {
				visit(member.declaration);
				if (member.default_initializer.has_value()) {
					visit(*member.default_initializer);
				}
				if (member.bitfield_width_expr.has_value()) {
					visit(*member.bitfield_width_expr);
				}
			}
			for (const auto& member_func : decl.member_functions()) {
				visit(member_func.function_declaration);
			}
			for (const auto& friend_decl : decl.friend_declarations()) {
				visit(friend_decl);
			}
			for (const auto& nested_class : decl.nested_classes()) {
				visit(nested_class);
			}
			for (const auto& static_member : decl.static_members()) {
				if (static_member.initializer.has_value()) {
					visit(*static_member.initializer);
				}
			}
			return;
		}

		if (node.is<NamespaceDeclarationNode>()) {
			for (const auto& decl : node.as<NamespaceDeclarationNode>().declarations()) {
				visit(decl);
			}
			return;
		}

		if (node.is<DeclarationNode>()) {
			const auto& decl = node.as<DeclarationNode>();
			for (const auto& dim : decl.array_dimensions()) {
				visit(dim);
			}
			if (decl.has_default_value()) {
				visit(decl.default_value());
			}
			return;
		}

		if (node.is<VariableDeclarationNode>()) {
			const auto& var = node.as<VariableDeclarationNode>();
			visit(var.declaration_node());
			if (var.initializer().has_value()) {
				visit(*var.initializer());
			}
			return;
		}

		if (node.is<InitializerListNode>()) {
			for (const auto& init : node.as<InitializerListNode>().initializers()) {
				visit(init);
			}
			return;
		}

		if (node.is<BlockNode>()) {
			for (const auto& stmt : node.as<BlockNode>().get_statements()) {
				visit(stmt);
			}
			return;
		}

		if (node.is<ReturnStatementNode>()) {
			const auto& ret = node.as<ReturnStatementNode>();
			if (ret.expression().has_value()) {
				visit(*ret.expression());
			}
			return;
		}

		if (node.is<IfStatementNode>()) {
			const auto& stmt = node.as<IfStatementNode>();
			if (stmt.has_init()) {
				visit(stmt.get_init_statement().value());
			}
			visit(stmt.get_condition());
			visit(stmt.get_then_statement());
			if (stmt.has_else()) {
				visit(stmt.get_else_statement().value());
			}
			return;
		}

		if (node.is<ForStatementNode>()) {
			const auto& stmt = node.as<ForStatementNode>();
			if (stmt.has_init()) {
				visit(stmt.get_init_statement().value());
			}
			if (stmt.has_condition()) {
				visit(stmt.get_condition().value());
			}
			if (stmt.has_update()) {
				visit(stmt.get_update_expression().value());
			}
			visit(stmt.get_body_statement());
			return;
		}

		if (node.is<WhileStatementNode>()) {
			const auto& stmt = node.as<WhileStatementNode>();
			visit(stmt.get_condition());
			visit(stmt.get_body_statement());
			return;
		}

		if (node.is<DoWhileStatementNode>()) {
			const auto& stmt = node.as<DoWhileStatementNode>();
			visit(stmt.get_body_statement());
			visit(stmt.get_condition());
			return;
		}

		if (node.is<SwitchStatementNode>()) {
			const auto& stmt = node.as<SwitchStatementNode>();
			visit(stmt.get_condition());
			visit(stmt.get_body());
			return;
		}

		if (node.is<CaseLabelNode>()) {
			const auto& case_node = node.as<CaseLabelNode>();
			visit(case_node.get_case_value());
			if (case_node.has_statement()) {
				visit(*case_node.get_statement());
			}
			return;
		}

		if (node.is<DefaultLabelNode>()) {
			const auto& default_node = node.as<DefaultLabelNode>();
			if (default_node.has_statement()) {
				visit(*default_node.get_statement());
			}
			return;
		}

		if (node.is<RangedForStatementNode>()) {
			const auto& stmt = node.as<RangedForStatementNode>();
			if (stmt.has_init_statement()) {
				visit(*stmt.get_init_statement());
			}
			visit(stmt.get_range_expression());
			visit(stmt.get_body_statement());
			return;
		}

		if (node.is<TryStatementNode>()) {
			const auto& stmt = node.as<TryStatementNode>();
			visit(stmt.try_block());
			for (const auto& catch_clause : stmt.catch_clauses()) {
				visit(catch_clause);
			}
			return;
		}

		if (node.is<CatchClauseNode>()) {
			const auto& clause = node.as<CatchClauseNode>();
			if (clause.exception_declaration().has_value()) {
				visit(*clause.exception_declaration());
			}
			visit(clause.body());
			return;
		}

		if (node.is<SehTryExceptStatementNode>()) {
			const auto& stmt = node.as<SehTryExceptStatementNode>();
			visit(stmt.try_block());
			visit(stmt.except_clause());
			return;
		}

		if (node.is<SehExceptClauseNode>()) {
			const auto& clause = node.as<SehExceptClauseNode>();
			visit(clause.filter_expression());
			visit(clause.body());
			return;
		}

		if (node.is<SehFilterExpressionNode>()) {
			visit(node.as<SehFilterExpressionNode>().expression());
			return;
		}

		if (node.is<SehTryFinallyStatementNode>()) {
			const auto& stmt = node.as<SehTryFinallyStatementNode>();
			visit(stmt.try_block());
			visit(stmt.finally_clause());
			return;
		}

		if (node.is<SehFinallyClauseNode>()) {
			visit(node.as<SehFinallyClauseNode>().body());
			return;
		}

		if (node.is<ThrowStatementNode>()) {
			const auto& stmt = node.as<ThrowStatementNode>();
			if (stmt.expression().has_value()) {
				visit(*stmt.expression());
			}
			return;
		}

		if (node.is<FriendDeclarationNode>()) {
			const auto& friend_decl = node.as<FriendDeclarationNode>();
			if (friend_decl.function_declaration().has_value()) {
				visit(*friend_decl.function_declaration());
			}
			return;
		}

		// Phase 1 intentionally skips parser-owned template declarations and other
		// non-sema roots here. The guardrail is scoped to the ordinary AST surface
		// that semantic analysis is about to own.
	}

	void visitExpression(const ExpressionNode& expr) {
		std::visit([this](const auto& e) {
			using T = std::decay_t<decltype(e)>;
			if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
				visit(e.get_lhs());
				visit(e.get_rhs());
			} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
				visit(e.get_operand());
			} else if constexpr (std::is_same_v<T, TernaryOperatorNode>) {
				visit(e.condition());
				visit(e.true_expr());
				visit(e.false_expr());
			} else if constexpr (std::is_same_v<T, ConstructorCallNode>) {
				for (const auto& arg : e.arguments()) {
					visit(arg);
				}
			} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
				visit(e.object());
			} else if constexpr (std::is_same_v<T, PointerToMemberAccessNode>) {
				visit(e.object());
				visit(e.member_pointer());
			} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
				visit(e.array_expr());
				visit(e.index_expr());
			} else if constexpr (std::is_same_v<T, SizeofExprNode>) {
				if (!e.is_type()) {
					visit(e.type_or_expr());
				}
			} else if constexpr (std::is_same_v<T, AlignofExprNode>) {
				if (!e.is_type()) {
					visit(e.type_or_expr());
				}
			} else if constexpr (std::is_same_v<T, NewExpressionNode>) {
				if (e.size_expr().has_value()) {
					visit(*e.size_expr());
				}
				for (const auto& arg : e.constructor_args()) {
					visit(arg);
				}
				for (const auto& arg : e.placement_args()) {
					visit(arg);
				}
			} else if constexpr (std::is_same_v<T, DeleteExpressionNode>) {
				visit(e.expr());
			} else if constexpr (std::is_same_v<T, StaticCastNode> ||
								 std::is_same_v<T, DynamicCastNode> ||
								 std::is_same_v<T, ConstCastNode> ||
								 std::is_same_v<T, ReinterpretCastNode>) {
				visit(e.expr());
			} else if constexpr (std::is_same_v<T, TypeidNode>) {
				if (!e.is_type()) {
					visit(e.operand());
				}
			} else if constexpr (std::is_same_v<T, LambdaExpressionNode>) {
				for (const auto& capture : e.captures()) {
					if (capture.has_initializer()) {
						visit(*capture.initializer());
					}
				}
				for (const auto& param : e.parameters()) {
					visit(param);
				}
				visit(e.body());
			} else if constexpr (std::is_same_v<T, FoldExpressionNode>) {
				report_.recordFold(e.get_token());
				if (e.init_expr().has_value()) {
					visit(*e.init_expr());
				}
				if (e.pack_expr().has_value()) {
					visit(*e.pack_expr());
				}
			} else if constexpr (std::is_same_v<T, PackExpansionExprNode>) {
				report_.recordPackExpansion(e.get_token());
				visit(e.pattern());
			} else if constexpr (std::is_same_v<T, NoexceptExprNode>) {
				visit(e.expr());
			} else if constexpr (std::is_same_v<T, InitializerListConstructionNode>) {
				for (const auto& element : e.elements()) {
					visit(element);
				}
			} else if constexpr (std::is_same_v<T, ThrowExpressionNode>) {
				if (e.expression().has_value()) {
					visit(*e.expression());
				}
			} else if constexpr (std::is_same_v<T, CallExprNode>) {
				// Recurse into receiver, template arguments, and arguments
				// like the other call nodes.
				if (e.has_receiver()) {
					visit(e.receiver());
				}
				for (const auto& template_arg : e.template_arguments()) {
					visit(template_arg);
				}
				for (const auto& arg : e.arguments()) {
					visit(arg);
				}
			} else {
				// Leaf expressions intentionally do not recurse.
			}
		},
				   expr);
	}

	PostParseBoundaryReport report_;
};

void logPostParseBoundaryReport(const PostParseBoundaryReport& report) {
	if (!report.hasViolations()) {
		FLASH_LOG(General, Debug,
				  "Post-parse boundary check: sema-owned AST surface is free of forbidden fold/pack helper nodes");
		return;
	}

	// Phase 4: since hasViolations() returned true above, at least one of
	// fold or pack counts is nonzero; the old Warning-only else branches were
	// dead code and have been removed.
	const size_t total_violations = report.fold_expression_count + report.pack_expansion_count;
	const auto* first_fold_sample = report.firstSample("FoldExpressionNode");
	const auto* first_pack_sample = report.firstSample("PackExpansionExprNode");
	const bool has_fold_violation = report.fold_expression_count != 0;
	const bool has_pack_violation = report.pack_expansion_count != 0;
	if (has_fold_violation) {
		FLASH_LOG(General, Error,
				  "Post-parse boundary check: found ", report.fold_expression_count,
				  " FoldExpressionNode and ", report.pack_expansion_count,
				  " PackExpansionExprNode instances on the sema-owned AST surface; enforcing fold boundary before semantic normalization");
	} else {
		FLASH_LOG(General, Error,
				  "Post-parse boundary check: found ", report.pack_expansion_count,
				  " PackExpansionExprNode instances on the sema-owned AST surface; pack expansion is unsupported there before semantic normalization");
	}

	for (const auto& sample : report.samples) {
		FLASH_LOG(General, Error,
				  "  sample ", sample.node_kind, " at ", sample.token.line(), ":", sample.token.column());
	}

	if (report.samples.size() < total_violations) {
		FLASH_LOG(General, Error,
				  "  ... ", (total_violations - report.samples.size()),
				  " additional post-parse boundary sample(s) omitted");
	}

	if (has_fold_violation) {
		std::string message = "unexpanded FoldExpressionNode reached semantic analysis after parsing/template substitution";
		if (first_fold_sample && first_fold_sample->token.line() > 0) {
			message += " near line " + std::to_string(first_fold_sample->token.line()) +
					   ":" + std::to_string(first_fold_sample->token.column());
		}
		throw InternalError(message);
	}

	if (has_pack_violation) {
		std::string message =
			"unsupported PackExpansionExprNode reached semantic analysis; pack expansion should have been eliminated during template substitution";
		if (first_pack_sample && first_pack_sample->token.line() > 0) {
			message += " near line " + std::to_string(first_pack_sample->token.line()) +
					   ":" + std::to_string(first_pack_sample->token.column());
		}
		throw CompileError(message);
	}
}
} // namespace

// --- SemanticAnalysis ---

SemanticAnalysis::SemanticAnalysis(Parser& parser, CompileContext& context, SymbolTable& symbols)
	: parser_(parser), context_(context), symbols_(symbols) {
	(void)context_;
	(void)symbols_;

	// Pre-intern the canonical bool type so tryAnnotateContextualBool avoids
	// repeated interning on every call.
	CanonicalTypeDesc bool_desc;
	bool_desc.type_index = nativeTypeIndex(TypeCategory::Bool);
	bool_type_id_ = type_context_.intern(bool_desc);
}

void SemanticAnalysis::run() {
	parser_.clearPendingSemanticRoots();

	const auto& nodes = parser_.get_nodes();
	stats_.total_roots = nodes.size();

	FLASH_LOG(General, Debug, "SemanticAnalysis: starting pass over ", nodes.size(), " top-level nodes");
	logPostParseBoundaryReport(PostParseBoundaryChecker{}.run(nodes));

	for (auto& node : nodes) {
		normalizeTopLevelNode(node);
	}

	normalizePendingSemanticRoots();

	resolveRemainingAutoReturns();

	FLASH_LOG(General, Debug, "SemanticAnalysis: pass complete - ",
			  stats_.roots_visited, " roots visited, ",
			  stats_.expressions_visited, " expressions, ",
			  stats_.statements_visited, " statements, ",
			  stats_.canonical_types_interned, " canonical types");
}

size_t SemanticAnalysis::normalizePendingSemanticRoots() {
	size_t normalized_root_count = 0;

	while (true) {
		std::vector<ASTNode> pending_roots = parser_.takePendingSemanticRoots();
		if (pending_roots.empty()) {
			break;
		}

		stats_.total_roots += pending_roots.size();
		logPostParseBoundaryReport(PostParseBoundaryChecker{}.run(pending_roots));

		for (const ASTNode& pending_root : pending_roots) {
			if (!pending_root.has_value()) {
				continue;
			}

			normalizeTopLevelNode(pending_root);
			ASTNode mutable_root = pending_root;
			resolveRemainingAutoReturnsInNode(mutable_root);
			++normalized_root_count;
		}
	}

	return normalized_root_count;
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

void SemanticAnalysis::registerOuterTemplateBindingsInScope(const FunctionDeclarationNode& func) {
	if (!func.has_outer_template_bindings()) {
		return;
	}

	const auto& param_names = func.outer_template_param_names();
	const auto& param_args = func.outer_template_args();
	const size_t binding_count = std::min(param_names.size(), param_args.size());
	for (size_t i = 0; i < binding_count; ++i) {
		const StringHandle param_name = param_names[i];
		if (!param_name.isValid()) {
			continue;
		}

		const CanonicalTypeDesc desc = canonicalTypeDescFromTemplateArgInfo(param_args[i]);
		if (desc.category() == TypeCategory::Invalid) {
			continue;
		}

		addLocalType(param_name, type_context_.intern(desc));
	}
}

void SemanticAnalysis::registerOuterTemplateBindingsInScope(const LambdaExpressionNode& lambda) {
	if (!lambda.has_outer_template_bindings()) {
		return;
	}

	const auto& param_names = lambda.outer_template_param_names();
	const auto& param_args = lambda.outer_template_args();
	const size_t binding_count = std::min(param_names.size(), param_args.size());
	for (size_t i = 0; i < binding_count; ++i) {
		const StringHandle param_name = param_names[i];
		if (!param_name.isValid()) {
			continue;
		}

		const CanonicalTypeDesc desc = canonicalTypeDescFromTemplateArgInfo(param_args[i]);
		if (desc.category() == TypeCategory::Invalid) {
			continue;
		}

		addLocalType(param_name, type_context_.intern(desc));
	}
}

void SemanticAnalysis::registerOuterTemplateBindingsInScope(const LambdaInfo& lambda_info) {
	const size_t binding_count = std::min(
		lambda_info.outer_template_param_names.size(),
		lambda_info.outer_template_args.size());
	for (size_t i = 0; i < binding_count; ++i) {
		const StringHandle param_name = lambda_info.outer_template_param_names[i];
		if (!param_name.isValid()) {
			continue;
		}

		const CanonicalTypeDesc desc = canonicalTypeDescFromTemplateArgInfo(lambda_info.outer_template_args[i]);
		if (desc.category() == TypeCategory::Invalid) {
			continue;
		}

		addLocalType(param_name, type_context_.intern(desc));
	}
}

void SemanticAnalysis::registerOuterTemplateBindingsInScope(const StructDeclarationNode& decl) {
	if (!decl.has_outer_template_bindings()) {
		return;
	}

	const auto& param_names = decl.outer_template_param_names();
	const auto& param_args = decl.outer_template_args();
	const size_t binding_count = std::min(param_names.size(), param_args.size());
	for (size_t i = 0; i < binding_count; ++i) {
		const StringHandle param_name = param_names[i];
		if (!param_name.isValid()) {
			continue;
		}

		const CanonicalTypeDesc desc = canonicalTypeDescFromTemplateArgInfo(param_args[i]);
		if (desc.category() == TypeCategory::Invalid) {
			continue;
		}

		addLocalType(param_name, type_context_.intern(desc));
	}
}

void SemanticAnalysis::registerOuterTemplateBindingsInScope(const VariableDeclarationNode& var) {
	if (!var.has_outer_template_bindings()) {
		return;
	}

	const auto& param_names = var.outer_template_param_names();
	const auto& param_args = var.outer_template_args();
	const size_t binding_count = std::min(param_names.size(), param_args.size());
	for (size_t i = 0; i < binding_count; ++i) {
		const StringHandle param_name = param_names[i];
		if (!param_name.isValid()) {
			continue;
		}

		const CanonicalTypeDesc desc = canonicalTypeDescFromTemplateArgInfo(param_args[i]);
		if (desc.category() == TypeCategory::Invalid) {
			continue;
		}

		addLocalType(param_name, type_context_.intern(desc));
	}
}

void SemanticAnalysis::registerOuterTemplateBindingsInScope(const ConstructorDeclarationNode& ctor) {
	if (!ctor.has_outer_template_bindings()) {
		return;
	}

	const auto& param_names = ctor.outer_template_param_names();
	const auto& param_args = ctor.outer_template_args();
	const size_t binding_count = std::min(param_names.size(), param_args.size());
	for (size_t i = 0; i < binding_count; ++i) {
		const StringHandle param_name = param_names[i];
		if (!param_name.isValid()) {
			continue;
		}

		const CanonicalTypeDesc desc = canonicalTypeDescFromTemplateArgInfo(param_args[i]);
		if (desc.category() == TypeCategory::Invalid) {
			continue;
		}

		addLocalType(param_name, type_context_.intern(desc));
	}
}

void SemanticAnalysis::registerOuterTemplateBindingsInScope(const DestructorDeclarationNode& dtor) {
	if (!dtor.has_outer_template_bindings()) {
		return;
	}

	const auto& param_names = dtor.outer_template_param_names();
	const auto& param_args = dtor.outer_template_args();
	const size_t binding_count = std::min(param_names.size(), param_args.size());
	for (size_t i = 0; i < binding_count; ++i) {
		const StringHandle param_name = param_names[i];
		if (!param_name.isValid()) {
			continue;
		}

		const CanonicalTypeDesc desc = canonicalTypeDescFromTemplateArgInfo(param_args[i]);
		if (desc.category() == TypeCategory::Invalid) {
			continue;
		}

		addLocalType(param_name, type_context_.intern(desc));
	}
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
	registerOuterTemplateBindingsInScope(lambda_info);

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

	if (isPlaceholderAutoType(lambda_info.return_type_index.category())) {
		if (auto deduced_type = deducePlaceholderReturnType(lambda_info.lambda_body, lambda_info.returnType());
			deduced_type.has_value()) {
			lambda_info.return_type_index = deduced_type->type_index().withCategory(deduced_type->type());
			lambda_info.return_value_mode = ReturnValueMode::None;
			if (deduced_type->pointer_depth() > 0) {
				lambda_info.return_value_mode |= ReturnValueMode::Pointer;
			}
			if (deduced_type->is_reference() || deduced_type->is_rvalue_reference()) {
				lambda_info.return_value_mode |= ReturnValueMode::Reference;
			}
			if (deduced_type->is_function_pointer() || deduced_type->has_function_signature()) {
				lambda_info.return_value_mode |= ReturnValueMode::FunctionPointer;
			}
			int deduced_size = getTypeSpecSizeBits(*deduced_type);
			lambda_info.return_size = lambda_info.returnsReference() ? 64 : deduced_size;
		}
	}

	SemanticContext lambda_ctx;
	if (!isPlaceholderAutoType(lambda_info.return_type_index.category())) {
		TypeSpecifierNode lambda_return_type(
			lambda_info.return_type_index.withCategory(lambda_info.returnType()),
			lambda_info.return_size,
			lambda_info.lambda_token,
			CVQualifier::None,
			ReferenceQualifier::None);
		if (lambda_info.returnsReference()) {
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
	auto& mutable_stmt = const_cast<RangedForStatementNode&>(stmt);
	mutable_stmt.set_resolved_dereference_function(nullptr);
	mutable_stmt.set_resolved_member_begin_function(nullptr);
	mutable_stmt.set_resolved_member_end_function(nullptr);
	mutable_stmt.set_resolved_begin_is_const(false);
	mutable_stmt.set_resolved_adl_begin_function(nullptr);
	mutable_stmt.set_resolved_adl_end_function(nullptr);

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
		return resolveRangedForLoopDeclNode(original_var_decl, *range_type);
	}

	if (range_type->category() != TypeCategory::Struct) {
		return original_var_decl.declaration_node();
	}

	const TypeInfo* range_type_info = tryGetTypeInfo(range_type->type_index());
	const StructTypeInfo* struct_info = range_type_info ? range_type_info->getStructInfo() : nullptr;
	if (!struct_info) {
		return original_var_decl.declaration_node();
	}

	const StructMemberFunction* begin_func = struct_info->findMemberFunction("begin"sv);
	const StructMemberFunction* end_func = struct_info->findMemberFunction("end"sv);
	bool has_member_begin = begin_func && begin_func->function_decl.is<FunctionDeclarationNode>();
	bool has_member_end = end_func && end_func->function_decl.is<FunctionDeclarationNode>();
	if (!has_member_begin && !has_member_end) {
		std::vector<TypeSpecifierNode> adl_arg_types;
		TypeSpecifierNode range_arg = *range_type;
		range_arg.set_reference_qualifier(ReferenceQualifier::LValueReference);
		adl_arg_types.push_back(range_arg);
		auto adl_begin = symbols_.lookup_adl("begin", adl_arg_types);
		auto adl_end = symbols_.lookup_adl("end", adl_arg_types);
		if (!adl_begin.empty() && !adl_end.empty()) {
			auto begin_res = resolve_overload(adl_begin, adl_arg_types);
			auto end_res = resolve_overload(adl_end, adl_arg_types);
			if (begin_res.has_match && !begin_res.is_ambiguous &&
				end_res.has_match && !end_res.is_ambiguous &&
				begin_res.selected_overload->is<FunctionDeclarationNode>() &&
				end_res.selected_overload->is<FunctionDeclarationNode>()) {
				const auto& adl_begin_decl = begin_res.selected_overload->as<FunctionDeclarationNode>();
				const auto& adl_end_decl = end_res.selected_overload->as<FunctionDeclarationNode>();
				mutable_stmt.set_resolved_adl_begin_function(&adl_begin_decl);
				mutable_stmt.set_resolved_adl_end_function(&adl_end_decl);
				const TypeSpecifierNode& begin_return_type = adl_begin_decl.decl_node().type_node().as<TypeSpecifierNode>();
				const FunctionDeclarationNode* dereference_func = nullptr;
				if (begin_return_type.pointer_depth() == 0) {
					dereference_func = getRangeIteratorDereferenceFunctionForSema(begin_return_type, range_type->is_const());
				}
				mutable_stmt.set_resolved_dereference_function(dereference_func);
				return normalizeRangedForLoopDecl(original_var_decl, *range_type, begin_return_type, dereference_func);
			}
		}
		throw CompileError("range-based for loop requires type to either provide member begin()/end() methods or be used with free functions begin()/end() found via argument-dependent lookup");
	}

	if (!has_member_begin || !has_member_end) {
		throw CompileError(!has_member_begin
			? "range-based for loop requires type to have a begin() method when member range access is selected"
			: "range-based for loop requires type to have an end() method when member range access is selected");
	}

	const auto& begin_func_decl = begin_func->function_decl.as<FunctionDeclarationNode>();
	const auto& end_func_decl = end_func->function_decl.as<FunctionDeclarationNode>();
	mutable_stmt.set_resolved_member_begin_function(&begin_func_decl);
	mutable_stmt.set_resolved_member_end_function(&end_func_decl);
	mutable_stmt.set_resolved_begin_is_const(begin_func->is_const());
	const TypeSpecifierNode& begin_return_type = begin_func_decl.decl_node().type_node().as<TypeSpecifierNode>();
	const bool prefer_const_deref = range_type->is_const() || begin_func->is_const();
	const FunctionDeclarationNode* dereference_func = nullptr;
	if (begin_return_type.pointer_depth() == 0) {
		dereference_func = getRangeIteratorDereferenceFunctionForSema(begin_return_type, prefer_const_deref);
	}
	mutable_stmt.set_resolved_dereference_function(dereference_func);
	return normalizeRangedForLoopDecl(original_var_decl, *range_type, begin_return_type, dereference_func);
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
				registerOuterTemplateBindingsInScope(func);
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

std::optional<TypeSpecifierNode> SemanticAnalysis::deducePlaceholderReturnType(const ASTNode& body, TypeCategory placeholder_type) {
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

		// Function and lambda callers seed parameter and outer-template bindings
		// before auto-return deduction, so this path now relies on sema-owned
		// inference only.
		return std::nullopt;
	};

	std::function<bool(const ASTNode&)> visit_returns = [&](const ASTNode& node) -> bool {
		if (!node.has_value()) {
			return true;
		}

		if (node.is<ReturnStatementNode>()) {
			const ReturnStatementNode& ret = node.as<ReturnStatementNode>();
			TypeSpecifierNode current_type(TypeCategory::Void, TypeQualifier::None, 0, ret.return_token(), CVQualifier::None);
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
		TypeCategory::Void,
		TypeQualifier::None,
		get_type_size_bits(TypeCategory::Void),
		Token{},
		CVQualifier::None));
}

TypeSpecifierNode SemanticAnalysis::finalizePlaceholderDeduction(TypeCategory placeholder_type, const TypeSpecifierNode& deduced_type) const {
	return finalizePlaceholderTypeDeduction(placeholder_type, deduced_type);
}

// --- Top-level dispatch ---

void SemanticAnalysis::normalizeTopLevelNode(const ASTNode& node) {
	if (!node.has_value())
		return;
	stats_.roots_visited++;

	if (node.is<FunctionDeclarationNode>()) {
		normalizeFunctionDeclaration(node.as<FunctionDeclarationNode>());
	} else if (node.is<StructDeclarationNode>()) {
		normalizeStructDeclaration(node.as<StructDeclarationNode>());
	} else if (node.is<NamespaceDeclarationNode>()) {
		normalizeNamespace(node.as<NamespaceDeclarationNode>());
	} else if (node.is<ConstructorDeclarationNode>()) {
		normalizeConstructorDeclaration(node.as<ConstructorDeclarationNode>());
	} else if (node.is<DestructorDeclarationNode>()) {
		normalizeDestructorDeclaration(node.as<DestructorDeclarationNode>());
	} else if (node.is<VariableDeclarationNode>()) {
		auto& var = node.as<VariableDeclarationNode>();
		const auto& init = var.initializer();
		if (init.has_value()) {
			SemanticContext ctx;
			pushScope();
			registerOuterTemplateBindingsInScope(var);
			normalizeExpression(*init, ctx);
			popScope();
		}
	}
	// Template declarations, forward declarations, typedefs, using directives,
	// enums, concepts - no semantic normalization needed in Phase 1
}

// --- Declaration handlers ---

void SemanticAnalysis::registerParametersInScope(const std::vector<ASTNode>& parameter_nodes) {
	for (const auto& param_node : parameter_nodes) {
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
}

void SemanticAnalysis::normalizeFunctionDeclaration(const FunctionDeclarationNode& func) {
	const auto& def = func.get_definition();
	if (!def.has_value())
		return; // Forward declaration only

	// Hidden friends can also be queued as top-level declarations. Normalize each
	// concrete body at most once so the enclosing-struct walk can own it without
	// later duplicating sema work from the queued top-level copy.
	if (!normalized_bodies_.insert(static_cast<const void*>(&(*def))).second) {
		return;
	}

	SemanticContext ctx;
	// Track return type for return-statement conversion annotation
	ASTNode type_node = func.decl_node().type_node();
	if (type_node.has_value() && type_node.is<TypeSpecifierNode>()) {
		ctx.current_function_return_type_id = canonicalizeType(type_node.as<TypeSpecifierNode>());
	}

	std::optional<TypeIndex> member_context_type_index;
	if (func.is_member_function()) {
		if (const TypeInfo* type_info = findStructTypeInfoByNameFragment(func.parent_struct_name());
			type_info && type_info->getStructInfo()) {
			member_context_type_index = type_info->type_index_;
			member_context_stack_.push_back(*member_context_type_index);
		}
	}
	auto member_context_cleanup = ScopeGuard([this, &member_context_type_index]() {
		if (member_context_type_index.has_value()) {
			member_context_stack_.pop_back();
		}
	});

	// Push a scope for this function's parameters
	pushScope();
	registerOuterTemplateBindingsInScope(func);
	registerParametersInScope(func.parameter_nodes());

	normalizeStatement(*def, ctx);
	popScope();
}

void SemanticAnalysis::normalizeConstructorDeclaration(const ConstructorDeclarationNode& ctor) {
	const auto& def = ctor.get_definition();
	if (!def.has_value())
		return;
	if (ctor.struct_name().view().find(kTemplatePatternStructSuffix) != std::string_view::npos)
		return;

	if (!normalized_bodies_.insert(static_cast<const void*>(&(*def))).second) {
		return;
	}

	SemanticContext ctx;

	std::optional<TypeIndex> member_context_type_index;
	if (const TypeInfo* type_info = findStructTypeInfoByNameFragment(ctor.struct_name().view());
		type_info && type_info->getStructInfo()) {
		member_context_type_index = type_info->type_index_;
		member_context_stack_.push_back(*member_context_type_index);
	}
	auto member_context_cleanup = ScopeGuard([this, &member_context_type_index]() {
		if (member_context_type_index.has_value()) {
			member_context_stack_.pop_back();
		}
	});

	// Push a scope for this constructor's parameters.
	pushScope();
	registerOuterTemplateBindingsInScope(ctor);
	registerParametersInScope(ctor.parameter_nodes());

	// C++20 [class.base.init]: normalize member initializer expressions so
	// they receive sema annotations (e.g. integral promotions in `result(x + 1)`).
	for (const auto& mi : ctor.member_initializers()) {
		normalizeExpression(mi.initializer_expr, ctx);
	}
	// Normalize base class initializer arguments as well.
	for (const auto& bi : ctor.base_initializers()) {
		for (const auto& arg : bi.arguments) {
			normalizeExpression(arg, ctx);
		}
	}
	// Normalize delegating constructor arguments if present.
	if (ctor.delegating_initializer().has_value()) {
		for (const auto& arg : ctor.delegating_initializer()->arguments) {
			normalizeExpression(arg, ctx);
		}
	}

	normalizeStatement(*def, ctx);
	popScope();
}

void SemanticAnalysis::normalizeDestructorDeclaration(const DestructorDeclarationNode& dtor) {
	const auto& def = dtor.get_definition();
	if (!def.has_value())
		return;

	if (!normalized_bodies_.insert(static_cast<const void*>(&(*def))).second) {
		return;
	}

	SemanticContext ctx;

	std::optional<TypeIndex> member_context_type_index;
	if (const TypeInfo* type_info = findStructTypeInfoByNameFragment(dtor.struct_name().view());
		type_info && type_info->getStructInfo()) {
		member_context_type_index = type_info->type_index_;
		member_context_stack_.push_back(*member_context_type_index);
	}
	auto member_context_cleanup = ScopeGuard([this, &member_context_type_index]() {
		if (member_context_type_index.has_value()) {
			member_context_stack_.pop_back();
		}
	});

	// Destructors cannot have parameters; push/pop a scope for consistency
	// with the function normalization pattern.
	pushScope();
	registerOuterTemplateBindingsInScope(dtor);
	normalizeStatement(*def, ctx);
	popScope();
}

void SemanticAnalysis::normalizeStructDeclaration(const StructDeclarationNode& decl) {
	SemanticContext ctx;
	pushScope();
	auto cleanup = ScopeGuard([this]() { popScope(); });
	registerOuterTemplateBindingsInScope(decl);

	for (const auto& member : decl.members()) {
		if (member.declaration.is<DeclarationNode>()) {
			const auto& member_decl = member.declaration.as<DeclarationNode>();
			for (const auto& dim : member_decl.array_dimensions()) {
				normalizeExpression(dim, ctx);
			}
			if (member_decl.has_default_value()) {
				normalizeExpression(member_decl.default_value(), ctx);
			}
		}

		if (member.default_initializer.has_value()) {
			normalizeExpression(*member.default_initializer, ctx);
		}
		if (member.bitfield_width_expr.has_value()) {
			normalizeExpression(*member.bitfield_width_expr, ctx);
		}
	}

	for (const auto& static_member : decl.static_members()) {
		if (static_member.initializer.has_value()) {
			normalizeExpression(*static_member.initializer, ctx);
		}
	}

	// Walk member function bodies (includes constructors and destructors)
	for (const auto& member_func : decl.member_functions()) {
		const auto& func_node = member_func.function_declaration;
		if (!func_node.has_value())
			continue;

		if (member_func.is_constructor && func_node.is<ConstructorDeclarationNode>()) {
			normalizeConstructorDeclaration(func_node.as<ConstructorDeclarationNode>());
		} else if (member_func.is_destructor && func_node.is<DestructorDeclarationNode>()) {
			normalizeDestructorDeclaration(func_node.as<DestructorDeclarationNode>());
		} else if (func_node.is<FunctionDeclarationNode>()) {
			normalizeFunctionDeclaration(func_node.as<FunctionDeclarationNode>());
		}
	}

	for (const auto& friend_decl_node : decl.friend_declarations()) {
		if (!friend_decl_node.is<FriendDeclarationNode>()) {
			continue;
		}

		const auto& friend_decl = friend_decl_node.as<FriendDeclarationNode>();
		if (!friend_decl.function_declaration().has_value()) {
			continue;
		}

		ASTNode friend_function = *friend_decl.function_declaration();
		if (friend_function.is<FunctionDeclarationNode>()) {
			normalizeFunctionDeclaration(friend_function.as<FunctionDeclarationNode>());
		}
	}

	for (const auto& nested_class_node : decl.nested_classes()) {
		if (nested_class_node.is<StructDeclarationNode>()) {
			normalizeStructDeclaration(nested_class_node.as<StructDeclarationNode>());
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
	if (!node.has_value())
		return;
	stats_.statements_visited++;

	auto normalizeCondition = [&](const ASTNode& condition_node, bool needs_contextual_bool) {
		if (condition_node.is<VariableDeclarationNode>()) {
			normalizeStatement(condition_node, ctx);
			return;
		}
		if (needs_contextual_bool) {
			tryAnnotateContextualBool(condition_node);
		}
		normalizeExpression(condition_node, ctx);
	};

	if (node.is<BlockNode>()) {
		const auto& block = node.as<BlockNode>();
		if (block.is_synthetic_decl_list()) {
			// Comma-separated declarations ("int x = 3, y = 4;") — no new scope.
			for (const auto& stmt : block.get_statements()) {
				normalizeStatement(stmt, ctx);
			}
		} else {
			normalizeBlock(block, ctx);
		}
	} else if (node.is<ExpressionNode>()) {
		normalizeExpression(node, ctx);
	} else if (node.is<ReturnStatementNode>()) {
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
	} else if (node.is<VariableDeclarationNode>()) {
		const auto& var = node.as<VariableDeclarationNode>();
		registerOuterTemplateBindingsInScope(var);
		// Record local variable type in the current scope for expression inference
		const auto& decl = var.declaration();
		const ASTNode vtype = decl.type_node();
		CanonicalTypeId decl_type_id{}; // default: invalid (value==0)
		if (vtype.has_value() && vtype.is<TypeSpecifierNode>()) {
			decl_type_id = canonicalizeType(vtype.as<TypeSpecifierNode>());
			const StringHandle vname = decl.identifier_token().handle();
			if (vname.isValid())
				addLocalType(vname, decl_type_id);
		}
		const auto& init = var.initializer();
		if (init.has_value()) {
			// For struct types initialized via InitializerListNode (direct-init syntax
			// like `Pair p(42, 3.14)`), annotate each initializer argument with the
			// matching constructor parameter type.
			if (init->is<InitializerListNode>() && vtype.has_value() && vtype.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& ts = vtype.as<TypeSpecifierNode>();
				if (ts.category() == TypeCategory::Struct) {
					const TypeInfo* type_info = tryGetTypeInfo(ts.type_index());
					const StructTypeInfo* si = type_info ? type_info->getStructInfo() : nullptr;
					if (si && si->hasAnyConstructor()) {
						const InitializerListNode& il = init->as<InitializerListNode>();
						tryAnnotateInitListConstructorArgs(il, *si);
					}
				}
			}
			// Annotate the initializer with any needed implicit conversion to the declared type.
			if (decl_type_id) {
				if (!tryAnnotateCopyInitConvertingConstructor(*init, decl_type_id,
															  " in variable initialization")) {
					tryAnnotateConversion(*init, decl_type_id);
					diagnoseScopedEnumConversion(*init, decl_type_id, " in variable initialization");
				}
			}
			normalizeExpression(*init, ctx);
		}
	} else if (node.is<StructuredBindingNode>()) {
		const auto& binding = node.as<StructuredBindingNode>();
		normalizeExpression(binding.initializer(), ctx);
	} else if (node.is<ThrowStatementNode>()) {
		const auto& throw_stmt = node.as<ThrowStatementNode>();
		if (throw_stmt.expression().has_value()) {
			normalizeExpression(*throw_stmt.expression(), ctx);
		}
	} else if (node.is<IfStatementNode>()) {
		const auto& stmt = node.as<IfStatementNode>();
		// C++17 [stmt.if]/2: variables declared in the init-statement go out of
		// scope at the end of the if statement.  Push/pop a scope to mirror this.
		pushScope();
		if (stmt.has_init()) {
			normalizeStatement(stmt.get_init_statement().value(), ctx);
		}
		normalizeCondition(stmt.get_condition(), true);
		normalizeStatement(stmt.get_then_statement(), ctx);
		if (stmt.has_else()) {
			normalizeStatement(stmt.get_else_statement().value(), ctx);
		}
		popScope();
	} else if (node.is<ForStatementNode>()) {
		const auto& stmt = node.as<ForStatementNode>();
		// C++20 [stmt.for]/1: the init-statement's scope extends to the end of
		// the for statement.  Push/pop a scope to prevent type leakage.
		pushScope();
		if (stmt.has_init()) {
			normalizeStatement(stmt.get_init_statement().value(), ctx);
		}
		if (stmt.has_condition()) {
			// `true` means the condition is contextually converted to bool.
			normalizeCondition(stmt.get_condition().value(), true);
		}
		if (stmt.has_update()) {
			normalizeExpression(stmt.get_update_expression().value(), ctx);
		}
		normalizeStatement(stmt.get_body_statement(), ctx);
		popScope();
	} else if (node.is<WhileStatementNode>()) {
		const auto& stmt = node.as<WhileStatementNode>();
		const bool has_condition_decl = stmt.get_condition().is<VariableDeclarationNode>();
		if (has_condition_decl) {
			pushScope();
		}
		normalizeCondition(stmt.get_condition(), true);
		normalizeStatement(stmt.get_body_statement(), ctx);
		if (has_condition_decl) {
			popScope();
		}
	} else if (node.is<DoWhileStatementNode>()) {
		const auto& stmt = node.as<DoWhileStatementNode>();
		normalizeStatement(stmt.get_body_statement(), ctx);
		// C++20 [stmt.do]: the condition is contextually converted to bool.
		normalizeCondition(stmt.get_condition(), true);
	} else if (node.is<SwitchStatementNode>()) {
		const auto& stmt = node.as<SwitchStatementNode>();
		const bool has_condition_decl = stmt.get_condition().is<VariableDeclarationNode>();
		if (has_condition_decl) {
			pushScope();
		}
		normalizeCondition(stmt.get_condition(), false);
		normalizeStatement(stmt.get_body(), ctx);
		if (has_condition_decl) {
			popScope();
		}
	} else if (node.is<RangedForStatementNode>()) {
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
	} else if (node.is<TryStatementNode>()) {
		const auto& stmt = node.as<TryStatementNode>();
		normalizeStatement(stmt.try_block(), ctx);
		for (const auto& handler : stmt.catch_clauses()) {
			if (handler.is<CatchClauseNode>()) {
				normalizeStatement(handler.as<CatchClauseNode>().body(), ctx);
			}
		}
	} else if (node.is<SehTryExceptStatementNode>()) {
		const auto& stmt = node.as<SehTryExceptStatementNode>();
		normalizeStatement(stmt.try_block(), ctx);
		const auto& except_clause = stmt.except_clause();
		if (except_clause.is<SehExceptClauseNode>()) {
			const auto& clause = except_clause.as<SehExceptClauseNode>();
			// Visit filter expression: unwrap SehFilterExpressionNode and annotate inner expr
			const auto& filter_expr_node = clause.filter_expression();
			if (filter_expr_node.is<SehFilterExpressionNode>()) {
				const auto& inner_expr = filter_expr_node.as<SehFilterExpressionNode>().expression();
				if (inner_expr.is<ExpressionNode>()) {
					normalizeExpression(inner_expr, ctx);
				}
			}
			// Visit except body
			normalizeStatement(clause.body(), ctx);
		}
	} else if (node.is<SehTryFinallyStatementNode>()) {
		const auto& stmt = node.as<SehTryFinallyStatementNode>();
		normalizeStatement(stmt.try_block(), ctx);
		const auto& finally_clause = stmt.finally_clause();
		if (finally_clause.is<SehFinallyClauseNode>()) {
			normalizeStatement(finally_clause.as<SehFinallyClauseNode>().body(), ctx);
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
	if (!node.has_value())
		return {};
	stats_.expressions_visited++;

	if (node.is<InitializerListNode>()) {
		for (const auto& initializer : node.as<InitializerListNode>().initializers()) {
			normalizeExpression(initializer, ctx);
		}
		return {};
	}

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
				// C++20 [expr.bit.and], [expr.bit.or], [expr.bit.xor]:
				// usual arithmetic conversions are performed on the operands.
				const bool is_bitwise =
					op == "&" || op == "|" || op == "^";
				const bool is_comparison =
					op == "<" || op == ">" || op == "<=" || op == ">=" ||
					op == "==" || op == "!=";
				const bool is_logical = op == "&&" || op == "||";
				// C++20 [expr.shift]: shift operands undergo independent integral
				// promotions, NOT usual arithmetic conversions.
				const bool is_shift =
					op == "<<" || op == ">>" || op == "<<=" || op == ">>=";
				const bool is_compound_assign = isCompoundAssignmentOp(op);
				const bool needs_binary_type_inference =
					(is_arithmetic || is_bitwise || is_comparison || is_compound_assign || is_shift) &&
					e.get_lhs().template is<ExpressionNode>() &&
					e.get_rhs().template is<ExpressionNode>();
				CanonicalTypeId lhs_type_id{};
				CanonicalTypeId rhs_type_id{};
				if (needs_binary_type_inference) {
					lhs_type_id = inferExpressionType(e.get_lhs());
					rhs_type_id = inferExpressionType(e.get_rhs());
					// C++20: scoped enums do not participate in implicit arithmetic
					// conversions. Diagnose before annotation so the error fires
					// early with a clear message.
					diagnoseScopedEnumBinaryOperands(e, lhs_type_id, rhs_type_id);
				}
				if (is_shift && needs_binary_type_inference) {
					tryAnnotateShiftOperandPromotions(e, lhs_type_id, rhs_type_id);
					// C++20 [expr.ass]/7: shift compound assignment back-conversion
					// from promoted LHS type to original LHS type.
					if (is_compound_assign && lhs_type_id) {
						const CanonicalTypeDesc& lhs_desc = type_context_.get(lhs_type_id);
						if (!lhs_desc.pointer_levels.empty() || lhs_desc.category() == TypeCategory::Struct ||
							lhs_desc.category() == TypeCategory::Invalid || isPlaceholderAutoType(lhs_desc.category())) {
							// Skip non-primitive types
						} else {
							const TypeCategory lhs_cat = resolveEnumUnderlyingTypeCategory(lhs_desc.type_index);
							const TypeCategory promoted_cat = promote_integer_type(lhs_cat);
							if (promoted_cat != lhs_cat) {
								CanonicalTypeDesc promoted_desc;
								promoted_desc.type_index = nativeTypeIndex(promoted_cat);
								const CanonicalTypeId promoted_id = type_context_.intern(promoted_desc);
								CanonicalTypeDesc lhs_base_desc;
								lhs_base_desc.type_index = nativeTypeIndex(lhs_cat);
								const CanonicalTypeId lhs_base_id = type_context_.intern(lhs_base_desc);
								storeCompoundAssignBackConvSlot(e, promoted_id, lhs_base_id);
							}
						}
					}
				} else if ((is_arithmetic || is_bitwise || is_comparison ||
							(is_compound_assign && !is_shift)) &&
						   needs_binary_type_inference) {
					tryAnnotateBinaryOperandConversions(e, lhs_type_id, rhs_type_id);
					// C++20 [expr.ass]/7: compound assignment back-conversion
					// from common type to LHS type. Annotate so codegen can verify.
					if (is_compound_assign) {
						tryAnnotateCompoundAssignBackConversion(e, lhs_type_id, rhs_type_id);
					}
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
					const CanonicalTypeId lhs_id = inferExpressionType(e.get_lhs());
					if (lhs_id) {
						const CanonicalTypeId rhs_id = inferExpressionType(e.get_rhs());
						tryAnnotateConversion(e.get_rhs(), lhs_id, rhs_id);
						diagnoseScopedEnumConversion(e.get_rhs(), lhs_id,
													 " in assignment", rhs_id);
					}
				}
				normalizeExpression(e.get_lhs(), ctx);
				normalizeExpression(e.get_rhs(), ctx);
			} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
				// C++20 [expr.unary.op]/9: the operand of ! is contextually
				// converted to bool.
				if (e.op() == "!") {
					tryAnnotateContextualBool(e.get_operand());
				}
				// C++20 [expr.unary.op]: the operand of unary +, -, ~ undergoes
				// integral promotion (bool/char/short -> int).
				else if (e.op() == "+" || e.op() == "-" || e.op() == "~") {
					tryAnnotateUnaryOperandPromotion(e);
				}
				normalizeExpression(e.get_operand(), ctx);
			} else if constexpr (std::is_same_v<T, TernaryOperatorNode>) {
				// C++20 [expr.cond]/1: the condition is contextually converted to bool.
				tryAnnotateContextualBool(e.condition());
				// C++20 [expr.cond]/7: usual arithmetic conversions on branches.
				tryAnnotateTernaryBranchConversions(e);
				normalizeExpression(e.condition(), ctx);
				normalizeExpression(e.true_expr(), ctx);
				normalizeExpression(e.false_expr(), ctx);
			} else if constexpr (std::is_same_v<T, ConstructorCallNode>) {
				tryAnnotateConstructorCallArgConversions(e);
				for (const auto& arg : e.arguments()) {
					normalizeExpression(arg, ctx);
				}
			} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
				normalizeExpression(e.object(), ctx);
				const void* member_access_key = static_cast<const void*>(&e);
				ResolvedMemberAccessInfo member_info;
				if (tryResolveMemberAccessInfo(e, member_info)) {
					resolved_member_access_table_[member_access_key] = member_info;
				} else {
					resolved_member_access_table_.erase(member_access_key);
				}
			} else if constexpr (std::is_same_v<T, PointerToMemberAccessNode>) {
				normalizeExpression(e.object(), ctx);
				normalizeExpression(e.member_pointer(), ctx);
			} else if constexpr (std::is_same_v<T, CallExprNode>) {
				tryAnnotateCallArgConversions(e);
				tryResolveCallableOperator(e);
				if (e.has_receiver()) {
					normalizeExpression(e.receiver(), ctx);
				}
				for (const auto& template_arg : e.template_arguments()) {
					normalizeExpression(template_arg, ctx);
				}
				for (const auto& arg : e.arguments()) {
					normalizeExpression(arg, ctx);
				}
			} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
				tryResolveSubscriptOperator(e);
				// If sema resolved this subscript to operator[], annotate the index
				// argument against the operator's parameter type using the shared
				// single-argument annotation helper.
				if (const FunctionDeclarationNode* op = getResolvedOpSubscript(&e)) {
					const auto& params = op->parameter_nodes();
					if (!params.empty() && params[0].is<DeclarationNode>()) {
						const ASTNode param_type_node = params[0].as<DeclarationNode>().type_node();
						if (param_type_node.has_value() && param_type_node.is<TypeSpecifierNode>()) {
							tryAnnotateSingleArgConversion(
								e.index_expr(),
								param_type_node.as<TypeSpecifierNode>(),
								" in subscript operator argument");
						}
					}
				}
				normalizeExpression(e.array_expr(), ctx);
				normalizeExpression(e.index_expr(), ctx);
			} else if constexpr (std::is_same_v<T, SizeofExprNode>) {
				if (!e.is_type()) {
					normalizeExpression(e.type_or_expr(), ctx);
				}
			} else if constexpr (std::is_same_v<T, AlignofExprNode>) {
				if (!e.is_type()) {
					normalizeExpression(e.type_or_expr(), ctx);
				}
			} else if constexpr (std::is_same_v<T, NewExpressionNode>) {
				if (e.size_expr().has_value()) {
					normalizeExpression(*e.size_expr(), ctx);
				}
				for (const auto& arg : e.constructor_args()) {
					normalizeExpression(arg, ctx);
				}
				for (const auto& arg : e.placement_args()) {
					normalizeExpression(arg, ctx);
				}
			} else if constexpr (std::is_same_v<T, DeleteExpressionNode>) {
				normalizeExpression(e.expr(), ctx);
			} else if constexpr (std::is_same_v<T, StaticCastNode>) {
				normalizeExpression(e.expr(), ctx);
			} else if constexpr (std::is_same_v<T, DynamicCastNode>) {
				normalizeExpression(e.expr(), ctx);
			} else if constexpr (std::is_same_v<T, ConstCastNode>) {
				normalizeExpression(e.expr(), ctx);
			} else if constexpr (std::is_same_v<T, ReinterpretCastNode>) {
				normalizeExpression(e.expr(), ctx);
			} else if constexpr (std::is_same_v<T, TypeidNode>) {
				if (!e.is_type()) {
					normalizeExpression(e.operand(), ctx);
				}
			} else if constexpr (std::is_same_v<T, LambdaExpressionNode>) {
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
				registerOuterTemplateBindingsInScope(e);
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
					} else {
						throw InternalError("Lambda parameter must be a DeclarationNode");
					}
				}
				normalizeStatement(e.body(), lambda_ctx);
				popScope();
			} else if constexpr (std::is_same_v<T, FoldExpressionNode>) {
				// Phase 4: unreachable after pre-sema boundary check.
				throw InternalError(
					"FoldExpressionNode reached SemanticAnalysis::normalizeExpression after post-parse boundary enforcement");
			} else if constexpr (std::is_same_v<T, PackExpansionExprNode>) {
				// PackExpansionExprNode in a function body: this can occur legitimately
				// for template member functions (like emplace<_Args...>) whose own
				// variadic params haven't been resolved yet.  Leave the node as-is;
				// it will be properly handled when the function is instantiated with
				// concrete template arguments.
				(void)e;
			} else if constexpr (std::is_same_v<T, NoexceptExprNode>) {
				normalizeExpression(e.expr(), ctx);
			} else if constexpr (std::is_same_v<T, InitializerListConstructionNode>) {
				for (const auto& element : e.elements()) {
					normalizeExpression(element, ctx);
				}
			} else if constexpr (std::is_same_v<T, ThrowExpressionNode>) {
				if (e.expression().has_value()) {
					normalizeExpression(*e.expression(), ctx);
				}
			}
			// Leaf nodes (IdentifierNode, QualifiedIdentifierNode, StringLiteralNode,
			// NumericLiteralNode, BoolLiteralNode, SizeofPackNode, OffsetofExprNode,
			// TypeTraitExprNode, TemplateParameterReferenceNode, PseudoDestructorCallNode)
			// do not recurse into child expressions here.
		},
				   expr);

		const void* key = static_cast<const void*>(&expr);
		auto existing_slot = getSlot(key);
		if (!existing_slot.has_value() || !existing_slot->has_type()) {
			if (const CanonicalTypeId inferred_type_id = inferExpressionType(node)) {
				SemanticSlot slot = existing_slot.value_or(SemanticSlot{});
				slot.type_id = inferred_type_id;
				slot.value_category = inferExpressionValueCategory(node);
				setSlot(key, slot);
			}
		}
	}

	return {};
}

// --- Helpers ---

CanonicalTypeId SemanticAnalysis::canonicalizeType(const TypeSpecifierNode& type) {
	CanonicalTypeDesc desc;
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

	if (type.type_index().is_valid()) {
		const ResolvedAliasTypeInfo alias_info = resolveAliasTypeInfo(type.type_index());
		if (alias_info.terminal_type_info && alias_info.terminal_type_info->isTypeAlias()) {
			// Keep walking through aliases until we reach the concrete terminal type.
			desc.type_index = alias_info.terminal_type_info->type_index_;
		} else if (alias_info.terminal_type_info) {
			desc.type_index = alias_info.terminal_type_info->type_index_;
		}
		for (size_t i = 0; i < alias_info.pointer_depth; ++i) {
			desc.pointer_levels.push_back(PointerLevel{CVQualifier::None});
		}
		if (desc.ref_qualifier == ReferenceQualifier::None &&
			alias_info.reference_qualifier != ReferenceQualifier::None) {
			desc.ref_qualifier = alias_info.reference_qualifier;
		}
		if (desc.array_dimensions.empty() && !alias_info.array_dimensions.empty()) {
			desc.array_dimensions = alias_info.array_dimensions;
		}
		if (!desc.function_signature.has_value() && alias_info.function_signature.has_value()) {
			desc.function_signature = alias_info.function_signature;
			desc.flags = desc.flags | CanonicalTypeFlags::IsFunctionType;
		}
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

namespace {
const void* getExpressionKey(const ASTNode& node) {
	return static_cast<const void*>(&node.as<ExpressionNode>());
}
}

std::optional<TypeSpecifierNode> SemanticAnalysis::getExpressionType(const ASTNode& node) const {
	if (!node.is<ExpressionNode>()) {
		return std::nullopt;
	}

	const auto* key = getExpressionKey(node);
	auto slot = getSlot(key);
	if (!slot.has_value() || !slot->has_type()) {
		const ExpressionNode& expr = node.as<ExpressionNode>();
		if (std::holds_alternative<StringLiteralNode>(expr)) {
			const int char_size_bits = static_cast<int>(get_type_size_bits(TypeCategory::Char));
			TypeSpecifierNode type(TypeCategory::Char, TypeQualifier::None, char_size_bits, Token{}, CVQualifier::Const);
			type.add_pointer_level();
			type.set_reference_qualifier(ReferenceQualifier::LValueReference);
			return type;
		}
		return std::nullopt;
	}

	TypeSpecifierNode type = materializeTypeSpecifier(type_context_.get(slot->type_id));
	switch (slot->value_category) {
		case ValueCategory::LValue:
			type.set_reference_qualifier(ReferenceQualifier::LValueReference);
			break;
		case ValueCategory::XValue:
			type.set_reference_qualifier(ReferenceQualifier::RValueReference);
			break;
		case ValueCategory::PRValue:
			break;
		default:
			throw InternalError("Unexpected semantic value category");
	}
	return type;
}

std::optional<TypeSpecifierNode> SemanticAnalysis::getOverloadResolutionArgType(const ASTNode& arg) {
	if (arg.is<ExpressionNode>()) {
		const void* key = getExpressionKey(arg);
		auto it = overload_resolution_arg_types_.find(key);
		if (it != overload_resolution_arg_types_.end()) {
			return it->second;
		}

		if (auto slot_backed_type = getExpressionType(arg); slot_backed_type.has_value()) {
			overload_resolution_arg_types_.emplace(key, *slot_backed_type);
			return slot_backed_type;
		}
	}
	auto result = buildOverloadResolutionArgType(arg, nullptr);
	if (result.has_value() && arg.is<ExpressionNode>()) {
		const void* key = getExpressionKey(arg);
		overload_resolution_arg_types_.emplace(key, *result);
	}
	return result;
}

ValueCategory SemanticAnalysis::inferExpressionValueCategory(const ASTNode& node) {
	if (!node.is<ExpressionNode>()) {
		return ValueCategory::PRValue;
	}

	const ExpressionNode& expr = node.as<ExpressionNode>();
	return std::visit([this](const auto& inner) -> ValueCategory {
		using T = std::decay_t<decltype(inner)>;
		if constexpr (std::is_same_v<T, IdentifierNode>) {
			return inner.binding() != IdentifierBinding::EnumConstant ? ValueCategory::LValue : ValueCategory::PRValue;
		} else if constexpr (std::is_same_v<T, QualifiedIdentifierNode>) {
			return ValueCategory::LValue;
		} else if constexpr (std::is_same_v<T, ArraySubscriptNode> ||
							 std::is_same_v<T, PointerToMemberAccessNode> ||
							 std::is_same_v<T, StringLiteralNode>) {
			return ValueCategory::LValue;
		} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
			// C++20 [expr.ref]/4: if the member is a reference, the result is
			// always an lvalue regardless of the object's value category.
			const CanonicalTypeId obj_type_id = inferExpressionType(inner.object());
			if (obj_type_id) {
				const CanonicalTypeDesc& obj_desc = type_context_.get(obj_type_id);
				if (obj_desc.category() == TypeCategory::Struct || obj_desc.category() == TypeCategory::UserDefined) {
					const TypeInfo* ti = tryGetTypeInfo(obj_desc.type_index);
					const StructTypeInfo* si = ti ? ti->getStructInfo() : nullptr;
					if (si) {
						const StringHandle member_handle = StringTable::getOrInternStringHandle(inner.member_name());
						for (const auto& m : si->members) {
							if (m.getName() == member_handle) {
								if (m.is_reference()) {
									return ValueCategory::LValue;
								}
								break;
							}
						}
					}
				}
			}
			const ValueCategory object_category = inferExpressionValueCategory(inner.object());
			switch (object_category) {
				case ValueCategory::LValue:
					return ValueCategory::LValue;
				case ValueCategory::XValue:
				case ValueCategory::PRValue:
					return ValueCategory::XValue;
				default:
					throw InternalError("Unexpected member-access object value category");
			}
		} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
			const std::string_view op = inner.op();
			if (op == "*" || op == "++" || op == "--") {
				return ValueCategory::LValue;
			}
			return ValueCategory::PRValue;
		} else if constexpr (std::is_same_v<T, CallExprNode>) {
			if (const FunctionDeclarationNode* func_decl = inner.callee().function_declaration_or_null()) {
				const ASTNode& type_node = func_decl->decl_node().type_node();
				if (type_node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& return_type = type_node.as<TypeSpecifierNode>();
					if (return_type.is_rvalue_reference()) {
						return ValueCategory::XValue;
					}
					if (return_type.is_reference()) {
						return ValueCategory::LValue;
					}
				}
			}
			return ValueCategory::PRValue;
		} else if constexpr (std::is_same_v<T, StaticCastNode> ||
							 std::is_same_v<T, ConstCastNode> ||
							 std::is_same_v<T, ReinterpretCastNode> ||
							 std::is_same_v<T, DynamicCastNode>) {
			const ASTNode& target_type_node = inner.target_type();
			if (target_type_node.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& target_type = target_type_node.as<TypeSpecifierNode>();
				if (target_type.is_rvalue_reference()) {
					return ValueCategory::XValue;
				}
				if (target_type.is_reference()) {
					return ValueCategory::LValue;
				}
			}
			return ValueCategory::PRValue;
		} else {
			return ValueCategory::PRValue;
		}
	},
					  expr);
}

const FunctionDeclarationNode* SemanticAnalysis::getResolvedOpCall(const void* key) const {
	auto it = op_call_table_.find(key);
	return it != op_call_table_.end() ? it->second : nullptr;
}

const FunctionDeclarationNode* SemanticAnalysis::getResolvedOpCall(const CallExprNode* key) const {
	return getResolvedOpCall(static_cast<const void*>(key));
}

const FunctionDeclarationNode* SemanticAnalysis::getResolvedDirectCall(const void* key) const {
	auto it = resolved_direct_call_table_.find(key);
	return it != resolved_direct_call_table_.end() ? it->second : nullptr;
}

const FunctionDeclarationNode* SemanticAnalysis::getResolvedDirectCall(const CallExprNode* key) const {
	return getResolvedDirectCall(static_cast<const void*>(key));
}

bool SemanticAnalysis::resolveOrGetMemberAccess(const MemberAccessNode& key,
												const StructTypeInfo*& out_struct_info,
												const StructMember*& out_member) {
	const void* cache_key = static_cast<const void*>(&key);
	auto it = resolved_member_access_table_.find(cache_key);
	if (it == resolved_member_access_table_.end()) {
		ResolvedMemberAccessInfo member_info;
		if (!tryResolveMemberAccessInfo(key, member_info)) {
			return false;
		}
		it = resolved_member_access_table_.emplace(cache_key, member_info).first;
	}

	const TypeInfo* owner_type_info = tryGetTypeInfo(it->second.owner_type_index);
	const StructTypeInfo* owner_struct_info = owner_type_info ? owner_type_info->getStructInfo() : nullptr;
	if (!owner_struct_info) {
		throw InternalError("Resolved member access owner type no longer names a struct");
	}
	if (it->second.member_index >= owner_struct_info->members.size()) {
		throw InternalError("Resolved member access index out of bounds");
	}

	out_struct_info = owner_struct_info;
	out_member = &owner_struct_info->members[it->second.member_index];
	return true;
}

const FunctionDeclarationNode* SemanticAnalysis::getResolvedOpSubscript(const ArraySubscriptNode* key) const {
	auto it = op_subscript_table_.find(key);
	return it != op_subscript_table_.end() ? it->second : nullptr;
}

const CallArgReferenceBindingInfo* SemanticAnalysis::getCallRefBinding(const void* key, size_t arg_index) const {
	auto it = call_ref_bindings_.find(key);
	if (it == call_ref_bindings_.end() || arg_index >= it->second.size()) {
		return nullptr;
	}
	return &it->second[arg_index];
}


const CallArgReferenceBindingInfo* SemanticAnalysis::getCallExprRefBinding(const CallExprNode* key, size_t arg_index) const {
	return getCallRefBinding(static_cast<const void*>(key), arg_index);
}

bool SemanticAnalysis::tryResolveMemberAccessInfo(const MemberAccessNode& member_access,
												  ResolvedMemberAccessInfo& out_info) {
	const ASTNode& object_node = member_access.object();
	auto tryResolveObjectTypeInfo = [&]() -> const TypeInfo* {
		const CanonicalTypeId object_type_id = inferExpressionType(object_node);
		if (object_type_id) {
			const CanonicalTypeDesc& object_desc = type_context_.get(object_type_id);
			if (object_desc.category() != TypeCategory::Struct &&
				object_desc.category() != TypeCategory::UserDefined) {
				return nullptr;
			}

			if (object_desc.category() == TypeCategory::UserDefined) {
				const ResolvedAliasTypeInfo alias_info = resolveAliasTypeInfo(object_desc.type_index);
				if (alias_info.terminal_type_info) {
					return alias_info.terminal_type_info;
				}
			}
			return tryGetTypeInfo(object_desc.type_index);
		}

		if (object_node.is<ExpressionNode>()) {
			const ExpressionNode& object_expr = object_node.as<ExpressionNode>();
			if (const auto* ident = std::get_if<IdentifierNode>(&object_expr);
				ident && ident->name() == "this"sv && !member_context_stack_.empty()) {
				return tryGetTypeInfo(member_context_stack_.back());
			}
		}

		return nullptr;
	};

	const TypeInfo* object_type_info = tryResolveObjectTypeInfo();
	if (!object_type_info) {
		return false;
	}

	const StructTypeInfo* struct_info = object_type_info->getStructInfo();
	if (!struct_info) {
		return false;
	}

	const StringHandle member_name = member_access.member_token().handle();
	if (!member_name.isValid()) {
		throw InternalError("Member token handle was not interned for '" + std::string(member_access.member_name()) + "'");
	}

	auto tryFillResolvedInfo = [&](const StructTypeInfo* owner_struct, const StructMember* member) -> bool {
		if (!owner_struct || !member) {
			return false;
		}

		const auto& members = owner_struct->members;
		const StructMember* members_begin = members.data();
		const StructMember* members_end = members_begin + members.size();
		// The lazy resolver returns raw StructMember pointers, so validate that the
		// pointer still belongs to the owning member array before computing the index.
		if (member < members_begin || member >= members_end) {
			throw InternalError("Resolved member access did not point at an owner member");
		}
		if (!owner_struct->own_type_index_.has_value()) {
			throw InternalError("Resolved member access owner struct is missing own_type_index_");
		}
		out_info.owner_type_index = *owner_struct->own_type_index_;
		out_info.member_index = static_cast<size_t>(member - members_begin);
		return true;
	};

	if (auto member_result = FlashCpp::gLazyMemberResolver.resolve(object_type_info->type_index_, member_name)) {
		return tryFillResolvedInfo(member_result.owner_struct, member_result.member);
	}

	for (size_t member_index = 0; member_index < struct_info->members.size(); ++member_index) {
		if (struct_info->members[member_index].name != member_name) {
			continue;
		}
		return tryFillResolvedInfo(struct_info, &struct_info->members[member_index]);
	}

	return false;
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

CastInfoIndex SemanticAnalysis::allocateNonUserDefinedCastInfo(CanonicalTypeId source_type_id,
															   CanonicalTypeId target_type_id,
															   StandardConversionKind cast_kind) {
	ImplicitCastInfo cast_info;
	cast_info.source_type_id = source_type_id;
	cast_info.target_type_id = target_type_id;
	cast_info.cast_kind = cast_kind;
	cast_info.value_category_after = ValueCategory::PRValue;
	return allocateCastInfo(cast_info);
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
	if (!node.has_value())
		return {};

	if (node.is<ExpressionNode>()) {
		if (auto slot = getSlot(getExpressionKey(node)); slot.has_value() && slot->has_type()) {
			return slot->type_id;
		}

		const auto& expr = node.as<ExpressionNode>();
		return std::visit([this, &node](const auto& e) -> CanonicalTypeId {
			using T = std::decay_t<decltype(e)>;
			if constexpr (std::is_same_v<T, NumericLiteralNode>) {
				CanonicalTypeDesc desc;
				desc.type_index = nativeTypeIndex(e.type());
				return type_context_.intern(desc);
			} else if constexpr (std::is_same_v<T, BoolLiteralNode>) {
				CanonicalTypeDesc desc;
				desc.type_index = nativeTypeIndex(TypeCategory::Bool);
				return type_context_.intern(desc);
			} else if constexpr (std::is_same_v<T, IdentifierNode>) {
				const CanonicalTypeId local_id = lookupLocalType(e.nameHandle());
				if (local_id)
					return local_id;

				auto infer_symbol_type = [&](const ASTNode& symbol) -> CanonicalTypeId {
					const DeclarationNode* decl = nullptr;
					if (symbol.is<DeclarationNode>()) {
						decl = &symbol.as<DeclarationNode>();
					} else if (symbol.is<VariableDeclarationNode>()) {
						decl = &symbol.as<VariableDeclarationNode>().declaration();
					}

					if (decl) {
						const ASTNode type_node = decl->type_node();
						if (!type_node.has_value() || !type_node.is<TypeSpecifierNode>()) {
							return {};
						}

						TypeSpecifierNode type = type_node.as<TypeSpecifierNode>();
						if (decl->is_array()) {
							type.add_pointer_level();
						}
						return canonicalizeType(type);
					}

					if (symbol.is<FunctionDeclarationNode>()) {
						const auto& func = symbol.as<FunctionDeclarationNode>();
						const ASTNode ret_type_node = func.decl_node().type_node();
						if (ret_type_node.has_value() && ret_type_node.is<TypeSpecifierNode>()) {
							return canonicalizeType(ret_type_node.as<TypeSpecifierNode>());
						}
					}

					return {};
				};

				auto lookup_bound_symbol = [&]() -> std::optional<ASTNode> {
					if (e.resolved_name().isValid()) {
						auto resolved_symbol = symbols_.lookup(e.resolved_name());
						if (resolved_symbol.has_value()) {
							return resolved_symbol;
						}
					}
					return symbols_.lookup(e.nameHandle());
				};

				if (auto symbol = lookup_bound_symbol(); symbol.has_value()) {
					if (const CanonicalTypeId symbol_type_id = infer_symbol_type(*symbol)) {
						return symbol_type_id;
					}
				}

				if (auto parser_type = parser_.get_expression_type(node); parser_type.has_value()) {
					return canonicalizeType(*parser_type);
				}

				if (e.binding() == IdentifierBinding::NonStaticMember &&
					!member_context_stack_.empty()) {
					const TypeIndex current_struct_type = member_context_stack_.back();
					if (const TypeInfo* type_info = tryGetTypeInfo(current_struct_type)) {
						const StructTypeInfo* struct_info = type_info->getStructInfo();
						if (struct_info) {
							if (auto member_result = FlashCpp::gLazyMemberResolver.resolve(current_struct_type, e.nameHandle())) {
								return type_context_.intern(canonicalTypeDescFromStructMember(
									*member_result.member, CVQualifier::None));
							}
							for (const auto& member : struct_info->members) {
								if (member.getName() == e.nameHandle()) {
									return type_context_.intern(canonicalTypeDescFromStructMember(
										member, CVQualifier::None));
								}
							}
						}
					}
				}

				return {};
			} else if constexpr (std::is_same_v<T, TemplateParameterReferenceNode>) {
				const CanonicalTypeId param_id = lookupLocalType(e.param_name());
				if (param_id)
					return param_id;
				// Phase 5 (boundary plan Workstream 2): after Phase 3, outer-template bindings
				// are seeded for all instantiated function/lambda/struct/variable bodies, so
				// this path indicates either a not-yet-migrated context or a valid unsupported
				// template form. Log at Debug level to make unresolved cases observable.
				FLASH_LOG(Templates, Debug,
						  "SemanticAnalysis: TemplateParameterReferenceNode '",
						  StringTable::getStringView(e.param_name()),
						  "' not resolved via sema-owned scope — returning empty type");
				return {};
			} else if constexpr (std::is_same_v<T, FoldExpressionNode>) {
				// Phase 4: unreachable after pre-sema boundary check.
				throw InternalError(
					"FoldExpressionNode reached SemanticAnalysis::inferExpressionType after post-parse boundary enforcement");
			} else if constexpr (std::is_same_v<T, PackExpansionExprNode>) {
				// PackExpansionExprNode in a function body: occurs legitimately
				// for template member functions with unresolved variadic params.
				// Return empty type; will be resolved when function is instantiated.
				(void)e;
			} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
				auto try_parser_member_type = [&]() -> CanonicalTypeId {
					if (auto parser_type = parser_.get_expression_type(node); parser_type.has_value()) {
						return canonicalizeType(*parser_type);
					}
					return {};
				};
				const CanonicalTypeId object_type_id = inferExpressionType(e.object());
				if (!object_type_id) {
					return try_parser_member_type();
				}
				const CanonicalTypeDesc& object_desc = type_context_.get(object_type_id);
				if (object_desc.category() != TypeCategory::Struct &&
					object_desc.category() != TypeCategory::UserDefined) {
					return try_parser_member_type();
				}
				const TypeInfo* object_type_info = nullptr;
				if (object_desc.category() == TypeCategory::UserDefined) {
					const ResolvedAliasTypeInfo alias_info = resolveAliasTypeInfo(object_desc.type_index);
					object_type_info = alias_info.terminal_type_info;
				}
				if (!object_type_info) {
					object_type_info = tryGetTypeInfo(object_desc.type_index);
				}
				ResolvedMemberAccessInfo member_info;
				if (!object_type_info || !tryResolveMemberAccessInfo(e, member_info)) {
					return try_parser_member_type();
				}

				const TypeInfo* owner_type_info = tryGetTypeInfo(member_info.owner_type_index);
				const StructTypeInfo* owner_struct_info = owner_type_info ? owner_type_info->getStructInfo() : nullptr;
				if (!owner_struct_info || member_info.member_index >= owner_struct_info->members.size()) {
					return try_parser_member_type();
				}
				return type_context_.intern(canonicalTypeDescFromStructMember(
					owner_struct_info->members[member_info.member_index], object_desc.base_cv));
			} else if constexpr (std::is_same_v<T, PointerToMemberAccessNode>) {
				CanonicalTypeId member_pointer_type_id = inferExpressionType(e.member_pointer());
				if (!member_pointer_type_id) {
					return {};
				}

				CanonicalTypeDesc result_desc = type_context_.get(member_pointer_type_id);
				if (!result_desc.pointer_levels.empty()) {
					result_desc.pointer_levels.pop_back();
				}
				return type_context_.intern(result_desc);
			} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
				// If sema resolved this subscript to operator[], return the operator[]'s return type.
				if (const FunctionDeclarationNode* op_subscript = getResolvedOpSubscript(&e)) {
					const ASTNode& ret_type_node = op_subscript->decl_node().type_node();
					if (ret_type_node.is<TypeSpecifierNode>())
						return canonicalizeType(ret_type_node.as<TypeSpecifierNode>());
					return {};
				}
				// Array subscript: the result type is the element type of the array.
				// Infer the array expression type and strip one array dimension.
				const CanonicalTypeId array_type_id = inferExpressionType(e.array_expr());
				if (!array_type_id)
					return {};
				const CanonicalTypeDesc& array_desc = type_context_.get(array_type_id);
				// If it has array dimensions, strip one to get element type.
				if (!array_desc.array_dimensions.empty()) {
					CanonicalTypeDesc elem_desc = array_desc;
					elem_desc.array_dimensions.clear();
					for (size_t i = 1; i < array_desc.array_dimensions.size(); ++i)
						elem_desc.array_dimensions.push_back(array_desc.array_dimensions[i]);
					return type_context_.intern(elem_desc);
				}
				// Pointer subscript: dereference removes one pointer level.
				if (!array_desc.pointer_levels.empty()) {
					CanonicalTypeDesc elem_desc = array_desc;
					elem_desc.pointer_levels.pop_back();
					return type_context_.intern(elem_desc);
				}
				// Plain type subscript — return base type.
				return array_type_id;
			} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
				const std::string_view op = e.op();
				// Unary +, - and ~ apply integral promotion: types with rank < int become int.
				if (op == "+" || op == "-" || op == "~") {
					const CanonicalTypeId operand_id = inferExpressionType(e.get_operand());
					if (!operand_id)
						return {};
					const CanonicalTypeDesc& operand_desc = type_context_.get(operand_id);
					// Resolve enum to underlying type
					const TypeCategory operand_cat = resolveEnumUnderlyingTypeCategory(operand_desc.type_index);
					const bool is_small_int =
						(isIntegralType(operand_cat) || operand_cat == TypeCategory::Bool) && get_integer_rank(operand_cat) < 3; // rank of int
					if (is_small_int) {
						CanonicalTypeDesc promoted;
						promoted.type_index = nativeTypeIndex(TypeCategory::Int);
						return type_context_.intern(promoted);
					}
					CanonicalTypeDesc result_desc;
					result_desc.type_index = nativeTypeIndex(operand_cat);
					return type_context_.intern(result_desc);
				}
				// Logical NOT always returns bool
				if (op == "!") {
					CanonicalTypeDesc desc;
					desc.type_index = nativeTypeIndex(TypeCategory::Bool);
					return type_context_.intern(desc);
				}
				// Prefix/postfix ++ and -- return the operand type
				if (op == "++" || op == "--") {
					return inferExpressionType(e.get_operand());
				}
				return {};
			} else if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
				const std::string_view op = e.op();
				// Three-way comparison (<=>) returns a comparison category type
				if (op == "<=>") {
					const CanonicalTypeId lhs_id = inferExpressionType(e.get_lhs());
					const CanonicalTypeId rhs_id = inferExpressionType(e.get_rhs());
					if (lhs_id && rhs_id) {
						const CanonicalTypeDesc& l = type_context_.get(lhs_id);
						const CanonicalTypeDesc& r = type_context_.get(rhs_id);
						// For struct types, the result type depends on the user-defined operator<=>.
						// For built-in types, C++20 [expr.spaceship]:
						//   - integral types → std::strong_ordering
						//   - floating-point types → std::partial_ordering
						//   - bool → std::strong_ordering
						if (l.category() != TypeCategory::Struct && r.category() != TypeCategory::Struct) {
							const bool is_float_cmp = is_floating_point_type(l.category()) || is_floating_point_type(r.category());
							const char* qualified_name = is_float_cmp ? "std::partial_ordering" : "std::strong_ordering";
							const char* unqualified_name = is_float_cmp ? "partial_ordering" : "strong_ordering";
							const StringHandle q_handle = StringTable::getOrInternStringHandle(qualified_name);
							const TypeInfo* ordering_type = findTypeByName(q_handle);
							if (!ordering_type) {
								const StringHandle u_handle = StringTable::getOrInternStringHandle(unqualified_name);
								ordering_type = findTypeByName(u_handle);
							}
							if (ordering_type) {
								CanonicalTypeDesc desc;
								desc.type_index = ordering_type->type_index_;
								return type_context_.intern(desc);
							}
							FLASH_LOG(General, Debug, "inferExpressionType: <=> on primitive types but ordering type not found: ",
									  qualified_name);
						}
					}
					// Fallback: if ordering types not found, return unknown
					return {};
				}
				// Comparison and logical operators always produce bool
				if (op == "==" || op == "!=" || op == "<" || op == ">" ||
					op == "<=" || op == ">=" || op == "&&" || op == "||") {
					CanonicalTypeDesc desc;
					desc.type_index = nativeTypeIndex(TypeCategory::Bool);
					return type_context_.intern(desc);
				}
				// Comma: result is the RHS type
				if (op == ",")
					return inferExpressionType(e.get_rhs());
				// Assignment: result is LHS type (lvalue of LHS)
				if (op == "=" || op == "+=" || op == "-=" || op == "*=" ||
					op == "/=" || op == "%=" || op == "&=" || op == "|=" ||
					op == "^=" || op == "<<=" || op == ">>=")
					return inferExpressionType(e.get_lhs());
				// Arithmetic/bitwise: usual arithmetic conversions
				{
					const CanonicalTypeId lhs_id = inferExpressionType(e.get_lhs());
					const CanonicalTypeId rhs_id = inferExpressionType(e.get_rhs());
					if (!lhs_id || !rhs_id)
						return {};
					const CanonicalTypeDesc& l = type_context_.get(lhs_id);
					const CanonicalTypeDesc& r = type_context_.get(rhs_id);
					if (l.category() == TypeCategory::Invalid || r.category() == TypeCategory::Invalid)
						return {};
					if (isPlaceholderAutoType(l.category()) || isPlaceholderAutoType(r.category()))
						return {};
					if (!l.pointer_levels.empty() || !r.pointer_levels.empty())
						return {};
					const TypeCategory common_cat = get_common_type(l.category(), r.category());
					if (common_cat == TypeCategory::Invalid)
						return {};
					CanonicalTypeDesc desc;
					desc.type_index = nativeTypeIndex(common_cat);
					return type_context_.intern(desc);
				}
			} else if constexpr (std::is_same_v<T, TernaryOperatorNode>) {
				// C++20 [expr.cond]/7: result type is the common type of the two branches.
				const CanonicalTypeId t_id = inferExpressionType(e.true_expr());
				const CanonicalTypeId f_id = inferExpressionType(e.false_expr());
				if (!t_id || !f_id)
					return {};
				if (canonical_types_match(t_id, f_id))
					return t_id;
				const CanonicalTypeDesc& t_desc = type_context_.get(t_id);
				const CanonicalTypeDesc& f_desc = type_context_.get(f_id);
				if (t_desc.category() == TypeCategory::Struct || f_desc.category() == TypeCategory::Struct)
					return {};
				if (!t_desc.pointer_levels.empty() || !f_desc.pointer_levels.empty())
					return {};
				const TypeCategory common_cat = get_common_type(t_desc.category(), f_desc.category());
				if (common_cat == TypeCategory::Invalid)
					return {};
				CanonicalTypeDesc ternary_desc;
				ternary_desc.type_index = nativeTypeIndex(common_cat);
				return type_context_.intern(ternary_desc);
			} else if constexpr (std::is_same_v<T, StaticCastNode> ||
								 std::is_same_v<T, ConstCastNode> ||
								 std::is_same_v<T, ReinterpretCastNode>) {
				// Explicit casts: the result type is the declared target type.
				const ASTNode& tt = e.target_type();
				if (tt.has_value() && tt.template is<TypeSpecifierNode>())
					return canonicalizeType(tt.template as<TypeSpecifierNode>());
				return {};
			} else if constexpr (std::is_same_v<T, SizeofExprNode> ||
								 std::is_same_v<T, SizeofPackNode> ||
								 std::is_same_v<T, AlignofExprNode>) {
				// sizeof, sizeof... and alignof always return size_t (UnsignedLongLong on 64-bit).
				CanonicalTypeDesc desc;
				desc.type_index = nativeTypeIndex(TypeCategory::UnsignedLongLong);
				return type_context_.intern(desc);
			} else if constexpr (std::is_same_v<T, TypeidNode>) {
				// The current backend models typeid as producing a const void* handle.
				CanonicalTypeDesc desc;
				desc.type_index = nativeTypeIndex(TypeCategory::Void);
				desc.base_cv = CVQualifier::Const;
				desc.pointer_levels.push_back(PointerLevel{CVQualifier::None});
				return type_context_.intern(desc);
			} else if constexpr (std::is_same_v<T, ConstructorCallNode>) {
				// Constructor call returns the type being constructed.
				const ASTNode& type_node = e.type_node();
				if (type_node.has_value() && type_node.template is<TypeSpecifierNode>())
					return canonicalizeType(type_node.template as<TypeSpecifierNode>());
				return {};
			} else if constexpr (std::is_same_v<T, InitializerListConstructionNode>) {
				const ASTNode& target_type_node = e.target_type();
				if (target_type_node.has_value() && target_type_node.template is<TypeSpecifierNode>())
					return canonicalizeType(target_type_node.template as<TypeSpecifierNode>());
				return {};
			} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
				// String literal has type "const char*" (array of const char).
				CanonicalTypeDesc desc;
				desc.type_index = nativeTypeIndex(TypeCategory::Char);
				desc.base_cv = CVQualifier::Const;
				desc.pointer_levels.push_back(PointerLevel{CVQualifier::None});
				return type_context_.intern(desc);
			} else if constexpr (std::is_same_v<T, QualifiedIdentifierNode>) {
				NamespaceHandle ns_handle = e.namespace_handle();
				if (!ns_handle.isGlobal()) {
					std::string_view owner_name = gNamespaceRegistry.getName(ns_handle);
					auto owner_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(owner_name));
					if (owner_it == getTypesByNameMap().end() && gNamespaceRegistry.getDepth(ns_handle) > 1) {
						std::string_view full_qualified_name = gNamespaceRegistry.getQualifiedName(ns_handle);
						owner_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(full_qualified_name));
					}

					if (owner_it != getTypesByNameMap().end()) {
						if (owner_it->second->isStruct()) {
							const StructTypeInfo* struct_info = owner_it->second->getStructInfo();
							if (struct_info) {
								const StringHandle member_name_handle = e.nameHandle();
								parser_.instantiateLazyStaticMember(struct_info->name, member_name_handle);
								const auto static_member_result = struct_info->findStaticMemberRecursive(member_name_handle);
								const StructStaticMember* static_member = static_member_result.first;
								if (static_member) {
									return type_context_.intern(canonicalTypeDescFromStaticMember(*static_member));
								}
							}
						} else if (owner_it->second->isEnum()) {
							CanonicalTypeDesc desc;
							desc.type_index = nativeTypeIndex(TypeCategory::Enum);
							desc.type_index = owner_it->second->type_index_;
							return type_context_.intern(desc);
						}
					}
				}
				return {};
			} else if constexpr (std::is_same_v<T, DynamicCastNode>) {
				const ASTNode& tt = e.target_type();
				if (tt.has_value() && tt.template is<TypeSpecifierNode>())
					return canonicalizeType(tt.template as<TypeSpecifierNode>());
				return {};
			} else if constexpr (std::is_same_v<T, OffsetofExprNode>) {
				// offsetof returns size_t (UnsignedLongLong on 64-bit).
				CanonicalTypeDesc desc;
				desc.type_index = nativeTypeIndex(TypeCategory::UnsignedLongLong);
				return type_context_.intern(desc);
			} else if constexpr (std::is_same_v<T, NoexceptExprNode>) {
				// noexcept(expr) returns bool.
				CanonicalTypeDesc desc;
				desc.type_index = nativeTypeIndex(TypeCategory::Bool);
				return type_context_.intern(desc);
			} else if constexpr (std::is_same_v<T, TypeTraitExprNode>) {
				// Type trait intrinsics return bool (e.g., __is_integral(T)).
				CanonicalTypeDesc desc;
				desc.type_index = nativeTypeIndex(TypeCategory::Bool);
				return type_context_.intern(desc);
			} else if constexpr (std::is_same_v<T, NewExpressionNode>) {
				// C++20 [expr.new]: new T returns T*; new T[n] returns T*.
				const ASTNode& type_node = e.type_node();
				if (type_node.has_value() && type_node.template is<TypeSpecifierNode>()) {
					CanonicalTypeId base_id = canonicalizeType(type_node.template as<TypeSpecifierNode>());
					if (base_id) {
						CanonicalTypeDesc ptr_desc = type_context_.get(base_id);
						ptr_desc.pointer_levels.push_back(PointerLevel{CVQualifier::None});
						return type_context_.intern(ptr_desc);
					}
				}
				return {};
			} else if constexpr (std::is_same_v<T, DeleteExpressionNode>) {
				// C++20 [expr.delete]: delete-expression is a void expression.
				CanonicalTypeDesc desc;
				desc.type_index = nativeTypeIndex(TypeCategory::Void);
				return type_context_.intern(desc);
			} else if constexpr (std::is_same_v<T, PseudoDestructorCallNode>) {
				CanonicalTypeDesc desc;
				desc.type_index = nativeTypeIndex(TypeCategory::Void);
				return type_context_.intern(desc);
			} else if constexpr (std::is_same_v<T, LambdaExpressionNode>) {
				// Lambda expression: its type is a unique closure class.
				// Look up the generated __lambda_N struct in getTypesByNameMap().
				const StringHandle lambda_name = e.generate_lambda_name();
				auto it = getTypesByNameMap().find(lambda_name);
				if (it != getTypesByNameMap().end() && it->second) {
					CanonicalTypeDesc desc;
					desc.type_index = nativeTypeIndex(TypeCategory::Struct);
					desc.type_index = it->second->type_index_;
					return type_context_.intern(desc);
				}
				return {};
			} else if constexpr (std::is_same_v<T, ThrowExpressionNode>) {
				// C++20 [expr.throw]: a throw-expression is of type void.
				CanonicalTypeDesc desc;
				desc.type_index = nativeTypeIndex(TypeCategory::Void);
				return type_context_.intern(desc);
			} else if constexpr (std::is_same_v<T, CallExprNode>) {
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

				const DeclarationNode& decl = e.callee().declaration();
				const ASTNode ret_type_node = decl.type_node();
				if (ret_type_node.has_value() && ret_type_node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& type_node = ret_type_node.as<TypeSpecifierNode>();
					if (!isPlaceholderAutoType(type_node.type())) {
						return canonicalizeType(type_node);
					}
				}
				return {};
			}
			return {};
		},
						  expr);
	}

	// Handle old-style nodes stored directly (not wrapped in ExpressionNode)
	if (node.is<NumericLiteralNode>()) {
		CanonicalTypeDesc desc;
		desc.type_index = nativeTypeIndex(node.as<NumericLiteralNode>().type());
		return type_context_.intern(desc);
	}
	if (node.is<BoolLiteralNode>()) {
		CanonicalTypeDesc desc;
		desc.type_index = nativeTypeIndex(TypeCategory::Bool);
		return type_context_.intern(desc);
	}

	return {};
}

std::optional<TypeSpecifierNode> SemanticAnalysis::buildOverloadResolutionArgType(
	const ASTNode& arg,
	CanonicalTypeId* inferred_type_id) {
	if (const CanonicalTypeId inferred_id = inferExpressionType(arg)) {
		if (inferred_type_id)
			*inferred_type_id = inferred_id;
		TypeSpecifierNode arg_type = materializeTypeSpecifier(type_context_.get(inferred_id));
		if (arg.is<ExpressionNode>()) {
			const ExpressionNode& expr = arg.as<ExpressionNode>();
			if (const auto* member_access = std::get_if<MemberAccessNode>(&expr)) {
				// C++20 [expr.ref]: a non-reference member access remains an lvalue for
				// lvalue bases but yields an xvalue for prvalue/xvalue bases. The generic
				// overload-resolution helper treats all member accesses as lvalues, so keep
				// this sema-owned refinement here instead of calling the generic helper
				// until that shared helper learns the same rule.
				if (!arg_type.is_reference()) {
					// Reference-typed members keep their declared reference category;
					// only object members without an explicit reference qualifier inherit
					// lvalue/xvalue behavior from the base expression.
					const ValueCategory object_category = inferExpressionValueCategory(member_access->object());
					switch (object_category) {
						case ValueCategory::LValue:
							arg_type.set_reference_qualifier(ReferenceQualifier::LValueReference);
							break;
						case ValueCategory::XValue:
							// [expr.ref]: xvalue base => xvalue member access.
							arg_type.set_reference_qualifier(ReferenceQualifier::RValueReference);
							break;
						case ValueCategory::PRValue:
							// [expr.ref]: prvalue base also yields an xvalue member access.
							arg_type.set_reference_qualifier(ReferenceQualifier::RValueReference);
							break;
						default:
							throw InternalError("Unexpected member-access object value category");
					}
				}
				// Member-access cases already have their final overload-resolution
				// category here; the generic helper would incorrectly rewrite all of
				// them to lvalues. TODO(Phase 2, docs/2026-04-04-codegen-name-lookup-investigation.md):
				// fold this into the shared helper once it understands member-access categories.
				return arg_type;
			}
		}
		adjust_argument_type_for_overload_resolution(arg, arg_type);
		return arg_type;
	}

	if (inferred_type_id)
		*inferred_type_id = {};
	return std::nullopt;
}

// --- Scoped enum diagnostic helper ---
// C++11+: scoped enums (enum class) do not allow implicit conversion to other types.

void SemanticAnalysis::diagnoseScopedEnumConversion(const ASTNode& expr_node,
													CanonicalTypeId target_type_id,
													const char* context_description,
													CanonicalTypeId expr_type_id) {
	if (!target_type_id || !expr_node.is<ExpressionNode>())
		return;
	if (!expr_type_id)
		expr_type_id = inferExpressionType(expr_node);
	if (!expr_type_id || expr_type_id == target_type_id)
		return;

	const CanonicalTypeDesc& from_desc = type_context_.get(expr_type_id);
	const CanonicalTypeDesc& to_desc = type_context_.get(target_type_id);

	if (from_desc.category() != TypeCategory::Enum)
		return;
	// Skip when both sides are the same enum type (same-type comparison/assignment is fine).
	if (to_desc.category() == TypeCategory::Enum && from_desc.type_index == to_desc.type_index)
		return;
	if (const TypeInfo* from_type_info = tryGetTypeInfo(from_desc.type_index)) {
		if (const EnumTypeInfo* ei = from_type_info->getEnumInfo()) {
			if (ei->is_scoped) {
				// Build target type name: use enum name if target is an enum, otherwise use primitive name.
				std::string target_name;
				if (to_desc.category() == TypeCategory::Enum) {
					if (const TypeInfo* target_type_info = tryGetTypeInfo(to_desc.type_index)) {
						if (const EnumTypeInfo* target_ei = target_type_info->getEnumInfo())
							target_name = StringTable::getStringView(target_ei->name);
					}
				}
				if (target_name.empty())
					target_name = getTypeName(to_desc.category());
				throw CompileError("cannot implicitly convert from scoped enum '" +
								   std::string(StringTable::getStringView(ei->name)) +
								   "' to '" + target_name +
								   "'" + std::string(context_description) + "; use static_cast");
			}
		}
	}
}

// --- Scoped enum binary operand diagnostic ---
// C++20: scoped enums only support relational/equality operators between values
// of the same scoped enum type.  Any other binary operator usage is ill-formed.

static bool isScopedEnum(const CanonicalTypeDesc& desc) {
	if (desc.category() != TypeCategory::Enum)
		return false;
	if (const TypeInfo* type_info = tryGetTypeInfo(desc.type_index)) {
		if (const EnumTypeInfo* ei = type_info->getEnumInfo())
			return ei->is_scoped;
	}
	return false;
}

static std::string getScopedEnumName(const CanonicalTypeDesc& desc) {
	if (const TypeInfo* type_info = tryGetTypeInfo(desc.type_index)) {
		if (const EnumTypeInfo* ei = type_info->getEnumInfo())
			return std::string(StringTable::getStringView(ei->name));
	}
	return "scoped enum";
}

void SemanticAnalysis::diagnoseScopedEnumBinaryOperands(const BinaryOperatorNode& bin_op,
														CanonicalTypeId lhs_type_id, CanonicalTypeId rhs_type_id) {
	if (!lhs_type_id)
		lhs_type_id = inferExpressionType(bin_op.get_lhs());
	if (!rhs_type_id)
		rhs_type_id = inferExpressionType(bin_op.get_rhs());
	if (!lhs_type_id || !rhs_type_id)
		return;

	const CanonicalTypeDesc& lhs_desc = type_context_.get(lhs_type_id);
	const CanonicalTypeDesc& rhs_desc = type_context_.get(rhs_type_id);

	const bool lhs_scoped = isScopedEnum(lhs_desc);
	const bool rhs_scoped = isScopedEnum(rhs_desc);
	if (!lhs_scoped && !rhs_scoped)
		return;

	const std::string_view op = bin_op.op();

	// Same scoped enum type: relational and equality comparisons are valid.
	if (lhs_scoped && rhs_scoped &&
		lhs_desc.category() == rhs_desc.category() &&
		lhs_desc.type_index == rhs_desc.type_index) {
		const bool is_comparison =
			op == "==" || op == "!=" || op == "<" || op == ">" ||
			op == "<=" || op == ">=";
		if (is_comparison)
			return; // valid same-type scoped enum comparison
	}

	// All other binary ops with a scoped enum operand are ill-formed.
	const std::string enum_name = lhs_scoped
									  ? getScopedEnumName(lhs_desc)
									  : getScopedEnumName(rhs_desc);
	throw CompileError("invalid operands to binary expression involving scoped enum '" +
					   enum_name + "' with operator '" + std::string(op) +
					   "'; use static_cast for explicit conversion");
}

// --- Conversion operator existence helper (Phase 5, Phase 21 Item 2) ---

// Phase 5: Returns true if the struct type described by from_desc has a user-defined
// conversion operator to the type described by to_desc.
// Used by tryAnnotateConversion to avoid emitting spurious UserDefined sema annotations
// when no matching conversion operator exists in the source struct.
// Mirrors the operator-existence logic in AstToIr::findConversionOperator, but is
// sema-owned and avoids interning new strings (uses string_view comparison only).
static bool structHasConversionOperatorTo(
	const CanonicalTypeDesc& from_desc,
	const CanonicalTypeDesc& to_desc,
	int depth = 0) {
	// Guard against infinite recursion in pathological inheritance graphs.
	static constexpr int kMaxInheritanceDepth = 8;
	if (depth > kMaxInheritanceDepth)
		return false;
	const TypeInfo* from_type_info = tryGetTypeInfo(from_desc.type_index);
	if (!from_type_info)
		return false;
	const StructTypeInfo* struct_info = from_type_info->getStructInfo();
	if (!struct_info)
		return false;
	TypeIndex canonical_target_type = canonicalize_conversion_target_type(to_desc.type_index, to_desc.category());
	if (!canonical_target_type.is_valid())
		return false;

	// Scan direct member functions for an exact conversion target match.
	for (const auto& mf : struct_info->member_functions) {
		if (mf.conversion_target_type == canonical_target_type)
			return true;
	}

	// Recurse into non-deferred base classes (inherited conversion operators).
	for (const auto& base : struct_info->base_classes) {
		if (base.is_deferred)
			continue;
		CanonicalTypeDesc base_from_desc;
		base_from_desc.type_index = nativeTypeIndex(TypeCategory::Struct);
		base_from_desc.type_index = base.type_index;
		if (structHasConversionOperatorTo(base_from_desc, to_desc, depth + 1))
			return true;
	}
	return false;
}

// --- Core conversion annotation helper ---

bool SemanticAnalysis::tryAnnotateConversion(const ASTNode& expr_node,
											 CanonicalTypeId target_type_id,
											 CanonicalTypeId expr_type_id) {
	if (!target_type_id)
		return false;
	if (!expr_node.is<ExpressionNode>())
		return false;

	if (!expr_type_id)
		expr_type_id = inferExpressionType(expr_node);
	if (!expr_type_id)
		return false;
	if (expr_type_id == target_type_id)
		return false; // exact match, no cast needed

	const CanonicalTypeDesc& from_desc = type_context_.get(expr_type_id);
	const CanonicalTypeDesc& to_desc = type_context_.get(target_type_id);

	// Same base type but different canonical IDs (differ only in qualifiers or type_index,
	// e.g. two UserDefined aliases, const vs non-const, etc.): no primitive conversion needed.
	if (from_desc.category() == to_desc.category())
		return false;

	// Bail out if either side is not a plain primitive scalar (or enum/struct source).
	// Enum source types are allowed: C++20 permits implicit enum->primitive conversions
	// (integral promotion, integral/floating conversion, boolean conversion).
	// Enum as *target* is rejected: C++11+ forbids implicit conversion TO enum.
	// Struct source is allowed when target is primitive: user-defined conversion operator.
	// is_unresolved_type: sources where sema cannot determine a concrete scalar conversion.
	auto is_unresolved_type = [](TypeCategory t) {
		return t == TypeCategory::UserDefined || t == TypeCategory::Invalid || isPlaceholderAutoType(t);
	};
	auto is_non_primitive_target = [](TypeCategory t) {
		return is_struct_type(t) ||
			   t == TypeCategory::Invalid || isPlaceholderAutoType(t);
	};
	if (!from_desc.pointer_levels.empty() || !to_desc.pointer_levels.empty())
		return false;
	if (!from_desc.array_dimensions.empty() || !to_desc.array_dimensions.empty())
		return false;
	// Allow Struct source for user-defined conversion operators (Struct->primitive).
	// Reject Struct->Struct (handled elsewhere) and primitive->Struct (converting constructors).
	if (from_desc.category() == TypeCategory::Struct && to_desc.category() == TypeCategory::Struct)
		return false;
	if (from_desc.category() != TypeCategory::Struct && is_unresolved_type(from_desc.category()))
		return false;
	if (is_non_primitive_target(to_desc.category()))
		return false;
	if (to_desc.category() == TypeCategory::Enum)
		return false; // no implicit conversion TO enum
	// C++11+: scoped enums (enum class) do not allow implicit conversion to other types.
	// Silently reject here; callers that need a diagnostic (variable init, return, assignment)
	// check isScopedEnum() and throw CompileError themselves.
	if (from_desc.category() == TypeCategory::Enum) {
		if (const TypeInfo* from_type_info = tryGetTypeInfo(from_desc.type_index)) {
			if (const EnumTypeInfo* ei = from_type_info->getEnumInfo()) {
				if (ei->is_scoped)
					return false;
			}
		}
	}
	if (from_desc.ref_qualifier != ReferenceQualifier::None)
		return false;

	const CanonicalTypeAlias from_canonical = canonicalize_type_alias(from_desc.type_index);
	const CanonicalTypeAlias to_canonical = canonicalize_type_alias(to_desc.type_index);
	const ConversionPlan plan = buildConversionPlan(from_canonical.typeEnum(), to_canonical.typeEnum());
	if (!plan.is_valid)
		return false;
	// Allow UserDefined rank only when source is Struct (conversion operator case).
	// Reject UserDefined for non-struct sources (converting constructors are separate).
	if (plan.rank == ConversionRank::UserDefined && from_desc.category() != TypeCategory::Struct)
		return false;

	// Phase 5: for UserDefined (struct->primitive) annotations, verify that a conversion
	// operator actually exists before annotating. Without this check, sema optimistically
	// annotates UserDefined for any Struct->primitive pair (Phase 21 item 2), which inflates
	// slots_filled stats and misrepresents the actual operator availability.
	// Codegen already handles null conv_op safely, so this is a stats/accuracy fix only.
	if (plan.rank == ConversionRank::UserDefined) {
		if (!structHasConversionOperatorTo(from_desc, to_desc)) {
			FLASH_LOG(General, Debug,
					  "SemanticAnalysis: skipping UserDefined annotation — "
					  "no conversion operator found in struct source type");
			return false;
		}
	}

	ImplicitCastInfo cast_info;
	cast_info.source_type_id = expr_type_id;
	cast_info.target_type_id = target_type_id;
	cast_info.cast_kind = plan.kind;
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
			  static_cast<int>(from_desc.category()), " -> ",
			  static_cast<int>(to_desc.category()),
			  " (kind=", static_cast<int>(plan.kind), ")");
	return true;
}

bool SemanticAnalysis::tryAnnotateCopyInitConvertingConstructor(const ASTNode& expr_node,
																CanonicalTypeId target_type_id,
																const char* context_description,
																CanonicalTypeId expr_type_id) {
	if (!target_type_id)
		return false;
	if (!expr_node.is<ExpressionNode>())
		return false;

	if (!expr_type_id)
		expr_type_id = inferExpressionType(expr_node);
	if (!expr_type_id || expr_type_id == target_type_id)
		return false;

	const CanonicalTypeDesc& from_desc = type_context_.get(expr_type_id);
	const CanonicalTypeDesc& to_desc = type_context_.get(target_type_id);

	if (to_desc.category() != TypeCategory::Struct || !to_desc.type_index.is_valid())
		return false;
	if (!from_desc.pointer_levels.empty() || !to_desc.pointer_levels.empty())
		return false;
	if (!from_desc.array_dimensions.empty() || !to_desc.array_dimensions.empty())
		return false;
	if (from_desc.ref_qualifier != ReferenceQualifier::None || to_desc.ref_qualifier != ReferenceQualifier::None)
		return false;
	if (from_desc.category() == TypeCategory::UserDefined ||
		from_desc.category() == TypeCategory::Invalid || isPlaceholderAutoType(from_desc.category())) {
		return false;
	}
	const TypeInfo* to_type_info = tryGetTypeInfo(to_desc.type_index);
	if (!to_type_info)
		return false;

	const StructTypeInfo* struct_info = to_type_info->getStructInfo();
	if (!struct_info || !struct_info->hasAnyConstructor())
		return false;

	auto arg_type_opt = buildOverloadResolutionArgType(expr_node, nullptr);
	if (!arg_type_opt.has_value())
		return false;

	const TypeSpecifierNode& arg_type = *arg_type_opt;
	const ConstructorDeclarationNode* best_non_explicit = nullptr;
	const ConstructorDeclarationNode* best_any = nullptr;
	ConversionRank best_non_explicit_rank = ConversionRank::NoMatch;
	ConversionRank best_any_rank = ConversionRank::NoMatch;
	bool ambiguous_non_explicit = false;
	bool found_explicit_viable = false;

	for (const auto& member_func : struct_info->member_functions) {
		if (!member_func.is_constructor || !member_func.function_decl.is<ConstructorDeclarationNode>()) {
			continue;
		}

		const auto& ctor_decl = member_func.function_decl.as<ConstructorDeclarationNode>();
		if (isImplicitCopyOrMoveConstructorCandidate(*struct_info, ctor_decl)) {
			continue;
		}

		const auto& parameters = ctor_decl.parameter_nodes();
		if (parameters.empty() || countMinRequiredArgs(ctor_decl) > 1) {
			continue;
		}
		if (!parameters[0].is<DeclarationNode>())
			continue;

		const ASTNode& param_type_node = parameters[0].as<DeclarationNode>().type_node();
		if (!param_type_node.is<TypeSpecifierNode>())
			continue;
		const auto& param_type = param_type_node.as<TypeSpecifierNode>();

		const TypeConversionResult conversion = can_convert_type(arg_type, param_type);
		if (!conversion.is_valid)
			continue;
		// A converting-constructor candidate already contributes the single permitted
		// user-defined conversion in the implicit conversion sequence. The source
		// argument must therefore reach the constructor's first parameter using only
		// standard conversions/reference binding, not another converting constructor.
		if (conversion.rank == ConversionRank::UserDefined)
			continue;

		if (!best_any || conversion.rank < best_any_rank) {
			best_any = &ctor_decl;
			best_any_rank = conversion.rank;
		}

		if (ctor_decl.is_explicit()) {
			found_explicit_viable = true;
			continue;
		}

		if (!best_non_explicit || conversion.rank < best_non_explicit_rank) {
			best_non_explicit = &ctor_decl;
			best_non_explicit_rank = conversion.rank;
			ambiguous_non_explicit = false;
		} else if (conversion.rank == best_non_explicit_rank) {
			ambiguous_non_explicit = true;
		}
	}

	if (ambiguous_non_explicit) {
		throw CompileError("Ambiguous constructor call");
	}
	if (!best_non_explicit) {
		if (found_explicit_viable || best_any) {
			// Special case: don't error for integer -> comparison category conversions.
			// In deferred template bodies, inferExpressionType may not fully resolve
			// built-in <=> results as comparison category types, so semantic analysis
			// can see "int -> strong_ordering" instead of the final target type.
			// Codegen still produces the correct comparison category object.
			const StructTypeInfo* target_struct = to_type_info->getStructInfo();
			if (is_integer_type(from_desc.category()) && target_struct && toSizeT(target_struct->total_size) == 1) {
				const std::string_view target_name = StringTable::getStringView(to_type_info->name());
				if (isExactComparisonCategoryType(to_type_info->type_index_)) {
					FLASH_LOG(General, Debug, "Suppressed explicit-ctor error for comparison category type '",
							  target_name, "' from integer type", context_description);
					return false;
				}
			}
			FLASH_LOG(General, Error, "Cannot use copy initialization with explicit constructor for target type '",
					  StringTable::getStringView(to_type_info->name()), "' from type category ",
					  static_cast<int>(from_desc.category()), context_description);
			throw CompileError("Cannot use copy initialization with explicit constructor");
		}
		return false;
	}

	const auto& selected_params = best_non_explicit->parameter_nodes();
	if (selected_params.empty() || !selected_params[0].is<DeclarationNode>())
		return false;
	const ASTNode& selected_param_type_node = selected_params[0].as<DeclarationNode>().type_node();
	if (!selected_param_type_node.is<TypeSpecifierNode>())
		return false;

	const CanonicalTypeId param_type_id = canonicalizeType(selected_param_type_node.as<TypeSpecifierNode>());
	if (!param_type_id)
		return false;

	diagnoseScopedEnumConversion(expr_node, param_type_id, context_description, expr_type_id);

	ImplicitCastInfo cast_info;
	cast_info.source_type_id = expr_type_id;
	cast_info.target_type_id = target_type_id;
	cast_info.cast_kind = StandardConversionKind::UserDefined;
	cast_info.value_category_after = ValueCategory::PRValue;
	cast_info.selected_constructor = best_non_explicit;

	const CastInfoIndex idx = allocateCastInfo(cast_info);

	SemanticSlot slot;
	slot.type_id = target_type_id;
	slot.cast_info_index = idx;
	slot.value_category = ValueCategory::PRValue;

	const void* key = static_cast<const void*>(&expr_node.as<ExpressionNode>());
	setSlot(key, slot);
	stats_.slots_filled++;

	FLASH_LOG(General, Debug,
			  "SemanticAnalysis: annotated copy-init converting constructor for target struct type index ",
			  to_desc.type_index.index());
	return true;
}

// --- Return conversion annotation ---

void SemanticAnalysis::tryAnnotateReturnConversion(const ASTNode& expr_node, const SemanticContext& ctx) {
	if (!ctx.current_function_return_type_id)
		return;
	if (!tryAnnotateCopyInitConvertingConstructor(expr_node, *ctx.current_function_return_type_id,
												  " in return statement")) {
		tryAnnotateConversion(expr_node, *ctx.current_function_return_type_id);
		diagnoseScopedEnumConversion(expr_node, *ctx.current_function_return_type_id,
									 " in return statement");
	}
}

// --- Binary operand conversion annotation ---

void SemanticAnalysis::tryAnnotateBinaryOperandConversions(const BinaryOperatorNode& bin_op,
														   CanonicalTypeId lhs_type_id, CanonicalTypeId rhs_type_id) {
	if (!lhs_type_id)
		lhs_type_id = inferExpressionType(bin_op.get_lhs());
	if (!rhs_type_id)
		rhs_type_id = inferExpressionType(bin_op.get_rhs());
	if (!lhs_type_id || !rhs_type_id)
		return;

	const CanonicalTypeDesc& lhs_desc = type_context_.get(lhs_type_id);
	const CanonicalTypeDesc& rhs_desc = type_context_.get(rhs_type_id);

	// Only handle plain primitive types and enum sources (no pointers, no arrays, no structs)
	if (!lhs_desc.pointer_levels.empty() || !rhs_desc.pointer_levels.empty())
		return;
	if (!lhs_desc.array_dimensions.empty() || !rhs_desc.array_dimensions.empty())
		return;
	if (lhs_desc.category() == TypeCategory::Struct || rhs_desc.category() == TypeCategory::Struct)
		return;
	if (lhs_desc.category() == TypeCategory::Invalid || rhs_desc.category() == TypeCategory::Invalid)
		return;
	if (isPlaceholderAutoType(lhs_desc.category()) || isPlaceholderAutoType(rhs_desc.category()))
		return;

	// Resolve enum operands to their underlying type for get_common_type,
	// which only handles primitive integer/floating-point types.
	const TypeCategory lhs_cat = resolveEnumUnderlyingTypeCategory(lhs_desc.type_index);
	const TypeCategory rhs_cat = resolveEnumUnderlyingTypeCategory(rhs_desc.type_index);

	const TypeCategory common_cat = get_common_type(lhs_cat, rhs_cat);
	if (common_cat == TypeCategory::Invalid)
		return;

	// Intern the common type
	CanonicalTypeDesc common_desc;
	common_desc.type_index = nativeTypeIndex(common_cat);
	const CanonicalTypeId common_type_id = type_context_.intern(common_desc);

	tryAnnotateConversion(bin_op.get_lhs(), common_type_id, lhs_type_id);
	tryAnnotateConversion(bin_op.get_rhs(), common_type_id, rhs_type_id);
}

// --- Compound assignment back-conversion annotation ---
// C++20 [expr.ass]/7: E1 op= E2 is equivalent to E1 = static_cast<T1>(E1 op E2).
// When the common type differs from the LHS type, the arithmetic result must be
// converted back from common type to LHS type.  Annotate this on the
// BinaryOperatorNode so that codegen hard enforcement can verify sema ownership.

// Shared helper: build an ImplicitCastInfo for source_type -> target_type and
// store the resulting SemanticSlot in compound_assign_back_conv_ keyed on &bin_op.
// Both shift and non-shift compound assignments use this same slot structure.
void SemanticAnalysis::storeCompoundAssignBackConvSlot(const BinaryOperatorNode& bin_op,
													   CanonicalTypeId source_type_id, CanonicalTypeId target_type_id) {
	const CanonicalTypeDesc& src_desc = type_context_.get(source_type_id);
	const CanonicalTypeDesc& tgt_desc = type_context_.get(target_type_id);
	const CanonicalTypeAlias src_canonical = canonicalize_type_alias(src_desc.type_index);
	const CanonicalTypeAlias tgt_canonical = canonicalize_type_alias(tgt_desc.type_index);
	const ConversionPlan plan = buildConversionPlan(src_canonical.typeEnum(), tgt_canonical.typeEnum());
	if (!plan.is_valid || plan.rank == ConversionRank::UserDefined)
		return;

	ImplicitCastInfo cast_info;
	cast_info.source_type_id = source_type_id;
	cast_info.target_type_id = target_type_id;
	cast_info.cast_kind = plan.kind;
	cast_info.value_category_after = ValueCategory::PRValue;

	const CastInfoIndex idx = allocateCastInfo(cast_info);

	SemanticSlot slot;
	slot.type_id = target_type_id;
	slot.cast_info_index = idx;
	slot.value_category = ValueCategory::PRValue;

	compound_assign_back_conv_[static_cast<const void*>(&bin_op)] = slot;
	stats_.slots_filled++;

	FLASH_LOG(General, Debug, "SemanticAnalysis: annotated compound assign back-conversion ",
			  static_cast<int>(src_desc.category()), " -> ", static_cast<int>(tgt_desc.category()),
			  " (kind=", static_cast<int>(plan.kind), ")");
}

void SemanticAnalysis::tryAnnotateCompoundAssignBackConversion(const BinaryOperatorNode& bin_op,
															   CanonicalTypeId lhs_type_id, CanonicalTypeId rhs_type_id) {
	if (!lhs_type_id)
		lhs_type_id = inferExpressionType(bin_op.get_lhs());
	if (!rhs_type_id)
		rhs_type_id = inferExpressionType(bin_op.get_rhs());
	if (!lhs_type_id || !rhs_type_id)
		return;

	const CanonicalTypeDesc& lhs_desc = type_context_.get(lhs_type_id);
	const CanonicalTypeDesc& rhs_desc = type_context_.get(rhs_type_id);

	// Same guards as tryAnnotateBinaryOperandConversions
	if (!lhs_desc.pointer_levels.empty() || !rhs_desc.pointer_levels.empty())
		return;
	if (!lhs_desc.array_dimensions.empty() || !rhs_desc.array_dimensions.empty())
		return;
	if (lhs_desc.category() == TypeCategory::Struct || rhs_desc.category() == TypeCategory::Struct)
		return;
	if (lhs_desc.category() == TypeCategory::Invalid || rhs_desc.category() == TypeCategory::Invalid)
		return;
	if (isPlaceholderAutoType(lhs_desc.category()) || isPlaceholderAutoType(rhs_desc.category()))
		return;

	const TypeCategory lhs_cat = resolveEnumUnderlyingTypeCategory(lhs_desc.type_index);
	const TypeCategory rhs_cat = resolveEnumUnderlyingTypeCategory(rhs_desc.type_index);

	const TypeCategory common_cat = get_common_type(lhs_cat, rhs_cat);
	if (common_cat == TypeCategory::Invalid)
		return;
	if (lhs_cat == common_cat)
		return; // No back-conversion needed

	// Build the back-conversion: common -> lhs_base
	CanonicalTypeDesc common_desc;
	common_desc.type_index = nativeTypeIndex(common_cat);
	const CanonicalTypeId common_type_id = type_context_.intern(common_desc);

	CanonicalTypeDesc lhs_base_desc;
	lhs_base_desc.type_index = nativeTypeIndex(lhs_cat);
	const CanonicalTypeId lhs_base_id = type_context_.intern(lhs_base_desc);

	storeCompoundAssignBackConvSlot(bin_op, common_type_id, lhs_base_id);
}

// --- Shift operand independent promotion annotation ---
// C++20 [expr.shift]: The operands shall be of integral or unscoped enumeration type
// and integral promotions are performed.  The type of the result is that of the
// promoted left operand.  Unlike usual arithmetic conversions, each operand is
// promoted independently — there is no shared "common type".

void SemanticAnalysis::tryAnnotateShiftOperandPromotions(const BinaryOperatorNode& bin_op,
														 CanonicalTypeId lhs_type_id, CanonicalTypeId rhs_type_id) {
	if (!lhs_type_id)
		lhs_type_id = inferExpressionType(bin_op.get_lhs());
	if (!rhs_type_id)
		rhs_type_id = inferExpressionType(bin_op.get_rhs());
	if (!lhs_type_id || !rhs_type_id)
		return;

	const CanonicalTypeDesc& lhs_desc = type_context_.get(lhs_type_id);
	const CanonicalTypeDesc& rhs_desc = type_context_.get(rhs_type_id);

	// Only handle plain primitive integral types and enum sources (no pointers, arrays, structs, floats)
	if (!lhs_desc.pointer_levels.empty() || !rhs_desc.pointer_levels.empty())
		return;
	if (!lhs_desc.array_dimensions.empty() || !rhs_desc.array_dimensions.empty())
		return;
	if (lhs_desc.category() == TypeCategory::Struct || rhs_desc.category() == TypeCategory::Struct)
		return;
	if (lhs_desc.category() == TypeCategory::Invalid || rhs_desc.category() == TypeCategory::Invalid)
		return;
	if (isPlaceholderAutoType(lhs_desc.category()) || isPlaceholderAutoType(rhs_desc.category()))
		return;

	// Resolve enum operands to their underlying type for promote_integer_type,
	// which only handles primitive integer types.
	const TypeCategory lhs_cat = resolveEnumUnderlyingTypeCategory(lhs_desc.type_index);
	const TypeCategory rhs_cat = resolveEnumUnderlyingTypeCategory(rhs_desc.type_index);

	// Shift is only defined for integral operands
	if (isFloatingPointType(lhs_cat) || isFloatingPointType(rhs_cat))
		return;

	// Independent integral promotion for each operand
	const TypeCategory promoted_lhs = promote_integer_type(lhs_cat);
	const TypeCategory promoted_rhs = promote_integer_type(rhs_cat);

	if (promoted_lhs != lhs_cat) {
		CanonicalTypeDesc promoted_desc;
		promoted_desc.type_index = nativeTypeIndex(promoted_lhs);
		tryAnnotateConversion(bin_op.get_lhs(), type_context_.intern(promoted_desc), lhs_type_id);
	}
	if (promoted_rhs != rhs_cat) {
		CanonicalTypeDesc promoted_desc;
		promoted_desc.type_index = nativeTypeIndex(promoted_rhs);
		tryAnnotateConversion(bin_op.get_rhs(), type_context_.intern(promoted_desc), rhs_type_id);
	}
}

// --- Unary operand integral promotion annotation ---
// C++20 [expr.unary.op]: The operand of unary +, -, and ~ shall have arithmetic or
// unscoped enumeration type and the result is the value of its operand promoted.
// Integral promotions are performed (bool/char/short -> int).

void SemanticAnalysis::tryAnnotateUnaryOperandPromotion(const UnaryOperatorNode& unary_op) {
	const CanonicalTypeId operand_type_id = inferExpressionType(unary_op.get_operand());
	if (!operand_type_id)
		return;

	const CanonicalTypeDesc& operand_desc = type_context_.get(operand_type_id);

	// Only handle plain primitive types and enum sources (no pointers, arrays, structs)
	if (!operand_desc.pointer_levels.empty())
		return;
	if (!operand_desc.array_dimensions.empty())
		return;
	if (operand_desc.category() == TypeCategory::Struct || operand_desc.category() == TypeCategory::Invalid)
		return;
	if (isPlaceholderAutoType(operand_desc.category()))
		return;

	// Resolve enum operands to their underlying type for promote_integer_type
	const TypeCategory operand_cat = resolveEnumUnderlyingTypeCategory(operand_desc.type_index);

	// Unary +, -, ~ are only defined for arithmetic types
	// C++20 [expr.unary.op]/10: ~ requires integral or unscoped enumeration type.
	if (unary_op.op() == "~" && isFloatingPointType(operand_cat)) {
		throw CompileError("operand of '~' must have integral or unscoped enumeration type");
	}
	if (isFloatingPointType(operand_cat))
		return; // float/double: no promotion needed

	const TypeCategory promoted_cat = promote_integer_type(operand_cat);
	if (promoted_cat != operand_cat) {
		CanonicalTypeDesc promoted_desc;
		promoted_desc.type_index = nativeTypeIndex(promoted_cat);
		tryAnnotateConversion(unary_op.get_operand(), type_context_.intern(promoted_desc));
	}
}

// --- Contextual bool annotation ---
// C++20 [conv.bool]: A prvalue of arithmetic, unscoped enumeration, pointer, or
// pointer-to-member type can be converted to a prvalue of type bool.
// Used for: if/while/for/do-while conditions, ternary condition, && / || operands.

void SemanticAnalysis::tryAnnotateContextualBool(const ASTNode& expr_node) {
	// First try the standard primitive conversion path (handles int/float/char -> bool).
	if (tryAnnotateConversion(expr_node, bool_type_id_))
		return;

	// Handle enum/pointer -> bool (C++20 [conv.bool]/1): contextual conversion
	// to bool applies to enums and pointers. Zero/null -> false, non-zero -> true.
	// The backend TEST instruction already handles this correctly; the annotation
	// records the semantic intent for future codegen migration.
	if (!expr_node.is<ExpressionNode>())
		return;
	const CanonicalTypeId expr_type_id = inferExpressionType(expr_node);
	if (!expr_type_id)
		return;
	const CanonicalTypeDesc& from_desc = type_context_.get(expr_type_id);
	const bool is_enum = (from_desc.category() == TypeCategory::Enum);
	const bool is_pointer = !from_desc.pointer_levels.empty();
	if (!is_enum && !is_pointer)
		return;

	ImplicitCastInfo cast_info;
	cast_info.source_type_id = expr_type_id;
	cast_info.target_type_id = bool_type_id_;
	cast_info.cast_kind = is_pointer
							  ? StandardConversionKind::PointerConversion
							  : StandardConversionKind::BooleanConversion;
	cast_info.value_category_after = ValueCategory::PRValue;

	const CastInfoIndex idx = allocateCastInfo(cast_info);

	SemanticSlot slot;
	slot.type_id = bool_type_id_;
	slot.cast_info_index = idx;
	slot.value_category = ValueCategory::PRValue;

	const void* key = static_cast<const void*>(&expr_node.as<ExpressionNode>());
	setSlot(key, slot);
	stats_.slots_filled++;
}

// --- Callable operator() resolution ---

void SemanticAnalysis::tryResolveCallableOperatorImpl(const CallInfo& call_info, const void* call_key) {
	if (call_info.has_receiver)
		return;

	const DeclarationNode& callee_decl = *call_info.declaration;
	const StringHandle callee_name = callee_decl.identifier_token().handle();
	if (!callee_name.isValid())
		return;

	const CanonicalTypeId callee_type_id = lookupLocalType(callee_name);
	if (!callee_type_id)
		return;

	const CanonicalTypeDesc& callee_desc = type_context_.get(callee_type_id);
	if (callee_desc.category() != TypeCategory::Struct)
		return;
	const TypeInfo* callee_type_info = tryGetTypeInfo(callee_desc.type_index);
	if (!callee_type_info)
		return;

	const StructTypeInfo* struct_info = callee_type_info->getStructInfo();
	if (!struct_info)
		return;

	const ChunkedVector<ASTNode>& arguments = *call_info.arguments;
	const size_t arg_count = arguments.size();
	std::vector<ASTNode> candidates;
	for (const auto& member_func : struct_info->member_functions) {
		if (member_func.operator_kind != OverloadableOperator::Call)
			continue;
		if (!member_func.function_decl.is<FunctionDeclarationNode>())
			continue;
		candidates.push_back(member_func.function_decl);
	}
	if (candidates.empty())
		return;

	std::vector<TypeSpecifierNode> arg_types;
	arg_types.reserve(arg_count);
	bool all_types_known = true;
	for (const ASTNode& arg : arguments) {
		const CanonicalTypeId arg_type_id = inferExpressionType(arg);
		if (arg_type_id) {
			arg_types.push_back(materializeTypeSpecifier(type_context_.get(arg_type_id)));
		} else {
			all_types_known = false;
			break;
		}
	}

	const FunctionDeclarationNode* best_match = nullptr;
	bool explicitly_ambiguous = false;
	if (all_types_known) {
		const OverloadResolutionResult result = resolve_overload(candidates, arg_types);
		if (result.has_match && !result.is_ambiguous) {
			best_match = &result.selected_overload->as<FunctionDeclarationNode>();
		}
		if (result.is_ambiguous) {
			explicitly_ambiguous = true;
		}
	}

	if (!best_match && !explicitly_ambiguous) {
		const FunctionDeclarationNode* default_argument_match = nullptr;
		bool default_argument_match_ambiguous = false;
		for (const auto& candidate_node : candidates) {
			const auto& candidate = candidate_node.as<FunctionDeclarationNode>();
			if (!candidate.is_variadic() && candidate.parameter_nodes().size() == arg_count) {
				best_match = &candidate;
				break;
			}
			if (!callableOperatorAcceptsArgumentCount(candidate, arg_count))
				continue;
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

	if (!best_match)
		return;

	op_call_table_[call_key] = best_match;
	stats_.op_calls_resolved++;

	FLASH_LOG_FORMAT(General, Debug,
					 "SemanticAnalysis: resolved operator() for '{}' -> {} params",
					 callee_decl.identifier_token().value(),
					 best_match->parameter_nodes().size());
}


void SemanticAnalysis::tryResolveCallableOperator(const CallExprNode& call_node) {
	tryResolveCallableOperatorImpl(CallInfo::from(call_node), &call_node);
}

void SemanticAnalysis::tryResolveSubscriptOperator(const ArraySubscriptNode& subscript_node) {
	// Determine whether the array expression has struct type (no pointer, no array dims).
	const CanonicalTypeId object_type_id = inferExpressionType(subscript_node.array_expr());
	if (!object_type_id)
		return;

	const CanonicalTypeDesc& object_desc = type_context_.get(object_type_id);
	if (object_desc.category() != TypeCategory::Struct)
		return;
	if (!object_desc.pointer_levels.empty() || !object_desc.array_dimensions.empty())
		return;

	const TypeInfo* type_info = tryGetTypeInfo(object_desc.type_index);
	if (!type_info)
		return;

	const StructTypeInfo* struct_info = type_info->getStructInfo();
	if (!struct_info)
		return;

	// Collect all operator[] candidates from this struct and its base classes.
	// Use a visited set to avoid collecting duplicate candidates in diamond inheritance.
	std::vector<ASTNode> candidates;
	std::unordered_set<const StructTypeInfo*> visited;
	auto collectCandidates = [&](auto&& self, const StructTypeInfo* current_struct) -> void {
		if (!visited.insert(current_struct).second)
			return;
		for (const auto& member_func : current_struct->member_functions) {
			if (member_func.operator_kind != OverloadableOperator::Subscript)
				continue;
			if (!member_func.function_decl.is<FunctionDeclarationNode>())
				continue;
			candidates.push_back(member_func.function_decl);
		}
		for (const auto& base_spec : current_struct->base_classes) {
			if (!base_spec.type_index.is_valid())
				continue;
			const TypeInfo* base_info = tryGetTypeInfo(base_spec.type_index);
			if (!base_info)
				continue;
			const StructTypeInfo* base_struct = base_info->getStructInfo();
			if (!base_struct)
				continue;
			self(self, base_struct);
		}
	};
	collectCandidates(collectCandidates, struct_info);

	if (candidates.empty())
		return;

	// Try overload resolution with the index argument type.
	const CanonicalTypeId index_type_id = inferExpressionType(subscript_node.index_expr());
	const FunctionDeclarationNode* best_match = nullptr;
	bool explicitly_ambiguous = false;

	if (index_type_id) {
		const TypeSpecifierNode index_type_spec = materializeTypeSpecifier(type_context_.get(index_type_id));
		std::vector<TypeSpecifierNode> arg_types = {index_type_spec};
		const OverloadResolutionResult result = resolve_overload(candidates, arg_types);
		if (result.has_match && !result.is_ambiguous)
			best_match = &result.selected_overload->as<FunctionDeclarationNode>();
		if (result.is_ambiguous)
			explicitly_ambiguous = true;
	}

	if (!best_match && !explicitly_ambiguous) {
		// Fallback: arity-based selection (single param matching).
		for (const auto& candidate_node : candidates) {
			const auto& candidate = candidate_node.as<FunctionDeclarationNode>();
			if (candidate.parameter_nodes().size() == 1) {
				best_match = &candidate;
				break;
			}
		}
	}

	if (!best_match)
		return;

	op_subscript_table_[&subscript_node] = best_match;

	FLASH_LOG_FORMAT(General, Debug,
					 "SemanticAnalysis: resolved operator[] on struct '{}' -> {} params",
					 StringTable::getStringView(type_info->name()),
					 best_match->parameter_nodes().size());
}

std::optional<CallArgReferenceBindingInfo> SemanticAnalysis::buildCallArgReferenceBinding(const ASTNode& arg,
																						  const TypeSpecifierNode& param_type,
																						  const char* context_description) {
	if (!arg.is<ExpressionNode>())
		return std::nullopt;
	if (!param_type.is_reference() && !param_type.is_rvalue_reference())
		return std::nullopt;

	CanonicalTypeId inferred_arg_type_id{};
	auto arg_binding_type_opt = buildOverloadResolutionArgType(arg, &inferred_arg_type_id);
	if (!arg_binding_type_opt.has_value())
		return std::nullopt;

	const TypeSpecifierNode& arg_binding_type = *arg_binding_type_opt;
	TypeSpecifierNode param_value_type = param_type;
	param_value_type.set_reference_qualifier(ReferenceQualifier::None);
	TypeSpecifierNode arg_value_type = arg_binding_type;
	arg_value_type.set_reference_qualifier(ReferenceQualifier::None);

	const CanonicalTypeId param_type_id = canonicalizeType(param_type);
	const CanonicalTypeId param_value_type_id = canonicalizeType(param_value_type);
	const CanonicalTypeId arg_value_type_id = canonicalizeType(arg_value_type);
	if (!param_type_id || !param_value_type_id || !arg_value_type_id)
		return std::nullopt;

	diagnoseScopedEnumConversion(arg, param_value_type_id, context_description, inferred_arg_type_id);

	CallArgReferenceBindingInfo info;
	info.parameter_type_id = param_type_id;

	const bool arg_is_lvalue = arg_binding_type.is_lvalue_reference();
	const bool arg_is_xvalue = arg_binding_type.is_rvalue_reference();
	const ConversionPlan direct_plan = buildConversionPlan(arg_binding_type, param_type);
	if (direct_plan.is_valid && (arg_is_lvalue || arg_is_xvalue) &&
		(direct_plan.kind == StandardConversionKind::None ||
		 direct_plan.kind == StandardConversionKind::DerivedToBase)) {
		info.flags = ConversionPlanFlags::IsValid | ConversionPlanFlags::BindsReferenceDirectly;
		return info;
	}

	if (arg_is_xvalue && param_type.is_reference() && param_type.is_const()) {
		const ConversionPlan xvalue_const_lref_plan = buildConversionPlan(arg_value_type, param_value_type);
		if (xvalue_const_lref_plan.is_valid &&
			xvalue_const_lref_plan.kind == StandardConversionKind::None) {
			info.flags = ConversionPlanFlags::IsValid | ConversionPlanFlags::BindsReferenceDirectly;
			return info;
		}
	}

	if (param_type.is_rvalue_reference()) {
		if (arg_is_lvalue)
			return std::nullopt;
	} else if (param_type.is_reference()) {
		if (!param_type.is_const())
			return std::nullopt;
	}

	const ConversionPlan value_plan = buildConversionPlan(arg_value_type, param_value_type);
	if (!value_plan.is_valid || value_plan.rank == ConversionRank::UserDefined) {
		return std::nullopt;
	}

	info.flags = ConversionPlanFlags::IsValid | ConversionPlanFlags::MaterializesTemporary;
	if (value_plan.kind != StandardConversionKind::None) {
		info.pre_bind_cast_info_index = allocateNonUserDefinedCastInfo(
			arg_value_type_id,
			param_value_type_id,
			value_plan.kind);
	}
	return info;
}

// --- Shared single-argument conversion annotation ---
// Factored out of the per-argument loops in tryAnnotateCallArgConversions,
// tryAnnotateMemberFunctionCallArgConversions, and the ArraySubscriptNode
// operator[] annotation path.

void SemanticAnalysis::tryAnnotateSingleArgConversion(const ASTNode& arg,
													  const TypeSpecifierNode& param_type,
													  const char* context_description) {
	if (!arg.is<ExpressionNode>())
		return;

	if (param_type.is_reference() || param_type.is_rvalue_reference()) {
		buildCallArgReferenceBinding(arg, param_type, context_description);
		return;
	}

	const CanonicalTypeId param_type_id = canonicalizeType(param_type);
	const CanonicalTypeId arg_type_id = inferExpressionType(arg);
	if (arg_type_id && canonical_types_match(arg_type_id, param_type_id))
		return;

	if (!tryAnnotateCopyInitConvertingConstructor(arg, param_type_id,
												  context_description, arg_type_id)) {
		tryAnnotateConversion(arg, param_type_id, arg_type_id);
		diagnoseScopedEnumConversion(arg, param_type_id, context_description, arg_type_id);
	}
}

// --- Function call argument conversion annotation ---

void SemanticAnalysis::annotateResolvedCallArgConversions(const void* call_key,
														  const ChunkedVector<ASTNode>& arguments,
														  const FunctionDeclarationNode& func_decl,
														  const char* context_description) {
	if (func_decl.is_variadic())
		return;

	const auto& param_nodes = func_decl.parameter_nodes();
	if (arguments.size() < countMinRequiredArgs(func_decl) || arguments.size() > param_nodes.size())
		return;

	auto& ref_bindings = call_ref_bindings_[call_key];
	ref_bindings.clear();
	ref_bindings.resize(arguments.size());

	for (size_t i = 0; i < arguments.size(); ++i) {
		const ASTNode& arg = arguments[i];
		if (!arg.is<ExpressionNode>())
			continue;

		const ASTNode& param_node = param_nodes[i];
		if (!param_node.is<DeclarationNode>())
			continue;
		const ASTNode param_type_node = param_node.as<DeclarationNode>().type_node();
		if (!param_type_node.has_value() || !param_type_node.is<TypeSpecifierNode>())
			continue;
		const TypeSpecifierNode& param_type = param_type_node.as<TypeSpecifierNode>();

		if (param_type.is_reference() || param_type.is_rvalue_reference()) {
			if (auto binding = buildCallArgReferenceBinding(arg, param_type, context_description)) {
				ref_bindings[i] = *binding;
			}
			continue;
		}

		tryAnnotateSingleArgConversion(arg, param_type, context_description);
	}
}

bool SemanticAnalysis::tryRecoverCallDeclFromStructMembers(const CallInfo& call_info,
														   const DeclarationNode& decl,
														   const ChunkedVector<ASTNode>& arguments,
														   const FunctionDeclarationNode*& func_decl) {
	auto searchStructMembers = [&](const StructTypeInfo* si) -> bool {
		if (!si)
			return false;
		const std::string_view func_name = decl.identifier_token().value();
		for (const auto& mf : si->member_functions) {
			if (!mf.function_decl.has_value())
				continue;
			if (!mf.function_decl.is<FunctionDeclarationNode>())
				continue;
			const auto& candidate = mf.function_decl.as<FunctionDeclarationNode>();
			if (&candidate.decl_node() == &decl) {
				func_decl = &candidate;
				return true;
			}
		}
		if (call_info.mangled_name.isValid()) {
			const std::string_view call_mangled = call_info.mangled_name.view();
			for (const auto& mf : si->member_functions) {
				if (!mf.function_decl.has_value())
					continue;
				if (!mf.function_decl.is<FunctionDeclarationNode>())
					continue;
				const auto& candidate = mf.function_decl.as<FunctionDeclarationNode>();
				if (candidate.has_mangled_name() &&
					candidate.mangled_name() == call_mangled) {
					func_decl = &candidate;
					return true;
				}
			}
		}
		const FunctionDeclarationNode* name_match = nullptr;
		bool ambiguous = false;
		for (const auto& mf : si->member_functions) {
			if (!mf.function_decl.has_value())
				continue;
			if (!mf.function_decl.is<FunctionDeclarationNode>())
				continue;
			const auto& candidate = mf.function_decl.as<FunctionDeclarationNode>();
			if (candidate.decl_node().identifier_token().value() == func_name &&
				candidate.parameter_nodes().size() == arguments.size()) {
				if (name_match) {
					ambiguous = true;
					break;
				}
				name_match = &candidate;
			}
		}
		if (name_match && !ambiguous) {
			func_decl = name_match;
			return true;
		}
		return false;
	};

	auto searchTypeMembers = [&](const TypeInfo* type_info) -> bool {
		if (!type_info) {
			return false;
		}
		if (searchStructMembers(type_info->getStructInfo())) {
			return true;
		}
		if (type_info->isTemplateInstantiation()) {
			auto pattern_it = getTypesByNameMap().find(type_info->baseTemplateName());
			if (pattern_it != getTypesByNameMap().end() &&
				searchStructMembers(pattern_it->second->getStructInfo())) {
				return true;
			}
		}
		return false;
	};

	auto searchMemberContextHierarchy = [&]() -> bool {
		if (member_context_stack_.empty()) {
			return false;
		}

		const TypeInfo* current_type_info = tryGetTypeInfo(member_context_stack_.back());
		if (!current_type_info) {
			return false;
		}

		if (!current_type_info->getStructInfo()) {
			return false;
		}

		std::unordered_set<const StructTypeInfo*> visited;
		auto searchHierarchy = [&](auto&& recurse, const TypeInfo* type_info) -> bool {
			const StructTypeInfo* struct_info = type_info ? type_info->getStructInfo() : nullptr;
			if (!struct_info || !visited.insert(struct_info).second) {
				return false;
			}
			if (searchTypeMembers(type_info)) {
				return true;
			}
			for (const auto& base_spec : struct_info->base_classes) {
				if (!base_spec.type_index.is_valid()) {
					continue;
				}
				const TypeInfo* base_type_info = tryGetTypeInfo(base_spec.type_index);
				if (!base_type_info) {
					continue;
				}
				if (recurse(recurse, base_type_info)) {
					return true;
				}
			}
			return false;
		};

		return searchHierarchy(searchHierarchy, current_type_info);
	};

	if (!call_info.qualified_name.isValid()) {
		return searchMemberContextHierarchy();
	}

	const std::string_view qname = call_info.qualified_name.view();
	const size_t scope_sep = qname.rfind("::");
	if (scope_sep == std::string_view::npos)
		return false;

	const std::string_view struct_name_sv = qname.substr(0, scope_sep);
	const auto struct_name_handle = StringTable::getOrInternStringHandle(struct_name_sv);
	if (!member_context_stack_.empty()) {
		if (const TypeInfo* current_type_info = tryGetTypeInfo(member_context_stack_.back())) {
			if ((current_type_info->isTemplateInstantiation() &&
				 StringTable::getStringView(current_type_info->baseTemplateName()) == struct_name_sv) ||
				StringTable::getStringView(current_type_info->name()) == struct_name_sv) {
				if (searchTypeMembers(current_type_info)) {
					return true;
				}
			}
		}
	}
	auto struct_it = getTypesByNameMap().find(struct_name_handle);
	if (struct_it != getTypesByNameMap().end() &&
		searchTypeMembers(struct_it->second)) {
		return true;
	}

	for (const auto& [handle, ti] : getTypesByNameMap()) {
		if (!ti)
			continue;
		const std::string_view registered_name = handle.view();
		if (registered_name == struct_name_sv ||
			(ti->isTemplateInstantiation() &&
			 StringTable::getStringView(ti->baseTemplateName()) == struct_name_sv)) {
			if (searchTypeMembers(ti))
				return true;
		} else if (registered_name.size() > struct_name_sv.size() + 1) {
			const size_t prefix_end = registered_name.size() - struct_name_sv.size();
			if (registered_name.substr(prefix_end) == struct_name_sv &&
				(registered_name[prefix_end - 1] == ':' ||
				 registered_name[prefix_end - 1] == '<')) {
				if (searchTypeMembers(ti))
					return true;
			}
		}
		if (registered_name.size() > struct_name_sv.size() &&
			registered_name[struct_name_sv.size()] == '<' &&
			registered_name.starts_with(struct_name_sv)) {
			if (searchTypeMembers(ti))
				return true;
		}
	}

	return false;
}

const FunctionDeclarationNode* SemanticAnalysis::resolveCallArgAnnotationTarget(const CallInfo& call_info,
																				const void* call_key) {
	const ChunkedVector<ASTNode>& arguments = *call_info.arguments;
	if (call_info.has_receiver)
		return call_info.function_declaration;

	const FunctionDeclarationNode* func_decl = getResolvedOpCall(call_key);
	if (!func_decl) {
		tryResolveCallableOperatorImpl(call_info, call_key);
		func_decl = getResolvedOpCall(call_key);
	}
	if (func_decl)
		return func_decl;

	if (call_info.function_declaration)
		return call_info.function_declaration;

	const DeclarationNode& decl = *call_info.declaration;
	const std::string_view name = call_info.qualified_name.isValid()
									  ? call_info.qualified_name.view()
									  : decl.identifier_token().value();
	auto overloads = symbols_.lookup_all(name);
	if (overloads.empty() && call_info.qualified_name.isValid()) {
		overloads = symbols_.lookup_all(decl.identifier_token().value());
	}
	if (overloads.empty()) {
		if (!tryRecoverCallDeclFromStructMembers(call_info, decl, arguments, func_decl)) {
			unresolved_call_args_.insert(call_key);
			return nullptr;
		}
		return func_decl;
	}

	for (const auto& overload : overloads) {
		const FunctionDeclarationNode* candidate = getCallTargetFunctionCandidate(overload);
		if (candidate && &candidate->decl_node() == &decl) {
			func_decl = candidate;
			break;
		}
	}

	if (!func_decl) {
		auto find_by_arg_count = [&]() -> const FunctionDeclarationNode* {
			for (const auto& overload : overloads) {
				if (const FunctionDeclarationNode* candidate = getCallTargetFunctionCandidate(overload)) {
					if (arguments.size() == candidate->parameter_nodes().size()) {
						return candidate;
					}
				}
			}
			return nullptr;
		};
		if (overloads.size() == 1) {
			func_decl = getCallTargetFunctionCandidate(overloads[0]);
		} else {
			func_decl = find_by_arg_count();
		}
	}

	if (!func_decl &&
		tryRecoverCallDeclFromStructMembers(call_info, decl, arguments, func_decl)) {
		return func_decl;
	}

	return func_decl;
}

void SemanticAnalysis::tryAnnotateCallArgConversionsImpl(const CallInfo& call_info,
														 const void* call_key,
														 const char* context_description) {
	const FunctionDeclarationNode* func_decl = resolveCallArgAnnotationTarget(call_info, call_key);
	if (!func_decl) {
		resolved_direct_call_table_.erase(call_key);
		return;
	}

	const FunctionDeclarationNode* resolved_op_call = getResolvedOpCall(call_key);
	const bool cache_as_direct_call =
		!resolved_op_call &&
		(!call_info.has_receiver ||
		 (call_info.function_declaration && call_info.function_declaration->is_static()));
	if (cache_as_direct_call) {
		resolved_direct_call_table_[call_key] = func_decl;
	} else {
		resolved_direct_call_table_.erase(call_key);
	}
	unresolved_call_args_.erase(call_key);

	annotateResolvedCallArgConversions(call_key, *call_info.arguments, *func_decl, context_description);
}


void SemanticAnalysis::tryAnnotateCallArgConversions(const CallExprNode& call_node) {
	tryAnnotateCallArgConversionsImpl(CallInfo::from(call_node), &call_node, " in call argument");
}


void SemanticAnalysis::tryAnnotateConstructorCallArgConversions(const ConstructorCallNode& call_node) {
	// Get the type being constructed.
	const ASTNode& type_node = call_node.type_node();
	if (!type_node.has_value() || !type_node.is<TypeSpecifierNode>())
		return;
	const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
	if (type_spec.category() != TypeCategory::Struct)
		return;
	const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index());
	if (!type_info)
		return;

	const StructTypeInfo* struct_info = type_info->getStructInfo();
	if (!struct_info || !struct_info->hasAnyConstructor())
		return;

	auto buildConstructorDiagnostic = [&](std::string_view prefix, size_t arg_count) {
		return std::string(
			StringBuilder()
				.append(prefix)
				.append(" for '")
				.append(StringTable::getStringView(type_info->name()))
				.append("' with ")
				.append(arg_count)
				.append(" argument(s)")
				.commit());
	};
	const bool require_constructor_match =
		struct_info->hasUserDefinedConstructor() &&
		call_node.called_from().value() == std::string_view{"{"};

	// Resolve the matching constructor via overload resolution.
	const auto& arguments = call_node.arguments();
	size_t num_args = 0;
	arguments.visit([&](ASTNode) { num_args++; });

	std::vector<TypeSpecifierNode> arg_types;
	std::vector<CanonicalTypeId> inferred_arg_type_ids;
	arg_types.reserve(num_args);
	inferred_arg_type_ids.reserve(num_args);
	arguments.visit([&](ASTNode arg) {
		// Classification: constructor-overload bridge. Prefer sema-owned argument
		// inference first; keep parser fallback only for the remaining lookup facts
		// that sema does not yet mirror locally.
		CanonicalTypeId inferred_arg_type_id{};
		auto arg_type_opt = buildOverloadResolutionArgType(arg, &inferred_arg_type_id);
		if (!arg_type_opt.has_value()) {
			arg_types.clear();
			inferred_arg_type_ids.clear();
			return;
		}
		arg_types.push_back(std::move(*arg_type_opt));
		inferred_arg_type_ids.push_back(inferred_arg_type_id);
	});
	if (arg_types.size() != num_args)
		return;

	// skip_implicit=true: avoid false ambiguity between an explicit copy/move
	// ctor and a compiler-generated implicit one with the same signature.
	auto resolution = resolve_constructor_overload(*struct_info, arg_types, true);
	if (resolution.is_ambiguous && require_constructor_match)
		throw CompileError(buildConstructorDiagnostic("Ambiguous constructor call", num_args));
	if (!resolution.selected_overload) {
		if (require_constructor_match)
			throw CompileError(buildConstructorDiagnostic("No matching constructor", num_args));
		return;
	}
	call_node.set_resolved_constructor(resolution.selected_overload);

	const auto& ctor_params = resolution.selected_overload->parameter_nodes();
	if (num_args > ctor_params.size())
		return;

	// Annotate each argument where its type differs from the parameter type.
	size_t i = 0;
	arguments.visit([&](ASTNode arg) {
		if (i >= ctor_params.size()) {
			++i;
			return;
		}
		if (!arg.is<ExpressionNode>()) {
			++i;
			return;
		}

		const ASTNode& param_node = ctor_params[i];
		if (!param_node.is<DeclarationNode>()) {
			++i;
			return;
		}
		const ASTNode param_type_node = param_node.as<DeclarationNode>().type_node();
		if (!param_type_node.has_value() || !param_type_node.is<TypeSpecifierNode>()) {
			++i;
			return;
		}

		const CanonicalTypeId param_type_id = canonicalizeType(param_type_node.as<TypeSpecifierNode>());
		const CanonicalTypeId arg_type_id = inferred_arg_type_ids[i];
		if (arg_type_id && canonical_types_match(arg_type_id, param_type_id)) {
			++i;
			return;
		}
		tryAnnotateConversion(arg, param_type_id, arg_type_id);
		diagnoseScopedEnumConversion(arg, param_type_id, " in constructor argument", arg_type_id);
		++i;
	});
}

void SemanticAnalysis::tryAnnotateInitListConstructorArgs(
	const InitializerListNode& init_list, const StructTypeInfo& struct_info) {
	const auto& initializers = init_list.initializers();
	if (initializers.empty())
		return;

	// Build argument types for overload resolution.
	// Mirror the codegen path: call adjust_argument_type_for_overload_resolution
	// so that lvalue arguments prefer reference overloads, and use skip_implicit=true
	// to avoid false ambiguity between explicit and implicit copy/move ctors.
	std::vector<TypeSpecifierNode> arg_types;
	std::vector<CanonicalTypeId> inferred_arg_type_ids;
	arg_types.reserve(initializers.size());
	inferred_arg_type_ids.reserve(initializers.size());
	for (size_t arg_idx = 0; arg_idx < initializers.size(); ++arg_idx) {
		const ASTNode& arg = initializers[arg_idx];
		// Classification: constructor-overload bridge for braced initialization.
		// This now shares the same sema-first argument typing path as ordinary
		// constructor calls.
		CanonicalTypeId inferred_arg_type_id{};
		auto arg_type_opt = buildOverloadResolutionArgType(arg, &inferred_arg_type_id);
		if (!arg_type_opt.has_value()) {
			return;
		}
		arg_types.push_back(std::move(*arg_type_opt));
		inferred_arg_type_ids.push_back(inferred_arg_type_id);
	}

	auto resolution = resolve_constructor_overload(struct_info, arg_types, true);
	if (!resolution.selected_overload) {
		// No constructor matched — restore the old scoped-enum diagnostic.
		// If any argument is a scoped enum that couldn't be implicitly converted,
		// find the closest ctor by arity and diagnose the bad arg against its
		// parameter type so the user gets a clear "use static_cast" message.
		auto arity_res = resolve_constructor_overload_arity(struct_info, initializers.size(), true);
		const ConstructorDeclarationNode* closest_ctor = arity_res.selected_overload;
		for (size_t i = 0; i < initializers.size(); ++i) {
			const ASTNode& arg = initializers[i];
			if (!arg.is<ExpressionNode>())
				continue;
			const CanonicalTypeId arg_type_id = inferred_arg_type_ids[i];
			if (!arg_type_id)
				continue;
			const CanonicalTypeDesc& arg_desc = type_context_.get(arg_type_id);
			if (arg_desc.category() != TypeCategory::Enum)
				continue;
			const TypeInfo* arg_type_info = tryGetTypeInfo(arg_desc.type_index);
			if (!arg_type_info)
				continue;
			const EnumTypeInfo* ei = arg_type_info->getEnumInfo();
			if (!ei || !ei->is_scoped)
				continue;
			// Found a scoped enum arg that caused the no-match.
			// Use the parameter type from the closest arity match for a precise error message.
			if (closest_ctor) {
				const auto& params = closest_ctor->parameter_nodes();
				if (i < params.size() && params[i].is<DeclarationNode>()) {
					const ASTNode& param_type_node = params[i].as<DeclarationNode>().type_node();
					if (param_type_node.is<TypeSpecifierNode>()) {
						const CanonicalTypeId param_type_id = canonicalizeType(param_type_node.as<TypeSpecifierNode>());
						diagnoseScopedEnumConversion(arg, param_type_id, " in constructor argument", arg_type_id);
					}
				}
			} else {
				// No arity match at all; diagnose against the first constructor parameter we can find.
				throw CompileError("cannot implicitly convert from scoped enum '" +
								   std::string(StringTable::getStringView(ei->name)) +
								   "' in constructor argument; use static_cast");
			}
		}
		return;
	}

	init_list.set_resolved_constructor(resolution.selected_overload);

	const auto& ctor_params = resolution.selected_overload->parameter_nodes();

	for (size_t i = 0; i < initializers.size() && i < ctor_params.size(); ++i) {
		const ASTNode& arg = initializers[i];
		if (!arg.is<ExpressionNode>())
			continue;

		const ASTNode& param_node = ctor_params[i];
		if (!param_node.is<DeclarationNode>())
			continue;
		const ASTNode param_type_node = param_node.as<DeclarationNode>().type_node();
		if (!param_type_node.has_value() || !param_type_node.is<TypeSpecifierNode>())
			continue;

		const CanonicalTypeId param_type_id = canonicalizeType(param_type_node.as<TypeSpecifierNode>());
		const CanonicalTypeId arg_type_id = inferred_arg_type_ids[i];
		FLASH_LOG(General, Debug, "SemanticAnalysis: init-list arg param_type=", param_type_id.value, " arg_type=", arg_type_id.value, " match=", (arg_type_id && canonical_types_match(arg_type_id, param_type_id)));
		if (arg_type_id && canonical_types_match(arg_type_id, param_type_id))
			continue;
		tryAnnotateConversion(arg, param_type_id, arg_type_id);
		diagnoseScopedEnumConversion(arg, param_type_id, " in constructor argument", arg_type_id);
	}
}

void SemanticAnalysis::tryAnnotateTernaryBranchConversions(const TernaryOperatorNode& ternary_node) {
	// C++20 [expr.cond]/7: if the second and third operands have different
	// arithmetic types, the usual arithmetic conversions are applied.
	const CanonicalTypeId true_type_id = inferExpressionType(ternary_node.true_expr());
	const CanonicalTypeId false_type_id = inferExpressionType(ternary_node.false_expr());
	if (!true_type_id || !false_type_id)
		return;
	if (canonical_types_match(true_type_id, false_type_id))
		return;

	const auto& true_desc = type_context_.get(true_type_id);
	const auto& false_desc = type_context_.get(false_type_id);

	// Only handle primitive arithmetic types (not structs, pointers, etc.)
	if (true_desc.category() == TypeCategory::Struct || false_desc.category() == TypeCategory::Struct)
		return;
	if (!true_desc.pointer_levels.empty() || !false_desc.pointer_levels.empty())
		return;
	// C++20 [expr.cond]/2: when one branch is a throw-expression (or delete-expression),
	// its type is void. The result type is the other branch's type — no conversion needed.
	if (true_desc.category() == TypeCategory::Void || false_desc.category() == TypeCategory::Void)
		return;

	TypeCategory common = get_common_type(true_desc.category(), false_desc.category());
	CanonicalTypeDesc common_desc;
	common_desc.type_index = nativeTypeIndex(common);
	CanonicalTypeId common_type_id = type_context_.intern(common_desc);

	// Annotate each branch if it needs conversion to the common type.
	if (!canonical_types_match(true_type_id, common_type_id))
		tryAnnotateConversion(ternary_node.true_expr(), common_type_id);
	if (!canonical_types_match(false_type_id, common_type_id))
		tryAnnotateConversion(ternary_node.false_expr(), common_type_id);
}
