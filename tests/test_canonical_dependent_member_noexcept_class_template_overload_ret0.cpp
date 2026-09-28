#if defined(_MSC_VER)
template <class Type>
struct Callable {
	int run() noexcept(sizeof(Type) == sizeof(int)) {
		return 0;
	}
};

using IntCallable = Callable<int>;
using CharCallable = Callable<char>;

struct NoexceptSelection { char marker[1]; };
struct ThrowingSelection { char marker[2]; };

NoexceptSelection choose(int (IntCallable::*)() noexcept);
ThrowingSelection choose(int (IntCallable::*)());

NoexceptSelection choose(int (CharCallable::*)() noexcept);
ThrowingSelection choose(int (CharCallable::*)());

template <class Type>
using SelectedMemberOverload = decltype(choose(&Callable<Type>::run));

static_assert(sizeof(SelectedMemberOverload<int>) == sizeof(NoexceptSelection));
static_assert(sizeof(SelectedMemberOverload<char>) == sizeof(ThrowingSelection));

int main() {
	return 0;
}
#else
// The Itanium mangler does not yet support member-function-pointer parameter
// types. Keep source-level overload checks on MSVC; canonical type-planner
// behavior is covered by the platform-independent unit tests.
int main() {
	return 0;
}
#endif
