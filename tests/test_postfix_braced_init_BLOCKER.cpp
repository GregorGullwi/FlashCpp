// FIXED: This test demonstrates the now-working feature for braced initialization in class body
// Feature: Function call followed by member access in constant expressions
// Impact: Unblocks <type_traits>, <exception>, <utility>, <tuple>, <span>, <array>, and many more

template<typename T>
struct __type_identity {
    using type = T;
};

struct true_type {
    static constexpr bool value = true;
};

// This function template from <type_traits> expects braced initialization as argument
template <typename _Tp>
constexpr true_type check(__type_identity<_Tp>) {
    return {};
}

// This works fine in function context:
int test_in_function() {
    auto result = check(__type_identity<int>{});
    return result.value ? 0 : 1;
}

// But this now works in class body context:
struct TestStruct {
    // This now works!
    static constexpr bool value = check(__type_identity<int>{}).value;
    
    // This also works!
    static_assert(check(__type_identity<int>{}).value, "test");
};

int main() {
    return test_in_function();
}

// The issue has been fixed:
// - Function body: __type_identity<int>{} works ✅
// - Class/struct body: __type_identity<int>{} now works ✅
// 
// The fix adds support for evaluating function calls followed by member access
// in constant expressions. This pattern is used extensively in standard library
// headers like <type_traits>:
//   template<typename _Tp>
//   struct is_copy_assignable {
//       static_assert(std::__is_complete_or_unbounded(__type_identity<_Tp>{}), "...");
//   };
//
// Implementation: Added evaluate_function_call_member_access() to ConstExprEvaluator.h
// which determines the return type of a function and accesses static constexpr members.
// See tests/std/README_STANDARD_HEADERS.md for more details
