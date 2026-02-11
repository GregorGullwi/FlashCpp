// Test: SFINAE member existence check
// Verifies that SFINAE correctly detects member function presence/absence
struct HasFoo { void foo() {} };
struct NoFoo {};

template<typename U>
auto check(U* u) -> decltype(u->foo(), true) { return true; }

template<typename U>
auto check(...) -> bool { return false; }

int main() {
bool has = check<HasFoo>(nullptr);
bool lacks = check<NoFoo>(nullptr);
return (has && !lacks) ? 5 : 0;
}
