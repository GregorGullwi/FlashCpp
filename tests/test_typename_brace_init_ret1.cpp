// Test case for typename T::type{} as function argument
// This pattern NOW PARSES correctly, but runtime evaluation of the deferred
// base class template argument is not yet implemented, so value is 0 (false)
// and the test returns 1.
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

// This pattern now parses correctly!
template<typename Result>
struct test : bool_constant<call_is_nt<Result>(typename Result::__invoke_type{})>
{ };

int main() {
    // Returns 1 because test<MyResult>::value is false (deferred base evaluation not implemented)
    // When fully working, this should return 0
    return test<MyResult>::value ? 0 : 1;
}
