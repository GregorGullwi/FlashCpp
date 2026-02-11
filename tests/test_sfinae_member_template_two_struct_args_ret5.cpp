// Test: SFINAE member template called with two different struct types
// from the same class instantiation. Without type_index in the cache key,
// the second call would get a false cache hit from the first.
template<typename T>
struct checker {
template<typename U>
static auto check(U* u) -> decltype(u->foo(), void(), true) { return true; }

template<typename U>
static auto check(...) -> bool { return false; }
};

struct HasFoo { void foo() {} };
struct NoFoo {};

int main() {
// Both calls use checker<int>::check<X>, so the cache key must
// differentiate HasFoo (type_index=N) from NoFoo (type_index=M)
bool a = checker<int>::check<HasFoo>(nullptr);
bool b = checker<int>::check<NoFoo>(nullptr);
return (a && !b) ? 5 : 0;
}
