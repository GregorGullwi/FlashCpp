// Regression test: __hash_enum-style NTTP default evaluation with is_enum-based default.
// template<typename Tp, bool = is_enum<Tp>::value> struct hash_enum { ... };
// When hash_enum<Color> is used (default NTTP), the NTTP must evaluate __is_enum(Color)
// = true and select the partial specialization hash_enum<Tp, true>.
// Previously failed with: "Could not evaluate non-type template default for parameter 1
// of 'hash_enum'" (Issue 6 in KNOWN_ISSUES.md, now FIXED).
// This test does NOT call operator() on the hash object; operator() call through
// default-NTTP + partial-specialization inheritance has a separate known issue.
template<typename Tp>
struct is_enum_trait {
    static constexpr bool value = __is_enum(Tp);
};

// Primary template: selected when is_enum<Tp>::value is false
template<typename Tp, bool = is_enum_trait<Tp>::value>
struct hash_enum {
    static constexpr int kind = 0;
};

// Partial specialization: selected when is_enum<Tp>::value is true
template<typename Tp>
struct hash_enum<Tp, true> {
    static constexpr int kind = 1;
};

// Wrapper inheriting from hash_enum (like std::hash)
template<typename Tp>
struct my_hash : hash_enum<Tp> {};

enum class Color { Red = 1 };
struct NotEnum { int x; };

int main() {
    // Color is an enum: hash_enum<Color> should select kind=1 specialization
    static_assert(my_hash<Color>::kind == 1, "enum should select specialization");
    // NotEnum is not an enum: hash_enum<NotEnum> should select primary with kind=0
    static_assert(my_hash<NotEnum>::kind == 0, "non-enum should select primary");
    return (my_hash<Color>::kind - 1) + my_hash<NotEnum>::kind;  // (1-1) + 0 = 0
}
