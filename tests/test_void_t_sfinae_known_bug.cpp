// Test demonstrating a KNOWN BUG with void_t SFINAE pattern
// 
// BUG DESCRIPTION:
// When using void_t for SFINAE-based type detection, FlashCpp incorrectly
// matches partial specializations even when the dependent type doesn't exist.
//
// For this pattern:
//   template<typename T, typename = void>
//   struct has_type : false_type {};
//   
//   template<typename T>
//   struct has_type<T, void_t<typename T::type>> : true_type {};
//
// When instantiating has_type<WithoutType> (where WithoutType has no 'type' member):
// - Expected (clang/gcc): primary template (false_type) is selected
// - Actual (FlashCpp): specialization (true_type) is incorrectly selected
//
// The SFINAE failure when evaluating "typename WithoutType::type" should cause
// the specialization to be rejected, but FlashCpp doesn't properly evaluate
// dependent type expressions during template pattern matching.
//
// ROOT CAUSE:
// Template pattern matching in TemplateRegistry.h doesn't attempt to resolve
// dependent types when matching partial specializations. The pattern 
// <T, void_t<typename T::type>> is stored with void as the second argument
// (since void_t<X> = void for any X), and during matching, only the result
// type 'void' is compared against the concrete argument 'void', without
// verifying that the dependent expression typename T::type is valid.
//
// TODO: Fix this by:
// 1. Recording dependent expressions when registering partial specialization patterns
// 2. During pattern matching, after binding template parameters, attempt to resolve
//    all dependent expressions in SFINAE context
// 3. If resolution fails, reject the pattern match

// This file compiles but has no main() - it's documentation/reference code only.
// DO NOT include this in normal test runs.

// To verify the bug manually, use test_void_t_detection_ret42.cpp and
// modify it to test the WithoutType case:
// 1. Change main() to test has_type_member<WithoutType> instead of WithType
// 2. Compile with FlashCpp and clang
// 3. FlashCpp will return wrong result (42 instead of 0), clang will be correct

template<typename...>
using void_t = void;

struct false_type {
    bool get_value() const { return false; }
};

struct true_type {
    bool get_value() const { return true; }
};

// Primary template
template<typename T, typename = void>
struct has_type_member : false_type {};

// Specialization - should only match when T::type exists
template<typename T>
struct has_type_member<T, void_t<typename T::type>> : true_type {};

// Type WITHOUT 'type' member
struct WithoutType {
    int value;
};

// Type WITH 'type' member
struct WithType {
    using type = int;
};

// This file doesn't have main() so it won't be run as a normal test
void document_bug() {
    // Test case 1: Type with 'type' member - should be true
    // Both FlashCpp and clang/gcc return true here (correct)
    has_type_member<WithType> test_with;
    bool with_result = test_with.get_value();
    // with_result should be true - WORKS
    
    // Test case 2: Type WITHOUT 'type' member - should be false  
    // FlashCpp returns true (BUG), clang/gcc return false (correct)
    has_type_member<WithoutType> test_without;
    bool without_result = test_without.get_value();
    // without_result should be false - BROKEN IN FLASHCPP
}
