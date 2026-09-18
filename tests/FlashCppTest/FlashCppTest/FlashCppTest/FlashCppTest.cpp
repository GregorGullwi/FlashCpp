#include "CompilerIncludes.h"

#include "CompileContext.h"
#include "FileTree.h"
#include "FileReader.h"
#include "Token.h"
#include "Lexer.h"
#include "Parser.h"
#include "IrGenerator.h"
#include "IRConverter.h"
#include "ChunkedAnyVector.h"
#include "InlineVector.h"
#include "TemplateRegistry.h"  // Includes ConceptRegistry as well
#include "InstantiationQueue.h"
#include "TemplateEngine.h"
#include <string>
#include <fstream>
#include <algorithm>
#include <typeindex>
#include <memory>
#include <span>
#include <cstdio>
#include <stdexcept>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../../../architecture/CanonicalTypeTests.h"

namespace {
void isolateGlobalSymbolTable() {
	gSymbolTable = SymbolTable();
}

struct GlobalSymbolTableIsolator : doctest::IReporter {
	explicit GlobalSymbolTableIsolator(const doctest::ContextOptions&) {}

	void report_query(const doctest::QueryData&) override {}
	void test_run_start() override {
		isolateGlobalSymbolTable();
	}
	void test_run_end(const doctest::TestRunStats&) override {
		isolateGlobalSymbolTable();
	}
	void test_case_start(const doctest::TestCaseData&) override {
		isolateGlobalSymbolTable();
	}
	void test_case_reenter(const doctest::TestCaseData&) override {
		isolateGlobalSymbolTable();
	}
	void test_case_end(const doctest::CurrentTestCaseStats&) override {
		isolateGlobalSymbolTable();
	}
	void test_case_exception(const doctest::TestCaseException&) override {}
	void subcase_start(const doctest::SubcaseSignature&) override {}
	void subcase_end() override {}
	void log_assert(const doctest::AssertData&) override {}
	void log_message(const doctest::MessageData&) override {}
	void test_case_skipped(const doctest::TestCaseData&) override {}
};

REGISTER_LISTENER("global_symbol_table_isolator", 1, GlobalSymbolTableIsolator);
} // namespace

static CompileContext compile_context;
static FrontendContext frontend_context;
static FileTree file_tree;

SemanticAnalysis& runSemanticAnalysisForTest(Parser& parser, CompileContext& context) {
	FlashCpp::gLazyMemberResolver.clearCache();
	SemanticAnalysis& sema = parser.semanticAnalysis();
	sema.run();
	return sema;
}

static const ASTNode* findReturnExprInNode(const ASTNode& node) {
	if (node.is<ReturnStatementNode>()) {
		const auto& return_stmt = node.as<ReturnStatementNode>();
		if (!return_stmt.expression().has_value()) {
			return nullptr;
		}
		return &*return_stmt.expression();
	}

	if (node.is<BlockNode>()) {
		for (const ASTNode& stmt : node.as<BlockNode>().get_statements()) {
			if (const ASTNode* return_expr = findReturnExprInNode(stmt)) {
				return return_expr;
			}
		}
	}

	return nullptr;
}

template <typename Predicate>
static const ASTNode* findAnyReturnExprNode(const Parser& parser, Predicate&& predicate) {
	for (const ASTNode& node : parser.get_nodes()) {
		const FunctionDeclarationNode* function = get_function_decl_node(node);
		if (function == nullptr) {
			continue;
		}
		if (!function->get_definition().has_value()) {
			continue;
		}
		const ASTNode& definition = *function->get_definition();
		if (const ASTNode* return_expr = findReturnExprInNode(definition)) {
			if (predicate(*return_expr)) {
				return return_expr;
			}
		}
	}
	return nullptr;
}

static const CallExprNode* findAnyReturnCallExpr(const Parser& parser) {
	const ASTNode* return_expr = findAnyReturnExprNode(parser, [](const ASTNode& expr) {
		return expr.is<ExpressionNode>() && std::holds_alternative<CallExprNode>(expr.as<ExpressionNode>());
	});
	if (return_expr == nullptr || !return_expr->is<ExpressionNode>()) {
		return nullptr;
	}
	return std::get_if<CallExprNode>(&return_expr->as<ExpressionNode>());
}

static const ASTNode* findAnyReturnCallExprNode(const Parser& parser) {
	return findAnyReturnExprNode(parser, [](const ASTNode& expr) {
		return expr.is<ExpressionNode>() && std::holds_alternative<CallExprNode>(expr.as<ExpressionNode>());
	});
}

static const ArraySubscriptNode* findAnyReturnSubscriptExpr(const Parser& parser) {
	const ASTNode* return_expr = findAnyReturnExprNode(parser, [](const ASTNode& expr) {
		return expr.is<ExpressionNode>() && std::holds_alternative<ArraySubscriptNode>(expr.as<ExpressionNode>());
	});
	if (return_expr == nullptr || !return_expr->is<ExpressionNode>()) {
		return nullptr;
	}
	return std::get_if<ArraySubscriptNode>(&return_expr->as<ExpressionNode>());
}

static bool compare_lexers_ignore_whitespace(Lexer& lexer1, Lexer& lexer2) {
	Token token1, token2;

	while (true) {
		token1 = lexer1.next_token();
		;
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
	file_reader.push_file_to_stack({__FILE__, __LINE__});
	CHECK(file_reader.preprocessFileContent(input));
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
		} else if (type == std::type_index(typeid(std::string))) {
			if (*reinterpret_cast<const std::string*>(arg) == "banana")
				++count;
		}
	});

	CHECK(count == 2);
}

TEST_CASE("InlineVector provides contiguous span storage after spilling past inline capacity") {
	TemplateVector<int, 2> values;
	values.push_back(10);
	values.push_back(20);
	values.push_back(30);
	values.insert(values.begin() + 1, 15);

	std::span<int> span = values;
	CHECK(span.size() == 4);
	CHECK(span.data() == values.data());
	CHECK(span[0] == 10);
	CHECK(span[1] == 15);
	CHECK(span[2] == 20);
	CHECK(span[3] == 30);
	CHECK(span.data() + 3 == &values[3]);

	span[2] = 25;
	CHECK(values[2] == 25);
}

TEST_CASE("InlineVector clears inline-held references immediately") {
	auto value = std::make_shared<int>(42);
	TemplateVector<std::shared_ptr<int>, 2> values;
	values.push_back(value);
	values.push_back(value);

	CHECK(value.use_count() == 3);

	values.pop_back();
	CHECK(value.use_count() == 2);

	values.clear();
	CHECK(value.use_count() == 1);
}

TEST_CASE("InlineVector preserves self-referential values when spilling to heap") {
	TemplateVector<std::string_view, 2> appended_values;
	appended_values.push_back("alpha");
	appended_values.push_back("beta");
	appended_values.push_back(appended_values[0]);

	CHECK(appended_values.size() == 3);
	CHECK(appended_values[0] == "alpha");
	CHECK(appended_values[1] == "beta");
	CHECK(appended_values[2] == "alpha");

	TemplateVector<std::string_view, 2> inserted_values;
	inserted_values.push_back("left");
	inserted_values.push_back("right");
	inserted_values.insert(inserted_values.begin() + 1, inserted_values[0]);

	CHECK(inserted_values.size() == 3);
	CHECK(inserted_values[0] == "left");
	CHECK(inserted_values[1] == "left");
	CHECK(inserted_values[2] == "right");

	// Regression: insert_at inline path must copy value before shifting
	// when value references an element at or after the insertion index.
	TemplateParamNameViewVector inline_insert;
	inline_insert.push_back("A");
	inline_insert.push_back("B");
	inline_insert.push_back("C");
	inline_insert.insert(inline_insert.begin(), inline_insert[2]); // insert "C" at front

	CHECK(inline_insert.size() == 4);
	CHECK(inline_insert[0] == "C");
	CHECK(inline_insert[1] == "A");
	CHECK(inline_insert[2] == "B");
	CHECK(inline_insert[3] == "C");
}

TEST_CASE("InlineVector supports vector-like insert and erase overloads") {
	TemplateVector<int, 3> values{1, 3};
	values.insert(values.begin() + 1, 2);
	values.insert(values.begin() + 2, 2, 7);

	CHECK(values.size() == 5);
	CHECK(values[0] == 1);
	CHECK(values[1] == 2);
	CHECK(values[2] == 7);
	CHECK(values[3] == 7);
	CHECK(values[4] == 3);

	values.erase(values.begin() + 1);
	CHECK(values.size() == 4);
	CHECK(values[0] == 1);
	CHECK(values[1] == 7);
	CHECK(values[2] == 7);
	CHECK(values[3] == 3);

	values.erase(values.begin() + 1, values.begin() + 3);
	CHECK(values.size() == 2);
	CHECK(values[0] == 1);
	CHECK(values[1] == 3);

	values.insert(values.end(), {4, 5});
	CHECK(values.size() == 4);
	CHECK(values[0] == 1);
	CHECK(values[1] == 3);
	CHECK(values[2] == 4);
	CHECK(values[3] == 5);

	auto it = values.emplace(values.begin() + 1, 9);
	CHECK(it == values.begin() + 1);
	CHECK(values.size() == 5);
	CHECK(values[0] == 1);
	CHECK(values[1] == 9);
	CHECK(values[2] == 3);
	CHECK(values[3] == 4);
	CHECK(values[4] == 5);
}

TEST_CASE("InlineVector supports vector-like assign, resize, and capacity helpers") {
	TemplateVector<int, 2> values;
	CHECK(values.capacity() == 2);

	values.assign(3, 4);
	CHECK(values.size() == 3);
	CHECK(values[0] == 4);
	CHECK(values[1] == 4);
	CHECK(values[2] == 4);
	CHECK(values.capacity() >= 3);

	values.resize(5, 8);
	CHECK(values.size() == 5);
	CHECK(values[0] == 4);
	CHECK(values[1] == 4);
	CHECK(values[2] == 4);
	CHECK(values[3] == 8);
	CHECK(values[4] == 8);

	values.resize(2);
	CHECK(values.size() == 2);
	CHECK(values[0] == 4);
	CHECK(values[1] == 4);

	values.assign({9, 10});
	CHECK(values.size() == 2);
	CHECK(values.at(0) == 9);
	CHECK(values.at(1) == 10);

	values.shrink_to_fit();
	CHECK(values.capacity() >= values.size());
}

TEST_CASE("Dependent and non-dependent type args produce different hashes") {
	TemplateTypeArg plain_arg = TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Int));
	TemplateTypeArg dependent_arg = plain_arg;
	dependent_arg.is_dependent = true;
	dependent_arg.dependent_name = StringTable::getOrInternStringHandle("T");

	CHECK_FALSE(plain_arg == dependent_arg);
	CHECK(plain_arg.hash() != dependent_arg.hash());
	CHECK(plain_arg.toHashString() != dependent_arg.toHashString());
	CHECK(TemplateTypeArgHash{}(plain_arg) != TemplateTypeArgHash{}(dependent_arg));
}

TEST_CASE("Non-dependent args with stale dependent_name compare equal") {
	// Defensive test: if a TemplateTypeArg is resolved (is_dependent=false) but
	// still carries a stale dependent_name from a prior dependent state, it must
	// compare equal to a cleanly-constructed non-dependent arg with the same type.
	// This matters for SpecializationKey lookup (TemplateRegistry_Pattern.h) and
	// recordDeduction consistency checks, where two args representing the same
	// concrete type must not be treated as different.
	TemplateTypeArg arg1 = TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Int));
	TemplateTypeArg arg2 = TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Int));
	// Simulate stale dependent_name left over after resolution
	arg2.dependent_name = StringTable::getOrInternStringHandle("T");

	// operator== must ignore dependent_name when is_dependent is false
	CHECK(arg1 == arg2);
	// hash contract: a == b → hash(a) == hash(b)
	CHECK(arg1.hash() == arg2.hash());
	CHECK(TemplateTypeArgHash{}(arg1) == TemplateTypeArgHash{}(arg2));
}

TEST_CASE("Instantiated names distinguish dependent type args") {
	TemplateTypeArg plain_arg = TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Int));
	TemplateTypeArg dependent_arg = plain_arg;
	dependent_arg.is_dependent = true;
	dependent_arg.dependent_name = StringTable::getOrInternStringHandle("T");
	std::vector<TemplateTypeArg> plain_args{plain_arg};
	std::vector<TemplateTypeArg> dependent_args{dependent_arg};

	auto plain_key = FlashCpp::makeInstantiationKey(
		StringTable::getOrInternStringHandle("Wrapper"),
		plain_args);
	auto dependent_key = FlashCpp::makeInstantiationKey(
		StringTable::getOrInternStringHandle("Wrapper"),
		dependent_args);

	CHECK_FALSE(plain_key == dependent_key);
	CHECK(FlashCpp::TemplateInstantiationKeyHash{}(plain_key) !=
		  FlashCpp::TemplateInstantiationKeyHash{}(dependent_key));
	CHECK(FlashCpp::generateInstantiatedNameFromArgs("Wrapper", plain_args) !=
		  FlashCpp::generateInstantiatedNameFromArgs("Wrapper", dependent_args));
}

TEST_CASE("ChunkedVector") {
	ChunkedVector<int, 2> vec;
	vec.push_back(1);
	vec.push_back(2);
	vec.push_back(3);

	CHECK(vec[0] == 1);
	CHECK(vec[1] == 2);
	CHECK(vec[2] == 3);

	for (int check = 0; int i : vec) {
		++check;
		CHECK(i == check);
	}
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

	SUBCASE("NestedConditionals") {
	// Test that nested conditionals inside a skipped block don't trigger errors
	// This was a bug where #error inside nested blocks would execute even when outer block was skipped
		const std::string input = R"(
			#ifdef OUTER_NOT_DEFINED
			  #ifndef INNER_NOT_DEFINED
			    #define RESULT 1
			  #else
			    #error This should NOT trigger
			  #endif
			#else
			  #define RESULT 2
			#endif
			int result = RESULT;
		  )";
		const std::string expected_output = R"(
			int result = 2;
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
		compile_context.addIncludeDir(R"(C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.38.33130\include)"sv);
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

	SUBCASE("__VA_OPT__") {
	// Test __VA_OPT__ with variadic arguments present
		const std::string input1 = R"(
			#define LOG(msg, ...) printf(msg __VA_OPT__(,) __VA_ARGS__)
			void test() {
				LOG("Hello %s", "world");
			}
		  )";
		const std::string expected_output1 = R"(
			void test() {
				printf("Hello %s" , "world");
			}
		  )";
		run_test_case(input1, expected_output1);

	// Test __VA_OPT__ with no variadic arguments
		const std::string input2 = R"(
			#define LOG(msg, ...) printf(msg __VA_OPT__(,) __VA_ARGS__)
			void test() {
				LOG("Hello");
			}
		  )";
		const std::string expected_output2 = R"(
			void test() {
				printf("Hello" );
			}
		  )";
		run_test_case(input2, expected_output2);
	}

	SUBCASE("#line directive") {
	// Test #line with just line number
		const std::string input1 = R"(
			int x = 1;
			#line 100
			int y = 2;
		  )";
	// We can't easily test the line number change in output, but we can verify it doesn't break
		run_test_case(input1, R"(
			int x = 1;
			int y = 2;
		  )");

	// Test #line with line number and filename
		const std::string input2 = R"(
			int x = 1;
			#line 50 "test.cpp"
			int y = 2;
		  )";
		run_test_case(input2, R"(
			int x = 1;
			int y = 2;
		  )");
	}

	SUBCASE("Predefined macros - __TIMESTAMP__") {
		const std::string input = R"(
			const char* timestamp = __TIMESTAMP__;
		  )";
	// We can't predict the exact timestamp, but we can verify it expands to a string
		CompileContext compile_context;
		FileTree file_tree;
		FileReader file_reader(compile_context, file_tree);
		file_reader.preprocessFileContent(input);
		const std::string& output = file_reader.get_result();
	// Check that __TIMESTAMP__ was replaced with something (should contain quotes)
		CHECK(output.find("__TIMESTAMP__") == std::string::npos);
		CHECK(output.find("timestamp = \"") != std::string::npos);
	}

	SUBCASE("Predefined macros - __INCLUDE_LEVEL__") {
		const std::string input = R"(
			int level = __INCLUDE_LEVEL__;
		  )";
		const std::string expected_output = R"(
			int level = 0;
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

TEST_SUITE("Parser") {
	TEST_CASE("Empty main() C++17 source string") {
		std::string_view code = R"(
			int main() {
				return 0;
			})";

		Lexer lexer(code);
		SemanticAnalysis parser_sema(compile_context, gSymbolTable);
		Parser parser(lexer, compile_context, parser_sema);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		for (auto& node_handle : ast) {
			std::printf("Type: %s\n", node_handle.type_name());
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
		SemanticAnalysis parser_sema(compile_context, gSymbolTable);
		Parser parser1(lexer1, compile_context, parser_sema);
		auto parse_result1 = parser1.parse();
		CHECK(!parse_result1.is_error());
		const auto& ast1 = parser1.get_nodes();

		// Test with auto and trailing return type
		Lexer lexer2(code_with_auto_return_type);
		SemanticAnalysis parser_sema2(compile_context, gSymbolTable);
		Parser parser2(lexer2, compile_context, parser_sema2);
		auto parse_result2 = parser2.parse();
		CHECK(!parse_result2.is_error());
		const auto& ast2 = parser2.get_nodes();

		// Compare AST nodes
		CHECK(ast1.size() == ast2.size());
		for (std::size_t i = 0; i < ast1.size(); ++i) {
			CHECK(typeid(ast1[i].type_name()) == typeid(ast2[i].type_name()));
		}
	}

	TEST_CASE("Function returning pointer to array") {
	// Test the pattern: char (*func(params))[size]
	// This is used by Windows SDK __countof_helper
		std::string_view code = R"(
			template <typename T, int N>
			char (*helper(T (&arr)[N]))[N];
		)";

		Lexer lexer(code);
		SemanticAnalysis parser_sema(compile_context, gSymbolTable);
		Parser parser(lexer, compile_context, parser_sema);
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		auto parse_result = parser.parse();

		if (parse_result.is_error()) {
			std::printf("Parse error: %s\n", parse_result.error_message().c_str());
		}
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();
	// Should have at least one node (the template function declaration)
		CHECK(ast.size() >= 1);
		std::printf("Parsed %zu AST nodes for function returning pointer to array\n", ast.size());
	}

	TEST_CASE("Reference to array parameter") {
	// Test the pattern: T (&arr)[N]
	// This is used in function parameters for array references
		std::string_view code = R"(
			template <typename T, int N>
			void process(T (&arr)[N]) {}
		)";

		Lexer lexer(code);
		SemanticAnalysis parser_sema(compile_context, gSymbolTable);
		Parser parser(lexer, compile_context, parser_sema);
		clearLegacyTypeTablesForTesting();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		auto parse_result = parser.parse();

		if (parse_result.is_error()) {
			std::printf("Parse error: %s\n", parse_result.error_message().c_str());
		}
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();
		CHECK(ast.size() >= 1);
		std::printf("Parsed %zu AST nodes for reference to array parameter\n", ast.size());
	}
}

TEST_SUITE("Code gen") {
	TEST_CASE("Empty main() C++17 source string") {
		std::string_view code = R"(
            int main() {
                return 1l;
            })";

		Lexer lexer(code);
		SemanticAnalysis parser_sema(compile_context, gSymbolTable);
		Parser parser(lexer, compile_context, parser_sema);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		runSemanticAnalysisForTest(parser, compile_context);
		AstToIr converter(gSymbolTable, compile_context, parser, parser_sema);
		for (auto& node_handle : ast) {
			converter.visit(node_handle);
		}

		// Now converter.ir should contain the IR for the code.
		const auto& ir = converter.getIr();

		std::puts("\n=== Test: Empty main() C++17 source string ===");

		// Let's just print the IR for now.
		for (const auto& instruction : ir.getInstructions()) {
			std::puts(instruction.getReadableString().c_str());
		}

		IrToObjConverter irConverter;
		irConverter.convert(ir, "return1.obj", "return1.cpp");

		COFFI::coffi ref;
		ref.load("tests/reference/return1_ref.obj");

		COFFI::coffi obj;
		obj.load("return1.obj");

	//CHECK(compare_obj(ref, obj));
	}
}

bool compare_obj(const COFFI::coffi& reader2, const COFFI::coffi& reader1, const std::string& file1_path = "", const std::string& file2_path = "") {
 // Compare section characteristics and flags
	const COFFI::sections& sections2 = reader2.get_sections();

 // Create a map of sections by name for the second reader
	std::map<std::string, const COFFI::section*> sections2_by_name;
	for (const auto& sec : sections2) {
		sections2_by_name[sec.get_name()] = &sec;
	}

 // Compare symbol table
	auto* symbols1 = reader1.get_symbols();
	auto* symbols2 = reader2.get_symbols();
	if (!symbols1 || !symbols2) {
		std::puts("One or both symbol tables are missing\n");
		return false;
	}

 // Create a map of symbols by name for the second reader
	std::map<std::string, const COFFI::symbol*> symbols2_by_name;
	for (const auto& sym : *symbols2) {
		symbols2_by_name[sym.get_name()] = &sym;
	}

 // Check that all symbols from reader1 exist in reader2
	bool all_symbols_found = true;
	for (const auto& sym1 : *symbols1) {
		const std::string& name = sym1.get_name();
		auto it = symbols2_by_name.find(name);
		if (it == symbols2_by_name.end()) {
			std::printf("Symbol %s not found in second file\n", name.c_str());
			all_symbols_found = false;
			continue;
		}
		const auto& sym2 = *it->second;

	// Compare symbol types and storage classes
		if (sym1.get_type() != sym2.get_type()) {
			std::printf("Symbol %s has different types: %d vs %d\n", name.c_str(), sym1.get_type(), sym2.get_type());
			all_symbols_found = false;
		}
		if (sym1.get_storage_class() != sym2.get_storage_class()) {
			std::printf("Symbol %s has different storage classes: %d vs %d\n", name.c_str(), sym1.get_storage_class(), sym2.get_storage_class());
			all_symbols_found = false;
		}
	}

 // Compare relocation entries for .text section
	auto find_section = [](const COFFI::coffi& reader, const std::string& name) -> const COFFI::section* {
		const auto& sections = reader.get_sections();
		for (const auto& sec : sections) {
			if (sec.get_name() == name) {
				return &sec;
			}
		}
		return nullptr;
	};

	auto text_section1 = find_section(reader1, ".text$mn");
	auto text_section2 = find_section(reader2, ".text$mn");
	if (text_section1 && text_section2) {
		const auto& relocs1 = text_section1->get_relocations();
		const auto& relocs2 = text_section2->get_relocations();
		if (relocs1.size() != relocs2.size()) {
			std::printf("Different number of relocations in .text$mn: %zu vs %zu\n", relocs1.size(), relocs2.size());
			return false;
		}

		for (size_t i = 0; i < relocs1.size(); i++) {
			const auto& reloc1 = relocs1[i];
			const auto& reloc2 = relocs2[i];

	// Compare relocation types and addresses
			if (reloc1.get_type() != reloc2.get_type()) {
				std::printf("Relocation %zu has different types: %d vs %d\n", i, reloc1.get_type(), reloc2.get_type());
				return false;
			}
		}
	}

 // Compare .drectve section content (linker directives)
	auto drectve1 = find_section(reader1, ".drectve");
	auto drectve2 = find_section(reader2, ".drectve");
	if (drectve1 && drectve2) {
		const char* data1 = drectve1->get_data();
		const char* data2 = drectve2->get_data();
		size_t size1 = drectve1->get_data_size();
		size_t size2 = drectve2->get_data_size();
		if (size1 != size2 || memcmp(data1, data2, size1) != 0) {
			std::puts("Different .drectve section content:\n");
			std::puts("First file: ");
			for (size_t i = 0; i < size1; i++) {
				if (data1[i] >= 32 && data1[i] <= 126) {
					std::printf("%c\n", data1[i]);
				} else {
					std::printf("\\x%02x\n", (unsigned char)data1[i]);
				}
			}
			std::puts("Second file: ");
			for (size_t i = 0; i < size2; i++) {
				if (data2[i] >= 32 && data2[i] <= 126) {
					std::printf("%c\n", data2[i]);
				} else {
					std::printf("\\x%02x\n", (unsigned char)data2[i]);
				}
			}
			return false;
		}
	}

 // Parse and compare debug information structures
	std::printf("\n=== Debug Information Comparison ===\n");

 // Helper function to parse and display debug symbols
	auto parse_debug_symbols = [](const char* data, size_t size, const std::string& file_name) {
		if (!data || size < 4) {
			std::printf("%s: No debug data or too small\n", file_name.c_str());
			return;
		}

		std::printf("\n--- %s Debug Symbols ---\n", file_name.c_str());

	// Skip 4-byte signature
		const uint8_t* start = reinterpret_cast<const uint8_t*>(data + 4);
		const uint8_t* ptr = start;
		const uint8_t* end = reinterpret_cast<const uint8_t*>(data + size);

		while (ptr < end - 8) { // Need at least 8 bytes for subsection header
	// Read subsection header
			uint32_t kind = *reinterpret_cast<const uint32_t*>(ptr);
			uint32_t length = *reinterpret_cast<const uint32_t*>(ptr + 4);

			std::printf("Subsection Kind: %u, Length: %u\n", kind, length);

	// Sanity check subsection length
			if (length == 0 || length > (end - ptr - 8)) {
				std::printf("  Invalid subsection length, stopping parse\n");
				break;
			}

			ptr += 8;
			const uint8_t* subsection_start = ptr;

			if (kind == 241) { // Symbols subsection
				const uint8_t* subsection_end = ptr + length;
				size_t symbol_count = 0;
				while (ptr < subsection_end - 4) {
					size_t offset_in_subsection = ptr - subsection_start;

		// Read symbol record header
					uint16_t record_length = *reinterpret_cast<const uint16_t*>(ptr);
					uint16_t record_kind = *reinterpret_cast<const uint16_t*>(ptr + 2);

		// Show hex bytes for debugging
					std::printf("  Symbol %zu at offset %zu: Length=%u, Kind=0x%04x [hex: ",
								symbol_count++, offset_in_subsection, record_length, record_kind);
					for (int i = 0; i < 8 && ptr + i < subsection_end; i++) {
						std::printf("%02x ", ptr[i]);
					}
					std::printf("]");

		// Sanity check the record length
					if (record_length == 0 || record_length > 1000) {
						std::printf(" (INVALID LENGTH - stopping parse)\n");
						std::printf("    Raw hex around this location: ");
						for (int i = -8; i < 16 && ptr + i >= subsection_start && ptr + i < subsection_end; i++) {
							std::printf("%02x ", ptr[i]);
						}
						std::printf("\n");
						break;
					}

					ptr += 4;

					if (record_kind == 0x1101) { // S_OBJNAME
						std::printf(" (S_OBJNAME)");
						if (ptr + 4 < subsection_end) {
							const uint8_t* name_ptr = ptr + 4; // Skip signature
		// Read null-terminated string without advancing main ptr
							std::string name;
							while (name_ptr < subsection_end && *name_ptr != 0) {
								name += static_cast<char>(*name_ptr++);
							}
							std::printf(": %s", name.c_str());
						}
					} else if (record_kind == 0x1147) { // S_GPROC32_ID
						std::printf(" (S_GPROC32_ID)");
						if (ptr + 32 < subsection_end) {
							uint32_t offset = *reinterpret_cast<const uint32_t*>(ptr + 28);
							uint16_t segment = *reinterpret_cast<const uint16_t*>(ptr + 32);
							const uint8_t* name_ptr = ptr + 35; // Skip to name
		// Read null-terminated string without advancing main ptr
							std::string name;
							while (name_ptr < subsection_end && *name_ptr != 0) {
								name += static_cast<char>(*name_ptr++);
							}
							std::printf(": [%04x:%08x] %s", segment, offset, name.c_str());
						}
					} else if (record_kind == 0x1012) { // S_FRAMEPROC
						std::printf(" (S_FRAMEPROC)");
					} else if (record_kind == 0x114F) { // S_PROC_ID_END
						std::printf(" (S_PROC_ID_END)");
					} else if (record_kind == 0x1111) { // S_REGREL32
						std::printf(" (S_REGREL32)");
						if (ptr + 10 < subsection_end) {
							uint32_t offset = *reinterpret_cast<const uint32_t*>(ptr);
							uint32_t type_index = *reinterpret_cast<const uint32_t*>(ptr + 4);
							uint16_t register_id = *reinterpret_cast<const uint16_t*>(ptr + 8);
							const uint8_t* name_ptr = ptr + 10;
		// Read null-terminated string without advancing main ptr
							std::string name;
							while (name_ptr < subsection_end && *name_ptr != 0) {
								name += static_cast<char>(*name_ptr++);
							}
							std::printf(": offset=0x%08x, type=0x%08x, reg=0x%04x, name=%s",
										offset, type_index, register_id, name.c_str());
						}
					} else if (record_kind == 0x113C) { // S_COMPILE3
						std::printf(" (S_COMPILE3)");
					} else if (record_kind == 0x1124) { // S_UNAMESPACE
						std::printf(" (S_UNAMESPACE)");
					} else if (record_kind == 0x114C) { // S_BUILDINFO
						std::printf(" (S_BUILDINFO)");
					} else if (record_kind == 0x113E) { // S_LOCAL
						std::printf(" (S_LOCAL)");
					} else if (record_kind == 0x1142) { // S_DEFRANGE_FRAMEPOINTER_REL
						std::printf(" (S_DEFRANGE_FRAMEPOINTER_REL)");
					} else {
		// Skip unknown record
						std::printf(" (Unknown record type)");
					}
					std::printf("\n");

		// Advance to next record: record_length includes the length field itself
		// So we need to advance by (record_length + 2) total, but we already advanced by 4
					size_t total_record_size = record_length + 2; // +2 for the length field itself
					size_t bytes_to_advance = total_record_size - 4; // -4 because we already read length+kind

					if (ptr + bytes_to_advance > subsection_end) {
						std::printf("  Record extends beyond subsection, stopping parse\n");
						break;
					}

					ptr += bytes_to_advance;
				}
			} else {
	// Skip other subsections
				std::printf("  (Skipping non-symbol subsection)\n");
			}

	// Always advance to the end of this subsection
			ptr = subsection_start + length;

	// Align to 4-byte boundary
			while ((reinterpret_cast<uintptr_t>(ptr) & 3) != 0 && ptr < end) {
				ptr++;
			}
		}
	};

	auto debug_s1 = find_section(reader1, ".debug$S");
	auto debug_s2 = find_section(reader2, ".debug$S");

	if (debug_s1) {
		parse_debug_symbols(debug_s1->get_data(), debug_s1->get_data_size(), "File1");
	} else {
		std::printf("File1: No .debug$S section found\n");
	}

	if (debug_s2) {
		parse_debug_symbols(debug_s2->get_data(), debug_s2->get_data_size(), "File2");
	} else {
		std::printf("File2: No .debug$S section found\n");
	}

	return all_symbols_found;
}

TEST_SUITE("Code gen") {
	TEST_CASE("Return integer from a function") {
		std::string_view code = R"(
            int return2() {
				return 4;
			}

            int main() {
                return return2();
            })";

		Lexer lexer(code);
		SemanticAnalysis parser_sema(compile_context, gSymbolTable);
		Parser parser(lexer, compile_context, parser_sema);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		runSemanticAnalysisForTest(parser, compile_context);
		AstToIr converter(gSymbolTable, compile_context, parser, parser_sema);
		for (auto& node_handle : ast) {
			converter.visit(node_handle);
		}

		const auto& ir = converter.getIr();

		std::puts("\n=== Test: Return integer from a function ===");

		for (const auto& instruction : ir.getInstructions()) {
			std::puts(instruction.getReadableString().c_str());
		}

		IrToObjConverter irConverter;
		irConverter.convert(ir, "return2func.obj");

		COFFI::coffi ref;
		ref.load("tests/reference/return2func_ref.obj");

		COFFI::coffi obj;
		obj.load("return2func.obj");

	//CHECK(compare_obj(ref, obj));
	}
}

TEST_SUITE("Code gen") {
	TEST_CASE("Returning parameter from a function") {
		std::string_view code = R"(
         int echo(int a) {
            return a;
         }

         int main() {
            return echo(5);
         })";

		Lexer lexer(code);
		SemanticAnalysis parser_sema(compile_context, gSymbolTable);
		Parser parser(lexer, compile_context, parser_sema);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		runSemanticAnalysisForTest(parser, compile_context);
		AstToIr converter(gSymbolTable, compile_context, parser, parser_sema);
		for (auto& node_handle : ast) {
			converter.visit(node_handle);
		}

		const auto& ir = converter.getIr();

		std::puts("\n=== Test: Returning parameter from a function ===");

		for (const auto& instruction : ir.getInstructions()) {
			std::puts(instruction.getReadableString().c_str());
		}

		IrToObjConverter irConverter;
		irConverter.convert(ir, "call_function_with_argument.obj");

	// Load reference object file
		COFFI::coffi ref;
		ref.load("tests/reference/call_function_with_argument_ref.obj");

	// Load generated object file
		COFFI::coffi obj;
		obj.load("call_function_with_argument.obj");

	// Compare reference and generated object files
	//CHECK(compare_obj(ref, obj));
	}
}

TEST_SUITE("Code gen"){
	TEST_CASE("Addition function"){
		std::string_view code = R"(
		 int add(int a, int b) {
            return a + b;
         }

         int main() {
            return add(3, 5);
         })";

Lexer lexer(code);
SemanticAnalysis parser_sema(compile_context, gSymbolTable);
Parser parser(lexer, compile_context, parser_sema);
auto parse_result = parser.parse();
CHECK(!parse_result.is_error());

const auto& ast = parser.get_nodes();

 runSemanticAnalysisForTest(parser, compile_context);
AstToIr converter(gSymbolTable, compile_context, parser, parser_sema);
for (auto& node_handle : ast) {
	converter.visit(node_handle);
}

const auto& ir = converter.getIr();

std::puts("\n=== Test: Addition function ===");

for (const auto& instruction : ir.getInstructions()) {
	std::puts(instruction.getReadableString().c_str());
}

IrToObjConverter irConverter;
irConverter.convert(ir, "add_function.obj");

  // Load reference object file
COFFI::coffi ref;
ref.load("tests/reference/add_function_ref.obj");

  // Load generated object file
COFFI::coffi obj;
obj.load("add_function.obj");

  // Compare reference and generated object files
  //CHECK(compare_obj(ref, obj));
}
}
;

TEST_SUITE("Code gen"){
	TEST_CASE("Function returning local variable"){
		std::string_view code = R"(
		 int add(int a, int b) {
			int c = a + b;
			return c;
         }

         int main() {
            return add(3, 5);
         })";

Lexer lexer(code);
SemanticAnalysis parser_sema(compile_context, gSymbolTable);
Parser parser(lexer, compile_context, parser_sema);
auto parse_result = parser.parse();
CHECK(!parse_result.is_error());

const auto& ast = parser.get_nodes();

runSemanticAnalysisForTest(parser, compile_context);
AstToIr converter(gSymbolTable, compile_context, parser, parser_sema);
for (auto& node_handle : ast) {
	converter.visit(node_handle);
}

const auto& ir = converter.getIr();

std::puts("\n=== Test: Function returning local variable ===");

for (const auto& instruction : ir.getInstructions()) {
	std::puts(instruction.getReadableString().c_str());
}

IrToObjConverter irConverter;
irConverter.convert(ir, "add_function_with_local_var.obj");
}
}
;

TEST_CASE("Arithmetic operations and nested function calls") {
	std::string_view code = R"(
		int add(int a, int b) {
			return a + b;
		}

		int subtract(int a, int b) {
			return a - b;
		}

		int multiply(int a, int b) {
			return a * b;
		}

		int divide(int a, int b) {
			return a / b;
		}

		int complex_math(int a, int b, int c, int d) {
			// This will test nested function calls and all arithmetic operations
			// (a + b) * (c - d) / (a + c)
			return divide(
				multiply(
					add(a, b),
					subtract(c, d)
				),
				add(a, c)
			);
		}

		int main() {
			return complex_math(10, 5, 20, 8);  // Should compute: (10 + 5) * (20 - 8) / (10 + 20) = 6
		})";

	Lexer lexer(code);
	SemanticAnalysis parser_sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, parser_sema);
	auto parse_result = parser.parse();
	CHECK(!parse_result.is_error());

	const auto& ast = parser.get_nodes();

	runSemanticAnalysisForTest(parser, compile_context);
	AstToIr converter(gSymbolTable, compile_context, parser, parser_sema);
	for (auto& node_handle : ast) {
		converter.visit(node_handle);
	}

	const auto& ir = converter.getIr();

	std::puts("\n=== Test: Arithmetic operations and nested function calls ===");

	for (const auto& instruction : ir.getInstructions()) {
		std::puts(instruction.getReadableString().c_str());
	}

	IrToObjConverter irConverter;
	irConverter.convert(ir, "arithmetic_test.obj");

 // Load reference object file
	COFFI::coffi ref;
	ref.load("tests/reference/arithmetic_test_ref.obj");

 // Load generated object file
	COFFI::coffi obj;
	obj.load("arithmetic_test.obj");

 // Compare reference and generated object files
 //CHECK(compare_obj(ref, obj));
}


TEST_CASE("SemanticAnalysis:ConcreteBodyRejectsParserOnlyHelperBeforeNormalization") {
	std::string code = "int phase4_boundary_target() { return 0; }";
	Lexer lexer(code);
	CompileContext test_context;
	test_context.setInputFile("test_lifecycle_negative_boundary.cpp");
	SemanticAnalysis parser_sema(test_context, gSymbolTable);
	Parser parser(lexer, test_context, parser_sema);
	auto parse_result = parser.parse();
	REQUIRE(!parse_result.is_error());

	const auto& roots = parser.get_nodes();
	REQUIRE(roots.size() == 1);
	REQUIRE(roots.front().is<FunctionDeclarationNode>());
	auto& function = const_cast<FunctionDeclarationNode&>(
		roots.front().as<FunctionDeclarationNode>());
	REQUIRE(function.is_materialized());
	REQUIRE(function.ownership_phase() == AstOwnershipPhase::ConcreteMaterialized);

	const Token& marker_token = function.decl_node().identifier_token();
	ASTNode pattern = ASTNode::emplace_node<ExpressionNode>(NumericLiteralNode(
		marker_token,
		0ULL,
		TypeCategory::Int,
		TypeQualifier::None,
		32));
	ASTNode helper = ASTNode::emplace_node<ExpressionNode>(
		PackExpansionExprNode(pattern, marker_token));
	ASTNode parent = ASTNode::emplace_node<ExpressionNode>(
		UnaryOperatorNode(marker_token, helper));
	ASTNode body = *function.get_definition();
	body.as<BlockNode>().add_statement_node(parent);

	try {
		parser_sema.run();
		FAIL("Concrete body containing PackExpansionExprNode reached semantic normalization");
	} catch (const InternalError& error) {
		const std::string message = error.what();
		CHECK(message.find("node kind=PackExpansionExprNode") != std::string::npos);
		CHECK(message.find("owner=phase4_boundary_target") != std::string::npos);
		CHECK(message.find("child=UnaryOperator.Operand[0]") != std::string::npos);
		CHECK(message.find("source token='phase4_boundary_target'") != std::string::npos);
	}
}

TEST_CASE("SemanticAnalysis:ResolvedDirectCallQueryTracksAnalysisState") {
	std::string code = R"(
		int foo() { return 7; }
		int main() { return foo(); }
	)";

	Lexer lexer(code);
	CompileContext test_context;
	test_context.setInputFile("test_resolved_direct_call_query.cpp");
	SemanticAnalysis parser_sema(test_context, gSymbolTable);
	Parser parser(lexer, test_context, parser_sema);
	auto parse_result = parser.parse();
	CHECK(!parse_result.is_error());
	if (parse_result.is_error()) {
		return;
	}

	const CallExprNode* call_expr = findAnyReturnCallExpr(parser);
	REQUIRE(call_expr != nullptr);

	ParserSemanticServices parser_services = parser.semanticAnalysis().parserSemanticServices();
	ResolvedFunctionQueryResult before_run = parser_services.getResolvedDirectCallQuery(call_expr);
	CHECK(before_run.state == ResolvedFunctionQueryResult::State::NotYetAnalyzed);
	CHECK(before_run.function == nullptr);
	CHECK(parser_services.getResolvedDirectCall(call_expr) == nullptr);

	SemanticAnalysis& sema = runSemanticAnalysisForTest(parser, test_context);
	ResolvedFunctionQueryResult after_run = sema.parserSemanticServices().getResolvedDirectCallQuery(call_expr);
	CHECK(after_run.state == ResolvedFunctionQueryResult::State::Available);
	REQUIRE(after_run.function != nullptr);
	CHECK(after_run.function->decl_node().identifier_token().value() == "foo"sv);
}

TEST_CASE("SemanticAnalysis:OverloadResolutionArgTypeQueryTracksAnalysisState") {
	std::string code = R"(
		int bar(int x) { return x; }
		int main() { return bar(1); }
	)";

	Lexer lexer(code);
	CompileContext test_context;
	test_context.setInputFile("test_overload_resolution_arg_query.cpp");
	SemanticAnalysis parser_sema(test_context, gSymbolTable);
	Parser parser(lexer, test_context, parser_sema);
	auto parse_result = parser.parse();
	CHECK(!parse_result.is_error());
	if (parse_result.is_error()) {
		return;
	}

	const CallExprNode* call_expr = findAnyReturnCallExpr(parser);
	REQUIRE(call_expr != nullptr);
	REQUIRE(call_expr->arguments().size() == 1);
	const ASTNode& arg_expr = call_expr->arguments()[0];

	ParserSemanticServices parser_services = parser.semanticAnalysis().parserSemanticServices();
	TypeSpecifierQueryResult before_run = parser_services.getOverloadResolutionArgTypeQuery(arg_expr);
	CHECK(before_run.state == TypeSpecifierQueryResult::State::NotYetAnalyzed);
	CHECK(!before_run.type.has_value());

	SemanticAnalysis& sema = runSemanticAnalysisForTest(parser, test_context);
	TypeSpecifierQueryResult after_run = sema.parserSemanticServices().getOverloadResolutionArgTypeQuery(arg_expr);
	CHECK(after_run.state == TypeSpecifierQueryResult::State::Available);
	REQUIRE(after_run.type.has_value());
	CHECK(after_run.type->type() == TypeCategory::Int);
}

TEST_CASE("SemanticAnalysis:ExpressionTypeQueryTracksAnalysisState") {
	std::string code = R"(
		int foo() { return 7; }
		int main() { return foo(); }
	)";

	Lexer lexer(code);
	CompileContext test_context;
	test_context.setInputFile("test_expression_type_query.cpp");
	SemanticAnalysis parser_sema(test_context, gSymbolTable);
	Parser parser(lexer, test_context, parser_sema);
	auto parse_result = parser.parse();
	CHECK(!parse_result.is_error());
	if (parse_result.is_error()) {
		return;
	}

	const ASTNode* return_expr = findAnyReturnCallExprNode(parser);
	REQUIRE(return_expr != nullptr);
	const CallExprNode* call_expr = findAnyReturnCallExpr(parser);
	REQUIRE(call_expr != nullptr);

	ParserSemanticServices parser_services = parser.semanticAnalysis().parserSemanticServices();
	TypeSpecifierQueryResult before_run = parser_services.getExpressionTypeQuery(*return_expr);
	CHECK(before_run.state == TypeSpecifierQueryResult::State::NotYetAnalyzed);
	CHECK(!before_run.type.has_value());

	SemanticAnalysis& sema = runSemanticAnalysisForTest(parser, test_context);
	TypeSpecifierQueryResult after_run = sema.parserSemanticServices().getExpressionTypeQuery(*return_expr);
	CHECK(after_run.state == TypeSpecifierQueryResult::State::Available);
	REQUIRE(after_run.type.has_value());
	CHECK(after_run.type->type() == TypeCategory::Int);
}

TEST_CASE("SemanticAnalysis:ResolvedSubscriptQueryTracksAnalysisState") {
	std::string code = R"(
		struct Buffer {
			int data[2];
			int& operator[](int index) { return data[index]; }
		};

		int main() {
			Buffer buffer{{3, 7}};
			return buffer[1];
		}
	)";

	Lexer lexer(code);
	CompileContext test_context;
	test_context.setInputFile("test_resolved_subscript_query.cpp");
	SemanticAnalysis parser_sema(test_context, gSymbolTable);
	Parser parser(lexer, test_context, parser_sema);
	auto parse_result = parser.parse();
	CHECK(!parse_result.is_error());
	if (parse_result.is_error()) {
		return;
	}

	const ArraySubscriptNode* subscript_expr = findAnyReturnSubscriptExpr(parser);
	REQUIRE(subscript_expr != nullptr);

	ParserSemanticServices parser_services = parser.semanticAnalysis().parserSemanticServices();
	ResolvedFunctionQueryResult before_run = parser_services.getResolvedOpSubscriptQuery(subscript_expr);
	CHECK(before_run.state == ResolvedFunctionQueryResult::State::NotYetAnalyzed);
	CHECK(before_run.function == nullptr);
	CHECK(parser_services.getResolvedOpSubscript(subscript_expr) == nullptr);

	SemanticAnalysis& sema = runSemanticAnalysisForTest(parser, test_context);
	ResolvedFunctionQueryResult after_run = sema.parserSemanticServices().getResolvedOpSubscriptQuery(subscript_expr);
	CHECK(after_run.state == ResolvedFunctionQueryResult::State::Available);
	REQUIRE(after_run.function != nullptr);
	CHECK(after_run.function->decl_node().identifier_token().value() == "operator[]"sv);
}

TEST_CASE("Templates:InheritedStaticStructMemberUsesInstantiatedOwner") {
	CompileContext test_context;
	test_context.setInputFile("tests/test_template_inherited_static_struct_member_ret13.cpp");

	FileTree test_file_tree;
	FileReader file_reader(test_context, test_file_tree);
	REQUIRE(file_reader.readFile(test_context.getInputFile().value()));
	const std::string& code = file_reader.get_result();

	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear();
	gConceptRegistry.clear();

	Lexer lexer(code, file_reader.get_line_map(), file_reader.get_file_paths());
	SemanticAnalysis parser_sema(test_context, gSymbolTable);
	Parser parser(lexer, test_context, parser_sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	auto parse_result = parser.parse();
	REQUIRE(!parse_result.is_error());

	runSemanticAnalysisForTest(parser, test_context);
	AstToIr converter(gSymbolTable, test_context, parser, parser_sema);
	for (auto& node_handle : parser.get_nodes()) {
		converter.visit(node_handle);
	}

	int instantiated_owner_count = 0;
	int derived_alias_count = 0;
	int pattern_alias_count = 0;
	for (const auto& instruction : converter.getIr().getInstructions()) {
		if (instruction.getOpcode() != IrOpcode::GlobalVariableDecl) {
			continue;
		}

		const auto& op = instruction.getTypedPayload<GlobalVariableDeclOp>();
		std::string_view global_name = StringTable::getStringView(op.getVarName());
		if (!global_name.ends_with("::payload")) {
			continue;
		}

		if (global_name == "Derived::payload") {
			++derived_alias_count;
		} else if (global_name == "Base::payload") {
			++pattern_alias_count;
		} else if (global_name.starts_with("Base$")) {
			++instantiated_owner_count;
		}
	}

	CHECK(instantiated_owner_count == 1);
	CHECK(derived_alias_count == 1);
	CHECK(pattern_alias_count == 0);
}


// ===== Log.h Tests =====
#include "Log.h"

TEST_CASE("Log:LogCategoryBitOperations") {
	using namespace FlashCpp;

 // Test OR operation
	LogCategory combined = LogCategory::Parser | LogCategory::Lexer;
	CHECK((static_cast<uint32_t>(combined) & static_cast<uint32_t>(LogCategory::Parser)) != 0);
	CHECK((static_cast<uint32_t>(combined) & static_cast<uint32_t>(LogCategory::Lexer)) != 0);
	CHECK((static_cast<uint32_t>(combined) & static_cast<uint32_t>(LogCategory::Templates)) == 0);

 // Test AND operation
	LogCategory andResult = combined & LogCategory::Parser;
	CHECK(static_cast<uint32_t>(andResult) == static_cast<uint32_t>(LogCategory::Parser));

 // Test None
	CHECK(static_cast<uint32_t>(LogCategory::None) == 0);

 // Test All
	CHECK(static_cast<uint32_t>(LogCategory::All) == 0xFFFFFFFF);
}

TEST_CASE("Log:LogLevelValues") {
	using namespace FlashCpp;

 // Verify log levels are in correct order (lower value = higher priority)
	CHECK(static_cast<uint8_t>(LogLevel::Error) < static_cast<uint8_t>(LogLevel::Warning));
	CHECK(static_cast<uint8_t>(LogLevel::Warning) < static_cast<uint8_t>(LogLevel::Info));
	CHECK(static_cast<uint8_t>(LogLevel::Info) < static_cast<uint8_t>(LogLevel::Debug));
	CHECK(static_cast<uint8_t>(LogLevel::Debug) < static_cast<uint8_t>(LogLevel::Trace));
}

TEST_CASE("Log:LogConfigRuntimeSettings") {
	using namespace FlashCpp;

 // Save original values
	LogLevel originalLevel = LogConfig::runtimeLevel;
	LogCategory originalCategories = LogConfig::runtimeCategories;
	std::ostream* originalStream = LogConfig::output_stream;

 // Test setLevel
	LogConfig::setLevel(LogLevel::Trace);
	CHECK(LogConfig::runtimeLevel == LogLevel::Trace);

	LogConfig::setLevel(LogLevel::Error);
	CHECK(LogConfig::runtimeLevel == LogLevel::Error);

 // Test setCategories
	LogConfig::setCategories(LogCategory::Parser);
	CHECK(static_cast<uint32_t>(LogConfig::runtimeCategories) == static_cast<uint32_t>(LogCategory::Parser));

 // Test enableCategory
	LogConfig::setCategories(LogCategory::None);
	LogConfig::enableCategory(LogCategory::Lexer);
	CHECK((static_cast<uint32_t>(LogConfig::runtimeCategories) & static_cast<uint32_t>(LogCategory::Lexer)) != 0);

	LogConfig::enableCategory(LogCategory::Parser);
	CHECK((static_cast<uint32_t>(LogConfig::runtimeCategories) & static_cast<uint32_t>(LogCategory::Parser)) != 0);
	CHECK((static_cast<uint32_t>(LogConfig::runtimeCategories) & static_cast<uint32_t>(LogCategory::Lexer)) != 0);

 // Test disableCategory
	LogConfig::disableCategory(LogCategory::Lexer);
	CHECK((static_cast<uint32_t>(LogConfig::runtimeCategories) & static_cast<uint32_t>(LogCategory::Lexer)) == 0);
	CHECK((static_cast<uint32_t>(LogConfig::runtimeCategories) & static_cast<uint32_t>(LogCategory::Parser)) != 0);

 // Test stream setters
	LogConfig::setOutputToStdout();
	CHECK(LogConfig::output_stream == &std::cout);

	LogConfig::setOutputToStderr();
	CHECK(LogConfig::output_stream == &std::cerr);

 // Restore original values
	LogConfig::setLevel(originalLevel);
	LogConfig::setCategories(originalCategories);
	LogConfig::setOutputStream(originalStream);
}
TEST_CASE("Log:LoggerLevelName") {
	using namespace FlashCpp;

	CHECK(Logger<LogLevel::Error, LogCategory::Parser>::levelName() == "ERROR");
	CHECK(Logger<LogLevel::Warning, LogCategory::Parser>::levelName() == "WARN ");
	CHECK(Logger<LogLevel::Info, LogCategory::Parser>::levelName() == "INFO ");
	CHECK(Logger<LogLevel::Debug, LogCategory::Parser>::levelName() == "DEBUG");
	CHECK(Logger<LogLevel::Trace, LogCategory::Parser>::levelName() == "TRACE");
}

TEST_CASE("Log:LoggerCategoryName") {
	using namespace FlashCpp;

	CHECK(Logger<LogLevel::Error, LogCategory::General>::categoryName() == "General");
	CHECK(Logger<LogLevel::Error, LogCategory::Parser>::categoryName() == "Parser");
	CHECK(Logger<LogLevel::Error, LogCategory::Lexer>::categoryName() == "Lexer");
	CHECK(Logger<LogLevel::Error, LogCategory::Templates>::categoryName() == "Templates");
	CHECK(Logger<LogLevel::Error, LogCategory::Symbols>::categoryName() == "Symbols");
	CHECK(Logger<LogLevel::Error, LogCategory::Types>::categoryName() == "Types");
	CHECK(Logger<LogLevel::Error, LogCategory::Codegen>::categoryName() == "Codegen");
	CHECK(Logger<LogLevel::Error, LogCategory::Scope>::categoryName() == "Scope");
	CHECK(Logger<LogLevel::Error, LogCategory::Mangling>::categoryName() == "Mangling");
}

TEST_CASE("Log:LogOutputCapture") {
	using namespace FlashCpp;

	// Save original config
	LogLevel originalLevel = LogConfig::runtimeLevel;
	LogCategory originalCategories = LogConfig::runtimeCategories;
	std::ostream* originalStream = LogConfig::output_stream;
	bool originalColors = LogConfig::use_colors;

	 // Disable colors for testing (avoid ANSI escape codes in output)
	LogConfig::setUseColors(false);

	// Setup capture
	std::ostringstream captureStream;
	LogConfig::setOutputStream(&captureStream);
	LogConfig::setLevel(LogLevel::Trace);
	LogConfig::setCategories(LogCategory::All);

	// Log a message (use Info level since Error goes to stderr)
	FLASH_LOG(Parser, Info, "Test message ", 42);

	std::string output = captureStream.str();
	CHECK(output.find("[INFO ]") != std::string::npos);
	CHECK(output.find("[Parser]") != std::string::npos);
	CHECK(output.find("Test message 42") != std::string::npos);

	// Clear and test different category
	captureStream.str("");
	FLASH_LOG(Lexer, Warning, "Lexer warning");

	output = captureStream.str();
	CHECK(output.find("[WARN ]") != std::string::npos);
	CHECK(output.find("[Lexer]") != std::string::npos);

	// Test category filtering - disable Parser
	captureStream.str("");
	LogConfig::setCategories(LogCategory::Lexer);  // Only enable Lexer

	FLASH_LOG(Parser, Info, "Should not appear");
	output = captureStream.str();
	 // Note: compile-time check may prevent this from being filtered at runtime
	 // if the category is disabled at compile time. Only check if the logger is
	 // compile-time enabled (all categories enabled by default).
	if (Logger<LogLevel::Info, LogCategory::Parser>::enabled) {
		CHECK(output.empty());  // Runtime filtering should block it
	}

	// Test level filtering
	captureStream.str("");
	LogConfig::setCategories(LogCategory::All);
	LogConfig::setLevel(LogLevel::Warning);	// Only Warning and Error

	FLASH_LOG(Parser, Debug, "Debug should not appear");
	output = captureStream.str();
	// Only check if Debug level is enabled at compile time
	if (Logger<LogLevel::Debug, LogCategory::Parser>::enabled) {
		CHECK(output.empty());  // Runtime filtering should block it
	}

	// Restore original config
	LogConfig::setLevel(originalLevel);
	LogConfig::setCategories(originalCategories);
	LogConfig::setOutputStream(originalStream);
	LogConfig::setUseColors(originalColors);
}

TEST_CASE("Log:LogMacroVariadicArgs") {
	using namespace FlashCpp;

	// Save original config
	LogLevel originalLevel = LogConfig::runtimeLevel;
	LogCategory originalCategories = LogConfig::runtimeCategories;
	std::ostream* originalStream = LogConfig::output_stream;
	bool originalColors = LogConfig::use_colors;

	// Disable colors for testing
	LogConfig::setUseColors(false);

	// Setup capture
	std::ostringstream captureStream;
	LogConfig::setOutputStream(&captureStream);
	LogConfig::setLevel(LogLevel::Trace);
	LogConfig::setCategories(LogCategory::All);

	// Test with multiple arguments
	FLASH_LOG(Parser, Info, "Value: ", 123, ", String: ", "test", ", Float: ", 3.14);

	std::string output = captureStream.str();
	CHECK(output.find("Value: 123") != std::string::npos);
	CHECK(output.find("String: test") != std::string::npos);
	CHECK(output.find("Float: 3.14") != std::string::npos);

	// Restore original config
	LogConfig::setLevel(originalLevel);
	LogConfig::setCategories(originalCategories);
	LogConfig::setOutputStream(originalStream);
	LogConfig::setUseColors(originalColors);
}

TEST_CASE("Log:GeneralCategoryNoPrefix") {
	using namespace FlashCpp;

	// Save original config
	LogLevel originalLevel = LogConfig::runtimeLevel;
	LogCategory originalCategories = LogConfig::runtimeCategories;
	std::ostream* originalStream = LogConfig::output_stream;

	// Setup capture
	std::ostringstream captureStream;
	LogConfig::setOutputStream(&captureStream);
	LogConfig::setLevel(LogLevel::Trace);
	LogConfig::setCategories(LogCategory::All);

	 // Test General category - should have no prefix
	FLASH_LOG(General, Info, "User message without prefix");

	std::string output = captureStream.str();
	CHECK(output.find("[") == std::string::npos);  // No brackets
	CHECK(output.find("User message without prefix") != std::string::npos);
	CHECK(output == "User message without prefix\n");

	// General category should always be enabled
	CHECK(Logger<LogLevel::Info, LogCategory::General>::enabled == true);

	// Restore original config
	LogConfig::setLevel(originalLevel);
	LogConfig::setCategories(originalCategories);
	LogConfig::setOutputStream(originalStream);
}
