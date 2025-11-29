// Test compiler intrinsic type traits
// These are compiler built-ins that can be used by <type_traits>

// Test struct/class for __is_class
struct MyStruct {
    int value;
};

class MyClass {
public:
    int value;
};

// Test enum for __is_enum
enum Color { Red, Green, Blue };
enum class ScopedColor { Red, Green, Blue };

// Test function returning type trait results
int test_is_void() {
    // __is_void(void) should be true
    return __is_void(void) ? 1 : 0;
}

int test_is_integral() {
    int result = 0;
    // These should all be true
    if (__is_integral(int)) result += 1;
    if (__is_integral(char)) result += 2;
    if (__is_integral(bool)) result += 4;
    if (__is_integral(long)) result += 8;
    if (__is_integral(unsigned int)) result += 16;
    
    // These should be false
    if (!__is_integral(float)) result += 32;
    if (!__is_integral(double)) result += 64;
    
    return result;  // Expected: 127 (all tests pass)
}

int test_is_floating_point() {
    int result = 0;
    // These should be true
    if (__is_floating_point(float)) result += 1;
    if (__is_floating_point(double)) result += 2;
    
    // These should be false
    if (!__is_floating_point(int)) result += 4;
    if (!__is_floating_point(char)) result += 8;
    
    return result;  // Expected: 15
}

int test_is_pointer() {
    int result = 0;
    // These should be true
    if (__is_pointer(int*)) result += 1;
    if (__is_pointer(void*)) result += 2;
    if (__is_pointer(MyStruct*)) result += 4;
    
    // These should be false
    if (!__is_pointer(int)) result += 8;
    if (!__is_pointer(MyStruct)) result += 16;
    
    return result;  // Expected: 31
}

int test_is_reference() {
    int result = 0;
    // Test lvalue references
    if (__is_lvalue_reference(int&)) result += 1;
    
    // Test rvalue references  
    if (__is_rvalue_reference(int&&)) result += 2;
    
    // These should be false for lvalue
    if (!__is_lvalue_reference(int)) result += 4;
    if (!__is_lvalue_reference(int*)) result += 8;
    
    return result;  // Expected: 15
}

int test_is_class() {
    int result = 0;
    // These should be true
    if (__is_class(MyStruct)) result += 1;
    if (__is_class(MyClass)) result += 2;
    
    // These should be false
    if (!__is_class(int)) result += 4;
    if (!__is_class(float)) result += 8;
    if (!__is_class(Color)) result += 16;
    
    return result;  // Expected: 31
}

int test_is_enum() {
    int result = 0;
    // These should be true
    if (__is_enum(Color)) result += 1;
    if (__is_enum(ScopedColor)) result += 2;
    
    // These should be false
    if (!__is_enum(int)) result += 4;
    if (!__is_enum(MyStruct)) result += 8;
    
    return result;  // Expected: 15
}

// Test __is_array (requires array type syntax to work)
// Note: Arrays in type traits like int[10] may not be fully supported yet
int test_is_array() {
    int result = 0;
    // Test that non-array types return false
    if (!__is_array(int)) result += 1;
    if (!__is_array(int*)) result += 2;
    if (!__is_array(MyStruct)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_member_object_pointer
int test_is_member_object_pointer() {
    int result = 0;
    // Test that non-member-object-pointer types return false
    if (!__is_member_object_pointer(int)) result += 1;
    if (!__is_member_object_pointer(int*)) result += 2;
    if (!__is_member_object_pointer(MyStruct)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_member_function_pointer
int test_is_member_function_pointer() {
    int result = 0;
    // Test that non-member-function-pointer types return false
    if (!__is_member_function_pointer(int)) result += 1;
    if (!__is_member_function_pointer(int*)) result += 2;
    if (!__is_member_function_pointer(MyStruct)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_union (unions are not currently supported by the compiler)
int test_is_union() {
    int result = 0;
    // Test that non-union types return false
    if (!__is_union(int)) result += 1;
    if (!__is_union(MyStruct)) result += 2;
    if (!__is_union(MyClass)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_nullptr
int test_is_nullptr() {
    int result = 0;
    // Test that non-nullptr types return false
    if (!__is_nullptr(int)) result += 1;
    if (!__is_nullptr(int*)) result += 2;
    if (!__is_nullptr(void*)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_function
int test_is_function() {
    int result = 0;
    // Test that non-function types return false
    if (!__is_function(int)) result += 1;
    if (!__is_function(int*)) result += 2;
    if (!__is_function(MyStruct)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_base_of(Base, Derived)
// Note: Requires proper base class information
int test_is_base_of() {
    int result = 0;
    // Test that non-inheritance returns false
    if (!__is_base_of(MyStruct, MyClass)) result += 1;
    // Same type should return true
    if (__is_base_of(MyStruct, MyStruct)) result += 2;
    if (__is_base_of(MyClass, MyClass)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_polymorphic (has virtual functions)
int test_is_polymorphic() {
    int result = 0;
    // Non-polymorphic classes
    if (!__is_polymorphic(MyStruct)) result += 1;
    if (!__is_polymorphic(MyClass)) result += 2;
    // Scalars are not polymorphic
    if (!__is_polymorphic(int)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_abstract (has pure virtual functions)
int test_is_abstract() {
    int result = 0;
    // Non-abstract classes
    if (!__is_abstract(MyStruct)) result += 1;
    if (!__is_abstract(MyClass)) result += 2;
    // Scalars are not abstract
    if (!__is_abstract(int)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_final
int test_is_final() {
    int result = 0;
    // Non-final classes
    if (!__is_final(MyStruct)) result += 1;
    if (!__is_final(MyClass)) result += 2;
    // Scalars are not final (n/a)
    if (!__is_final(int)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_empty (no non-static data members)
int test_is_empty() {
    int result = 0;
    // MyStruct and MyClass have members, so not empty
    if (!__is_empty(MyStruct)) result += 1;
    if (!__is_empty(MyClass)) result += 2;
    // Scalars are not empty (not applicable)
    if (!__is_empty(int)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_standard_layout
int test_is_standard_layout() {
    int result = 0;
    // Scalars are standard layout
    if (__is_standard_layout(int)) result += 1;
    if (__is_standard_layout(double)) result += 2;
    // Simple structs are standard layout
    if (__is_standard_layout(MyStruct)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_trivially_copyable
int test_is_trivially_copyable() {
    int result = 0;
    // Scalars are trivially copyable
    if (__is_trivially_copyable(int)) result += 1;
    if (__is_trivially_copyable(double)) result += 2;
    // Pointers are trivially copyable
    if (__is_trivially_copyable(int*)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_trivial
int test_is_trivial() {
    int result = 0;
    // Scalars are trivial
    if (__is_trivial(int)) result += 1;
    if (__is_trivial(double)) result += 2;
    // Pointers are trivial
    if (__is_trivial(int*)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_pod (plain old data)
int test_is_pod() {
    int result = 0;
    // Scalars are POD
    if (__is_pod(int)) result += 1;
    if (__is_pod(double)) result += 2;
    // Pointers are POD
    if (__is_pod(int*)) result += 4;
    
    return result;  // Expected: 7
}

// Test __has_unique_object_representations
int test_has_unique_object_representations() {
    int result = 0;
    // Integer types have unique object representations
    if (__has_unique_object_representations(int)) result += 1;
    if (__has_unique_object_representations(char)) result += 2;
    if (__has_unique_object_representations(long)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_constructible(T, Args...)
int test_is_constructible() {
    int result = 0;
    // Scalars are default constructible
    if (__is_constructible(int)) result += 1;
    if (__is_constructible(double)) result += 2;
    // Scalars are copy constructible
    if (__is_constructible(int, int)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_assignable(To, From)
int test_is_assignable() {
    int result = 0;
    // Scalars are assignable
    if (__is_assignable(int, int)) result += 1;
    if (__is_assignable(double, double)) result += 2;
    if (__is_assignable(int, double)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_destructible(T)
int test_is_destructible() {
    int result = 0;
    // Scalars are destructible
    if (__is_destructible(int)) result += 1;
    if (__is_destructible(double)) result += 2;
    // Trivially destructible scalars
    if (__is_trivially_destructible(int)) result += 4;
    
    return result;  // Expected: 7
}

// Test __is_constant_evaluated()
int test_is_constant_evaluated() {
    int result = 0;
    // At runtime, __is_constant_evaluated() returns false
    if (!__is_constant_evaluated()) result += 1;
    // But we can still test the intrinsic parses
    result += 2;  // Always add this to show the test ran
    
    return result;  // Expected: 3
}

// Test __is_layout_compatible(T, U)
int test_is_layout_compatible() {
    int result = 0;
    // Same types are layout compatible
    if (__is_layout_compatible(int, int)) result += 1;
    if (__is_layout_compatible(double, double)) result += 2;
    
    return result;  // Expected: 3
}

int main() {
    int total = 0;
    
    total += test_is_void();                   // +1
    total += test_is_integral();               // +127
    total += test_is_floating_point();         // +15
    total += test_is_pointer();                // +31
    total += test_is_reference();              // +15
    total += test_is_class();                  // +31
    total += test_is_enum();                   // +15
    total += test_is_array();                  // +7
    total += test_is_member_object_pointer();  // +7
    total += test_is_member_function_pointer(); // +7
    total += test_is_union();                  // +7
    total += test_is_nullptr();                // +7
    total += test_is_function();               // +7
    total += test_is_base_of();                // +7
    total += test_is_polymorphic();            // +7
    total += test_is_abstract();               // +7
    total += test_is_final();                  // +7
    total += test_is_empty();                  // +7
    total += test_is_standard_layout();        // +7
    total += test_is_trivially_copyable();     // +7
    total += test_is_trivial();                // +7
    total += test_is_pod();                    // +7
    total += test_has_unique_object_representations(); // +7
    total += test_is_constructible();          // +7
    total += test_is_assignable();             // +7
    total += test_is_destructible();           // +7
    total += test_is_constant_evaluated();     // +3
    total += test_is_layout_compatible();      // +3
    
    return total;  // Expected: 381
}
