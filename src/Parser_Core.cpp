#include "Parser.h"
#include "OverloadResolution.h"
#include "TemplateRegistry.h"
#include "ConstExprEvaluator.h"
#include "NameMangling.h"
#include "TemplateProfilingStats.h"
#include "ExpressionSubstitutor.h"
#include "TypeTraitEvaluator.h"
#include "LazyMemberResolver.h"
#include "InstantiationQueue.h"
#include "RebindStaticMemberAst.h"
#include "SemanticAnalysis.h"
#include <atomic> // Include atomic for constrained partial specialization counter
#include <string_view> // Include string_view header
#include <unordered_set> // Include unordered_set header
#include <ranges> // Include ranges for std::ranges::find
#include <array> // Include array for std::array
#include <charconv> // Include charconv for std::from_chars
#include <cctype>
#include "StringBuilder.h"
#include "Log.h"

// Break into the debugger only on Windows
#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX	 // Prevent Windows.h from defining min/max macros
#endif
#include <windows.h>
#define DEBUG_BREAK()          \
	if (IsDebuggerPresent()) { \
		DebugBreak();          \
	}
#else
	// On non-Windows platforms, define as a no-op (does nothing)
#define DEBUG_BREAK() ((void)0)
#endif

// Define the global symbol table (declared as extern in SymbolTable.h)
SymbolTable gSymbolTable;
ChunkedStringAllocator gChunkedStringAllocator;
// Global registries
TemplateRegistry gTemplateRegistry;
ConceptRegistry gConceptRegistry;
const std::unordered_set<std::string_view> type_keywords = {
	"int", "float", "double", "char", "bool", "void",
	"short", "long", "signed", "unsigned", "const", "volatile", "alignas",
	"auto", "wchar_t", "char8_t", "char16_t", "char32_t", "decltype",
	"__int8", "__int16", "__int32", "__int64"};

MemberSizeAndAlignment calculateMemberSizeAndAlignment(const TypeSpecifierNode& type_spec) {
	MemberSizeAndAlignment result;

	// For pointers, references, and function pointers, size and alignment are always sizeof(void*)
	if (type_spec.is_pointer() || type_spec.is_reference() || type_spec.is_function_pointer()) {
		result.size = sizeof(void*);
		result.alignment = sizeof(void*);
	} else {
		result.size = get_type_size_bits(type_spec.type()) / 8;
		result.alignment = get_type_alignment(type_spec.type(), result.size);
	}

	return result;
}

const StructTypeInfo* tryGetStructTypeInfo(TypeIndex type_index) {
	if (const TypeInfo* type_info = tryGetTypeInfo(type_index)) {
		if (const StructTypeInfo* struct_info = type_info->getStructInfo()) {
			return struct_info;
		}
		if (type_info->isTypeAlias()) {
			ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(type_index);
			if (resolved_alias.type_index.index() != type_index.index()) {
				if (const TypeInfo* resolved_type_info = tryGetTypeInfo(resolved_alias.type_index)) {
					return resolved_type_info->getStructInfo();
				}
			}
		}
		std::string_view type_name = StringTable::getStringView(type_info->name());
		size_t sep_pos = type_name.rfind("::");
		if (sep_pos != std::string_view::npos) {
			std::string_view parent_name = type_name.substr(0, sep_pos);
			std::string_view nested_name = type_name.substr(sep_pos + 2);
			std::string_view base_template_name = extractBaseTemplateName(parent_name);
			if (!base_template_name.empty()) {
				const StructTypeInfo* resolved_struct_info = nullptr;
				for (const auto& [candidate_name, candidate_type_info] : getTypesByNameMap()) {
					if (!candidate_type_info || !candidate_type_info->getStructInfo()) {
						continue;
					}
					std::string_view candidate_view = StringTable::getStringView(candidate_name);
					size_t candidate_sep = candidate_view.rfind("::");
					if (candidate_sep == std::string_view::npos) {
						continue;
					}
					std::string_view candidate_parent = candidate_view.substr(0, candidate_sep);
					std::string_view candidate_nested = candidate_view.substr(candidate_sep + 2);
					if (candidate_nested != nested_name) {
						continue;
					}
					if (extractBaseTemplateName(candidate_parent) != base_template_name) {
						continue;
					}
					if (candidate_parent.find('$') == std::string_view::npos) {
						continue;
					}
					if (resolved_struct_info != nullptr) {
						FLASH_LOG(Types, Warning,
								  "Ambiguous nested instantiated struct lookup for alias-remapped type '",
								  type_name, "' while matching nested name '", nested_name,
								  "' under base template '", base_template_name, "'");
						return nullptr;
					}
					resolved_struct_info = candidate_type_info->getStructInfo();
				}
				if (resolved_struct_info) {
					return resolved_struct_info;
				}
			}
		}
	}
	return nullptr;
}

size_t getResolvedTypeSizeBytes(const TypeSpecifierNode& type_spec, TypeIndex resolved_type_index) {
	if (const TypeInfo* type_info = tryGetTypeInfo(resolved_type_index)) {
		if (type_info->hasStoredSize()) {
			return toSizeT(type_info->sizeInBytes());
		}
	}
	TypeCategory resolved_category = resolved_type_index.category();
	if (resolved_category == TypeCategory::Invalid) {
		resolved_category = type_spec.type();
	}
	return get_type_size_bits(resolved_category) / 8;
}

MemberSizeAndAlignment calculateResolvedMemberSizeAndAlignment(const TypeSpecifierNode& type_spec, TypeIndex resolved_type_index) {
	if (type_spec.is_pointer() || type_spec.is_reference() || type_spec.is_rvalue_reference() || type_spec.is_function_pointer()) {
		return MemberSizeAndAlignment{sizeof(void*), sizeof(void*)};
	}

	size_t size = getResolvedTypeSizeBytes(type_spec, resolved_type_index);
	TypeCategory resolved_category = resolved_type_index.category();
	if (resolved_category == TypeCategory::Invalid) {
		resolved_category = type_spec.type();
	}

	size_t alignment = get_type_alignment(resolved_category, size);
	if (const StructTypeInfo* struct_info = tryGetStructTypeInfo(resolved_type_index)) {
		alignment = struct_info->alignment;
	}

	return MemberSizeAndAlignment{size, alignment};
}

// Helper function to safely get type size from TemplateTypeArg
int getTypeSizeFromTemplateArgument(const TemplateTypeArg& arg) {
	// Check if this is a builtin type that get_type_size_bits can handle
	if (is_builtin_type(arg.category())) {
		return static_cast<size_t>(get_type_size_bits(arg.category()));
	}
	// For UserDefined and other types, use type_index for direct O(1) lookup
	if (const TypeInfo* type_info = tryGetTypeInfo(arg.type_index)) {
		if (type_info->hasStoredSize()) {
			return static_cast<size_t>(type_info->sizeInBits().value);
		}
	}
	return 0;  // Will be resolved during member access
}

// Helper to convert a std::vector of template arguments into an InlineVector so
// the existing substitution helpers can reuse the current argument list without
// re-implementing the conversion at each call site.
InlineVector<TemplateTypeArg, 4> toInlineTemplateArgs(const std::vector<TemplateTypeArg>& template_args) {
	InlineVector<TemplateTypeArg, 4> result;
	for (const auto& arg : template_args) {
		result.push_back(arg);
	}
	return result;
}

// Helper to convert TemplateTypeArg vector to TypeInfo::TemplateArgInfo vector
// This enables storing template instantiation metadata in TypeInfo for O(1) lookup
InlineVector<TypeInfo::TemplateArgInfo, 4> convertToTemplateArgInfo(const std::vector<TemplateTypeArg>& template_args) {
	InlineVector<TypeInfo::TemplateArgInfo, 4> result;
	for (const auto& arg : template_args) {
		TypeInfo::TemplateArgInfo info;
		info.type_index = arg.type_index;  // carries both index and TypeCategory
		info.pointer_depth = arg.pointer_depth;
		info.pointer_cv_qualifiers = arg.pointer_cv_qualifiers;
		info.ref_qualifier = arg.ref_qualifier;
		info.cv_qualifier = arg.cv_qualifier;
		info.is_array = arg.is_array;
		info.array_size = arg.array_size();
		info.value = arg.value;
		info.is_value = arg.is_value;
		info.dependent_name = arg.dependent_name;
		info.function_signature = arg.function_signature;
		info.dependent_expr = arg.dependent_expr;
		result.push_back(info);
	}
	return result;
}

// Helper to check if a type name is a dependent template placeholder.
// This now relies on canonical TypeInfo metadata instead of reconstructing from strings.
// Returns: {is_dependent, base_template_name}
std::pair<bool, std::string_view> isDependentTemplatePlaceholder(std::string_view type_name) {
	// First try TypeInfo-based detection (O(1), preferred)
	auto type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(type_name));
	if (type_it != getTypesByNameMap().end()) {
		const TypeInfo* type_info = type_it->second;
		if (type_info->isTemplateInstantiation()) {
			return {true, StringTable::getStringView(type_info->baseTemplateName())};
		}
	}

	return {false, {}};
}

// Split a qualified namespace string ("a::b::c") into components for mangling.
std::vector<std::string_view> splitQualifiedNamespace(std::string_view qualified_namespace) {
	std::vector<std::string_view> components;
	if (qualified_namespace.empty()) {
		return components;
	}
	size_t start = 0;
	while (true) {
		size_t pos = qualified_namespace.find("::", start);
		if (pos == std::string_view::npos) {
			components.push_back(qualified_namespace.substr(start));
			break;
		}
		components.push_back(qualified_namespace.substr(start, pos - start));
		start = pos + 2;
	}
	return components;
}

// Re-parse an alias target from the current token position and capture the base
// template-id as unevaluated AST nodes for deferred alias materialization.
// - out_target_template_name receives the qualified template name on success.
//   A leading global `::` is skipped because registry lookups normalize names
//   without that prefix.
// - out_target_template_arg_nodes receives the parsed template argument AST nodes.
// - consume_dependent_member_suffix controls whether trailing dependent members
//   like `::type` are consumed after the base template-id is captured.
// Returns true only when a full template-id was captured successfully.
bool Parser::parseDeferredAliasTargetTemplateId(
	StringHandle& out_target_template_name,
	std::vector<ASTNode>& out_target_template_arg_nodes,
	bool consume_dependent_member_suffix) {
	out_target_template_name = StringHandle{};
	out_target_template_arg_nodes.clear();

	SaveHandle start_pos = save_token_position();
	StringBuilder target_name_builder;
	auto restore_on_failure = [&]() {
		restore_token_position(start_pos);
		discard_saved_token(start_pos);
		target_name_builder.reset();
		out_target_template_name = StringHandle{};
		out_target_template_arg_nodes.clear();
		return false;
	};

	parse_cv_qualifiers();
	skip_noop_gnu_qualifiers();
	if (peek() == "typename"_tok) {
		advance();
		parse_cv_qualifiers();
		skip_noop_gnu_qualifiers();
	}

	// Accept either `Name<...>` or `::qualified::Name<...>` spellings here.
	// A leading `::` is validated again below because it must still be followed
	// by an identifier for this to form a template-id we can capture.
	if (!(peek().is_identifier() || peek() == "::"_tok)) {
		return restore_on_failure();
	}

	if (peek() == "::"_tok) {
		advance();
	}
	if (!peek().is_identifier()) {
		return restore_on_failure();
	}

	target_name_builder.append(peek_info().value());
	advance();

	while (peek() == "::"_tok) {
		advance();
		if (peek() == "template"_tok) {
			advance();
		}
		if (!peek().is_identifier()) {
			break;
		}
		target_name_builder.append("::"sv).append(peek_info().value());
		advance();
	}

	if (peek() != "<"_tok) {
		return restore_on_failure();
	}

	out_target_template_name = StringTable::getOrInternStringHandle(target_name_builder.commit());
	auto parsed_template_args = parse_explicit_template_arguments(&out_target_template_arg_nodes);
	if (!parsed_template_args.has_value()) {
		return restore_on_failure();
	}

	if (consume_dependent_member_suffix) {
		while (peek() == "::"_tok) {
			advance();
			if (peek() == "template"_tok) {
				advance();
			}
			if (peek_info().type() != Token::Type::Identifier) {
				break;
			}
			advance();
			if (peek() == "<"_tok) {
				auto ignored_member_args = parse_explicit_template_arguments();
				if (!ignored_member_args.has_value()) {
					break;
				}
			}
		}
	}

	discard_saved_token(start_pos);
	return true;
}

// Deduce and append a best-effort argument type for function-call overload
// resolution. When arg_types_out is null, this becomes a no-op so the same
// argument-collection path can be reused for calls that do not need type data.
void Parser::appendFunctionCallArgType(const ASTNode& arg_node, std::vector<TypeSpecifierNode>* arg_types_out) {
	if (arg_types_out == nullptr || !arg_node.is<ExpressionNode>()) {
		return;
	}

	const auto& expr = arg_node.as<ExpressionNode>();
	TypeSpecifierNode arg_type(TypeCategory::Int, TypeQualifier::None, get_type_size_bits(TypeCategory::Int), Token(), CVQualifier::None);

	std::visit([&](const auto& inner) {
		using T = std::decay_t<decltype(inner)>;
		if constexpr (std::is_same_v<T, BoolLiteralNode>) {
			arg_type = TypeSpecifierNode(TypeCategory::Bool, TypeQualifier::None, get_type_size_bits(TypeCategory::Bool), Token(), CVQualifier::None);
		} else if constexpr (std::is_same_v<T, NumericLiteralNode>) {
			arg_type = TypeSpecifierNode(inner.type(), TypeQualifier::None, get_type_size_bits(inner.type()), Token(), CVQualifier::None);
		} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
			arg_type = TypeSpecifierNode(TypeCategory::Char, TypeQualifier::None, get_type_size_bits(TypeCategory::Char), Token(), CVQualifier::None);
		} else if constexpr (std::is_same_v<T, IdentifierNode>) {
			auto id_type = lookup_symbol(StringTable::getOrInternStringHandle(inner.name()));
			if (id_type.has_value()) {
				if (const DeclarationNode* decl = get_decl_from_symbol(*id_type)) {
					if (decl->type_node().template is<TypeSpecifierNode>()) {
						arg_type = decl->type_node().template as<TypeSpecifierNode>();
						return;
					}
				}
			}
			arg_type = TypeSpecifierNode(TypeCategory::Invalid, TypeQualifier::None, 0, Token(), CVQualifier::None);
		} else if constexpr (std::is_same_v<T, StaticCastNode> ||
							 std::is_same_v<T, ConstCastNode> ||
							 std::is_same_v<T, ReinterpretCastNode> ||
							 std::is_same_v<T, DynamicCastNode>) {
			// The target type of a cast is directly available and exact.
			arg_type = inner.target_type();
		} else if constexpr (std::is_same_v<T, CallExprNode>) {
			// Use the return type of the resolved callee function.
			if (const FunctionDeclarationNode* func_decl = inner.callee().function_declaration_or_null()) {
				const ASTNode& ret_node = func_decl->decl_node().type_node();
				if (ret_node.template is<TypeSpecifierNode>()) {
					arg_type = ret_node.template as<TypeSpecifierNode>();
					return;
				}
			}
			arg_type = TypeSpecifierNode(TypeCategory::Invalid, TypeQualifier::None, 0, Token(), CVQualifier::None);
		} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
			// For logical-not, deduce Bool; for arithmetic/bitwise unary ops, recurse on the
			// operand; for dereferencing or address-of, fall through to the Invalid default.
			if (inner.op() == "!") {
				arg_type = TypeSpecifierNode(TypeCategory::Bool, TypeQualifier::None, get_type_size_bits(TypeCategory::Bool), Token(), CVQualifier::None);
			} else if (inner.op() != "*" && inner.op() != "&") {
				// For +, -, ~, prefix ++/--, post ++/--, the result type matches the operand.
				std::vector<TypeSpecifierNode> operand_types;
				appendFunctionCallArgType(inner.get_operand(), &operand_types);
				if (!operand_types.empty()) {
					arg_type = operand_types.front();
					return;
				}
				arg_type = TypeSpecifierNode(TypeCategory::Invalid, TypeQualifier::None, 0, Token(), CVQualifier::None);
			} else {
				arg_type = TypeSpecifierNode(TypeCategory::Invalid, TypeQualifier::None, 0, Token(), CVQualifier::None);
			}
		}
	},
			   expr);

	arg_types_out->push_back(arg_type);
}

// Helper function to find all local variable declarations in an AST node
void findLocalVariableDeclarations(const ASTNode& node, std::unordered_set<StringHandle>& var_names) {
	RebindStaticMemberAst::visitAST(node, [&var_names](const ASTNode& current) {
		if (!current.is<VariableDeclarationNode>()) {
			return;
		}

		const auto& decl = current.as<VariableDeclarationNode>().declaration();
		var_names.insert(decl.identifier_token().handle());
	});
}

// Helper function to find capture candidates and detect implicit [this] usage in lambdas
void collectLambdaCaptureCandidates(const ASTNode& node,
									std::unordered_set<StringHandle>& capture_candidates,
									bool& uses_implicit_this_capture) {
	if (node.is<IdentifierNode>()) {
		const auto& identifier = node.as<IdentifierNode>();
		if (identifier.name() == "this"sv || identifier.binding() == IdentifierBinding::NonStaticMember) {
			uses_implicit_this_capture = true;
			return;
		}
		if (identifier.binding() == IdentifierBinding::Local ||
			identifier.binding() == IdentifierBinding::Unresolved) {
			capture_candidates.insert(identifier.nameHandle());
		}
		return;
	} else if (node.is<ExpressionNode>()) {
		const auto& expr = node.as<ExpressionNode>();
		std::visit([&](const auto& inner_node) {
			using T = std::decay_t<decltype(inner_node)>;
			if constexpr (std::is_same_v<T, IdentifierNode>) {
				if (inner_node.name() == "this"sv || inner_node.binding() == IdentifierBinding::NonStaticMember) {
					uses_implicit_this_capture = true;
				} else if (inner_node.binding() == IdentifierBinding::Local ||
						   inner_node.binding() == IdentifierBinding::Unresolved) {
					capture_candidates.insert(inner_node.nameHandle());
				}
			} else if constexpr (std::is_same_v<T, LambdaExpressionNode>) {
				return; // Don't descend into nested lambdas
			} else if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
				collectLambdaCaptureCandidates(inner_node.get_lhs(), capture_candidates, uses_implicit_this_capture);
				collectLambdaCaptureCandidates(inner_node.get_rhs(), capture_candidates, uses_implicit_this_capture);
			} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
				collectLambdaCaptureCandidates(inner_node.get_operand(), capture_candidates, uses_implicit_this_capture);
			} else if constexpr (std::is_same_v<T, TernaryOperatorNode>) {
				collectLambdaCaptureCandidates(inner_node.condition(), capture_candidates, uses_implicit_this_capture);
				collectLambdaCaptureCandidates(inner_node.true_expr(), capture_candidates, uses_implicit_this_capture);
				collectLambdaCaptureCandidates(inner_node.false_expr(), capture_candidates, uses_implicit_this_capture);
			} else if constexpr (std::is_same_v<T, CallExprNode>) {
				if (!inner_node.has_receiver()) {
					if (const FunctionDeclarationNode* func_decl = inner_node.callee().function_declaration_or_null();
						func_decl && !func_decl->is_static() && !func_decl->parent_struct_name().empty()) {
						uses_implicit_this_capture = true;
					}
				} else {
					collectLambdaCaptureCandidates(inner_node.receiver(), capture_candidates, uses_implicit_this_capture);
				}
				for (const auto& argument : inner_node.arguments()) {
					collectLambdaCaptureCandidates(argument, capture_candidates, uses_implicit_this_capture);
				}
				for (const auto& template_arg : inner_node.template_arguments()) {
					collectLambdaCaptureCandidates(template_arg, capture_candidates, uses_implicit_this_capture);
				}
			} else if constexpr (std::is_same_v<T, ConstructorCallNode>) {
				for (const auto& argument : inner_node.arguments()) {
					collectLambdaCaptureCandidates(argument, capture_candidates, uses_implicit_this_capture);
				}
			} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
				collectLambdaCaptureCandidates(inner_node.object(), capture_candidates, uses_implicit_this_capture);
			} else if constexpr (std::is_same_v<T, PointerToMemberAccessNode>) {
				collectLambdaCaptureCandidates(inner_node.object(), capture_candidates, uses_implicit_this_capture);
				collectLambdaCaptureCandidates(inner_node.member_pointer(), capture_candidates, uses_implicit_this_capture);
			} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
				collectLambdaCaptureCandidates(inner_node.array_expr(), capture_candidates, uses_implicit_this_capture);
				collectLambdaCaptureCandidates(inner_node.index_expr(), capture_candidates, uses_implicit_this_capture);
			} else if constexpr (std::is_same_v<T, StaticCastNode> ||
								 std::is_same_v<T, ConstCastNode> ||
								 std::is_same_v<T, ReinterpretCastNode> ||
								 std::is_same_v<T, DynamicCastNode>) {
				collectLambdaCaptureCandidates(inner_node.expr(), capture_candidates, uses_implicit_this_capture);
			}
		},
				   expr);
	} else if (node.is<BinaryOperatorNode>()) {
		const auto& binop = node.as<BinaryOperatorNode>();
		collectLambdaCaptureCandidates(binop.get_lhs(), capture_candidates, uses_implicit_this_capture);
		collectLambdaCaptureCandidates(binop.get_rhs(), capture_candidates, uses_implicit_this_capture);
	} else if (node.is<UnaryOperatorNode>()) {
		const auto& unop = node.as<UnaryOperatorNode>();
		collectLambdaCaptureCandidates(unop.get_operand(), capture_candidates, uses_implicit_this_capture);
	} else if (node.is<CallExprNode>()) {
		const auto& call = node.as<CallExprNode>();
		if (!call.has_receiver()) {
			if (const FunctionDeclarationNode* func_decl = call.callee().function_declaration_or_null();
				func_decl && !func_decl->is_static() && !func_decl->parent_struct_name().empty()) {
				uses_implicit_this_capture = true;
			}
		} else {
			collectLambdaCaptureCandidates(call.receiver(), capture_candidates, uses_implicit_this_capture);
		}
		for (const auto& argument : call.arguments()) {
			collectLambdaCaptureCandidates(argument, capture_candidates, uses_implicit_this_capture);
		}
		for (const auto& template_arg : call.template_arguments()) {
			collectLambdaCaptureCandidates(template_arg, capture_candidates, uses_implicit_this_capture);
		}
	} else if (node.is<ReturnStatementNode>()) {
		const auto& ret = node.as<ReturnStatementNode>();
		if (ret.expression().has_value()) {
			collectLambdaCaptureCandidates(*ret.expression(), capture_candidates, uses_implicit_this_capture);
		}
	} else if (node.is<BlockNode>()) {
		const auto& block = node.as<BlockNode>();
		const auto& stmts = block.get_statements();
		for (size_t i = 0; i < stmts.size(); ++i) {
			collectLambdaCaptureCandidates(stmts[i], capture_candidates, uses_implicit_this_capture);
		}
	} else if (node.is<IfStatementNode>()) {
		const auto& if_stmt = node.as<IfStatementNode>();
		collectLambdaCaptureCandidates(if_stmt.get_condition(), capture_candidates, uses_implicit_this_capture);
		collectLambdaCaptureCandidates(if_stmt.get_then_statement(), capture_candidates, uses_implicit_this_capture);
		if (if_stmt.get_else_statement().has_value()) {
			collectLambdaCaptureCandidates(*if_stmt.get_else_statement(), capture_candidates, uses_implicit_this_capture);
		}
	} else if (node.is<WhileStatementNode>()) {
		const auto& while_stmt = node.as<WhileStatementNode>();
		collectLambdaCaptureCandidates(while_stmt.get_condition(), capture_candidates, uses_implicit_this_capture);
		collectLambdaCaptureCandidates(while_stmt.get_body_statement(), capture_candidates, uses_implicit_this_capture);
	} else if (node.is<DoWhileStatementNode>()) {
		const auto& do_while = node.as<DoWhileStatementNode>();
		collectLambdaCaptureCandidates(do_while.get_body_statement(), capture_candidates, uses_implicit_this_capture);
		collectLambdaCaptureCandidates(do_while.get_condition(), capture_candidates, uses_implicit_this_capture);
	} else if (node.is<ForStatementNode>()) {
		const auto& for_stmt = node.as<ForStatementNode>();
		if (for_stmt.get_init_statement().has_value()) {
			collectLambdaCaptureCandidates(*for_stmt.get_init_statement(), capture_candidates, uses_implicit_this_capture);
		}
		if (for_stmt.get_condition().has_value()) {
			collectLambdaCaptureCandidates(*for_stmt.get_condition(), capture_candidates, uses_implicit_this_capture);
		}
		if (for_stmt.get_update_expression().has_value()) {
			collectLambdaCaptureCandidates(*for_stmt.get_update_expression(), capture_candidates, uses_implicit_this_capture);
		}
		collectLambdaCaptureCandidates(for_stmt.get_body_statement(), capture_candidates, uses_implicit_this_capture);
	} else if (node.is<MemberAccessNode>()) {
		const auto& member = node.as<MemberAccessNode>();
		collectLambdaCaptureCandidates(member.object(), capture_candidates, uses_implicit_this_capture);
	} else if (node.is<ArraySubscriptNode>()) {
		const auto& subscript = node.as<ArraySubscriptNode>();
		collectLambdaCaptureCandidates(subscript.array_expr(), capture_candidates, uses_implicit_this_capture);
		collectLambdaCaptureCandidates(subscript.index_expr(), capture_candidates, uses_implicit_this_capture);
	} else if (node.is<VariableDeclarationNode>()) {
		const auto& var_decl = node.as<VariableDeclarationNode>();
		if (var_decl.initializer().has_value()) {
			collectLambdaCaptureCandidates(*var_decl.initializer(), capture_candidates, uses_implicit_this_capture);
		}
	}
}

Parser::Parser(Lexer& lexer, CompileContext& context)
	: lexer_(lexer), context_(context), current_token_(lexer_.next_token()) {
	initialize_native_types();
	ast_nodes_.reserve(default_ast_tree_size_);
}

void Parser::setActiveSemanticAnalysis(SemanticAnalysis* sema) {
	active_sema_ = sema;
}

SemanticAnalysis* Parser::getActiveSemanticAnalysis() const {
	return active_sema_;
}

void Parser::normalizePendingSemanticRootsIfAvailable() {
	if (active_sema_ != nullptr) {
		active_sema_->normalizePendingSemanticRoots();
	}
}

int Parser::getStructTypeSizeBits(TypeIndex type_index) const {
	if (const TypeInfo* type_info = tryGetTypeInfo(type_index)) {
		if (type_info->hasStoredSize()) {
			return type_info->sizeInBits().value;
		}
	}

	return 0;
}

Parser::ScopedTokenPosition::ScopedTokenPosition(class Parser& parser, const std::source_location location)
	: parser_(parser), saved_handle_(parser.save_token_position()), location_(location) {}

Parser::ScopedTokenPosition::~ScopedTokenPosition() {
	if (!discarded_) {
		parser_.restore_token_position(saved_handle_);
	}
}

ParseResult Parser::ScopedTokenPosition::success(ASTNode node) {
	discarded_ = true;
	parser_.discard_saved_token(saved_handle_);
	return ParseResult::success(node);
}

ParseResult Parser::ScopedTokenPosition::error(std::string_view error_message) {
	discarded_ = true;
	parser_.discard_saved_token(saved_handle_);
	return ParseResult::error(std::string(error_message),
							  parser_.peek_info());
}

ParseResult Parser::ScopedTokenPosition::propagate(ParseResult&& result) {
	// Sub-parser already handled position restoration (if needed)
	// Just discard our saved position and forward the result
	discarded_ = true;
	parser_.discard_saved_token(saved_handle_);
	return std::move(result);
}

Token Parser::consume_token() {
	Token token = current_token_;

	// Phase 5: Check if we have an injected token (from >> splitting)
	if (injected_token_.type() != Token::Type::Uninitialized) {
		// Use injected token as the next current_token_
		current_token_ = injected_token_;
		injected_token_ = Token{};  // reset to Uninitialized
		FLASH_LOG_FORMAT(Parser, Debug, "consume_token: Consumed token='{}', next token from injected='{}'",
						 std::string(token.value()),
						 std::string(current_token_.value()));
	} else {
		// Normal path: get next token from lexer
		Token next = lexer_.next_token();
		FLASH_LOG_FORMAT(Parser, Debug, "consume_token: Consumed token='{}', next token from lexer='{}'",
						 std::string(token.value()),
						 std::string(next.value()));
		current_token_ = next;
	}
	return token;
}

Token Parser::peek_token() {
	// Return current token — EndOfFile is a valid token (not nullopt)
	return current_token_;
}

Token Parser::peek_token(size_t lookahead) {
	if (lookahead == 0) {
		return peek_token();	 // Peek at current token
	}

	// Save current position
	SaveHandle saved_handle = save_token_position();

	// Consume tokens to reach the lookahead position
	for (size_t i = 0; i < lookahead; ++i) {
		consume_token();
	}

	// Peek at the token at lookahead position
	Token result = peek_token();

	// Restore original position
	restore_lexer_position_only(saved_handle);

	// Discard the saved position as we're done with it
	discard_saved_token(saved_handle);

	return result;
}

// Phase 5: Split >> token into two > tokens for nested templates
// This is needed for C++20 maximal munch rules: Foo<Bar<int>> should parse as Foo<Bar<int> >
void Parser::split_right_shift_token() {
	if (current_token_.kind() != ">>"_tok) {
		FLASH_LOG(Parser, Error, "split_right_shift_token called but current token is not >>");
		return;
	}

	FLASH_LOG(Parser, Debug, "Splitting >> token into two > tokens for nested template");

	// Create two synthetic > tokens
	// We use static storage for the ">" string since string_view needs valid memory
	static const std::string_view gt_str = ">";

	Token first_gt(Token::Type::Operator, gt_str,
				   current_token_.line(),
				   current_token_.column(),
				   current_token_.file_index());

	Token second_gt(Token::Type::Operator, gt_str,
					current_token_.line(),
					current_token_.column() + 1,	 // Second > is one character after first
					current_token_.file_index());

	// Replace current >> with first >
	current_token_ = first_gt;

	// Inject second > to be consumed next
	injected_token_ = second_gt;
}

// ---- New TokenKind-based API (Phase 0) ----

// Static EOF token returned by peek_info() when at end of input
static const Token eof_token_sentinel(Token::Type::EndOfFile, ""sv, 0, 0, 0);

TokenKind Parser::peek() const {
	return current_token_.kind();
}

TokenKind Parser::peek(size_t lookahead) {
	if (lookahead == 0) {
		return peek();
	}
	return peek_token(lookahead).kind();
}

const Token& Parser::peek_info() const {
	return current_token_;
}

Token Parser::peek_info(size_t lookahead) {
	if (lookahead == 0) {
		return peek_info();
	}
	return peek_token(lookahead);
}

Token Parser::advance() {
	Token result = current_token_;

	// Phase 5: Check if we have an injected token (from >> splitting)
	if (injected_token_.type() != Token::Type::Uninitialized) {
		current_token_ = injected_token_;
		injected_token_ = Token{};  // reset to Uninitialized
	} else {
		current_token_ = lexer_.next_token();
	}
	return result;
}

bool Parser::consume(TokenKind kind) {
	if (peek() == kind) {
		advance();
		return true;
	}
	return false;
}

Token Parser::expect(TokenKind kind) {
	if (peek() == kind) {
		return advance();
	}
	// Emit diagnostic — find the spelling for the expected kind
	std::string_view expected_spelling = "?";
	for (const auto& entry : all_fixed_tokens) {
		if (entry.kind == kind) {
			expected_spelling = entry.spelling;
			break;
		}
	}
	const Token& cur = peek_info();
	FLASH_LOG(Parser, Error, "Expected '", expected_spelling, "' but got '", cur.value(),
			  "' at line ", cur.line(), " column ", cur.column());
	return eof_token_sentinel;
}

Parser::SaveHandle Parser::save_token_position() {
	// Generate unique handle using static incrementing counter
	// This prevents collisions even when multiple saves happen at the same cursor position
	SaveHandle handle = next_save_handle_++;

	// Save current parser state (including injected token for >> splitting)
	TokenPosition lexer_pos = lexer_.save_token_position();
	saved_tokens_[handle] = {current_token_, injected_token_, ast_nodes_.size(), lexer_pos};

	FLASH_LOG_FORMAT(Parser, Debug, "save_token_position: handle={}, token={}",
					 static_cast<unsigned long>(handle), std::string(current_token_.value()));

	return handle;
}

void Parser::restore_token_position(SaveHandle handle, [[maybe_unused]] const std::source_location location) {
	auto it = saved_tokens_.find(handle);
	if (it == saved_tokens_.end()) {
		// Handle not found - this shouldn't happen in correct usage
		return;
	}

	const SavedToken& saved_token = it->second;
	{
		std::string saved_tok = std::string(saved_token.current_token_.value());
		std::string current_tok = std::string(current_token_.value());

		FLASH_LOG_FORMAT(Parser, Debug, "restore_token_position: handle={}, saved token={}, current={}",
						 static_cast<unsigned long>(handle), saved_tok, current_tok);
	}

	lexer_.restore_token_position(saved_token.lexer_position_);
	current_token_ = saved_token.current_token_;

	// Phase 5: Restore injected token state from save point
	// If the save was made before a >> split, injected_token_ will be Uninitialized (clearing it).
	// If the save was made after a >> split, injected_token_ will contain the second >.
	injected_token_ = saved_token.injected_token_;

	// Process AST nodes that were added after the saved position.
	// We need to:
	// 1. Keep FunctionDeclarationNode and StructDeclarationNode in ast_nodes_ - they may be
	//    template instantiations registered in gTemplateRegistry.instantiations_ cache
	// 2. Move other nodes to ast_discarded_nodes_ to keep them alive (prevent memory corruption)
	//    but not pollute the AST tree
	//
	// This can happen when parsing expressions like `(all(1,1,1) ? 1 : TypeIndex{})`:
	// 1. Parser tries fold expression patterns, saving position
	// 2. Parser parses `all(1,1,1)`, which instantiates the template
	// 3. Parser finds it's not a fold expression, restores position
	// 4. Template instantiation must be kept in ast_nodes_ for code generation
	size_t new_size = saved_token.ast_nodes_size_;
	// Safety check: don't iterate past the current vector size
	if (new_size > ast_nodes_.size()) {
		// This shouldn't happen, but if it does, just skip the cleanup
		return;
	}

	// Iterate from the end to avoid invalidating iterators when removing elements
	for (size_t i = ast_nodes_.size(); i > new_size;) {
		--i;
		ASTNode& node = ast_nodes_[i];
		if (node.is<FunctionDeclarationNode>() || node.is<StructDeclarationNode>()) {
			// Keep function and struct declarations - they may be template instantiations
			// or struct definitions that are already registered in the symbol table
			// Leave this node in place
		} else {
			// Move this node to discarded list to keep it alive, then remove from ast_nodes_
			ast_discarded_nodes_.push_back(std::move(node));
			eraseTopLevelNodeAt(i);
		}
	}
}

void Parser::restore_lexer_position_only(Parser::SaveHandle handle) {
	// Restore lexer position and current token, but keep AST nodes
	auto it = saved_tokens_.find(handle);
	if (it == saved_tokens_.end()) {
		return;
	}

	const SavedToken& saved_token = it->second;
	lexer_.restore_token_position(saved_token.lexer_position_);
	current_token_ = saved_token.current_token_;
	injected_token_ = saved_token.injected_token_;
	// Don't erase AST nodes - they were intentionally created during re-parsing
}

void Parser::discard_saved_token(SaveHandle handle) {
	saved_tokens_.erase(handle);
}

void Parser::skip_balanced_braces() {
	skip_balanced_delimiters("{"_tok, "}"_tok);
}

void Parser::skip_balanced_parens() {
	skip_balanced_delimiters("("_tok, ")"_tok);
}

// Skip one or more catch clauses: ('catch' '(' ... ')' '{' ... '}')+
void Parser::skip_catch_clauses() {
	while (peek() == "catch"_tok) {
		advance();  // consume 'catch'
		skip_balanced_parens();	// skip '(' exception-declaration ')'
		skip_balanced_braces();	// skip catch body
	}
}

// Skip a complete function body, which is either:
//   '{' ... '}'  (normal block)
//   'try' '{' ... '}' ('catch' '(' ... ')' '{' ... '}')+
void Parser::skip_function_body() {
	if (peek() == "{"_tok) {
		skip_balanced_braces();
	} else if (peek() == "try"_tok) {
		advance();  // consume 'try'
		skip_balanced_braces();	// skip the try body
		skip_catch_clauses();
	}
}

void Parser::skip_balanced_delimiters(TokenKind open, TokenKind close) {
	if (peek() != open) {
		return;
	}

	int depth = 0;
	size_t token_count = 0;
	const size_t MAX_TOKENS = 10000;	 // Safety limit to prevent infinite loops

	while (!peek().is_eof() && token_count < MAX_TOKENS) {
		auto kind = peek();
		if (kind == open) {
			depth++;
		} else if (kind == close) {
			depth--;
			if (depth == 0) {
				advance();
				break;
			}
		}
		advance();
		token_count++;
	}
}

void Parser::skip_template_arguments() {
	// Expect the current token to be '<'
	if (peek() != "<"_tok) {
		return;
	}

	int angle_depth = 0;
	size_t token_count = 0;
	const size_t MAX_TOKENS = 10000;	 // Safety limit to prevent infinite loops

	while (!peek().is_eof() && token_count < MAX_TOKENS) {
		update_angle_depth(peek(), angle_depth);
		advance();

		if (angle_depth == 0) {
			// We've consumed the closing '>' or '>>'
			break;
		}

		token_count++;
	}
}

void Parser::skip_qualified_name_parts() {
	// Consume namespace-qualified name parts (::identifier pairs).
	// For callers that need the full qualified name, use consume_qualified_name_suffix() instead.
	// Lookahead before consuming '::' to avoid leaving the parser in an inconsistent state
	// if '::' is not followed by a valid identifier (e.g., "std::{").
	while (peek() == "::"_tok && (peek(1).is_identifier() || peek(1).is_keyword())) {
		advance(); // consume '::'
		advance(); // consume the qualified name part
	}
}

std::string_view Parser::consume_qualified_name_suffix(std::string_view base_name) {
	// Consume ::identifier pairs and build the full qualified name.
	// Given "std" already consumed, if peek() is "::", consumes "::optional"
	// and returns "std::optional" (interned via StringBuilder).
	// If no "::" follows, returns base_name unchanged (no allocation).
	// Lookahead before consuming '::' to avoid producing invalid names like "std::"
	// when '::' is not followed by a valid identifier (e.g., "std::{").
	if (peek() != "::"_tok || !(peek(1).is_identifier() || peek(1).is_keyword()))
		return base_name;

	StringBuilder builder;
	builder.append(base_name);
	while (peek() == "::"_tok && (peek(1).is_identifier() || peek(1).is_keyword())) {
		advance(); // consume '::'
		builder.append("::");
		builder.append(peek_info().value());
		advance(); // consume the qualified name part
	}
	return builder.commit();
}

void Parser::skip_member_declaration_to_semicolon() {
	// Skip tokens until we reach ';' at top level, or an unmatched '}'
	// Handles nested parentheses, angle brackets, and braces
	int paren_depth = 0;
	int angle_depth = 0;
	int brace_depth = 0;

	while (!peek().is_eof()) {
		auto kind = peek();

		if (kind == "("_tok) {
			paren_depth++;
			advance();
		} else if (kind == ")"_tok) {
			paren_depth--;
			advance();
		} else if (kind == "<"_tok || kind == ">"_tok || kind == ">>"_tok) {
			update_angle_depth(kind, angle_depth);
			advance();
		} else if (kind == "{"_tok) {
			brace_depth++;
			advance();
		} else if (kind == "}"_tok) {
			if (brace_depth == 0) {
				break;  // Don't consume - this is end of struct
			}
			brace_depth--;
			advance();
		} else if (kind == ";"_tok && paren_depth == 0 && angle_depth == 0 && brace_depth == 0) {
			advance();
			break;
		} else {
			advance();
		}
	}
}

// Helper function to parse the contents of pack(...) after the opening '('
// Returns success and consumes the closing ')' on success
ParseResult Parser::parse_pragma_pack_inner() {
	// Check if it's empty: pack()
	if (consume(")"_tok)) {
		context_.setPackAlignment(0); // Reset to default
		return ParseResult::success();
	}

	// Check for push/pop/show: pack(push) or pack(pop) or pack(show)
	// Full syntax:
	//   pack(push [, identifier] [, n])
	//   pack(pop [, {identifier | n}])
	//   pack(show)
	if (peek().is_identifier()) {
		std::string_view pack_action = peek_info().value();

		// Handle pack(show)
		if (pack_action == "show") {
			advance(); // consume 'show'
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after pragma pack show", current_token_);
			}
			// Emit a warning showing the current pack alignment
			size_t current_align = context_.getCurrentPackAlignment();
			if (current_align == 0) {
				FLASH_LOG(Parser, Warning, "current pack alignment is default (natural alignment)");
			} else {
				FLASH_LOG(Parser, Warning, "current pack alignment is ", current_align);
			}
			return ParseResult::success();
		}

		if (pack_action == "push" || pack_action == "pop") {
			advance(); // consume 'push' or 'pop'

			// Check for optional parameters
			if (peek() == ","_tok) {
				advance(); // consume ','

				// First parameter could be identifier or number
				if (!peek().is_eof()) {
					// Check if it's an identifier (label name)
					if (peek().is_identifier()) {
						std::string_view identifier = peek_info().value();
						advance(); // consume the identifier

						// Check for second comma and alignment value
						if (peek() == ","_tok) {
							advance(); // consume second ','

							if (!peek().is_eof()) {
								if (peek().is_literal()) {
									std::string_view value_str = peek_info().value();
									size_t alignment = 0;
									auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), alignment);
									if (result.ec == std::errc()) {
										if (pack_action == "push") {
											context_.pushPackAlignment(identifier, alignment);
										}
										advance(); // consume the number
									} else {
										advance(); // consume invalid number
										if (pack_action == "push") {
											context_.pushPackAlignment(identifier);
										} else {
											context_.popPackAlignment(identifier);
										}
									}
								} else if (peek().is_identifier()) {
									// Another identifier (macro) - treat as no alignment specified
									advance();
									if (pack_action == "push") {
										context_.pushPackAlignment(identifier);
									} else {
										context_.popPackAlignment(identifier);
									}
								}
							}
						} else {
							// Just identifier, no alignment
							if (pack_action == "push") {
								context_.pushPackAlignment(identifier);
							} else {
								context_.popPackAlignment(identifier);
							}
						}
					}
					// Check if it's a number directly (no identifier)
					else if (peek().is_literal()) {
						std::string_view value_str = peek_info().value();
						size_t alignment = 0;
						auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), alignment);
						if (result.ec == std::errc()) {
							if (pack_action == "push") {
								context_.pushPackAlignment(alignment);
							}
							advance(); // consume the number
						} else {
							advance(); // consume invalid number
							if (pack_action == "push") {
								context_.pushPackAlignment();
							} else {
								context_.popPackAlignment();
							}
						}
					}
				}
			} else {
				// No parameters - simple push/pop
				if (pack_action == "push") {
					context_.pushPackAlignment();
				} else {
					context_.popPackAlignment();
				}
			}

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after pragma pack push/pop", current_token_);
			}
			return ParseResult::success();
		}
	}

	// Try to parse a number: pack(N)
	if (peek().is_literal()) {
		std::string_view value_str = peek_info().value();
		size_t alignment = 0;
		auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), alignment);
		if (result.ec == std::errc() &&
			(alignment == 0 || alignment == 1 || alignment == 2 ||
			 alignment == 4 || alignment == 8 || alignment == 16)) {
			context_.setPackAlignment(alignment);
			advance(); // consume the number
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after pack alignment value", current_token_);
			}
			return ParseResult::success();
		}
	}

	// If we get here, it's an unsupported pragma pack format
	return ParseResult::error("Unsupported pragma pack format", current_token_);
}

void Parser::register_builtin_functions() {
	// Register compiler builtin functions so they can be recognized as function calls
	// These will be handled as intrinsics in CodeGen

	// Create dummy tokens for builtin functions
	Token dummy_token(Token::Type::Identifier, ""sv, 0, 0, 0);

	// Helper lambda to register a builtin function with one parameter
	auto register_builtin = [&](std::string_view name, TypeCategory return_type, TypeCategory param_type) {
		// Create return type node
		Token type_token = dummy_token;
		auto return_type_node = emplace_node<TypeSpecifierNode>(return_type, TypeQualifier::None, 64, type_token, CVQualifier::None);

		// Create function name token
		Token func_token = dummy_token;
		func_token = Token(Token::Type::Identifier, name, 0, 0, 0);

		// Create declaration node for the function
		auto decl_node = emplace_node<DeclarationNode>(return_type_node, func_token);

		// Create function declaration node
		auto [func_decl_node, func_decl_ref] = emplace_node_ref<FunctionDeclarationNode>(decl_node.as<DeclarationNode>());

		// Create parameter
		Token param_token = dummy_token;
		auto param_type_node = emplace_node<TypeSpecifierNode>(param_type, TypeQualifier::None, 64, param_token, CVQualifier::None);
		auto param_decl = emplace_node<DeclarationNode>(param_type_node, param_token);
		func_decl_ref.add_parameter_node(param_decl);

		// Set extern "C" linkage
		func_decl_ref.set_linkage(Linkage::C);

		// Register in global symbol table
		gSymbolTable.insert(name, func_decl_node);
	};

	// Helper lambda to register a builtin function with two parameters
	auto register_two_param_builtin = [&](std::string_view name, TypeCategory return_type, TypeCategory param1_type, TypeCategory param2_type) {
		// Create return type node
		Token type_token = dummy_token;
		auto return_type_node = emplace_node<TypeSpecifierNode>(return_type, TypeQualifier::None, 64, type_token, CVQualifier::None);

		// Create function name token
		Token func_token = dummy_token;
		func_token = Token(Token::Type::Identifier, name, 0, 0, 0);

		// Create declaration node for the function
		auto decl_node = emplace_node<DeclarationNode>(return_type_node, func_token);

		// Create function declaration node
		auto [func_decl_node, func_decl_ref] = emplace_node_ref<FunctionDeclarationNode>(decl_node.as<DeclarationNode>());

		// Create first parameter
		Token param1_token = dummy_token;
		auto param1_type_node = emplace_node<TypeSpecifierNode>(param1_type, TypeQualifier::None, 64, param1_token, CVQualifier::None);
		auto param1_decl = emplace_node<DeclarationNode>(param1_type_node, param1_token);
		func_decl_ref.add_parameter_node(param1_decl);

		// Create second parameter
		Token param2_token = dummy_token;
		auto param2_type_node = emplace_node<TypeSpecifierNode>(param2_type, TypeQualifier::None, 64, param2_token, CVQualifier::None);
		auto param2_decl = emplace_node<DeclarationNode>(param2_type_node, param2_token);
		func_decl_ref.add_parameter_node(param2_decl);

		// Set extern "C" linkage
		func_decl_ref.set_linkage(Linkage::C);

		// Register in global symbol table
		gSymbolTable.insert(name, func_decl_node);
	};

	// Helper lambda to register a builtin function with no parameters
	auto register_no_param_builtin = [&](std::string_view name, TypeCategory return_type) {
		// Create return type node
		Token type_token = dummy_token;
		auto return_type_node = emplace_node<TypeSpecifierNode>(return_type, TypeQualifier::None, 64, type_token, CVQualifier::None);

		// Create function name token
		Token func_token = dummy_token;
		func_token = Token(Token::Type::Identifier, name, 0, 0, 0);

		// Create declaration node for the function
		auto decl_node = emplace_node<DeclarationNode>(return_type_node, func_token);

		// Create function declaration node
		auto [func_decl_node, func_decl_ref] = emplace_node_ref<FunctionDeclarationNode>(decl_node.as<DeclarationNode>());

		// Register in global symbol table
		gSymbolTable.insert(name, func_decl_node);
	};

	// Register variadic argument intrinsics (support both __va_start and __builtin_va_start)
	// __builtin_va_start(va_list*, last_param) - Clang-style
	// __va_start(va_list*, last_param) - MSVC-style (legacy)
	// Both return void
	register_two_param_builtin("__builtin_va_start", TypeCategory::Void, TypeCategory::UnsignedLongLong, TypeCategory::UnsignedLongLong);
	register_two_param_builtin("__va_start", TypeCategory::Void, TypeCategory::UnsignedLongLong, TypeCategory::UnsignedLongLong);

	// __builtin_va_arg(va_list, type) - returns the specified type
	// For registration purposes, we use int as the return type (will be overridden in codegen)
	// The second parameter is the type identifier, but we just register it as int for parsing
	register_two_param_builtin("__builtin_va_arg", TypeCategory::Int, TypeCategory::UnsignedLongLong, TypeCategory::Int);

	// Register integer abs builtins
	register_builtin("__builtin_labs", TypeCategory::Long, TypeCategory::Long);
	register_builtin("__builtin_llabs", TypeCategory::LongLong, TypeCategory::LongLong);

	// Register floating point abs builtins
	register_builtin("__builtin_fabs", TypeCategory::Double, TypeCategory::Double);
	register_builtin("__builtin_fabsf", TypeCategory::Float, TypeCategory::Float);
	register_builtin("__builtin_fabsl", TypeCategory::LongDouble, TypeCategory::LongDouble);

	// Register optimization hint intrinsics
	// __builtin_unreachable() - marks unreachable code paths
	register_no_param_builtin("__builtin_unreachable", TypeCategory::Void);

	// __builtin_assume(condition) - assumes condition is true for optimization
	register_builtin("__builtin_assume", TypeCategory::Void, TypeCategory::Bool);

	// __builtin_expect(expr, expected) - branch prediction hint, returns expr
	// Using LongLong to match typical usage pattern
	register_two_param_builtin("__builtin_expect", TypeCategory::LongLong, TypeCategory::LongLong, TypeCategory::LongLong);

	// __builtin_launder(ptr) - optimization barrier for pointers
	// Using UnsignedLongLong (pointer-sized) for the parameter and return type
	register_builtin("__builtin_launder", TypeCategory::UnsignedLongLong, TypeCategory::UnsignedLongLong);

	// Helper to register an extern "C" function builtin with an arbitrary signature.
	auto register_extern_c_builtin = [&](std::string_view name, const ASTNode& return_type, std::initializer_list<ASTNode> params) {
		auto decl = emplace_node<DeclarationNode>(return_type, Token(Token::Type::Identifier, name, 0, 0, 0));
		auto [fn, fn_ref] = emplace_node_ref<FunctionDeclarationNode>(decl.as<DeclarationNode>());
		for (const auto& param_type : params) {
			fn_ref.add_parameter_node(emplace_node<DeclarationNode>(param_type, dummy_token));
		}
		fn_ref.set_linkage(Linkage::C);
		gSymbolTable.insert(name, fn);
	};

	auto make_builtin_type = [&](TypeCategory base_type, CVQualifier cv, int pointer_depth) {
		auto [t, t_ref] = emplace_node_ref<TypeSpecifierNode>(base_type, TypeQualifier::None, get_type_size_bits(base_type), dummy_token, cv);
		for (int i = 0; i < pointer_depth; ++i) {
			t_ref.add_pointer_level();
		}
		return t;
	};

	// size_t is 64-bit on all supported platforms, but the underlying type differs:
	// LLP64 (Windows): unsigned long long (unsigned long is 32-bit)
	// LP64  (Linux):    unsigned long      (unsigned long is 64-bit)
	const TypeCategory size_t_base = context_.isLLP64() ? TypeCategory::UnsignedLongLong : TypeCategory::UnsignedLong;
	const ASTNode void_type = make_builtin_type(TypeCategory::Void, CVQualifier::None, 0);
	const ASTNode bool_type = make_builtin_type(TypeCategory::Bool, CVQualifier::None, 0);
	const ASTNode int_type = make_builtin_type(TypeCategory::Int, CVQualifier::None, 0);
	const ASTNode void_ptr = make_builtin_type(TypeCategory::Void, CVQualifier::None, 1);
	const ASTNode const_void_ptr = make_builtin_type(TypeCategory::Void, CVQualifier::Const, 1);
	const ASTNode volatile_void_ptr = make_builtin_type(TypeCategory::Void, CVQualifier::Volatile, 1);
	const ASTNode const_volatile_void_ptr = make_builtin_type(TypeCategory::Void, CVQualifier::ConstVolatile, 1);
	const ASTNode size_t_type = make_builtin_type(size_t_base, CVQualifier::None, 0);
	// These __atomic* builtins are type-generic in GCC/Clang. We currently register
	// them with a wide integer placeholder for value positions so phase-1 lookup and
	// most unqualified builtin-name binding succeed in libstdc++ headers. Pointer-
	// typed operations still need proper generic signature modeling, which is why
	// <atomic>/<latch> now fail later on __atomic_add_fetch rather than at name lookup.
	const ASTNode generic_atomic_value_type = make_builtin_type(TypeCategory::UnsignedLongLong, CVQualifier::None, 0);

	// __builtin_strlen(const char*) - returns length of string
	register_extern_c_builtin(
		"__builtin_strlen",
		size_t_type,
		{make_builtin_type(TypeCategory::Char, CVQualifier::Const, 1)});
	register_extern_c_builtin(
		"__builtin_memcmp",
		int_type,
		{
			const_void_ptr,
			const_void_ptr,
			size_t_type
		});
	register_extern_c_builtin(
		"__builtin_memcpy",
		void_ptr,
		{
			void_ptr,
			const_void_ptr,
			size_t_type
		});
	// __builtin_alloca(size_t) — allocates on the current stack frame, returns void*.
	// libstdc++'s <bits/locale_facets.h> / <bits/locale_facets_nonio.tcc> uses it
	// through helpers like `__builtin_alloca_with_align` via template bodies, so the
	// unqualified name must be visible at name-lookup time to satisfy the
	// non-dependent-name rule.
	register_extern_c_builtin("__builtin_alloca", void_ptr, {size_t_type});
	register_extern_c_builtin("__builtin_alloca_with_align", void_ptr, {size_t_type, size_t_type});
	register_extern_c_builtin("__atomic_store", void_type, {volatile_void_ptr, const_void_ptr, int_type});
	register_extern_c_builtin("__atomic_store_n", void_type, {volatile_void_ptr, generic_atomic_value_type, int_type});
	register_extern_c_builtin("__atomic_load", void_type, {const_volatile_void_ptr, void_ptr, int_type});
	register_extern_c_builtin("__atomic_load_n", generic_atomic_value_type, {const_volatile_void_ptr, int_type});
	register_extern_c_builtin("__atomic_exchange", void_type, {volatile_void_ptr, const_void_ptr, void_ptr, int_type});
	register_extern_c_builtin("__atomic_exchange_n", generic_atomic_value_type, {volatile_void_ptr, generic_atomic_value_type, int_type});
	register_extern_c_builtin(
		"__atomic_compare_exchange",
		bool_type,
		{volatile_void_ptr, void_ptr, const_void_ptr, bool_type, int_type, int_type});
	register_extern_c_builtin(
		"__atomic_compare_exchange_n",
		bool_type,
		{volatile_void_ptr, void_ptr, generic_atomic_value_type, bool_type, int_type, int_type});
	register_extern_c_builtin("__atomic_fetch_add", generic_atomic_value_type, {volatile_void_ptr, generic_atomic_value_type, int_type});
	register_extern_c_builtin("__atomic_fetch_sub", generic_atomic_value_type, {volatile_void_ptr, generic_atomic_value_type, int_type});
	register_extern_c_builtin("__atomic_fetch_and", generic_atomic_value_type, {volatile_void_ptr, generic_atomic_value_type, int_type});
	register_extern_c_builtin("__atomic_fetch_or", generic_atomic_value_type, {volatile_void_ptr, generic_atomic_value_type, int_type});
	register_extern_c_builtin("__atomic_fetch_xor", generic_atomic_value_type, {volatile_void_ptr, generic_atomic_value_type, int_type});
	register_extern_c_builtin("__atomic_add_fetch", generic_atomic_value_type, {volatile_void_ptr, generic_atomic_value_type, int_type});
	register_extern_c_builtin("__atomic_sub_fetch", generic_atomic_value_type, {volatile_void_ptr, generic_atomic_value_type, int_type});
	register_extern_c_builtin("__atomic_and_fetch", generic_atomic_value_type, {volatile_void_ptr, generic_atomic_value_type, int_type});
	register_extern_c_builtin("__atomic_or_fetch", generic_atomic_value_type, {volatile_void_ptr, generic_atomic_value_type, int_type});
	register_extern_c_builtin("__atomic_xor_fetch", generic_atomic_value_type, {volatile_void_ptr, generic_atomic_value_type, int_type});
	register_extern_c_builtin("__atomic_is_lock_free", bool_type, {size_t_type, const_volatile_void_ptr});
	register_extern_c_builtin("__atomic_always_lock_free", bool_type, {size_t_type, const_volatile_void_ptr});
	register_extern_c_builtin("__atomic_test_and_set", bool_type, {volatile_void_ptr, int_type});
	register_extern_c_builtin("__atomic_clear", void_type, {volatile_void_ptr, int_type});
	register_extern_c_builtin("__atomic_thread_fence", void_type, {int_type});
	register_extern_c_builtin("__atomic_signal_fence", void_type, {int_type});

	// Wide-character memory/string functions needed by char_traits<wchar_t>.
	// These are declared in <wchar.h>/<cwchar> but char_traits.h may use them
	// before those headers are explicitly included.
	const ASTNode wchar_t_ptr = make_builtin_type(TypeCategory::WChar, CVQualifier::None, 1);
	const ASTNode const_wchar_t_ptr = make_builtin_type(TypeCategory::WChar, CVQualifier::Const, 1);

	register_extern_c_builtin(
		"wmemcmp",
		make_builtin_type(TypeCategory::Int, CVQualifier::None, 0),
		{const_wchar_t_ptr, const_wchar_t_ptr, size_t_type});
	register_extern_c_builtin(
		"wmemchr",
		wchar_t_ptr,
		{wchar_t_ptr, make_builtin_type(TypeCategory::WChar, CVQualifier::None, 0), size_t_type});
	register_extern_c_builtin(
		"wmemchr",
		const_wchar_t_ptr,
		{const_wchar_t_ptr, make_builtin_type(TypeCategory::WChar, CVQualifier::None, 0), size_t_type});
	register_extern_c_builtin(
		"wmemcpy",
		wchar_t_ptr,
		{wchar_t_ptr, const_wchar_t_ptr, size_t_type});
	register_extern_c_builtin(
		"wmemmove",
		wchar_t_ptr,
		{wchar_t_ptr, const_wchar_t_ptr, size_t_type});
	register_extern_c_builtin(
		"wmemset",
		wchar_t_ptr,
		{wchar_t_ptr, make_builtin_type(TypeCategory::WChar, CVQualifier::None, 0), size_t_type});
	register_extern_c_builtin(
		"wcslen",
		size_t_type,
		{const_wchar_t_ptr});

	// Register std::terminate - no pre-computed mangled name, will be mangled with namespace context
	// Note: Forward declarations inside functions don't capture namespace context,
	// so we register it globally without explicit mangling
	register_no_param_builtin("terminate", TypeCategory::Void);
}
