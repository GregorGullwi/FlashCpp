#pragma once

#include <cmath>
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
#include <string_view>
#include <source_location>
#include <functional>

#include "AstNodeTypes.h"
#include "Lexer.h"
#include "Token.h"
#include "SymbolTable.h"
#include "CompileContext.h"
#include "TemplateRegistry.h"  // Includes ConceptRegistry as well
#include "ParserTypes.h"       // Unified parsing types (Phase 1)
#include "ParserScopeGuards.h" // RAII scope guards (Phase 3)

#ifndef WITH_DEBUG_INFO
#define WITH_DEBUG_INFO 0
#endif // WITH_DEBUG_INFO

using namespace std::literals::string_view_literals;

// RAII helper to execute a cleanup function on scope exit
// Usage: ScopeGuard guard([&]() { cleanup(); });
template<typename Func>
class ScopeGuard {
public:
	explicit ScopeGuard(Func&& cleanup) : cleanup_(std::forward<Func>(cleanup)), active_(true) {}
	~ScopeGuard() { if (active_) cleanup_(); }
	
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
template<typename Func>
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
                : value_or_error_(Error{ std::move(error_message), std::move(token) }) {}

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
        std::string format_error(const std::vector<std::string>& file_paths, 
                                const std::vector<SourceLineMapping>& line_map = {},
                                const Lexer* lexer = nullptr) const {
                if (!is_error()) return "";
                
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

class Parser {
	// Friend classes that need access to private members
	friend class ExpressionSubstitutor;
	
public:
        static constexpr size_t default_ast_tree_size_ = 256 * 1024;

        explicit Parser(Lexer& lexer, CompileContext& context);

        ParseResult parse() {
                gSymbolTable = SymbolTable();
                register_builtin_functions();
                ParseResult parseResult;
                while (peek_token().has_value() && !parseResult.is_error() &&
                        peek_token()->type() != Token::Type::EndOfFile) {
                        parseResult = parse_top_level_node();
                }

                if (parseResult.is_error()) {
                    last_error_ = parseResult.error_message();
                }
                return parseResult;
        }

        const auto& get_nodes() { return ast_nodes_; }
        ASTNode get_inner_node(ASTNode node) const {
                return node;
        }

        template <typename T> bool is(ASTNode node) const {
                return node.is<T>();
        }

        template <typename T> T& as(ASTNode node) {
                return node.as<T>();
        }

        template <typename T> const T& as(ASTNode node) const {
                return node.as<T>();
        }

        template <typename T> T& as(ParseResult parse_result) {
                auto node = parse_result.node();
                return node ? node->as<T>() : ASTNode{}.as<T>();
        }

        bool generate_coff(const std::string& outputFilename);
        std::string get_last_error() const { return last_error_; }

#if WITH_DEBUG_INFO
        std::optional<int> break_at_line_;
#endif // WITH_DEBUG_INFO

private:
        Lexer& lexer_;
        CompileContext& context_;
        std::optional<Token> current_token_;
        std::optional<Token> injected_token_;  // Phase 5: For >> splitting in nested templates
        std::vector<ASTNode> ast_nodes_;
        std::string last_error_;

        // Track current function for __FUNCTION__, __func__, __PRETTY_FUNCTION__
        // Store pointer to the FunctionDeclarationNode which contains all the info we need
        const FunctionDeclarationNode* current_function_ = nullptr;

        // Track current linkage for extern "C" blocks
        Linkage current_linkage_ = Linkage::None;
        
        // Track last calling convention found in parse_type_and_name()
        // This is used to communicate calling convention from type parsing to function declaration
        CallingConvention last_calling_convention_ = CallingConvention::Default;

        // Track current struct context for member function parsing
        struct MemberFunctionContext {
                StringHandle struct_name;  // Points directly into source text from lexer token
                size_t struct_type_index;
                StructDeclarationNode* struct_node;  // Pointer to the struct being parsed
                StructTypeInfo* local_struct_info;   // Pointer to local struct_info being built (for static member lookup)
        };
        std::vector<MemberFunctionContext> member_function_context_stack_;

        // Track current struct being parsed (for nested class support)
        struct StructParsingContext {
                std::string_view struct_name;  // Points directly into source text from lexer token
                StructDeclarationNode* struct_node;  // Pointer to the struct being parsed
                StructTypeInfo* local_struct_info = nullptr;  // Pointer to StructTypeInfo being built (for static member lookup)
        };
        std::vector<StructParsingContext> struct_parsing_context_stack_;

        // Store parsed explicit template arguments for cross-function access
        // This allows template arguments parsed in one function (e.g., parse_primary_expression)
        // to be accessible in another function (e.g., parse_postfix_expression) for template instantiation
        std::optional<std::vector<TemplateTypeArg>> pending_explicit_template_args_;

        // Handle-based save/restore to avoid cursor position collisions
        // Each save gets a unique handle from a static incrementing counter
        using SaveHandle = size_t;

        // Delayed function body parsing for inline member functions
        struct DelayedFunctionBody {
                FunctionDeclarationNode* func_node;      // The function node to attach body to
                SaveHandle body_start;                   // Handle to saved position at '{'
                SaveHandle initializer_list_start;       // Handle to saved position at ':' for constructor initializer list
                StringHandle struct_name;                // For member function context
                size_t struct_type_index;                // For member function context
                StructDeclarationNode* struct_node;      // Pointer to struct being parsed
                bool has_initializer_list;               // True if constructor has an initializer list to re-parse
                bool is_constructor;                     // Special handling for constructors
                bool is_destructor;                      // Special handling for destructors
                ConstructorDeclarationNode* ctor_node;   // For constructors (nullptr for regular functions)
                DestructorDeclarationNode* dtor_node;    // For destructors (nullptr for regular functions)
                std::vector<StringHandle> template_param_names; // For template member functions
        };
        std::vector<DelayedFunctionBody> delayed_function_bodies_;

        // Deferred template class member function bodies (for two-phase lookup)
        // These are populated when parsing a template class definition and need to be
        // attached to the TemplateClassDeclarationNode for parsing during instantiation
        std::vector<DeferredTemplateMemberBody> pending_template_deferred_bodies_;

        // Template member function body for delayed instantiation
        // This stores the token position and template parameter info for re-parsing
        struct TemplateMemberFunctionBody {
                SaveHandle body_start;                           // Handle to saved position at '{'
                std::vector<std::string_view> template_param_names; // Names of template parameters (e.g., "T", "U") - from Token storage
                FunctionDeclarationNode* template_func_node;        // The original template function node
        };
        // Map from template function to its body info
		std::unordered_map<FunctionDeclarationNode*, TemplateMemberFunctionBody> template_member_function_bodies_;

		// Track if we're currently parsing a template class (to skip delayed body parsing)
		bool parsing_template_class_ = false;
		// Track when an inline namespace declaration was prefixed with 'inline'
		bool pending_inline_namespace_ = false;
		std::vector<StringHandle> current_template_param_names_;  // Names of current template parameters - from Token storage

        // Template parameter substitution for deferred template body parsing
        // Maps template parameter names to their substituted values (for non-type parameters)
        struct TemplateParamSubstitution {
            std::string_view param_name;
            bool is_value_param;  // true for non-type parameters
            int64_t value;        // For non-type parameters
            Type value_type;      // Type of the value
        };
        std::vector<TemplateParamSubstitution> template_param_substitutions_;

        // Track if we're parsing a template body (for template parameter reference recognition)
        bool parsing_template_body_ = false;
        std::vector<std::string_view> template_param_names_;  // Template parameter names in current scope
        
        // Track if we're instantiating a lazy member function (to prevent infinite recursion)
        bool instantiating_lazy_member_ = false;
        
        // Track if we're in SFINAE context (template argument substitution)
        // When true, type resolution errors should be treated as substitution failures instead of hard errors
        bool in_sfinae_context_ = false;

        // Track nesting of inline namespaces (parallel to parse_namespace recursion)
        std::vector<bool> inline_namespace_stack_;
        
        // Track if current scope has parameter packs (enables fold expression parsing)
        bool has_parameter_packs_ = false;

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

        // Track ASTNode addresses currently being processed in get_expression_type to prevent infinite recursion
        mutable std::unordered_set<const void*> expression_type_resolution_stack_;

        // Track template aliases currently being resolved to prevent infinite recursion
        std::unordered_set<std::string_view> resolving_aliases_;

        // Pending variable declarations from struct definitions (e.g., struct Point { ... } p, q;)
        std::vector<ASTNode> pending_struct_variables_;

        template <typename T>
        std::pair<ASTNode, T&> create_node_ref(T&& node) {
                return emplace_node_ref<T>(node);
        }

        template <typename T, typename... Args>
        std::pair<ASTNode, T&> emplace_node_ref(Args&&... args) {
                ASTNode ast_node = ASTNode::emplace_node<T>(std::forward<Args>(args)...);
                return { ast_node, ast_node.as<T>() };
        }

        template <typename T, typename... Args> ASTNode emplace_node(Args&&... args) {
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
                std::optional<Token> current_token_;
                size_t ast_nodes_size_ = 0;
                TokenPosition lexer_position_;  // Store the lexer position with each save
        };
        
        std::unordered_map<SaveHandle, SavedToken> saved_tokens_;
        size_t next_save_handle_ = 0;  // Auto-incrementing handle generator

        std::optional<Token> consume_token();

        std::optional<Token> peek_token();
        std::optional<Token> peek_token(size_t lookahead);  // Peek ahead N tokens (0 = current, 1 = next, etc.)

        // Phase 5: >> token splitting for nested templates (e.g., Foo<Bar<int>>)
        // When we encounter >> and need just >, this splits it by consuming first > and injecting second >
        void split_right_shift_token();  // Split >> into > and > (for nested templates)

        // Parsing functions for different constructs
        ParseResult parse_top_level_node();
        ParseResult parse_pragma_pack_inner();   // NEW: Parse the contents of pragma pack()
        ParseResult parse_type_and_name();
        ParseResult parse_structured_binding(CVQualifier cv_qualifiers, ReferenceQualifier ref_qualifier);  // NEW: Parse structured bindings: auto [a, b] = expr; auto& [x, y] = pair;
        ParseResult parse_declarator(TypeSpecifierNode& base_type, Linkage linkage = Linkage::None);  // NEW: Parse declarators (function pointers, arrays, etc.)
        ParseResult parse_direct_declarator(TypeSpecifierNode& base_type, Token& out_identifier, Linkage linkage);  // NEW: Helper for direct declarators
        ParseResult parse_postfix_declarator(TypeSpecifierNode& base_type, const Token& identifier);  // NEW: Helper for postfix declarators
        ParseResult parse_namespace();
        ParseResult parse_using_directive_or_declaration();  // Parse using directive/declaration/alias
        ParseResult parse_type_specifier();
        ParseResult parse_decltype_specifier();  // NEW: Parse decltype(expr) type specifier
        
        // Helper function to get Type and size for built-in type keywords
        std::optional<std::pair<Type, unsigned char>> get_builtin_type_info(std::string_view type_name);
        
        // Helper function to parse functional-style cast: Type(expression)
        // Returns ParseResult with StaticCastNode on success
        ParseResult parse_functional_cast(std::string_view type_name, const Token& type_token);
        
        // Helper function to parse cv-qualifiers (const/volatile) from token stream
        // Returns combined CVQualifier flags (None, Const, Volatile, or ConstVolatile)
        CVQualifier parse_cv_qualifiers();
        
        // Helper function to parse reference qualifiers (& or &&) from token stream
        // Returns ReferenceQualifier: None, LValueReference, or RValueReference
        ReferenceQualifier parse_reference_qualifier();
        
        // Helper function to append template type argument suffix to a StringBuilder
        // Used when building instantiated template names (e.g., "is_arithmetic_int")
        static void append_type_name_suffix(StringBuilder& sb, const TemplateTypeArg& arg);
        
        // Phase 4: Unified declaration parsing
        // This is the single entry point for parsing all declarations (variables and functions)
        // Context determines what forms are legal and how they're interpreted
        ParseResult parse_declaration(FlashCpp::DeclarationContext context = FlashCpp::DeclarationContext::Auto);
        
        // Legacy functions - now implemented as wrappers around parse_declaration()
        ParseResult parse_declaration_or_function_definition();
        ParseResult parse_function_declaration(DeclarationNode& declaration_node, CallingConvention calling_convention = CallingConvention::Default);
        ParseResult parse_parameter_list(FlashCpp::ParsedParameterList& out_params, CallingConvention calling_convention = CallingConvention::Default);  // Phase 1: Unified parameter list parsing
        ParseResult parse_function_trailing_specifiers(FlashCpp::MemberQualifiers& out_quals, FlashCpp::FunctionSpecifiers& out_specs);  // Phase 2: Unified trailing specifiers
        ParseResult parse_function_header(const FlashCpp::FunctionParsingContext& ctx, FlashCpp::ParsedFunctionHeader& out_header);  // Phase 4: Unified function header parsing
        ParseResult create_function_from_header(const FlashCpp::ParsedFunctionHeader& header, const FlashCpp::FunctionParsingContext& ctx);  // Phase 4: Create FunctionDeclarationNode from header
        ParseResult parse_function_body_with_context(const FlashCpp::FunctionParsingContext& ctx, const FlashCpp::ParsedFunctionHeader& header, std::optional<ASTNode>& out_body);  // Phase 5: Unified body parsing
        void setup_member_function_context(StructDeclarationNode* struct_node, StringHandle struct_name, size_t struct_type_index);  // Phase 5: Helper for member function scope setup
        void register_member_functions_in_scope(StructDeclarationNode* struct_node, size_t struct_type_index);  // Phase 5: Register member functions in symbol table
        void register_parameters_in_scope(const std::vector<ASTNode>& params);  // Phase 5: Register function parameters in symbol table
        ParseResult parse_delayed_function_body(DelayedFunctionBody& delayed, std::optional<ASTNode>& out_body);  // Phase 5: Unified delayed body parsing
        FlashCpp::SignatureValidationResult validate_signature_match(const FunctionDeclarationNode& declaration, const FunctionDeclarationNode& definition);  // Phase 7: Unified signature validation
        void compute_and_set_mangled_name(FunctionDeclarationNode& func_node);  // Phase 6 (mangling): Generate and set mangled name
        ParseResult parse_struct_declaration();  // Add struct declaration parser
        ParseResult parse_member_type_alias(std::string_view keyword, StructDeclarationNode* struct_ref, AccessSpecifier current_access);  // Helper: Parse typedef/using in struct/template
        ParseResult parse_enum_declaration();    // Add enum declaration parser
        ParseResult parse_typedef_declaration(); // Add typedef declaration parser
        ParseResult parse_static_assert();       // NEW: Parse static_assert declarations
        ParseResult parse_friend_declaration();  // NEW: Parse friend declarations
        ParseResult parse_template_friend_declaration(StructDeclarationNode& struct_node);  // NEW: Parse template friend declarations
        ParseResult parse_template_declaration();  // NEW: Parse template declarations
        ParseResult parse_concept_declaration();   // NEW: Parse C++20 concept declarations
       ParseResult parse_requires_expression();   // NEW: Parse C++20 requires expressions
        ParseResult parse_member_function_template(StructDeclarationNode& struct_node, AccessSpecifier access);  // NEW: Parse member function templates
        ParseResult parse_member_template_alias(StructDeclarationNode& struct_node, AccessSpecifier access);  // NEW: Parse member template aliases
        ParseResult parse_member_struct_template(StructDeclarationNode& struct_node, AccessSpecifier access);  // NEW: Parse member struct/class templates
        ParseResult parse_member_template_or_function(StructDeclarationNode& struct_node, AccessSpecifier access);  // Helper: Detect and parse member template alias or function
        // Phase 6: Shared helper for template function declaration parsing
        // Parses: type_and_name + function_declaration + body handling (semicolon or skip braces)
        // Returns the TemplateFunctionDeclarationNode in out_template_node
        ParseResult parse_template_function_declaration_body(
            std::vector<ASTNode>& template_params,
            std::optional<ASTNode> requires_clause,
            ASTNode& out_template_node);
        ParseResult parse_template_parameter_list(std::vector<ASTNode>& out_params);  // NEW: Parse template parameter list
        ParseResult parse_template_parameter();  // NEW: Parse a single template parameter
        // Simple struct to hold constant expression evaluation results
        // Public members are intentional for this lightweight data structure
        struct ConstantValue {
                int64_t value;
                Type type;
        };
        
        ParseResult parse_template_template_parameter_forms(std::vector<ASTNode>& out_params);  // NEW: Parse template<template<typename> class T> forms
        ParseResult parse_template_template_parameter_form();  // NEW: Parse single template<template<typename> class T> form
        std::optional<std::vector<TemplateTypeArg>> parse_explicit_template_arguments(std::vector<ASTNode>* out_type_nodes = nullptr);  // NEW: Parse explicit template arguments like <int, float>
        bool could_be_template_arguments();  // NEW: Lookahead to check if '<' starts template arguments (Phase 1 of C++20 disambiguation)
        
        // Phase 2: Unified Qualified Identifier Parser (Sprint 3-4)
        std::optional<QualifiedIdParseResult> parse_qualified_identifier_with_templates();  // NEW: Unified parser for all qualified identifier contexts
        
        std::optional<ConstantValue> try_evaluate_constant_expression(const ASTNode& expr_node);  // NEW: Evaluate constant expressions for template arguments (e.g., is_int<T>::value)
        std::optional<ASTNode> try_instantiate_template(std::string_view template_name, const std::vector<TypeSpecifierNode>& arg_types);  // NEW: Try to instantiate a template
        std::optional<ASTNode> try_instantiate_single_template(const ASTNode& template_node, std::string_view template_name, const std::vector<TypeSpecifierNode>& arg_types, int& recursion_depth);  // Helper: Try to instantiate a specific template node (for SFINAE)
        std::optional<ASTNode> try_instantiate_template_explicit(std::string_view template_name, const std::vector<TemplateTypeArg>& explicit_types);  // NEW: Instantiate with explicit args
        std::optional<ASTNode> try_instantiate_class_template(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args, bool force_eager = false);  // NEW: Instantiate class template
        std::optional<ASTNode> instantiate_full_specialization(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args, const ASTNode& spec_node);  // Instantiate full specialization
        std::optional<ASTNode> try_instantiate_variable_template(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args);  // NEW: Instantiate variable template
        ASTNode substitute_template_params_in_expression(const ASTNode& expr, const std::unordered_map<TypeIndex, TemplateTypeArg>& type_substitution_map);  // NEW: Substitute template parameters in expressions
        std::optional<ASTNode> try_instantiate_member_function_template(std::string_view struct_name, std::string_view member_name, const std::vector<TypeSpecifierNode>& arg_types);  // NEW: Instantiate member function template
        std::optional<ASTNode> try_instantiate_member_function_template_explicit(std::string_view struct_name, std::string_view member_name, const std::vector<TemplateTypeArg>& template_type_args);  // NEW: Instantiate member function template with explicit args
        std::optional<ASTNode> instantiateLazyMemberFunction(const LazyMemberFunctionInfo& lazy_info);  // NEW: Instantiate lazy member function on-demand
        std::string_view get_instantiated_class_name(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args);  // NEW: Get mangled name for instantiated class
        std::string_view instantiate_and_register_base_template(std::string_view& base_class_name, const std::vector<TemplateTypeArg>& template_args);  // Helper: Instantiate base class template and add to AST
        std::optional<bool> try_parse_out_of_line_template_member(const std::vector<ASTNode>& template_params, const std::vector<StringHandle>& template_param_names);  // NEW: Parse out-of-line template member function
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
        
public:  // Public methods for template instantiation
	// Parse a template function body with concrete type bindings (for template instantiation)
	std::optional<ASTNode> parseTemplateBody(
		SaveHandle body_pos,
		const std::vector<std::string_view>& template_param_names,
		const std::vector<Type>& concrete_types,
		std::string_view struct_name = "",  // Optional: for member functions
		TypeIndex struct_type_index = 0     // Optional: for member functions
	);

	// Substitute template parameters in an AST node with concrete types/values
	ASTNode substituteTemplateParameters(
		const ASTNode& node,
		const std::vector<ASTNode>& template_params,
		const std::vector<TemplateArgument>& template_args
	);private:  // Resume private methods
		void register_builtin_functions();  // Register compiler builtin functions
        ParseResult parse_block();
        ParseResult parse_statement_or_declaration();
        ParseResult parse_variable_declaration();
        FlashCpp::DeclarationSpecifiers parse_declaration_specifiers();  // Phase 1: Shared specifier parsing
        bool looks_like_function_parameters();  // Phase 2: Detect if '(' starts function params vs direct init
        // Phase 3: Consolidated initialization helpers
        std::optional<ASTNode> parse_direct_initialization();  // Parse Type var(args) - returns initializer node
        std::optional<ASTNode> parse_copy_initialization(DeclarationNode& decl_node, TypeSpecifierNode& type_specifier);  // Parse Type var = expr or Type var = {args}
        ParseResult parse_extern_block(Linkage linkage);  // Parse extern "C" { ... } block
        ParseResult parse_brace_initializer(const TypeSpecifierNode& type_specifier);  // Add brace initializer parser
        ParseResult parse_for_loop();  // Add this line
        ParseResult parse_while_loop();  // Add while-loop parser
        ParseResult parse_do_while_loop();  // Add do-while-loop parser
        ParseResult parse_if_statement();  // Add if-statement parser
        ParseResult parse_switch_statement();  // Add switch-statement parser
        ParseResult parse_return_statement();
        ParseResult parse_break_statement();  // Add break-statement parser
        ParseResult parse_continue_statement();  // Add continue-statement parser
        ParseResult parse_goto_statement();  // Add goto-statement parser
        ParseResult parse_label_statement();  // Add label-statement parser
        ParseResult parse_lambda_expression();  // Add lambda expression parser
        ParseResult parse_try_statement();  // Add try-catch statement parser
        ParseResult parse_throw_statement();  // Add throw statement parser

        // Helper functions for auto type deduction
        Type deduce_type_from_expression(const ASTNode& expr) const;
        void deduce_and_update_auto_return_type(FunctionDeclarationNode& func_decl);
        void process_deferred_lambda_deductions();  // Process deferred lambda return type deductions
        bool are_types_compatible(const TypeSpecifierNode& type1, const TypeSpecifierNode& type2) const;  // Check if two types are compatible
        std::string type_to_string(const TypeSpecifierNode& type) const;  // Convert type to string for error messages
        static unsigned char get_type_size_bits(Type type);

        // Helper function for counting pack elements in template parameter packs
        size_t count_pack_elements(std::string_view pack_name) const;

        // Phase 3: Expression context tracking for template disambiguation
        enum class ExpressionContext {
            Normal,              // Regular expression context
            Decltype,            // Inside decltype() - strictest template-first rules
            TemplateArgument,    // Template argument context
            RequiresClause,      // Requires clause expression
            ConceptDefinition    // Concept definition context
        };

        // Minimum precedence to accept all operators (assignment has lowest precedence = 3)
        static constexpr int MIN_PRECEDENCE = 0;
        // Default precedence excludes comma operator (precedence 1) to prevent it from being
        // treated as an operator in contexts where it's a separator (declarations, arguments, etc.)
        static constexpr int DEFAULT_PRECEDENCE = 2;
        // NOTE: ExpressionContext is required (no default) to prevent bugs where context
        // is accidentally not passed in recursive calls (e.g., ternary branch parsing).
        // Use ExpressionContext::Normal for most cases; TemplateArgument when inside <...>.
        ParseResult parse_expression(int precedence, ExpressionContext context);
        ParseResult parse_expression_statement() { return parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal); }  // Wrapper for keyword map
        ParseResult parse_primary_expression(ExpressionContext context = ExpressionContext::Normal);
        ParseResult parse_postfix_expression(ExpressionContext context = ExpressionContext::Normal);  // Phase 3: New postfix operator layer
        ParseResult parse_unary_expression(ExpressionContext context = ExpressionContext::Normal);
        ParseResult parse_qualified_identifier();  // Parse namespace::identifier
        ParseResult parse_qualified_identifier_after_template(const Token& template_base_token);  // Parse Template<T>::member
        
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
        void skip_cpp_attributes();                   // Skip C++ standard [[...]] attributes
        void skip_gcc_attributes();                   // Skip GCC __attribute__((...)) specifications
        void skip_noexcept_specifier();               // Skip noexcept or noexcept(expr) specifier
        void skip_function_trailing_specifiers();     // Skip all trailing specifiers after function parameters
        bool parse_constructor_exception_specifier(); // Parse noexcept or throw() and return true if noexcept
        void apply_trailing_reference_qualifiers(TypeSpecifierNode& type_spec);  // Apply & or && reference qualifiers to a type
        
        // Helper to parse static member functions (reduces code duplication)
        bool parse_static_member_function(
            ParseResult& type_and_name_result,
            bool is_static_constexpr,
            StringHandle struct_name_handle,
            StructDeclarationNode& struct_ref,
            StructTypeInfo* struct_info,
            AccessSpecifier current_access,
            const std::vector<StringHandle>& current_template_param_names
        );
        
        // Helper to parse entire static member block (data or function) - reduces code duplication
        ParseResult parse_static_member_block(
            StringHandle struct_name_handle,
            StructDeclarationNode& struct_ref,
            StructTypeInfo* struct_info,
            AccessSpecifier current_access,
            const std::vector<StringHandle>& current_template_param_names,
            bool use_struct_type_info = false  // If true, use struct_type_info.getStructInfo() for data members
        );
        
        Linkage parse_declspec_attributes();          // Parse Microsoft __declspec(...) and return linkage
        AttributeInfo parse_attributes();             // Parse all types of attributes and return linkage + calling convention
        CallingConvention parse_calling_convention(); // Parse calling convention keywords

        // Helper to build __PRETTY_FUNCTION__ signature from FunctionDeclarationNode
        std::string buildPrettyFunctionSignature(const FunctionDeclarationNode& func_node) const;
        int get_operator_precedence(const std::string_view& op);
        std::optional<size_t> parse_alignas_specifier();  // Parse alignas(n) and return alignment value

        // Helper to extract type from an expression for overload resolution
        std::optional<TypeSpecifierNode> get_expression_type(const ASTNode& expr_node) const;

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

        // Helper: Look up a type alias including inherited ones from base classes
        // Returns the TypeInfo pointer if found, nullptr otherwise
        // Uses depth limit to prevent infinite recursion in malformed input
        const TypeInfo* lookup_inherited_type_alias(StringHandle struct_name, StringHandle member_name, int depth = 0);
        // Convenience overload for string_view parameters
        const TypeInfo* lookup_inherited_type_alias(std::string_view struct_name, std::string_view member_name, int depth = 0) {
            return lookup_inherited_type_alias(
                StringTable::getOrInternStringHandle(struct_name),
                StringTable::getOrInternStringHandle(member_name),
                depth
            );
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
                depth
            );
        }

        // Substitute template parameter in a type specification
        // Handles complex transformations like const T& -> const int&, T* -> int*, etc.
        std::pair<Type, TypeIndex> substitute_template_parameter(
            const TypeSpecifierNode& original_type,
            const std::vector<ASTNode>& template_params,
            const std::vector<TemplateTypeArg>& template_args
        );
       
        // Lookup symbol with template parameter checking
        std::optional<ASTNode> lookup_symbol_with_template_check(StringHandle identifier);

        // Unified symbol lookup that automatically provides template parameters when parsing templates
        std::optional<ASTNode> lookup_symbol(StringHandle identifier) const {
            if (parsing_template_body_ && !current_template_param_names_.empty()) {
                return gSymbolTable.lookup(identifier, gSymbolTable.get_current_scope_handle(), &current_template_param_names_);
            } else {
                return gSymbolTable.lookup(identifier);
            }
        }

        // Overload for qualified lookups
        std::optional<ASTNode> lookup_symbol_qualified(const std::vector<StringType<>>& namespaces, std::string_view identifier) const {
            if (parsing_template_body_ && !current_template_param_names_.empty()) {
                // For qualified lookups, we still need template params for the base lookup
                // But qualified lookups are less common in template bodies
                return gSymbolTable.lookup_qualified(namespaces, identifier);
            } else {
                return gSymbolTable.lookup_qualified(namespaces, identifier);
            }
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

        SaveHandle save_token_position();
        void restore_token_position(SaveHandle handle, const std::source_location location = std::source_location::current());
        void restore_lexer_position_only(SaveHandle handle);  // Restore lexer without erasing AST nodes
        void discard_saved_token(SaveHandle handle);

        // Helper for delayed parsing
        void skip_balanced_braces();  // Skip over a balanced brace block
        void skip_balanced_parens();  // Skip over a balanced parentheses block
};

struct TypedNumeric {
        Type type = Type::Int;
        TypeQualifier typeQualifier = TypeQualifier::None;
        unsigned char sizeInBits = 0;
        NumericLiteralValue value = 0ULL;
};

// =============================================================================
// Phase 3: Inline implementations for scope guards that need Parser internals
// =============================================================================

// FunctionScopeGuard::addParameters implementation
// Adds function parameters to the symbol table within the function scope
inline void FlashCpp::FunctionScopeGuard::addParameters(const std::vector<ASTNode>& params) {
	for (const auto& param : params) {
		if (param.is<DeclarationNode>()) {
			const auto& param_decl_node = param.as<DeclarationNode>();
			const Token& param_token = param_decl_node.identifier_token();
			gSymbolTable.insert(param_token.value(), param);
		}
	}
}

// FunctionScopeGuard::injectThisPointer implementation
// Creates and injects 'this' pointer for member functions
inline void FlashCpp::FunctionScopeGuard::injectThisPointer() {
	// Only inject 'this' for member functions, constructors, and destructors
	if (ctx_.kind != FunctionKind::Member &&
	    ctx_.kind != FunctionKind::Constructor &&
	    ctx_.kind != FunctionKind::Destructor) {
		return;
	}
	
	// Find the parent struct type
	auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(ctx_.parent_struct_name));
	if (type_it == gTypesByName.end()) {
		return;  // Can't inject 'this' without knowing the struct type
	}
	
	// Create 'this' pointer type: StructName*
	// Note: This creates a temporary node that will be cleaned up with the scope
	TypeSpecifierNode this_type_node(
		Type::Struct, 
		type_it->second->type_index_,
		64,  // Pointer size in bits
		Token()
	);
	this_type_node.add_pointer_level();
	
	// Create a declaration node for 'this'
	Token this_token(Token::Type::Keyword, "this", 0, 0, 0);
	ASTNode type_node = ASTNode::emplace_node<TypeSpecifierNode>(this_type_node);
	ASTNode this_decl = ASTNode::emplace_node<DeclarationNode>(type_node, this_token);
	
	// Insert 'this' into the symbol table
	gSymbolTable.insert("this"sv, this_decl);
}
