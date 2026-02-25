// Test: escaped quotes inside string literals in static_assert
// Regression test for lexer not skipping escaped characters in consume_string_literal()
static_assert(true, "string with \"escaped\" quotes");
static_assert(true, "multi " "part " "with \"escapes\" inside" " works");
static_assert(true, "backslash-n: \n and backslash-t: \t");

int main() { return 0; }
