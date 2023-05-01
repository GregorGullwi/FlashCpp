#include "CompileContext.h"
#include "FileTree.h"
#include "FileReader.h"
#include "Token.h"
#include "Lexer.h"
#include "Parser.h"
#include <string>
#include <algorithm>
#include <cctype>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

static CompileContext compile_context;
static FileTree file_tree;

static bool compare_lexers_ignore_whitespace(Lexer& lexer1, Lexer& lexer2) {
	Token token1, token2;

	while (true) {
		token1 = lexer1.next_token();
		token2 = lexer2.next_token();

		// If both tokens are EndOfFile, the token sequences are identical
		if (token1.type() == Token::Type::EndOfFile && token2.type() == Token::Type::EndOfFile) {
			return true;
		}

		// If the current tokens do not match, the token sequences are not identical
		if (token1.type() != token2.type() || token1.value() != token2.value()) {
			return false;
		}
	}
}

static void run_test_case(const std::string& input, const std::string& expected_output) {
	FileReader file_reader(compile_context, file_tree.reset());
	file_reader.push_file_to_stack({ __FILE__, __LINE__ });
	CHECK(file_reader.processFileContent(input));
	const std::string& actual_output = file_reader.get_result();
	Lexer lexer_expected(expected_output);
	Lexer lexer_actual(actual_output);
	CHECK(compare_lexers_ignore_whitespace(lexer_expected, lexer_actual));
}

TEST_CASE("preprocessor") {
	SUBCASE("SimpleReplacement") {
		const std::string input = R"(
			#define PI 3.14159
			const double radius = 1.0;
			const double circumference = 2 * PI * radius;
		  )";
		const std::string expected_output = R"(
			const double radius = 1.0;
			const double circumference = 2 * 3.14159 * radius;
		  )";
		run_test_case(input, expected_output);
	}

	SUBCASE("NestedReplacement") {
		const std::string input = R"(
	    #define PI 3.14159
	    #define CIRCLE_AREA(r) (PI * (r) * (r))
	    const double radius = 1.0;
	    const double area = CIRCLE_AREA(radius);
	  )";
		const std::string expected_output = R"(
			const double radius = 1.0;
			const double area = (3.14159 * (radius) * (radius));
		  )";
		run_test_case(input, expected_output);
	}

#define SQUARE(x) ((x) * (x))
#define DOUBLE(x) ((x) * 2)
	const int num = DOUBLE(SQUARE(3));

	SUBCASE("NestedMacros") {
		const std::string input = R"(
			#define SQUARE(x) ((x) * (x))
			#define DOUBLE(n) ((n) * 2)
			const int num = DOUBLE(SQUARE(3));
		  )";
		const std::string expected_output = R"(
			const int num = ((((3) * (3))) * 2);
		  )";
		run_test_case(input, expected_output);
	}

	SUBCASE("ConditionalCompilation") {
		const std::string input = R"(
			#define DEBUG
			#ifdef DEBUG
			  const int x = 1;
			#else
			  const int x = 0;
			#endif
		  )";
		const std::string expected_output = R"(
			const int x = 1;
		  )";
		run_test_case(input, expected_output);
	}

#define STR(x) #x
	const char* str = STR(hello world);

	SUBCASE("Stringification") {
		const std::string input = R"(
			#define STR(x) #x
			const char* str = STR(hello world);
		  )";
		const std::string expected_output = R"(
			const char* str = "hello world";
		  )";
		run_test_case(input, expected_output);
	}

	SUBCASE("Concatenation") {
		const std::string input = R"(
			#define CONCAT(a, b) a ## b
			const int num = CONCAT(3, 4);
		  )";
		const std::string expected_output = R"(
			const int num = 34;
		  )";
		run_test_case(input, expected_output);
	}

	SUBCASE("__has_include") {
		const std::string input = R"(
			#if __has_include(<iostream>)
			  const bool has_iostream = true;
			#else
			  const bool has_iostream = false;
			#endif
		  )";
		const std::string expected_output_false = R"(
			  const bool has_iostream = false;
		  )";
		const std::string expected_output_true = R"(
			  const bool has_iostream = true;
		  )";
		run_test_case(input, expected_output_false);
#ifdef _WIN32
		compile_context.addIncludeDir(R"(C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.35.32215\include)"sv);
		run_test_case(input, expected_output_true);
#endif
	}

	SUBCASE("__COUNTER__") {
		const std::string input = R"(
			#define NAME(x) var_ ## x ## _ ## __COUNTER__
			const int NAME(foo) = 42;
			const int NAME(bar) = 84;
		  )";
		const std::string expected_output = R"(
			const int var_foo_0 = 42;
			const int var_bar_1 = 84;
		  )";
		run_test_case(input, expected_output);
	}

	SUBCASE("__VA_ARGS__") {
		const std::string input = R"(
			#define SUM(initial, ...) sum(initial, __VA_ARGS__)
			int sum(int x, int y, int z) { return x + y + z; }
			const int a = 1, b = 2, c = 3;
			const int total = SUM(4, a, b, c);
		  )";
		const std::string expected_output = R"(
			int sum(int x, int y, int z) { return x + y + z; }
			const int a = 1, b = 2, c = 3;
			const int total = sum(4, a, b, c);
		  )";
		run_test_case(input, expected_output);
	}

	SUBCASE("#undef") {
		const std::string input = R"(
			#define FOO 42
			#undef FOO
			#ifndef FOO
			  const bool has_foo = false;
			#else
			  const bool has_foo = true;
			#endif
		  )";
		const std::string expected_output = R"(
			const bool has_foo = false;
		  )";
		run_test_case(input, expected_output);
	}

	SUBCASE("__STDCPP_DEFAULT_NEW_ALIGNMENT__") {
		const std::string input = R"(
			const std::size_t alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
			const std::size_t size = 1024;
			void* ptr = ::operator new(size, std::align_val_t(alignment));
		  )";
		const std::string expected_output = R"(
			const std::size_t alignment = 8U;
			const std::size_t size = 1024;
			void* ptr = ::operator new(size, std::align_val_t(alignment));
		)";
#if (STDCPP_DEFAULT_NEW_ALIGNMENT == 8)
		run_test_case(input, expected_output);
#endif
	}
}

TEST_SUITE("Lexer") {
	TEST_CASE("Simple C++17 program") {
		const std::string input = R"(
			void foo();

			int main() {
			  foo();
			  return 0;
			}
		  )";

		Lexer lexer(input);
		std::vector<std::pair<Token::Type, std::string>> expected_tokens{
		  {Token::Type::Keyword, "void"},
		  {Token::Type::Identifier, "foo"},
		  {Token::Type::Punctuator, "("},
		  {Token::Type::Punctuator, ")"},
		  {Token::Type::Punctuator, ";"},
		  {Token::Type::Keyword, "int"},
		  {Token::Type::Identifier, "main"},
		  {Token::Type::Punctuator, "("},
		  {Token::Type::Punctuator, ")"},
		  {Token::Type::Punctuator, "{"},
		  {Token::Type::Identifier, "foo"},
		  {Token::Type::Punctuator, "("},
		  {Token::Type::Punctuator, ")"},
		  {Token::Type::Punctuator, ";"},
		  {Token::Type::Keyword, "return"},
		  {Token::Type::Literal, "0"},
		  {Token::Type::Punctuator, ";"},
		  {Token::Type::Punctuator, "}"},
		};

		for (const auto& expected_token : expected_tokens) {
			Token token = lexer.next_token();
			REQUIRE(token.type() == expected_token.first);
			REQUIRE(token.value() == expected_token.second);
		}

		CHECK(lexer.next_token().type() == Token::Type::EndOfFile);
	}
}

///
/// Parser
///

// Helper function to check if parsed ASTNode is a specific node type
template <typename T>
bool is_ast_node_type(const ASTNode& node) {
	return std::holds_alternative<T>(node);
}

TEST_SUITE("Parser") {
	TEST_CASE("Empty main() C++17 source string") {
		std::string_view code = R"(
			int main() {
				return 0;
			})";

		Lexer lexer(code);
		Parser parser(lexer);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		for (auto node_handle : ast)
		{
			std::visit([](const auto& val) {
				std::cout << "Type: " << typeid(val).name() << "\n";
			}, parser.get_inner_node(node_handle).node());
		}
	}
}
