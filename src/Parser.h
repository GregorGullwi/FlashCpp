#pragma once

#include <cmath>
#include <chrono>
#include <exception>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <charconv>
#include <cstdlib>
#include <algorithm>
#include <numeric>
#include <string_view>
#include <source_location>
#include <functional>
#include <type_traits>
#include <span>

#include "AstNodeTypes.h"
#include "Lexer.h"
#include "Token.h"
#include "SymbolTable.h"
#include "CompileContext.h"
#include "TemplateRegistry.h"  // Includes ConceptRegistry as well
#include "ParserTypes.h"		 // Unified parsing types (Phase 1)
#include "ParserScopeGuards.h" // RAII scope guards (Phase 3)
#include "LazyMemberResolver.h" // For gLazyMemberResolver used in createBoundIdentifier
#include "ParserInternal.h"

#ifndef WITH_DEBUG_INFO
#define WITH_DEBUG_INFO 0
#endif // WITH_DEBUG_INFO

using namespace std::literals::string_view_literals;

// Resets the per-compilation template instantiation counters in
// Parser_Templates_Inst_ClassTemplate.cpp.  Must be called once at the
// start of every parse() invocation so that iteration counts and trip flags
// from a previous compilation (e.g. in a long-lived compiler driver or
// language server) do not bleed over into the new compilation unit.
void resetTemplateInstantiationCounters();

namespace ConstExpr {
class Evaluator;
struct EvalResult;
}

class SemanticAnalysis;

// RAII helper to execute a cleanup function on scope exit
// Usage: ScopeGuard guard([&]() { cleanup(); });
template <typename Func>
class ScopeGuard {
public:
	explicit ScopeGuard(Func&& cleanup) : cleanup_(std::forward<Func>(cleanup)), active_(true) {}
	~ScopeGuard() {
		if (active_)
			cleanup_();
	}

	// Prevent copying
	ScopeGuard(const ScopeGuard&) = delete;
	ScopeGuard& operator=(const ScopeGuard&) = delete;

	// Allow moving
	ScopeGuard(ScopeGuard&& other) noexcept : cleanup_(std::move(other.cleanup_)), active_(other.active_) {
		other.active_ = false;
	}

	// Dismiss the guard (don't run cleanup)
	void dismiss() { active_ = false; }

private:
	Func cleanup_;
	bool active_;
};

// Deduction guide for ScopeGuard
template <typename Func>
ScopeGuard(Func) -> ScopeGuard<Func>;

enum class ParserError {
	None,
	UnexpectedToken,
	MissingSemicolon,
	RedefinedSymbolWithDifferentValue,

	NotImplemented
};

static std::string_view get_parser_error_string(ParserError e) {
	switch (e) {
	case ParserError::None:
	default:
		return "Internal error";

	case ParserError::UnexpectedToken:
		return "Unexpected token";

	case ParserError::MissingSemicolon:
		return "Missing semicolon(;)";

	case ParserError::RedefinedSymbolWithDifferentValue:
		return "Redefined symbol with different value";

	case ParserError::NotImplemented:
		return "Feature/token type not implemented yet";
	}
}

class ParseResult {
public:
	ParseResult() : value_or_error_(std::monostate{}) {}
	ParseResult(ASTNode node) : value_or_error_(node) {}
	ParseResult(std::string error_message, Token token)
		: value_or_error_(Error{std::move(error_message), std::move(token)}) {}

	bool is_error() const {
		return std::holds_alternative<Error>(value_or_error_);
	}

	bool is_null() const {
		return std::holds_alternative<std::monostate>(value_or_error_);
	}

	std::optional<ASTNode> node() const {
		if (is_error() || is_null()) {
			return std::nullopt;
		}
		return std::get<ASTNode>(value_or_error_);
	}

	bool has_value() const {
		return std::holds_alternative<ASTNode>(value_or_error_);
	}

	const std::string& error_message() const {
		static const std::string empty;
		return is_error() ? std::get<Error>(value_or_error_).error_message_ : empty;
	}

	const Token& error_token() const {
		static const Token empty_token;
		return is_error() ? std::get<Error>(value_or_error_).token_ : empty_token;
	}

		// Format error message with file:line:column information and include stack
	std::string format_error(const std::deque<std::string>& file_paths,
							 const std::vector<SourceLineMapping>& line_map = {},
							 const Lexer* lexer = nullptr) const {
		if (!is_error())
			return "";

		const auto& err = std::get<Error>(value_or_error_);
		const Token& tok = err.token_;
		std::string result;

				// Build include stack by walking parent chain
		if (!line_map.empty() && tok.line() > 0 && tok.line() <= line_map.size()) {
			std::vector<size_t> include_chain;
			size_t current = tok.line();

						// Walk up the parent chain
			while (current > 0 && current <= line_map.size()) {
				const auto& mapping = line_map[current - 1];
				include_chain.push_back(current);
				current = mapping.parent_line;
			}

						// Reverse to get main file first
			std::reverse(include_chain.begin(), include_chain.end());

						// Show the include chain (skip the last one as it's the error location itself)
			if (include_chain.size() > 1) {
				for (size_t i = 0; i < include_chain.size() - 1; ++i) {
					size_t line_idx = include_chain[i];
					const auto& mapping = line_map[line_idx - 1];
					if (mapping.source_file_index < file_paths.size()) {
						result += "In file included from " + file_paths[mapping.source_file_index];
						result += ":" + std::to_string(mapping.source_line);
						if (i + 1 < include_chain.size() - 1) {
							result += ",\n";
						} else {
							result += ":\n";
						}
					}
				}
			}
		}

				// Primary error location
		size_t error_line = tok.line();
		size_t error_file_index = tok.file_index();

				// Map preprocessed line to source line if line_map is available
		if (!line_map.empty() && tok.line() > 0 && tok.line() <= line_map.size()) {
			const auto& mapping = line_map[tok.line() - 1];
			error_line = mapping.source_line;
			error_file_index = mapping.source_file_index;
		}

		if (error_file_index < file_paths.size()) {
			result += file_paths[error_file_index] + ":" +
					  std::to_string(error_line) + ":" +
					  std::to_string(tok.column()) + ": ";
		} else {
			result += "<unknown>:" + std::to_string(error_line) + ":" +
					  std::to_string(tok.column()) + ": ";
		}

		result += "error: " + err.error_message_;

				// Add the source line if lexer is provided
		if (lexer && tok.line() > 0) {
			std::string line_text = lexer->get_line_text(tok.line());
			if (!line_text.empty()) {
				result += "\n  " + line_text;
								// Add a caret pointing to the error column
				if (tok.column() > 0) {
					result += "\n  ";
					for (size_t i = 1; i < tok.column(); ++i) {
						result += " ";
					}
					result += "^";
				}
			}
		}

		return result;
	}

	static ParseResult success(ASTNode node = ASTNode{}) {
		return ParseResult(node);
	}
	static ParseResult error(const std::string& error_message, Token token) {
		return ParseResult(error_message, std::move(token));
	}
	static ParseResult error(ParserError e, Token token) {
		return ParseResult(std::string(get_parser_error_string(e)),
						   std::move(token));
	}
	static ParseResult null() {
		return ParseResult();
	}

	struct Error {
		std::string error_message_;
		Token token_;
	};

private:
	std::variant<std::monostate, ASTNode, Error> value_or_error_;
};

// Phase 2: Unified Qualified Identifier Parser Result Structure
// This consolidates all qualified identifier parsing into a single interface
struct QualifiedIdParseResult {
	std::vector<StringHandle> namespaces;
	Token final_identifier;
	std::optional<std::vector<TemplateTypeArg>> template_args;
	bool has_template_arguments;

	QualifiedIdParseResult(const std::vector<StringHandle>& ns, const Token& id)
		: namespaces(ns), final_identifier(id), has_template_arguments(false) {}

	QualifiedIdParseResult(const std::vector<StringHandle>& ns, const Token& id,
						   const std::vector<TemplateTypeArg>& args)
		: namespaces(ns), final_identifier(id), template_args(args), has_template_arguments(true) {}
};

// Result of consuming ::member type access and ... pack expansion after template arguments
// in a base class specifier. Shared across all base class parsing sites.
struct BaseClassPostTemplateInfo {
	std::vector<QualifiedTypeMemberAccess> member_type_chain;
	std::optional<Token> member_name_token;
	bool is_pack_expansion = false;
};

// Result of building a template parameter-to-argument substitution map.
// Used by ExpressionSubstitutor and other template instantiation sites.
struct SubstitutionParamMap {
	std::unordered_map<std::string_view, TemplateTypeArg> param_map;
	std::vector<std::string_view> param_order;

	bool empty() const { return param_map.empty(); }
};

inline TemplateTypeArg enrichTemplateArgForParameter(
	const TemplateParameterNode& param,
	TemplateTypeArg arg) {
	if (param.kind() != TemplateParameterKind::Template) {
		return arg;
	}
	arg.is_template_template_arg = true;
	if (!arg.template_name_handle.isValid()) {
		if (arg.type_index.is_valid()) {
			if (const TypeInfo* ti = tryGetTypeInfo(arg.type_index)) {
				arg.template_name_handle = ti->name_;
			}
		}
		if (!arg.template_name_handle.isValid() && arg.dependent_name.isValid()) {
			arg.template_name_handle = arg.dependent_name;
		}
	}
	return arg;
}

// Build a SubstitutionParamMap from a params container and a template_args container.
// The params container must support operator[] and size() with ASTNode elements
// (works with both InlineVector<ASTNode, 4> and std::vector<ASTNode>).
// The arg container must support operator[] and size()
// (works with std::vector<TemplateTypeArg> and std::span<const TemplateTypeArg>).
template <typename ParamsContainer, typename ArgContainer>
inline SubstitutionParamMap buildSubstitutionParamMap(
	const ParamsContainer& template_params,
	const ArgContainer& template_args) {
	SubstitutionParamMap result;
	result.param_order.reserve(std::min(template_params.size(), template_args.size()));
	for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
		if (!template_params[i].template is<TemplateParameterNode>()) {
			continue;
		}
		const TemplateParameterNode& param = template_params[i].template as<TemplateParameterNode>();
		TemplateTypeArg arg_to_insert = enrichTemplateArgForParameter(param, template_args[i]);
		result.param_map[param.name()] = arg_to_insert;
		result.param_order.push_back(param.name());
	}
	return result;
}

template <typename ParamsContainer>
inline std::vector<std::string_view> buildTemplateParamNames(
	const ParamsContainer& template_params) {
	std::vector<std::string_view> param_names;
	param_names.reserve(template_params.size());
	for (const auto& template_param_node : template_params) {
		if (!template_param_node.template is<TemplateParameterNode>()) {
			continue;
		}
		param_names.push_back(
			template_param_node.template as<TemplateParameterNode>().name());
	}
	return param_names;
}

// Convert a successful ConstExpr::EvalResult into a TemplateTypeArg for non-type
// template arguments.  This consolidates the bool/ull/int dispatch that was
// previously duplicated at every default-argument and alias-materialization site.
// Declared here (not in TemplateRegistry_Types.h) to avoid a dependency on
// ConstExprEvaluator.h from the template-registry header.
TemplateTypeArg templateTypeArgFromEvalResult(const ConstExpr::EvalResult& eval_result);

// Thread-local instantiation backtrace.  Unlike current_instantiation_ctx_ (which is RAII
// and clears during stack unwinding), this string persists through exception propagation
// so that catch sites can report it.  Populated by ScopedParserInstantiationContext on
// the first destructor invocation during unwinding; cleared by the catch site after use.
inline thread_local std::string g_parser_instantiation_notes;

class Parser {
	// Friend classes that need access to private members
	friend class ExpressionSubstitutor;
	friend class ConstExpr::Evaluator;  // Allow constexpr evaluator to instantiate templates
	friend class TemplateInstantiationHelper;  // Allow shared template helper to instantiate templates
	friend class SemanticAnalysis;
	friend class FlashCpp::FunctionParsingScopeGuard;  // Access current_function_, setup_member_function_context, etc.

public:
	static constexpr size_t default_ast_tree_size_ = 256 * 1024;

	explicit Parser(Lexer& lexer, CompileContext& context);

	ParseResult parse() {
		resetTemplateInstantiationCounters();
		gSymbolTable = SymbolTable();
		register_builtin_functions();
		ParseResult parseResult;
#if FLASHCPP_LOG_LEVEL >= 2	// Info level progress logging
		size_t top_level_count = 0;
		auto start_time = std::chrono::high_resolution_clock::now();
#endif
				// The main parse loop: process top-level nodes until EOF or error
		while (!peek().is_eof() && !parseResult.is_error()) {
			try {
				parseResult = parse_top_level_node();
			} catch (const std::bad_any_cast& e) {
				FLASH_LOG(General, Error, "bad_any_cast during parsing at ", peek_info().value(), ": ", e.what());
				parseResult = ParseResult::error("Internal compiler error: bad_any_cast during parsing", peek_info());
				break;
			}
#if FLASHCPP_LOG_LEVEL >= 2	// Info level progress logging
			++top_level_count;
						// Log progress every 500 top-level nodes
			if (top_level_count % 500 == 0) {
				auto now = std::chrono::high_resolution_clock::now();
				auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
				FLASH_LOG(General, Info, "[Progress] Parsed ", top_level_count, " top-level nodes in ", elapsed_ms, " ms");
			}
#endif
		}

#if FLASHCPP_LOG_LEVEL >= 2	// Info level progress logging
				// Log final summary (even on error) for debugging
		auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(
							  std::chrono::high_resolution_clock::now() - start_time)
							  .count();
		FLASH_LOG(General, Info, "[Progress] Parsing ", (parseResult.is_error() ? "stopped" : "complete"), ": ",
				  top_level_count, " top-level nodes, ", ast_nodes_.size(), " AST nodes in ", total_time, " ms");
#endif

		if (parseResult.is_error()) {
			last_error_ = parseResult.error_message();
		}
		return parseResult;
	}

	const auto& get_nodes() { return ast_nodes_; }

	// Returns true if the node at `index` in `get_nodes()` was added via the
	// late-materialization helpers (`registerLateMaterializedTopLevelNode*`),
	// i.e., it is a template instantiation / synthesized root rather than a
	// user-written top-level declaration. See the lifecycle contract below.
	bool isInstantiatedNode(size_t index) const {
		return index < ast_node_is_instantiated_.size()
			&& ast_node_is_instantiated_[index] != 0;
	}

	// Returns true when the node at `index` is a late-materialized top-level
	// struct root. These are tracked separately from user-written source nodes
	// for codegen/drain consumption even though they still live in ast_nodes_
	// for parser/sema lifecycle purposes.
	bool isInstantiatedStructNode(size_t index) const {
		return isInstantiatedNode(index)
			&& index < ast_nodes_.size()
			&& ast_nodes_[index].is<StructDeclarationNode>();
	}

	// Number of user-written top-level nodes (added during initial parsing).
	size_t userNodeCount() const {
		size_t n = 0;
		for (size_t i = 0; i < ast_node_is_instantiated_.size(); ++i) {
			if (ast_node_is_instantiated_[i] == 0) {
				++n;
			}
		}
		// Account for any tail entries that predate tracking (shouldn't happen
		// in practice because all append sites go through appendUserNode, but
		// treat trailing unmarked entries as user nodes).
		if (ast_nodes_.size() > ast_node_is_instantiated_.size()) {
			n += ast_nodes_.size() - ast_node_is_instantiated_.size();
		}
		return n;
	}

	// Number of late-materialized / instantiated top-level nodes.
	size_t instantiatedNodeCount() const {
		size_t n = 0;
		for (size_t i = 0; i < ast_node_is_instantiated_.size(); ++i) {
			if (ast_node_is_instantiated_[i] != 0) {
				++n;
			}
		}
		return n;
	}
	std::vector<ASTNode> takePendingSemanticRoots() {
		std::vector<ASTNode> pending_roots = std::move(pending_semantic_roots_);
		pending_semantic_root_keys_.clear();
		return pending_roots;
	}
	void clearPendingSemanticRoots() {
		pending_semantic_roots_.clear();
		pending_semantic_root_keys_.clear();
	}
	void setActiveSemanticAnalysis(SemanticAnalysis* sema);
	SemanticAnalysis* getActiveSemanticAnalysis() const;
	void normalizePendingSemanticRootsIfAvailable();
	ASTNode get_inner_node(ASTNode node) const {
		return node;
	}

	template <typename T>
	bool is(ASTNode node) const {
		return node.is<T>();
	}

	template <typename T>
	T& as(ASTNode node) {
		return node.as<T>();
	}

	template <typename T>
	const T& as(ASTNode node) const {
		return node.as<T>();
	}

	template <typename T>
	T& as(ParseResult parse_result) {
		auto node = parse_result.node();
		return node ? node->as<T>() : ASTNode{}.as<T>();
	}

	std::string get_last_error() const { return last_error_; }

#if WITH_DEBUG_INFO
	std::optional<int> break_at_line_;
#endif // WITH_DEBUG_INFO

private:
	Lexer& lexer_;
	CompileContext& context_;
	Token current_token_;
	Token injected_token_;  // Phase 5: For >> splitting in nested templates (Uninitialized = empty)
	std::vector<ASTNode> ast_nodes_;
	// Parallel to ast_nodes_: ast_node_is_instantiated_[i] is non-zero when
	// ast_nodes_[i] was added via registerLateMaterializedTopLevelNode* (i.e.,
	// it is a template instantiation / late-materialized root). User-written
	// nodes pushed during initial parsing have their bit set to 0.
	//
	// Maintained in lockstep with ast_nodes_ via the appendUserNode /
	// registerLateMaterializedTopLevelNode* / eraseTopLevelNodeAt helpers.
	std::vector<uint8_t> ast_node_is_instantiated_;
	// Dedup set for late-materialized (instantiated) top-level nodes.
	// Keyed by ASTNode::raw_pointer() which is stable for the node's lifetime
	// (ChunkedAnyVector storage). Prevents duplicate pushes of the same
	// instantiated struct/function, which would cause LNK2005 "symbol defined
	// multiple times" at link time when codegen iterates the list. See
	// docs/2026-04-21-phase5-slice-g-analysis.md item #8.
	std::unordered_set<const void*> instantiated_node_keys_;
	std::vector<ASTNode> pending_semantic_roots_;
	std::unordered_set<const void*> pending_semantic_root_keys_;
	SemanticAnalysis* active_sema_ = nullptr;
	std::vector<ASTNode> ast_discarded_nodes_;  // Keep discarded nodes alive to prevent memory corruption
	std::string last_error_;

	// Track current function for __FUNCTION__, __func__, __PRETTY_FUNCTION__
	// Store pointer to the FunctionDeclarationNode which contains all the info we need
	const FunctionDeclarationNode* current_function_ = nullptr;

	// Track current linkage for extern "C" blocks
	Linkage current_linkage_ = Linkage::None;

	// Track last calling convention found in parse_type_and_name()
	// This is used to communicate calling convention from type parsing to function declaration
	CallingConvention last_calling_convention_ = CallingConvention::Default;

		// Result of constructor/destructor lookahead detection
	struct ConstructorLookaheadResult {
		bool detected = false;
		bool is_destructor = false;
	};

	// Track current struct context for member function parsing
	struct MemberFunctionContext {
		StringHandle struct_name;  // Points directly into source text from lexer token
		TypeIndex struct_type_index;
		StructDeclarationNode* struct_node;	// Pointer to the struct being parsed
		StructTypeInfo* local_struct_info;   // Pointer to local struct_info being built (for static member lookup)
		bool has_implicit_this = true;
	};
	std::vector<MemberFunctionContext> member_function_context_stack_;

	// Track current struct being parsed (for nested class support)
	struct StructParsingContext {
		std::string_view struct_name;  // Points directly into source text from lexer token
		StructDeclarationNode* struct_node;	// Pointer to the struct being parsed
		StructTypeInfo* local_struct_info = nullptr;	 // Pointer to StructTypeInfo being built (for static member lookup)
		NamespaceHandle namespace_handle = NamespaceHandle{0};  // Namespace where the struct is declared
		std::vector<StringHandle> imported_members;	// Members imported via using-declarations
		bool has_inherited_constructors = false;	 // True if constructors are inherited from base class
	};
	std::vector<StructParsingContext> struct_parsing_context_stack_;

	// Store parsed explicit template arguments for cross-function access
	// This allows template arguments parsed in one function (e.g., parse_primary_expression)
	// to be accessible in another function (e.g., parse_postfix_expression) for template instantiation
	std::optional<std::vector<TemplateTypeArg>> pending_explicit_template_args_;
	const std::vector<TypeSpecifierNode>* current_explicit_call_arg_types_ = nullptr;

	// Handle-based save/restore to avoid cursor position collisions
	// Each save gets a unique handle from a static incrementing counter
	using SaveHandle = size_t;

	// Delayed function body parsing for inline member functions
	struct DelayedFunctionBody {
		FunctionDeclarationNode* func_node;		// The function node to attach body to
		SaveHandle body_start;				   // Handle to saved position at '{' (or 'try' for function-try-blocks)
		SaveHandle initializer_list_start;	   // Handle to saved position at ':' for constructor initializer list
		StringHandle struct_name;				  // For member function context
		TypeIndex struct_type_index;				 // For member function context
		StructDeclarationNode* struct_node;		// Pointer to struct being parsed
		bool has_initializer_list;			   // True if constructor has an initializer list to re-parse
		bool is_constructor;					 // Special handling for constructors
		bool is_destructor;						// Special handling for destructors
		ConstructorDeclarationNode* ctor_node;   // For constructors (nullptr for regular functions)
		DestructorDeclarationNode* dtor_node;	  // For destructors (nullptr for regular functions)
		InlineVector<StringHandle, 4> template_param_names; // For template member functions
		bool is_member_function_template = false; // True when this is a member function template (template<T> void f())
														  // as opposed to a regular member of a template class
		bool is_free_function = false;			   // True for non-member friend functions defined inside a class body
		bool has_function_try = false;			   // True when body_start is at '{' inside a function-try-block
														  // (i.e. 'try' was already consumed; catch clauses follow the body)
	};
	std::vector<DelayedFunctionBody> delayed_function_bodies_;

	// Deferred template class member function bodies (for two-phase lookup)
	// These are populated when parsing a template class definition and need to be
	// attached to the TemplateClassDeclarationNode for parsing during instantiation
	std::vector<DeferredTemplateMemberBody> pending_template_deferred_bodies_;

	// Template member function body for delayed instantiation
	// This stores the token position and template parameter info for re-parsing
	struct TemplateMemberFunctionBody {
		SaveHandle body_start;						   // Handle to saved position at '{'
		std::vector<std::string_view> template_param_names; // Names of template parameters (e.g., "T", "U") - from Token storage
		FunctionDeclarationNode* template_func_node;		 // The original template function node
	};
	// Map from template function to its body info
	std::unordered_map<FunctionDeclarationNode*, TemplateMemberFunctionBody> template_member_function_bodies_;

	// Track if we're currently parsing a template class (to skip delayed body parsing)
	bool parsing_template_class_ = false;
	// Track when an inline namespace declaration was prefixed with 'inline'
	bool pending_inline_namespace_ = false;
	struct ActiveTemplateParameterState {
		InlineVector<StringHandle, 4> names;	 // Names of current template parameters - from Token storage
		InlineVector<TemplateParameterKind, 4> kinds;

		bool empty() const { return names.empty(); }
		size_t size() const { return names.size(); }

		void clear() {
			names.clear();
			kinds.clear();
		}

		void setNames(const InlineVector<StringHandle, 4>& param_names) {
			names = param_names;
			kinds.clear();
		}

		void setNames(InlineVector<StringHandle, 4>&& param_names) {
			names = std::move(param_names);
			kinds.clear();
		}

		void setNamesAndKinds(const InlineVector<StringHandle, 4>& param_names,
							  const InlineVector<TemplateParameterKind, 4>& param_kinds) {
			names = param_names;
			kinds = param_kinds;
		}

		void setNamesAndKinds(InlineVector<StringHandle, 4>&& param_names,
							  InlineVector<TemplateParameterKind, 4>&& param_kinds) {
			names = std::move(param_names);
			kinds = std::move(param_kinds);
		}

		void pushName(StringHandle param_name) {
			names.push_back(param_name);
		}

		std::optional<TemplateParameterKind> kindOf(StringHandle param_name) const {
			for (size_t i = 0; i < names.size(); ++i) {
				if (names[i] != param_name) {
					continue;
				}
				if (i < kinds.size()) {
					return kinds[i];
				}
				break;
			}
			return std::nullopt;
		}
	};
	ActiveTemplateParameterState current_template_params_;

	// Template parameter substitution for deferred template body parsing
	// Maps template parameter names to their substituted values (for non-type AND type parameters)
	struct TemplateParamSubstitution {
		StringHandle param_name;
		bool is_value_param = false;	 // true for non-type parameters
		int64_t value = 0;			   // For non-type parameters
		TypeCategory value_type = TypeCategory::Void; // Type of the value
		// For type parameters - the concrete type to substitute
		bool is_type_param = false;
		TemplateTypeArg substituted_type;  // The concrete type for type parameters
		// For template template parameters - maps param name to concrete template name
		bool is_template_template_param = false;
		StringHandle concrete_template_name;	 // e.g. "MyVec" when Container=MyVec
	};
	InlineVector<TemplateParamSubstitution, 4> template_param_substitutions_;

	// Track nesting depth of template body parsing (for template parameter reference recognition).
	// A value > 0 means we are inside one or more template definitions.
	// Use FlashCpp::TemplateDepthGuard to increment/decrement; use ScopedState to temporarily
	// suppress (set to 0) during SFINAE and lazy instantiation.
	size_t parsing_template_depth_ = 0;

	// Phase 1 two-phase name lookup enforcement (C++20 [temp.res]/9).
	// Set to the opening-brace line of the template body being re-parsed.
	// Zero means we are NOT currently in a Phase 1 re-parse check.
	size_t phase1_cutoff_line_ = 0;
	size_t phase1_cutoff_file_idx_ = SIZE_MAX;
	std::optional<Token> phase1_violation_token_;

	// Add parsing depth counter to detect infinite loops
	// This is incremented/decremented in critical parsing functions
	size_t parsing_depth_ = 0;
	static constexpr size_t MAX_PARSING_DEPTH = 500;	 // Reasonable limit for nested parsing
	std::vector<std::string_view> template_param_names_;	 // Template parameter names in current scope

	// Track if we're instantiating a lazy member function (to prevent infinite recursion)
	bool instantiating_lazy_member_ = false;

	enum class TemplateInstantiationMode {
		// Ordinary instantiation request: commit cache, symbol-table, and AST/lazy-member side effects.
		HardUse,
		// SFINAE-driven probe: substitution failures are soft and the caller may try another overload.
		// Unlike ShapeOnly, substitution errors in SfinaeProbe cause the probe to soft-fail and the
		// caller may select an alternative overload.  Commit-time side effects are also suppressed.
		SfinaeProbe,
		// Declaration-time shape probe: used when parsing default template arguments and computing
		// type/signature shapes that do not yet require a full, committed instantiation.
		//
		// CONTRACT:
		//   - Compute types and member signatures (layout, return types, overload sets) as normal.
		//   - Suppress ALL commit-time side effects: type-map commits, symbol-table entries,
		//     AST registration, and lazy-member side-effect callbacks.
		//   - Unlike SfinaeProbe, substitution failures are NOT soft-failures: an error in
		//     ShapeOnly mode is still an error.  The mode only controls the commit phase.
		//   - ShapeOnly is STICKY: selectTemplateInstantiationMode() propagates ShapeOnly
		//     downward into nested instantiations so an entire shape-probe subtree stays in
		//     ShapeOnly mode.  The only sites that observe and honour the mode flag are
		//     shouldCommitTemplateInstantiationArtifacts() and selectTemplateInstantiationMode().
		ShapeOnly
	};

	TemplateInstantiationMode template_instantiation_mode_ = TemplateInstantiationMode::HardUse;

	// Parser-level instantiation context: threaded through class-template, function-template,
	// and lazy-member instantiation to carry provenance and support instantiation backtraces.
	// This is distinct from the type-owned InstantiationContext in AstNodeTypes_DeclNodes.h,
	// which stores concrete argument bindings for codegen/constexpr use.
	struct ParserInstantiationContext {
		TemplateInstantiationMode mode = TemplateInstantiationMode::HardUse;
		StringHandle origin_name;             // Name of the template being instantiated
		const ParserInstantiationContext* parent = nullptr; // Enclosing instantiation (for backtraces)
	};

	// RAII guard that pushes a ParserInstantiationContext onto the parser's context stack,
	// sets template_instantiation_mode_, and restores both on destruction.
	// On the first destructor invocation during stack unwinding, captures the instantiation
	// backtrace into g_parser_instantiation_notes for use by catch sites.
	class ScopedParserInstantiationContext {
	public:
		ScopedParserInstantiationContext(Parser& p, TemplateInstantiationMode mode, StringHandle origin)
			: parser_(p),
			  prev_ctx_(p.current_instantiation_ctx_),
			  prev_mode_(p.template_instantiation_mode_) {
			ctx_ = {mode, origin, prev_ctx_};
			parser_.current_instantiation_ctx_ = &ctx_;
			parser_.template_instantiation_mode_ = mode;
		}
		~ScopedParserInstantiationContext() {
			// Capture backtrace on the first destructor invocation during unwinding.
			// std::uncaught_exceptions() > 0 means we're inside an active exception.
			// The empty-check ensures we capture only once (innermost guard first).
			if (std::uncaught_exceptions() > 0 && g_parser_instantiation_notes.empty()) {
				g_parser_instantiation_notes = buildInstantiationNotes(parser_.current_instantiation_ctx_);
			}
			parser_.current_instantiation_ctx_ = prev_ctx_;
			parser_.template_instantiation_mode_ = prev_mode_;
		}
		ScopedParserInstantiationContext(const ScopedParserInstantiationContext&) = delete;
		ScopedParserInstantiationContext& operator=(const ScopedParserInstantiationContext&) = delete;
	private:
		Parser& parser_;
		const ParserInstantiationContext* prev_ctx_;
		TemplateInstantiationMode prev_mode_;
		ParserInstantiationContext ctx_;
	};

	// Format an "in instantiation of …" backtrace from the given context chain.
	// Only includes frames whose origin_name is valid (mode-only guards pass StringHandle{}).
	// Returns empty string if ctx is null or no valid origin is found.
	static std::string buildInstantiationNotes(const ParserInstantiationContext* ctx) {
		if (ctx == nullptr) return {};
		std::string notes;
		const ParserInstantiationContext* c = ctx;
		while (c != nullptr) {
			if (c->origin_name.isValid()) {
				notes += "\n  note: in instantiation of '";
				notes += StringTable::getStringView(c->origin_name);
				notes += "' requested here";
			}
			c = c->parent;
		}
		return notes;
	}

	// SFINAE type substitution map: maps template parameter name handles to concrete type indices.
	// Populated during SFINAE trailing return type re-parse so the expression parser can resolve
	// template parameter types (e.g., U → WithoutFoo) for member access validation.
	std::unordered_map<StringHandle, TypeIndex, StringHash, StringEqual> sfinae_type_map_;

	// Active parser-level instantiation context chain (null when not inside any instantiation).
	// Managed by ScopedParserInstantiationContext RAII guards.
	const ParserInstantiationContext* current_instantiation_ctx_ = nullptr;

	// Last parsed trailing requires clause from caller-specific requires handling
	// skip_function_trailing_specifiers() stops before 'requires' so callers can
	// parse it themselves with proper function parameter scope setup.
	std::optional<ASTNode> last_parsed_requires_clause_;

	// Track if current scope has parameter packs (enables fold expression parsing)
	bool has_parameter_packs_ = false;

	// Track parameter pack expansions during variadic template instantiation
	struct PackParamInfo {
		std::string_view original_name;	// e.g., "rest"
		size_t start_index;				// Index of first expanded param (e.g., rest_0)
		size_t pack_size;				  // Number of expanded elements
	};
	std::vector<PackParamInfo> pack_param_info_;

	// Per-template-parameter-pack sizes set by try_instantiate_template_explicit before
	// reparsing a function body.  Allows substituteTemplateParameters to resolve
	// sizeof...(P) to the correct count even when multiple variadic packs are present
	// (the naive template_args.size()-non_variadic_count formula overcounts in that case).
	// Saved/restored around each instantiation so nesting is safe.
	std::vector<std::pair<StringHandle, size_t>> template_param_pack_sizes_;

	// Track class template parameter pack sizes for sizeof...() in member function templates
	// When a class template like tuple<int, float, double> is instantiated, the pack _Elements
	// has size 3. Member function templates need access to this info when they reference sizeof...(_Elements).
	struct ClassTemplatePackInfo {
		std::string_view pack_name;	// e.g., "_Elements"
		size_t pack_size;			  // e.g., 3 for tuple<int, float, double>
	};
	std::vector<std::vector<ClassTemplatePackInfo>> class_template_pack_stack_;

	// Persistent map: instantiated class name → class template pack sizes
	// This allows member function templates to look up their enclosing class's pack sizes
	std::unordered_map<StringHandle, std::vector<ClassTemplatePackInfo>, TransparentStringHash, std::equal_to<>> class_template_pack_registry_;

	// Get pack size from class template pack context (stack-based, for within instantiation)
	std::optional<size_t> get_class_template_pack_size(std::string_view pack_name) const {
		// First check the stack (active instantiation)
		for (auto it = class_template_pack_stack_.rbegin(); it != class_template_pack_stack_.rend(); ++it) {
			for (const auto& info : *it) {
				if (info.pack_name == pack_name) {
					return info.pack_size;
				}
			}
		}
		// Then check the persistent registry via member function context
		for (auto it = member_function_context_stack_.rbegin(); it != member_function_context_stack_.rend(); ++it) {
			auto reg_it = class_template_pack_registry_.find(it->struct_name);
			if (reg_it != class_template_pack_registry_.end()) {
				for (const auto& info : reg_it->second) {
					if (info.pack_name == pack_name) {
						return info.pack_size;
					}
				}
			}
		}
		// Also check struct_parsing_context_stack_ (for bodies parsed during class instantiation)
		// Note: struct_parsing_context_stack_ entries pushed during template instantiation
		// use the instantiated name (e.g., "tuple$hash"), so the direct lookup here
		// matches the registry key exactly.
		// Two passes: first try exact pack_name match across all entries, then fall back
		// to anonymous pack matching. This prevents an unrelated intermediate class template
		// with an anonymous pack from short-circuiting before the correct entry is reached.
		for (auto it = struct_parsing_context_stack_.rbegin(); it != struct_parsing_context_stack_.rend(); ++it) {
			auto reg_it = class_template_pack_registry_.find(StringTable::getOrInternStringHandle(it->struct_name));
			if (reg_it != class_template_pack_registry_.end()) {
				for (const auto& info : reg_it->second) {
					if (info.pack_name == pack_name) {
						return info.pack_size;
					}
				}
			}
		}
		// Second pass: handle anonymous pack names from forward declarations.
		// Only fall back if exactly one anonymous variadic pack exists in the entry.
		for (auto it = struct_parsing_context_stack_.rbegin(); it != struct_parsing_context_stack_.rend(); ++it) {
			auto reg_it = class_template_pack_registry_.find(StringTable::getOrInternStringHandle(it->struct_name));
			if (reg_it != class_template_pack_registry_.end()) {
				if (reg_it->second.size() == 1 && reg_it->second[0].pack_name.starts_with("__anon_type_")) {
					return reg_it->second[0].pack_size;
				}
			}
		}
		return std::nullopt;
	}

	// RAII guard to push/pop class template pack info
	struct ClassTemplatePackGuard {
		std::vector<std::vector<ClassTemplatePackInfo>>& stack_;
		bool active_ = false;
		ClassTemplatePackGuard(std::vector<std::vector<ClassTemplatePackInfo>>& stack) : stack_(stack) {}
		void push(std::vector<ClassTemplatePackInfo> info) {
			stack_.push_back(std::move(info));
			active_ = true;
		}
		~ClassTemplatePackGuard() {
			if (active_ && !stack_.empty())
				stack_.pop_back();
		}
	};

	// Track last failed template argument parse handle to prevent infinite loops
	SaveHandle last_failed_template_arg_parse_handle_ = SIZE_MAX;

	// Track functions currently undergoing auto return type deduction to prevent infinite recursion
	std::unordered_set<const FunctionDeclarationNode*> functions_being_deduced_;

	// Deferred lambda return type deduction: store lambdas that need return type deduction
	// after the enclosing scope completes to avoid circular dependencies
	struct DeferredLambdaDeduction {
		LambdaExpressionNode* lambda_node;
		ASTNode* return_type_node;  // Pointer to the return type node to update
		Token lambda_token;
	};
	std::vector<DeferredLambdaDeduction> deferred_lambda_deductions_;

	// Stack tracking explicit lambda capture kinds while parsing lambda bodies.
	// Each entry maps a captured variable's StringHandle to its CaptureKind.
	// Pushed before parse_block() and popped after.
	std::vector<std::unordered_map<StringHandle, LambdaCaptureNode::CaptureKind>> lambda_capture_stack_;

	// Track ASTNode addresses currently being processed in get_expression_type to prevent infinite recursion
	mutable std::unordered_set<const void*> expression_type_resolution_stack_;

	// Track template aliases currently being resolved to prevent infinite recursion
	std::unordered_set<std::string_view> resolving_aliases_;

	// Pending variable declarations from struct definitions (e.g., struct Point { ... } p, q;)
	std::vector<ASTNode> pending_struct_variables_;

	// Pending hidden friend function definitions from inline friend bodies inside class/struct.
	// These need to be added to the enclosing namespace's declaration list (or the top-level
	// AST) so the IR converter generates code for them, since they are not regular members.
	std::vector<ASTNode> pending_hidden_friend_defs_;

	template <typename T>
	std::pair<ASTNode, T&> create_node_ref(T&& node) {
		return emplace_node_ref<T>(node);
	}

	template <typename T, typename... Args>
	std::pair<ASTNode, T&> emplace_node_ref(Args&&... args) {
		ASTNode ast_node = ASTNode::emplace_node<T>(std::forward<Args>(args)...);
		return {ast_node, ast_node.as<T>()};
	}

	template <typename T, typename... Args>
	ASTNode emplace_node(Args&&... args) {
		return ASTNode::emplace_node<T>(std::forward<Args>(args)...);
	}

	class ScopedTokenPosition {
	public:
		explicit ScopedTokenPosition(class Parser& parser,
									 const std::source_location location = std::source_location::current());

		~ScopedTokenPosition();

		ParseResult success(ASTNode node = ASTNode{});

		ParseResult error(std::string_view error_message);

				// Propagate result from a sub-parser that has its own ScopedTokenPosition
				// Sub-parsers handle their own position restoration, so we just discard ours
		ParseResult propagate(ParseResult&& result);

	private:
		class Parser& parser_;
		SaveHandle saved_handle_;
		bool discarded_ = false;
		std::source_location location_;
	};

	struct SavedToken {
		Token current_token_;
		Token injected_token_;  // Phase 5: Save injected token state for >> splitting
		size_t ast_nodes_size_ = 0;
		TokenPosition lexer_position_;  // Store the lexer position with each save
	};

	std::unordered_map<SaveHandle, SavedToken> saved_tokens_;
	size_t next_save_handle_ = 0;  // Auto-incrementing handle generator

	Token consume_token();

	Token peek_token();
	Token peek_token(size_t lookahead);	// Peek ahead N tokens (0 = current, 1 = next, etc.)

	// ---- New TokenKind-based API (Phase 0) ----
	// Returns the TokenKind of the current token. Returns TokenKind::eof() at end.
	TokenKind peek() const;
	// Returns the TokenKind of the token at +lookahead positions.
	TokenKind peek(size_t lookahead);
	// Returns the full Token of the current token (always valid, returns EOF token at end).
	const Token& peek_info() const;
	// Like peek(lookahead) but returns full info.
	Token peek_info(size_t lookahead);
	// Consumes the current token and returns it.
	Token advance();
	// Consumes the current token only if it matches `kind`. Returns true if consumed.
	bool consume(TokenKind kind);
	// Consumes the current token if it matches; otherwise emits a diagnostic.
	Token expect(TokenKind kind);

	// >> token splitting for nested templates (e.g., Foo<Bar<int>>)
	// When we encounter >> and need just >, this splits it by consuming first > and injecting second >
	void split_right_shift_token();	// Split >> into > and > (for nested templates)

	// Parsing functions for different constructs
	ParseResult parse_top_level_node();
	ParseResult parse_pragma_pack_inner();   // NEW: Parse the contents of pragma pack()
	ParseResult parse_type_and_name();
	ParseResult parse_structured_binding(CVQualifier cv_qualifiers, ReferenceQualifier ref_qualifier);  // NEW: Parse structured bindings: auto [a, b] = expr; auto& [x, y] = pair;
	ParseResult parse_declarator(TypeSpecifierNode& base_type, Linkage linkage = Linkage::None);	 // NEW: Parse declarators (function pointers, arrays, etc.)
	ParseResult parse_direct_declarator(TypeSpecifierNode& base_type, Token& out_identifier, Linkage linkage);  // NEW: Helper for direct declarators
	ParseResult parse_postfix_declarator(TypeSpecifierNode& base_type, const Token& identifier, Linkage linkage = Linkage::None);  // NEW: Helper for postfix declarators
	// Parse the parameter-type-list inside a function pointer declarator.
	// Used for both standalone function pointer declarators and function declarations
	// whose return type is itself a function pointer.
	ParseResult parse_function_pointer_parameter_types(std::vector<TypeIndex>& out_param_types);
	ParseResult parse_member_function_declarator_result(ParseResult& member_result, FunctionDeclarationNode*& out_func_decl, DeclarationNode*& out_decl);
	ParseResult parse_namespace();
	ParseResult parse_using_directive_or_declaration();	// Parse using directive/declaration/alias
	ParseResult parse_type_specifier();
	ParseResult parse_decltype_specifier();	// NEW: Parse decltype(expr) type specifier

	// Helper function to parse members of anonymous struct/union (handles recursive nesting)
	// Returns the StructMember info for each member parsed
	// out_members: vector to add parsed members to
	// parent_name_prefix: prefix for generating unique anonymous type names
	ParseResult parse_anonymous_struct_union_members(StructTypeInfo* out_struct_info, std::string_view parent_name_prefix);

	// Helper function to try parsing a function pointer member in struct/union context
	// Pattern: type (*name)(params);
	// Returns std::optional<StructMember> - empty if not a function pointer pattern
	// Advances token position if successful, restores on failure
	std::optional<StructMember> try_parse_function_pointer_member(TypeSpecifierNode return_type_spec);

	// Helper function to get Type and size for built-in type keywords
	std::optional<std::pair<TypeCategory, unsigned char>> get_builtin_type_info(std::string_view type_name);

	struct AliasTemplateMaterializationResult;
	// Helper function to parse functional-style cast: Type(expression)
	// Returns ParseResult with StaticCastNode on success
	ParseResult parse_functional_cast(std::string_view type_name, const Token& type_token);
	bool templateArgMatchesCurrentInstantiationSlot(
		const TemplateTypeArg& parsed_arg,
		StringHandle current_param_name,
		const TypeInfo::TemplateArgInfo* concrete_arg) const;
	// May materialize the concrete current-instantiation owner name from active template bindings,
	// so this helper is intentionally non-const.
	std::optional<AliasTemplateMaterializationResult> tryResolveCurrentInstantiationTemplateOwner(
		std::string_view primary_template_name,
		const std::vector<TemplateTypeArg>& template_args);

	// Helper function to parse cv-qualifiers (const/volatile) from token stream
	// Returns combined CVQualifier flags (None, Const, Volatile, or ConstVolatile)
	CVQualifier parse_cv_qualifiers();

	// Helper function to parse reference qualifiers (& or &&) from token stream
	// Returns ReferenceQualifier: None, LValueReference, or RValueReference
	ReferenceQualifier parse_reference_qualifier();

	// Unified declaration parsing
	// This is the single entry point for parsing all declarations (variables and functions)
	// Context determines what forms are legal and how they're interpreted
	ParseResult parse_declaration(FlashCpp::DeclarationContext context = FlashCpp::DeclarationContext::Auto);

	// Legacy functions - now implemented as wrappers around parse_declaration()
	ParseResult parse_declaration_or_function_definition();
	ParseResult parse_out_of_line_constructor_or_destructor(std::string_view class_name, bool is_destructor, const FlashCpp::DeclarationSpecifiers& specs);	// NEW: Parse out-of-line constructor/destructor
	ParseResult parse_function_declaration(DeclarationNode& declaration_node, CallingConvention calling_convention = CallingConvention::Default);
	ParseResult parse_parameter_list(FlashCpp::ParsedParameterList& out_params, CallingConvention calling_convention = CallingConvention::Default);	// Phase 1: Unified parameter list parsing
	FlashCpp::ParsedFunctionArguments parse_function_arguments(const FlashCpp::FunctionArgumentContext& ctx = {});  // Unified function call argument parsing
	std::vector<TypeSpecifierNode> apply_lvalue_reference_deduction(const ChunkedVector<ASTNode>& args, const std::vector<TypeSpecifierNode>& arg_types);  // For template deduction: marks lvalue args with lvalue_reference for T&& forwarding
	struct CppAttributeInfo {
		bool has_no_unique_address = false;
	};
	FlashCpp::MemberLeadingSpecifiers parse_member_leading_specifiers();	 // Consume constexpr/consteval/inline/explicit/virtual before a member
	CppAttributeInfo consume_cpp_attribute_blocks();	 // Consume consecutive [[...]] blocks and return detected flags
	ParseResult parse_function_trailing_specifiers(FlashCpp::MemberQualifiers& out_quals, FlashCpp::FunctionSpecifiers& out_specs);	// Phase 2: Unified trailing specifiers
	ParseResult parse_function_trailing_specifiers(FlashCpp::MemberQualifiers& out_quals, FlashCpp::FunctionSpecifiers& out_specs, const std::vector<ASTNode>& params);
	ParseResult parse_function_header(const FlashCpp::FunctionParsingContext& ctx, FlashCpp::ParsedFunctionHeader& out_header);	// Phase 4: Unified function header parsing
	ParseResult create_function_from_header(const FlashCpp::ParsedFunctionHeader& header, const FlashCpp::FunctionParsingContext& ctx);	// Phase 4: Create FunctionDeclarationNode from header
	void setup_member_function_context(StructDeclarationNode* struct_node, StringHandle struct_name, TypeIndex struct_type_index, bool inject_this);  // Phase 5: Helper for member function scope setup
	void register_member_functions_in_scope(StructDeclarationNode* struct_node, TypeIndex struct_type_index);  // Phase 5: Register member functions in symbol table
	void register_parameters_in_scope(const std::vector<ASTNode>& params);  // Phase 5: Register function parameters in symbol table
	StructMemberFunction* find_member_function_by_signature(StructTypeInfo& struct_info, StringHandle name, const FlashCpp::MemberQualifiers& quals, size_t param_count);  // Priority 4: Lookup regular member function by name, cv-qualifiers, and parameter count
	StructMemberFunction* find_ctor_dtor_for_definition(StructTypeInfo& struct_info, bool is_destructor, const FlashCpp::ParsedParameterList& params);  // Priority 4: Lookup constructor/destructor matching the given parameter list
	ParseResult parse_delayed_function_body(DelayedFunctionBody& delayed, std::optional<ASTNode>& out_body);	 // Phase 5: Unified delayed body parsing
	FlashCpp::SignatureValidationResult validate_signature_match(const FunctionDeclarationNode& declaration, const FunctionDeclarationNode& definition);	 // Phase 7: Unified signature validation
	void copy_function_properties(FunctionDeclarationNode& dest, const FunctionDeclarationNode& src);  // Copy semantic properties needed before signature finalization/mangling
	ASTNode create_defaulted_member_function_body(const FunctionDeclarationNode& func_node);	 // Synthesize parser-owned bodies for defaulted member functions
	void finalize_function_signature_after_definition(FunctionDeclarationNode& func_node);  // Materialize return type and other body-dependent signature data
	void finalize_function_after_definition(FunctionDeclarationNode& func_node, bool force_recompute_mangled_name = false);	// Finalize signature, then mangle
	void compute_and_set_mangled_name(FunctionDeclarationNode& func_node, bool force_recompute = false);	 // Phase 6 (mangling): Generate and set mangled name
	ParseResult parse_struct_declaration();	// Add struct declaration parser (entry point)
	ParseResult parse_struct_declaration_with_specs(bool pre_is_constexpr, bool pre_is_inline);	// With pre-parsed specifiers
	ParseResult parse_member_type_alias(std::string_view keyword, StructDeclarationNode* struct_ref, AccessSpecifier current_access);  // Helper: Parse typedef/using in struct/template
	ParseResult parse_enum_declaration();	  // Add enum declaration parser
	ParseResult parse_typedef_declaration(); // Add typedef declaration parser
	ParseResult parse_static_assert();	   // NEW: Parse static_assert declarations
	ParseResult parse_friend_declaration();	// NEW: Parse friend declarations
	ParseResult parse_template_friend_declaration(StructDeclarationNode& struct_node);  // NEW: Parse template friend declarations
	void registerFriendInStructInfo(const FriendDeclarationNode& friend_decl, StructTypeInfo* struct_info);	// Helper: register friend in StructTypeInfo (all kinds)
	ParseResult parse_template_declaration();  // NEW: Parse template declarations
	ParseResult parse_concept_declaration();	 // NEW: Parse C++20 concept declarations
	ParseResult parse_requires_expression();	 // NEW: Parse C++20 requires expressions
	ParseResult parse_member_function_template(StructDeclarationNode& struct_node, AccessSpecifier access);	// NEW: Parse member function templates
	ParseResult parse_member_template_alias(StructDeclarationNode& struct_node, AccessSpecifier access);	 // NEW: Parse member template aliases
	ParseResult parse_member_struct_template(StructDeclarationNode& struct_node, AccessSpecifier access);  // NEW: Parse member struct/class templates
	ParseResult parse_member_variable_template(StructDeclarationNode& struct_node, AccessSpecifier access);	// NEW: Parse member variable templates
	ParseResult parse_member_template_or_function(StructDeclarationNode& struct_node, AccessSpecifier access);  // Helper: Detect and parse member template alias or function
	StringHandle getStructQualifiedNameForRegistration(const StructDeclarationNode& struct_node) const;
	ParseResult parse_bitfield_width(std::optional<size_t>& out_width, std::optional<ASTNode>* out_expr = nullptr);	// Helper: Parse ': <const-expr>' for bitfields
	// Shared helper for template function declaration parsing
	// Parses: type_and_name + function_declaration + body handling (semicolon or skip braces)
	// Returns the TemplateFunctionDeclarationNode in out_template_node
	ParseResult parse_template_function_declaration_body(
		InlineVector<ASTNode, 4>& template_params,
		std::optional<ASTNode> requires_clause,
		ASTNode& out_template_node);
	ParseResult parse_template_parameter_list(InlineVector<ASTNode, 4>& out_params);	 // NEW: Parse template parameter list
	ParseResult parse_template_parameter();	// NEW: Parse a single template parameter
	TypeInfo& ensureTemplateParameterTypeRegistration(TemplateParameterNode& tparam);
	void registerTemplateTypeParametersInScope(
		InlineVector<ASTNode, 4>& template_params,
		FlashCpp::TemplateParameterScope& template_scope);
	bool parseDeferredAliasTargetTemplateId(
		StringHandle& out_target_template_name,
		std::vector<ASTNode>& out_target_template_arg_nodes,
		bool consume_dependent_member_suffix);
	// Simple struct to hold constant expression evaluation results
	// Public members are intentional for this lightweight data structure
	struct ConstantValue {
		int64_t value;
		TypeCategory type;
	};

	enum TokenDestroyPattern {
		Discard,
		Restore,
	};
	struct TemplateTypeArgParsingResult
	{
	protected:
		std::vector<TemplateTypeArg> template_type_args_;	// use read_template_type_args() or move_template_type_args() to access

	public:
		Parser* parser_ = nullptr;
		SaveHandle original_token_ = 0;
		mutable TokenDestroyPattern destroy_pattern_ = TokenDestroyPattern::Discard;

		TemplateTypeArgParsingResult() = default;

		TemplateTypeArgParsingResult(Parser* p, std::vector<TemplateTypeArg> args, SaveHandle save, TokenDestroyPattern destroy_pattern)
			: template_type_args_(std::move(args)), parser_(p), original_token_(save), destroy_pattern_(destroy_pattern) {}

		TemplateTypeArgParsingResult(TemplateTypeArgParsingResult&& other) noexcept
			: template_type_args_(std::move(other.template_type_args_)), parser_(other.parser_),
			  original_token_(other.original_token_), destroy_pattern_(other.destroy_pattern_) {
			other.original_token_ = 0;  // Prevent destructor from restoring
		}

		TemplateTypeArgParsingResult& operator=(TemplateTypeArgParsingResult&& other) noexcept {
			if (this != &other) {
				parser_ = other.parser_;
				template_type_args_ = std::move(other.template_type_args_);
				original_token_ = other.original_token_;
				destroy_pattern_ = other.destroy_pattern_;
				other.original_token_ = 0;
			}
			return *this;
		}

		void restore_token_position() {
			if (parser_ && original_token_) {
				parser_->restore_token_position(original_token_);
				parser_->discard_saved_token(original_token_);
				original_token_ = 0;
			}
		}

		~TemplateTypeArgParsingResult() {
			if (destroy_pattern_ == Restore) {
				restore_token_position();
			} else if (parser_ && original_token_) {
				parser_->discard_saved_token(original_token_);
			}
		}

		operator bool() const { return (parser_ != nullptr && original_token_ != 0); }
		const std::vector<TemplateTypeArg>& read_template_type_args() const {	// If we use the arguments, we change to discard
			destroy_pattern_ = TokenDestroyPattern::Discard;
			return template_type_args_;
		}
		std::vector<TemplateTypeArg>&& move_template_type_args() {	// If we use the arguments, we change to discard
			destroy_pattern_ = TokenDestroyPattern::Discard;
			return std::move(template_type_args_);
		}
	};

	ParseResult parse_template_template_parameter_forms(std::vector<ASTNode>& out_params);  // NEW: Parse template<template<typename> class T> forms
	ParseResult parse_template_template_parameter_form();  // NEW: Parse single template<template<typename> class T> form
	std::optional<std::vector<TemplateTypeArg>> parse_explicit_template_arguments(std::vector<ASTNode>* out_type_nodes = nullptr);  // NEW: Parse explicit template arguments like <int, float>
	TemplateTypeArgParsingResult parse_explicit_template_arguments_as_result(TokenDestroyPattern destroy_pattern);	// NEW: Lookahead to check if '<' starts template arguments (Phase 1 of C++20 disambiguation)
	ConstructorLookaheadResult consume_constructor_or_destructor_prefix(std::string_view class_name);  // Priority 3: Consume ClassName[<...>]::[~] prefix and detect ClassName( pattern (advances token position)
	ConstructorLookaheadResult lookahead_constructor_or_destructor(std::string_view class_name);	 // Priority 3: Detect ClassName[<...>]::[~]ClassName( pattern with save/restore

		// Phase 2: Unified Qualified Identifier Parser (Sprint 3-4)
	std::optional<QualifiedIdParseResult> parse_qualified_identifier_with_templates();  // NEW: Unified parser for all qualified identifier contexts

	std::optional<ConstantValue> try_evaluate_constant_expression(const ASTNode& expr_node);	 // NEW: Evaluate constant expressions for template arguments (e.g., is_int<T>::value)
	std::optional<ASTNode> try_instantiate_template(std::string_view template_name, const std::vector<TypeSpecifierNode>& arg_types);  // NEW: Try to instantiate a template
	std::optional<ASTNode> try_instantiate_single_template(const ASTNode& template_node, std::string_view template_name, const std::vector<TypeSpecifierNode>& arg_types, int& recursion_depth);	 // Helper: Try to instantiate a specific template node (for SFINAE)
	std::optional<ASTNode> try_instantiate_template_explicit(std::string_view template_name, const std::vector<TemplateTypeArg>& explicit_types, size_t call_arg_count = SIZE_MAX);	// NEW: Instantiate with explicit args
	std::optional<ASTNode> try_instantiate_template_explicit(std::string_view template_name, const std::vector<TemplateTypeArg>& explicit_types, const std::vector<TypeSpecifierNode>& arg_types);
	struct CallArgDeductionInfo {
		std::unordered_map<StringHandle, TemplateTypeArg, StringHash, StringEqual> param_name_to_arg;
		std::unordered_set<size_t> pre_deduced_arg_indices;
		std::vector<size_t> func_param_to_call_arg_index;
		size_t function_pack_call_arg_start = SIZE_MAX;
		size_t function_pack_call_arg_end = SIZE_MAX;
		// Name of the template-parameter pack that the function-parameter pack expands.
		// Used by try_instantiate_template_explicit to gate the call-arg-slice deduction
		// check on the correct template pack (not e.g. a non-type pack that sits in the
		// template parameter list but has no function-parameter counterpart).
		StringHandle function_pack_template_param_name;
		// All template-parameter pack names that appear as dependent positions in the
		// function-parameter pack's element type.  For simple "Ts... args" this is {Ts}.
		// For template-specialisation element types like "Box<Ts>... args" this is {Ts}.
		// For multi-dependent types like "Pair<Ts,Us>... args" this is {Ts, Us}.
		// Used by deduceTemplateArgsFromCall to allow each matching pack param to consume
		// its corresponding position from every pack call argument.
		std::unordered_set<StringHandle, StringHash, StringEqual> function_pack_dependent_param_names;
		// TypeIndex of the function-parameter pack's element type.
		// Set when the pack parameter has a type (e.g. for "Box<Ts>... boxes" this holds
		// the TypeIndex for Box<Ts>).  Used by deduceTemplateArgsFromCall to extract inner
		// template arguments (e.g. Ts→int) from pack call arguments (e.g. Box<int>) rather
		// than deducing the outer type (Box<int>) directly.
		TypeIndex function_pack_element_type_index;
	};
	static void collectDependentTemplateParamNamesFromType(
		TypeIndex pattern_type_index,
		const std::unordered_map<StringHandle, const TemplateParameterNode*, StringHash, StringEqual>&
			tparam_nodes_by_name,
		StringHandle& primary_name,
		std::unordered_set<StringHandle, StringHash, StringEqual>& dependent_param_names);
	static std::optional<TemplateTypeArg> extractNestedTemplateArgForDependentName(
		TypeIndex pattern_type_index,
		TypeIndex concrete_type_index,
		StringHandle dependent_name);
	static std::optional<bool> preDeduceTemplateArgsFromMatchingTypes(
		const TypeSpecifierNode& pattern_type,
		const TypeSpecifierNode& concrete_type,
		const std::unordered_map<StringHandle, const TemplateParameterNode*, StringHash, StringEqual>&
			tparam_nodes_by_name,
		std::unordered_map<StringHandle, TemplateTypeArg, StringHash, StringEqual>& param_name_to_arg,
		int recursion_depth);
	bool tryAppendDefaultTemplateArg(
		const TemplateParameterNode& param,
		const std::vector<ASTNode>& template_params,
		InlineVector<TemplateTypeArg, 4>& template_args,
		NamespaceHandle source_namespace);
	bool tryAppendMemberDefaultTemplateArg(
		const TemplateParameterNode& param,
		const std::vector<ASTNode>& template_params,
		const OuterTemplateBinding* outer_binding,
		InlineVector<TemplateTypeArg, 4>& template_args);
	void appendOuterBindingSubstitutionInputs(
		const OuterTemplateBinding& outer_binding,
		InlineVector<ASTNode, 4>& out_params,
		InlineVector<TemplateTypeArg, 4>& out_args);
	ASTNode substituteNonTypeDefaultExpression(
		const ASTNode& default_node,
		const std::vector<ASTNode>& template_params,
		std::span<const TemplateTypeArg> template_args);
	ASTNode substituteNonTypeDefaultExpression(
		const ASTNode& default_node,
		const InlineVector<ASTNode, 4>& template_params,
		std::span<const TemplateTypeArg> template_args);
	std::optional<TemplateTypeArg> substituteAndEvaluateNonTypeDefault(
		const ASTNode& default_node,
		const std::vector<ASTNode>& template_params,
		std::span<const TemplateTypeArg> template_args,
		std::span<const std::string_view> template_param_names);
	std::optional<TemplateTypeArg> substituteAndEvaluateNonTypeDefault(
		const ASTNode& default_node,
		const InlineVector<ASTNode, 4>& template_params,
		std::span<const TemplateTypeArg> template_args,
		std::span<const std::string_view> template_param_names);
	std::optional<InlineVector<TemplateTypeArg, 4>> deduceTemplateArgsFromCall(
		const std::vector<ASTNode>& template_params,
		const std::vector<TypeSpecifierNode>& arg_types,
		const CallArgDeductionInfo& deduction_info,
		size_t function_pack_arg_start,
		int recursion_depth,
		NamespaceHandle source_namespace);
	// Shared pre-deduction helper for matching function-parameter slots to call-argument
	// types. The returned metadata also carries the canonical function-param → call-arg
	// mapping so deduction sites can reuse one pack-aware view of the call shape.
	std::optional<CallArgDeductionInfo> buildDeductionMapFromCallArgs(
		const std::vector<ASTNode>& template_params,
		const std::vector<ASTNode>& func_params,
		const std::vector<TypeSpecifierNode>& arg_types,
		int recursion_depth);
	std::optional<CallArgDeductionInfo> buildDeductionMapFromCallArgs(
		const std::vector<ASTNode>& template_params,
		const FunctionDeclarationNode& func_decl,
		const std::vector<TypeSpecifierNode>& arg_types,
		int recursion_depth);
	void appendFunctionCallArgType(const ASTNode& arg_node, std::vector<TypeSpecifierNode>* arg_types_out);
	// Shared helper: re-parse a template function body with concrete argument substitution.
	// Called from both try_instantiate_template_explicit (preserve_ref_qualifier=true) and
	// try_instantiate_single_template (preserve_ref_qualifier=false, default) after cycle
	// detection has already passed.  Sets new_func_ref's definition.
	// Pack-parameter state (pack_param_info_, has_parameter_packs_) and cycle detection
	// remain entirely in the callers.  The callers must set pack_param_info_ to the
	// already-expanded pack info before the call and restore it afterwards.
	// This function does not touch pack_param_info_ so that complex pack types such as
	// std::pair<Args,int>... work correctly (the type-name matching done by the old
	// internal rebuild only worked for simple Args... cases).
	void reparse_template_function_body(
		FunctionDeclarationNode& new_func_ref,
		const FunctionDeclarationNode& func_decl,
		const InlineVector<ASTNode, 4>& template_params,
		const InlineVector<TemplateTypeArg, 4>& template_args,
		bool preserve_ref_qualifier = false);
	// Populate template_param_substitutions_ from parallel (name, arg) pairs for
	// body-reparse paths so non-type params (e.g. int N → 4) are resolved in parse_block().
	// Overload 1: TemplateTypeArg source (lazy body-reparse path).
	// Overload 2: TemplateTypeArg source (member-func body-reparse path).
	void populateTemplateParamSubstitutions(
		InlineVector<TemplateParamSubstitution, 4>& subs,
		const InlineVector<StringHandle, 4>& param_names,
		const std::vector<TemplateTypeArg>& type_args);
	void populateTemplateParamSubstitutions(
		InlineVector<TemplateParamSubstitution, 4>& subs,
		const std::vector<ASTNode>& template_params,
		const std::vector<TemplateTypeArg>& template_args);
	// Build outer-template binding data from the AST template parameter list so
	// parameter names and args stay index-aligned even if the parameter list
	// ever stops being a pure TemplateParameterNode sequence.
	template <typename ArgContainer, typename OutArgContainer>
	void collectOuterTemplateBindings(
		const InlineVector<ASTNode, 4>& template_params,
		const ArgContainer& template_args,
		InlineVector<StringHandle, 4>& out_param_names,
		OutArgContainer& out_args) const {
		const size_t pair_count = std::min(template_params.size(), template_args.size());
		out_param_names.clear();
		out_args.clear();
		out_param_names.reserve(pair_count);
		out_args.reserve(pair_count);

		for (size_t i = 0; i < pair_count; ++i) {
			if (!template_params[i].is<TemplateParameterNode>()) {
				continue;
			}

			out_param_names.push_back(template_params[i].as<TemplateParameterNode>().nameHandle());
			out_args.push_back(template_args[i]);
		}
	}
	template <typename ArgContainer>
	void collectOuterTemplateBinding(
		const InlineVector<ASTNode, 4>& template_params,
		const ArgContainer& template_args,
		OuterTemplateBinding& out_binding) const {
		out_binding.param_names.clear();
		out_binding.param_args.clear();
		out_binding.params.clear();
		out_binding.all_args.clear();
		out_binding.param_names.reserve(template_params.size());
		out_binding.param_args.reserve(template_params.size());
		out_binding.params.reserve(template_params.size());
		out_binding.all_args.reserve(template_args.size());

		size_t arg_index = 0;
		for (size_t i = 0; i < template_params.size(); ++i) {
			if (!template_params[i].is<TemplateParameterNode>()) {
				continue;
			}

			const auto& tparam = template_params[i].as<TemplateParameterNode>();
			out_binding.param_names.push_back(tparam.nameHandle());
			out_binding.params.push_back(template_params[i]);

			if (tparam.is_variadic()) {
				size_t remaining_args = arg_index < template_args.size()
										 ? template_args.size() - arg_index
										 : 0;
				size_t required_after = countRequiredTemplateArgsAfter<
					InlineVector<ASTNode, 4>,
					ArgContainer>(template_params, i + 1);
				size_t pack_size = remaining_args > required_after
									 ? remaining_args - required_after
									 : 0;
				// Maintain the 1:1 invariant between param_names/params and
				// param_args that legacy consumers (e.g. the fallback path in
				// appendOuterBindingSubstitutionInputs) rely on. For an empty
				// pack, push a placeholder TemplateTypeArg so indices stay
				// aligned with the variadic parameter entry above.
				if (pack_size > 0) {
					out_binding.param_args.push_back(template_args[arg_index]);
				} else {
					out_binding.param_args.push_back(TemplateTypeArg());
				}
				for (size_t pack_index = 0; pack_index < pack_size && (arg_index + pack_index) < template_args.size(); ++pack_index) {
					out_binding.all_args.push_back(template_args[arg_index + pack_index]);
				}
				arg_index += pack_size;
				continue;
			}

			if (arg_index >= template_args.size()) {
				continue;
			}

			out_binding.param_args.push_back(template_args[arg_index]);
			out_binding.all_args.push_back(template_args[arg_index]);
			++arg_index;
		}
	}
	template <typename NodeT, typename ArgContainer>
	void setOuterTemplateBindingsFromParams(
		NodeT& node,
		const InlineVector<ASTNode, 4>& template_params,
		const ArgContainer& template_args) {
		InlineVector<StringHandle, 4> outer_template_param_names;
		std::vector<std::decay_t<decltype(template_args[0])>> filtered_template_args;
		collectOuterTemplateBindings(template_params, template_args, outer_template_param_names, filtered_template_args);

		if (!outer_template_param_names.empty()) {
			node.set_outer_template_bindings(outer_template_param_names, filtered_template_args);
		}
	}
	template <typename TemplateParamsContainer, typename TemplateArgsContainer>
	StringHandle resolveBaseInitializerNameForTemplateArgs(
		StringHandle base_name,
		const TemplateParamsContainer& template_params,
		const TemplateArgsContainer& template_args) {
		for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
			if (!template_params[i].template is<TemplateParameterNode>()) {
				continue;
			}

			const TemplateParameterNode& param = template_params[i].template as<TemplateParameterNode>();
			if (param.kind() != TemplateParameterKind::Type || param.nameHandle() != base_name) {
				continue;
			}

			const TemplateTypeArg& concrete_arg = template_args[i];
			if (concrete_arg.is_value || !concrete_arg.type_index.is_valid()) {
				break;
			}

			const TypeInfo* base_type_info = tryGetTypeInfo(concrete_arg.type_index);
			if (!base_type_info) {
				break;
			}

			if (base_type_info->isTemplateInstantiation() &&
				(!base_type_info->getStructInfo() || !base_type_info->getStructInfo()->total_size.is_set())) {
				std::string_view base_template_name = StringTable::getStringView(base_type_info->baseTemplateName());
				std::vector<TemplateTypeArg> concrete_base_args =
					materializeTemplateArgs(*base_type_info, template_params, template_args,
						[this](const ASTNode& expr, std::span<const ASTNode> params, std::span<const TemplateTypeArg> args) {
							return this->evaluateDependentNTTPExpression(expr, params, args);
						});
				auto instantiated_base = try_instantiate_class_template(base_template_name, concrete_base_args);
				if (instantiated_base.has_value() && instantiated_base->is<StructDeclarationNode>()) {
					registerAndNormalizeLateMaterializedTopLevelNode(*instantiated_base);
				}
				std::string_view instantiated_base_name =
					get_instantiated_class_name(base_template_name, concrete_base_args);
				auto instantiated_base_it =
					getTypesByNameMap().find(StringTable::getOrInternStringHandle(instantiated_base_name));
				if (instantiated_base_it != getTypesByNameMap().end()) {
					return instantiated_base_it->second->name();
				}
			}

			return base_type_info->name();
		}

		return base_name;
	}
	template <typename TemplateParamsContainer, typename TemplateArgsContainer>
	std::vector<TemplateTypeArg> materializePlaceholderTemplateArgs(
		const TypeInfo& placeholder_info,
		const TemplateParamsContainer& template_params,
		const TemplateArgsContainer& template_args) {
		auto appendExpandedPackArgs =
			[&](const TypeInfo::TemplateArgInfo& arg_info,
				std::vector<TemplateTypeArg>& out_args) -> bool {
			if (!arg_info.dependent_name.isValid()) {
				return false;
			}

			size_t arg_index = 0;
			for (size_t i = 0; i < template_params.size(); ++i) {
				if (!template_params[i].template is<TemplateParameterNode>()) {
					continue;
				}

				const auto& param =
					template_params[i].template as<TemplateParameterNode>();
				if (param.is_variadic()) {
					size_t remaining_args = arg_index < template_args.size()
						? template_args.size() - arg_index
						: 0;
					size_t required_after =
						countRequiredTemplateArgsAfter<
							TemplateParamsContainer,
							TemplateArgsContainer>(template_params, i + 1);
					size_t pack_size = remaining_args > required_after
						? remaining_args - required_after
						: 0;

					if (param.nameHandle() == arg_info.dependent_name) {
						for (size_t pack_index = 0;
							 pack_index < pack_size &&
							 (arg_index + pack_index) < template_args.size();
							 ++pack_index) {
							out_args.push_back(rebindDependentTemplateTypeArg(
								template_args[arg_index + pack_index],
								arg_info));
						}
						return true;
					}

					arg_index += pack_size;
					continue;
				}

				if (arg_index >= template_args.size()) {
					break;
				}
				++arg_index;
			}

			return false;
		};

		std::vector<TemplateTypeArg> concrete_args;
		concrete_args.reserve(placeholder_info.templateArgs().size());
		for (const auto& arg_info : placeholder_info.templateArgs()) {
			if (appendExpandedPackArgs(arg_info, concrete_args)) {
				continue;
			}
			concrete_args.push_back(
				materializeTemplateArg(arg_info, template_params, template_args,
					[this](const ASTNode& expr, std::span<const ASTNode> params, std::span<const TemplateTypeArg> args) {
						return this->evaluateDependentNTTPExpression(expr, params, args);
					}));
		}

		const auto resolveConcreteBaseHandle = [&](std::string_view base_name) -> StringHandle {
			auto base_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(base_name));
			if (base_it == getTypesByNameMap().end() || base_it->second == nullptr) {
				return {};
			}

			const TypeInfo* base_info = base_it->second;
			if (!base_info->isTemplateInstantiation()) {
				return base_info->name();
			}

			std::string_view nested_template_name =
				StringTable::getStringView(base_info->baseTemplateName());
			if (nested_template_name.empty()) {
				return base_info->name();
			}

			std::vector<TemplateTypeArg> nested_args =
				materializeTemplateArgs(*base_info, template_params, template_args,
					[this](const ASTNode& expr, std::span<const ASTNode> params, std::span<const TemplateTypeArg> args) {
						return this->evaluateDependentNTTPExpression(expr, params, args);
					});
			try_instantiate_class_template(nested_template_name, nested_args);
			std::string_view instantiated_nested_name =
				get_instantiated_class_name(nested_template_name, nested_args);
			if (instantiated_nested_name.empty()) {
				return base_info->name();
			}

			return StringTable::getOrInternStringHandle(instantiated_nested_name);
		};

		for (auto& concrete_arg : concrete_args) {
			if (!concrete_arg.dependent_name.isValid()) {
				continue;
			}

			std::string_view dep_name = StringTable::getStringView(concrete_arg.dependent_name);
			size_t scope_pos = dep_name.rfind("::");
			if (scope_pos == std::string_view::npos) {
				continue;
			}

			StringHandle concrete_base_handle =
				resolveConcreteBaseHandle(dep_name.substr(0, scope_pos));
			if (!concrete_base_handle.isValid()) {
				continue;
			}

			std::string_view member_name = dep_name.substr(scope_pos + 2);
			Token member_token(Token::Type::Identifier, member_name, 0, 0, 0);
			NamespaceHandle member_ns = gNamespaceRegistry.getOrCreateNamespace(
				NamespaceRegistry::GLOBAL_NAMESPACE,
				concrete_base_handle);
			QualifiedIdentifierNode& member_qual_id =
				gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(member_ns, member_token);
			ExpressionNode& member_expr =
				gChunkedAnyStorage.emplace_back<ExpressionNode>(member_qual_id);
			if (auto value = try_evaluate_constant_expression(ASTNode(&member_expr))) {
				concrete_arg.is_value = true;
				concrete_arg.is_dependent = false;
				concrete_arg.dependent_name = {};
				concrete_arg.type_index = nativeTypeIndex(value->type);
				concrete_arg.value = value->value;
				continue;
			}

			auto concrete_type_it = getTypesByNameMap().find(concrete_base_handle);
			if (concrete_type_it == getTypesByNameMap().end() || concrete_type_it->second == nullptr) {
				continue;
			}

			const TypeInfo* concrete_type_info = concrete_type_it->second;
			if (concrete_type_info->isTemplateInstantiation()) {
				std::string_view base_template_name =
					StringTable::getStringView(concrete_type_info->baseTemplateName());
				if (!base_template_name.empty()) {
					std::vector<TemplateTypeArg> exact_args =
						materializeTemplateArgs(*concrete_type_info, template_params, template_args,
							[this](const ASTNode& expr, std::span<const ASTNode> params, std::span<const TemplateTypeArg> args) {
								return this->evaluateDependentNTTPExpression(expr, params, args);
							});
					auto specialization_ast =
						gTemplateRegistry.lookupExactSpecialization(base_template_name, exact_args);
					if (!specialization_ast.has_value()) {
						specialization_ast =
							gTemplateRegistry.matchSpecializationPattern(base_template_name, exact_args);
					}
					if (specialization_ast.has_value() && specialization_ast->is<StructDeclarationNode>()) {
						const auto& specialization_struct =
							specialization_ast->as<StructDeclarationNode>();
						for (const auto& static_member_decl : specialization_struct.static_members()) {
							if (StringTable::getStringView(static_member_decl.name) != member_name ||
								!static_member_decl.initializer.has_value()) {
								continue;
							}
							if (auto value =
									try_evaluate_constant_expression(*static_member_decl.initializer)) {
								concrete_arg.is_value = true;
								concrete_arg.is_dependent = false;
								concrete_arg.dependent_name = {};
								concrete_arg.type_index = nativeTypeIndex(value->type);
								concrete_arg.value = value->value;
								break;
							}
						}
						if (!concrete_arg.is_dependent) {
							continue;
						}
					}
				}
			}

			const StructTypeInfo* concrete_struct = concrete_type_info->getStructInfo();
			if (!concrete_struct) {
				continue;
			}
			auto [static_member, owner_struct] = concrete_struct->findStaticMemberRecursive(
				StringTable::getOrInternStringHandle(member_name));
			(void)owner_struct;
			if (!static_member || !static_member->initializer.has_value()) {
				continue;
			}
			if (auto value = try_evaluate_constant_expression(*static_member->initializer)) {
				concrete_arg.is_value = true;
				concrete_arg.is_dependent = false;
				concrete_arg.dependent_name = {};
				concrete_arg.type_index = nativeTypeIndex(value->type);
				concrete_arg.value = value->value;
			}
		}

		return concrete_args;
	}
	template <typename TemplateParamsContainer, typename TemplateArgsContainer, typename ConcreteBaseInstantiator>
	const TypeInfo* materializeDeferredBasePlaceholderIfNeeded(
		const TypeInfo* base_type,
		const TemplateParamsContainer& template_params,
		const TemplateArgsContainer& template_args,
		ConcreteBaseInstantiator&& instantiate_concrete_base) {
		if (base_type == nullptr ||
			!base_type->is_incomplete_instantiation_ ||
			!base_type->isTemplateInstantiation()) {
			return base_type;
		}

		std::vector<TemplateTypeArg> concrete_base_args =
			materializePlaceholderTemplateArgs(*base_type, template_params, template_args);
		std::string_view concrete_base_template_name =
			StringTable::getStringView(base_type->baseTemplateName());
		std::string_view concrete_base_name =
			instantiate_concrete_base(concrete_base_template_name, concrete_base_args);
		auto concrete_base_it = getTypesByNameMap().find(
			StringTable::getOrInternStringHandle(concrete_base_name));
		if (concrete_base_it != getTypesByNameMap().end()) {
			return concrete_base_it->second;
		}

		return base_type;
	}
	bool isReachableVirtualBaseInitializer(const StructTypeInfo* struct_info, std::string_view candidate_name) const;
	std::optional<ASTNode> try_instantiate_class_template(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args, bool force_eager = false);	// NEW: Instantiate class template
	std::optional<ASTNode> instantiate_full_specialization(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args, ASTNode& spec_node);  // Instantiate full specialization
	std::optional<ASTNode> try_instantiate_variable_template(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args);	 // NEW: Instantiate variable template
	std::optional<TemplateTypeArg> materializeDeferredAliasTemplateArg(
		const ASTNode& arg_node,
		const InlineVector<ASTNode, 4>& template_parameters,
		const InlineVector<StringHandle, 4>& param_names,
		const std::vector<TemplateTypeArg>& template_args);
	std::optional<std::vector<TemplateTypeArg>> materializeDeferredAliasTemplateArgs(
		const TemplateAliasNode& alias_node,
		const std::vector<TemplateTypeArg>& template_args);
	// Substitute template parameters in an expression.
	// substitution_owner: when valid, member aliases of that instantiation are resolved for sizeof/alignof.
	ASTNode substitute_template_params_in_expression(
		const ASTNode& expr,
		const std::unordered_map<TypeIndex, TemplateTypeArg>& type_substitution_map,
		const std::unordered_map<std::string_view, int64_t>& nontype_substitution_map,
		StringHandle substitution_owner);
	// Helper: resolve sizeof(member_alias) for a given substitution owner.
	// Returns a concrete sizeof(resolved_type) node, or nullopt if not found.
	std::optional<ASTNode> tryResolveSizeofMemberAlias(
		StringHandle substitution_owner,
		std::string_view type_name,
		const Token& sizeof_token);
	// Evaluate a dependent NTTP expression (e.g., sizeof(T), alignof(T)) with concrete template arguments.
	// Builds substitution maps, substitutes the expression, then evaluates via ConstExpr::Evaluator.
	// Returns the evaluated integer value, or nullopt if evaluation fails.
	std::optional<int64_t> evaluateDependentNTTPExpression(
		const ASTNode& dependent_expr,
		std::span<const ASTNode> template_params,
		std::span<const TemplateTypeArg> template_args);
	std::optional<ASTNode> try_instantiate_member_function_template(std::string_view struct_name, std::string_view member_name, const std::vector<TypeSpecifierNode>& arg_types);  // NEW: Instantiate member function template
	std::optional<ASTNode> try_instantiate_member_function_template_explicit(std::string_view struct_name, std::string_view member_name, const std::vector<TemplateTypeArg>& template_type_args);  // NEW: Instantiate member function template with explicit args
		// Core logic shared by both try_instantiate_member_function_template and _explicit.
		// Given a resolved template node and template arguments, performs type substitution,
		// body parsing, scope management, and AST registration.
	std::optional<ASTNode> instantiate_member_function_template_core(
		std::string_view struct_name, std::string_view member_name,
		StringHandle qualified_name,
		const ASTNode& template_node,
		const std::vector<TemplateTypeArg>& template_args,
		const FlashCpp::TemplateInstantiationKey& key,
		const std::vector<TypeSpecifierNode>& call_arg_types);
	bool buildSubstitutionForPackElement(
		StringHandle pack_param_name,
		size_t pack_element_offset,
		const std::unordered_set<StringHandle, StringHash, StringEqual>& dependent_pack_names,
		const InlineVector<ASTNode, 4>& template_params,
		const std::vector<size_t>& template_param_arg_starts,
		const std::vector<size_t>& template_param_arg_counts,
		const std::vector<TemplateTypeArg>& template_args,
		InlineVector<ASTNode, 4>& subst_params,
		InlineVector<TemplateTypeArg, 4>& subst_args);
	ASTNode buildMaterializedParamType(
		const TypeSpecifierNode& original_param_type,
		const InlineVector<ASTNode, 4>& materialized_template_params,
		const InlineVector<TemplateTypeArg, 4>& materialized_template_args);
	bool enqueuePendingSemanticRoot(const ASTNode& node) {
		if (!node.has_value()) {
			return false;
		}

		const void* root_key = node.raw_pointer();
		if (!root_key) {
			return false;
		}

		auto [_, inserted] = pending_semantic_root_keys_.insert(root_key);
		if (!inserted) {
			return false;
		}

		pending_semantic_roots_.push_back(node);
		return true;
	}
	// Returns true when the current instantiation mode permits committing artifacts.
	// "Artifacts" are any globally-visible side effects produced during instantiation:
	//   - type-map commits (getTypesByNameMap() insertions)
	//   - symbol-table entries (gSymbolTable insertions)
	//   - AST registration (registerLateMaterializedTopLevelNode calls)
	//   - lazy-member side-effect callbacks
	// In ShapeOnly mode all of these must be suppressed so that repeated shape probes
	// remain idempotent and do not pollute the global compiler state.
	bool shouldCommitTemplateInstantiationArtifacts() const {
		return template_instantiation_mode_ != TemplateInstantiationMode::ShapeOnly;
	}
	// Select the instantiation mode to use for a nested instantiation triggered from
	// the current context.
	//
	// ShapeOnly is STICKY: once the outer instantiation is in ShapeOnly mode, every
	// nested instantiation it triggers must also stay in ShapeOnly mode, regardless of
	// the outer_sfinae_context flag.  This ensures that an entire shape-probe subtree
	// (e.g. default-template-argument parsing) never commits artifacts to global state.
	//
	// Outside of ShapeOnly mode, the mode is determined by the caller's SFINAE context:
	// outer_sfinae_context == true  → SfinaeProbe (soft substitution failures)
	// outer_sfinae_context == false → HardUse (full commit)
	TemplateInstantiationMode selectTemplateInstantiationMode(bool outer_sfinae_context) const {
		if (template_instantiation_mode_ == TemplateInstantiationMode::ShapeOnly) {
			return TemplateInstantiationMode::ShapeOnly;
		}
		if (outer_sfinae_context) {
			return TemplateInstantiationMode::SfinaeProbe;
		}
		return TemplateInstantiationMode::HardUse;
	}
	std::optional<ASTNode> lookupLateMaterializedOwningStructRoot(StringHandle struct_name) const {
		std::string_view struct_name_view = StringTable::getStringView(struct_name);
		if (struct_name_view.empty()) {
			return std::nullopt;
		}

		const auto as_struct_root = [](const std::optional<ASTNode>& symbol) -> std::optional<ASTNode> {
			if (symbol.has_value() && symbol->is<StructDeclarationNode>()) {
				return symbol;
			}
			return std::nullopt;
		};

		const auto find_nested_struct = [](const ASTNode& root,
											 const std::vector<std::string_view>& components,
											 size_t start_index) -> std::optional<ASTNode> {
			ASTNode current = root;
			for (size_t i = start_index; i < components.size(); ++i) {
				if (!current.is<StructDeclarationNode>()) {
					return std::nullopt;
				}

				std::optional<ASTNode> next_nested;
				const auto& struct_decl = current.as<StructDeclarationNode>();
				StringHandle nested_name_handle = StringTable::getOrInternStringHandle(components[i]);
				for (const ASTNode& nested_node : struct_decl.nested_classes()) {
					if (!nested_node.is<StructDeclarationNode>()) {
						continue;
					}

					if (nested_node.as<StructDeclarationNode>().name() == nested_name_handle) {
						next_nested = nested_node;
						break;
					}
				}

				if (!next_nested.has_value()) {
					return std::nullopt;
				}

				current = *next_nested;
			}

			return current;
		};

		const std::vector<std::string_view> components = splitQualifiedNamespace(struct_name_view);
		if (components.empty()) {
			return std::nullopt;
		}

		for (size_t namespace_component_count = components.size() - 1;;) {
			NamespaceHandle namespace_handle = NamespaceRegistry::GLOBAL_NAMESPACE;
			bool namespace_path_valid = true;
			for (size_t i = 0; i < namespace_component_count; ++i) {
				namespace_handle = gNamespaceRegistry.lookupNamespace(
					namespace_handle,
					StringTable::getOrInternStringHandle(components[i]));
				if (!namespace_handle.isValid()) {
					namespace_path_valid = false;
					break;
				}
			}

			if (namespace_path_valid && namespace_component_count < components.size()) {
				if (auto root_symbol = as_struct_root(
						gSymbolTable.lookup_qualified(namespace_handle, components[namespace_component_count]));
					root_symbol.has_value()) {
					if (namespace_component_count + 1 == components.size()) {
						return root_symbol;
					}

					if (auto nested_symbol = find_nested_struct(
							*root_symbol,
							components,
							namespace_component_count + 1);
						nested_symbol.has_value()) {
						return nested_symbol;
					}
				}
			}

			if (namespace_component_count == 0) {
				break;
			}
			--namespace_component_count;
		}

		StringHandle root_name_handle = StringTable::getOrInternStringHandle(components[0]);
		for (const ASTNode& root_node : ast_nodes_) {
			if (!root_node.is<StructDeclarationNode>()) {
				continue;
			}
			if (root_node.as<StructDeclarationNode>().name() != root_name_handle) {
				continue;
			}
			if (components.size() == 1) {
				return root_node;
			}
			if (auto nested_symbol = find_nested_struct(root_node, components, 1);
				nested_symbol.has_value()) {
				return nested_symbol;
			}
		}

		return std::nullopt;
	}

public:
	// ====================================================================
	// Late Materialization Lifecycle Contract (Phase 3)
	// ====================================================================
	//
	// When template instantiation creates a new AST root (class, function,
	// variable, etc.), it must flow through the following lifecycle:
	//
	//   1. **Materialize** - Create the AST node via emplace_node<T> or similar.
	//   2. **Register** - Call registerLateMaterializedTopLevelNode() to:
	//      - Add the node to ast_nodes_ (so codegen can find it)
	//      - Enqueue it in pending_semantic_roots_ (so sema can normalize it)
	//   3. **Normalize** - Call normalizePendingSemanticRootsIfAvailable() to:
	//      - Let SemanticAnalysis process all pending roots
	//      - Resolve forward declarations, auto types, etc.
	//
	// **Single-Node Instantiation** (immediate lookup needs normalized result):
	//   Call `registerAndNormalizeLateMaterializedTopLevelNode(node)` which
	//   combines registration and normalization in one step.
	//
	// **Batched Instantiation** (multiple members of same class):
	//   Call `registerLateMaterializedTopLevelNode(node)` for each member,
	//   then call `normalizePendingSemanticRootsIfAvailable()` once at the
	//   end. This is more efficient than normalizing after each node.
	//
	// **Callers of parser materialization helpers** (from codegen/constexpr):
	//   If the helper returns a newly-instantiated node but doesn't normalize,
	//   the caller must call the appropriate normalize function:
	//   - Parser callers: normalizePendingSemanticRootsIfAvailable()
	//   - AstToIr callers: normalizePendingSemanticRoots()
	//   - ConstExpr callers: context.normalizePendingSemanticRoots()
	//
	// This contract ensures that late-materialized templates are normalized
	// consistently, regardless of whether they were triggered by parser
	// lookup, constexpr evaluation, or codegen-side lazy generation.
	// ====================================================================

	/// Append a user-written (non-instantiated) top-level AST node. Maintains
	/// the parallel `ast_node_is_instantiated_` vector in lockstep. Callers
	/// that currently use `ast_nodes_.push_back(...)` during initial parsing
	/// should migrate to this helper.
	void appendUserNode(const ASTNode& node) {
		ast_nodes_.push_back(node);
		ast_node_is_instantiated_.push_back(0);
	}

	/// Erase the top-level AST node at `index`, keeping the parallel
	/// instantiation-tag vector in sync. Used by token-save rollback.
	void eraseTopLevelNodeAt(size_t index) {
		if (index >= ast_nodes_.size()) {
			return;
		}
		if (const void* key = ast_nodes_[index].raw_pointer()) {
			instantiated_node_keys_.erase(key);
		}
		ast_nodes_.erase(ast_nodes_.begin() + static_cast<std::ptrdiff_t>(index));
		if (index < ast_node_is_instantiated_.size()) {
			ast_node_is_instantiated_.erase(
				ast_node_is_instantiated_.begin() + static_cast<std::ptrdiff_t>(index));
		}
	}

	/// Register a late-materialized AST node for codegen and sema processing.
	/// Does NOT normalize immediately - use for batched registration.
	/// After batched registration, call normalizePendingSemanticRootsIfAvailable().
	///
	/// Idempotent: if `node` has already been registered as a late-materialized
	/// top-level node (same ASTNode::raw_pointer()), this is a no-op. Multiple
	/// instantiation paths (partial-spec success, primary-template success,
	/// base-class helper recursion) may reach the same freshly-created struct;
	/// silently double-pushing would produce LNK2005 at codegen time.
	void registerLateMaterializedTopLevelNode(const ASTNode& node) {
		if (const void* key = node.raw_pointer()) {
			auto [_, inserted] = instantiated_node_keys_.insert(key);
			if (!inserted) {
				// Still make sure sema sees it (enqueuePendingSemanticRoot is
				// itself dedup'd, so this is safe).
				enqueuePendingSemanticRoot(node);
				return;
			}
		}
		ast_nodes_.push_back(node);
		ast_node_is_instantiated_.push_back(1);
		enqueuePendingSemanticRoot(node);
	}

	/// Register a late-materialized AST node at the front of the node list.
	/// Does NOT normalize immediately - use for dependencies that must be processed first.
	/// Idempotent by raw_pointer() identity (see registerLateMaterializedTopLevelNode).
	void registerLateMaterializedTopLevelNodeFront(const ASTNode& node) {
		if (const void* key = node.raw_pointer()) {
			auto [_, inserted] = instantiated_node_keys_.insert(key);
			if (!inserted) {
				enqueuePendingSemanticRoot(node);
				return;
			}
		}
		ast_nodes_.insert(ast_nodes_.begin(), node);
		ast_node_is_instantiated_.insert(ast_node_is_instantiated_.begin(), 1);
		enqueuePendingSemanticRoot(node);
	}

	/// Register AND normalize a single late-materialized AST node.
	/// Use this when the node must be immediately visible to subsequent lookups.
	/// This is the preferred entry point for single-node instantiation.
	void registerAndNormalizeLateMaterializedTopLevelNode(const ASTNode& node) {
		registerLateMaterializedTopLevelNode(node);
		normalizePendingSemanticRootsIfAvailable();
	}

	/// Register AND normalize a single late-materialized AST node at the front.
	/// Use for dependencies that must be processed first and immediately visible.
	void registerAndNormalizeLateMaterializedTopLevelNodeFront(const ASTNode& node) {
		registerLateMaterializedTopLevelNodeFront(node);
		normalizePendingSemanticRootsIfAvailable();
	}
	void registerLateMaterializedOwningStructRoot(StringHandle struct_name) {
		if (!struct_name.isValid()) {
			return;
		}

		if (auto symbol = lookupLateMaterializedOwningStructRoot(struct_name); symbol.has_value()) {
			enqueuePendingSemanticRoot(*symbol);
		}
	}
	std::optional<ASTNode> instantiateLazyMemberFunction(const LazyMemberFunctionInfo& lazy_info);  // NEW: Instantiate lazy member function on-demand
	std::optional<ASTNode> try_instantiate_constructor_template(StringHandle instantiated_struct_name, const ConstructorDeclarationNode& ctor_decl, const std::vector<TypeSpecifierNode>& arg_types);
	const ConstructorDeclarationNode* materializeMatchingConstructorTemplate(
		StringHandle instantiated_struct_name,
		const StructTypeInfo& struct_info,
		const std::vector<TypeSpecifierNode>& arg_types,
		const ConstructorDeclarationNode* preferred_ctor,
		bool& is_ambiguous);
	bool instantiateLazyStaticMember(StringHandle instantiated_class_name, StringHandle member_name);  // NEW: Instantiate lazy static member on-demand
private:
	bool instantiateLazyClassToPhase(StringHandle instantiated_name, ClassInstantiationPhase target_phase);	// Phase 2: Instantiate lazy class to specified phase
	std::optional<TypeIndex> evaluateLazyTypeAlias(StringHandle instantiated_class_name, StringHandle member_name);	// Phase 3: Evaluate lazy type alias on-demand
	std::optional<TypeIndex> instantiateLazyNestedType(StringHandle parent_class_name, StringHandle nested_type_name);  // Phase 4: Instantiate lazy nested type on-demand
	struct AliasTemplateMaterializationResult {
		std::string_view instantiated_name{};
		const TypeInfo* resolved_type_info = nullptr;
		std::optional<ASTNode> instantiated_struct_node{};  // Set when try_instantiate_class_template returns a StructDeclarationNode
	};
	std::string_view get_instantiated_class_name(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args);	 // NEW: Get mangled name for instantiated class
	std::string_view instantiate_and_register_base_template(std::string_view& base_class_name, const std::vector<TemplateTypeArg>& template_args);  // Helper: Instantiate base class template and add to AST
	AliasTemplateMaterializationResult materializeTemplateInstantiationForLookup(
		std::string_view template_name,
		const std::vector<TemplateTypeArg>& template_args);
	AliasTemplateMaterializationResult materializePrimaryTemplateOwnerForLookup(
		std::string_view primary_template_name,
		std::string_view fallback_template_name,
		const std::vector<TemplateTypeArg>& template_args);
	AliasTemplateMaterializationResult materializePrimaryTemplateOwnerForConstructorLookup(
		std::string_view primary_template_name,
		std::string_view fallback_template_name,
		const std::vector<TemplateTypeArg>& template_args);
	ParseResult parseMaterializedTemplateFunctionalCast(
		const AliasTemplateMaterializationResult& materialized_owner,
		const Token& source_token);
	const TypeInfo* materializeInstantiatedMemberAliasTarget(
		const TypeSpecifierNode& alias_type_spec,
		TypeIndex fallback_type_index,
		const InlineVector<ASTNode, 4>& template_params,
		const std::vector<TemplateTypeArg>& template_args);
	AliasTemplateMaterializationResult materializeAliasTemplateInstantiation(
		std::string_view alias_template_name,
		const std::vector<TemplateTypeArg>& template_args);
	void normalizeDependentNonTypeTemplateArgs(
		const InlineVector<ASTNode, 4>& template_parameters,
		std::vector<TemplateTypeArg>& template_args);
	bool resolveAliasTemplateInstantiation(
		TypeSpecifierNode& type_spec,
		std::string_view alias_template_name,
		const std::vector<TemplateTypeArg>& template_args);
	bool resolveAliasTemplateInstantiation(TypeSpecifierNode& type_spec);
	// Shared alias-target capture helper: parses the template-id that forms the
	// right-hand side of an alias declaration from the current token position.
	// Skips leading cv-qualifiers, optional `typename`, and an optional global
	// `::` prefix, then parses a possibly-qualified identifier followed by any
	// `<template-args>`. Populates `out_args` with the parsed argument nodes,
	// `out_concrete_args` with the evaluated `TemplateTypeArg` values, and sets
	// `out_has_template_args` to true when a `<...>` argument list was present
	// (even if empty). Returns an interned handle for the parsed name (invalid
	// handle if no identifier is found). Does NOT restore the token position;
	// callers must save/restore if needed.
	StringHandle parseRawAliasTargetTemplateId(
		std::vector<ASTNode>& out_args,
		std::vector<TemplateTypeArg>& out_concrete_args,
		bool& out_has_template_args);
	StringHandle parseRawAliasTargetTemplateId(std::vector<ASTNode>& out_args, bool& out_has_template_args);

		// Template name extraction helpers - extract base template names from mangled/instantiated names
	std::string_view extract_base_template_name(std::string_view mangled_name);	// Extract by searching for underscores left-to-right
	std::string_view extract_base_template_name_by_stripping(std::string_view instantiated_name);  // Extract by stripping suffixes right-to-left

		// Template instantiation helper methods (extracted from try_instantiate_class_template)
	std::optional<ASTNode> substitute_nontype_template_param(
		std::string_view param_name,
		const std::vector<TemplateTypeArg>& args,
		const std::vector<ASTNode>& params);	 // Substitute non-type template parameter in initializer

	std::optional<bool> try_parse_out_of_line_template_member(const InlineVector<ASTNode, 4>& template_params, const InlineVector<StringHandle, 4>& template_param_names, const InlineVector<ASTNode, 4>& inner_template_params = {}, const InlineVector<StringHandle, 4>& inner_template_param_names = {});	 // NEW: Parse out-of-line template member function
	bool try_apply_deduction_guides(TypeSpecifierNode& type_specifier, const InitializerListNode& init_list);
	bool deduce_template_arguments_from_guide(const DeductionGuideNode& guide,
											  const std::vector<TypeSpecifierNode>& argument_types,
											  std::vector<TemplateTypeArg>& out_template_args) const;
	bool match_template_parameter_type(TypeSpecifierNode param_type,
									   TypeSpecifierNode argument_type,
									   const std::unordered_map<std::string_view, const TemplateParameterNode*>& template_params,
									   std::unordered_map<std::string_view, TypeSpecifierNode>& bindings) const;
	std::optional<std::string_view> extract_template_param_name(const TypeSpecifierNode& type_spec,
																const std::unordered_map<std::string_view, const TemplateParameterNode*>& template_params) const;
	bool types_equivalent(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) const;
	bool instantiate_deduced_template(std::string_view class_name,
									  const std::vector<TemplateTypeArg>& template_args,
									  TypeSpecifierNode& type_specifier);

public:	// Public methods for template instantiation
	// Parse a template function body with concrete type bindings (for template instantiation)
	std::optional<ASTNode> parseTemplateBody(
		SaveHandle body_pos,
		const InlineVector<std::string_view, 4>& template_param_names,
		const std::vector<TypeCategory>& concrete_types,
		StringHandle struct_name,  // Optional: for member functions
		TypeIndex struct_type_index = TypeIndex{}	  // Optional: for member functions
	);

	// Substitute template parameters in an AST node with concrete types/values
	ASTNode substituteTemplateParameters(
		const ASTNode& node,
		const InlineVector<ASTNode, 4>& template_params,
		const InlineVector<TemplateTypeArg, 4>& template_args);

	// Helper to extract type from an expression for overload resolution.
	// Public so codegen/constexpr consumers can reuse the parser's type deduction.
	std::optional<TypeSpecifierNode> get_expression_type(const ASTNode& expr_node);

	// Returns the current parser-level instantiation context chain (null when not inside any instantiation).
	// The chain is linked via ParserInstantiationContext::parent for backtrace traversal.
	const ParserInstantiationContext* currentInstantiationContext() const { return current_instantiation_ctx_; }

private:	 // Resume private methods


	void register_builtin_functions();  // Register compiler builtin functions
	ParseResult parse_block();
	ParseResult parse_statement_or_declaration();
	ParseResult parse_variable_declaration();
	FlashCpp::DeclarationSpecifiers parse_declaration_specifiers();	//  Shared specifier parsing
	bool looks_like_function_parameters();  // Detect if '(' starts function params vs direct init
	bool looks_like_elaborated_type_variable_declaration();  // Disambiguate 'struct Foo f = ...' from 'struct Foo { ... }'

	std::optional<ASTNode> parse_direct_initialization();  // Parse Type var(args) - returns initializer node
	std::optional<ASTNode> parse_copy_initialization(DeclarationNode& decl_node, TypeSpecifierNode& type_specifier);	 // Parse Type var = expr or Type var = {args}
	void prepareArrayTypeForBraceInitializer(const DeclarationNode& decl_node, TypeSpecifierNode& type_specifier);
	void inferUnsizedArraySizeFromInitializer(const DeclarationNode& decl_node,
											 TypeSpecifierNode& type_specifier,
											 const std::optional<ASTNode>& initializer);
	ParseResult parse_extern_block(Linkage linkage);	 // Parse extern "C" { ... } block
	ParseResult parse_brace_initializer_clause_list(std::vector<ASTNode>& elements,
													const TypeSpecifierNode* nested_type_specifier,
													bool allow_nested_braces);
	std::optional<TypeSpecifierNode> get_initializer_list_element_type_spec(const TypeSpecifierNode& type_specifier,
																			const Token& token) const;
	ParseResult parse_brace_initializer(const TypeSpecifierNode& type_specifier);  // Add brace initializer parser
	static bool isFoldOperatorToken(std::string_view op);
	ParseResult parse_for_loop();
	ParseResult parse_while_loop();
	ParseResult parse_do_while_loop();
	ParseResult parse_if_statement();
	ParseResult parse_switch_statement();
	ParseResult parse_return_statement();
	ParseResult parse_break_statement();
	ParseResult parse_continue_statement();
	ParseResult parse_goto_statement();
	ParseResult parse_label_statement();
	ParseResult parse_lambda_expression();
	ParseResult parse_try_statement();
	ParseResult parse_throw_statement();
	ParseResult parse_function_body(bool is_ctor_or_dtor = false);  // Parse function body: '{...}' or function-try-block 'try {...} catch...'
	// Parse one catch clause at the current token position into catch_clauses.
	// Returns an error ParseResult on failure, or an empty success otherwise.
	ParseResult parse_one_catch_clause(std::vector<ASTNode>& catch_clauses);
	// Wrap try_body + catch_clauses into a BlockNode containing a single TryStatementNode.
	// Both parse_function_body() and parse_delayed_function_body() use this.
	// Set is_ctor_or_dtor=true for constructor/destructor function-try-blocks so that the IR
	// generator can emit the C++20 [except.handle]/15 implicit rethrow at each handler end.
	ASTNode make_try_block_body(ASTNode try_body, std::vector<ASTNode> catch_clauses, Token try_token, bool is_ctor_or_dtor = false);

	// Windows SEH (Structured Exception Handling) parsers
	ParseResult parse_seh_try_statement();  // Parse __try/__except or __try/__finally
	ParseResult parse_seh_leave_statement();	 // Parse __leave statement

	// Helper functions for auto type deduction
	TypeCategory deduce_type_from_expression(const ASTNode& expr);
	void deduce_and_update_auto_return_type(FunctionDeclarationNode& func_decl);
	std::optional<TypeSpecifierNode> deduce_lambda_return_type(const LambdaExpressionNode& lambda);
	std::optional<TypeSpecifierNode> build_function_pointer_type_from_lambda(const LambdaExpressionNode& lambda);
	std::optional<TypeSpecifierNode> build_function_pointer_type_from_struct(const StructTypeInfo& struct_info, const Token& source_token);
	void process_deferred_lambda_deductions();  // Process deferred lambda return type deductions
	bool are_types_compatible(const TypeSpecifierNode& type1, const TypeSpecifierNode& type2) const;	 // Check if two types are compatible
	std::string type_to_string(const TypeSpecifierNode& type) const;	 // Convert type to string for error messages
	// Note: Use global ::get_type_size_bits() from AstNodeTypes.h for type sizes
	int getStructTypeSizeBits(TypeIndex type_index) const;

	// Helper functions for std::initializer_list support
	// Check if a type is std::initializer_list<T>, returns element type index if so
	std::optional<TypeIndex> is_initializer_list_type(const TypeSpecifierNode& type_spec) const;
	// Find a constructor that takes std::initializer_list<T> as its parameter
	std::optional<std::pair<const StructMemberFunction*, TypeIndex>>
	find_initializer_list_constructor(const StructTypeInfo& struct_info) const;

	// Helper function for counting pack elements in template parameter packs
	size_t count_pack_elements(std::string_view pack_name) const;

	// Get pack size from pack_param_info_ (more reliable than count_pack_elements during nested instantiation)
	// Returns std::nullopt if pack name is not found (unknown pack)
	std::optional<size_t> get_pack_size(std::string_view pack_name) const {
		for (const auto& info : pack_param_info_) {
			if (info.original_name == pack_name) {
				return info.pack_size;
			}
		}
		return std::nullopt;
	}

	// Get the per-template-parameter-pack element count set by try_instantiate_template_explicit.
	// This is the authoritative count for sizeof...(P) when multiple packs are present,
	// since the naive "total_args - non_variadic_count" fallback overcounts in that case.
	std::optional<size_t> get_template_param_pack_size(std::string_view pack_name) const {
		for (const auto& [handle, count] : template_param_pack_sizes_) {
			if (StringTable::getStringView(handle) == pack_name) {
				return count;
			}
		}
		return std::nullopt;
	}

	std::vector<ASTNode> expandPackExpressionArgument(const ASTNode& pattern);

	// Replace a pack parameter identifier in an expression pattern with its expanded name
	// e.g., replace "args" with "args_0" in a pattern like identity(args)
	ASTNode replacePackIdentifierInExpr(const ASTNode& expr, std::string_view pack_name, size_t element_index);

	// Expand a PackExpansionExprNode into substituted call arguments.
	// Returns true when the node was recognized and consumed; empty packs
	// legitimately contribute zero arguments.
	bool expandPackExpansionArgs(
		const PackExpansionExprNode& pack_expansion,
		const InlineVector<ASTNode, 4>& template_params,
		const InlineVector<TemplateTypeArg, 4>& template_args,
		ChunkedVector<ASTNode>& out_args);

		// Substitute a single argument, expanding PackExpansionExprNode into
		// multiple arguments when present.  Falls back to substituteTemplateParameters
		// for non-pack arguments.  Appends result(s) to `out`.
	void substituteArgWithPackExpansion(
		const ASTNode& arg,
		const InlineVector<ASTNode, 4>& template_params,
		const InlineVector<TemplateTypeArg, 4>& template_args,
		ChunkedVector<ASTNode>& out);

		// Phase 3: Expression context tracking for template disambiguation
	enum class ExpressionContext {
		Normal,				// Regular expression context
		Decltype,			  // Inside decltype() - strictest template-first rules
		TemplateTypeArg,	 // Template argument context
		RequiresClause,		// Requires clause expression
		ConceptDefinition	  // Concept definition context
	};

	// Minimum precedence to accept all operators (assignment has lowest precedence = 3)
	static constexpr int MIN_PRECEDENCE = 0;
	// Default precedence excludes comma operator (precedence 1) to prevent it from being
	// treated as an operator in contexts where it's a separator (declarations, arguments, etc.)
	static constexpr int DEFAULT_PRECEDENCE = 2;
	// NOTE: ExpressionContext is required (no default) to prevent bugs where context
	// is accidentally not passed in recursive calls (e.g., ternary branch parsing).
	// Use ExpressionContext::Normal for most cases; TemplateTypeArg when inside <...>.
	ParseResult parse_expression(int precedence, ExpressionContext context);
	ParseResult parse_expression_statement() { return parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal); }	 // Wrapper for keyword map
	ParseResult parse_primary_expression(ExpressionContext context);
	ParseResult parse_postfix_expression(ExpressionContext context);	 // Phase 3: New postfix operator layer
	ParseResult apply_postfix_operators(ASTNode& result);  // Apply postfix operators (., ->, [], (), ++, --) to existing result
	std::optional<ASTNode> try_synthesize_atomic_builtin_overload(std::string_view builtin_name, const std::vector<TypeSpecifierNode>& arg_types, const Token& call_token);
	const FunctionDeclarationNode* tryResolveConcreteMemberFunction(const std::optional<ASTNode>& object_expr, std::string_view member_name);
	std::optional<ASTNode> tryResolveMemberFunctionTemplate(const std::optional<ASTNode>& object_expr, std::string_view member_name,
															const std::optional<std::vector<TemplateTypeArg>>& explicit_template_args, const std::vector<TypeSpecifierNode>& arg_types);
	ParseResult parse_member_postfix(std::optional<ASTNode>& result, const Token& operator_start_token);
	ParseResult parse_unary_expression(ExpressionContext context);
	ParseResult parse_qualified_operator_call(const Token& context_token, const std::vector<StringType<32>>& namespaces);  // Parse operator symbol + call after 'operator' keyword consumed
	// Shared helper: parse operator symbol/name after the 'operator' keyword has been consumed.
	// Handles all operator forms: symbols (+, =, <<, etc.), (), [], new/delete, user-defined literals, and conversion operators.
	// On success returns std::nullopt and sets operator_name_out; on error returns a ParseResult with the error.
	std::optional<ParseResult> parse_operator_name(const Token& operator_keyword_token, std::string_view& operator_name_out);

	// C++ cast operators helper
	enum class CppCastKind {
		Static,
		Dynamic,
		Const,
		Reinterpret
	};
	ParseResult parse_cpp_cast_expression(CppCastKind kind, std::string_view cast_name, const Token& cast_token);

	ParseResult parse_qualified_identifier_after_template(const Token& template_base_token, bool* had_template_keyword = nullptr);  // Parse Template<T>::member

	// Helper to parse template brace initialization: Template<Args>{}
	// Returns ParseResult with ConstructorCallNode on success
	ParseResult parse_template_brace_initialization(
		const std::vector<TemplateTypeArg>& template_args,
		std::string_view template_name,
		const Token& identifier_token);

	// Helper to parse member template function calls: Template<T>::member<U>()
	// Returns:
	// - std::nullopt if not a function call (no '(' found after member name)
	// - ParseResult with success if function call was parsed successfully
	// - ParseResult with error if parsing failed
	std::optional<ParseResult> try_parse_member_template_function_call(
		std::string_view instantiated_class_name,
		std::string_view member_name,
		const Token& member_token);

	// Utility functions
	bool consume_punctuator(const std::string_view& value);
	bool consume_keyword(const std::string_view& value);

	// Attribute parsing result
	struct AttributeInfo {
		Linkage linkage = Linkage::None;
		CallingConvention calling_convention = CallingConvention::Default;
	};

	// Attribute handling
	void skip_cpp_attributes();					// Skip C++ standard [[...]] attributes
	CppAttributeInfo skip_cpp_attributes_with_info();	 // Skip C++ standard [[...]] attributes and return detected flags
	void skip_gcc_attributes();					// Skip GCC __attribute__((...)) specifications
	void skip_noop_gnu_qualifiers();				 // Skip GNU-style no-op qualifiers like __restrict
	bool skip_asm_suffix(std::optional<std::string_view>* asm_symbol_name = nullptr); // Skip declaration-suffix __asm("...") / __asm__("...")
	void parse_variable_declarator_suffixes(DeclarationNode& decl);
	void skip_noexcept_specifier();				// Skip noexcept or noexcept(expr) specifier
	bool parse_noexcept_value();				// Parse noexcept or noexcept(expr), returning evaluated bool
	void skip_function_trailing_specifiers(FlashCpp::MemberQualifiers& out_quals);	   // Skip all trailing specifiers after function parameters (stops before 'requires')
	void skip_trailing_requires_clause();		  // Parse and discard trailing requires clause (if present)
	std::optional<ASTNode> parse_trailing_requires_clause();	 // Parse trailing requires clause, return RequiresClauseNode
	bool parse_constructor_exception_specifier(); // Parse noexcept or throw() and return true if noexcept
	void consume_conversion_operator_target_modifiers(TypeSpecifierNode& target_type);  // Consume *, &, && after conversion operator target type
	void consume_pointer_ref_modifiers(TypeSpecifierNode& type_spec);  // Consume trailing *, &, && and apply to type specifier
	// Parse trailing return type (-> type) with the given parameters visible for decltype expressions.
	// Expects the '->' token to be the next token. Consumes it, registers params in a temporary scope,
	// calls parse_type_specifier + consume_pointer_ref_modifiers, then pops the scope.
	// Returns ParseResult::error on failure, or success with a TypeSpecifierNode.
	ParseResult parse_trailing_return_type_with_params(const std::vector<ASTNode>& params);
	ParseResult parse_member_trailing_return_type(FunctionDeclarationNode& func_decl);

	// Helper to parse static member functions (reduces code duplication)
	bool parse_static_member_function(
		ParseResult& type_and_name_result,
		bool is_static_constexpr,
		StringHandle struct_name_handle,
		StructDeclarationNode& struct_ref,
		StructTypeInfo* struct_info,
		AccessSpecifier current_access,
		const InlineVector<StringHandle, 4>& current_template_param_names,
		bool add_to_struct_info,
		bool add_to_ast_nodes);

	// Helper to parse entire static member block (data or function) - reduces code duplication
	ParseResult parse_static_member_block(
		StringHandle struct_name_handle,
		StructDeclarationNode& struct_ref,
		StructTypeInfo* struct_info,
		AccessSpecifier current_access,
		const InlineVector<StringHandle, 4>& current_template_param_names,
		bool use_struct_type_info,
		bool add_functions_to_ast_nodes);

	Linkage parse_declspec_attributes();			 // Parse Microsoft __declspec(...) and return linkage
	AttributeInfo parse_attributes();			  // Parse all types of attributes and return linkage + calling convention
	[[nodiscard]] CallingConvention parse_calling_convention(CallingConvention calling_conv); // Parse calling convention keywords

	// Helper to build __PRETTY_FUNCTION__ signature from FunctionDeclarationNode
	std::string buildPrettyFunctionSignature(const FunctionDeclarationNode& func_node) const;
	int get_operator_precedence(const std::string_view& op);
	std::optional<size_t> parse_alignas_specifier();	 // Parse alignas(n) and return alignment value

	// Check if an identifier name is a template parameter in current scope
	bool is_template_parameter(std::string_view name) const;

	// Check if a base class name is a template parameter (used for template parameter inheritance)
	bool is_base_class_template_parameter(std::string_view base_class_name) const;

	// Helper: Validate and add a base class (consolidates lookup, validation, and registration)
	// Returns ParseResult::success() on success, or error if validation fails
	ParseResult validate_and_add_base_class(
		std::string_view base_class_name,
		StructDeclarationNode& struct_ref,
		StructTypeInfo* struct_info,
		AccessSpecifier base_access,
		bool is_virtual_base,
		const Token& error_token);

	// Helper: After parsing template arguments for a base class specifier, consume
	// optional ::member type access and ... pack expansion in the correct order.
	// Centralizes the parsing so all call sites get consistent behavior.
	// Returns std::nullopt if '::' is found but not followed by an identifier (parse error).
	std::optional<BaseClassPostTemplateInfo> consume_base_class_qualifiers_after_template_args();
	const TypeInfo* resolveBaseClassMemberTypeChain(
		std::string_view base_class_name,
		const std::vector<QualifiedTypeMemberAccess>& member_type_chain);

	// Helper: Build TemplateArgumentNodeInfo vector from parsed template args and AST nodes.
	// Shared across all base class deferral sites.
	static std::vector<TemplateArgumentNodeInfo> build_template_arg_infos(
		const std::vector<TemplateTypeArg>& template_args,
		const std::vector<ASTNode>& template_arg_nodes);

	// Helper: Parse base class list for a member struct template (already consumed ':').
	// All base classes are stored as deferred entries since the struct is inside a template body.
	ParseResult parse_member_struct_template_base_class_list(
		StructDeclarationNode& struct_ref,
		bool is_class);

	// Helper: Parse an optional access specifier keyword (public/protected/private) at the
	// current position. If one is present it is consumed and the out parameter is updated.
	// Returns true if an access specifier was consumed, false otherwise.
	bool parse_base_access_specifier(AccessSpecifier& out_access) {
		if (!peek().is_keyword())
			return false;
		std::string_view kw = peek_info().value();
		if (kw == "public") {
			out_access = AccessSpecifier::Public;
			advance();
			return true;
		}
		if (kw == "protected") {
			out_access = AccessSpecifier::Protected;
			advance();
			return true;
		}
		if (kw == "private") {
			out_access = AccessSpecifier::Private;
			advance();
			return true;
		}
		return false;
	}

	// Helper: Look up a type alias including inherited ones from base classes
	// Returns the TypeInfo pointer if found, nullptr otherwise
	// Uses depth limit to prevent infinite recursion in malformed input
	const TypeInfo* lookup_inherited_type_alias(StringHandle struct_name, StringHandle member_name, int depth = 0);
	// Convenience overload for string_view parameters
	const TypeInfo* lookup_inherited_type_alias(std::string_view struct_name, std::string_view member_name, int depth = 0) {
		return lookup_inherited_type_alias(
			StringTable::getOrInternStringHandle(struct_name),
			StringTable::getOrInternStringHandle(member_name),
			depth);
	}

	// Helper: Look up a template function including inherited ones from base classes
	// Returns the vector of all template overloads if found, nullptr otherwise
	// Uses depth limit to prevent infinite recursion in malformed input
	const std::vector<ASTNode>* lookup_inherited_template(StringHandle struct_name, std::string_view template_name, int depth = 0);
	// Convenience overload for string_view parameters
	const std::vector<ASTNode>* lookup_inherited_template(std::string_view struct_name, std::string_view template_name, int depth = 0) {
		return lookup_inherited_template(
			StringTable::getOrInternStringHandle(struct_name),
			template_name,
			depth);
	}

	// Substitute template parameter in a type specification
	// Handles complex transformations like const T& -> const int&, T* -> int*, etc.
	TypeIndex substitute_template_parameter(
		const TypeSpecifierNode& original_type,
		const InlineVector<ASTNode, 4>& template_params,
		const InlineVector<TemplateTypeArg, 4>& template_args);

	// Lookup symbol with template parameter checking
	std::optional<ASTNode> lookup_symbol_with_template_check(StringHandle identifier);

	bool hasActiveTemplateParameters() const {
		return !current_template_params_.empty();
	}

	bool isTemplateBodyWithActiveParameters() const {
		return parsing_template_depth_ > 0 && hasActiveTemplateParameters();
	}

	bool isTemplateParameterTrackingActive() const {
		return parsing_template_depth_ > 0 || hasActiveTemplateParameters();
	}

	bool isTemplateClassOrActiveParameters() const {
		return parsing_template_class_ || hasActiveTemplateParameters();
	}

	bool isDependentTemplateContext() const {
		return parsing_template_class_ || isTemplateParameterTrackingActive();
	}

	const InlineVector<StringHandle, 4>& currentTemplateParamNames() const {
		return current_template_params_.names;
	}

	size_t currentTemplateParamCount() const {
		return current_template_params_.size();
	}

	std::optional<TemplateParameterKind> currentTemplateParamKind(StringHandle param_name) const {
		return current_template_params_.kindOf(param_name);
	}

	void setCurrentTemplateParamNames(const InlineVector<StringHandle, 4>& param_names) {
		current_template_params_.setNames(param_names);
	}

	void setCurrentTemplateParamNames(InlineVector<StringHandle, 4>&& param_names) {
		current_template_params_.setNames(std::move(param_names));
	}

	void setCurrentTemplateParameters(const InlineVector<StringHandle, 4>& param_names,
									  const InlineVector<TemplateParameterKind, 4>& param_kinds) {
		current_template_params_.setNamesAndKinds(param_names, param_kinds);
	}

	void setCurrentTemplateParameters(InlineVector<StringHandle, 4>&& param_names,
									  InlineVector<TemplateParameterKind, 4>&& param_kinds) {
		current_template_params_.setNamesAndKinds(std::move(param_names), std::move(param_kinds));
	}

	void clearCurrentTemplateParameters() {
		current_template_params_.clear();
	}

	void pushCurrentTemplateParamName(StringHandle param_name) {
		current_template_params_.pushName(param_name);
	}

	ActiveTemplateParameterState& currentTemplateParamState() {
		return current_template_params_;
	}

	const ActiveTemplateParameterState& currentTemplateParamState() const {
		return current_template_params_;
	}

	// Unified symbol lookup that automatically provides template parameters when parsing templates
	std::optional<ASTNode> lookup_symbol(StringHandle identifier) const {
		if (isTemplateBodyWithActiveParameters()) {
			return gSymbolTable.lookup(identifier, gSymbolTable.get_current_scope_handle(), &currentTemplateParamNames());
		} else {
			return gSymbolTable.lookup(identifier);
		}
	}

	// If an argument is a plain identifier naming an array declaration, propagate its
	// declared array bounds into the provided type specifier so overload resolution and
	// template deduction can observe the real extent information.
	void applyIdentifierArgumentArrayBounds(const ASTNode& arg_node, TypeSpecifierNode& arg_type_node) const;

	// Check if a template name is a template-template parameter in the current template context
	bool isTemplateTemplateParameter(StringHandle template_name_handle) const;

	// Build an interned placeholder name for TTP instantiation.
	StringHandle buildTTPPlaceholderName(std::string_view ttp_name, const std::vector<TemplateTypeArg>& template_args);

	// Create a QualifiedIdentifierNode for a TTP-qualified expression like W<int>::id
	ParseResult buildTTPQualifiedIdentifier(StringHandle ttp_placeholder_name);

	// Overload for qualified lookups with vector of strings
	std::optional<ASTNode> lookup_symbol_qualified(const std::vector<StringType<>>& namespaces, std::string_view identifier) const {
		if (isTemplateBodyWithActiveParameters()) {
				// For qualified lookups, we still need template params for the base lookup
				// But qualified lookups are less common in template bodies
			return gSymbolTable.lookup_qualified(namespaces, identifier);
		} else {
			return gSymbolTable.lookup_qualified(namespaces, identifier);
		}
	}

		// Overload for qualified lookups with NamespaceHandle
	std::optional<ASTNode> lookup_symbol_qualified(NamespaceHandle namespace_handle, std::string_view identifier) const {
		return gSymbolTable.lookup_qualified(namespace_handle, identifier);
	}

		// Helper to get DeclarationNode from a symbol that could be either DeclarationNode or VariableDeclarationNode
		// Returns nullptr if the symbol is neither type
	static const DeclarationNode* get_decl_from_symbol(const ASTNode& symbol) {
		if (symbol.is<DeclarationNode>()) {
			return &symbol.as<DeclarationNode>();
		} else if (symbol.is<VariableDeclarationNode>()) {
			return &symbol.as<VariableDeclarationNode>().declaration();
		}
		return nullptr;
	}

		// Create an IdentifierNode and perform ordinary unqualified lookup to classify its binding.
		// Non-const because it may call instantiateLazyStaticMember.
		// Returns Unresolved when the name cannot be classified (template-dependent names,
		// 'this', unknown identifiers, etc.). Safe to call for any token; the Unresolved
		// fallback preserves all existing codegen behaviour.
	IdentifierNode createBoundIdentifier(Token token) {
		IdentifierNode node(token);

			// In template bodies with active template parameters, names may be dependent.
		auto sym = lookup_symbol(token.handle());

			// Lambda capture bindings: check lambda_capture_stack_ for explicit captures
		if (sym.has_value() && sym->is<DeclarationNode>()) {
			auto scope_type = gSymbolTable.get_scope_type_of_symbol(token.value());
			bool is_local = scope_type.has_value() &&
							scope_type != ScopeType::Global && scope_type != ScopeType::Namespace;
			if (is_local && !lambda_capture_stack_.empty()) {
				auto cap_it = lambda_capture_stack_.back().find(token.handle());
				if (cap_it != lambda_capture_stack_.back().end()) {
					if (cap_it->second == LambdaCaptureNode::CaptureKind::ByValue)
						node.set_binding(IdentifierBinding::CapturedByValue);
					else if (cap_it->second == LambdaCaptureNode::CaptureKind::ByReference)
						node.set_binding(IdentifierBinding::CapturedByRef);
					return node;
				}
			}
		}
		if (sym.has_value() && sym->is<VariableDeclarationNode>()) {
			const auto& var_decl = sym->as<VariableDeclarationNode>();
			auto scope_type = gSymbolTable.get_scope_type_of_symbol(token.value());
			bool is_local = scope_type.has_value() &&
							scope_type != ScopeType::Global && scope_type != ScopeType::Namespace;
			if (is_local && var_decl.storage_class() != StorageClass::Static && !lambda_capture_stack_.empty()) {
				auto cap_it = lambda_capture_stack_.back().find(token.handle());
				if (cap_it != lambda_capture_stack_.back().end()) {
					if (cap_it->second == LambdaCaptureNode::CaptureKind::ByValue)
						node.set_binding(IdentifierBinding::CapturedByValue);
					else if (cap_it->second == LambdaCaptureNode::CaptureKind::ByReference)
						node.set_binding(IdentifierBinding::CapturedByRef);
					return node;
				}
			}
		}

			// Helper lambdas for member context binding
		auto bindStaticMemberFromStructInfo = [&](const StructTypeInfo* struct_info) -> bool {
			if (!struct_info)
				return false;
			StringHandle member_handle = token.handle();
			instantiateLazyStaticMember(struct_info->name, member_handle);
			auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_handle);
			if (!static_member || !owner_struct)
				return false;
			bool is_template_member_context = isTemplateBodyWithActiveParameters();
			if (is_template_member_context && owner_struct != struct_info)
				return false;
			node.set_binding(IdentifierBinding::StaticMember);
			node.set_resolved_name(StringTable::getOrInternStringHandle(
				StringBuilder().append(owner_struct->getName()).append("::"sv).append(member_handle).commit()));
			return true;
		};

		auto bindNonStaticMemberFromContext = [&](TypeIndex struct_type_index, const StructTypeInfo* local_struct_info) -> bool {
			const StructTypeInfo* current_struct_info = local_struct_info;
			if (!current_struct_info) {
				if (const TypeInfo* ti = tryGetTypeInfo(struct_type_index))
					current_struct_info = ti->getStructInfo();
			}
			if (!current_struct_info)
				return false;
			bool is_template_member_context = isTemplateBodyWithActiveParameters();
				// When parsing a template body and the struct has dependent (deferred) base classes,
				// skip gLazyMemberResolver which traverses concrete base classes — a dependent base
				// may also provide the same name, making eager binding to a concrete base incorrect
				// (C++20 [temp.res]/2: dependent names must not be bound at first-phase parse time).
				// Only own-declared members are safe to bind eagerly; names inherited from concrete
				// bases stay Unresolved so codegen's runtime lookup handles them at instantiation.
			bool skip_base_traversal = is_template_member_context && current_struct_info->has_deferred_base_classes;
			if (!skip_base_traversal && tryGetTypeInfo(struct_type_index)) {
				auto member_result = FlashCpp::gLazyMemberResolver.resolve(struct_type_index, token.handle());
				if (!member_result)
					return false;
				if (is_template_member_context && member_result.owner_struct != current_struct_info)
					return false;
				node.set_binding(IdentifierBinding::NonStaticMember);
				return true;
			}
			for (const auto& member : current_struct_info->members) {
				if (member.getName() == token.handle()) {
					node.set_binding(IdentifierBinding::NonStaticMember);
					return true;
				}
			}
			return false;
		};

		auto tryBindMemberContext = [&]() -> bool {
			if (!member_function_context_stack_.empty()) {
				const auto& member_ctx = member_function_context_stack_.back();
				const StructTypeInfo* struct_info = member_ctx.local_struct_info;
				if (!struct_info) {
					if (const TypeInfo* ti = tryGetTypeInfo(member_ctx.struct_type_index))
						struct_info = ti->getStructInfo();
				}
				if (bindStaticMemberFromStructInfo(struct_info))
					return true;
				if (member_ctx.has_implicit_this &&
					bindNonStaticMemberFromContext(member_ctx.struct_type_index, member_ctx.local_struct_info))
					return true;
			}
			if (!struct_parsing_context_stack_.empty()) {
				const auto& struct_ctx = struct_parsing_context_stack_.back();
				const StructTypeInfo* struct_info = struct_ctx.local_struct_info;
				if (!struct_info) {
					auto struct_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(struct_ctx.struct_name));
					if (struct_it != getTypesByNameMap().end()) {
						struct_info = struct_it->second->getStructInfo();
					}
				}
				if (bindStaticMemberFromStructInfo(struct_info))
					return true;
				TypeIndex struct_type_index{};
				if (!struct_ctx.struct_name.empty()) {
					auto struct_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(struct_ctx.struct_name));
					if (struct_it != getTypesByNameMap().end()) {
						struct_type_index = struct_it->second->type_index_;
					}
				}
				if (bindNonStaticMemberFromContext(struct_type_index, struct_ctx.local_struct_info))
					return true;
			}
			return false;
		};

		if (!sym.has_value()) {
				// Not found in symbol table -> check member context
			if (tryBindMemberContext())
				return node;
			return node; // Unresolved
		}

			// Template parameter reference
		if (sym->is<TemplateParameterReferenceNode>()) {
			node.set_binding(IdentifierBinding::TemplateParameter);
			return node;
		}

			// Helper: record a Phase 1 violation if this declaration was added after
			// the template body opening brace (C++20 [temp.res]/9).
		auto checkPhase1 = [&](const Token& decl_tok) {
			if (phase1_cutoff_line_ > 0 && !phase1_violation_token_.has_value() &&
				decl_tok.file_index() == phase1_cutoff_file_idx_) {
				// Use source line numbers for the comparison, not preprocessed line numbers.
				// Preprocessed line numbers can be misleading when a header is included multiple
				// times (even if guarded) or when forward declarations are created during template
				// reparsing.  Source line numbers within the same file are always correctly ordered.
				// When source line lookup fails (returns 0, e.g. no line_map in unit tests),
				// fall back to comparing preprocessed line numbers to preserve Phase 1 checking.
				size_t decl_src_line = lexer_.getSourceLine(decl_tok.line());
				size_t cutoff_src_line = lexer_.getSourceLine(phase1_cutoff_line_);
				bool violation = (decl_src_line > 0 && cutoff_src_line > 0)
					? decl_src_line > cutoff_src_line
					: decl_tok.line() > phase1_cutoff_line_;
				if (violation) {
					phase1_violation_token_ = token;
				}
			}
		};

			// Function or function-template -> Function binding
		if (sym->is<FunctionDeclarationNode>() || sym->is<TemplateFunctionDeclarationNode>()) {
			if (sym->is<FunctionDeclarationNode>()) {
				checkPhase1(sym->as<FunctionDeclarationNode>().decl_node().identifier_token());
			} else {
				checkPhase1(sym->as<TemplateFunctionDeclarationNode>().function_decl_node().decl_node().identifier_token());
			}
			node.set_binding(IdentifierBinding::Function);
			return node;
		}

			// DeclarationNode
		if (sym->is<DeclarationNode>()) {
			const auto& decl = sym->as<DeclarationNode>();
				// Check if this DeclarationNode is actually an enumerator constant
				// (unscoped enum values are registered as DeclarationNode with Type::Enum).
				// Variables/parameters of enum type are also DeclarationNode with Type::Enum,
				// so we must verify via EnumTypeInfo::findEnumerator before classifying.
			{
				const auto& ts = decl.type_specifier_node();
				if (ts.category() == TypeCategory::Enum && !ts.is_reference() && ts.pointer_depth() == 0) {
					if (const TypeInfo* ti = tryGetTypeInfo(ts.type_index())) {
						const EnumTypeInfo* enum_info = ti->getEnumInfo();
						if (enum_info && enum_info->findEnumerator(token.handle())) {
							node.set_binding(IdentifierBinding::EnumConstant);
							return node;
						}
					}
				}
			}
			auto scope_type = gSymbolTable.get_scope_type_of_symbol(token.value());
			bool is_global_scope = (scope_type == ScopeType::Global || scope_type == ScopeType::Namespace);
			if (is_global_scope && tryBindMemberContext())
				return node;
			if (is_global_scope) {
				checkPhase1(decl.identifier_token());
				node.set_binding(IdentifierBinding::Global);
			} else if (scope_type.has_value()) {
				node.set_binding(IdentifierBinding::Local);
			}
			return node;
		}

			// VariableDeclarationNode
		if (sym->is<VariableDeclarationNode>()) {
			const auto& var_decl = sym->as<VariableDeclarationNode>();
			auto scope_type = gSymbolTable.get_scope_type_of_symbol(token.value());
			bool is_global_scope = (scope_type == ScopeType::Global || scope_type == ScopeType::Namespace);
			if (is_global_scope && tryBindMemberContext())
				return node;
			if (is_global_scope) {
				checkPhase1(var_decl.declaration().identifier_token());
				node.set_binding(IdentifierBinding::Global);
			} else if (scope_type.has_value()) {
				if (var_decl.storage_class() == StorageClass::Static) {
					node.set_binding(IdentifierBinding::StaticLocal);
				} else {
					node.set_binding(IdentifierBinding::Local);
				}
			}
			return node;
		}

			// EnumeratorNode
		if (sym->is<EnumeratorNode>()) {
			node.set_binding(IdentifierBinding::EnumConstant);
			return node;
		}

		return node; // Unresolved for other symbol types
	}

	SaveHandle save_token_position();
	void restore_token_position(SaveHandle handle, const std::source_location location = std::source_location::current());
	void restore_lexer_position_only(SaveHandle handle);	 // Restore lexer without erasing AST nodes
	void discard_saved_token(SaveHandle handle);

		// Helper for delayed parsing
	void skip_balanced_braces();	 // Skip over a balanced brace block
	void skip_balanced_parens();	 // Skip over a balanced parentheses block
	void skip_balanced_delimiters(TokenKind open, TokenKind close);	// Generic balanced delimiter skip
	void skip_template_arguments();	// Skip over template arguments <...>
	void skip_qualified_name_parts();  // Skip namespace-qualified name parts (e.g., ::Class after 'ns')
	std::string_view consume_qualified_name_suffix(std::string_view base_name);	// Same but builds and returns full qualified name
	void skip_member_declaration_to_semicolon();	 // Skip member declaration until ';' or end of struct
	void skip_function_body();  // Skip over '{...}' or function-try-block 'try {...} catch...'
	void skip_catch_clauses();  // Skip over one or more 'catch(...){}' clauses

		// Finalize an out-of-line static member variable definition.
		// Sets the initializer on the member and returns a VariableDeclarationNode.
		// When init_expr is std::nullopt, creates a zero literal from the member's type (empty brace init).
	ParseResult finalize_static_member_init(StructStaticMember* static_member,
											std::optional<ASTNode> init_expr,
											DeclarationNode& decl_node,
											const Token& name_token,
											ScopedTokenPosition& saved_position);

		// Parse a function type parameter list for template argument parsing
		// Parses types separated by commas, handling pack expansion (...), C-style varargs,
		// and pointer/reference modifiers. Used for bare function types and function pointer types.
		// Returns true if parsing succeeded and ')' was NOT consumed (caller must consume it).
	bool parse_function_type_parameter_list(std::vector<TypeIndex>& out_param_types);

		// Helper to update angle bracket depth for template parsing
		// Handles both '>' (decrement by 1) and '>>' (decrement by 2) for nested templates
	inline void update_angle_depth(std::string_view tok, int& angle_depth) {
		if (tok == "<") {
			angle_depth++;
		} else if (tok == ">>") {
			angle_depth -= 2;  // Handle nested templates (e.g., vector<vector<int>>)
		} else if (tok == ">") {
			angle_depth--;
		}
	}

		// TokenKind-based overload — avoids string comparison
	inline void update_angle_depth(TokenKind kind, int& angle_depth) {
		if (kind == tok::Less) {
			angle_depth++;
		} else if (kind == tok::ShiftRight) {
			angle_depth -= 2;
		} else if (kind == tok::Greater) {
			angle_depth--;
		}
	}
};

struct TypedNumeric {
	TypeCategory type = TypeCategory::Int;
	TypeQualifier typeQualifier = TypeQualifier::None;
	unsigned char sizeInBits = 0;
	NumericLiteralValue value = 0ULL;
};

// =============================================================================
// FunctionParsingScopeGuard constructor/destructor inline implementations
// (defined here because they need access to Parser internals)
// =============================================================================

inline FlashCpp::FunctionParsingScopeGuard::FunctionParsingScopeGuard(
	Parser& parser,
	bool has_member_context,
	bool inject_this,
	StructDeclarationNode* struct_node,
	StringHandle struct_name,
	TypeIndex struct_type_index,
	const std::vector<ASTNode>& params,
	const FunctionDeclarationNode* current_function)
	: parser_(parser), scope_(ScopeType::Function), pop_member_ctx_(has_member_context), saved_function_(parser.current_function_) {
	parser_.current_function_ = current_function;
	if (has_member_context) {
		parser_.setup_member_function_context(struct_node, struct_name, struct_type_index, inject_this);
	}
	parser_.register_parameters_in_scope(params);
}

inline FlashCpp::FunctionParsingScopeGuard::~FunctionParsingScopeGuard() {
	if (pop_member_ctx_) {
		parser_.member_function_context_stack_.pop_back();
	}
	parser_.current_function_ = saved_function_;
	// scope_ auto-exits the symbol table scope in its own destructor
}
