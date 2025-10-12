#include "CompileContext.h"
#include "FileTree.h"
#include "FileReader.h"
#include "Token.h"
#include "Lexer.h"
#include "Parser.h"
#include "CodeGen.h"
#include "IRConverter.h"
#include "ChunkedAnyVector.h"
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
void run_test_from_file(const std::string& filename, const std::string& test_name, bool generate_obj = true) {
    std::string code = read_test_file(filename);

    Lexer lexer(code);
    Parser parser(lexer);
    auto parse_result = parser.parse();

	CHECK(!parse_result.is_error());

    if (parse_result.is_error()) {
        std::printf("Parse error in %s: %s\n", test_name.c_str(), parse_result.error_message().c_str());
		return;
    }

    const auto& ast = parser.get_nodes();

    AstToIr converter;
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
	const auto& sections1 = reader1.get_sections();
	const auto& sections2 = reader2.get_sections();

	// Create a map of sections by name for the second reader
	std::map<std::string, const COFFI::section*> sections2_by_name;
	for (const auto& sec : sections2) {
		sections2_by_name[sec->get_name()] = sec;
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
	auto find_section = [](const COFFI::coffi& reader, const std::string& name) -> COFFI::section* {
		const auto& sections = reader.get_sections();
		for (const auto& sec : sections) {
			if (sec->get_name() == name) {
				return sec;
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
		Parser parser(lexer);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter;
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
		Parser parser(lexer);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter;
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
		Parser parser(lexer);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter;
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
		Parser parser(lexer);
		auto parse_result = parser.parse();
		CHECK(!parse_result.is_error());

		const auto& ast = parser.get_nodes();

		AstToIr converter;
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
	Parser parser(lexer);
	auto parse_result = parser.parse();
	CHECK(!parse_result.is_error());

	const auto& ast = parser.get_nodes();

	AstToIr converter;
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

TEST_CASE("Shift operations") {
	run_test_from_file("shift_operations.cpp", "Shift operations", true);
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
	run_test_from_file("for_loops_test.cpp", "For loops", true);
}

TEST_CASE("While loops") {
	run_test_from_file("while_loops.cpp", "While loops", false);
}

TEST_CASE("Do-while loops") {
	run_test_from_file("do_while_loops.cpp", "Do-while loops", false);
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

