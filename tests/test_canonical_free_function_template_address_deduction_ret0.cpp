template<class Type>
int invoke(Type value) {
	return sizeof(Type);
}

using IntCallback = int (*)(int);
static_assert(__is_same(decltype(static_cast<IntCallback>(&invoke)), IntCallback));

struct ExactSelection {};
struct FallbackSelection {};

ExactSelection select(IntCallback);
FallbackSelection select(...);
static_assert(__is_same(decltype(select(&invoke)), ExactSelection));

template<class Type>
int address_target(Type) {
	return 1;
}

template<class Type>
int address_target(Type*) {
	return 2;
}

struct PointerSelection {};
struct PointerFallbackSelection {};

PointerSelection select_pointer(int (*)(int*));
PointerFallbackSelection select_pointer(...);
static_assert(__is_same(decltype(select_pointer(&address_target)), PointerSelection));

int direct_address_target(int*) {
	return 3;
}

int direct_address_target(char*) {
	return 4;
}

struct DirectSelection {};
struct DirectFallbackSelection {};

DirectSelection select_direct(int (*)(int*));
DirectFallbackSelection select_direct(...);
static_assert(__is_same(decltype(select_direct(&direct_address_target)), DirectSelection));

int main() {
	return 0;
}
