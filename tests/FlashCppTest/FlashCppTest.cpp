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
	FileReader file_reader(compile_context, file_tree.reset());
    REQUIRE(file_reader.processFileContent(input));
	const std::string& actual_output = file_reader.get_result();
	REQUIRE(compare_strings_ignore_whitespace(actual_output, expected_output));
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
	FileReader file_reader(compile_context, file_tree.reset());
	REQUIRE(file_reader.processFileContent(input));
	const std::string& actual_output = file_reader.get_result();
	REQUIRE(compare_strings_ignore_whitespace(actual_output, expected_output));
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
	FileReader file_reader(compile_context, file_tree.reset());
	REQUIRE(file_reader.processFileContent(input));
	const std::string& actual_output = file_reader.get_result();
	REQUIRE(compare_strings_ignore_whitespace(actual_output, expected_output));
}