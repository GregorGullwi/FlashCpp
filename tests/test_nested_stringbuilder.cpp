// Test nested StringBuilder usage
#include <cassert>
#include <iostream>
#include <string_view>

// Forward declarations - these would come from ChunkedString.h
class ChunkedStringAllocator;
class StringBuilder;
extern ChunkedStringAllocator gChunkedStringAllocator;

// Mock implementation for testing
#include "../src/ChunkedString.h"

ChunkedStringAllocator gChunkedStringAllocator;

// Function that uses StringBuilder internally (simulates generateMangledName)
std::string_view createMangledName(std::string_view name, std::string_view suffix) {
    StringBuilder builder;
    builder.append("mangled_");
    builder.append(name);
    builder.append("_");
    builder.append(suffix);
    return builder.commit();
}

// Function that uses StringBuilder and calls another function that also uses StringBuilder
std::string_view createQualifiedName(std::string_view ns, std::string_view name) {
    StringBuilder builder;
    builder.append(ns);
    builder.append("::");
    
    // This creates a nested StringBuilder inside createMangledName
    std::string_view mangled = createMangledName(name, "v1");
    
    builder.append(mangled);
    return builder.commit();
}

int main() {
    std::cout << "Testing nested StringBuilder usage...\n";
    
    // Test 1: Simple non-nested usage
    {
        StringBuilder sb;
        sb.append("hello");
        sb.append(" ");
        sb.append("world");
        std::string_view result = sb.commit();
        assert(result == "hello world");
        std::cout << "Test 1 passed: Simple usage works\n";
    }
    
    // Test 2: Sequential StringBuilders (should work even without nesting support)
    {
        std::string_view r1 = createMangledName("foo", "v1");
        std::string_view r2 = createMangledName("bar", "v2");
        assert(r1 == "mangled_foo_v1");
        assert(r2 == "mangled_bar_v2");
        std::cout << "Test 2 passed: Sequential StringBuilders work\n";
    }
    
    // Test 3: Nested StringBuilders (the main test)
    {
        std::string_view result = createQualifiedName("MyNamespace", "MyFunction");
        std::cout << "Test 3 result: '" << result << "'\n";
        std::cout << "Expected: 'MyNamespace::mangled_MyFunction_v1'\n";
        assert(result == "MyNamespace::mangled_MyFunction_v1");
        std::cout << "Test 3 passed: Nested StringBuilders work\n";
    }
    
    // Test 4: Multiple levels of nesting
    {
        StringBuilder outer;
        outer.append("outer[");
        
        {
            StringBuilder middle;
            middle.append("middle[");
            
            {
                StringBuilder inner;
                inner.append("inner");
                std::string_view inner_result = inner.commit();
                middle.append(inner_result);
            }
            
            middle.append("]");
            std::string_view middle_result = middle.commit();
            outer.append(middle_result);
        }
        
        outer.append("]");
        std::string_view result = outer.commit();
        assert(result == "outer[middle[inner]]");
        std::cout << "Test 4 passed: Multiple levels of nesting work\n";
    }
    
    // Test 5: Interleaved append operations
    {
        StringBuilder sb1;
        sb1.append("first");
        
        StringBuilder sb2;
        sb2.append("second");
        std::string_view r2 = sb2.commit();
        
        sb1.append("_");
        sb1.append(r2);
        std::string_view r1 = sb1.commit();
        
        assert(r1 == "first_second");
        assert(r2 == "second");
        std::cout << "Test 5 passed: Interleaved operations work\n";
    }
    
    std::cout << "\nAll tests passed!\n";
    
    // Test 6: Parallel usage detection (should fail with assertion)
    // Uncomment to test - this should trigger the parallel usage assertion
    /*
    std::cout << "\nTest 6: Testing parallel usage detection (should assert)...\n";
    {
        StringBuilder sb1;
        sb1.append("first");
        
        StringBuilder sb2;
        sb2.append("second");  // This should trigger assertion for parallel usage
        
        std::string_view r1 = sb1.commit();
        std::string_view r2 = sb2.commit();
    }
    */
    
    return 0;
}
