#include "CompileContext.h"
#include "FileTree.h"
#include "FileReader.h"
#include "Token.h"
#include "Lexer.h"
#include "Parser.h"
#include "CodeGen.h"
#include "IRConverter.h"
#include "ChunkedAnyVector.h"
#include "TemplateRegistry.h"
#include <string>
#include <algorithm>
#include <cctype>
#include <typeindex>
#include <sstream>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

static CompileContext compile_context;
static FileTree file_tree;

// Helper function to read test files from Reference directory
std::string read_test_file(const std::string& filename) {
    std::ifstream file("tests/Reference/" + filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open test file: tests/Reference/" + filename);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Helper function to run a test with a given source file
void run_test_from_file(const std::string& filename, const std::string& test_name, bool generate_obj, std::optional<int> break_at_line = {}) {
	std::cout << "run_test_from_file: " << test_name.c_str() << std::endl;

	CompileContext test_context;
	test_context.setInputFile(filename);

	FileTree file_tree;
    FileReader file_reader(test_context, file_tree);
	const std::string& code = file_reader.get_result();

	gTypeInfo.clear();
	gNativeTypes.clear();  // Clear native types map before reinitializing
	gTypesByName.clear();  // Clear types by name map as well
	gTemplateRegistry.clear();
    Lexer lexer(code, file_reader.get_line_map(), file_reader.get_file_paths());
    Parser parser(lexer, test_context);
#if WITH_DEBUG_INFO
	parser.break_at_line_ = break_at_line;
#endif
    auto parse_result = parser.parse();

	if (parse_result.is_error()) {
        std::printf("Parse error in %s: %s\n", test_name.c_str(), parse_result.error_message().c_str());
    }
	CHECK(!parse_result.is_error());
    if (parse_result.is_error()) {
		return;
    }

    const auto& ast = parser.get_nodes();

    AstToIr converter(gSymbolTable, compile_context, parser);
    for (auto& node_handle : ast) {
        converter.visit(node_handle);
    }

    const auto& ir = converter.getIr();

    std::printf("\n=== Test: %s ===\n", test_name.c_str());

    for (const auto& instruction : ir.getInstructions()) {
        std::puts(instruction.getReadableString().c_str());
    }

    std::puts("=== End Test ===\n");

    if (generate_obj) {
        IrToObjConverter irConverter;
        std::string obj_filename = "tests/Reference/x64/" + filename.substr(0, filename.find_last_of('.')) + ".obj";
        irConverter.convert(ir, obj_filename.c_str(), filename);
    }

    // For now, don't fail tests due to parsing issues while we're developing
}

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
		Parser parser(lexer, compile_context);
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
		Parser parser1(lexer1, compile_context);
		auto parse_result1 = parser1.parse();
		CHECK(!parse_result1.is_error());
		const auto& ast1 = parser1.get_nodes();

		// Test with auto and trailing return type
		Lexer lexer2(code_with_auto_return_type);
		Parser parser2(lexer2, compile_context);
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
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter(gSymbolTable, compile_context, parser);
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
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter(gSymbolTable, compile_context, parser);
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
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter(gSymbolTable, compile_context, parser);
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

TEST_SUITE("Code gen") {
	TEST_CASE("Addition function") {
		std::string_view code = R"(
		 int add(int a, int b) {
            return a + b;
         }

         int main() {
            return add(3, 5);
         })";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter(gSymbolTable, compile_context, parser);
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
};

TEST_SUITE("Code gen") {
	TEST_CASE("Function returning local variable") {
		std::string_view code = R"(
		 int add(int a, int b) {
			int c = a + b;
			return c;
         }

         int main() {
            return add(3, 5);
         })";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter(gSymbolTable, compile_context, parser);
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
};

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
	Parser parser(lexer, compile_context);
	auto parse_result = parser.parse();
	CHECK(!parse_result.is_error());

	const auto& ast = parser.get_nodes();

	AstToIr converter(gSymbolTable, compile_context, parser);
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

TEST_CASE("Variadic functions") {
	run_test_from_file("test_va_simple.cpp", "Variadic function call", false);
}

TEST_CASE("Shift operations") {
	run_test_from_file("shift_operations.cpp", "Shift operations", false);
}

TEST_CASE("Signed vs Unsigned support") {
	run_test_from_file("signed_unsigned_support.cpp", "Signed vs Unsigned support", false);
}

TEST_CASE("Signed vs Unsigned shift operations") {
	run_test_from_file("signed_unsigned_shifts.cpp", "Signed vs Unsigned shift operations", false);
}

TEST_CASE("Integer types and promotions") {
	run_test_from_file("integer_promotions.cpp", "Integer types and promotions", false);
}

TEST_CASE("Bitwise operations") {
	run_test_from_file("bitwise_operations.cpp", "Bitwise operations", false);
}

TEST_CASE("Comprehensive operators") {
	run_test_from_file("comprehensive_operators.cpp", "Comprehensive operators", false);
}

TEST_CASE("Comparison operators") {
	run_test_from_file("comparison_operators.cpp", "Comparison operators", false);
}

TEST_CASE("Logical operators") {
	run_test_from_file("logical_operators.cpp", "Logical operators", false);
}

TEST_CASE("Bool support") {
	run_test_from_file("bool_support.cpp", "Bool support", false);
}

TEST_CASE("Modulo operator") {
	run_test_from_file("modulo_operator.cpp", "Modulo operator", false);
}

TEST_CASE("Assignment operators") {
	run_test_from_file("assignment_operators.cpp", "Assignment operators", false);
}

TEST_CASE("Increment and decrement") {
	run_test_from_file("increment_decrement.cpp", "Increment and decrement", false);
}

TEST_CASE("Float arithmetic") {
	run_test_from_file("float_arithmetic.cpp", "Float arithmetic", false);
}

TEST_CASE("Double arithmetic") {
	run_test_from_file("double_arithmetic.cpp", "Double arithmetic", false);
}

TEST_CASE("Float comparisons") {
	run_test_from_file("float_comparisons.cpp", "Float comparisons", false);
}

TEST_CASE("Mixed arithmetic") {
	run_test_from_file("mixed_arithmetic.cpp", "Mixed arithmetic", false);
}

TEST_CASE("If statements") {
	run_test_from_file("if_statements.cpp", "If statements", false);
}

TEST_CASE("For loops") {
	run_test_from_file("for_loops_test.cpp", "For loops", false);
}

TEST_CASE("While loops") {
	run_test_from_file("while_loops.cpp", "While loops", false);
}

TEST_CASE("Do-while loops") {
	run_test_from_file("do_while_loops.cpp", "Do-while loops", false);
}

TEST_CASE("Switch statements") {
	run_test_from_file("test_switch.cpp", "Switch statements", false);
}

TEST_CASE("C-style casts") {
	run_test_from_file("test_c_style_casts.cpp", "C-style casts", false);
}

TEST_CASE("Goto and labels") {
	run_test_from_file("test_goto_labels.cpp", "Goto and labels", false);
}

TEST_CASE("Namespace features") {
	run_test_from_file("test_using_directives.cpp", "Namespace features", false);
}

TEST_CASE("Anonymous namespace") {
	run_test_from_file("test_anonymous_ns.cpp", "Anonymous namespace", false);
}

TEST_CASE("Using directives and aliases") {
	run_test_from_file("test_using_enhanced.cpp", "Using directives and aliases", false);
}

TEST_CASE("Auto type deduction") {
	run_test_from_file("test_auto_simple.cpp", "Auto type deduction", false);
}

TEST_CASE("Control flow comprehensive") {
	run_test_from_file("control_flow_comprehensive.cpp", "Control flow comprehensive", false);
}

TEST_CASE("While loops comprehensive") {
	run_test_from_file("while_loops_comprehensive.cpp", "While loops comprehensive", false);
}

TEST_CASE("While loops with break and continue") {
	run_test_from_file("while_loops_with_break_continue.cpp", "While loops with break and continue", false);
}

TEST_CASE("For loops simple") {
	run_test_from_file("for_loops_simple.cpp", "For loops simple", false);
}

TEST_CASE("For loops") {
	run_test_from_file("for_loops.cpp", "For loops", false);
}

TEST_CASE("Float double mixed") {
	run_test_from_file("float_double_mixed.cpp", "Float double mixed", false);
}

TEST_CASE("Float edge cases") {
	run_test_from_file("float_edge_cases.cpp", "Float edge cases", false);
}

TEST_CASE("Double literals") {
	run_test_from_file("double_literals.cpp", "Double literals", false);
}

TEST_CASE("Array declaration only") {
	run_test_from_file("test_array_decl_only.cpp", "Array declaration only", false);
}

TEST_CASE("Array basic") {
	run_test_from_file("test_array_basic.cpp", "Array basic", false);
}

TEST_CASE("Array comprehensive") {
	run_test_from_file("test_arrays_comprehensive.cpp", "Array comprehensive", false);
}

TEST_CASE("Break and continue") {
	run_test_from_file("test_break_continue.cpp", "Break and continue", false);
}

TEST_CASE("Nested break and continue") {
	run_test_from_file("test_nested_break_continue.cpp", "Nested break and continue", false);
}

TEST_CASE("Break targets inner loop") {
	run_test_from_file("test_break_targets_inner.cpp", "Break targets inner loop", false);
}

TEST_CASE("Compound assignment operators") {
	run_test_from_file("test_compound_assign.cpp", "Compound assignment operators", false);
}

TEST_CASE("All loops") {
	run_test_from_file("test_all_loops.cpp", "All loops", false);
}

TEST_CASE("All increments") {
	run_test_from_file("test_all_increments.cpp", "All increments", false);
}

TEST_CASE("All test mix") {
	run_test_from_file("test_all_mix.cpp", "Mixed things", false);
}

TEST_CASE("Const test") {
	run_test_from_file("test_pointer_declarations.cpp", "Const test", false);
}

TEST_CASE("Struct member access") {
	run_test_from_file("test_struct_simple.cpp", "Struct member access", false);
}

TEST_CASE("Alignas:Struct") {
	run_test_from_file("test_alignas.cpp", "Alignas on struct declarations", false);
}

TEST_CASE("Alignas:Variables") {
	run_test_from_file("test_alignas_var.cpp", "Alignas on local variables", false);
}

TEST_CASE("Alignas:NestedStructs") {
	run_test_from_file("test_nested_struct.cpp", "Nested struct alignment", false);
}

TEST_CASE("Struct member function") {
	run_test_from_file("test_struct_method_simple.cpp", "Simple member function", false);
}

TEST_CASE("sizeof() and offsetof()") {
	run_test_from_file("test_sizeof_offsetof.cpp", "sizeof() and offsetof()", false);
}

TEST_CASE("Namespace:Nested") {
	run_test_from_file("test_nested_namespace.cpp", "Nested namespace declarations", false);
}

TEST_CASE("Enums") {
	run_test_from_file("test_enum.cpp", "Enum and Enum class tests", false);
}

TEST_CASE("String literals and puts") {
	run_test_from_file("test_puts_stack.cpp", "Tests char literals and .rdata strings by calling puts()", false);
}

static const char* test_function_name;
void test_function() {
	test_function_name = __FUNCTION__;
}

#if 0	// Disabled until we get a preprocessor refactor that works with source text and not just files
TEST_CASE("Parser:FunctionNameIdentifiers") {
	SUBCASE("__FUNCTION__, __func__, __PRETTY_FUNCTION__ inside function") {
		// Test that these identifiers work inside a function and expand to the function name
		std::string code = R"(
			void test_function() {
				const char* msvc_name = __FUNCTION__;
				const char* standard_name = __func__;
				const char* gcc_name = __PRETTY_FUNCTION__;
			}

			int main() {
				const char* name = __func__;
				return 0;
			}
		)";

		Lexer lexer(code);
		compile_context.setInputFile("test_function_names.cpp");
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();

		// Should parse successfully
		CHECK(!parse_result.is_error());

		if (!parse_result.is_error()) {
			const auto& ast = parser.get_nodes();

			// Convert to IR to verify the string literals are created correctly
			AstToIr converter(gSymbolTable, compile_context);
			for (auto& node_handle : ast) {
				converter.visit(node_handle);
			}

			test_function();	// to initialize test_function_name
			const auto& ir = converter.getIr();
			for (const auto& instruction : ir.getInstructions()) {
				std::puts(instruction.getReadableString().c_str());
				if (instruction.getOpcode() == IrOpcode::StringLiteral) {
					const std::string_view func_name = instruction.getOperandAs<std::string_view>(1);
					bool is_valid_func_name = (func_name == test_function_name || func_name == "void test_function()"sv || func_name == "main"sv);
					CHECK(is_valid_func_name);
				}
			}
		}
	}
}
#endif

TEST_CASE("Constructor with no parameters") {
	run_test_from_file("test_constructor_no_params.cpp", "Constructor with no parameters", false);
}

TEST_CASE("Constructor with parameters") {
	run_test_from_file("test_constructor_with_params.cpp", "Constructor with parameters", false);
}

TEST_CASE("Constructor with initializer list") {
	run_test_from_file("test_constructor_initializer_list.cpp", "Constructor with initializer list", false);
}

TEST_CASE("Destructor") {
	run_test_from_file("test_destructor.cpp", "Destructor", false);
}

TEST_CASE("Default constructor generation") {
	run_test_from_file("test_default_constructor.cpp", "Default constructor generation", false);
}

TEST_CASE("Copy constructor generation") {
	run_test_from_file("test_copy_constructor.cpp", "Copy constructor generation", false);
}

TEST_CASE("Implicit copy constructor generation") {
	run_test_from_file("test_implicit_copy_constructor.cpp", "Implicit copy constructor generation", false);
}

TEST_CASE("Implicit copy assignment operator generation") {
	run_test_from_file("test_implicit_operator_assign.cpp", "Implicit copy assignment operator generation", false);
}

TEST_SUITE("= default and = delete special member functions") {
	// C++20 Rule: Explicitly defaulted functions behave as if they were implicitly declared
	TEST_CASE("Defaulted default constructor") {
		const std::string code = R"(
			struct Point {
				int x;
				int y;
				Point() = default;
			};

			int main() {
				Point p;
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	TEST_CASE("Defaulted copy constructor") {
		const std::string code = R"(
			struct Point {
				int x;
				int y;
				Point(Point& other) = default;
			};

			int main() {
				Point p1;
				Point p2(p1);
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	TEST_CASE("Defaulted move constructor") {
		const std::string code = R"(
			struct Point {
				int x;
				int y;
				Point(Point&& other) = default;
			};

			int main() {
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	TEST_CASE("Defaulted copy assignment operator") {
		const std::string code = R"(
			struct Point {
				int x;
				int y;
				Point& operator=(Point& other) = default;
			};

			int main() {
				Point p1;
				Point p2;
				p2 = p1;
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	TEST_CASE("Defaulted move assignment operator") {
		const std::string code = R"(
			struct Point {
				int x;
				int y;
				Point& operator=(Point&& other) = default;
			};

			int main() {
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	TEST_CASE("Defaulted destructor") {
		const std::string code = R"(
			struct Point {
				int x;
				int y;
				~Point() = default;
			};

			int main() {
				Point p;
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	// C++20 Rule: Deleted functions participate in overload resolution but cause compilation error if selected
	TEST_CASE("Deleted copy constructor - NonCopyable pattern") {
		const std::string code = R"(
			struct NonCopyable {
				int x;
				NonCopyable() = default;
				NonCopyable(NonCopyable& other) = delete;
			};

			int main() {
				NonCopyable nc;
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	TEST_CASE("Deleted copy assignment operator") {
		const std::string code = R"(
			struct NonCopyable {
				int x;
				NonCopyable() = default;
				NonCopyable& operator=(NonCopyable& other) = delete;
			};

			int main() {
				NonCopyable nc;
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	TEST_CASE("Deleted move constructor") {
		const std::string code = R"(
			struct NonMovable {
				int x;
				NonMovable() = default;
				NonMovable(NonMovable&& other) = delete;
			};

			int main() {
				NonMovable nm;
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	TEST_CASE("Deleted move assignment operator") {
		const std::string code = R"(
			struct NonMovable {
				int x;
				NonMovable() = default;
				NonMovable& operator=(NonMovable&& other) = delete;
			};

			int main() {
				NonMovable nm;
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	// C++20 Rule: Deleting copy operations prevents implicit move generation
	TEST_CASE("Deleted copy constructor suppresses implicit move constructor") {
		const std::string code = R"(
			struct Test {
				int x;
				Test() = default;
				Test(Test& other) = delete;
				// Move constructor is NOT implicitly generated because copy constructor is user-declared
			};

			int main() {
				Test t;
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	// C++20 Rule: All special member functions can be defaulted or deleted
	TEST_CASE("All special member functions explicitly defaulted") {
		const std::string code = R"(
			struct AllDefaulted {
				int x;
				int y;

				AllDefaulted() = default;
				AllDefaulted(AllDefaulted& other) = default;
				AllDefaulted(AllDefaulted&& other) = default;
				AllDefaulted& operator=(AllDefaulted& other) = default;
				AllDefaulted& operator=(AllDefaulted&& other) = default;
				~AllDefaulted() = default;
			};

			int main() {
				AllDefaulted a1;
				AllDefaulted a2(a1);
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	// C++20 Rule: Deleted default constructor prevents object creation
	TEST_CASE("Deleted default constructor") {
		const std::string code = R"(
			struct NoDefault {
				int x;
				NoDefault() = delete;
				NoDefault(int val) : x(val) {}
			};

			int main() {
				NoDefault nd(42);
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	// C++20 Rule: Deleted destructor prevents object destruction
	TEST_CASE("Deleted destructor") {
		const std::string code = R"(
			struct NoDestroy {
				int x;
				~NoDestroy() = delete;
			};

			int main() {
				// Cannot create NoDestroy on stack (would need to destroy it)
				// Can only create via new (and never delete)
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}

	// C++20 Rule: Mix of defaulted and user-defined special members
	TEST_CASE("Mix of defaulted and user-defined special members") {
		const std::string code = R"(
			struct Mixed {
				int* data;

				Mixed() : data(0) {}  // User-defined default constructor
				Mixed(Mixed& other) = default;  // Defaulted copy constructor
				Mixed(Mixed&& other) = default;  // Defaulted move constructor
				Mixed& operator=(Mixed& other) = default;  // Defaulted copy assignment
				~Mixed() = default;  // Defaulted destructor
			};

			int main() {
				Mixed m1;
				Mixed m2(m1);
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());
	}
}

TEST_SUITE("new and delete operators") {
	TEST_CASE("Simple new and delete for int") {
		const std::string code = R"(
			int main() {
				int* p = new int;
				*p = 42;
				delete p;
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter(gSymbolTable, compile_context, parser);
		for (auto& node_handle : ast) {
			converter.visit(node_handle);
		}

		const auto& ir = converter.getIr();

		std::puts("\n=== Test: Simple new and delete for int ===");
		for (const auto& instruction : ir.getInstructions()) {
			std::puts(instruction.getReadableString().c_str());
		}

		// Check that we have HeapAlloc and HeapFree instructions
		bool has_heap_alloc = false;
		bool has_heap_free = false;
		for (const auto& instruction : ir.getInstructions()) {
			if (instruction.getOpcode() == IrOpcode::HeapAlloc) {
				has_heap_alloc = true;
			}
			if (instruction.getOpcode() == IrOpcode::HeapFree) {
				has_heap_free = true;
			}
		}
		CHECK(has_heap_alloc);
		CHECK(has_heap_free);
	}

	TEST_CASE("Array new and delete") {
		const std::string code = R"(
			int main() {
				int* arr = new int[10];
				arr[0] = 1;
				arr[9] = 10;
				delete[] arr;
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter(gSymbolTable, compile_context, parser);
		for (auto& node_handle : ast) {
			converter.visit(node_handle);
		}

		const auto& ir = converter.getIr();

		std::puts("\n=== Test: Array new and delete ===");
		for (const auto& instruction : ir.getInstructions()) {
			std::puts(instruction.getReadableString().c_str());
		}

		// Check that we have HeapAllocArray and HeapFreeArray instructions
		bool has_heap_alloc_array = false;
		bool has_heap_free_array = false;
		for (const auto& instruction : ir.getInstructions()) {
			if (instruction.getOpcode() == IrOpcode::HeapAllocArray) {
				has_heap_alloc_array = true;
			}
			if (instruction.getOpcode() == IrOpcode::HeapFreeArray) {
				has_heap_free_array = true;
			}
		}
		CHECK(has_heap_alloc_array);
		CHECK(has_heap_free_array);
	}

	TEST_CASE("New with constructor arguments") {
		const std::string code = R"(
			struct Point {
				int x;
				int y;
				Point(int a, int b) : x(a), y(b) {}
			};

			int main() {
				Point* p = new Point(10, 20);
				delete p;
				return 0;
			}
		)";

		Lexer lexer(code);
		Parser parser(lexer, compile_context);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter(gSymbolTable, compile_context, parser);
		for (auto& node_handle : ast) {
			converter.visit(node_handle);
		}

		const auto& ir = converter.getIr();

		std::puts("\n=== Test: New with constructor arguments ===");
		for (const auto& instruction : ir.getInstructions()) {
			std::puts(instruction.getReadableString().c_str());
		}

		// Check that we have HeapAlloc and ConstructorCall instructions
		bool has_heap_alloc = false;
		bool has_constructor_call = false;
		for (const auto& instruction : ir.getInstructions()) {
			if (instruction.getOpcode() == IrOpcode::HeapAlloc) {
				has_heap_alloc = true;
			}
			if (instruction.getOpcode() == IrOpcode::ConstructorCall) {
				has_constructor_call = true;
			}
		}
		CHECK(has_heap_alloc);
		CHECK(has_constructor_call);
	}
}

TEST_CASE("Basic class inheritance") {
	run_test_from_file("test_inheritance_basic.cpp", "Class inheritance", false);
}

TEST_CASE("Virtual functions") {
	run_test_from_file("test_virtual_basic.cpp", "Virtual functions", false);
}

TEST_CASE("Virtual class inheritance") {
	run_test_from_file("test_virtual_inheritance.cpp", "Virtual class inheritance", false);
}

TEST_CASE("Diamond inheritance") {
	run_test_from_file("test_diamond_inheritance.cpp", "Diamond inheritance", false);
}

TEST_CASE("Abstract classes") {
	run_test_from_file("test_abstract_class.cpp", "Abstract classes", false);
}

TEST_CASE("Virtual base classes") {
	run_test_from_file("test_virtual_base_classes.cpp", "Virtual base classes", false);
}

TEST_CASE("RTTI") {
	run_test_from_file("test_rtti_basic.cpp", "Dynamic cast", false);
}

TEST_CASE("Global variables") {
	run_test_from_file("global_variables.cpp", "Global variables", false);
}

TEST_CASE("Static variables") {
	run_test_from_file("static_local.cpp", "Static variables", false);
}

TEST_CASE("RegisterSpilling:ManyLocals") {
	run_test_from_file("test_register_spilling.cpp", "Register spilling with many local variables", false);
}

TEST_CASE("opererator() call") {
	run_test_from_file("test_operator_call.cpp", "Test of calling operator()", false);
}

TEST_CASE("Lambda: No captures") {
	run_test_from_file("test_lambda_no_capture.cpp", "Local lambdas without captures", false);
}

TEST_CASE("Lambda: Simple captures") {
	run_test_from_file("test_lambda_capture_simple.cpp", "Lambda with simple by-value captures", false);
}

TEST_CASE("Lambda: Comprehensive captures") {
	run_test_from_file("test_lambda_captures_comprehensive.cpp", "Comprehensive lambda capture tests", false);
}

TEST_CASE("FunctionPointer:BasicDeclaration") {
	run_test_from_file("test_function_pointer_basic.cpp", "Basic function pointer declaration", false);
}

TEST_CASE("Functions:Extern C") {
	run_test_from_file("test_extern_c_single.cpp", "Test of extern C declaration", false);
}

TEST_CASE("Typedef:Basic") {
	run_test_from_file("test_typedef.cpp", "Basic typedef support", false);
}

TEST_CASE("Decltype") {
	run_test_from_file("test_decltype.cpp", "Decltype type deduction", false);
}

TEST_CASE("DesignatedInitializers") {
	run_test_from_file("test_designated_init.cpp", "Designated initializers", false);
}

TEST_CASE("Friend classes") {
	run_test_from_file("test_friend_declarations.cpp", "Friend declarations", false);
}

TEST_CASE("Nested classes") {
	run_test_from_file("test_nested_classes.cpp", "Nested classes", false);
}

TEST_CASE("Comma operator") {
	run_test_from_file("test_comma_comprehensive.cpp", "Nested classes", false);
}

TEST_SUITE("Namespaces") {
	TEST_CASE("Global namespaces") {
		run_test_from_file("test_global_namespace_scope.cpp", "Global namespace scope", false);
	}
}

TEST_SUITE("Delayed parsing - forward references in member functions") {
	// C++20 Rule: Inline member function bodies are parsed in complete-class context
	// This means they can reference members declared later in the class

	TEST_CASE("Member function references later member variable") {
		run_test_from_file("test_delayed_parsing_member_var.cpp", "Delayed parsing: member variable forward reference", false);
	}

	TEST_CASE("Member function calls later member function") {
		run_test_from_file("test_delayed_parsing_member_func.cpp", "Delayed parsing: member function forward reference", false);
	}

	TEST_CASE("Constructor references later member variable") {
		run_test_from_file("test_delayed_parsing_constructor.cpp", "Delayed parsing: constructor forward reference", false);
	}

	TEST_CASE("Destructor references member variables") {
		run_test_from_file("test_delayed_parsing_destructor.cpp", "Delayed parsing: destructor forward reference", false);
	}

	TEST_CASE("Multiple member functions with forward references") {
		run_test_from_file("test_delayed_parsing_multiple.cpp", "Delayed parsing: multiple forward references", false);
	}
}

TEST_SUITE("Member initialization") {
	TEST_CASE("Simple member initialization") {
		run_test_from_file("test_member_init_simple.cpp", "Member initialization: simple", false);
	}

	TEST_CASE("Member initialization with explicit constructor") {
		run_test_from_file("test_member_init_explicit_ctor.cpp", "Member initialization: explicit constructor", false);
	}

	TEST_CASE("Constructor initializer overrides default") {
		run_test_from_file("test_member_init_override.cpp", "Member initialization: override", false);
	}

	TEST_CASE("Nested struct with member initialization") {
		run_test_from_file("test_member_init_nested.cpp", "Member initialization: nested", false);
	}

	TEST_CASE("Mixed initialization") {
		run_test_from_file("test_member_init_mixed.cpp", "Member initialization: mixed", false);
	}

	TEST_CASE("Various member initialization forms") {
		run_test_from_file("test_member_init_designated.cpp", "Member initialization: various forms", false);
	}

	TEST_CASE("Local struct declaration") {
		run_test_from_file("test_local_struct.cpp", "Local struct declaration", false);
	}
}

TEST_SUITE("Templates") {
	TEST_CASE("Templates:Simple") {
		run_test_from_file("template_simple.cpp", "Templates:Simple", false);
	}

	TEST_CASE("Templates:ParsingTest") {
		run_test_from_file("template_parsing_test.cpp", "Templates:ParsingTest", false);
	}

	TEST_CASE("Templates:Declaration") {
		run_test_from_file("template_declaration.cpp", "Templates:Declaration", false);
	}

	// Template instantiation tests (Phase 2)
	TEST_CASE("Templates:InstantiationDecl") {
		run_test_from_file("template_inst_decl.cpp", "Templates:InstantiationDecl", false);
	}

	TEST_CASE("Templates:InstantiationSimple") {
		run_test_from_file("template_inst_simple.cpp", "Templates:InstantiationSimple", false);
	}

	TEST_CASE("Templates:InstantiationMultipleTypes") {
		run_test_from_file("template_inst_multi.cpp", "Templates:InstantiationMultipleTypes", false);
	}

	// Template function body tests (Phase 3)
	TEST_CASE("Templates:WithBody") {
		run_test_from_file("template_with_body.cpp", "Templates:WithBody", false);
	}

	TEST_CASE("Templates:BodyTest") {
		run_test_from_file("template_body_test.cpp", "Templates:BodyTest", false);
	}

	// Explicit template arguments tests (Phase 4)
	TEST_CASE("Templates:ExplicitArgs") {
		run_test_from_file("template_explicit_args.cpp", "Templates:ExplicitArgs", false);
	}

	// Multiple template parameters tests (Phase 5)
	TEST_CASE("Templates:MultipleParams") {
		run_test_from_file("template_multi_param.cpp", "Templates:MultipleParams", false);
	}

	// Class template tests (Phase 6)
	TEST_CASE("Templates:ClassSimple") {
		run_test_from_file("template_class_simple.cpp", "Templates:ClassSimple", false);
	}

	// Class template instantiation tests (Phase 7)
	TEST_CASE("Templates:ClassInst") {
		run_test_from_file("template_class_inst.cpp", "Templates:ClassInst", false);
	}

	// Class template member function tests (Phase 8)
	TEST_CASE("Templates:ClassMethods") {
		run_test_from_file("template_class_methods.cpp", "Templates:ClassMethods", false);
	}

	// Out-of-line template member function definitions
	TEST_CASE("Templates:OutOfLine") {
		run_test_from_file("template_out_of_line.cpp", "Templates:OutOfLine", false);
	}

	// Template template parameter tests
	TEST_CASE("Templates:TemplateTemplateParams") {
		run_test_from_file("template_template_params.cpp", "Templates:TemplateTemplateParams", false);
	}
}