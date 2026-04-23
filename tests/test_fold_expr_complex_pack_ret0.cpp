// Test: fold expressions with complex pack expressions (non-bare-identifier pack).
// Exercises the Phase 6 parser fix for:
//   Pattern 1: (... op cast-expr)   — unary left fold where pack is a subexpression
//   Pattern 3: (init op ... op cast-expr) — binary left fold with complex pack
//
// Prior to the fix only bare identifiers were accepted as the pack operand in
// these two patterns; parenthesised or compound expressions were silently skipped
// and fell through to a failed template instantiation.

// Unary left fold: (... op (pack_elem op literal))
template<typename... Ts>
bool all_positive(Ts... args) {
return (... && (args > 0));
}

// Unary left fold: (... op (pack_elem relop literal)) with different operator
template<typename... Ts>
bool none_negative(Ts... args) {
return (... && (args >= 0));
}

// Binary left fold: (init op ... op (pack_elem op literal))
template<typename... Ts>
int sum_doubled(Ts... args) {
return (0 + ... + (args * 2));
}

// Binary left fold with ternary in pack expression
template<typename... Ts>
int count_positive(Ts... args) {
return (0 + ... + (args > 0 ? 1 : 0));
}

int main() {
// all_positive
if (!all_positive(1, 2, 3))    return 1;
if (all_positive(0, 1, 2))     return 2;   // 0 > 0 is false
if (all_positive(1, -1, 3))    return 3;

// none_negative
if (!none_negative(0, 1, 2))   return 4;
if (none_negative(-1, 0, 1))   return 5;

// sum_doubled: 0 + 2 + 4 + 6 = 12
int sd = sum_doubled(1, 2, 3);
if (sd != 12)                  return 6;

// count_positive: count of args > 0 in {-1, 0, 2, 3}
int cp = count_positive(-1, 0, 2, 3);
if (cp != 2)                   return 7;

return 0;
}
