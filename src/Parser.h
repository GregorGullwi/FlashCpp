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

#include "AstNodeTypes.h"
#include "Lexer.h"
#include "Token.h"
#include "SymbolTable.h"
#include "CompileContext.h"

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

private:
        Lexer& lexer_;
        CompileContext& context_;
        std::optional<Token> current_token_;
        std::vector<ASTNode> ast_nodes_;
        std::string last_error_;

        // Track current function for __FUNCTION__, __func__, __PRETTY_FUNCTION__
        // Store pointer to the FunctionDeclarationNode which contains all the info we need
        const FunctionDeclarationNode* current_function_ = nullptr;

        // Track current struct context for member function parsing
        struct MemberFunctionContext {
                std::string_view struct_name;  // Points directly into source text from lexer token
                size_t struct_type_index;
                StructDeclarationNode* struct_node;  // Pointer to the struct being parsed
        };
        std::vector<MemberFunctionContext> member_function_context_stack_;

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
                explicit ScopedTokenPosition(class Parser& parser);

                ~ScopedTokenPosition();

                ParseResult success(ASTNode node = ASTNode{});

                ParseResult error(std::string_view error_message);

        private:
                class Parser& parser_;
                TokenPosition saved_position_;
                bool discarded_ = false;
        };

        struct SavedToken {
                std::optional<Token> current_token_;
                size_t ast_nodes_size_ = 0;
        };
        std::unordered_map<size_t, SavedToken> saved_tokens_;

        std::optional<Token> consume_token();

        std::optional<Token> peek_token();

        // Parsing functions for different constructs
        ParseResult parse_top_level_node();
        ParseResult parse_type_and_name();
        ParseResult parse_namespace();
        ParseResult parse_type_specifier();
        ParseResult parse_declaration_or_function_definition();
        ParseResult parse_function_declaration(DeclarationNode& declaration_node);
        ParseResult parse_struct_declaration();  // Add struct declaration parser
        ParseResult parse_block();
        ParseResult parse_statement_or_declaration();
        ParseResult parse_variable_declaration();
        ParseResult parse_brace_initializer(const TypeSpecifierNode& type_specifier);  // Add brace initializer parser
        ParseResult parse_for_loop();  // Add this line
        ParseResult parse_while_loop();  // Add while-loop parser
        ParseResult parse_do_while_loop();  // Add do-while-loop parser
        ParseResult parse_if_statement();  // Add if-statement parser
        ParseResult parse_return_statement();
        ParseResult parse_break_statement();  // Add break-statement parser
        ParseResult parse_continue_statement();  // Add continue-statement parser
        ParseResult parse_statement();
        // Minimum precedence to accept all operators (assignment has lowest precedence = 3)
        static constexpr int MIN_PRECEDENCE = 0;
        ParseResult parse_expression(int precedence = MIN_PRECEDENCE);
        ParseResult parse_primary_expression();
        ParseResult parse_unary_expression();
        ParseResult parse_binary_expression(int min_precedence = 0);
        ParseResult parse_parenthesized_expression();
        ParseResult parse_qualified_identifier();  // Parse namespace::identifier

        // Utility functions
        bool consume_punctuator(const std::string_view& value);
        bool consume_keyword(const std::string_view& value);

        // Helper to build __PRETTY_FUNCTION__ signature from FunctionDeclarationNode
        std::string buildPrettyFunctionSignature(const FunctionDeclarationNode& func_node) const;
        int get_operator_precedence(const std::string_view& op);
        std::optional<size_t> parse_alignas_specifier();  // Parse alignas(n) and return alignment value

        // Helper to extract type from an expression for overload resolution
        std::optional<TypeSpecifierNode> get_expression_type(const ASTNode& expr_node) const;

        TokenPosition save_token_position();
        void restore_token_position(const TokenPosition& token_position);
        void discard_saved_token(const TokenPosition& token_position);
};

struct TypedNumeric {
        Type type = Type::Int;
        TypeQualifier typeQualifier = TypeQualifier::None;
        unsigned char sizeInBits = 0;
        NumericLiteralValue value = 0ULL;
};

// only handles unsigned integer types for now
static std::optional<TypedNumeric> get_numeric_literal_type(std::string_view text);
