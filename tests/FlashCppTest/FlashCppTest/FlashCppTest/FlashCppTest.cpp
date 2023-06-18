#include "CompileContext.h"
#include "FileTree.h"
#include "FileReader.h"
#include "Token.h"
#include "Lexer.h"
#include "Parser.h"
#include "CodeGen.h"
#include "IRConverter.h"
#include <string>
#include <algorithm>
#include <cctype>
#include <typeindex>
#include "ChunkedAnyVector.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

static CompileContext compile_context;
static FileTree file_tree;

static bool compare_lexers_ignore_whitespace(Lexer& lexer1, Lexer& lexer2) {
	Token token1, token2;

	while (true) {
		token1 = lexer1.next_token();;
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

TEST_CASE("ChunkedVector") {
	ChunkedAnyVector<> chunked_vector;

	int32_t& p1 = chunked_vector.push_back((int32_t)10);
	CHECK(p1 == 10);

	std::string& p2 = chunked_vector.push_back(std::string("banana"));
	CHECK(p2 == "banana");

	int count = 0;
	chunked_vector.visit([&](void* arg, auto&& type) {
		if (type == std::type_index(typeid(int32_t))) {
			if (*reinterpret_cast<const int32_t*>(arg) == 10)
				++count;
		}
		else if (type == std::type_index(typeid(std::string))) {
			if (*reinterpret_cast<const std::string*>(arg) == "banana")
				++count;
		}
	});
	CHECK(count == 2);
}

TEST_CASE("ChunkedVector") {
	ChunkedVector<int> vec;
	vec.push_back(1);
	vec.push_back(2);
	vec.push_back(3);

	CHECK(vec[0] == 1);
	CHECK(vec[1] == 2);
	CHECK(vec[2] == 3);
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
	[[maybe_unused]] const int num = DOUBLE(SQUARE(3));

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
	[[maybe_unused]] const char* str = STR(hello world);

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
		compile_context.addIncludeDir(R"(C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.36.32532\include)"sv);
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

		for (auto& node_handle : ast) {
			std::cout << "Type: " << node_handle.type_name() << "\n";
		}
	}

	TEST_CASE("Trailing return type for functions") {
		std::string_view code_with_return_type = R"(
			int main() {
				return 0;
			})";

		std::string_view code_with_auto_return_type = R"(
			auto main() -> int {
				return 0;
			})";

		// Test with function return type
		Lexer lexer1(code_with_return_type);
		Parser parser1(lexer1);
		auto parse_result1 = parser1.parse();
		CHECK(!parse_result1.is_error());
		const auto& ast1 = parser1.get_nodes();

		// Test with auto and trailing return type
		Lexer lexer2(code_with_auto_return_type);
		Parser parser2(lexer2);
		auto parse_result2 = parser2.parse();
		CHECK(!parse_result2.is_error());
		const auto& ast2 = parser2.get_nodes();

		// Compare AST nodes
		CHECK(ast1.size() == ast2.size());
		for (std::size_t i = 0; i < ast1.size(); ++i) {
			CHECK(typeid(ast1[i].type_name()) == typeid(ast2[i].type_name()));
		}
	}
}

TEST_SUITE("Code gen") {
	TEST_CASE("Empty main() C++17 source string") {
		std::string_view code = R"(
            int main() {
                return 1l;
            })";

		Lexer lexer(code);
		Parser parser(lexer);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter;
		for (auto& node_handle : ast) {
			converter.visit(node_handle);
		}

		// Now converter.ir should contain the IR for the code.
		const auto& ir = converter.getIr();

		// Let's just print the IR for now.
		for (const auto& instruction : ir.getInstructions()) {
			std::cout << instruction.getReadableString() << "\n";
		}

		IrToObjConverter irConverter;
		irConverter.convert(ir, "return1.obj");
	}
}

bool compare_obj(const COFFI::coffi& ref, const COFFI::coffi& obj)
{
	return true;
}

TEST_SUITE("Code gen") {
	TEST_CASE("Return integer from a function") {
		std::string_view code = R"(
            int return2();

            int main() {
                return2();
				return 3;
            }

			int return2() {
				return 2;
            })";

		Lexer lexer(code);
		Parser parser(lexer);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter;
		for (auto& node_handle : ast) {
			converter.visit(node_handle);
		}

		// Now converter.ir should contain the IR for the code.
		const auto& ir = converter.getIr();

		// Let's just print the IR for now.
		for (const auto& instruction : ir.getInstructions()) {
			std::cout << instruction.getReadableString() << "\n";
		}

		IrToObjConverter irConverter;
		irConverter.convert(ir, "return2func.obj");

		COFFI::coffi ref;
		ref.load("return2func_ref.obj");

		COFFI::coffi obj;
		obj.load("return2func.obj");

		CHECK(compare_obj(ref, obj));
	}
}
