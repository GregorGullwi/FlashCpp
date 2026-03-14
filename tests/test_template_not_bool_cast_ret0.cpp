// Test: Template __not_ pattern with bool() functional cast in deferred base class
// This tests the ExpressionSubstitutor handling of StaticCastNode
// (functional casts like bool(x) are parsed as StaticCastNode)
template<bool V>
struct integral_constant {
    static constexpr bool value = V;
    using type = integral_constant;
};
using true_type = integral_constant<true>;
using false_type = integral_constant<false>;

template<bool C>
using __bool_constant = integral_constant<C>;

template<typename P>
struct my_not : __bool_constant<!bool(P::value)> {};

int main() {
    // my_not<false_type> should be true_type (negation of false)
    // my_not<true_type> should be false_type (negation of true)
    if (my_not<false_type>::value != true) return 1;
    if (my_not<true_type>::value != false) return 2;
    return 0;
}
