#pragma once

#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
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
                if (tok.file_index() < file_paths.size()) {
                        result += file_paths[tok.file_index()] + ":" + 
                                std::to_string(tok.line()) + ":" + 
                                std::to_string(tok.column()) + ": ";
                } else {
                        result += "<unknown>:" + std::to_string(tok.line()) + ":" + 
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

class Parser {
public:
        static constexpr size_t default_ast_tree_size_ = 256 * 1024;

        explicit Parser(Lexer& lexer, CompileContext& context);

        ParseResult parse() {
                gSymbolTable = SymbolTable();
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
                std::string_view struct_name;  // Points directly into source text from lexer token
                size_t struct_type_index;
                StructDeclarationNode* struct_node;  // Pointer to the struct being parsed
        };
        std::vector<MemberFunctionContext> member_function_context_stack_;

        // Track current struct being parsed (for nested class support)
        struct StructParsingContext {
                std::string_view struct_name;  // Points directly into source text from lexer token
                StructDeclarationNode* struct_node;  // Pointer to the struct being parsed
        };
        std::vector<StructParsingContext> struct_parsing_context_stack_;

        // Delayed function body parsing for inline member functions
        struct DelayedFunctionBody {
                FunctionDeclarationNode* func_node;      // The function node to attach body to
                TokenPosition body_start;                 // Position of the '{'
                TokenPosition initializer_list_start;     // Position of ':' for constructor initializer list (optional)
                bool has_initializer_list;                // True if constructor has an initializer list to re-parse
                std::string_view struct_name;            // For member function context
                size_t struct_type_index;                // For member function context
                StructDeclarationNode* struct_node;      // Pointer to struct being parsed
                bool is_constructor;                      // Special handling for constructors
                bool is_destructor;                       // Special handling for destructors
                ConstructorDeclarationNode* ctor_node;   // For constructors (nullptr for regular functions)
                DestructorDeclarationNode* dtor_node;    // For destructors (nullptr for regular functions)
                std::vector<std::string_view> template_param_names; // For template member functions
        };
        std::vector<DelayedFunctionBody> delayed_function_bodies_;

        // Template member function body for delayed instantiation
        // This stores the token position and template parameter info for re-parsing
        struct TemplateMemberFunctionBody {
                TokenPosition body_start;                           // Position of the '{'
                std::vector<std::string_view> template_param_names; // Names of template parameters (e.g., "T", "U") - from Token storage
                FunctionDeclarationNode* template_func_node;        // The original template function node
        };
        // Map from template function to its body info
        std::unordered_map<FunctionDeclarationNode*, TemplateMemberFunctionBody> template_member_function_bodies_;

        // Track if we're currently parsing a template class (to skip delayed body parsing)
        bool parsing_template_class_ = false;
        std::vector<std::string_view> current_template_param_names_;  // Names of current template parameters - from Token storage

        // Track if we're parsing a template body (for template parameter reference recognition)
        bool parsing_template_body_ = false;
        std::vector<std::string_view> template_param_names_;  // Template parameter names in current scope

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
                TokenPosition saved_position_;
                bool discarded_ = false;
                std::source_location location_;
        };

        struct SavedToken {
                std::optional<Token> current_token_;
                size_t ast_nodes_size_ = 0;
        };
        std::unordered_map<size_t, SavedToken> saved_tokens_;

        std::optional<Token> consume_token();

        std::optional<Token> peek_token();
        std::optional<Token> peek_token(size_t lookahead);  // Peek ahead N tokens (0 = current, 1 = next, etc.)

        // Parsing functions for different constructs
        ParseResult parse_top_level_node();
        ParseResult parse_type_and_name();
        ParseResult parse_declarator(TypeSpecifierNode& base_type, Linkage linkage = Linkage::None);  // NEW: Parse declarators (function pointers, arrays, etc.)
        ParseResult parse_direct_declarator(TypeSpecifierNode& base_type, Token& out_identifier, Linkage linkage);  // NEW: Helper for direct declarators
        ParseResult parse_postfix_declarator(TypeSpecifierNode& base_type, const Token& identifier);  // NEW: Helper for postfix declarators
        ParseResult parse_namespace();
        ParseResult parse_using_directive_or_declaration();  // Parse using directive/declaration/alias
        ParseResult parse_type_specifier();
        ParseResult parse_decltype_specifier();  // NEW: Parse decltype(expr) type specifier
        ParseResult parse_declaration_or_function_definition();
        ParseResult parse_function_declaration(DeclarationNode& declaration_node, CallingConvention calling_convention = CallingConvention::Default);
        ParseResult parse_struct_declaration();  // Add struct declaration parser
        ParseResult parse_enum_declaration();    // Add enum declaration parser
        ParseResult parse_typedef_declaration(); // Add typedef declaration parser
        ParseResult parse_static_assert();       // NEW: Parse static_assert declarations
        ParseResult parse_friend_declaration();  // NEW: Parse friend declarations
        ParseResult parse_template_declaration();  // NEW: Parse template declarations
        ParseResult parse_concept_declaration();   // NEW: Parse C++20 concept declarations
       ParseResult parse_requires_expression();   // NEW: Parse C++20 requires expressions
        ParseResult parse_member_function_template(StructDeclarationNode& struct_node, AccessSpecifier access);  // NEW: Parse member function templates
        ParseResult parse_template_parameter_list(std::vector<ASTNode>& out_params);  // NEW: Parse template parameter list
        ParseResult parse_template_parameter();  // NEW: Parse a single template parameter
        ParseResult parse_template_template_parameter_forms(std::vector<ASTNode>& out_params);  // NEW: Parse template<template<typename> class T> forms
        ParseResult parse_template_template_parameter_form();  // NEW: Parse single template<template<typename> class T> form
        std::optional<std::vector<TemplateTypeArg>> parse_explicit_template_arguments(std::vector<ASTNode>* out_type_nodes = nullptr);  // NEW: Parse explicit template arguments like <int, float>
        std::optional<ASTNode> try_instantiate_template(std::string_view template_name, const std::vector<TypeSpecifierNode>& arg_types);  // NEW: Try to instantiate a template
        std::optional<ASTNode> try_instantiate_template_explicit(std::string_view template_name, const std::vector<TemplateTypeArg>& explicit_types);  // NEW: Instantiate with explicit args
        std::optional<ASTNode> try_instantiate_class_template(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args);  // NEW: Instantiate class template
        std::optional<ASTNode> try_instantiate_variable_template(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args);  // NEW: Instantiate variable template
        ASTNode substitute_template_params_in_expression(const ASTNode& expr, const std::unordered_map<TypeIndex, TemplateTypeArg>& type_substitution_map);  // NEW: Substitute template parameters in expressions
        std::optional<ASTNode> try_instantiate_member_function_template(std::string_view struct_name, std::string_view member_name, const std::vector<TypeSpecifierNode>& arg_types);  // NEW: Instantiate member function template
        std::optional<ASTNode> try_instantiate_member_function_template_explicit(std::string_view struct_name, std::string_view member_name, const std::vector<TemplateTypeArg>& template_type_args);  // NEW: Instantiate member function template with explicit args
        std::string_view get_instantiated_class_name(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args);  // NEW: Get mangled name for instantiated class
        std::optional<bool> try_parse_out_of_line_template_member(const std::vector<ASTNode>& template_params, const std::vector<std::string_view>& template_param_names);  // NEW: Parse out-of-line template member function
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
		TokenPosition body_pos,
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
        ParseResult parse_block();
        ParseResult parse_statement_or_declaration();
        ParseResult parse_variable_declaration();
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
        ParseResult transformLambdaToStruct(const LambdaExpressionNode& lambda);  // Transform lambda to struct with operator()
        ParseResult parse_try_statement();  // Add try-catch statement parser
        ParseResult parse_throw_statement();  // Add throw statement parser

        // Helper functions for auto type deduction
        Type deduce_type_from_expression(const ASTNode& expr) const;
        static unsigned char get_type_size_bits(Type type);

        // Minimum precedence to accept all operators (assignment has lowest precedence = 3)
        static constexpr int MIN_PRECEDENCE = 0;
        // Default precedence excludes comma operator (precedence 1) to prevent it from being
        // treated as an operator in contexts where it's a separator (declarations, arguments, etc.)
        static constexpr int DEFAULT_PRECEDENCE = 2;
        ParseResult parse_expression(int precedence = DEFAULT_PRECEDENCE);
        ParseResult parse_expression_statement() { return parse_expression(); }  // Wrapper for keyword map
        ParseResult parse_primary_expression();
        ParseResult parse_unary_expression();
        ParseResult parse_qualified_identifier();  // Parse namespace::identifier
        ParseResult parse_qualified_identifier_after_template(const Token& template_base_token);  // Parse Template<T>::member

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

        // Substitute template parameter in a type specification
        // Handles complex transformations like const T& -> const int&, T* -> int*, etc.
        std::pair<Type, TypeIndex> substitute_template_parameter(
            const TypeSpecifierNode& original_type,
            const std::vector<ASTNode>& template_params,
            const std::vector<TemplateTypeArg>& template_args
        );
       
        // Lookup symbol with template parameter checking
        std::optional<ASTNode> lookup_symbol_with_template_check(std::string_view identifier);

        // Unified symbol lookup that automatically provides template parameters when parsing templates
        std::optional<ASTNode> lookup_symbol(std::string_view identifier) const {
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

        TokenPosition save_token_position();
        void restore_token_position(const TokenPosition& token_position, const std::source_location location = std::source_location::current());
        void restore_lexer_position_only(const TokenPosition& token_position);  // Restore lexer without erasing AST nodes
        void discard_saved_token(const TokenPosition& token_position);

        // Helper for delayed parsing
        void skip_balanced_braces();  // Skip over a balanced brace block
};

struct TypedNumeric {
        Type type = Type::Int;
        TypeQualifier typeQualifier = TypeQualifier::None;
        unsigned char sizeInBits = 0;
        NumericLiteralValue value = 0ULL;
};
