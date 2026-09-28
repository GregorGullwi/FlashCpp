#if defined(_MSC_VER)
template <class Type>
struct Base {
	Type run();
};

template <class Type>
struct Derived : Base<Type> {};

using IntDerived = Derived<int>;
using CharDerived = Derived<char>;

struct IntSelection {
	char marker[3];
};

struct CharSelection {
	char marker[5];
};

struct FallbackSelection {
	char marker[7];
};

IntSelection choose(int (IntDerived::*)());
CharSelection choose(char (CharDerived::*)());
FallbackSelection choose(...);

template <class Type>
using BaseMemberChoice = decltype(choose(&Base<Type>::run));

static_assert(sizeof(BaseMemberChoice<int>) == sizeof(IntSelection));
static_assert(sizeof(BaseMemberChoice<char>) == sizeof(CharSelection));

int main() {
	return 0;
}
#else
// The Itanium mangler does not yet support member-function-pointer parameter
// types. Platform-independent owner planning is covered by CanonicalTypeTests.
int main() {
	return 0;
}
#endif
