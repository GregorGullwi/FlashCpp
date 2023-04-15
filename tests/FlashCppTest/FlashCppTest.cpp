#include "CompileContext.h"
#include "FileTree.h"
#include "FileReader.h"
#include <string>
#include <algorithm>
#include <cctype>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

static CompileContext compile_context;
static FileTree file_tree;

static bool compare_strings_ignore_whitespace(const std::string& str1, const std::string& str2) {
	auto remove_whitespace = [](std::string str) {
		str.erase(std::remove_if(str.begin(), str.end(), [](char c) { return std::isspace(c); }), str.end());
		return str;
	};
	return remove_whitespace(str1) == remove_whitespace(str2);
}

static void run_test_case(const std::string& input, const std::string& expected_output) {
	FileReader file_reader(compile_context, file_tree.reset());
	REQUIRE(file_reader.processFileContent(input));
	const std::string& actual_output = file_reader.get_result();
	REQUIRE(compare_strings_ignore_whitespace(actual_output, expected_output));
}

TEST_CASE("SimpleReplacement", "[preprocessor]") {
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

TEST_CASE("NestedReplacement", "[preprocessor]") {
	const std::string input = R"(
    #define PI 3.14159
    #define CIRCLE_AREA(radius) (PI * (radius) * (radius))
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

TEST_CASE("NestedMacros", "[preprocessor]") {
	const std::string input = R"(
    #define SQUARE(x) ((x) * (x))
    #define DOUBLE(x) ((x) * 2)
    const int num = DOUBLE(SQUARE(3));
  )";
	const std::string expected_output = R"(
    const int num = ((((3) * (3))) * 2);
  )";
    run_test_case(input, expected_output);
}

TEST_CASE("ConditionalCompilation", "[preprocessor]") {
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

TEST_CASE("Stringification", "[preprocessor]") {
	const std::string input = R"(
    #define STR(x) #x
    const char* str = STR(hello world);
  )";
	const std::string expected_output = R"(
    const char* str = "hello world";
  )";
	run_test_case(input, expected_output);
}

TEST_CASE("Concatenation", "[preprocessor]") {
	const std::string input = R"(
    #define CONCAT(a, b) a ## b
    const int num = CONCAT(3, 4);
  )";
	const std::string expected_output = R"(
    const int num = 34;
  )";
	run_test_case(input, expected_output);
}

TEST_CASE("__has_include", "[preprocessor]") {
	const std::string input = R"(
    #if __has_include(<iostream>)
      #include <iostream>
      const bool has_iostream = true;
    #else
      const bool has_iostream = false;
    #endif
  )";
	const std::string expected_output = R"(
    #if 1
      #include <iostream>
      const bool has_iostream = true;
    #else
      const bool has_iostream = false;
    #endif
  )";
    compile_context.addIncludeDir(R"(C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.35.32215\include)"sv);
    run_test_case(input, expected_output);
}
