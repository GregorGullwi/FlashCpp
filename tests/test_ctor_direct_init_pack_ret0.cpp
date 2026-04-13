// Regression test: constructor direct-init with pack expansion Foo(args...)
// Previously the declaration path (Pair p(args...)) failed to expand the pack
// because parse_direct_initialization() didn't handle the ... token after
// each argument.  This left the function body uninstantiated (link error).

// Simple 2-arg struct
struct Pair {
int x;
int y;
Pair(int a, int b) : x(a), y(b) {}
int sum() const { return x + y; }
};

// 3-arg struct
struct Triple {
int a;
int b;
int c;
Triple(int x, int y, int z) : a(x), b(y), c(z) {}
int sum() const { return a + b + c; }
};

// Variadic template: constructs Pair from the pack
template<typename... Args>
int pair_sum(Args... args) {
Pair p(args...);  // simple identifier pack in constructor direct-init
return p.sum();
}

// Variadic template: constructs Triple from the pack
template<typename... Args>
int triple_sum(Args... args) {
Triple t(args...);
return t.sum();
}

// Mixed: one explicit + pack
template<typename First, typename... Rest>
int pair_of_first_and_count(First first, Rest... rest) {
Pair p(first, static_cast<int>(sizeof...(rest)));
return p.x + p.y;
}

int main() {
// 2-element pack expansion in constructor
if (pair_sum(10, 20) != 30) return 1;
if (pair_sum(100, 200) != 300) return 2;

// 3-element pack expansion in constructor
if (triple_sum(1, 2, 3) != 6) return 3;
if (triple_sum(10, 20, 30) != 60) return 4;

// Pack size used in constructor arg
if (pair_of_first_and_count(42, 1, 2, 3) != 45) return 5;

return 0;
}
