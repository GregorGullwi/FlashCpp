// Test case for typename T::type{} as function argument
// This pattern NOW WORKS - the deferred base class template argument is correctly
// evaluated at template instantiation time, and the constexpr function returns true.
template<bool B>
struct bool_constant {
    static constexpr bool value = B;
};

// A struct with a nested type
struct MyResult {
    using __invoke_type = int;
};

// A function that takes a type and returns bool
template<typename Result>
constexpr bool call_is_nt(typename Result::__invoke_type) {
    return true;
}

// This pattern now works correctly!
template<typename Result>
struct test : bool_constant<call_is_nt<Result>(typename Result::__invoke_type{})>
{ };

int main() {
    // test<MyResult>::value is true (from the constexpr function call), so returns 0
    return test<MyResult>::value ? 0 : 1;
}
