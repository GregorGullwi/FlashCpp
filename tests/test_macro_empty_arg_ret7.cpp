// Test: preprocessor handling of empty trailing macro arguments
// and nested macro expansion with token pasting (like glibc math headers)

#define CONCAT_INNER(x, y) x ## y
#define CONCAT(x, y) CONCAT_INNER(x, y)
#define PRECNAME(name, r) CONCAT(name, r)
#define DECL_IMPL(type, function, suffix, args) \
  type PRECNAME(function, suffix) args
#define DECL(type, function, suffix, args) \
  DECL_IMPL(type, function, suffix, args)
#define DECL_ALIAS(type, function, suffix, args, alias) \
  DECL(type, function, suffix, args)

// Empty suffix via macro chain (like glibc __MATHDECL_ALIAS pattern)
DECL_ALIAS(int, get_value,, (), fpclassify) { return 7; }

// Non-empty suffix via macro chain
DECL_ALIAS(int, get_value_f, _f, (), fpclassify) { return 42; }

int main() {
    // get_value() should exist from empty-suffix expansion
    // get_value_f() should exist from non-empty-suffix expansion
    return get_value();
}
