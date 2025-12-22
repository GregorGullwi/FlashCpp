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

// Test __is_union (unions are not currently supported by the compiler)
int test_is_union() {
    int result = 0;
    // Test that non-union types return false
    if (!__is_union(int)) result += 1;
    if (!__is_union(MyStruct)) result += 2;
    if (!__is_union(MyClass)) result += 4;
    
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

    total += test_is_class();                  // +31
    total += test_is_enum();                   // +15
    total += test_is_union();                  // +7
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
    total += test_is_layout_compatible();      // +3
    
    return total;  // Expected: 146
}
