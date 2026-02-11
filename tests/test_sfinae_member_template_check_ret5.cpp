template<typename T>
struct has_foo {
template<typename U>
static auto check(U* u) -> decltype(u->foo(), void(), true) { return true; }

template<typename U>
static auto check(...) -> bool { return false; }
};

struct WithFoo { void foo() {} };
struct WithoutFoo {};

int main() {
bool a = has_foo<WithFoo>::check<WithFoo>(nullptr);
bool b = has_foo<WithoutFoo>::check<WithoutFoo>(nullptr);

return (a && !b) ? 5 : 0;
}
