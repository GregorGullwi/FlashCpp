// Regression test: concrete typedef alias member type access
//
// Bug: `using X = S<char>; using T = X::type;` incorrectly produced an
// incomplete/zero-sized type instead of resolving to `char`.
//
// Root cause: in parse_type_specifier(), when the full name "X::type" was not
// found directly in the type map (because the member is registered under the
// instantiated name "S_char_::type"), there was no fallback that split the
// name and followed the typedef alias chain via lookup_inherited_type_alias.
//
// Fix: after the direct lookup fails for a qualified name containing "::", the
// parser now splits it into (base, member), resolves the base through the type
// alias registry, and uses lookup_inherited_type_alias to find the member.

template<typename T>
struct Wrapper {
    using type = T;
    using value_type = T;
};

template<typename T>
struct Outer {
    using type = Wrapper<T>;
};

// Pattern 1: basic member access through typedef
using IntWrapper = Wrapper<int>;
using ViaTypedef = IntWrapper::type;
static_assert(sizeof(ViaTypedef) == sizeof(int), "IntWrapper::type should be int");

// Pattern 2: chained typedef member access
using CharWrapper = Wrapper<char>;
using CharViaTypedef = CharWrapper::type;
static_assert(sizeof(CharViaTypedef) == sizeof(char), "CharWrapper::type should be char");

// Pattern 3: second member name through typedef
using CharValueType = CharWrapper::value_type;
static_assert(sizeof(CharValueType) == sizeof(char), "CharWrapper::value_type should be char");

// Pattern 4: nested typedef chain: Y = X = Wrapper<int>, then Y::type
using X = IntWrapper;  // X is an alias for an alias for Wrapper<int>
using NestedChain = X::type;
static_assert(sizeof(NestedChain) == sizeof(int), "X::type via nested alias should be int");

// Pattern 5: function using the resolved types
ViaTypedef make_int()       { return 42; }
CharViaTypedef make_char()  { return 'A'; }
CharValueType make_val()    { return 'B'; }
NestedChain make_nested()   { return 99; }

int main() {
    ViaTypedef    a = make_int();
    CharViaTypedef b = make_char();
    CharValueType  c = make_val();
    NestedChain    d = make_nested();

    if (a != 42)   return 1;
    if (b != 'A')  return 2;
    if (c != 'B')  return 3;
    if (d != 99)   return 4;
    return 0;
}
