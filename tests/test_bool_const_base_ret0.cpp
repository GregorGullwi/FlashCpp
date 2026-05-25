// Regression test: std::bool_constant<false> and std::bool_constant<true> as base classes
// Previously failed with "Base class 'std::bool_constant' not found" because
// parse_qualified_identifier_with_templates consumed the <> but then only tried
// try_instantiate_class_template which skips alias templates.
#include <type_traits>

struct AlwaysFalse : public std::bool_constant<false> {};
struct AlwaysTrue : public std::bool_constant<true> {};

// Also test the alias chain: bool_constant -> __bool_constant -> integral_constant
struct MyFalse : public std::false_type {};
struct MyTrue : public std::true_type {};

int main() {
    static_assert(AlwaysFalse::value == false);
    static_assert(AlwaysTrue::value == true);
    static_assert(MyFalse::value == false);
    static_assert(MyTrue::value == true);
    return 0;
}
