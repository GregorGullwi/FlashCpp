// Direct type-trait operands may spell a member pointer wherever a type-id is
// accepted. The answers must describe the declarator: `int S::*` names an
// object member pointer, `int (S::*)()` names a function member pointer, and an
// `int*` pointee keeps the member pointer distinct from a plain `int`.
struct S {
	int value;
	int read() const;
	int* pointer;
};

typedef int S::* MemberInt;
using MemberFunction = int (S::*)() const;

constexpr bool plain_object_member = __is_member_object_pointer(int S::*);
constexpr bool plain_function_member = __is_member_function_pointer(int S::*);
constexpr bool callable_object_member = __is_member_object_pointer(int (S::*)());
constexpr bool callable_function_member = __is_member_function_pointer(int (S::*)());
constexpr bool alias_object_member = __is_member_object_pointer(MemberInt);
constexpr bool alias_function_member = __is_member_function_pointer(MemberFunction);
constexpr bool pointer_pointee_object_member = __is_member_object_pointer(int* S::*);
constexpr bool same_member_pointer = __is_same(int S::*, int S::*);
constexpr bool member_pointer_differs_from_int = __is_same(int S::*, int);
constexpr bool member_pointer_differs_from_second_operand = __is_same(int, int S::*);
constexpr bool pointer_pointee_same = __is_same(int S::*, int* S::*);

static_assert(plain_object_member, "int S::* is an object member pointer");
static_assert(!plain_function_member, "int S::* is not a function member pointer");
static_assert(!callable_object_member, "int (S::*)() is not an object member pointer");
static_assert(callable_function_member, "int (S::*)() is a function member pointer");
static_assert(alias_object_member, "the typedef spelling is an object member pointer");
static_assert(alias_function_member, "the alias spelling is a function member pointer");
static_assert(pointer_pointee_object_member, "int* S::* is an object member pointer");
static_assert(same_member_pointer, "the same member pointer type is identical");
static_assert(!member_pointer_differs_from_int, "a member pointer differs from int");
static_assert(!member_pointer_differs_from_second_operand, "a member pointer differs from int in either operand");
static_assert(!pointer_pointee_same, "an int* pointee makes the member pointer type distinct");

int main() {
	if (!plain_object_member) {
		return 1;
	}
	if (plain_function_member) {
		return 2;
	}
	if (callable_object_member) {
		return 3;
	}
	if (!callable_function_member) {
		return 4;
	}
	if (!alias_object_member) {
		return 5;
	}
	if (!alias_function_member) {
		return 6;
	}
	if (!pointer_pointee_object_member) {
		return 7;
	}
	if (!same_member_pointer) {
		return 8;
	}
	if (member_pointer_differs_from_int) {
		return 9;
	}
	if (member_pointer_differs_from_second_operand) {
		return 10;
	}
	if (pointer_pointee_same) {
		return 11;
	}
	return 0;
}
