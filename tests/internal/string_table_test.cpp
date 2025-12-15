// Test file for StringTable functionality
// This tests the string interning system for FlashCpp

#include "../src/StringTable.h"
#include <iostream>
#include <cassert>
#include <string>
#include <unordered_map>

// Global allocator (declared extern, defined in src/Parser.cpp)
extern ChunkedStringAllocator gChunkedStringAllocator;

void test_string_handle_creation() {
	std::cout << "Test: StringHandle creation and round-trip... ";
	
	std::string_view test_str = "hello_world";
	StringHandle handle = StringTable::createStringHandle(test_str);
	
	assert(handle.isValid() && "Handle should be valid");
	
	std::string_view retrieved = StringTable::getStringView(handle);
	assert(retrieved == test_str && "Retrieved string should match original");
	
	std::cout << "PASSED\n";
}

void test_string_interning() {
	std::cout << "Test: String interning deduplication... ";
	
	StringTable::clearInternMap();
	
	std::string_view str1 = "variable_name";
	std::string_view str2 = "variable_name";  // Same content, different view
	
	StringHandle handle1 = StringTable::getOrInternStringHandle(str1);
	StringHandle handle2 = StringTable::getOrInternStringHandle(str2);
	
	assert(handle1 == handle2 && "Same string should return same handle");
	assert(StringTable::getInternedCount() == 1 && "Should have only 1 interned string");
	
	std::string_view str3 = "different_name";
	StringHandle handle3 = StringTable::getOrInternStringHandle(str3);
	
	assert(handle3 != handle1 && "Different strings should have different handles");
	assert(StringTable::getInternedCount() == 2 && "Should have 2 interned strings");
	
	std::cout << "PASSED\n";
}

void test_hash_consistency() {
	std::cout << "Test: Hash consistency... ";
	
	std::string_view test_str = "test_variable";
	
	// Compute hash directly
	uint64_t computed_hash = StringTable::hashString(test_str);
	
	// Create handle and retrieve hash
	StringHandle handle = StringTable::createStringHandle(test_str);
	uint64_t stored_hash = StringTable::getHash(handle);
	
	assert(computed_hash == stored_hash && "Stored hash should match computed hash");
	
	std::cout << "PASSED\n";
}

void test_empty_string() {
	std::cout << "Test: Empty string handling... ";
	
	std::string_view empty = "";
	StringHandle handle = StringTable::createStringHandle(empty);
	
	assert(handle.isValid() && "Handle should be valid even for empty string");
	
	std::string_view retrieved = StringTable::getStringView(handle);
	assert(retrieved.empty() && "Retrieved string should be empty");
	assert(retrieved.size() == 0 && "Retrieved string should have size 0");
	
	std::cout << "PASSED\n";
}

void test_long_string() {
	std::cout << "Test: Long string handling... ";
	
	std::string long_str(1000, 'x');
	StringHandle handle = StringTable::createStringHandle(long_str);
	
	assert(handle.isValid() && "Handle should be valid for long string");
	
	std::string_view retrieved = StringTable::getStringView(handle);
	assert(retrieved.size() == 1000 && "Retrieved string should have correct size");
	assert(retrieved == long_str && "Retrieved string should match original");
	
	std::cout << "PASSED\n";
}

void test_special_characters() {
	std::cout << "Test: Special characters... ";
	
	std::string_view special = "var$name_123!@#";
	StringHandle handle = StringTable::createStringHandle(special);
	
	std::string_view retrieved = StringTable::getStringView(handle);
	assert(retrieved == special && "Special characters should be preserved");
	
	std::cout << "PASSED\n";
}

void test_handle_as_map_key() {
	std::cout << "Test: StringHandle as map key... ";
	
	std::unordered_map<StringHandle, int> test_map;
	
	StringHandle h1 = StringTable::createStringHandle("key1");
	StringHandle h2 = StringTable::createStringHandle("key2");
	StringHandle h3 = StringTable::createStringHandle("key1");  // Duplicate content, different handle
	
	test_map[h1] = 100;
	test_map[h2] = 200;
	test_map[h3] = 300;  // Should overwrite or create new entry depending on handle equality
	
	assert(test_map.size() == 3 && "Map should handle StringHandle keys");
	assert(test_map[h1] == 100 && "Should retrieve correct value");
	assert(test_map[h2] == 200 && "Should retrieve correct value");
	
	std::cout << "PASSED\n";
}

int main() {
	std::cout << "=== StringTable Unit Tests ===\n\n";
	
	test_string_handle_creation();
	test_string_interning();
	test_hash_consistency();
	test_empty_string();
	test_long_string();
	test_special_characters();
	test_handle_as_map_key();
	
	std::cout << "\n=== All tests PASSED ===\n";
	return 0;
}
