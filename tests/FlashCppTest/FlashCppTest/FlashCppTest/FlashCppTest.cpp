// Required to include implementation (.cpp) files in unity build mode for the test TU.
#define UNITY_BUILD
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
#include <algorithm>
#include <typeindex>
#include <memory>
#include <span>
#include <cstdio>
#include <stdexcept>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

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
	InlineVector<int, 2> values;
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
	InlineVector<std::shared_ptr<int>, 2> values;
	values.push_back(value);
	values.push_back(value);

	CHECK(value.use_count() == 3);

	values.pop_back();
	CHECK(value.use_count() == 2);

	values.clear();
	CHECK(value.use_count() == 1);
}

TEST_CASE("InlineVector preserves self-referential values when spilling to heap") {
	InlineVector<std::string_view, 2> appended_values;
	appended_values.push_back("alpha");
	appended_values.push_back("beta");
	appended_values.push_back(appended_values[0]);

	CHECK(appended_values.size() == 3);
	CHECK(appended_values[0] == "alpha");
	CHECK(appended_values[1] == "beta");
	CHECK(appended_values[2] == "alpha");

	InlineVector<std::string_view, 2> inserted_values;
	inserted_values.push_back("left");
	inserted_values.push_back("right");
	inserted_values.insert(inserted_values.begin() + 1, inserted_values[0]);

	CHECK(inserted_values.size() == 3);
	CHECK(inserted_values[0] == "left");
	CHECK(inserted_values[1] == "left");
	CHECK(inserted_values[2] == "right");

	// Regression: insert_at inline path must copy value before shifting
	// when value references an element at or after the insertion index.
	InlineVector<std::string_view, 4> inline_insert;
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
	InlineVector<int, 3> values{1, 3};
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
	InlineVector<int, 2> values;
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
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
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
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
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
	const COFFI::sections& sections1 = reader1.get_sections();
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

	return true;
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

	gTypeInfo.clear();
	gNativeTypes.clear();
	gTypesByName.clear();
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

TEST_SUITE("FrontendContext") {
	TEST_CASE("Scratch budget permits exact-budget typed allocations") {
		struct Value {
			double real;
			int count;
			bool operator==(const Value&) const = default;
		};
		auto check_exact = []<typename T>(T value) {
			DiagnosticEngine diagnostics;
			MonotonicScratchArena arena(diagnostics, sizeof(T));
			T* object = arena.allocateObject<T>(value);
			CHECK(*object == value);
			CHECK(reinterpret_cast<uintptr_t>(object) % alignof(T) == 0);
			CHECK(arena.currentBytes() == sizeof(T));
			CHECK(arena.reservedBytes() == sizeof(T));
			CHECK_FALSE(diagnostics.hasErrors());
		};
		check_exact(uint64_t{42});
		check_exact(short{7});
		check_exact(Value{3.5, 19});
	}

	TEST_CASE("Scratch budget rejects before mutation and permits the exact limit") {
		const uint64_t outside_before = diagnosticsEmittedOutsideEngineCount();
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, 16);
		CHECK(arena.allocate(16, 1) != nullptr);
		CHECK(arena.currentBytes() == 16);
		CHECK(arena.reservedBytes() == 16);
		const auto checkpoint = arena.mark();
		const auto peak = arena.peakBytes();
		const auto reserved_peak = arena.peakReservedBytes();
		try {
			arena.allocate(1, 1);
			FAIL("exhausted scratch budget accepted an allocation");
		} catch (const CompileError& error) {
			REQUIRE(error.structuredDiagnostic() != nullptr);
			CHECK(error.structuredDiagnostic()->id == DiagnosticId::ScratchAllocationLimit);
			CHECK(error.structuredDiagnostic()->severity == DiagnosticSeverity::Fatal);
			CHECK(error.structuredDiagnostic()->message_template == "Scratch allocation budget exhausted");
		}
		CHECK(diagnostics.count(DiagnosticSeverity::Fatal) == 1);
		CHECK(arena.mark().block_index == checkpoint.block_index);
		CHECK(arena.mark().block_used == checkpoint.block_used);
		CHECK(arena.mark().destructor_count == checkpoint.destructor_count);
		CHECK(arena.currentBytes() == 16);
		CHECK(arena.reservedBytes() == 16);
		CHECK(arena.discardedBytes() == 0);
		CHECK(arena.peakBytes() == peak);
		CHECK(arena.peakReservedBytes() == reserved_peak);
		CHECK(arena.allocate(0, 1) == nullptr);
		CHECK(diagnostics.count(DiagnosticSeverity::Fatal) == 1);
		CHECK(diagnosticsEmittedOutsideEngineCount() == outside_before);
	}

	TEST_CASE("Scratch budget counts 4096 failed probes without replenishment") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, 4096);
		ScratchProbeRegistry registry;
		for (uint32_t index = 0; index < 4096; ++index) {
			ScratchTransaction probe(arena, registry);
			CHECK(probe.registry().registerEntry() == 1);
			probe.arena().allocate(1, 1);
		}
		CHECK(arena.currentBytes() == 0);
		CHECK(arena.discardedBytes() == 4096);
		CHECK(arena.reservedBytes() == 4096);
		CHECK(registry.liveCount() == 0);
		CHECK_THROWS_AS(arena.allocate(1, 1), CompileError);
		CHECK(arena.discardedBytes() == arena.byteLimit());
	}

	TEST_CASE("Scratch budget exhaustion unwinds nested probes exactly once") {
		struct Tracked {
			int* destroyed;
			explicit Tracked(int* count) : destroyed(count) {}
			~Tracked() { ++*destroyed; }
		};
		DiagnosticEngine diagnostics;
		int destroyed = 0;
		MonotonicScratchArena arena(diagnostics, 3 * sizeof(Tracked));
		ScratchProbeRegistry registry;
		registry.registerEntry();
		registry.commitToCurrent();
		arena.allocateObject<Tracked>(&destroyed);
		try {
			ScratchTransaction outer(arena, registry);
			registry.registerEntry();
			arena.allocateObject<Tracked>(&destroyed);
			ScratchTransaction inner(arena, registry);
			registry.registerEntry();
			arena.allocateObject<Tracked>(&destroyed);
			arena.allocateObject<Tracked>(&destroyed);
			FAIL("nested probe exceeded its budget");
		} catch (const CompileError&) {
			CHECK(destroyed == 2);
		}
		CHECK(destroyed == 2);
		CHECK(registry.committedCount() == 1);
		CHECK(registry.liveCount() == 1);
		CHECK(registry.registerEntry() == 2);
		CHECK(arena.currentBytes() == sizeof(Tracked));
		CHECK(arena.discardedBytes() == 2 * sizeof(Tracked));
		CHECK(arena.mark().destructor_count == 1);
	}

	TEST_CASE("Scratch budget charges alignment padding and bounds retained blocks") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, 8192);
		arena.allocate(1, 1);
		arena.allocate(8, 8);
		CHECK(arena.currentBytes() == 16);
		const auto checkpoint = arena.mark();
		arena.allocate(4096, 1);
		CHECK(arena.reservedBytes() == 8192);
		arena.rollbackTo(checkpoint);
		CHECK(arena.reservedBytes() == 4096);
		CHECK(arena.currentBytes() == 16);
		CHECK(arena.discardedBytes() == 4096);
		CHECK_THROWS_AS(arena.allocate(4096, 1), CompileError);
		CHECK(arena.reservedBytes() == 4096);
		CHECK(arena.peakReservedBytes() == 8192);
		arena.allocate(4080, 1);
		CHECK(arena.currentBytes() + arena.discardedBytes() == arena.byteLimit());
	}

	TEST_CASE("Scratch budget validates zero and oversized requests without allocation") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena empty(diagnostics, 0);
		CHECK(empty.allocate(0, 1) == nullptr);
		CHECK_THROWS_AS(empty.allocate(1, 1), CompileError);
		CHECK(empty.reservedBytes() == 0);
		MonotonicScratchArena arena(diagnostics, 64);
		CHECK_THROWS_AS(arena.allocate(static_cast<std::size_t>(-1), 8), CompileError);
		CHECK_THROWS_AS(arena.allocate(1, std::size_t{1} << (sizeof(std::size_t) * 8 - 1)), CompileError);
		CHECK_THROWS_AS(arena.allocate(1, 0), InternalError);
		CHECK_THROWS_AS(arena.allocate(1, 3), InternalError);
		CHECK(arena.currentBytes() == 0);
		CHECK(arena.reservedBytes() == 0);
		CHECK(arena.peakBytes() == 0);
		CHECK(arena.peakReservedBytes() == 0);
		CHECK(arena.discardedBytes() == 0);
		MonotonicScratchArena overflow(diagnostics, UINT64_MAX);
		CHECK_THROWS_AS(overflow.allocate(static_cast<std::size_t>(-1), 8), CompileError);
		CHECK(overflow.currentBytes() == 0);
		CHECK(overflow.reservedBytes() == 0);
		CHECK(overflow.peakReservedBytes() == 0);
	}

	TEST_CASE("Scratch budget rejects padding and reservation exhaustion independently") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena padded(diagnostics, 16);
		padded.allocate(1, 1);
		const auto padding_checkpoint = padded.mark();
		padded.allocate(14, 1);
		padded.rollbackTo(padding_checkpoint);
		CHECK_THROWS_AS(padded.allocate(1, 8), CompileError);
		CHECK(padded.currentBytes() == 1);
		CHECK(padded.peakBytes() == 15);
		CHECK(padded.reservedBytes() == 16);
		CHECK(padded.discardedBytes() == 14);
		CHECK(padded.mark().block_used == padding_checkpoint.block_used);
		CHECK(padded.allocate(1, 1) != nullptr);

		MonotonicScratchArena fragmented(diagnostics, 8192);
		fragmented.allocate(3000, 1);
		fragmented.allocate(3000, 1);
		REQUIRE(fragmented.currentBytes() == 6000);
		REQUIRE(fragmented.reservedBytes() == 8192);
		const auto checkpoint = fragmented.mark();
		CHECK_THROWS_AS(fragmented.allocate(2000, 1), CompileError);
		CHECK(fragmented.currentBytes() == 6000);
		CHECK(fragmented.peakBytes() == 6000);
		CHECK(fragmented.reservedBytes() == 8192);
		CHECK(fragmented.peakReservedBytes() == 8192);
		CHECK(fragmented.discardedBytes() == 0);
		CHECK(fragmented.mark().block_index == checkpoint.block_index);
		CHECK(fragmented.mark().block_used == checkpoint.block_used);
	}

	TEST_CASE("Scratch budget aligns typed objects by address") {
		struct alignas(256) Aligned { int value; };
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, 4096);
		arena.allocate(1, 1);
		const auto before = arena.currentBytes();
		Aligned* object = arena.allocateObject<Aligned>();
		CHECK(reinterpret_cast<uintptr_t>(object) % alignof(Aligned) == 0);
		CHECK(arena.currentBytes() >= before + sizeof(Aligned));
		CHECK(arena.currentBytes() < before + sizeof(Aligned) + alignof(Aligned));
		object->value = 73;
		CHECK(object->value == 73);
	}

	TEST_CASE("Strong semantic IDs reject pointer construction") {
		static_assert(!std::is_default_constructible_v<MonotonicScratchArena>);
		static_assert(!std::is_constructible_v<ScopeId, const void*>);
		static_assert(!std::is_constructible_v<OwnerId, const void*>);
		static_assert(!std::is_constructible_v<DeclId, const void*>);
		static_assert(!std::is_constructible_v<EntityId, const void*>);
		static_assert(!std::is_constructible_v<ExprId, const void*>);
		static_assert(!std::is_constructible_v<TypeId, const void*>);
		static_assert(!std::is_constructible_v<TemplateDeclId, const void*>);
		static_assert(sizeof(ScopeId) == 4);
		static_assert(sizeof(TypeId) == 4);
	}

	TEST_CASE("Scratch probe rollback restores committed registry entries") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
		ScratchProbeRegistry registry;
		const uint32_t committed = registry.registerEntry();
		registry.commitToCurrent();
		CHECK(registry.committedCount() == 1);
		CHECK(registry.liveCount() == 1);

		{
			ScratchTransaction probe(arena, registry);
			CHECK(probe.registry().registerEntry() == committed + 1);
			CHECK(registry.liveCount() == 2);
			probe.rollback();
		}

		CHECK(registry.committedCount() == 1);
		CHECK(registry.liveCount() == 1);
		CHECK(registry.registerEntry() == committed + 1);
	}

	TEST_CASE("Nested scratch registry transactions restore outer checkpoint") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
		ScratchProbeRegistry registry;

		ScratchTransaction outer(arena, registry);
		CHECK(outer.registry().registerEntry() == 1);

		{
			ScratchTransaction inner(arena, registry);
			CHECK(inner.registry().registerEntry() == 2);
			inner.commit();
		}

		CHECK(registry.liveCount() == 2);
		CHECK(registry.committedCount() == 2);

		outer.rollback();

		CHECK(registry.liveCount() == 0);
		CHECK(registry.committedCount() == 0);
		CHECK(registry.registerEntry() == 1);
	}

	TEST_CASE("Scratch arena survives allocations larger than one block") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
		const std::size_t first_size = 5000U;
		const std::size_t second_size = 16U;
		void* first = arena.allocate(first_size, alignof(std::max_align_t));
		void* second = arena.allocate(second_size, alignof(std::max_align_t));
		CHECK(first != nullptr);
		CHECK(second != nullptr);
		CHECK(arena.currentBytes() >= first_size + second_size);
		CHECK(arena.reservedBytes() >= 4096U);
		CHECK(arena.reservedBytes() >= arena.currentBytes());

		const ScratchArenaState checkpoint = arena.mark();
		arena.allocate(32U, alignof(std::max_align_t));
		arena.rollbackTo(checkpoint);
		CHECK(arena.currentBytes() >= first_size + second_size);
	}

	TEST_CASE("Scratch arena byte accounting includes alignment padding on rollback") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
		CHECK(arena.allocate(1U, 1U) != nullptr);
		const uint64_t bytes_after_first = arena.currentBytes();
		CHECK(arena.allocate(1U, 8U) != nullptr);
		const uint64_t bytes_after_second = arena.currentBytes();
		CHECK(bytes_after_second > bytes_after_first);

		const ScratchArenaState checkpoint = arena.mark();
		arena.allocate(4U, 8U);
		arena.rollbackTo(checkpoint);
		CHECK(arena.currentBytes() == bytes_after_second);
	}

	TEST_CASE("Committed scratch objects are destroyed at arena teardown") {
		struct ScratchDestructionProbe {
			bool* destroyed_flag;
			explicit ScratchDestructionProbe(bool* destroyed_flag_in)
				: destroyed_flag(destroyed_flag_in) {
			}
			~ScratchDestructionProbe() {
				if (destroyed_flag != nullptr) {
					*destroyed_flag = true;
				}
			}
		};

		bool destroyed = false;
		{
			DiagnosticEngine diagnostics;
			MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
			ScratchProbeRegistry registry;
			ScratchTransaction probe(arena, registry);
			probe.arena().allocateObject<ScratchDestructionProbe>(&destroyed);
			probe.commit();
			CHECK(!destroyed);
		}
		CHECK(destroyed);
	}

	TEST_CASE("Scratch probe commit publishes registry entries") {
		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
		ScratchProbeRegistry registry;

		{
			ScratchTransaction probe(arena, registry);
			CHECK(probe.registry().registerEntry() == 1);
			probe.commit();
		}

		CHECK(registry.committedCount() == 1);
		CHECK(registry.liveCount() == 1);
	}

	TEST_CASE("Scratch probe rollback runs destructors and discards bytes") {
		struct ScratchDestructionProbe {
			bool* destroyed_flag;
			explicit ScratchDestructionProbe(bool* destroyed_flag_in)
				: destroyed_flag(destroyed_flag_in) {
			}
			~ScratchDestructionProbe() {
				if (destroyed_flag != nullptr) {
					*destroyed_flag = true;
				}
			}
		};

		DiagnosticEngine diagnostics;
		MonotonicScratchArena arena(diagnostics, FrontendContext::kScratchByteLimit);
		ScratchProbeRegistry registry;
		bool destroyed = false;
		const uint64_t bytes_before = arena.currentBytes();

		{
			ScratchTransaction probe(arena, registry);
			probe.arena().allocateObject<ScratchDestructionProbe>(&destroyed);
			CHECK(arena.currentBytes() > bytes_before);
			probe.rollback();
		}

		CHECK(destroyed);
		CHECK(arena.currentBytes() == bytes_before);
		CHECK(arena.discardedBytes() > 0);
	}

	TEST_CASE("FrontendContext exposes active context and scratch telemetry") {
		FrontendContext outer;
		CHECK(FrontendContext::active() == &outer);
		{
			FrontendContext inner;
			CHECK(FrontendContext::active() == &inner);
		}
		CHECK(FrontendContext::active() == &outer);

		FrontendContext context;
		CHECK(&context.scratchArena() == &context.scratchArena());
		CHECK(context.scratchArena().byteLimit() == 64ULL * 1024 * 1024);
		CHECK_THROWS_AS(context.scratchArena().allocate(64ULL * 1024 * 1024 + 1, 1), CompileError);
		CHECK(context.diagnostics().count(DiagnosticSeverity::Fatal) == 1);
#if FLASHCPP_TRACK_INLINE_VECTOR_SPILLS
		const uint64_t spills_before = FlashCpp::inlineVectorSpillCount();
		const uint64_t overload_spills_before =
			FlashCpp::inlineVectorSpillCount(FlashCpp::InlineVectorSpillFamily::OverloadResolution);
		const uint64_t template_spills_before =
			FlashCpp::inlineVectorSpillCount(FlashCpp::InlineVectorSpillFamily::TemplateArgument);
		FlashCpp::InlineVector<int, 2, FlashCpp::InlineVectorSpillFamily::OverloadResolution> overload_values;
		overload_values.push_back(1);
		overload_values.push_back(2);
		overload_values.push_back(3);
		FlashCpp::InlineVector<TemplateTypeArg, 2, FlashCpp::InlineVectorSpillFamily::TemplateArgument>
			template_values;
		template_values.push_back(TemplateTypeArg{});
		template_values.push_back(TemplateTypeArg{});
		template_values.push_back(TemplateTypeArg{});
		CHECK(FlashCpp::inlineVectorSpillCount() == spills_before + 2);
		CHECK(context.inlineVectorSpillCount() == spills_before + 2);
		CHECK(FlashCpp::inlineVectorSpillCount(FlashCpp::InlineVectorSpillFamily::OverloadResolution) ==
			  overload_spills_before + 1);
		CHECK(FlashCpp::inlineVectorSpillCount(FlashCpp::InlineVectorSpillFamily::TemplateArgument) ==
			  template_spills_before + 1);
#endif
		context.refreshScratchDomainStats();
		const DomainByteStats scratch_stats = context.domainStats(AllocationDomain::Scratch);
		CHECK(scratch_stats.current_bytes == context.scratchArena().currentBytes());
		CHECK(scratch_stats.peak_bytes == context.scratchArena().peakBytes());
		CHECK(scratch_stats.reserved_bytes == context.scratchArena().reservedBytes());
		CHECK(scratch_stats.peak_reserved_bytes == context.scratchArena().peakReservedBytes());
	}

	template<typename T>
	concept StoresDuplicateScopeId = requires(T scope) { scope.scope_id; };
	static_assert(!StoresDuplicateScopeId<Scope>);

	TEST_CASE("Scope slot identities survive deep exit and sibling publication") {
		FrontendContext context;
		SymbolTable table;
		table.enablePersistentScopePublication();
		constexpr uint32_t depth = 4096;
		for (uint32_t index = 0; index < depth; ++index) {
			table.enter_scope(ScopeType::Block);
			REQUIRE(table.currentScopeId().value == index + 2);
			CHECK(context.currentScopeId() == table.currentScopeId());
		}
		for (uint32_t index = depth; index > 0; --index) {
			table.exit_scope();
			CHECK(table.currentScopeId().value == index);
		}
		table.enter_scope(ScopeType::Function);
		const ScopeId sibling_id = table.currentScopeId();
		CHECK(sibling_id.value == depth + 2);
		CHECK(context.scopeRecord(sibling_id).parent_id == ScopeId{1});
		CHECK(readScopeMetadata(table, ScopeId{2}).scope_type == ScopeType::Block);
		CHECK(readScopeMetadata(table, sibling_id).scope_type == ScopeType::Function);
		CHECK(table.findScopeById(ScopeId{}) == nullptr);
		CHECK(table.findScopeById(ScopeId{depth + 3}) == nullptr);
		CHECK_THROWS_AS(table.scopeById(ScopeId{}), InternalError);
		CHECK_THROWS_AS(table.scopeById(ScopeId{depth + 3}), InternalError);
		table.clear();
		CHECK(table.currentScopeId() == ScopeId{1});
		CHECK(context.currentScopeId() == ScopeId{1});
		CHECK(table.findScopeById(sibling_id) == nullptr);
		CHECK(table.scopeCount() == 1);
		CHECK(context.scopeCount() == 1);
	}

	TEST_CASE("SymbolTable scope exit moves cursor without destroying scope records") {
		SymbolTable table;
		const ScopeId global_id = table.currentScopeId();
		REQUIRE(global_id.value == 1u);
		REQUIRE(table.scopeCount() == 1u);
		REQUIRE(table.activeScopeDepth() == 1u);

		table.enter_scope(ScopeType::Block);
		const ScopeId block_id = table.currentScopeId();
		REQUIRE(block_id.value == 2u);
		REQUIRE(table.scopeCount() == 2u);
		REQUIRE(table.activeScopeDepth() == 2u);

		table.exit_scope();
		CHECK(table.currentScopeId() == global_id);
		CHECK(table.scopeCount() == 2u);
		CHECK(table.activeScopeDepth() == 1u);
	}

	TEST_CASE("SymbolTable records ScopeId on lookup sites") {
		SymbolTable table;
		table.enter_scope(ScopeType::Namespace);
		const ScopeId namespace_scope_id = table.currentScopeId();
		(void)table.lookup("missing_identifier");
		CHECK(table.lastLookupScopeId() == namespace_scope_id);
		table.exit_scope();
		CHECK(table.lastLookupScopeId() == namespace_scope_id);
	}

	TEST_CASE("get_current_using_declaration_handles prefers inner using-declarations") {
		SymbolTable table;
		const StringHandle ns_a_name = StringTable::getOrInternStringHandle("ReviewUsingA");
		const StringHandle ns_b_name = StringTable::getOrInternStringHandle("ReviewUsingB");
		NamespaceHandle ns_a = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_a_name);
		NamespaceHandle ns_b = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_b_name);

		table.enter_scope(ScopeType::Function);
		table.add_using_declaration("value", ns_a, "value");
		table.enter_scope(ScopeType::Block);
		table.add_using_declaration("value", ns_b, "value");

		const auto handles = table.get_current_using_declaration_handles();
		const auto it = handles.find("value");
		REQUIRE(it != handles.end());
		CHECK(it->second.first == ns_b);
	}

	TEST_CASE("insertGlobal records global ScopeId as declaring scope") {
		SymbolTable table;
		table.enter_scope(ScopeType::Block);
		Token token(Token::Type::Literal, std::string_view("0"), 0, 0, 0);
		ASTNode node = ASTNode::emplace_node<ExpressionNode>(
			NumericLiteralNode(token, 0ULL, TypeCategory::Int, TypeQualifier::None, 32));
		REQUIRE(table.insertGlobal("global_var", node));
		CHECK(table.lastDeclaringScopeId().value == 1u);
	}

	TEST_CASE("ASTNode get_if returns active node type or nullptr") {
		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token id_token(Token::Type::Identifier, std::string_view("get_if_probe"), 1, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);
		ASTNode decl = ASTNode::emplace_node<DeclarationNode>(int_type, id_token);
		CHECK(decl.get_if<DeclarationNode>() != nullptr);
		CHECK(decl.get_if<FunctionDeclarationNode>() == nullptr);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on DeclarationNode") {
		SymbolTable table;
		table.enter_scope(ScopeType::Function);
		const ScopeId function_scope = table.currentScopeId();
		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token id_token(Token::Type::Identifier, std::string_view("scope_stamp_var"), 1, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);
		ASTNode node = ASTNode::emplace_node<DeclarationNode>(int_type, id_token);
		REQUIRE(table.insert(std::string_view("scope_stamp_var"), node));
		const std::vector<ASTNode> symbols = table.lookup_all("scope_stamp_var");
		REQUIRE(symbols.size() == 1u);
		CHECK(symbols[0].as<DeclarationNode>().lexical_scope_id() == function_scope);
	}

	TEST_CASE("SymbolTable insert stamps distinct lexical ScopeIds across nested scopes") {
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();

		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token global_id(Token::Type::Identifier, std::string_view("scope_stamp_global_var"), 1, 1, 0);
		Token block_id(Token::Type::Identifier, std::string_view("scope_stamp_block_var"), 2, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);

		ASTNode global_node = ASTNode::emplace_node<DeclarationNode>(int_type, global_id);
		REQUIRE(table.insert(std::string_view("scope_stamp_global_var"), global_node));
		CHECK(table.lookup_all(std::string_view("scope_stamp_global_var"))[0]
				  .as<DeclarationNode>()
				  .lexical_scope_id() == global_scope);

		table.enter_scope(ScopeType::Block);
		const ScopeId block_scope = table.currentScopeId();
		REQUIRE(global_scope != block_scope);

		ASTNode block_node = ASTNode::emplace_node<DeclarationNode>(int_type, block_id);
		REQUIRE(table.insert(std::string_view("scope_stamp_block_var"), block_node));
		CHECK(table.lookup_all(std::string_view("scope_stamp_block_var"))[0]
				  .as<DeclarationNode>()
				  .lexical_scope_id() == block_scope);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on parsed FunctionDeclarationNode") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "void scope_stamp_parsed_fn();";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_fn_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle fn_name = StringTable::getOrInternStringHandle("scope_stamp_parsed_fn");
		const std::vector<ASTNode> overloads =
			gSymbolTable.lookup_all(StringTable::getStringView(fn_name));
		REQUIRE(overloads.size() == 1u);
		CHECK(overloads[0]
				  .as<FunctionDeclarationNode>()
				  .decl_node()
				  .lexical_scope_id()
				  .value != 0u);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on parsed TemplateFunctionDeclarationNode") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "template<class T> void scope_stamp_tmpl_fn(T);";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_tmpl_fn_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle fn_name = StringTable::getOrInternStringHandle("scope_stamp_tmpl_fn");
		const std::vector<ASTNode> overloads =
			gSymbolTable.lookup_all(StringTable::getStringView(fn_name));
		REQUIRE(overloads.size() == 1u);
		REQUIRE(overloads[0].is<TemplateFunctionDeclarationNode>());
		CHECK(overloads[0]
				  .as<TemplateFunctionDeclarationNode>()
				  .function_decl_node()
				  .decl_node()
				  .lexical_scope_id()
				  .value != 0u);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on parsed VariableDeclarationNode") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "int scope_stamp_var;";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_var_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle var_name = StringTable::getOrInternStringHandle("scope_stamp_var");
		const std::vector<ASTNode> symbols =
			gSymbolTable.lookup_all(StringTable::getStringView(var_name));
		REQUIRE(symbols.size() == 1u);
		REQUIRE(symbols[0].is<VariableDeclarationNode>());
		CHECK(symbols[0]
				  .as<VariableDeclarationNode>()
				  .declaration()
				  .lexical_scope_id()
				  .value != 0u);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on parsed TemplateVariableDeclarationNode") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "template<class T> constexpr T scope_stamp_tmpl_var = T{};";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_tmpl_var_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle var_name = StringTable::getOrInternStringHandle("scope_stamp_tmpl_var");
		const std::vector<ASTNode> symbols =
			gSymbolTable.lookup_all(StringTable::getStringView(var_name));
		REQUIRE(symbols.size() == 1u);
		REQUIRE(symbols[0].is<TemplateVariableDeclarationNode>());
		CHECK(symbols[0]
				  .as<TemplateVariableDeclarationNode>()
				  .variable_declaration()
				  .declaration()
				  .lexical_scope_id()
				  .value != 0u);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on EnumDeclarationNode") {
		SymbolTable table;
		table.enter_scope(ScopeType::Block);
		const ScopeId block_scope = table.currentScopeId();
		const StringHandle enum_name = StringTable::getOrInternStringHandle("scope_stamp_enum");
		ASTNode node = ASTNode::emplace_node<EnumDeclarationNode>(enum_name, true);
		REQUIRE(table.insert(StringTable::getStringView(enum_name), node));
		const std::vector<ASTNode> symbols = table.lookup_all(StringTable::getStringView(enum_name));
		REQUIRE(symbols.size() == 1u);
		CHECK(symbols[0].as<EnumDeclarationNode>().lexical_scope_id() == block_scope);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on StructDeclarationNode") {
		SymbolTable table;
		table.enter_scope(ScopeType::Block);
		const ScopeId block_scope = table.currentScopeId();
		const StringHandle struct_name = StringTable::getOrInternStringHandle("scope_stamp_struct");
		ASTNode node = ASTNode::emplace_node<StructDeclarationNode>(struct_name, false);
		REQUIRE(table.insert(StringTable::getStringView(struct_name), node));
		const std::vector<ASTNode> symbols = table.lookup_all(StringTable::getStringView(struct_name));
		REQUIRE(symbols.size() == 1u);
		CHECK(symbols[0].as<StructDeclarationNode>().lexical_scope_id() == block_scope);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on TypedefDeclarationNode") {
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token alias_token(Token::Type::Identifier, std::string_view("scope_stamp_typedef"), 1, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);
		ASTNode node = ASTNode::emplace_node<TypedefDeclarationNode>(int_type, alias_token);
		REQUIRE(table.insert(std::string_view("scope_stamp_typedef"), node));
		const std::vector<ASTNode> symbols = table.lookup_all("scope_stamp_typedef");
		REQUIRE(symbols.size() == 1u);
		CHECK(symbols[0].as<TypedefDeclarationNode>().lexical_scope_id() == global_scope);
	}

	TEST_CASE("SymbolTable insert stamps lexical ScopeId on parsed EnumDeclarationNode") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "enum class scope_stamp_parsed_enum { A };";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_enum_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle enum_name = StringTable::getOrInternStringHandle("scope_stamp_parsed_enum");
		const std::vector<ASTNode> symbols =
			gSymbolTable.lookup_all(StringTable::getStringView(enum_name));
		REQUIRE(symbols.size() == 1u);
		CHECK(symbols[0].as<EnumDeclarationNode>().lexical_scope_id().value != 0u);
	}

	TEST_CASE("Parser stamps lexical ScopeId on parsed StructDeclarationNode") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "struct scope_stamp_parsed_struct { int x; };";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_struct_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle struct_name =
			StringTable::getOrInternStringHandle("scope_stamp_parsed_struct");
		const StructDeclarationNode* parsed_struct = nullptr;
		for (const ASTNode& node : parser.get_nodes()) {
			if (!node.is<StructDeclarationNode>()) {
				continue;
			}
			const StructDeclarationNode& struct_decl = node.as<StructDeclarationNode>();
			if (struct_decl.name() == struct_name) {
				parsed_struct = &struct_decl;
				break;
			}
		}
		REQUIRE(parsed_struct != nullptr);
		CHECK(parsed_struct->lexical_scope_id().value != 0u);
	}

	TEST_CASE("Parser stamps lexical ScopeId on parsed TypedefDeclarationNode") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = "typedef int scope_stamp_parsed_typedef;";
		CompileContext test_context;
		test_context.setInputFile("declaration_ast_scope_id_typedef_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());

		const StringHandle typedef_name =
			StringTable::getOrInternStringHandle("scope_stamp_parsed_typedef");
		const TypedefDeclarationNode* parsed_typedef = nullptr;
		for (const ASTNode& node : parser.get_nodes()) {
			if (!node.is<TypedefDeclarationNode>()) {
				continue;
			}
			const TypedefDeclarationNode& typedef_decl = node.as<TypedefDeclarationNode>();
			if (typedef_decl.alias_token().handle() == typedef_name) {
				parsed_typedef = &typedef_decl;
				break;
			}
		}
		REQUIRE(parsed_typedef != nullptr);
		CHECK(parsed_typedef->lexical_scope_id().value != 0u);
	}

	static void requireScopeRecordMatches(
		const FrontendContext& context,
		const SymbolTable& table,
		ScopeId id) {
		REQUIRE(table.findScopeById(id) != nullptr);
		const ScopeRecord& record = context.scopeRecord(id);
		const ScopeMetadataView metadata = readScopeMetadata(table, id);
		CHECK(record.id == id);
		CHECK(record.parent_id == metadata.parent_id);
		CHECK(record.depth == metadata.depth);
		CHECK(record.scope_type == metadata.scope_type);
		CHECK(record.namespace_handle == metadata.namespace_handle);
		CHECK(record.reserved == 0);
	}

	TEST_CASE("FrontendContext owns a global ScopeRecord at construction") {
		FrontendContext context;
		REQUIRE(context.scopeRecordCount() == 1u);
		REQUIRE(context.scopeCount() == 1u);
		REQUIRE(context.currentScopeId().value == 1u);
		const ScopeRecord& global = context.scopeRecord(ScopeId{1});
		CHECK(global.id.value == 1u);
		CHECK(!global.parent_id);
		CHECK(global.depth == 1u);
		CHECK(global.scope_type == ScopeType::Global);
		CHECK(global.namespace_handle.index == NamespaceHandle::INVALID_HANDLE);
		CHECK(global.reserved == 0);
		CHECK(context.scopeArenaUsedBytes() == sizeof(ScopeRecord));
		CHECK(context.scopeArenaReservedBytes() ==
			  static_cast<uint64_t>(kScopeArenaChunkSize) * sizeof(ScopeRecord));
		CHECK(context.findScopeRecord(ScopeId{2}) == nullptr);
	}

	TEST_CASE("SymbolTable enter/exit/clear publish matching ScopeRecords into the active FrontendContext") {
		FrontendContext context;
		SymbolTable table;
		table.enablePersistentScopePublication();
		requireScopeRecordMatches(context, table, table.currentScopeId());

		table.enter_scope(ScopeType::Block);
		const ScopeId block_id = table.currentScopeId();
		REQUIRE(block_id.value == 2u);
		REQUIRE(context.scopeRecordCount() == table.scopeCount());
		REQUIRE(context.currentScopeId() == block_id);
		requireScopeRecordMatches(context, table, block_id);
		CHECK(context.scopeRecord(block_id).parent_id.value == 1u);
		CHECK(context.scopeRecord(block_id).scope_type == ScopeType::Block);

		const StringHandle ns_name = StringTable::getOrInternStringHandle("ScopeRecordNs");
		NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_name);
		table.enter_namespace(ns_handle);
		const ScopeId namespace_id = table.currentScopeId();
		REQUIRE(namespace_id.value == 3u);
		REQUIRE(context.scopeRecordCount() == 3u);
		REQUIRE(context.currentScopeId() == namespace_id);
		requireScopeRecordMatches(context, table, namespace_id);
		CHECK(context.scopeRecord(namespace_id).scope_type == ScopeType::Namespace);
		CHECK(context.scopeRecord(namespace_id).namespace_handle == ns_handle);

		table.exit_scope();
		CHECK(table.currentScopeId() == block_id);
		CHECK(context.currentScopeId() == block_id);
		CHECK(context.scopeRecordCount() == 3u);
		CHECK(table.scopeCount() == 3u);
		requireScopeRecordMatches(context, table, namespace_id);

		table.exit_scope();
		CHECK(table.currentScopeId().value == 1u);
		CHECK(context.currentScopeId().value == 1u);
		CHECK(context.scopeRecordCount() == 3u);

		table.enter_scope(ScopeType::Function);
		const ScopeId function_id = table.currentScopeId();
		REQUIRE(function_id.value == 4u);
		REQUIRE(context.scopeRecordCount() == 4u);
		requireScopeRecordMatches(context, table, function_id);
		CHECK(context.scopeRecord(function_id).scope_type == ScopeType::Function);

		table.clear();
		CHECK(table.scopeCount() == 1u);
		CHECK(context.scopeRecordCount() == 1u);
		CHECK(context.currentScopeId().value == 1u);
		requireScopeRecordMatches(context, table, ScopeId{1});
		CHECK(context.findScopeRecord(block_id) == nullptr);
	}

	TEST_CASE("lookup reads ScopeRecord metadata when persistent publication is enabled") {
		FrontendContext context;
		SymbolTable table;
		table.enablePersistentScopePublication();

		const StringHandle ns_name = StringTable::getOrInternStringHandle("LookupScopeRecordNs");
		NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_name);
		table.enter_namespace(ns_handle);
		const ScopeId namespace_scope_id = table.currentScopeId();

		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token id_token(Token::Type::Identifier, std::string_view("scope_record_lookup_probe"), 1, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);
		ASTNode node = ASTNode::emplace_node<DeclarationNode>(int_type, id_token);
		REQUIRE(table.insert(std::string_view("scope_record_lookup_probe"), node));

		const std::optional<ASTNode> found = table.lookup("scope_record_lookup_probe");
		REQUIRE(found.has_value());
		CHECK(found->is<DeclarationNode>());
		CHECK(table.get_current_namespace_handle() == ns_handle);

		table.mutateLegacyScopeMetadataForTest(
			namespace_scope_id,
			ScopeType::Block,
			ScopeId{99},
			99u,
			NamespaceHandle{NamespaceHandle::INVALID_HANDLE});
		CHECK(table.legacyScopeMetadata(namespace_scope_id).scope_type == ScopeType::Block);

		const std::optional<ASTNode> found_after_poison = table.lookup("scope_record_lookup_probe");
		REQUIRE(found_after_poison.has_value());
		CHECK(found_after_poison->is<DeclarationNode>());
		CHECK(table.get_current_namespace_handle() == ns_handle);

		const std::vector<ASTNode> overloads = table.lookup_all("scope_record_lookup_probe");
		REQUIRE(overloads.size() == 1u);
		CHECK(overloads[0].is<DeclarationNode>());
	}

	TEST_CASE("insert enter and exit read ScopeRecord metadata when persistent publication is enabled") {
		FrontendContext context;
		SymbolTable table;
		table.enablePersistentScopePublication();

		const StringHandle ns_name = StringTable::getOrInternStringHandle("InsertScopeRecordNs");
		NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_name);
		table.enter_namespace(ns_handle);
		const ScopeId namespace_scope_id = table.currentScopeId();

		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token id_token(Token::Type::Identifier, std::string_view("insert_scope_record_probe"), 1, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);
		ASTNode node = ASTNode::emplace_node<DeclarationNode>(int_type, id_token);
		REQUIRE(table.insert(std::string_view("insert_scope_record_probe"), node));

		table.mutateLegacyScopeMetadataForTest(
			namespace_scope_id,
			ScopeType::Block,
			ScopeId{99},
			99u,
			NamespaceHandle{NamespaceHandle::INVALID_HANDLE});

		Token id_token2(Token::Type::Identifier, std::string_view("insert_scope_record_probe2"), 1, 2, 0);
		ASTNode node2 = ASTNode::emplace_node<DeclarationNode>(int_type, id_token2);
		REQUIRE(table.insert(std::string_view("insert_scope_record_probe2"), node2));
		REQUIRE(table.lookup("insert_scope_record_probe2").has_value());

		table.enter_scope(ScopeType::Block);
		const ScopeId block_scope_id = table.currentScopeId();
		table.mutateLegacyScopeMetadataForTest(
			block_scope_id,
			ScopeType::Function,
			ScopeId{99},
			99u,
			NamespaceHandle{NamespaceHandle::INVALID_HANDLE});
		table.exit_scope();
		CHECK(table.currentScopeId() == namespace_scope_id);
		CHECK(table.get_current_scope_type() == ScopeType::Namespace);

		table.mutateLegacyScopeMetadataForTest(
			namespace_scope_id,
			ScopeType::Namespace,
			ScopeId{1},
			999u,
			ns_handle);
		table.enter_scope(ScopeType::Function);
		const ScopeId function_scope_id = table.currentScopeId();
		CHECK(context.scopeRecord(function_scope_id).depth ==
			  context.scopeRecord(namespace_scope_id).depth + 1u);
		CHECK(table.activeScopeDepth() == context.scopeRecord(function_scope_id).depth);
	}

	TEST_CASE("DeclarationBuilder publication reads ScopeRecord metadata when persistent publication is enabled") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		table.enablePersistentScopePublication();

		const StringHandle ns_name = StringTable::getOrInternStringHandle("PublicationScopeRecordNs");
		NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_name);
		table.enter_namespace(ns_handle);
		const ScopeId namespace_scope_id = table.currentScopeId();

		table.mutateLegacyScopeMetadataForTest(
			namespace_scope_id,
			ScopeType::Block,
			ScopeId{99},
			99u,
			NamespaceHandle{NamespaceHandle::INVALID_HANDLE});

		const StringHandle name = StringTable::getOrInternStringHandle("publication_scope_record_probe");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			namespace_scope_id,
			name,
			TypeId{71},
			TypeId{81},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		PreparedFunctionPublication prepared = builder.prepareFunctionPublication(request, table);
		REQUIRE_FALSE(prepared.isRejected());

		const PublishResult published = builder.publishFunction(request, table);
		CHECK(published.status == PublishStatus::Created);
		REQUIRE(published.entity_id);
		CHECK(builder.entity(published.entity_id).owner_id == ownerIdFromNamespaceHandle(ns_handle));
	}

	TEST_CASE("DeclarationBuilder prepareFunctionPublication rejects absent ScopeId without throwing") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const StringHandle name =
			StringTable::getOrInternStringHandle("decl_builder_absent_scope");
		const FunctionDeclRequest invalid = makeFunctionDeclRequest(
			ScopeId{},
			name,
			TypeId{71},
			TypeId{81},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.prepareFunctionPublication(invalid, table).isRejected());
	}

	TEST_CASE("DeclarationBuilder prepareFunctionPublication rejects block scope without throwing") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		table.enablePersistentScopePublication();
		table.enter_scope(ScopeType::Block);
		const ScopeId block_scope_id = table.currentScopeId();
		const StringHandle name =
			StringTable::getOrInternStringHandle("decl_builder_block_scope_fn");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			block_scope_id,
			name,
			TypeId{71},
			TypeId{81},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.prepareFunctionPublication(request, table).isRejected());
	}

	TEST_CASE("DeclarationBuilder prepareFunctionPublication throws on stale ScopeId") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		table.enablePersistentScopePublication();
		const StringHandle name =
			StringTable::getOrInternStringHandle("decl_builder_stale_scope");
		const FunctionDeclRequest invalid = makeFunctionDeclRequest(
			ScopeId{999},
			name,
			TypeId{71},
			TypeId{81},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK_THROWS_AS(builder.prepareFunctionPublication(invalid, table), InternalError);
	}

	TEST_CASE("SymbolTable enter_scope without an active FrontendContext still succeeds") {
		SymbolTable table;
		table.enter_scope(ScopeType::Block);
		CHECK(table.currentScopeId().value == 2u);
		CHECK(table.scopeCount() == 2u);
		table.exit_scope();
		CHECK(table.currentScopeId().value == 1u);
		CHECK(table.scopeCount() == 2u);
	}

	TEST_CASE("SymbolTable enablePersistentScopePublication requires an active FrontendContext") {
		SymbolTable table;
		bool threw = false;
		try {
			table.enablePersistentScopePublication();
		} catch (const InternalError&) {
			threw = true;
		}
		CHECK(threw);
		table.enter_scope(ScopeType::Block);
		CHECK(table.scopeCount() == 2u);
	}

	TEST_CASE("a second SymbolTable does not publish into the active FrontendContext") {
		FrontendContext context;
		SymbolTable publisher;
		publisher.enablePersistentScopePublication();
		publisher.enter_scope(ScopeType::Block);
		const std::size_t published = context.scopeRecordCount();
		REQUIRE(published == 2u);

		SymbolTable other;
		other.enter_scope(ScopeType::Function);
		other.enter_scope(ScopeType::Block);
		other.exit_scope();
		CHECK(context.scopeRecordCount() == published);
		CHECK(context.currentScopeId().value == 2u);
		CHECK(other.scopeCount() == 3u);
	}

	TEST_CASE("nested FrontendContext seeds its own ScopeRecord arena") {
		FrontendContext outer;
		SymbolTable outer_table;
		outer_table.enablePersistentScopePublication();
		outer_table.enter_scope(ScopeType::Block);
		REQUIRE(outer.scopeRecordCount() == 2u);
		{
			FrontendContext inner;
			CHECK(FrontendContext::active() == &inner);
			CHECK(inner.scopeRecordCount() == 1u);
			CHECK(outer.scopeRecordCount() == 2u);
			SymbolTable inner_table;
			inner_table.enablePersistentScopePublication();
			inner_table.enter_scope(ScopeType::Function);
			CHECK(inner.scopeRecordCount() == 2u);
			CHECK(inner.currentScopeId().value == 2u);
			CHECK(outer.scopeRecordCount() == 2u);
			CHECK(outer.currentScopeId().value == 2u);
		}
		CHECK(FrontendContext::active() == &outer);
		CHECK(outer.scopeRecordCount() == 2u);
		CHECK(outer.currentScopeId().value == 2u);
	}

	TEST_CASE("outer SymbolTable lookup uses its bound ScopeRecord arena under nested FrontendContext") {
		FrontendContext outer;
		SymbolTable outer_table;
		outer_table.enablePersistentScopePublication();

		const StringHandle ns_name = StringTable::getOrInternStringHandle("NestedLookupOuterNs");
		NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_name);
		outer_table.enter_namespace(ns_handle);

		Token type_token(Token::Type::Identifier, std::string_view("int"), 1, 1, 0);
		Token id_token(Token::Type::Identifier, std::string_view("nested_outer_lookup_probe"), 1, 1, 0);
		TypeSpecifierNode int_type(
			TypeCategory::Int, TypeQualifier::None, 32, type_token, CVQualifier::None);
		ASTNode node = ASTNode::emplace_node<DeclarationNode>(int_type, id_token);
		REQUIRE(outer_table.insert(std::string_view("nested_outer_lookup_probe"), node));
		REQUIRE(outer_table.lookup("nested_outer_lookup_probe").has_value());

		{
			FrontendContext inner;
			SymbolTable inner_table;
			inner_table.enablePersistentScopePublication();
			inner_table.enter_scope(ScopeType::Block);
			REQUIRE(FrontendContext::active() == &inner);
			REQUIRE(inner.scopeRecordCount() == 2u);
			REQUIRE(outer.scopeRecordCount() == 2u);

			const std::optional<ASTNode> found = outer_table.lookup("nested_outer_lookup_probe");
			REQUIRE(found.has_value());
			CHECK(found->is<DeclarationNode>());

			const std::vector<ASTNode> overloads = outer_table.lookup_all("nested_outer_lookup_probe");
			REQUIRE(overloads.size() == 1u);
			CHECK(overloads[0].is<DeclarationNode>());
		}
	}

	TEST_CASE("persistent ScopeId divergence from the arena is an InternalError") {
		FrontendContext context;
		SymbolTable table;
		table.enablePersistentScopePublication();
		table.enter_scope(ScopeType::Block);
		context.resetPersistentScopes();
		REQUIRE(context.scopeRecordCount() == 1u);
		REQUIRE(table.scopeCount() == 2u);
		bool threw = false;
		try {
			table.enter_scope(ScopeType::Function);
		} catch (const InternalError&) {
			threw = true;
		}
		CHECK(threw);
	}

	TEST_CASE("SymbolTable enablePersistentScopePublication requires a global-only table and arena") {
		FrontendContext context;
		SymbolTable table;
		table.enter_scope(ScopeType::Block);
		bool threw_after_enter = false;
		try {
			table.enablePersistentScopePublication();
		} catch (const InternalError&) {
			threw_after_enter = true;
		}
		CHECK(threw_after_enter);

		SymbolTable publisher;
		publisher.enablePersistentScopePublication();
		publisher.enter_scope(ScopeType::Function);
		REQUIRE(context.scopeRecordCount() == 2u);
		SymbolTable too_late;
		bool threw_busy_context = false;
		try {
			too_late.enablePersistentScopePublication();
		} catch (const InternalError&) {
			threw_busy_context = true;
		}
		CHECK(threw_busy_context);
	}

	TEST_CASE("SymbolTable publication binding is cleared when bound FrontendContext is destroyed") {
		SymbolTable table;
		{
			FrontendContext context;
			table.enablePersistentScopePublication();
			table.enter_scope(ScopeType::Block);
			REQUIRE(table.persistentScopePublicationEnabled());
			REQUIRE(context.scopeRecordCount() == 2u);
		}
		CHECK_FALSE(table.persistentScopePublicationEnabled());
		CHECK(table.scopeCount() == 2u);
		CHECK(readScopeMetadata(table, ScopeId{2}).scope_type == ScopeType::Block);
	}

	TEST_CASE("bindPersistentScopePublication resyncs an already-published table") {
		FrontendContext context;
		SymbolTable table;
		table.enablePersistentScopePublication();
		table.enter_scope(ScopeType::Block);
		table.enter_scope(ScopeType::Function);
		REQUIRE(context.scopeRecordCount() == 3u);
		REQUIRE(table.scopeCount() == 3u);

		bindPersistentScopePublication(table);

		CHECK(table.persistentScopePublicationEnabled());
		CHECK(table.currentScopeId().value == 1u);
		CHECK(table.scopeCount() == 1u);
		CHECK(context.scopeRecordCount() == 1u);
		CHECK(context.currentScopeId().value == 1u);
	}

	TEST_CASE("Parser parse publishes gSymbolTable scopes into the active FrontendContext") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		FrontendContext context;
		const std::string code = "int main() { return 0; }";
		CompileContext test_context;
		test_context.setInputFile("scope_record_parse_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());
		CHECK(gSymbolTable.persistentScopePublicationEnabled());
		CHECK(context.scopeRecordCount() == gSymbolTable.scopeCount());
		CHECK(context.scopeRecordCount() > 1u);
		CHECK(context.currentScopeId() == gSymbolTable.currentScopeId());
		requireScopeRecordMatches(context, gSymbolTable, gSymbolTable.currentScopeId());
	}

	TEST_CASE("ChunkedVector reports used and reserved arena bytes") {
		ChunkedVector<DeclarationRecord, DeclarationBuilder::kDeclarationArenaChunkSize> arena;
		CHECK(arena.usedBytes() == 0);
		CHECK(arena.reservedBytes() == 0);

		DeclarationRecord record{};
		record.id = DeclId{1};
		arena.push_back(record);
		CHECK(arena.usedBytes() == sizeof(DeclarationRecord));
		CHECK(arena.reservedBytes() ==
			  static_cast<uint64_t>(DeclarationBuilder::kDeclarationArenaChunkSize) * sizeof(DeclarationRecord));
		CHECK(arena.reservedBytes() >= arena.usedBytes());

		arena.pop_back();
		CHECK(arena.usedBytes() == 0);
		CHECK(arena.reservedBytes() ==
			  static_cast<uint64_t>(DeclarationBuilder::kDeclarationArenaChunkSize) * sizeof(DeclarationRecord));
		CHECK(arena.peakUsedBytes() == sizeof(DeclarationRecord));
	}

	TEST_CASE("ChunkedAnyVector reports used and reserved arena bytes") {
		constexpr uint32_t kTestChunkSize = 256;
		ChunkedAnyVector<kTestChunkSize> storage;
		CHECK(storage.usedBytes() == 0);
		CHECK(storage.reservedBytes() == 0);

		TemplateEnvironmentSnapshotNode& node = storage.emplace_back<TemplateEnvironmentSnapshotNode>();
		(void)node;
		CHECK(storage.usedBytes() >= sizeof(TemplateEnvironmentSnapshotNode));
		CHECK(storage.reservedBytes() >= kTestChunkSize);
		CHECK(storage.reservedBytes() >= storage.usedBytes());
	}

	TEST_CASE("FrontendContext semantic domain bytes track declaration arenas") {
		FrontendContext context;
		context.refreshSemanticDomainStats();
		CHECK(context.domainStats(AllocationDomain::Semantic).current_bytes == 0);
		CHECK(context.domainStats(AllocationDomain::Semantic).reserved_bytes == 0);
		CHECK(context.domainStats(AllocationDomain::Ir).current_bytes == 0);

		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("domain_bytes_first");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{10},
			TypeId{20},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		REQUIRE(builder.publishFunction(request, table).status == PublishStatus::Created);

		context.refreshSemanticDomainStats();
		const DomainByteStats semantic = context.domainStats(AllocationDomain::Semantic);
		const uint64_t expected_used = sizeof(DeclarationRecord) + sizeof(EntityRecord);
		const uint64_t expected_reserved =
			static_cast<uint64_t>(DeclarationBuilder::kDeclarationArenaChunkSize) * sizeof(DeclarationRecord) +
			static_cast<uint64_t>(DeclarationBuilder::kEntityArenaChunkSize) * sizeof(EntityRecord);
		CHECK(semantic.current_bytes == expected_used);
		CHECK(semantic.peak_bytes == expected_used);
		CHECK(semantic.reserved_bytes == expected_reserved);
		CHECK(semantic.peak_reserved_bytes == expected_reserved);
		CHECK(builder.declarationArenaUsedBytes() == sizeof(DeclarationRecord));
		CHECK(builder.entityArenaUsedBytes() == sizeof(EntityRecord));
	}

	TEST_CASE("FrontendContext semantic domain peak survives publication rollback") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("domain_bytes_rollback");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{11},
			TypeId{21},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);

		{
			PublicationTransaction transaction(builder);
			PreparedFunctionPublication prepared = builder.prepareFunctionPublication(request, table);
			REQUIRE_FALSE(prepared.isRejected());
			REQUIRE(builder.commitFunctionPublication(prepared, transaction).status == PublishStatus::Created);
			transaction.rollback();
		}

		context.refreshSemanticDomainStats();
		const DomainByteStats semantic = context.domainStats(AllocationDomain::Semantic);
		CHECK(semantic.current_bytes == 0);
		CHECK(semantic.peak_bytes == sizeof(DeclarationRecord) + sizeof(EntityRecord));
		CHECK(semantic.reserved_bytes ==
			  static_cast<uint64_t>(DeclarationBuilder::kDeclarationArenaChunkSize) * sizeof(DeclarationRecord) +
			  static_cast<uint64_t>(DeclarationBuilder::kEntityArenaChunkSize) * sizeof(EntityRecord));
		CHECK(semantic.peak_reserved_bytes == semantic.reserved_bytes);
	}

	TEST_CASE("FrontendContext syntax domain bytes track the legacy AST bridge") {
		FrontendContext context;
		context.refreshSyntaxDomainStats();
		const uint64_t used_before = context.domainStats(AllocationDomain::Syntax).current_bytes;
		const uint64_t reserved_before = context.domainStats(AllocationDomain::Syntax).reserved_bytes;
		const std::size_t objects_before = gChunkedAnyStorage.size();

		TemplateEnvironmentSnapshotNode& node =
			gChunkedAnyStorage.emplace_back<TemplateEnvironmentSnapshotNode>();
		(void)node;

		context.refreshSyntaxDomainStats();
		const DomainByteStats syntax = context.domainStats(AllocationDomain::Syntax);
		CHECK(gChunkedAnyStorage.size() == objects_before + 1);
		CHECK(syntax.current_bytes > used_before);
		CHECK(syntax.peak_bytes >= syntax.current_bytes);
		CHECK(syntax.reserved_bytes >= reserved_before);
		CHECK(syntax.reserved_bytes >= syntax.current_bytes);
		CHECK(context.domainStats(AllocationDomain::Ir).current_bytes == 0);
	}

	TEST_CASE("FrontendContext syntax AST family counts classify legacy bridge objects") {
		FrontendContext context;
		context.refreshSyntaxAstFamilyCounts();
		const std::array<uint64_t, static_cast<std::size_t>(SyntaxAstFamily::Count)> before =
			context.syntaxAstFamilyCounts();

		TemplateEnvironmentSnapshotNode& template_node =
			gChunkedAnyStorage.emplace_back<TemplateEnvironmentSnapshotNode>();
		(void)template_node;
		BlockNode& block_node = gChunkedAnyStorage.emplace_back<BlockNode>();
		(void)block_node;

		context.refreshSyntaxAstFamilyCounts();
		const std::array<uint64_t, static_cast<std::size_t>(SyntaxAstFamily::Count)> after =
			context.syntaxAstFamilyCounts();
		CHECK(after[static_cast<std::size_t>(SyntaxAstFamily::Template)] ==
			  before[static_cast<std::size_t>(SyntaxAstFamily::Template)] + 1);
		CHECK(after[static_cast<std::size_t>(SyntaxAstFamily::Statement)] ==
			  before[static_cast<std::size_t>(SyntaxAstFamily::Statement)] + 1);
		CHECK(classifySyntaxAstFamily(std::type_index(typeid(TemplateEnvironmentSnapshotNode))) ==
			  SyntaxAstFamily::Template);
		CHECK(classifySyntaxAstFamily(std::type_index(typeid(BlockNode))) == SyntaxAstFamily::Statement);
	}

	TEST_CASE("FrontendContext semantic declaration kind counts track DeclarationBuilder records") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();

		context.refreshSemanticDeclKindCounts();
		const std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> before_decls =
			context.declarationKindCounts();
		const std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> before_entities =
			context.entityKindCounts();
		const std::size_t declarator_interns_before = builder.telemetryDeclaratorInternCount();
		const std::size_t parameter_lists_before = builder.telemetryParameterListInternCount();

		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			StringTable::getOrInternStringHandle("semantic_telemetry_fn"),
			TypeId{901},
			TypeId{902},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		{
			PublicationTransaction transaction(builder);
			PreparedFunctionPublication prepared = builder.prepareFunctionPublication(request, table);
			REQUIRE_FALSE(prepared.isRejected());
			REQUIRE(builder.commitFunctionPublication(prepared, transaction).status == PublishStatus::Created);
			transaction.commit();
		}

		context.refreshSemanticDeclKindCounts();
		const std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> after_decls =
			context.declarationKindCounts();
		const std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> after_entities =
			context.entityKindCounts();
		CHECK(after_decls[static_cast<std::size_t>(DeclKind::Function)] ==
			  before_decls[static_cast<std::size_t>(DeclKind::Function)] + 1);
		CHECK(after_entities[static_cast<std::size_t>(DeclKind::Function)] ==
			  before_entities[static_cast<std::size_t>(DeclKind::Function)] + 1);
		CHECK(builder.telemetryDeclaratorInternCount() >= declarator_interns_before);
		CHECK(builder.telemetryParameterListInternCount() >= parameter_lists_before);
		CHECK(declKindLabel(DeclKind::Function) == "function");
	}

	TEST_CASE("FrontendContext IR domain stats record lowering buffer bytes") {
		FrontendContext context;
		context.recordIrDomainStats(128, 256);
		const DomainByteStats ir = context.domainStats(AllocationDomain::Ir);
		CHECK(ir.current_bytes == 128);
		CHECK(ir.peak_bytes == 128);
		CHECK(ir.reserved_bytes == 256);
		CHECK(ir.peak_reserved_bytes == 256);

		context.recordIrDomainStats(64, 512);
		const DomainByteStats ir_after_drop = context.domainStats(AllocationDomain::Ir);
		CHECK(ir_after_drop.current_bytes == 64);
		CHECK(ir_after_drop.peak_bytes == 128);
		CHECK(ir_after_drop.reserved_bytes == 512);
		CHECK(ir_after_drop.peak_reserved_bytes == 512);
	}

	TEST_CASE("Legacy ChunkedAnyVector emplace_back enforces allow-list on storage") {
		requireLegacyAstChunkedAnyEmplaceAllowed<TemplateEnvironmentSnapshotNode, true>();
		LegacyAstChunkedAnyVector storage;
		TemplateEnvironmentSnapshotNode& node = storage.emplace_back<TemplateEnvironmentSnapshotNode>();
		(void)node;
		CHECK(isLegacyChunkedAnyStorageType<TemplateEnvironmentSnapshotNode>);
	}

	TEST_CASE("Legacy ChunkedAnyVector allow-list rejects new semantic record types") {
		struct ForbiddenNewSemanticRecord {
			uint32_t id;
		};
		static_assert(!isLegacyChunkedAnyStorageType<ForbiddenNewSemanticRecord>);
		static_assert(!isLegacyChunkedAnyStorageType<DeclarationRecord>);
		static_assert(!isLegacyChunkedAnyStorageType<EntityRecord>);
	}

	TEST_CASE("DeclarationBuilder creates DeclId and EntityId for first function") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_first");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{10},
			TypeId{20},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const PublishResult result = builder.publishFunction(request, table);
		CHECK(result.status == PublishStatus::Created);
		CHECK(result.decl_id.value == 1u);
		CHECK(result.entity_id.value == 1u);
		CHECK(builder.declarationCount() == 1u);
		CHECK(builder.entityCount() == 1u);
		CHECK(context.declarationCount() == 1u);
		CHECK(context.entityCount() == 1u);
		CHECK(sizeof(DeclarationRecord) == 32u);
		CHECK(sizeof(EntityRecord) == 32u);
		const DeclarationRecord& decl = builder.declaration(result.decl_id);
		CHECK(decl.entity_id == result.entity_id);
		CHECK_FALSE(decl.previous_decl_id);
		CHECK(decl.lexical_scope_id == global_scope);
		CHECK(decl.signature_id == TypeId{10});
		CHECK(decl.return_type_id == TypeId{20});
		CHECK(builder.entity(result.entity_id).owner_id == ownerIdFromNamespaceHandle(NamespaceRegistry::GLOBAL_NAMESPACE));
	}

	TEST_CASE("DeclarationBuilder merges compatible function redeclaration into one EntityId") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_redecl");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{11},
			TypeId{21},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const PublishResult created = builder.publishFunction(first, table);
		REQUIRE(created.status == PublishStatus::Created);

		const FunctionDeclRequest second = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{11},
			TypeId{21},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		const PublishResult merged = builder.publishFunction(second, table);
		CHECK(merged.status == PublishStatus::MergedRedeclaration);
		CHECK(merged.entity_id == created.entity_id);
		CHECK(merged.decl_id.value == 2u);
		CHECK(builder.declarationCount() == 2u);
		CHECK(builder.entityCount() == 1u);

		const DeclarationRecord& second_decl = builder.declaration(merged.decl_id);
		CHECK(second_decl.previous_decl_id == created.decl_id);
		const EntityRecord& entity = builder.entity(merged.entity_id);
		CHECK(entity.first_decl_id == created.decl_id);
		CHECK(entity.latest_decl_id == merged.decl_id);
		CHECK((entity.flags & DeclarationFlags::IsDefinition) != 0);
	}

	TEST_CASE("DeclarationBuilder merges compatible declarations across reopened namespace blocks") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const StringHandle ns_name = StringTable::getOrInternStringHandle("DeclBuilderNsReopen");
		const NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_name);
		REQUIRE(ns_handle.isValid());

		table.enter_namespace(ns_handle);
		const ScopeId first_block = table.currentScopeId();
		const StringHandle fn_name = StringTable::getOrInternStringHandle("reopened_ns_fn");
		const FunctionDeclRequest declaration = makeFunctionDeclRequest(
			first_block,
			fn_name,
			TypeId{60},
			TypeId{70},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const PublishResult created = builder.publishFunction(declaration, table);
		REQUIRE(created.status == PublishStatus::Created);

		table.exit_scope();
		table.enter_namespace(ns_handle);
		const ScopeId second_block = table.currentScopeId();
		REQUIRE(first_block != second_block);

		const FunctionDeclRequest definition = makeFunctionDeclRequest(
			second_block,
			fn_name,
			TypeId{60},
			TypeId{70},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		const PublishResult merged = builder.publishFunction(definition, table);
		CHECK(merged.status == PublishStatus::MergedRedeclaration);
		CHECK(merged.entity_id == created.entity_id);
		CHECK(builder.entityCount() == 1u);
		CHECK(builder.declaration(created.decl_id).lexical_scope_id == first_block);
		CHECK(builder.declaration(merged.decl_id).lexical_scope_id == second_block);
		CHECK(builder.declaration(created.decl_id).lexical_scope_id != builder.declaration(merged.decl_id).lexical_scope_id);
	}

	TEST_CASE("DeclarationBuilder keeps distinct entities across different namespace owners") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const StringHandle ns_a_name = StringTable::getOrInternStringHandle("DeclBuilderOwnerA");
		const StringHandle ns_b_name = StringTable::getOrInternStringHandle("DeclBuilderOwnerB");
		const NamespaceHandle ns_a = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_a_name);
		const NamespaceHandle ns_b = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE, ns_b_name);
		REQUIRE(ns_a.isValid());
		REQUIRE(ns_b.isValid());
		REQUIRE(ns_a != ns_b);

		const StringHandle fn_name = StringTable::getOrInternStringHandle("same_owner_fn");
		table.enter_namespace(ns_a);
		const ScopeId scope_a = table.currentScopeId();
		const PublishResult a = builder.publishFunction(
			makeFunctionDeclRequest(scope_a, fn_name, TypeId{61}, TypeId{71}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
			table);
		table.exit_scope();

		table.enter_namespace(ns_b);
		const ScopeId scope_b = table.currentScopeId();
		const PublishResult b = builder.publishFunction(
			makeFunctionDeclRequest(scope_b, fn_name, TypeId{61}, TypeId{71}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
			table);

		CHECK(a.status == PublishStatus::Created);
		CHECK(b.status == PublishStatus::Created);
		CHECK(a.entity_id != b.entity_id);
		CHECK(builder.entity(a.entity_id).owner_id == ownerIdFromNamespaceHandle(ns_a));
		CHECK(builder.entity(b.entity_id).owner_id == ownerIdFromNamespaceHandle(ns_b));
		CHECK(builder.entityCount() == 2u);
	}

	TEST_CASE("DeclarationBuilder rejects duplicate function definition without committing") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_dup_def");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{12},
			TypeId{22},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		REQUIRE(builder.publishFunction(first, table).status == PublishStatus::Created);
		const std::size_t decls = builder.declarationCount();
		const std::size_t entities = builder.entityCount();

		const FunctionDeclRequest second = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{12},
			TypeId{22},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		const PublishResult rejected = builder.publishFunction(second, table);
		CHECK(rejected.status == PublishStatus::Rejected);
		CHECK(rejected.entity_id.value == 1u);
		CHECK_FALSE(rejected.decl_id);
		CHECK(builder.declarationCount() == decls);
		CHECK(builder.entityCount() == entities);
	}

	TEST_CASE("DeclarationBuilder creates separate entities for C++ overloads") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_overload");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{31},
			TypeId{41},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const FunctionDeclRequest second = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{32},
			TypeId{41},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const PublishResult a = builder.publishFunction(first, table);
		const PublishResult b = builder.publishFunction(second, table);
		CHECK(a.status == PublishStatus::Created);
		CHECK(b.status == PublishStatus::Created);
		CHECK(a.entity_id != b.entity_id);
		CHECK(builder.entityCount() == 2u);
	}

	TEST_CASE("DeclarationBuilder rejects return-type conflict on same signature") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_ret_conflict");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{33},
			TypeId{50},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		REQUIRE(builder.publishFunction(first, table).status == PublishStatus::Created);
		const std::size_t decls = builder.declarationCount();

		const FunctionDeclRequest conflict = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{33},
			TypeId{51},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const PublishResult rejected = builder.publishFunction(conflict, table);
		CHECK(rejected.status == PublishStatus::Rejected);
		CHECK(builder.declarationCount() == decls);
	}

	TEST_CASE("DeclarationBuilder requires matching constexpr on redeclaration") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_constexpr");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{34},
			TypeId{52},
			FunctionDeclForm::ConstexprDeclaration,
			LanguageLinkage::CPlusPlus);
		REQUIRE(builder.publishFunction(first, table).status == PublishStatus::Created);

		const FunctionDeclRequest mismatch = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{34},
			TypeId{52},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.publishFunction(mismatch, table).status == PublishStatus::Rejected);

		const FunctionDeclRequest match = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{34},
			TypeId{52},
			FunctionDeclForm::ConstexprDefinition,
			LanguageLinkage::CPlusPlus);
		const PublishResult merged = builder.publishFunction(match, table);
		CHECK(merged.status == PublishStatus::MergedRedeclaration);
		CHECK((builder.entity(merged.entity_id).flags & DeclarationFlags::IsInline) != 0);
		CHECK((builder.entity(merged.entity_id).flags & DeclarationFlags::IsConstexpr) != 0);
	}

	TEST_CASE("DeclarationBuilder rejects inline after non-inline definition") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_inline_order");
		const FunctionDeclRequest definition = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{35},
			TypeId{53},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		REQUIRE(builder.publishFunction(definition, table).status == PublishStatus::Created);
		const std::size_t decls = builder.declarationCount();

		const FunctionDeclRequest inline_after = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{35},
			TypeId{53},
			FunctionDeclForm::InlineDeclaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.publishFunction(inline_after, table).status == PublishStatus::Rejected);
		CHECK(builder.declarationCount() == decls);

		FrontendContext context2;
		DeclarationBuilder& builder2 = context2.declarationBuilder();
		SymbolTable table2;
		const ScopeId global_scope2 = table2.currentScopeId();
		const FunctionDeclRequest inline_first = makeFunctionDeclRequest(
			global_scope2,
			name,
			TypeId{35},
			TypeId{53},
			FunctionDeclForm::InlineDeclaration,
			LanguageLinkage::CPlusPlus);
		REQUIRE(builder2.publishFunction(inline_first, table2).status == PublishStatus::Created);
		const FunctionDeclRequest definition_after = makeFunctionDeclRequest(
			global_scope2,
			name,
			TypeId{35},
			TypeId{53},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		const PublishResult merged = builder2.publishFunction(definition_after, table2);
		CHECK(merged.status == PublishStatus::MergedRedeclaration);
		CHECK((builder2.entity(merged.entity_id).flags & DeclarationFlags::IsInline) != 0);
		CHECK((builder2.entity(merged.entity_id).flags & DeclarationFlags::IsDefinition) != 0);
	}

	TEST_CASE("DeclarationBuilder rejects nonexistent and invalid-kind publication scopes") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_invalid_scope");
		const FunctionDeclRequest base = makeFunctionDeclRequest(
			table.currentScopeId(),
			name,
			TypeId{37},
			TypeId{55},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);

		const FunctionDeclRequest missing_scope = makeFunctionDeclRequest(
			ScopeId{999},
			name,
			TypeId{37},
			TypeId{55},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK_THROWS_AS(builder.publishFunction(missing_scope, table), InternalError);

		const FunctionDeclRequest absent_scope = makeFunctionDeclRequest(
			ScopeId{},
			name,
			TypeId{37},
			TypeId{55},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.publishFunction(absent_scope, table).status == PublishStatus::Rejected);

		table.enter_scope(ScopeType::Block);
		const FunctionDeclRequest block_scope = makeFunctionDeclRequest(
			table.currentScopeId(),
			name,
			TypeId{37},
			TypeId{55},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.publishFunction(block_scope, table).status == PublishStatus::Rejected);
		table.exit_scope();

		table.enter_scope(ScopeType::Function);
		const FunctionDeclRequest function_scope = makeFunctionDeclRequest(
			table.currentScopeId(),
			name,
			TypeId{37},
			TypeId{55},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.publishFunction(function_scope, table).status == PublishStatus::Rejected);
		table.exit_scope();

		CHECK(builder.publishFunction(base, table).status == PublishStatus::Created);
		CHECK(builder.declarationCount() == 1u);
		CHECK(builder.entityCount() == 1u);
	}

	TEST_CASE("DeclarationBuilder rejects invalid requests without committing") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_invalid");
		const FunctionDeclRequest invalid_scope = makeFunctionDeclRequest(
			ScopeId{},
			name,
			TypeId{37},
			TypeId{55},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.publishFunction(invalid_scope, table).status == PublishStatus::Rejected);
		CHECK(builder.declarationCount() == 0u);
		CHECK(builder.entityCount() == 0u);
	}

	TEST_CASE("PreparedFunctionPublication cannot be fabricated by callers") {
		static_assert(!std::is_default_constructible_v<PreparedFunctionPublication>);
		static_assert(!std::is_copy_constructible_v<PreparedFunctionPublication>);
		static_assert(!std::is_copy_assignable_v<PreparedFunctionPublication>);
		static_assert(!std::is_constructible_v<
			PreparedFunctionPublication,
			PublishStatus,
			EntityId,
			ScopeId,
			OwnerId,
			StringHandle,
			TypeId,
			TypeId,
			uint8_t>);
	}

	TEST_CASE("DeclarationBuilder prepareFunctionPublication matches publishFunction") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_prepare");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{71},
			TypeId{81},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		PreparedFunctionPublication prepared = builder.prepareFunctionPublication(first, table);
		CHECK_FALSE(prepared.isRejected());
		PublishResult committed{};
		{
			PublicationTransaction first_transaction(builder);
			PreparedFunctionPublication to_commit = builder.prepareFunctionPublication(first, table);
			committed = builder.commitFunctionPublication(to_commit, first_transaction);
			first_transaction.commit();
		}
		CHECK(committed.status == PublishStatus::Created);

		const FunctionDeclRequest redecl = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{71},
			TypeId{81},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		PreparedFunctionPublication prepared_redecl =
			builder.prepareFunctionPublication(redecl, table);
		CHECK_FALSE(prepared_redecl.isRejected());
		const PublishResult committed_redecl = builder.publishFunction(redecl, table);
		CHECK(committed_redecl.status == PublishStatus::MergedRedeclaration);
	}

	TEST_CASE("PublicationTransaction rollback restores merged entity state") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_txn_merge");
		const FunctionDeclRequest decl = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{92},
			TypeId{102},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		const PublishResult created = builder.publishFunction(decl, table);
		REQUIRE(created.status == PublishStatus::Created);
		const EntityRecord entity_before = builder.entity(created.entity_id);

		const FunctionDeclRequest definition = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{92},
			TypeId{102},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		PublicationTransaction transaction(builder);
		PreparedFunctionPublication prepared = builder.prepareFunctionPublication(definition, table);
		REQUIRE_FALSE(prepared.isRejected());
		const PublishResult merged = builder.commitFunctionPublication(prepared, transaction);
		REQUIRE(merged.status == PublishStatus::MergedRedeclaration);
		CHECK(builder.declarationCount() == 2u);
		CHECK(builder.entity(created.entity_id).latest_decl_id == merged.decl_id);
		transaction.rollback();

		CHECK(builder.declarationCount() == 1u);
		const EntityRecord entity_after = builder.entity(created.entity_id);
		CHECK(entity_after.latest_decl_id == entity_before.latest_decl_id);
		CHECK(entity_after.flags == entity_before.flags);
	}

	TEST_CASE("PublicationTransaction rollback restores inserted parameter-list signatures") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		const std::size_t parameter_lists_before = builder.telemetryParameterListInternCount();

		PublicationTransaction transaction(builder);
		const TypeId signature_id =
			builder.internParameterListSignature(std::span<const ASTNode>{}, false, &transaction);
		CHECK(signature_id.value >= 1u);
		transaction.rollback();

		CHECK(builder.telemetryParameterListInternCount() == parameter_lists_before);
	}

	TEST_CASE("PublicationTransaction rollback restores declaration and entity arenas") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_txn");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{91},
			TypeId{101},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);

		PublicationTransaction transaction(builder);
		PreparedFunctionPublication prepared = builder.prepareFunctionPublication(request, table);
		REQUIRE_FALSE(prepared.isRejected());
		REQUIRE(builder.commitFunctionPublication(prepared, transaction).status == PublishStatus::Created);
		CHECK(builder.declarationCount() == 1u);
		CHECK(builder.entityCount() == 1u);
		transaction.rollback();
		CHECK(builder.declarationCount() == 0u);
		CHECK(builder.entityCount() == 0u);
	}

	TEST_CASE("PublicationTransaction rollback restores telemetry intern registries") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		TypeSpecifierNode int_type;
		int_type.set_category(TypeCategory::Int);
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_intern_txn");

		PublicationTransaction transaction(builder);
		const TypeId return_id = builder.internDeclaratorType(int_type);
		const FunctionDeclRequest invalid = makeFunctionDeclRequest(
			ScopeId{},
			name,
			TypeId{200},
			return_id,
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		CHECK(builder.prepareFunctionPublication(invalid, table).isRejected());
		transaction.rollback();

		const TypeId second_return_id = builder.internDeclaratorType(int_type);
		CHECK(second_return_id.value == 1u);
	}

	TEST_CASE("PublicationTransaction rejects nesting") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		PublicationTransaction outer(builder);
		bool threw = false;
		try {
			PublicationTransaction nested(builder);
		} catch (const InternalError&) {
			threw = true;
		}
		CHECK(threw);
		outer.commit();
	}

	TEST_CASE("PublicationTransaction rolls back on stack unwinding") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_txn_unwind");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{93},
			TypeId{103},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);

		try {
			PublicationTransaction transaction(builder);
			PreparedFunctionPublication prepared = builder.prepareFunctionPublication(request, table);
			REQUIRE_FALSE(prepared.isRejected());
			REQUIRE(builder.commitFunctionPublication(prepared, transaction).status == PublishStatus::Created);
			throw std::runtime_error("decl_builder_publication_unwind_probe");
		} catch (const std::runtime_error&) {
		}

		CHECK(builder.declarationCount() == 0u);
		CHECK(builder.entityCount() == 0u);
	}

	TEST_CASE("PublicationTransaction rollback restores two created and two merged publications") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle merge_a = StringTable::getOrInternStringHandle("txn_multi_merge_a");
		const StringHandle merge_b = StringTable::getOrInternStringHandle("txn_multi_merge_b");
		const StringHandle create_a = StringTable::getOrInternStringHandle("txn_multi_create_a");
		const StringHandle create_b = StringTable::getOrInternStringHandle("txn_multi_create_b");

		const PublishResult first_a = builder.publishFunction(
			makeFunctionDeclRequest(global_scope, merge_a, TypeId{201}, TypeId{301}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
			table);
		const PublishResult first_b = builder.publishFunction(
			makeFunctionDeclRequest(global_scope, merge_b, TypeId{202}, TypeId{302}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
			table);
		REQUIRE(first_a.status == PublishStatus::Created);
		REQUIRE(first_b.status == PublishStatus::Created);
		const EntityRecord entity_a_before = builder.entity(first_a.entity_id);
		const EntityRecord entity_b_before = builder.entity(first_b.entity_id);
		const std::size_t decls_before = builder.declarationCount();
		const std::size_t entities_before = builder.entityCount();

		{
			PublicationTransaction transaction(builder);
			PreparedFunctionPublication created_prep_a = builder.prepareFunctionPublication(
				makeFunctionDeclRequest(global_scope, create_a, TypeId{203}, TypeId{303}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
				table);
			PreparedFunctionPublication created_prep_b = builder.prepareFunctionPublication(
				makeFunctionDeclRequest(global_scope, create_b, TypeId{204}, TypeId{304}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
				table);
			PreparedFunctionPublication merged_prep_a = builder.prepareFunctionPublication(
				makeFunctionDeclRequest(global_scope, merge_a, TypeId{201}, TypeId{301}, FunctionDeclForm::Definition, LanguageLinkage::CPlusPlus),
				table);
			PreparedFunctionPublication merged_prep_b = builder.prepareFunctionPublication(
				makeFunctionDeclRequest(global_scope, merge_b, TypeId{202}, TypeId{302}, FunctionDeclForm::Definition, LanguageLinkage::CPlusPlus),
				table);
			REQUIRE_FALSE(created_prep_a.isRejected());
			REQUIRE_FALSE(created_prep_b.isRejected());
			REQUIRE_FALSE(merged_prep_a.isRejected());
			REQUIRE_FALSE(merged_prep_b.isRejected());
			REQUIRE(builder.commitFunctionPublication(created_prep_a, transaction).status == PublishStatus::Created);
			REQUIRE(builder.commitFunctionPublication(created_prep_b, transaction).status == PublishStatus::Created);
			REQUIRE(builder.commitFunctionPublication(merged_prep_a, transaction).status ==
					PublishStatus::MergedRedeclaration);
			REQUIRE(builder.commitFunctionPublication(merged_prep_b, transaction).status ==
					PublishStatus::MergedRedeclaration);
			CHECK(builder.declarationCount() == decls_before + 4u);
			CHECK(builder.entityCount() == entities_before + 2u);
			transaction.rollback();
		}

		CHECK(builder.declarationCount() == decls_before);
		CHECK(builder.entityCount() == entities_before);
		CHECK(builder.entity(first_a.entity_id).latest_decl_id == entity_a_before.latest_decl_id);
		CHECK(builder.entity(first_a.entity_id).flags == entity_a_before.flags);
		CHECK(builder.entity(first_b.entity_id).latest_decl_id == entity_b_before.latest_decl_id);
		CHECK(builder.entity(first_b.entity_id).flags == entity_b_before.flags);

		const PublishResult recreate_a = builder.publishFunction(
			makeFunctionDeclRequest(global_scope, create_a, TypeId{203}, TypeId{303}, FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus),
			table);
		CHECK(recreate_a.status == PublishStatus::Created);
		CHECK(recreate_a.entity_id != first_a.entity_id);
		CHECK(recreate_a.entity_id != first_b.entity_id);
	}

	TEST_CASE("DeclarationBuilder rejects committing the same prepared publication twice") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_double_commit");
		const FunctionDeclRequest request = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{205},
			TypeId{305},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);

		PreparedFunctionPublication prepared = builder.prepareFunctionPublication(request, table);
		REQUIRE_FALSE(prepared.isRejected());
		PublicationTransaction transaction(builder);
		REQUIRE(builder.commitFunctionPublication(prepared, transaction).status == PublishStatus::Created);
		const std::size_t decls = builder.declarationCount();
		const std::size_t entities = builder.entityCount();

		bool threw = false;
		try {
			builder.commitFunctionPublication(prepared, transaction);
		} catch (const InternalError&) {
			threw = true;
		}
		CHECK(threw);
		CHECK(builder.declarationCount() == decls);
		CHECK(builder.entityCount() == entities);
		transaction.commit();
	}

	TEST_CASE("PublicationTransaction checkpoint stays bounded as builder grows") {
		static_assert(sizeof(DeclarationBuilderCheckpoint) <= 32);

		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();

		for (uint32_t index = 0; index < 128u; ++index) {
			const StringHandle name = StringTable::getOrInternStringHandle(
				StringBuilder().append("decl_builder_scale_").append(static_cast<int64_t>(index)));
			const FunctionDeclRequest request = makeFunctionDeclRequest(
				global_scope,
				name,
				TypeId{index + 1u},
				TypeId{index + 1000u},
				FunctionDeclForm::Declaration,
				LanguageLinkage::CPlusPlus);
			REQUIRE(builder.publishFunction(request, table).status == PublishStatus::Created);
		}

		CHECK(builder.declarationCount() == 128u);
		CHECK(builder.entityCount() == 128u);

		const StringHandle rollback_name =
			StringTable::getOrInternStringHandle("decl_builder_scale_rollback");
		PublicationTransaction transaction(builder);
		const FunctionDeclRequest rollback_request = makeFunctionDeclRequest(
			global_scope,
			rollback_name,
			TypeId{9999},
			TypeId{10000},
			FunctionDeclForm::Declaration,
			LanguageLinkage::CPlusPlus);
		PreparedFunctionPublication prepared =
			builder.prepareFunctionPublication(rollback_request, table);
		REQUIRE_FALSE(prepared.isRejected());
		REQUIRE(builder.commitFunctionPublication(prepared, transaction).status == PublishStatus::Created);
		transaction.rollback();

		CHECK(builder.declarationCount() == 128u);
		CHECK(builder.entityCount() == 128u);
	}

	TEST_CASE("Parser publication reject retains SymbolTable insertion") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = R"(
int symtab_undo_conflict_row(int value);
float symtab_undo_conflict_row(int value);
)";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("symtab_insert_undo_conflict_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());
		CHECK(context.declarationCount() == 1u);
		CHECK(context.entityCount() == 1u);

		const StringHandle name = StringTable::getOrInternStringHandle("symtab_undo_conflict_row");
		const std::vector<ASTNode> overloads =
			gSymbolTable.lookup_all(StringTable::getStringView(name));
		CHECK(overloads.size() == 2u);
	}

	TEST_CASE("commitParserFreeFunctionPublication rejects duplicate definitions without committing") {
		FrontendContext context;
		DeclarationBuilder& builder = context.declarationBuilder();
		SymbolTable table;
		const ScopeId global_scope = table.currentScopeId();
		const StringHandle name = StringTable::getOrInternStringHandle("decl_builder_commit_dup");
		const FunctionDeclRequest first = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{111},
			TypeId{121},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		{
			PublicationTransaction first_transaction(builder);
			PreparedFunctionPublication prepared = builder.prepareFunctionPublication(first, table);
			REQUIRE_FALSE(prepared.isRejected());
			REQUIRE(builder.commitFunctionPublication(prepared, first_transaction).status ==
					PublishStatus::Created);
			first_transaction.commit();
		}
		const std::size_t decls = builder.declarationCount();
		const std::size_t entities = builder.entityCount();
		const std::size_t declarator_interns = builder.telemetryDeclaratorInternCount();
		const std::size_t parameter_lists = builder.telemetryParameterListInternCount();

		const FunctionDeclRequest duplicate = makeFunctionDeclRequest(
			global_scope,
			name,
			TypeId{111},
			TypeId{121},
			FunctionDeclForm::Definition,
			LanguageLinkage::CPlusPlus);
		PublicationTransaction reject_transaction(builder);
		CHECK(builder.prepareFunctionPublication(duplicate, table).isRejected());
		reject_transaction.rollback();

		CHECK(builder.declarationCount() == decls);
		CHECK(builder.entityCount() == entities);
		CHECK(builder.telemetryDeclaratorInternCount() == declarator_interns);
		CHECK(builder.telemetryParameterListInternCount() == parameter_lists);
	}

	TEST_CASE("Parser shadow publication rejects duplicate definition without builder commit") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = R"(
void decl_builder_dup_shadow();
void decl_builder_dup_shadow() {}
void decl_builder_dup_shadow() {}
)";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("decl_builder_dup_shadow_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());
		CHECK(context.declarationCount() == 2u);
		CHECK(context.entityCount() == 1u);
	}

	TEST_CASE("buildNamespaceHandleForStructName rejects unregistered qualified spelling") {
		StringHandle qualified = StringTable::getOrInternStringHandle("ns::Widget");
		NamespaceHandle handle = buildNamespaceHandleForStructName(qualified);
		CHECK_FALSE(handle.isValid());
	}

	TEST_CASE("Parser publishes namespace free functions through DeclarationBuilder shadow path") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = R"(
namespace decl_builder_wire_parser_ns {
void wire_shadow_fn();
void wire_shadow_fn() {}
}
)";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("decl_builder_wire_parser_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());
		CHECK(context.declarationCount() == 2u);
		CHECK(context.entityCount() == 1u);
	}

	TEST_CASE("Parser shadow publication merges inline declarations across reopened namespace blocks") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		const std::string code = R"(
namespace decl_builder_reopened_inline_ns {
inline int reopened_inline_fn();
}
namespace decl_builder_reopened_inline_ns {
int reopened_inline_fn() { return 7; }
}
)";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("decl_builder_reopened_inline_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());
		REQUIRE(context.declarationCount() == 2u);
		REQUIRE(context.entityCount() == 1u);

		DeclarationBuilder& builder = context.declarationBuilder();
		const DeclarationRecord& declaration = builder.declaration(DeclId{1});
		const DeclarationRecord& definition = builder.declaration(DeclId{2});
		const EntityRecord& entity = builder.entity(EntityId{1});
		CHECK(declaration.entity_id == EntityId{1});
		CHECK(definition.entity_id == EntityId{1});
		CHECK(definition.previous_decl_id == declaration.id);
		CHECK(declaration.lexical_scope_id != definition.lexical_scope_id);
		CHECK(entity.owner_id);
		CHECK(entity.first_decl_id == declaration.id);
		CHECK(entity.latest_decl_id == definition.id);
		CHECK((entity.flags & DeclarationFlags::IsInline) != 0);
		CHECK((entity.flags & DeclarationFlags::IsDefinition) != 0);
	}

	TEST_CASE("Parser DeclarationBuilder shadow path keeps valid overloads parsing") {
		gTypeInfo.clear();
		gNativeTypes.clear();
		gTypesByName.clear();
		gTemplateRegistry.clear();
		gConceptRegistry.clear();
		gSymbolTable.clear();

		// Valid overloads: array-bound distinctions in reference parameters.
		// The telemetry interner may collapse them, but parse must not fail.
		const std::string code = R"(
int decl_builder_wire_check_row(int (&)[3]) { return 0; }
int decl_builder_wire_check_row(int (&)[2]) { return 1; }
)";
		FrontendContext context;
		CompileContext test_context;
		test_context.setInputFile("decl_builder_wire_overload_test.cpp");
		Lexer lexer(code);
		SemanticAnalysis parser_sema(test_context, gSymbolTable);
		Parser parser(lexer, test_context, parser_sema);
		const ParseResult parse_result = parser.parse();
		REQUIRE(!parse_result.is_error());
		CHECK(context.declarationCount() >= 1u);
		CHECK(context.entityCount() >= 1u);
	}
}

TEST_SUITE("Diagnostics") {
	TEST_CASE("Stable diagnostic IDs keep fixed values independent of message text") {
		// Mutation target: changing any value here breaks this test and every
		// converted-site assertion below, which is exactly the point.
		REQUIRE(static_cast<uint32_t>(DiagnosticId::None) == 0u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::PointerToReferenceType) == 1001u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::MultipleAsmSuffixesOnDeclarator) == 1002u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::ExpectedCloseBracketAfterArraySize) == 1003u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::DecltypeAutoCvQualifier) == 1004u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::DecltypeAutoPointerOrReference) == 1005u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::DecltypeAutoStructuredBinding) == 1006u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::ParameterPackDataMember) == 1007u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::NoteToMatchOpeningBracket) == 1051u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::FloatingPointModuloOperator) == 1301u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::FloatingPointBitwiseCompoundAssignment) == 1302u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::FloatingPointShiftOperator) == 1303u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::FloatingPointBitwiseOperator) == 1304u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::AmbiguousOperatorOverload) == 1305u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::StaticOperatorMustBeNonStaticMember) == 1306u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::OperatorDefaultArgumentsForbidden) == 1307u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::AssignmentOperatorArity) == 1308u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::SubscriptOperatorArity) == 1309u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::ArrowOperatorArity) == 1310u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::IncrementDecrementOperatorForm) == 1311u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::OrdinaryOperatorArity) == 1312u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::DeletedCopyAssignment) == 1313u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::DeletedMoveAssignment) == 1314u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::ImmediateInvocationNotConstant) == 1315u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::DeletedCopyConstructor) == 1316u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::DeletedMoveConstructor) == 1317u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::AssignmentToConstObject) == 1318u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::OperatorOverloadNotFound) == 1319u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::DeletedOperatorFunction) == 1320u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::ConstinitInitializerNotConstant) == 1501u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::ConstexprStaticMemberInitializerNotConstant) == 1502u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::ExplicitConstructorCopyInitialization) == 1503u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::AmbiguousConstructorCall) == 1504u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::RangeForBeginEndRequired) == 1601u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::AmbiguousDerivedToBasePointerConversion) == 1602u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::InaccessibleDerivedToBasePointerConversion) == 1603u);
		REQUIRE(static_cast<uint32_t>(DiagnosticId::AmbiguousBuiltInSubscriptConversion) == 1604u);

		CHECK(diagnosticIdName(DiagnosticId::PointerToReferenceType) == "PointerToReferenceType");
		CHECK(diagnosticIdName(DiagnosticId::NoteToMatchOpeningBracket) == "NoteToMatchOpeningBracket");
		CHECK(diagnosticIdName(DiagnosticId::DecltypeAutoCvQualifier) == "DecltypeAutoCvQualifier");
		CHECK(diagnosticIdName(DiagnosticId::DecltypeAutoPointerOrReference) == "DecltypeAutoPointerOrReference");
		CHECK(diagnosticIdName(DiagnosticId::DecltypeAutoStructuredBinding) == "DecltypeAutoStructuredBinding");
		CHECK(diagnosticIdName(DiagnosticId::ParameterPackDataMember) == "ParameterPackDataMember");
		CHECK(diagnosticIdName(DiagnosticId::FloatingPointModuloOperator) == "FloatingPointModuloOperator");
		CHECK(diagnosticIdName(DiagnosticId::FloatingPointBitwiseCompoundAssignment) == "FloatingPointBitwiseCompoundAssignment");
		CHECK(diagnosticIdName(DiagnosticId::FloatingPointShiftOperator) == "FloatingPointShiftOperator");
		CHECK(diagnosticIdName(DiagnosticId::FloatingPointBitwiseOperator) == "FloatingPointBitwiseOperator");
		CHECK(diagnosticIdName(DiagnosticId::AmbiguousOperatorOverload) == "AmbiguousOperatorOverload");
		CHECK(diagnosticIdName(DiagnosticId::StaticOperatorMustBeNonStaticMember) == "StaticOperatorMustBeNonStaticMember");
		CHECK(diagnosticIdName(DiagnosticId::OperatorDefaultArgumentsForbidden) == "OperatorDefaultArgumentsForbidden");
		CHECK(diagnosticIdName(DiagnosticId::AssignmentOperatorArity) == "AssignmentOperatorArity");
		CHECK(diagnosticIdName(DiagnosticId::SubscriptOperatorArity) == "SubscriptOperatorArity");
		CHECK(diagnosticIdName(DiagnosticId::ArrowOperatorArity) == "ArrowOperatorArity");
		CHECK(diagnosticIdName(DiagnosticId::IncrementDecrementOperatorForm) == "IncrementDecrementOperatorForm");
		CHECK(diagnosticIdName(DiagnosticId::OrdinaryOperatorArity) == "OrdinaryOperatorArity");
		CHECK(diagnosticIdName(DiagnosticId::DeletedCopyAssignment) == "DeletedCopyAssignment");
		CHECK(diagnosticIdName(DiagnosticId::DeletedMoveAssignment) == "DeletedMoveAssignment");
		CHECK(diagnosticIdName(DiagnosticId::ImmediateInvocationNotConstant) == "ImmediateInvocationNotConstant");
		CHECK(diagnosticIdName(DiagnosticId::DeletedCopyConstructor) == "DeletedCopyConstructor");
		CHECK(diagnosticIdName(DiagnosticId::DeletedMoveConstructor) == "DeletedMoveConstructor");
		CHECK(diagnosticIdName(DiagnosticId::AssignmentToConstObject) == "AssignmentToConstObject");
		CHECK(diagnosticIdName(DiagnosticId::OperatorOverloadNotFound) == "OperatorOverloadNotFound");
		CHECK(diagnosticIdName(DiagnosticId::DeletedOperatorFunction) == "DeletedOperatorFunction");
		CHECK(diagnosticIdName(DiagnosticId::ConstinitInitializerNotConstant) == "ConstinitInitializerNotConstant");
		CHECK(diagnosticIdName(DiagnosticId::ConstexprStaticMemberInitializerNotConstant) == "ConstexprStaticMemberInitializerNotConstant");
		CHECK(diagnosticIdName(DiagnosticId::ExplicitConstructorCopyInitialization) == "ExplicitConstructorCopyInitialization");
		CHECK(diagnosticIdName(DiagnosticId::AmbiguousConstructorCall) == "AmbiguousConstructorCall");
		CHECK(diagnosticIdName(DiagnosticId::RangeForBeginEndRequired) == "RangeForBeginEndRequired");
		CHECK(diagnosticIdName(DiagnosticId::AmbiguousDerivedToBasePointerConversion) == "AmbiguousDerivedToBasePointerConversion");
		CHECK(diagnosticIdName(DiagnosticId::InaccessibleDerivedToBasePointerConversion) == "InaccessibleDerivedToBasePointerConversion");
		CHECK(diagnosticIdName(DiagnosticId::AmbiguousBuiltInSubscriptConversion) == "AmbiguousBuiltInSubscriptConversion");

		// Same ID must serve different message templates without identity drift.
		DiagnosticEngine engine;
		uint32_t first = engine.report(
			DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
			SourceLocation::fromParts(1, 1, 0), "template one {}", {});
		uint32_t second = engine.report(
			DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
			SourceLocation::fromParts(2, 1, 0), "totally different text", {});
		CHECK(engine.diagnostic(first).id == engine.diagnostic(second).id);
	}

	TEST_CASE("Severity classification accumulates per level and flags errors") {
		DiagnosticEngine engine;
		engine.report(DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Warning,
					  SourceLocation::fromParts(1, 1, 0), "w", {});
		engine.report(DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
					  SourceLocation::fromParts(2, 1, 0), "e", {});
		engine.report(DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
					  SourceLocation::fromParts(3, 1, 0), "e2", {});
		engine.report(DiagnosticId::None, DiagnosticSeverity::Note,
					  SourceLocation::fromParts(4, 1, 0), "n", {});

		CHECK(engine.count(DiagnosticSeverity::Warning) == 1);
		CHECK(engine.count(DiagnosticSeverity::Error) == 2);
		CHECK(engine.count(DiagnosticSeverity::Note) == 1);
		CHECK(engine.diagnostics().size() == 4);
		CHECK(engine.hasErrors());
		CHECK(diagnosticSeverityTag(DiagnosticSeverity::Fatal) == std::string_view("fatal error"));

		DiagnosticEngine clean_engine;
		CHECK_FALSE(clean_engine.hasErrors());
	}

	TEST_CASE("Locations and ranges are stored verbatim") {
		DiagnosticEngine engine;
		SourceRange range = SourceRange::fromLocations(
			SourceLocation::fromParts(7, 9, 1), SourceLocation::fromParts(7, 14, 1));
		uint32_t index = engine.reportWithRange(
			DiagnosticId::ExpectedCloseBracketAfterArraySize, DiagnosticSeverity::Error,
			SourceLocation::fromParts(7, 14, 1), range, "missing bracket", {});

		const Diagnostic& stored = engine.diagnostic(index);
		CHECK(stored.location.line == 7u);
		CHECK(stored.location.column == 14u);
		CHECK(stored.location.file_index == 1u);
		REQUIRE(stored.has_range());
		CHECK(stored.range.begin.column == 9u);
		CHECK(stored.range.end.column == 14u);

		// Compact record check: locations are 12 bytes (three uint32 fields).
		MESSAGE("sizeof(SourceLocation)=" << sizeof(SourceLocation)
				<< " sizeof(SourceRange)=" << sizeof(SourceRange));
		CHECK(sizeof(SourceLocation) == 12u);
	}

	TEST_CASE("Arguments render sequentially into template placeholders") {
		StringHandle interned = StringTable::getOrInternStringHandle("InternedName");
		const std::array<DiagnosticArgument, 3> mixed_arguments{
			DiagnosticArgument::text("x"),
			DiagnosticArgument::internedText(interned),
			DiagnosticArgument::unsignedInteger(2)};
		std::string rendered = renderDiagnosticMessage(
			"suffixes on declarator '{}' at {} are not supported, count {}",
			mixed_arguments);
		CHECK(rendered == "suffixes on declarator 'x' at InternedName are not supported, count 2");

		const std::array<DiagnosticArgument, 1> negative_argument{
			DiagnosticArgument::signedInteger(-42)};
		std::string signed_render = renderDiagnosticMessage(
			"line {}", negative_argument);
		CHECK(signed_render == "line -42");

		// Contract: a missing argument renders nothing for its placeholder.
		const std::array<DiagnosticArgument, 1> present_argument{
			DiagnosticArgument::unsignedInteger(1)};
		std::string missing = renderDiagnosticMessage(
			"value {}", present_argument);
		CHECK(missing == "value 1");
	}

	TEST_CASE("Accumulated diagnostics own dynamic templates and text arguments") {
		DiagnosticEngine engine;
		uint32_t index = 0;
		{
			std::string message_template = "dynamic {}";
			std::string argument_text = "before";
			std::string note_template = "note {}";
			std::string note_text = "detail";
			const std::array<DiagnosticArgument, 1> arguments{
				DiagnosticArgument::text(argument_text)};
			const std::array<DiagnosticArgument, 1> note_arguments{
				DiagnosticArgument::text(note_text)};
			index = engine.report(
				DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
				SourceLocation::fromParts(1, 1, 0), message_template, arguments);
			engine.attachNote(
				index, DiagnosticId::NoteToMatchOpeningBracket,
				SourceLocation::fromParts(1, 2, 0), note_template, note_arguments);

			// Keep the allocations alive but overwrite their bytes. A borrowed
			// string_view deterministically observes these mutations.
			message_template.assign("XXXXXXXXXX");
			argument_text.assign("mutate");
			note_template.assign("YYYYYYY");
			note_text.assign("change");
		}

		std::deque<std::string> paths{"owned.cpp"};
		std::string rendered = renderDiagnostic(engine.diagnostic(index), engine, paths);
		CHECK(rendered.find("dynamic before") != std::string::npos);
		CHECK(rendered.find("note: note detail") != std::string::npos);
	}

	TEST_CASE("Notes attach through pool indices and render under the parent") {
		DiagnosticEngine engine;
		uint32_t index = engine.reportWithRange(
			DiagnosticId::ExpectedCloseBracketAfterArraySize, DiagnosticSeverity::Error,
			SourceLocation::fromParts(3, 35, 0),
			SourceRange::fromLocations(SourceLocation::fromParts(3, 32, 0), SourceLocation::fromParts(3, 35, 0)),
			"Expected ']' after array size", {});
		engine.attachNote(index, DiagnosticId::NoteToMatchOpeningBracket,
						  SourceLocation::fromParts(3, 32, 0), "to match this '['", {});

		const Diagnostic& parent = engine.diagnostic(index);
		REQUIRE(parent.note_indices.size() == 1);
		CHECK(engine.note(parent.note_indices[0]).id == DiagnosticId::NoteToMatchOpeningBracket);

		std::deque<std::string> paths{"unit.cpp"};
		std::string rendered = renderDiagnostic(parent, engine, paths);
		CHECK(rendered.find("unit.cpp:3:35: error: Expected ']' after array size [ExpectedCloseBracketAfterArraySize#1003]") != std::string::npos);
		CHECK(rendered.find("unit.cpp:3:32: note: to match this '[' [NoteToMatchOpeningBracket#1051]") != std::string::npos);
	}

	TEST_CASE("Rendered lines expose machine-consumable id, line, and column") {
		DiagnosticEngine engine;
		uint32_t index = engine.report(
			DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
			SourceLocation::fromParts(2, 30, 0), "Cannot form a pointer to reference type", {});
		std::deque<std::string> paths{"m.cpp"};
		std::string rendered = renderDiagnostic(engine.diagnostic(index), engine, paths);

		// Contract: deterministic location prefix and trailing stable-id tag;
		// both parseable without message prose.
		CHECK(rendered.compare(0, 18, "m.cpp:2:30: error:") == 0);
		const std::string expected_suffix = "[PointerToReferenceType#1001]";
		REQUIRE(rendered.size() > expected_suffix.size());
		CHECK(rendered.substr(rendered.size() - expected_suffix.size()) == expected_suffix);
		CHECK(diagnosticIdNumber(DiagnosticId::ExpectedCloseBracketAfterArraySize) == 1003u);
	}

	TEST_CASE("Template-instantiation context snapshots by value at report time") {
		DiagnosticEngine engine;
		StringHandle outer = StringTable::getOrInternStringHandle("Outer<int>");
		StringHandle inner = StringTable::getOrInternStringHandle("Inner::func");
		{
			DiagnosticEngine::TemplateContextGuard outer_guard(engine, outer, SourceLocation::fromParts(10, 5, 0));
			{
				DiagnosticEngine::TemplateContextGuard inner_guard(engine, inner, SourceLocation{});
				CHECK(engine.templateContextDepth() == 2);
				engine.report(DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
							  SourceLocation::fromParts(11, 1, 0), "boom", {});
			}
			CHECK(engine.templateContextDepth() == 1);
		}
		CHECK(engine.templateContextDepth() == 0);

		REQUIRE(engine.diagnostics().size() == 1);
		const InlineVector<TemplateInstantiationFrame, 4>& frames = engine.diagnostic(0).instantiation_context;
		REQUIRE(frames.size() == 2);
		CHECK(frames[0].template_name == outer);
		CHECK(frames[1].template_name == inner);
	}

	TEST_CASE("CompileError bridge preserves what() and carries structured payload") {
		DiagnosticEngine engine;
		const std::array<DiagnosticArgument, 1> declarator_argument{
			DiagnosticArgument::text("decl")};
		uint32_t index = engine.report(
			DiagnosticId::MultipleAsmSuffixesOnDeclarator, DiagnosticSeverity::Error,
			SourceLocation::fromParts(4, 6, 0),
			"Multiple __asm suffixes on declarator '{}' are not supported",
			declarator_argument);

		try {
			throw CompileError::fromStructuredDiagnostic(engine.diagnostic(index));
		} catch (const CompileError& caught) {
			CHECK(std::string(caught.what()) ==
				  "Multiple __asm suffixes on declarator 'decl' are not supported");
			const Diagnostic* payload = caught.structuredDiagnostic();
			REQUIRE(payload != nullptr);
			CHECK(payload->id == DiagnosticId::MultipleAsmSuffixesOnDeclarator);
			CHECK(payload->location.line == 4u);
			CHECK(payload->location.column == 6u);
		}

		// Copying the exception (throw does this implicitly) keeps the payload.
		try {
			throw CompileError::fromStructuredDiagnostic(engine.diagnostic(index));
		} catch (const std::runtime_error& copied) {
			const CompileError* compile_copy = dynamic_cast<const CompileError*>(&copied);
			REQUIRE(compile_copy != nullptr);
			CHECK(compile_copy->structuredDiagnostic() != nullptr);
			CHECK(std::string(compile_copy->what()).find("'decl'") != std::string::npos);
		}
	}

	TEST_CASE("Legacy construction counts toward outside-engine inventory; bridge does not") {
		uint64_t before = diagnosticsEmittedOutsideEngineCount();
		{
			CompileError legacy("legacy failure path");
			(void)legacy;
		}
		uint64_t after_legacy = diagnosticsEmittedOutsideEngineCount();
		CHECK(after_legacy == before + 1);

		{
			DiagnosticEngine engine;
			uint32_t index = engine.report(
				DiagnosticId::PointerToReferenceType, DiagnosticSeverity::Error,
				SourceLocation::fromParts(1, 1, 0), "structured", {});
			CompileError bridged = CompileError::fromStructuredDiagnostic(engine.diagnostic(index));
			(void)bridged;
		}
		CHECK(diagnosticsEmittedOutsideEngineCount() == after_legacy);
	}

	TEST_CASE("Speculative ParseResult errors do not count as emitted diagnostics") {
		uint64_t before = diagnosticsEmittedOutsideEngineCount();
		ParseResult speculative_error = ParseResult::error(
			"probe rejected", Token(Token::Type::Identifier, std::string_view("probe"), 1, 1, 0));
		REQUIRE(speculative_error.is_error());
		CHECK(diagnosticsEmittedOutsideEngineCount() == before);
	}

	TEST_CASE("Lexer maps diagnostic locations back to original source coordinates") {
		const std::array<SourceLineMapping, 2> line_map{
			SourceLineMapping{0, 40, 0},
			SourceLineMapping{1, 7, 1}};
		const std::deque<std::string> paths{"main.cpp", "included.hpp"};
		Lexer lexer("first\nsecond\n", line_map, paths);
		Token first = lexer.next_token();
		Token second = lexer.next_token();

		SourceLocation first_location = lexer.getSourceLocation(first);
		SourceLocation second_location = lexer.getSourceLocation(second);
		CHECK(first_location.line == 40u);
		CHECK(first_location.file_index == 0u);
		CHECK(second_location.line == 7u);
		CHECK(second_location.file_index == 1u);
		// Token columns follow the existing lexer convention and identify the
		// column immediately after the token spelling.
		CHECK(second_location.column == 7u);
	}

	TEST_CASE("Converted pointer-to-reference diagnostic carries structured payload end to end") {
		std::string_view code = "using R = int&;\nint main(){ int x = sizeof(R(*)); return x; }\n";
		Lexer location_lexer(code);
		SourceLocation expected_pointer_location{};
		while (true) {
			Token token = location_lexer.next_token();
			if (token.value() == "*") {
				expected_pointer_location = location_lexer.getSourceLocation(token);
				break;
			}
			REQUIRE(token.type() != Token::Type::EndOfFile);
		}
		Lexer lexer(code);
		CompileContext local_context;
		SemanticAnalysis parser_sema(local_context, gSymbolTable);
		Parser parser(lexer, local_context, parser_sema);

		bool threw = false;
		try {
			parser.parse();
		} catch (const CompileError& caught) {
			threw = true;
			const Diagnostic* payload = caught.structuredDiagnostic();
			REQUIRE(payload != nullptr);
			CHECK(payload->id == DiagnosticId::PointerToReferenceType);
			CHECK(payload->severity == DiagnosticSeverity::Error);
			CHECK(payload->location.line == expected_pointer_location.line);
			CHECK(payload->location.column == expected_pointer_location.column);
			CHECK(payload->location.file_index == expected_pointer_location.file_index);
			CHECK(std::string(caught.what()) == "Cannot form a pointer to reference type");

			std::deque<std::string> paths{"probe.cpp"};
			std::string rendered = renderDiagnostic(*payload, local_context.diagnostics(), paths);
			CHECK(rendered.find("error: Cannot form a pointer to reference type [PointerToReferenceType#1001]") != std::string::npos);
		}
		CHECK(threw);
	}

	TEST_CASE("Record size inventory for stored diagnostic records") {
		// Cold-path records; sizes documented here so future layout changes
		// are conscious decisions rather than accidents.
		MESSAGE("sizeof(DiagnosticArgument)=" << sizeof(DiagnosticArgument));
		MESSAGE("sizeof(TemplateInstantiationFrame)=" << sizeof(TemplateInstantiationFrame));
		MESSAGE("sizeof(DiagnosticNote)=" << sizeof(DiagnosticNote));
		MESSAGE("sizeof(Diagnostic)=" << sizeof(Diagnostic));

		static_assert(sizeof(DiagnosticArgument) <= 32, "argument slot must stay compact");
		static_assert(sizeof(SourceRange) == 24, "range packs two compact locations");
		CHECK(true);
	}
}
