#if defined(_MSC_VER)
template <class Type>
struct Base {
	Type run() {
		return Type{};
	}
};

template <class Type>
struct Derived : Base<Type> {};

using IntBase = Base<int>;
using CharBase = Base<char>;
using IntDerived = Derived<int>;
using CharDerived = Derived<char>;

struct IntSelection {
	char marker[3];
};

struct CharSelection {
	char marker[5];
};

IntSelection choose(int (IntDerived::*)());
CharSelection choose(char (CharDerived::*)());

static_assert(sizeof(decltype(choose(&IntBase::run))) == sizeof(IntSelection));
static_assert(sizeof(decltype(choose(&CharBase::run))) == sizeof(CharSelection));

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
