// A function declarator wrapped around a member-function-pointer alias makes
// the member pointer the function's return type, not a nested function.

struct S {
	void method() {}
};

using Method = void (S::*)();
using MethodWithInt = void (S::*)(int);

Method (*select)() = nullptr;
MethodWithInt (*select_with_int)(double) = nullptr;

static_assert(__is_same(Method (*)(), decltype(select)));
static_assert(__is_same(MethodWithInt (*)(double), decltype(select_with_int)));

int main() {
	Method (*select_copy)() = select;
	if (select_copy != nullptr) {
		return 1;
	}
	if (select_with_int != nullptr) {
		return 2;
	}
	if (sizeof(select) != sizeof(void*)) {
		return 3;
	}
	if (sizeof(MethodWithInt) != sizeof(void*)) {
		return 4;
	}
	return 0;
}
