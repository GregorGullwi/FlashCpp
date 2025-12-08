// Unit tests for DWARF CFI encoding utilities
#include "../src/DwarfCFI.h"
#include <cstdio>
#include <cstring>

// Simple test framework
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
	do { \
		if (condition) { \
			printf("✓ %s\n", message); \
			tests_passed++; \
		} else { \
			printf("✗ %s\n", message); \
			tests_failed++; \
		} \
	} while(0)

// Helper to compare vectors
bool vectorsEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i] != b[i]) return false;
	}
	return true;
}

// Test ULEB128 encoding
void test_uleb128() {
	printf("\n--- Testing ULEB128 Encoding ---\n");
	
	// Test 0
	{
		auto result = DwarfCFI::encodeULEB128(0);
		std::vector<uint8_t> expected = {0x00};
		TEST_ASSERT(vectorsEqual(result, expected), "ULEB128(0) = 0x00");
	}
	
	// Test 1
	{
		auto result = DwarfCFI::encodeULEB128(1);
		std::vector<uint8_t> expected = {0x01};
		TEST_ASSERT(vectorsEqual(result, expected), "ULEB128(1) = 0x01");
	}
	
	// Test 127 (max single byte)
	{
		auto result = DwarfCFI::encodeULEB128(127);
		std::vector<uint8_t> expected = {0x7f};
		TEST_ASSERT(vectorsEqual(result, expected), "ULEB128(127) = 0x7f");
	}
	
	// Test 128 (requires 2 bytes)
	{
		auto result = DwarfCFI::encodeULEB128(128);
		std::vector<uint8_t> expected = {0x80, 0x01};
		TEST_ASSERT(vectorsEqual(result, expected), "ULEB128(128) = 0x80 0x01");
	}
	
	// Test 624485 (example from DWARF spec)
	{
		auto result = DwarfCFI::encodeULEB128(624485);
		std::vector<uint8_t> expected = {0xe5, 0x8e, 0x26};
		TEST_ASSERT(vectorsEqual(result, expected), "ULEB128(624485) = 0xe5 0x8e 0x26");
	}
}

// Test SLEB128 encoding
void test_sleb128() {
	printf("\n--- Testing SLEB128 Encoding ---\n");
	
	// Test 0
	{
		auto result = DwarfCFI::encodeSLEB128(0);
		std::vector<uint8_t> expected = {0x00};
		TEST_ASSERT(vectorsEqual(result, expected), "SLEB128(0) = 0x00");
	}
	
	// Test 1
	{
		auto result = DwarfCFI::encodeSLEB128(1);
		std::vector<uint8_t> expected = {0x01};
		TEST_ASSERT(vectorsEqual(result, expected), "SLEB128(1) = 0x01");
	}
	
	// Test -1
	{
		auto result = DwarfCFI::encodeSLEB128(-1);
		std::vector<uint8_t> expected = {0x7f};
		TEST_ASSERT(vectorsEqual(result, expected), "SLEB128(-1) = 0x7f");
	}
	
	// Test -2
	{
		auto result = DwarfCFI::encodeSLEB128(-2);
		std::vector<uint8_t> expected = {0x7e};
		TEST_ASSERT(vectorsEqual(result, expected), "SLEB128(-2) = 0x7e");
	}
	
	// Test 127 (max positive single byte)
	{
		auto result = DwarfCFI::encodeSLEB128(127);
		std::vector<uint8_t> expected = {0xff, 0x00};
		TEST_ASSERT(vectorsEqual(result, expected), "SLEB128(127) = 0xff 0x00");
	}
	
	// Test -128 (min negative single byte)
	{
		auto result = DwarfCFI::encodeSLEB128(-128);
		std::vector<uint8_t> expected = {0x80, 0x7f};
		TEST_ASSERT(vectorsEqual(result, expected), "SLEB128(-128) = 0x80 0x7f");
	}
	
	// Test -8 (common data alignment on x86-64)
	{
		auto result = DwarfCFI::encodeSLEB128(-8);
		std::vector<uint8_t> expected = {0x78};
		TEST_ASSERT(vectorsEqual(result, expected), "SLEB128(-8) = 0x78");
	}
}

// Test pointer encoding
void test_pointer_encoding() {
	printf("\n--- Testing Pointer Encoding ---\n");
	
	// Test absptr (8 bytes on x86-64)
	{
		auto result = DwarfCFI::encodePointer(0x12345678, DwarfCFI::DW_EH_PE_absptr);
		TEST_ASSERT(result.size() == 8, "absptr encoding is 8 bytes");
		TEST_ASSERT(result[0] == 0x78 && result[1] == 0x56 && result[2] == 0x34 && result[3] == 0x12,
		           "absptr little-endian byte order");
	}
	
	// Test udata4
	{
		auto result = DwarfCFI::encodePointer(0x12345678, DwarfCFI::DW_EH_PE_udata4);
		std::vector<uint8_t> expected = {0x78, 0x56, 0x34, 0x12};
		TEST_ASSERT(vectorsEqual(result, expected), "udata4 encoding");
	}
	
	// Test sdata4 (negative)
	{
		auto result = DwarfCFI::encodePointer(static_cast<uint64_t>(-4), DwarfCFI::DW_EH_PE_sdata4);
		std::vector<uint8_t> expected = {0xfc, 0xff, 0xff, 0xff};
		TEST_ASSERT(vectorsEqual(result, expected), "sdata4 encoding (negative)");
	}
	
	// Test uleb128
	{
		auto result = DwarfCFI::encodePointer(128, DwarfCFI::DW_EH_PE_uleb128);
		std::vector<uint8_t> expected = {0x80, 0x01};
		TEST_ASSERT(vectorsEqual(result, expected), "uleb128 encoding");
	}
	
	// Test omit
	{
		auto result = DwarfCFI::encodePointer(0, DwarfCFI::DW_EH_PE_omit);
		TEST_ASSERT(result.empty(), "omit encoding produces empty result");
	}
}

// Test helper functions
void test_helpers() {
	printf("\n--- Testing Helper Functions ---\n");
	
	// Test appendULEB128
	{
		std::vector<uint8_t> data = {0x01, 0x02};
		DwarfCFI::appendULEB128(data, 128);
		std::vector<uint8_t> expected = {0x01, 0x02, 0x80, 0x01};
		TEST_ASSERT(vectorsEqual(data, expected), "appendULEB128 appends correctly");
	}
	
	// Test appendSLEB128
	{
		std::vector<uint8_t> data = {0x01, 0x02};
		DwarfCFI::appendSLEB128(data, -8);
		std::vector<uint8_t> expected = {0x01, 0x02, 0x78};
		TEST_ASSERT(vectorsEqual(data, expected), "appendSLEB128 appends correctly");
	}
}

int main() {
	printf("DWARF CFI Encoding Tests\n");
	printf("=========================\n");
	
	test_uleb128();
	test_sleb128();
	test_pointer_encoding();
	test_helpers();
	
	printf("\n=========================\n");
	printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
	
	return tests_failed > 0 ? 1 : 0;
}
