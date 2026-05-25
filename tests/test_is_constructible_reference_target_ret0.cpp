struct MyStruct { int x; };
enum class E1 { A };
enum class E2 { B };

int main() {
// const scalar lvalue ref from rvalue
static_assert(__is_constructible(const int&, int));
// const scalar lvalue ref from const lvalue ref
static_assert(__is_constructible(const int&, const int&));
// const scalar lvalue ref from float (implicit conversion temp)
static_assert(__is_constructible(const int&, float));
// scalar lvalue ref from lvalue ref of same type
static_assert(__is_constructible(int&, int&));
// rvalue ref from rvalue of same type
static_assert(__is_constructible(int&&, int));
static_assert(__is_constructible(int&&, int&&));
// const struct lvalue ref from struct rvalue
static_assert(__is_constructible(const MyStruct&, MyStruct));
// struct lvalue ref from struct lvalue ref
static_assert(__is_constructible(MyStruct&, MyStruct&));
// pointer target requires pointer arg
static_assert(__is_constructible(int*, int*));
static_assert(!__is_constructible(int*, int));
// reference to pointer should reject incompatible pointee conversion
static_assert(!__is_constructible(const int*&, float*&));
// non-const scalar lvalue ref from different type must fail
static_assert(!__is_constructible(int&, float&));
// rvalue ref from lvalue ref must fail
static_assert(!__is_constructible(int&&, int&));
// distinct enum types are not implicitly reference-bindable
static_assert(!__is_constructible(const E1&, E2));
return 0;
}
