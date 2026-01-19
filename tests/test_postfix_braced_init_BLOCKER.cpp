// BLOCKER: This test documents the missing feature that blocks most standard headers
// Issue: Braced initialization Type<Args>{} fails when parsing in class/struct body context
// Impact: Blocks <type_traits>, <exception>, <utility>, <tuple>, <span>, <array>, and many more

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

// But FAILS in class body context:
struct TestStruct {
    // ERROR: Expected ';' after static member declaration
    // static constexpr bool value = check(__type_identity<int>{}).value;
    
    // ERROR: Expected ')' after static_assert
    // static_assert(check(__type_identity<int>{}).value, "test");
};

int main() {
    return test_in_function();
}

// The issue is context-specific:
// - Function body: __type_identity<int>{} works
// - Class/struct body: __type_identity<int>{} fails to parse
// 
// This is why type_traits fails - it uses this pattern in static_assert inside class templates:
//   template<typename _Tp>
//   struct is_copy_assignable {
//       static_assert(std::__is_complete_or_unbounded(__type_identity<_Tp>{}), "...");
//   };
//
// Fix needed: Make braced initialization parsing work in class/struct body context
// See tests/std/README_STANDARD_HEADERS.md for more details
