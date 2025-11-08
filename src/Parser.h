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

#include "AstNodeTypes.h"
#include "Lexer.h"
#include "Token.h"
#include "SymbolTable.h"
#include "CompileContext.h"

#ifndef WITH_DEBUG_INFO
#define WITH_DEBUG_INFO 0
#endif // WITH_DEBUG_INFO

using namespace std::literals::string_view_literals;

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
                    std::cerr << last_error_;
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
                std::string_view struct_name;            // For member function context
                size_t struct_type_index;                // For member function context
                StructDeclarationNode* struct_node;      // Pointer to struct being parsed
                bool is_constructor;                      // Special handling for constructors
                bool is_destructor;                       // Special handling for destructors
                ConstructorDeclarationNode* ctor_node;   // For constructors (nullptr for regular functions)
                DestructorDeclarationNode* dtor_node;    // For destructors (nullptr for regular functions)
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
        ParseResult parse_friend_declaration();  // NEW: Parse friend declarations
        ParseResult parse_template_declaration();  // NEW: Parse template declarations
        ParseResult parse_member_function_template(StructDeclarationNode& struct_node, AccessSpecifier access);  // NEW: Parse member function templates
        ParseResult parse_template_parameter_list(std::vector<ASTNode>& out_params);  // NEW: Parse template parameter list
        ParseResult parse_template_parameter();  // NEW: Parse a single template parameter
        ParseResult parse_template_template_parameter_forms(std::vector<ASTNode>& out_params);  // NEW: Parse template<template<typename> class T> forms
        ParseResult parse_template_template_parameter_form();  // NEW: Parse single template<template<typename> class T> form
        std::optional<std::vector<Type>> parse_explicit_template_arguments();  // NEW: Parse explicit template arguments like <int, float>
        std::optional<ASTNode> try_instantiate_template(std::string_view template_name, const std::vector<TypeSpecifierNode>& arg_types);  // NEW: Try to instantiate a template
        std::optional<ASTNode> try_instantiate_template_explicit(std::string_view template_name, const std::vector<Type>& explicit_types);  // NEW: Instantiate with explicit args
        std::optional<ASTNode> try_instantiate_class_template(std::string_view template_name, const std::vector<Type>& template_args);  // NEW: Instantiate class template
        std::optional<ASTNode> try_instantiate_member_function_template(std::string_view struct_name, std::string_view member_name, const std::vector<TypeSpecifierNode>& arg_types);  // NEW: Instantiate member function template
        std::string_view get_instantiated_class_name(std::string_view template_name, const std::vector<Type>& template_args);  // NEW: Get mangled name for instantiated class
        std::optional<bool> try_parse_out_of_line_template_member(const std::vector<ASTNode>& template_params, const std::vector<std::string_view>& template_param_names);  // NEW: Parse out-of-line template member function
        
public:  // Public methods for template instantiation
	// Parse a template function body with concrete type bindings (for template instantiation)
	std::optional<ASTNode> parseTemplateBody(
		TokenPosition body_pos,
		const std::vector<std::string_view>& template_param_names,
		const std::vector<Type>& concrete_types,
		std::string_view struct_name = "",  // Optional: for member functions
		TypeIndex struct_type_index = 0     // Optional: for member functions
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
        Linkage parse_declspec_attributes();          // Parse Microsoft __declspec(...) and return linkage
        AttributeInfo parse_attributes();             // Parse all types of attributes and return linkage + calling convention
        CallingConvention parse_calling_convention(); // Parse calling convention keywords

        // Helper to build __PRETTY_FUNCTION__ signature from FunctionDeclarationNode
        std::string buildPrettyFunctionSignature(const FunctionDeclarationNode& func_node) const;
        int get_operator_precedence(const std::string_view& op);
        std::optional<size_t> parse_alignas_specifier();  // Parse alignas(n) and return alignment value

        // Helper to extract type from an expression for overload resolution
        static std::optional<TypeSpecifierNode> get_expression_type(const ASTNode& expr_node);

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
